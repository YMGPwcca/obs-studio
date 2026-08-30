#include "runtime.hpp"

#include "events.hpp"
#include "source_event_capture.hpp"
#include "validation.hpp"

#include <callback/calldata.h>
#include <callback/signal.h>
#include <obs-source-media-internal.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace obs_engine {

enum class MediaV2Signal { Play, Pause, Restart, Stop, Next, Previous, Time, Started, Ended };

struct MediaV2Observer;

struct DeferredMediaEventBatch {
	uint64_t order = 0;
	uint64_t handle = 0;
	MediaV2Signal signal = MediaV2Signal::Play;
	uint64_t action_serial = 0;
	bool orphaned = false;
	std::vector<RuntimeV2Event> events;
};

struct MediaActionKey {
	uint64_t handle = 0;
	uint64_t action_serial = 0;

	bool operator==(const MediaActionKey &other) const noexcept
	{
		return handle == other.handle && action_serial == other.action_serial;
	}
};

struct MediaActionKeyHash {
	size_t operator()(const MediaActionKey &key) const noexcept
	{
		const size_t handle_hash = std::hash<uint64_t>{}(key.handle);
		const size_t serial_hash = std::hash<uint64_t>{}(key.action_serial);
		return handle_hash ^ (serial_hash + static_cast<size_t>(0x9e3779b9) + (handle_hash << 6) + (handle_hash >> 2));
	}
};

struct MediaV2State {
	std::mutex mutex;
	std::condition_variable callback_cv;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	RuntimeV2Result *capture = nullptr;
	SourceEventCaptureGate capture_gate;
	std::deque<DeferredMediaEventBatch> deferred;
	size_t deferred_event_count = 0;
	size_t direct_callbacks_inflight = 0;
	uint64_t next_batch_order = 1;
	bool deferred_overflow = false;
	bool accepting = false;
	std::unordered_map<uint64_t, std::shared_ptr<MediaV2Observer>> observers;
	std::vector<std::shared_ptr<MediaV2Observer>> retired;
	std::unordered_set<MediaActionKey, MediaActionKeyHash> timed_out;
	bool timed_out_tracking_uncertain = false;
};

struct MediaV2Observer {
	MediaV2State *state = nullptr;
	uint64_t handle = 0;
	obs_weak_source_t *weak = nullptr;
	std::mutex cache_mutex;
	bool state_known = false;
	obs_media_state state_value = OBS_MEDIA_STATE_NONE;
};

namespace {

constexpr size_t kMaxMediaActionBatches = kDefaultEventQueueCapacity;
constexpr auto kMediaActionSettleTimeout = std::chrono::seconds(5);

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "media operation failed";
	return false;
}

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool read_string_field(obs_data_t *data, const char *name, std::string &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	out = value ? value : "";
	obs_data_item_release(&item);
	return true;
}

bool read_integer_field(obs_data_t *data, const char *name, long long &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_NUMBER || obs_data_item_numtype(item) != OBS_DATA_NUM_INT) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_int(item);
	obs_data_item_release(&item);
	return true;
}

bool parse_handle_text(std::string_view text, uint64_t &out)
{
	if (text.empty() || (text.size() > 1 && text.front() == '0'))
		return false;

	uint64_t value = 0;
	const char *begin = text.data();
	const char *end = begin + text.size();
	const auto parsed = std::from_chars(begin, end, value, 10);
	if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0 ||
	    value > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
		return false;

	out = value;
	return true;
}

bool read_handle_field(obs_data_t *data, const char *name, uint64_t &out)
{
	std::string text;
	bool present = false;
	return read_string_field(data, name, text, present) && present && parse_handle_text(text, out);
}

void set_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	const std::string text = std::to_string(handle);
	obs_data_set_string(data, name, text.c_str());
}

const char *media_state_name(obs_media_state state)
{
	constexpr const char *names[] = {"none", "playing", "opening", "buffering",
					 "paused", "stopped", "ended", "error"};
	const auto index = static_cast<size_t>(state);
	return index < std::size(names) ? names[index] : "unknown";
}

ObsDataPtr make_state_data(uint64_t handle, obs_media_state state)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_string(data.get(), "state", media_state_name(state));
	return data;
}

ObsDataPtr make_action_data(uint64_t handle, const char *action, obs_media_state state, bool processed)
{
	ObsDataPtr data = make_state_data(handle, state);
	obs_data_set_string(data.get(), "action", action);
	obs_data_set_bool(data.get(), "processed", processed);
	return data;
}

void append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	result.events.push_back(RuntimeV2Event{name, std::move(data)});
}

bool result_has_source_event(const RuntimeV2Result &result, std::string_view name, uint64_t handle)
{
	return std::any_of(result.events.begin(), result.events.end(), [&](const RuntimeV2Event &event) {
		if (event.name != name || !event.data)
			return false;
		uint64_t event_handle = 0;
		return read_handle_field(event.data.get(), "source", event_handle) && event_handle == handle;
	});
}

bool is_action_signal(MediaV2Signal signal)
{
	constexpr bool action_signals[] = {true, true, true, true, true, true, true, false, false};
	const auto index = static_cast<size_t>(signal);
	return index < std::size(action_signals) && action_signals[index];
}

bool action_requires_resync(MediaV2Signal signal)
{
	constexpr bool resync_signals[] = {false, false, true, false, true, true, true, false, false};
	const auto index = static_cast<size_t>(signal);
	return index < std::size(resync_signals) && resync_signals[index];
}

uint64_t read_action_serial(calldata_t *calldata)
{
	long long serial = 0;
	if (!calldata || !calldata_get_int(calldata, "action_serial", &serial) || serial <= 0)
		return 0;
	return static_cast<uint64_t>(serial);
}

class MediaCallbackScope {
public:
	MediaCallbackScope(MediaV2State &state, uint64_t handle) : state_(state)
	{
		std::lock_guard lock(state_.mutex);
		if (!state_.accepting)
			return;

		accepted_ = true;
		route_ = state_.capture_gate.route_for_current_thread();
		if (route_ == SourceEventCaptureRoute::Direct) {
			++state_.direct_callbacks_inflight;
			counted_direct_ = true;
		} else if (route_ == SourceEventCaptureRoute::Capture && state_.capture &&
			   result_has_source_event(*state_.capture, "source.removed", handle)) {
			suppressed_ = true;
		}
	}

	~MediaCallbackScope()
	{
		if (!counted_direct_)
			return;
		std::lock_guard lock(state_.mutex);
		if (state_.direct_callbacks_inflight > 0)
			--state_.direct_callbacks_inflight;
		state_.callback_cv.notify_all();
	}

	bool accepted() const noexcept
	{
		return accepted_;
	}

	bool suppressed() const noexcept
	{
		return suppressed_;
	}

private:
	MediaV2State &state_;
	SourceEventCaptureRoute route_ = SourceEventCaptureRoute::Direct;
	bool accepted_ = false;
	bool counted_direct_ = false;
	bool suppressed_ = false;
};

struct DeferredMediaEventSnapshot {
	std::deque<DeferredMediaEventBatch> batches;
	EventDispatcher *events = nullptr;
	bool overflow = false;
};

void clear_deferred_media_events(MediaV2State &state)
{
	state.deferred.clear();
	state.deferred_event_count = 0;
	state.deferred_overflow = false;
}

void mark_orphaned_media_batch_locked(MediaV2State &state, DeferredMediaEventBatch &batch)
{
	if (!is_action_signal(batch.signal))
		return;
	if (state.timed_out_tracking_uncertain ||
	    (batch.action_serial != 0 &&
	     state.timed_out.erase(MediaActionKey{batch.handle, batch.action_serial}) != 0))
		batch.orphaned = true;
}

bool should_discard_media_batch(const MediaV2State &state, const DeferredMediaEventBatch &batch, bool end_capture)
{
	return end_capture && state.capture && result_has_source_event(*state.capture, "source.removed", batch.handle);
}

DeferredMediaEventSnapshot take_deferred_media_events(MediaV2State &state, bool wait_for_pre_capture,
							     bool end_capture)
{
	DeferredMediaEventSnapshot snapshot;
	std::unique_lock lock(state.mutex);
	if (wait_for_pre_capture) {
		state.callback_cv.wait(lock, [&] { return !state.accepting || state.direct_callbacks_inflight == 0; });
	}

	if (!state.accepting) {
		clear_deferred_media_events(state);
		if (end_capture) {
			state.capture = nullptr;
			state.capture_gate.end();
		}
		return snapshot;
	}

	snapshot.events = state.events;
	snapshot.overflow = state.deferred_overflow;
	state.deferred_overflow = false;
	while (!state.deferred.empty()) {
		DeferredMediaEventBatch batch = std::move(state.deferred.front());
		state.deferred.pop_front();
		state.deferred_event_count -= batch.events.size();
		mark_orphaned_media_batch_locked(state, batch);
		if (should_discard_media_batch(state, batch, end_capture))
			continue;
		snapshot.batches.push_back(std::move(batch));
	}

	if (end_capture) {
		state.capture = nullptr;
		state.capture_gate.end();
	}
	return snapshot;
}

uint64_t commit_media_resync_revision(RevisionState::MutationGuard &guard)
{
	return guard.can_commit_mutation() ? guard.commit_mutation() : guard.current();
}

void report_media_resync(EventDispatcher *events, uint64_t revision, bool after_queued_events,
				const char *message)
	{
	if (events) {
		if (after_queued_events)
			events->require_resync_after_queued_events(revision);
		else
			events->require_resync_due_to_overflow(revision);
	}
	std::fprintf(stderr, "%s\n", message);
	std::fflush(stderr);
}


bool publish_media_batch(DeferredMediaEventBatch &batch, const DeferredMediaEventSnapshot &snapshot,
				 RevisionState::MutationGuard &guard)
{
	const bool requires_resync = batch.orphaned ||
		(is_action_signal(batch.signal) && action_requires_resync(batch.signal));
	if (requires_resync) {
		report_media_resync(snapshot.events, commit_media_resync_revision(guard), true,
				    "obs-engine: deferred media action completion is not incrementally representable; controller resync required");
		return true;
	}
	if (batch.events.empty())
		return true;
	if (!guard.can_commit_mutation()) {
		report_media_resync(snapshot.events, guard.current(), false,
				    "obs-engine: deferred media events require resync because revision space is exhausted");
		return false;
	}
	const uint64_t revision = guard.commit_mutation();
	if (snapshot.events) {
		for (RuntimeV2Event &event : batch.events)
			snapshot.events->publish(EngineEventKind::State, event.name, revision, event.data.get());
	}
	return true;
}

void publish_deferred_media_snapshot(DeferredMediaEventSnapshot snapshot, RevisionState::MutationGuard &guard)
{
	if (snapshot.overflow)
		report_media_resync(snapshot.events, commit_media_resync_revision(guard), false,
				    "obs-engine: deferred media event queue overflowed; controller resync required");

	for (DeferredMediaEventBatch &batch : snapshot.batches) {
		if (!publish_media_batch(batch, snapshot, guard))
			return;
	}
}

bool consume_timed_out_media_action_locked(MediaV2State &state, uint64_t handle, uint64_t action_serial)
{
	if (state.timed_out_tracking_uncertain)
		return true;
	return action_serial != 0 && state.timed_out.erase(MediaActionKey{handle, action_serial}) != 0;
}

bool queue_deferred_media_events_locked(MediaV2State &state, uint64_t handle, MediaV2Signal signal,
					uint64_t action_serial, bool orphaned, std::vector<RuntimeV2Event> generated)
{
	if (state.deferred_overflow)
		return true;
	if (state.deferred.size() >= kMaxMediaActionBatches ||
	    generated.size() > kDefaultEventQueueCapacity ||
	    state.deferred_event_count > kDefaultEventQueueCapacity - generated.size()) {
		state.deferred.clear();
		state.deferred_event_count = 0;
		state.deferred_overflow = true;
		return true;
	}
	if (state.next_batch_order == 0) {
		state.deferred_overflow = true;
		return true;
	}
	const uint64_t order = state.next_batch_order;
	if (state.next_batch_order == std::numeric_limits<uint64_t>::max())
		state.next_batch_order = 0;
	else
		++state.next_batch_order;
	state.deferred_event_count += generated.size();
	state.deferred.push_back(
		DeferredMediaEventBatch{order, handle, signal, action_serial, orphaned, std::move(generated)});
	state.callback_cv.notify_all();
	return true;
}

bool route_queued_media_event_locked(MediaV2State &state, SourceEventCaptureRoute route, uint64_t handle,
					     MediaV2Signal signal, uint64_t action_serial, bool orphaned,
					     std::vector<RuntimeV2Event> &generated)
{
	if (route != SourceEventCaptureRoute::Capture && route != SourceEventCaptureRoute::Defer)
		return false;
	if (route == SourceEventCaptureRoute::Capture && state.capture &&
	    result_has_source_event(*state.capture, "source.removed", handle))
		return true;
	queue_deferred_media_events_locked(state, handle, signal, action_serial, orphaned, std::move(generated));
	return true;
}

bool commit_direct_media_event_locked(MediaV2State &state, RevisionState *&revisions, EventDispatcher *&events,
					      uint64_t &revision);

struct MediaEventRouting {
	MediaV2State &state;
	SourceEventCaptureRoute route;
	uint64_t handle;
	MediaV2Signal signal;
	uint64_t action_serial;
	bool orphaned;
	bool require_resync;
	std::vector<RuntimeV2Event> &generated;
	RevisionState *&revisions;
	EventDispatcher *&events;
	uint64_t &revision;
};

struct MediaEventDecision {
	bool orphaned = false;
	bool require_resync = false;
	bool resync_after_queued_events = false;
};

MediaEventDecision classify_media_event_locked(MediaV2State &state, uint64_t handle, MediaV2Signal signal,
						       uint64_t action_serial, SourceEventCaptureRoute route)
{
	MediaEventDecision decision;
	if (!is_action_signal(signal))
		return decision;
	decision.require_resync = action_requires_resync(signal);
	decision.orphaned = consume_timed_out_media_action_locked(state, handle, action_serial);
	if (decision.orphaned) {
		decision.resync_after_queued_events = true;
		decision.require_resync = decision.require_resync || route == SourceEventCaptureRoute::Direct;
	}
	return decision;
}

bool route_media_event_locked(MediaEventRouting &routing)
{
	if (route_queued_media_event_locked(routing.state, routing.route, routing.handle, routing.signal,
						    routing.action_serial, routing.orphaned, routing.generated))
		return true;
	if (!routing.require_resync && routing.generated.empty())
		return true;
	return !commit_direct_media_event_locked(routing.state, routing.revisions, routing.events, routing.revision);
}

bool commit_direct_media_event_locked(MediaV2State &state, RevisionState *&revisions, EventDispatcher *&events,
					      uint64_t &revision)
{
	revisions = state.revisions;
	events = state.events;
	if (!revisions || !events)
		return false;
	if (!revisions->can_commit_mutation()) {
		std::fprintf(stderr, "obs-engine: media event requires resync because revision space is exhausted\n");
		std::fflush(stderr);
		events->require_resync_due_to_overflow(revisions->current());
		return false;
	}
	revision = revisions->commit_mutation();
	return true;
}

void publish_media_events(MediaV2State &state, uint64_t handle, MediaV2Signal signal,
				  uint64_t action_serial, std::vector<RuntimeV2Event> generated)
{
	const bool action_signal = is_action_signal(signal);
	if (generated.empty() && !action_signal)
		return;

	EventDispatcher *events = nullptr;
	RevisionState *revisions = nullptr;
	uint64_t revision = 0;
	MediaEventDecision decision;
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting)
			return;

		const SourceEventCaptureRoute route = state.capture_gate.route_for_current_thread();
		decision = classify_media_event_locked(state, handle, signal, action_serial, route);

		MediaEventRouting routing{state, route, handle, signal, action_serial, decision.orphaned,
						  decision.require_resync, generated, revisions, events, revision};
		if (route_media_event_locked(routing))
			return;
	}

	if (decision.require_resync) {
		if (decision.resync_after_queued_events)
			events->require_resync_after_queued_events(revision);
		else
			events->require_resync_due_to_overflow(revision);
		std::fprintf(stderr, "obs-engine: late media action completion requires controller resync\n");
		std::fflush(stderr);
		return;
	}

	for (RuntimeV2Event &event : generated)
		events->publish(EngineEventKind::State, event.name, revision, event.data.get());
}

bool promote_deferred_media_action_locked(MediaV2State &state, uint64_t handle, MediaV2Signal signal,
							  uint64_t action_serial, RuntimeV2Result &result)
{
	auto expected = std::find_if(state.deferred.begin(), state.deferred.end(),
					 [&](const DeferredMediaEventBatch &batch) {
						 return batch.handle == handle && batch.signal == signal &&
							batch.action_serial == action_serial && !batch.orphaned;
					 });
	if (expected == state.deferred.end())
		return false;

	for (RuntimeV2Event &event : expected->events) {
		if (!result_has_source_event(result, event.name, handle))
			result.events.push_back(std::move(event));
	}
	state.deferred_event_count -= expected->events.size();
	state.deferred.erase(expected);
	return true;
}

template<typename Submit>
bool enqueue_media_action(MediaV2State &state, Submit &&submit, uint64_t &action_serial, RuntimeV2Error &error)
{
	const auto enqueue_status = std::forward<Submit>(submit)(action_serial);
	if (enqueue_status == OBS_SOURCE_MEDIA_ACTION_SERIAL_EXHAUSTED)
		return fail(error, "internal_error", "media action ticket space is exhausted");
	if (enqueue_status == OBS_SOURCE_MEDIA_ACTION_ENQUEUED && action_serial != 0)
		return true;
	{
		std::lock_guard lock(state.mutex);
		state.deferred_overflow = true;
		state.callback_cv.notify_all();
	}
	return fail(error, "timeout", "media action could not be queued for settlement");
}

enum class MediaActionWaitResult { Settled, TimedOut, Unavailable };

bool has_pending_media_action_locked(const MediaV2State &state, uint64_t handle, MediaV2Signal signal,
					     uint64_t action_serial)
{
	return std::any_of(state.deferred.begin(), state.deferred.end(), [&](const DeferredMediaEventBatch &batch) {
		return batch.handle == handle && batch.signal == signal && batch.action_serial == action_serial &&
		       !batch.orphaned;
	});
}

MediaActionWaitResult wait_for_media_action(MediaV2State &state, uint64_t handle, MediaV2Signal signal,
					    uint64_t action_serial, RuntimeV2Result &result)
{
	std::unique_lock lock(state.mutex);
	const auto deadline = std::chrono::steady_clock::now() + kMediaActionSettleTimeout;
	for (;;) {
		if (promote_deferred_media_action_locked(state, handle, signal, action_serial, result))
			return MediaActionWaitResult::Settled;
		if (!state.accepting)
			return MediaActionWaitResult::Unavailable;
		if (state.deferred_overflow)
			return MediaActionWaitResult::TimedOut;
		if (!state.callback_cv.wait_until(lock, deadline, [&] {
				return !state.accepting || state.deferred_overflow ||
				       has_pending_media_action_locked(state, handle, signal, action_serial);
				}))
			return MediaActionWaitResult::TimedOut;
	}
}

void remember_timed_out_media_action(MediaV2State &state, uint64_t handle, uint64_t action_serial)
{
	std::lock_guard lock(state.mutex);
	const MediaActionKey key{handle, action_serial};
	if (!state.timed_out.contains(key)) {
		if (state.timed_out.size() >= kMaxMediaActionBatches) {
			state.timed_out.clear();
			state.timed_out_tracking_uncertain = true;
		} else {
			state.timed_out.insert(key);
		}
	}
	state.deferred_overflow = true;
	state.callback_cv.notify_all();
}

template<typename Submit>
bool settle_media_action(MediaV2State &state, uint64_t handle, obs_source_t *source, MediaV2Signal signal,
				 Submit &&submit, RuntimeV2Result &result, RuntimeV2Error &error)
{
	uint64_t action_serial = 0;
	if (!enqueue_media_action(state, std::forward<Submit>(submit), action_serial, error))
		return false;
	const MediaActionWaitResult wait_result = wait_for_media_action(state, handle, signal, action_serial, result);
	if (wait_result == MediaActionWaitResult::Settled)
		return true;
	if (wait_result == MediaActionWaitResult::Unavailable)
		return fail(error, "not_available", "media event bridge is shutting down");

	if (obs_source_removed(source))
		return fail(error, "not_found", "media source was removed during action settlement");
	remember_timed_out_media_action(state, handle, action_serial);
	return fail(error, "timeout", "media action was not observed before the settlement deadline");
}

template<typename Submit>
bool run_media_action(MediaV2State *state, uint64_t handle, obs_source_t *source, MediaV2Signal signal,
			      const char *action, obs_media_state before, bool always_mutating, Submit &&submit,
			      RuntimeV2Result &result, RuntimeV2Error &error)
{
	if (!state || !settle_media_action(*state, handle, source, signal, std::forward<Submit>(submit), result, error))
		return false;
	const obs_media_state after = obs_source_media_get_state(source);
	result.data = make_action_data(handle, action, after, true);
	result.mutated = always_mutating || before != after || !result.events.empty();
	return true;
}

struct MediaSignalEventDescriptor {
	MediaV2Signal signal;
	const char *name;
	obs_media_state expected_state;
	bool requires_expected_state;
};

constexpr MediaSignalEventDescriptor kMediaSignalEvents[] = {
	{MediaV2Signal::Play, "media.playing", OBS_MEDIA_STATE_PLAYING, true},
	{MediaV2Signal::Pause, "media.paused", OBS_MEDIA_STATE_PAUSED, true},
	{MediaV2Signal::Stop, "media.stopped", OBS_MEDIA_STATE_STOPPED, true},
	{MediaV2Signal::Started, "media.started", OBS_MEDIA_STATE_NONE, false},
	{MediaV2Signal::Ended, "media.ended", OBS_MEDIA_STATE_NONE, false},
};

void append_media_signal_event(std::vector<RuntimeV2Event> &events, uint64_t handle, MediaV2Signal signal,
				       obs_media_state state)
{
	for (const MediaSignalEventDescriptor &entry : kMediaSignalEvents) {
		if (entry.signal != signal || (entry.requires_expected_state && entry.expected_state != state))
			continue;
		events.push_back(RuntimeV2Event{entry.name, make_state_data(handle, state)});
		return;
	}
}

void collect_media_state_events(MediaV2Observer &observer, MediaV2Signal signal, obs_source_t *source,
					std::vector<RuntimeV2Event> &generated)
{
	const obs_media_state current_state = obs_source_media_get_state(source);
	std::lock_guard cache_lock(observer.cache_mutex);
	const bool changed = !observer.state_known || current_state != observer.state_value;
	if (changed)
		generated.push_back(RuntimeV2Event{"media.stateChanged", make_state_data(observer.handle, current_state)});

	append_media_signal_event(generated, observer.handle, signal, current_state);

	if (current_state == OBS_MEDIA_STATE_ERROR &&
	    (!observer.state_known || observer.state_value != OBS_MEDIA_STATE_ERROR))
		generated.push_back(RuntimeV2Event{"media.error", make_state_data(observer.handle, current_state)});

	observer.state_known = true;
	observer.state_value = current_state;
}

void collect_media_signal(MediaV2Observer &observer, MediaV2Signal signal, uint64_t action_serial)
{
	MediaV2State *state = observer.state;
	if (!state)
		return;

	MediaCallbackScope callback_scope(*state, observer.handle);
	if (!callback_scope.accepted() || callback_scope.suppressed())
		return;

	obs_source_t *source = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (!source)
		return;

	std::vector<RuntimeV2Event> generated;
	try {
		collect_media_state_events(observer, signal, source, generated);
	} catch (...) {
		obs_source_release(source);
		throw;
	}

	obs_source_release(source);
	publish_media_events(*state, observer.handle, signal, action_serial, std::move(generated));
}

void media_play_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Play, read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media play event normalization failed\n");
	}
}

void media_pause_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Pause, read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media pause event normalization failed\n");
	}
}

void media_restart_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Restart,
					 read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media restart event normalization failed\n");
	}
}

void media_stop_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Stop, read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media stop event normalization failed\n");
	}
}

void media_next_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Next, read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media next event normalization failed\n");
	}
}

void media_previous_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Previous,
					 read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media previous event normalization failed\n");
	}
}

void media_time_cb(void *data, calldata_t *calldata)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Time, read_action_serial(calldata));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media time event normalization failed\n");
	}
}

void media_started_cb(void *data, calldata_t *)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Started, 0);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media started event normalization failed\n");
	}
}

void media_ended_cb(void *data, calldata_t *)
{
	try {
		collect_media_signal(*static_cast<MediaV2Observer *>(data), MediaV2Signal::Ended, 0);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: media ended event normalization failed\n");
	}
}

void connect_media_observer(MediaV2Observer &observer, obs_source_t *source)
{
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	signal_handler_connect(handler, "media_play", media_play_cb, &observer);
	signal_handler_connect(handler, "media_pause", media_pause_cb, &observer);
	signal_handler_connect(handler, "media_restart", media_restart_cb, &observer);
	signal_handler_connect(handler, "media_stopped", media_stop_cb, &observer);
	signal_handler_connect(handler, "media_next", media_next_cb, &observer);
	signal_handler_connect(handler, "media_previous", media_previous_cb, &observer);
	signal_handler_connect(handler, "media_time", media_time_cb, &observer);
	signal_handler_connect(handler, "media_started", media_started_cb, &observer);
	signal_handler_connect(handler, "media_ended", media_ended_cb, &observer);
}

void disconnect_media_observer(MediaV2Observer &observer)
{
	obs_source_t *source = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (source) {
		signal_handler_t *handler = obs_source_get_signal_handler(source);
		signal_handler_disconnect(handler, "media_play", media_play_cb, &observer);
		signal_handler_disconnect(handler, "media_pause", media_pause_cb, &observer);
		signal_handler_disconnect(handler, "media_restart", media_restart_cb, &observer);
		signal_handler_disconnect(handler, "media_stopped", media_stop_cb, &observer);
		signal_handler_disconnect(handler, "media_next", media_next_cb, &observer);
		signal_handler_disconnect(handler, "media_previous", media_previous_cb, &observer);
		signal_handler_disconnect(handler, "media_time", media_time_cb, &observer);
		signal_handler_disconnect(handler, "media_started", media_started_cb, &observer);
		signal_handler_disconnect(handler, "media_ended", media_ended_cb, &observer);
		obs_source_release(source);
	}
	if (observer.weak) {
		obs_weak_source_release(observer.weak);
		observer.weak = nullptr;
	}
}

} // namespace

bool Engine::v2_get_media_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
					 RuntimeV2Error &error) const
{
	if (!read_handle_field(params, "source", handle))
		return fail(error, "bad_request", "params.source must be a canonical decimal handle string");
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return fail(error, "not_found", "source handle was not found");
	source = it->second;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA) == 0)
		return fail(error, "unsupported_capability", "source does not advertise controllable media support");
	return true;
}

void Engine::v2_add_media_observer(uint64_t handle, obs_source_t *source,
					    std::vector<std::shared_ptr<MediaV2Observer>> &retire)
{
	auto observer = std::make_shared<MediaV2Observer>();
	observer->state = media_v2_state_.get();
	observer->handle = handle;
	observer->weak = obs_source_get_weak_source(source);
	observer->state_value = obs_source_media_get_state(source);
	observer->state_known = true;
	connect_media_observer(*observer, source);

	bool keep = false;
	{
		std::lock_guard lock(media_v2_state_->mutex);
		const auto source_it = sources_.find(handle);
		const bool controllable = source_it != sources_.end() &&
			(obs_source_get_output_flags(source_it->second) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
		if (media_v2_state_->accepting && controllable && !media_v2_state_->observers.contains(handle)) {
			media_v2_state_->observers.emplace(handle, observer);
			keep = true;
		}
	}
	if (!keep) {
		disconnect_media_observer(*observer);
		retire.push_back(std::move(observer));
	}
}

void Engine::v2_bind_media_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!media_v2_state_)
		media_v2_state_ = std::make_shared<MediaV2State>();
	std::lock_guard lock(media_v2_state_->mutex);
	media_v2_state_->revisions = revisions;
	media_v2_state_->events = events;
	media_v2_state_->accepting = revisions && events;
}

void Engine::v2_begin_media_event_capture(RuntimeV2Result &result)
{
	if (!media_v2_state_)
		return;
	std::lock_guard lock(media_v2_state_->mutex);
	if (!media_v2_state_->accepting)
		return;
	media_v2_state_->capture = &result;
	media_v2_state_->capture_gate.begin();
}

void Engine::v2_wait_for_media_event_callbacks()
{
	if (!media_v2_state_)
		return;
	std::unique_lock lock(media_v2_state_->mutex);
	media_v2_state_->callback_cv.wait(lock, [&] {
		return !media_v2_state_->accepting || media_v2_state_->direct_callbacks_inflight == 0;
	});
}

void Engine::v2_end_media_event_capture() noexcept
{
	if (!media_v2_state_)
		return;

	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	bool lost_deferred = false;
	{
		std::lock_guard lock(media_v2_state_->mutex);
		media_v2_state_->capture = nullptr;
		media_v2_state_->capture_gate.end();
		lost_deferred = media_v2_state_->deferred_overflow || !media_v2_state_->deferred.empty();
		if (lost_deferred) {
			clear_deferred_media_events(*media_v2_state_);
			revisions = media_v2_state_->revisions;
			events = media_v2_state_->events;
		}
	}

	if (lost_deferred && revisions && events) {
		std::fprintf(stderr, "obs-engine: deferred media events abandoned; forcing controller resync\n");
		std::fflush(stderr);
		events->require_resync_due_to_overflow(revisions->current());
	}
}

void Engine::v2_drain_deferred_media_events(RevisionState::MutationGuard &guard)
{
	if (!media_v2_state_)
		return;
	DeferredMediaEventSnapshot snapshot = take_deferred_media_events(*media_v2_state_, true, false);
	publish_deferred_media_snapshot(std::move(snapshot), guard);
}

void Engine::v2_flush_deferred_media_events(RevisionState::MutationGuard &guard)
{
	if (!media_v2_state_)
		return;
	DeferredMediaEventSnapshot snapshot = take_deferred_media_events(*media_v2_state_, false, true);
	publish_deferred_media_snapshot(std::move(snapshot), guard);
}

bool Engine::v2_collect_media_observer_changes(std::vector<std::pair<uint64_t, obs_source_t *>> &add,
						       std::vector<std::shared_ptr<MediaV2Observer>> &retire)
{
	std::lock_guard lock(media_v2_state_->mutex);
	if (!media_v2_state_->accepting)
		return false;
	for (const auto &[handle, source] : sources_) {
		const bool controllable =
			(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
		if (controllable && !media_v2_state_->observers.contains(handle))
			add.emplace_back(handle, source);
	}
	for (auto it = media_v2_state_->observers.begin(); it != media_v2_state_->observers.end();) {
		auto source_it = sources_.find(it->first);
		const bool still_controllable = source_it != sources_.end() &&
			(obs_source_get_output_flags(source_it->second) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
		if (still_controllable) {
			++it;
			continue;
		}
		retire.push_back(it->second);
		it = media_v2_state_->observers.erase(it);
	}
	return true;
}

void Engine::v2_sync_media_observers()
{
	if (!media_v2_state_)
		return;

	std::vector<std::shared_ptr<MediaV2Observer>> retire;
	std::vector<std::pair<uint64_t, obs_source_t *>> add;
	if (!v2_collect_media_observer_changes(add, retire))
		return;

	for (const auto &[handle, source] : add) {
		v2_add_media_observer(handle, source, retire);
	}

	for (auto &observer : retire)
		disconnect_media_observer(*observer);
	if (!retire.empty()) {
		std::lock_guard lock(media_v2_state_->mutex);
		media_v2_state_->retired.insert(media_v2_state_->retired.end(), retire.begin(), retire.end());
	}
}

void Engine::v2_prepare_media_shutdown() noexcept
{
	if (!media_v2_state_)
		return;

	std::vector<std::shared_ptr<MediaV2Observer>> observers;
	{
		std::lock_guard lock(media_v2_state_->mutex);
		media_v2_state_->accepting = false;
		media_v2_state_->capture = nullptr;
		media_v2_state_->capture_gate.end();
		clear_deferred_media_events(*media_v2_state_);
		media_v2_state_->timed_out.clear();
		media_v2_state_->timed_out_tracking_uncertain = false;
		media_v2_state_->callback_cv.notify_all();
		media_v2_state_->revisions = nullptr;
		media_v2_state_->events = nullptr;
		for (auto &[_, observer] : media_v2_state_->observers)
			observers.push_back(observer);
		media_v2_state_->observers.clear();
	}

	for (auto &observer : observers) {
		try {
			disconnect_media_observer(*observer);
		} catch (...) {
		}
	}
	if (!observers.empty()) {
		std::lock_guard lock(media_v2_state_->mutex);
		media_v2_state_->retired.insert(media_v2_state_->retired.end(), observers.begin(), observers.end());
	}
}

bool Engine::v2_media_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;
	result.data = make_state_data(handle, obs_source_media_get_state(source));
	return true;
}

bool Engine::v2_media_play(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;

	const obs_media_state before = obs_source_media_get_state(source);
	if (before == OBS_MEDIA_STATE_PLAYING) {
		result.data = make_action_data(handle, "play", before, false);
		return true;
	}
	return run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Play, "play", before, false,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(source, OBS_SOURCE_MEDIA_ACTION_PLAY_PAUSE, 0, &serial);
				},
				result, error);
}

bool Engine::v2_media_pause(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;

	const obs_media_state before = obs_source_media_get_state(source);
	if (before == OBS_MEDIA_STATE_PAUSED) {
		result.data = make_action_data(handle, "pause", before, false);
		return true;
	}
	return run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Pause, "pause", before, false,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(source, OBS_SOURCE_MEDIA_ACTION_PLAY_PAUSE, 1, &serial);
				},
				result, error);
}

bool Engine::v2_media_toggle_pause(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;

	const obs_media_state before = obs_source_media_get_state(source);
	const bool pause = before == OBS_MEDIA_STATE_PLAYING || before == OBS_MEDIA_STATE_OPENING ||
			   before == OBS_MEDIA_STATE_BUFFERING;
	if (!pause && before != OBS_MEDIA_STATE_PAUSED)
		return fail(error, "invalid_state", "media.togglePause requires playing, opening, buffering, or paused state");
	const char *action = pause ? "pause" : "play";
	const MediaV2Signal signal = pause ? MediaV2Signal::Pause : MediaV2Signal::Play;
	return run_media_action(media_v2_state_.get(), handle, source, signal, action, before, false,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(
						source, OBS_SOURCE_MEDIA_ACTION_PLAY_PAUSE, pause ? 1 : 0, &serial);
				},
				result, error);
}

bool Engine::v2_media_stop(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;

	const obs_media_state before = obs_source_media_get_state(source);
	if (before == OBS_MEDIA_STATE_STOPPED) {
		result.data = make_action_data(handle, "stop", before, false);
		return true;
	}
	return run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Stop, "stop", before, false,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(source, OBS_SOURCE_MEDIA_ACTION_STOP, 0, &serial);
				},
				result, error);
}

bool Engine::v2_media_restart(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;
	return run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Restart, "restart",
				obs_source_media_get_state(source), true,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(source, OBS_SOURCE_MEDIA_ACTION_RESTART, 0, &serial);
				},
				result, error);
}

bool Engine::v2_media_next(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;
	return run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Next, "next",
				obs_source_media_get_state(source), true,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(source, OBS_SOURCE_MEDIA_ACTION_NEXT, 0, &serial);
				},
				result, error);
}

bool Engine::v2_media_previous(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;
	return run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Previous, "previous",
				obs_source_media_get_state(source), true,
				[&](uint64_t &serial) {
					return obs_source_media_action_enqueue(source, OBS_SOURCE_MEDIA_ACTION_PREVIOUS, 0, &serial);
				},
				result, error);
}

bool Engine::v2_media_get_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_int(data.get(), "durationMs", static_cast<long long>(obs_source_media_get_duration(source)));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_media_get_position(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_int(data.get(), "positionMs", static_cast<long long>(obs_source_media_get_time(source)));
	result.data = std::move(data);
	return true;
}

bool read_media_position(obs_data_t *params, obs_source_t *source, int64_t &position, RuntimeV2Error &error)
{
	long long raw_position = 0;
	bool present = false;
	if (!read_integer_field(params, "positionMs", raw_position, present) || !present || raw_position < 0)
		return fail(error, "bad_request", "params.positionMs must be a non-negative signed 64-bit integer");
	const int64_t duration = obs_source_media_get_duration(source);
	if (duration >= 0 && raw_position > duration)
		return fail(error, "bad_request", "params.positionMs exceeds the current media duration");
	position = static_cast<int64_t>(raw_position);
	return true;
}

bool Engine::v2_media_set_position(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_media_source(params, handle, source, error))
		return false;

	int64_t position = 0;
	if (!read_media_position(params, source, position, error))
		return false;
	const int64_t current_position = obs_source_media_get_time(source);
	if (current_position == position) {
		result.data = make_action_data(handle, "setPosition", obs_source_media_get_state(source), false);
		obs_data_set_int(result.data.get(), "positionMs", position);
		return true;
	}
	if (!run_media_action(media_v2_state_.get(), handle, source, MediaV2Signal::Time, "setPosition",
				      obs_source_media_get_state(source), false,
				      [&](uint64_t &serial) {
					      return obs_source_media_action_enqueue(
						      source, OBS_SOURCE_MEDIA_ACTION_SET_TIME, position, &serial);
				      },
				      result, error))
		return false;
	const int64_t settled_position = obs_source_media_get_time(source);
	obs_data_set_int(result.data.get(), "positionMs", static_cast<long long>(settled_position));
	result.mutated = settled_position != current_position || !result.events.empty();
	return true;
}

} // namespace obs_engine
