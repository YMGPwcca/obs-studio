#include "runtime.hpp"

#include "events.hpp"
#include "obs_source_update_private.hpp"
#include "source_event_capture.hpp"
#include "validation.hpp"

#include <callback/calldata.h>
#include <callback/signal.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace obs_engine {

struct FilterV2Observer;

struct DeferredFilterEventBatch {
	uint64_t handle = 0;
	uint64_t update_generation = 0;
	uint64_t update_serial_begin = 0;
	uint64_t update_serial_end = 0;
	bool uncertain = false;
	std::vector<RuntimeV2Event> events;
};

struct FilterV2State {
	std::mutex mutex;
	std::condition_variable callback_cv;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	RuntimeV2Result *capture = nullptr;
	SourceEventCaptureGate capture_gate;
	std::deque<DeferredFilterEventBatch> deferred;
	size_t deferred_event_count = 0;
	size_t direct_callbacks_inflight = 0;
	bool deferred_overflow = false;
	bool accepting = false;
	std::unordered_map<uint64_t, std::shared_ptr<FilterV2Observer>> observers;
	std::vector<std::shared_ptr<FilterV2Observer>> retired;
	std::unordered_map<uint64_t, std::set<uint64_t>> uncertain_update_serials;
	size_t uncertain_update_count = 0;
	bool uncertain_tracking = false;
};

struct FilterV2Observer {
	FilterV2State *state = nullptr;
	uint64_t handle = 0;
	uint64_t source_id = 0;
	obs_weak_source_t *weak = nullptr;
	std::mutex cache_mutex;
	std::string name;
	std::string settings;
	bool enabled = false;
	std::mutex update_mutex;
	std::condition_variable update_cv;
	uint64_t update_generation = 0;
	uint64_t last_update_serial_begin = 0;
	uint64_t last_update_serial_end = 0;
	bool attached = true;
};

namespace {

constexpr size_t kMaxFilterKindBytes = 128;
constexpr size_t kMaxFilterNameBytes = 256;
constexpr size_t kMaxFilterUncertainHandles = kDefaultEventQueueCapacity;
constexpr auto kFilterUpdateSettleTimeout = std::chrono::seconds(5);

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "filter operation failed";
	return false;
}

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool is_bounded_string(const char *value, size_t max_bytes)
{
	if (!value)
		return false;
	for (size_t index = 0; index <= max_bytes; ++index) {
		if (value[index] == '\0')
			return true;
	}
	return false;
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

bool read_bool_field(obs_data_t *data, const char *name, bool &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_BOOLEAN) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_bool(item);
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

bool read_object_field(obs_data_t *data, const char *name, ObsDataPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_obj(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
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

ObsDataPtr clone_data(obs_data_t *data)
{
	if (!data)
		return {};
	const char *json = obs_data_get_json(data);
	return json ? ObsDataPtr(obs_data_create_from_json(json)) : ObsDataPtr{};
}

bool filter_type_exists(const char *kind)
{
	const char *candidate = nullptr;
	for (size_t index = 0; obs_enum_filter_types(index, &candidate); ++index) {
		if (candidate && std::strcmp(candidate, kind) == 0)
			return true;
	}
	return false;
}

bool read_filter_kind(obs_data_t *params, std::string &kind, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxFilterKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid filter kind identifier");
	if (!filter_type_exists(kind.c_str()))
		return fail(error, "not_found", "filter kind is not registered");
	return true;
}

bool read_optional_filter_name(obs_data_t *params, std::string &name, const char *invalid_message,
					       RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (present && (name.empty() || !is_bounded_string(name.c_str(), kMaxFilterNameBytes)))
		return fail(error, "bad_request", invalid_message);
	return true;
}

struct FilterSettingsInput {
	ObsDataPtr requested;
	ObsDataPtr before;
	ObsDataPtr proposed;
};

bool read_filter_settings_input(obs_data_t *params, obs_source_t *filter, bool replace_settings,
					FilterSettingsInput &input, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_object_field(params, "settings", input.requested, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	input.before.reset(obs_source_get_settings(filter));
	if (!input.before)
		return fail(error, "obs_error", replace_settings ? "could not read filter settings before replacement"
										 : "could not read filter settings before update");
	if (replace_settings)
		return true;
	input.proposed = clone_data(input.before.get());
	if (!input.proposed)
		return fail(error, "obs_error", "could not read filter settings before update");
	obs_data_apply(input.proposed.get(), input.requested.get());
	return true;
}

bool filter_settings_are_equal(const FilterSettingsInput &input, bool replace_settings)
{
	const char *before_json = obs_data_get_json(input.before.get());
	const char *requested_json = replace_settings ? obs_data_get_json(input.requested.get())
								     : obs_data_get_json(input.proposed.get());
	return before_json && requested_json && std::strcmp(before_json, requested_json) == 0;
}

bool submit_filter_settings(obs_source_t *filter, const FilterSettingsInput &input, bool replace_settings,
					 uint64_t &update_serial, RuntimeV2Error &error)
{
	const bool submitted = replace_settings
				       ? obs_source_reset_settings_tracked(filter, input.requested.get(), &update_serial)
				       : obs_source_update_tracked(filter, input.requested.get(), &update_serial);
	if (submitted && update_serial != 0)
		return true;
	const char *message = replace_settings ? "filter replacement could not obtain a deferred-update identity"
						       : "filter update could not obtain a deferred-update identity";
	return fail(error, "internal_error", message);
}

ObsDataPtr make_filter_settings_result(uint64_t handle, const FilterEntry &entry, obs_data_t *settings)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", entry.source_id);
	obs_data_set_obj(data.get(), "settings", settings);
	return data;
}

void append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	result.events.push_back(RuntimeV2Event{name, std::move(data)});
}

bool result_has_filter_event(const RuntimeV2Result &result, std::string_view name, uint64_t handle)
{
	return std::any_of(result.events.begin(), result.events.end(), [&](const RuntimeV2Event &event) {
		if (event.name != name || !event.data)
			return false;
		uint64_t event_handle = 0;
		return read_handle_field(event.data.get(), "filter", event_handle) && event_handle == handle;
	});
}

bool settings_json(obs_source_t *filter, std::string &json)
{
	ObsDataPtr settings(obs_source_get_settings(filter));
	if (!settings)
		return false;
	const char *value = obs_data_get_json(settings.get());
	if (!value)
		return false;
	json = value;
	return true;
}

enum class FilterSignal { Update, Rename, Enabled };

void read_update_serial_range(calldata_t *params, uint64_t &begin, uint64_t &end)
{
	begin = 0;
	end = 0;
	if (!params)
		return;

	const long long begin_value = calldata_int(params, "update_serial_begin");
	const long long end_value = calldata_int(params, "update_serial_end");
	if (begin_value <= 0 || end_value < begin_value)
		return;
	begin = static_cast<uint64_t>(begin_value);
	end = static_cast<uint64_t>(end_value);
}

class FilterCallbackScope {
public:
	FilterCallbackScope(FilterV2State &state, uint64_t handle) : state_(state)
	{
		std::lock_guard lock(state_.mutex);
		if (!state_.accepting)
			return;
		accepted_ = true;
		const SourceEventCaptureRoute route = state_.capture_gate.route_for_current_thread();
		if (route == SourceEventCaptureRoute::Direct) {
			++state_.direct_callbacks_inflight;
			counted_direct_ = true;
		} else if (route == SourceEventCaptureRoute::Capture && state_.capture &&
			   result_has_filter_event(*state_.capture, "filter.removed", handle)) {
			suppressed_ = true;
		}
	}

	~FilterCallbackScope()
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
	FilterV2State &state_;
	bool accepted_ = false;
	bool counted_direct_ = false;
	bool suppressed_ = false;
};

struct DeferredFilterEventSnapshot {
	std::deque<DeferredFilterEventBatch> batches;
	EventDispatcher *events = nullptr;
	bool overflow = false;
};

void clear_deferred_filter_events(FilterV2State &state)
{
	state.deferred.clear();
	state.deferred_event_count = 0;
	state.deferred_overflow = false;
}

void clear_uncertain_filter_updates(FilterV2State &state)
{
	state.uncertain_update_serials.clear();
	state.uncertain_update_count = 0;
}

bool resolve_uncertain_filter_updates_locked(FilterV2State &state, uint64_t handle, uint64_t serial_end);

void annotate_filter_batch_locked(FilterV2State &state, DeferredFilterEventBatch &batch, bool &overflow)
{
	if (state.uncertain_tracking) {
		batch.uncertain = true;
		return;
	}
	auto uncertain = state.uncertain_update_serials.find(batch.handle);
	if (uncertain == state.uncertain_update_serials.end())
		return;
	batch.uncertain = true;
	if (resolve_uncertain_filter_updates_locked(state, batch.handle, batch.update_serial_end))
		overflow = true;
}

bool should_discard_filter_batch(const FilterV2State &state, const DeferredFilterEventBatch &batch, bool end_capture)
{
	return end_capture && state.capture && result_has_filter_event(*state.capture, "filter.removed", batch.handle);
}

bool resolve_uncertain_filter_updates_locked(FilterV2State &state, uint64_t handle, uint64_t serial_end)
{
	if (serial_end == 0)
		return false;
	const auto it = state.uncertain_update_serials.find(handle);
	if (it == state.uncertain_update_serials.end())
		return false;

	bool resolved = false;
	for (auto pending = it->second.begin(); pending != it->second.end() && *pending <= serial_end;) {
		pending = it->second.erase(pending);
		if (state.uncertain_update_count > 0)
			--state.uncertain_update_count;
		resolved = true;
	}
	if (it->second.empty())
		state.uncertain_update_serials.erase(it);
	return resolved;
}

void forget_uncertain_filter_updates_locked(FilterV2State &state, uint64_t handle)
{
	const auto it = state.uncertain_update_serials.find(handle);
	if (it == state.uncertain_update_serials.end())
		return;
	if (state.uncertain_update_count >= it->second.size())
		state.uncertain_update_count -= it->second.size();
	else
		state.uncertain_update_count = 0;
	state.uncertain_update_serials.erase(it);
}

void remember_uncertain_filter_update_locked(FilterV2State &state, uint64_t handle, uint64_t serial)
{
	if (state.uncertain_tracking)
		return;
	if (serial == 0 || state.uncertain_update_count >= kMaxFilterUncertainHandles) {
		clear_uncertain_filter_updates(state);
		state.uncertain_tracking = true;
		return;
	}

	auto &pending = state.uncertain_update_serials[handle];
	if (pending.insert(serial).second)
		++state.uncertain_update_count;
}

DeferredFilterEventSnapshot take_deferred_filter_events(FilterV2State &state, bool wait_for_pre_capture,
							bool end_capture)
{
	DeferredFilterEventSnapshot snapshot;
	std::unique_lock lock(state.mutex);
	if (wait_for_pre_capture)
		state.callback_cv.wait(lock, [&] { return !state.accepting || state.direct_callbacks_inflight == 0; });

	if (!state.accepting) {
		clear_deferred_filter_events(state);
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
		DeferredFilterEventBatch batch = std::move(state.deferred.front());
		state.deferred.pop_front();
		state.deferred_event_count -= batch.events.size();
		annotate_filter_batch_locked(state, batch, snapshot.overflow);
		if (should_discard_filter_batch(state, batch, end_capture))
			continue;
		snapshot.batches.push_back(std::move(batch));
	}

	if (end_capture) {
		state.capture = nullptr;
		state.capture_gate.end();
	}
	return snapshot;
}

uint64_t commit_filter_resync_revision(RevisionState::MutationGuard &guard)
{
	return guard.can_commit_mutation() ? guard.commit_mutation() : guard.current();
}

void report_filter_resync(EventDispatcher *events, uint64_t revision, bool after_queued_events,
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

bool publish_filter_batch(DeferredFilterEventBatch &batch, const DeferredFilterEventSnapshot &snapshot,
				  RevisionState::MutationGuard &guard)
{
	if (batch.uncertain) {
		report_filter_resync(snapshot.events, commit_filter_resync_revision(guard), true,
				      "obs-engine: deferred filter completion was uncertain; controller resync required");
		return true;
	}
	if (!guard.can_commit_mutation()) {
		report_filter_resync(snapshot.events, guard.current(), false,
				      "obs-engine: deferred filter events require resync because revision space is exhausted");
		return false;
	}
	const uint64_t revision = guard.commit_mutation();
	if (snapshot.events) {
		for (RuntimeV2Event &event : batch.events)
			snapshot.events->publish(EngineEventKind::State, event.name, revision, event.data.get());
	}
	return true;
}

void publish_deferred_filter_snapshot(DeferredFilterEventSnapshot snapshot, RevisionState::MutationGuard &guard)
{
	if (snapshot.overflow)
		report_filter_resync(snapshot.events, commit_filter_resync_revision(guard), false,
				      "obs-engine: deferred filter event queue overflowed; controller resync required");

	for (DeferredFilterEventBatch &batch : snapshot.batches) {
		if (!publish_filter_batch(batch, snapshot, guard))
			return;
	}
}

bool update_filter_uncertainty_locked(FilterV2State &state, uint64_t handle, uint64_t serial_end,
					     SourceEventCaptureRoute route, bool &resolved)
{
	resolved = false;
	if (state.uncertain_tracking)
		return true;
	if (!state.uncertain_update_serials.contains(handle))
		return false;
	if (!resolve_uncertain_filter_updates_locked(state, handle, serial_end))
		return true;
	resolved = true;
	if (route != SourceEventCaptureRoute::Direct)
		state.deferred_overflow = true;
	return false;
}

bool capture_filter_events_locked(FilterV2State &state, uint64_t handle, std::vector<RuntimeV2Event> &generated)
{
	if (!state.capture)
		return false;
	if (result_has_filter_event(*state.capture, "filter.removed", handle))
		return true;
	for (RuntimeV2Event &event : generated) {
		if (!result_has_filter_event(*state.capture, event.name, handle))
			state.capture->events.push_back(std::move(event));
	}
	state.capture->mutated = true;
	return true;
}

bool queue_deferred_filter_events_locked(FilterV2State &state, uint64_t handle, uint64_t update_generation,
					 uint64_t update_serial_begin, uint64_t update_serial_end,
					 std::vector<RuntimeV2Event> generated)
{
	if (state.deferred_overflow)
		return true;
	if (state.deferred.size() >= kDefaultEventQueueCapacity ||
	    generated.size() > kDefaultEventQueueCapacity ||
	    state.deferred_event_count > kDefaultEventQueueCapacity - generated.size()) {
		state.deferred.clear();
		state.deferred_event_count = 0;
		state.deferred_overflow = true;
		state.callback_cv.notify_all();
		return true;
	}
	state.deferred_event_count += generated.size();
	state.deferred.push_back(DeferredFilterEventBatch{handle, update_generation, update_serial_begin,
								 update_serial_end, false, std::move(generated)});
	state.callback_cv.notify_all();
	return true;
}

bool commit_direct_filter_event_locked(FilterV2State &state, RevisionState *&revisions, EventDispatcher *&events,
					       uint64_t &revision)
{
	revisions = state.revisions;
	events = state.events;
	if (!revisions || !events)
		return false;
	if (!revisions->can_commit_mutation()) {
		std::fprintf(stderr, "obs-engine: filter event requires resync because revision space is exhausted\n");
		std::fflush(stderr);
		events->require_resync_due_to_overflow(revisions->current());
		return false;
	}
	revision = revisions->commit_mutation();
	return true;
}

bool quarantine_uncertain_filter_event_locked(FilterV2State &state, SourceEventCaptureRoute route)
{
	if ((route == SourceEventCaptureRoute::Capture && state.capture) || route == SourceEventCaptureRoute::Defer) {
		state.deferred_overflow = true;
		state.callback_cv.notify_all();
		return true;
	}
	return false;
}

struct FilterEventRouting {
	FilterV2State &state;
	uint64_t handle;
	uint64_t update_generation;
	uint64_t update_serial_begin;
	uint64_t update_serial_end;
	SourceEventCaptureRoute route;
	bool uncertain;
	bool uncertainty_resolved;
	std::vector<RuntimeV2Event> &generated;
	RevisionState *&revisions;
	EventDispatcher *&events;
	uint64_t &revision;
	bool &require_resync;
};

enum class FilterEventRoutingResult { NotApplicable, Handled, ContinueAfterUnlock };

FilterEventRoutingResult route_filter_uncertainty_locked(FilterEventRouting &routing)
{
	if (!routing.uncertain &&
	    !(routing.uncertainty_resolved && routing.route == SourceEventCaptureRoute::Direct))
		return FilterEventRoutingResult::NotApplicable;
	if (quarantine_uncertain_filter_event_locked(routing.state, routing.route))
		return FilterEventRoutingResult::Handled;
	if (!commit_direct_filter_event_locked(routing.state, routing.revisions, routing.events, routing.revision))
		return FilterEventRoutingResult::Handled;
	routing.require_resync = true;
	return FilterEventRoutingResult::ContinueAfterUnlock;
}

bool route_filter_payload_locked(FilterEventRouting &routing)
{
	if (routing.generated.empty()) {
		if (routing.uncertainty_resolved && routing.route != SourceEventCaptureRoute::Direct) {
			routing.state.deferred_overflow = true;
			routing.state.callback_cv.notify_all();
		}
		return true;
	}
	if (routing.route == SourceEventCaptureRoute::Capture &&
	    capture_filter_events_locked(routing.state, routing.handle, routing.generated))
		return true;
	if (routing.route == SourceEventCaptureRoute::Defer) {
		queue_deferred_filter_events_locked(routing.state, routing.handle, routing.update_generation,
								    routing.update_serial_begin, routing.update_serial_end,
								    std::move(routing.generated));
		return true;
	}
	return !commit_direct_filter_event_locked(routing.state, routing.revisions, routing.events, routing.revision);
}

bool route_filter_event_locked(FilterEventRouting &routing)
{
	const FilterEventRoutingResult uncertainty = route_filter_uncertainty_locked(routing);
	if (uncertainty == FilterEventRoutingResult::Handled)
		return true;
	if (uncertainty == FilterEventRoutingResult::ContinueAfterUnlock)
		return false;
	return route_filter_payload_locked(routing);
}

void publish_filter_events(FilterV2State &state, uint64_t handle, uint64_t update_generation,
			   uint64_t update_serial_begin, uint64_t update_serial_end, bool is_update_signal,
			   std::vector<RuntimeV2Event> generated)
{
	EventDispatcher *events = nullptr;
	RevisionState *revisions = nullptr;
	uint64_t revision = 0;
	bool require_resync = false;
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting)
			return;

		const SourceEventCaptureRoute route = state.capture_gate.route_for_current_thread();
		bool uncertainty_resolved = false;
		const bool uncertain = is_update_signal &&
			update_filter_uncertainty_locked(state, handle, update_serial_end, route, uncertainty_resolved);
		FilterEventRouting routing{state, handle, update_generation, update_serial_begin, update_serial_end, route,
						  uncertain, uncertainty_resolved, generated, revisions, events, revision, require_resync};
		if (route_filter_event_locked(routing))
			return;
	}

	if (require_resync) {
		events->require_resync_due_to_overflow(revision);
		std::fprintf(stderr, "obs-engine: late filter update completion requires controller resync\n");
		std::fflush(stderr);
		return;
	}

	for (RuntimeV2Event &event : generated)
		events->publish(EngineEventKind::State, event.name, revision, event.data.get());
}

ObsDataPtr make_filter_summary(uint64_t handle, const FilterEntry &entry, obs_source_t *parent)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", entry.source_id);
	obs_data_set_string(data.get(), "name", obs_source_get_name(entry.filter));
	obs_data_set_string(data.get(), "kind", obs_source_get_id(entry.filter));
	obs_data_set_string(data.get(), "unversionedKind", obs_source_get_unversioned_id(entry.filter));
	obs_data_set_bool(data.get(), "enabled", obs_source_enabled(entry.filter));
	obs_data_set_int(data.get(), "index", static_cast<long long>(obs_source_filter_get_index(parent, entry.filter)));
	obs_data_set_int(data.get(), "outputFlags", static_cast<long long>(obs_source_get_output_flags(entry.filter)));
	return data;
}

} // namespace

void Engine::v2_register_filter(uint64_t handle, uint64_t source_id, obs_source_t *filter)
{
	bool inserted_filter = false;
	bool inserted_handle = false;
	try {
		if (!filters_.emplace(handle, FilterEntry{source_id, filter}).second)
			throw std::runtime_error("filter handle collision");
		inserted_filter = true;
		inserted_handle = filter_handles_.emplace(filter, handle).second;
		if (!inserted_handle)
			throw std::runtime_error("filter object was already registered");
	} catch (...) {
		if (inserted_handle)
			filter_handles_.erase(filter);
		if (inserted_filter)
			filters_.erase(handle);
		obs_source_release(filter);
		throw;
	}
}

void Engine::v2_register_attached_filter(uint64_t source_id, obs_source_t *parent, uint64_t handle,
						 obs_source_t *filter)
{
	bool inserted_filter = false;
	bool inserted_handle = false;
	try {
		if (!filters_.emplace(handle, FilterEntry{source_id, filter}).second)
			throw std::runtime_error("filter handle collision");
		inserted_filter = true;
		inserted_handle = filter_handles_.emplace(filter, handle).second;
		if (!inserted_handle)
			throw std::runtime_error("filter object was already registered");
	} catch (...) {
		if (inserted_handle)
			filter_handles_.erase(filter);
		if (inserted_filter)
			filters_.erase(handle);
		obs_source_filter_remove(parent, filter);
		obs_source_release(filter);
		throw;
	}
}

ObsDataPtr Engine::v2_filter_order_data(uint64_t source_id, uint64_t changed, obs_source_t *parent) const
{
	std::vector<std::pair<int, uint64_t>> ordered;
	for (const auto &[handle, entry] : filters_) {
		if (entry.source_id != source_id)
			continue;
		const int index = obs_source_filter_get_index(parent, entry.filter);
		if (index >= 0)
			ordered.emplace_back(index, handle);
	}
	std::sort(ordered.begin(), ordered.end());

	ObsArrayPtr filters(obs_data_array_create());
	for (const auto &[_, handle] : ordered) {
		ObsDataPtr value(obs_data_create());
		set_handle(value.get(), "filter", handle);
		obs_data_array_push_back(filters.get(), value.get());
	}
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", source_id);
	obs_data_set_array(data.get(), "filters", filters.get());
	if (changed != 0)
		set_handle(data.get(), "changed", changed);
	return data;
}

struct FilterReferenceCollection {
	std::vector<obs_source_t *> values;
	bool failed = false;
};

void collect_filter_ref(obs_source_t *, obs_source_t *child, void *param)
{
	auto *collection = static_cast<FilterReferenceCollection *>(param);
	if (!child || obs_source_get_type(child) != OBS_SOURCE_TYPE_FILTER)
		return;
	try {
		obs_source_t *reference = obs_source_get_ref(child);
		if (reference) {
			try {
				collection->values.push_back(reference);
			} catch (...) {
				obs_source_release(reference);
				throw;
			}
		}
	} catch (...) {
		collection->failed = true;
	}
}

bool advance_filter_update_generation(FilterV2State &state, FilterV2Observer &observer, calldata_t *params,
					      uint64_t &generation, uint64_t &serial_begin, uint64_t &serial_end)
{
	read_update_serial_range(params, serial_begin, serial_end);
	{
		std::lock_guard update_lock(observer.update_mutex);
		if (observer.update_generation == std::numeric_limits<uint64_t>::max()) {
			generation = 0;
		} else {
			generation = ++observer.update_generation;
			observer.last_update_serial_begin = serial_begin;
			observer.last_update_serial_end = serial_end;
			return true;
		}
	}
	std::lock_guard state_lock(state.mutex);
	state.uncertain_tracking = true;
	return false;
}

ObsDataPtr make_filter_event_data(const FilterV2Observer &observer)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", observer.handle);
	set_handle(data.get(), "source", observer.source_id);
	return data;
}

void append_filter_settings_event(FilterV2Observer &observer, FilterSignal signal,
					  ObsDataPtr &current_settings_data, bool have_settings,
					  const std::string &current_settings, std::vector<RuntimeV2Event> &generated)
{
	if (signal != FilterSignal::Update || !have_settings || current_settings == observer.settings)
		return;
	ObsDataPtr data = make_filter_event_data(observer);
	obs_data_set_obj(data.get(), "settings", current_settings_data.get());
	generated.push_back(RuntimeV2Event{"filter.settingsChanged", std::move(data)});
}

void append_filter_rename_event(FilterV2Observer &observer, const std::string &current_name,
					std::vector<RuntimeV2Event> &generated)
{
	if (current_name == observer.name)
		return;
	ObsDataPtr data = make_filter_event_data(observer);
	obs_data_set_string(data.get(), "name", current_name.c_str());
	obs_data_set_string(data.get(), "previousName", observer.name.c_str());
	generated.push_back(RuntimeV2Event{"filter.renamed", std::move(data)});
}

void append_filter_enabled_event(FilterV2Observer &observer, bool current_enabled,
					 std::vector<RuntimeV2Event> &generated)
{
	if (current_enabled == observer.enabled)
		return;
	ObsDataPtr data = make_filter_event_data(observer);
	obs_data_set_bool(data.get(), "enabled", current_enabled);
	generated.push_back(RuntimeV2Event{"filter.enabledChanged", std::move(data)});
}

void collect_filter_state_events(FilterV2Observer &observer, FilterSignal signal, obs_source_t *filter,
					 std::vector<RuntimeV2Event> &generated)
{
	std::lock_guard cache_lock(observer.cache_mutex);
	const char *name = obs_source_get_name(filter);
	const std::string current_name = name ? name : "";
	const bool current_enabled = obs_source_enabled(filter);
	ObsDataPtr current_settings_data(obs_source_get_settings(filter));
	const char *current_settings_json = current_settings_data ? obs_data_get_json(current_settings_data.get()) : nullptr;
	const bool have_settings = current_settings_json != nullptr;
	const std::string current_settings = current_settings_json ? current_settings_json : "";

	append_filter_settings_event(observer, signal, current_settings_data, have_settings, current_settings, generated);
	append_filter_rename_event(observer, current_name, generated);
	append_filter_enabled_event(observer, current_enabled, generated);

	observer.name = current_name;
	observer.enabled = current_enabled;
	if (signal == FilterSignal::Update && have_settings)
		observer.settings = current_settings;
}

void collect_filter_signal(FilterV2Observer &observer, FilterSignal signal, calldata_t *params)
{
	FilterV2State *state = observer.state;
	if (!state)
		return;
	FilterCallbackScope callback_scope(*state, observer.handle);
	if (!callback_scope.accepted() || callback_scope.suppressed())
		return;

	obs_source_t *filter = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (!filter)
		return;

	uint64_t update_generation = 0;
	uint64_t update_serial_begin = 0;
	uint64_t update_serial_end = 0;
	if (signal == FilterSignal::Update)
		advance_filter_update_generation(*state, observer, params, update_generation, update_serial_begin,
						 update_serial_end);

	std::vector<RuntimeV2Event> generated;
	try {
		collect_filter_state_events(observer, signal, filter, generated);
	} catch (...) {
		obs_source_release(filter);
		throw;
	}

	obs_source_release(filter);
	for (RuntimeV2Event &event : generated) {
		event.filter_update_serial_begin = update_serial_begin;
		event.filter_update_serial_end = update_serial_end;
	}
	publish_filter_events(*state, observer.handle, update_generation, update_serial_begin, update_serial_end,
			      signal == FilterSignal::Update, std::move(generated));
	if (signal == FilterSignal::Update)
		observer.update_cv.notify_all();
}

void filter_update_cb(void *data, calldata_t *params)
{
	try {
		collect_filter_signal(*static_cast<FilterV2Observer *>(data), FilterSignal::Update, params);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: filter update event normalization failed\n");
	}
}

void filter_rename_cb(void *data, calldata_t *params)
{
	try {
		collect_filter_signal(*static_cast<FilterV2Observer *>(data), FilterSignal::Rename, params);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: filter rename event normalization failed\n");
	}
}

void filter_enabled_cb(void *data, calldata_t *params)
{
	try {
		collect_filter_signal(*static_cast<FilterV2Observer *>(data), FilterSignal::Enabled, params);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: filter enable event normalization failed\n");
	}
}

void connect_filter_observer(FilterV2Observer &observer, obs_source_t *filter)
{
	signal_handler_t *handler = obs_source_get_signal_handler(filter);
	if (!handler)
		return;
	signal_handler_connect(handler, "update", filter_update_cb, &observer);
	signal_handler_connect(handler, "rename", filter_rename_cb, &observer);
	signal_handler_connect(handler, "enable", filter_enabled_cb, &observer);
}

void disconnect_filter_observer(FilterV2Observer &observer)
{
	obs_source_t *filter = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (filter) {
		signal_handler_t *handler = obs_source_get_signal_handler(filter);
		if (handler) {
			signal_handler_disconnect(handler, "update", filter_update_cb, &observer);
			signal_handler_disconnect(handler, "rename", filter_rename_cb, &observer);
			signal_handler_disconnect(handler, "enable", filter_enabled_cb, &observer);
		}
		obs_source_release(filter);
	}
	if (observer.weak) {
		obs_weak_source_release(observer.weak);
		observer.weak = nullptr;
	}
	{
		std::lock_guard lock(observer.update_mutex);
		observer.attached = false;
	}
	observer.update_cv.notify_all();
}

void remove_filter_observer(FilterV2State *state, uint64_t handle)
{
	if (!state)
		return;
	std::shared_ptr<FilterV2Observer> observer;
	{
		std::lock_guard lock(state->mutex);
		auto it = state->observers.find(handle);
		if (it == state->observers.end())
			return;
		observer = std::move(it->second);
		state->observers.erase(it);
		forget_uncertain_filter_updates_locked(*state, handle);
	}
	disconnect_filter_observer(*observer);
	{
		std::lock_guard lock(state->mutex);
		state->retired.push_back(std::move(observer));
	}
}

bool filter_update_event_covers_serial(const RuntimeV2Event &event, uint64_t serial)
{
	return serial != 0 && event.filter_update_serial_begin != 0 &&
	       event.filter_update_serial_begin <= serial && event.filter_update_serial_end >= serial;
}

bool filter_settings_event_matches(const RuntimeV2Event &event, uint64_t handle, uint64_t serial,
				   const std::string &expected_settings)
{
	if (event.name != "filter.settingsChanged" || !event.data)
		return false;
	if (!filter_update_event_covers_serial(event, serial))
		return false;
	uint64_t event_handle = 0;
	if (!read_handle_field(event.data.get(), "filter", event_handle) || event_handle != handle)
		return false;
	ObsDataPtr settings;
	bool present = false;
	if (!read_object_field(event.data.get(), "settings", settings, present) || !present || !settings)
		return false;
	const char *json = obs_data_get_json(settings.get());
	return json && expected_settings == json;
}

bool result_matches_filter_update(const RuntimeV2Result &result, uint64_t handle, uint64_t serial,
					 const std::string &expected_settings)
{
	return std::any_of(result.events.begin(), result.events.end(), [&](const RuntimeV2Event &event) {
		return filter_settings_event_matches(event, handle, serial, expected_settings);
	});
}

bool batch_matches_filter_update(const DeferredFilterEventBatch &batch, uint64_t handle, uint64_t baseline_generation,
					 uint64_t serial, const std::string &expected_settings)
{
	if (batch.handle != handle || batch.uncertain || batch.update_generation <= baseline_generation || serial == 0 ||
	    batch.update_serial_begin == 0 || batch.update_serial_begin > serial || batch.update_serial_end < serial)
		return false;
	return std::any_of(batch.events.begin(), batch.events.end(), [&](const RuntimeV2Event &event) {
		return filter_settings_event_matches(event, handle, serial, expected_settings);
	});
}

bool promote_deferred_filter_update(FilterV2State &state, uint64_t handle, uint64_t baseline_generation,
					    uint64_t serial, const std::string &expected_settings, RuntimeV2Result &result)
{
	std::lock_guard lock(state.mutex);
	for (auto it = state.deferred.begin(); it != state.deferred.end(); ++it) {
		if (!batch_matches_filter_update(*it, handle, baseline_generation, serial, expected_settings))
			continue;
		if (state.uncertain_tracking || state.uncertain_update_serials.contains(handle))
			return false;
		const size_t event_count = it->events.size();
		for (RuntimeV2Event &event : it->events) {
			if (!result_has_filter_event(result, event.name, handle))
				result.events.push_back(std::move(event));
		}
		state.deferred_event_count -= event_count;
		state.deferred.erase(it);
		return true;
	}
	return false;
}

std::shared_ptr<FilterV2Observer> find_filter_observer(FilterV2State &state, uint64_t handle)
{
	std::lock_guard lock(state.mutex);
	auto it = state.observers.find(handle);
	return it == state.observers.end() ? nullptr : it->second;
}

bool read_filter_observer_generation(const std::shared_ptr<FilterV2Observer> &observer, uint64_t baseline_generation,
					     uint64_t &observed_generation)
{
	std::lock_guard lock(observer->update_mutex);
	if (!observer->attached || observer->update_generation < baseline_generation)
		return false;
	observed_generation = observer->update_generation;
	return true;
}

bool filter_update_is_settled(const std::shared_ptr<FilterV2Observer> &observer, const RuntimeV2Result &result,
				      uint64_t handle, uint64_t baseline_generation, uint64_t serial,
				      const std::string &expected_settings)
{
	if (!result_matches_filter_update(result, handle, serial, expected_settings))
		return false;
	std::lock_guard lock(observer->update_mutex);
	return observer->attached && observer->update_generation > baseline_generation;
}

bool wait_for_filter_update(FilterV2State &state, const std::shared_ptr<FilterV2Observer> &observer,
				    uint64_t handle, uint64_t baseline_generation, uint64_t serial,
				    const std::string &expected_settings, RuntimeV2Result &result, uint64_t observed_generation)
{
	const auto deadline = std::chrono::steady_clock::now() + kFilterUpdateSettleTimeout;
	for (;;) {
		{
			std::unique_lock lock(observer->update_mutex);
			if (!observer->update_cv.wait_until(lock, deadline, [&] {
				    return observer->update_generation != observed_generation || !observer->attached;
				    }))
				break;
			if (!observer->attached)
				break;
			observed_generation = observer->update_generation;
		}
		if (filter_update_is_settled(observer, result, handle, baseline_generation, serial, expected_settings))
			return true;
		if (promote_deferred_filter_update(state, handle, baseline_generation, serial, expected_settings, result))
			return true;
	}
	return filter_update_is_settled(observer, result, handle, baseline_generation, serial, expected_settings);
}

bool settle_deferred_filter_update(FilterV2State &state, uint64_t handle, uint64_t baseline_generation,
					   uint64_t serial, const std::string &expected_settings, RuntimeV2Result &result)
{
	std::shared_ptr<FilterV2Observer> observer = find_filter_observer(state, handle);
	if (!observer)
		return false;

	uint64_t observed_generation = baseline_generation;
	if (!read_filter_observer_generation(observer, baseline_generation, observed_generation))
		return false;
	if (filter_update_is_settled(observer, result, handle, baseline_generation, serial, expected_settings))
		return true;
	if (promote_deferred_filter_update(state, handle, baseline_generation, serial, expected_settings, result))
		return true;
	return wait_for_filter_update(state, observer, handle, baseline_generation, serial, expected_settings, result,
					     observed_generation);
}

void mark_filter_settlement_lost(FilterV2State &state, uint64_t handle, uint64_t baseline_generation,
					 uint64_t update_serial)
{
	std::shared_ptr<FilterV2Observer> observer;
	{
		std::lock_guard lock(state.mutex);
		if (auto it = state.observers.find(handle); it != state.observers.end())
			observer = it->second;
	}

	bool callback_seen = false;
	if (observer) {
		std::lock_guard update_lock(observer->update_mutex);
		callback_seen = observer->update_generation > baseline_generation && update_serial != 0 &&
				observer->last_update_serial_end >= update_serial;
	}

	std::lock_guard lock(state.mutex);
	if (callback_seen) {
		// The callback ran but its canonical event was lost to the overflow
		// boundary. The resync marker below is sufficient; do not quarantine a
		// future request after the observed completion.
		resolve_uncertain_filter_updates_locked(state, handle, update_serial);
	} else if (!state.uncertain_tracking) {
		remember_uncertain_filter_update_locked(state, handle, update_serial);
	}
	state.deferred_overflow = true;
	state.callback_cv.notify_all();
}

void Engine::v2_bind_filter_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!filter_v2_state_)
		filter_v2_state_ = std::make_shared<FilterV2State>();
	std::lock_guard lock(filter_v2_state_->mutex);
	filter_v2_state_->revisions = revisions;
	filter_v2_state_->events = events;
	filter_v2_state_->accepting = revisions && events;
}

void Engine::v2_begin_filter_event_capture(RuntimeV2Result &result)
{
	if (!filter_v2_state_)
		return;
	std::lock_guard lock(filter_v2_state_->mutex);
	if (!filter_v2_state_->accepting)
		return;
	filter_v2_state_->capture = &result;
	filter_v2_state_->capture_gate.begin();
}

void Engine::v2_wait_for_filter_event_callbacks()
{
	if (!filter_v2_state_)
		return;
	std::unique_lock lock(filter_v2_state_->mutex);
	filter_v2_state_->callback_cv.wait(lock, [&] {
		return !filter_v2_state_->accepting || filter_v2_state_->direct_callbacks_inflight == 0;
	});
}

void Engine::v2_end_filter_event_capture() noexcept
{
	if (!filter_v2_state_)
		return;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	bool lost_deferred = false;
	{
		std::lock_guard lock(filter_v2_state_->mutex);
		filter_v2_state_->capture = nullptr;
		filter_v2_state_->capture_gate.end();
		lost_deferred = filter_v2_state_->deferred_overflow || !filter_v2_state_->deferred.empty();
		if (lost_deferred) {
			clear_deferred_filter_events(*filter_v2_state_);
			revisions = filter_v2_state_->revisions;
			events = filter_v2_state_->events;
		}
	}
	if (lost_deferred && revisions && events) {
		std::fprintf(stderr, "obs-engine: deferred filter events abandoned; forcing controller resync\n");
		std::fflush(stderr);
		events->require_resync_due_to_overflow(revisions->current());
	}
}

void Engine::v2_drain_deferred_filter_events(RevisionState::MutationGuard &guard)
{
	if (filter_v2_state_)
		publish_deferred_filter_snapshot(take_deferred_filter_events(*filter_v2_state_, true, false), guard);
}

void Engine::v2_flush_deferred_filter_events(RevisionState::MutationGuard &guard)
{
	if (filter_v2_state_)
		publish_deferred_filter_snapshot(take_deferred_filter_events(*filter_v2_state_, false, true), guard);
}

void Engine::v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source)
{
	if (!source)
		return;
	FilterReferenceCollection collection;
	obs_source_enum_filters(source, collect_filter_ref, &collection);
	if (collection.failed) {
		for (obs_source_t *filter : collection.values)
			obs_source_release(filter);
		throw std::runtime_error("could not enumerate attached filters");
	}
	for (obs_source_t *filter : collection.values) {
		if (!filter)
			continue;
		if (auto known = filter_handles_.find(filter); known != filter_handles_.end()) {
			if (filters_.contains(known->second)) {
				obs_source_release(filter);
				continue;
			}
			filter_handles_.erase(known);
		}

		v2_register_filter(allocate_handle(), source_id, filter);
	}
}

void Engine::v2_add_filter_observer(uint64_t handle)
{
	if (!filter_v2_state_)
		return;
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return;

	auto observer = std::make_shared<FilterV2Observer>();
	observer->state = filter_v2_state_.get();
	observer->handle = handle;
	observer->source_id = it->second.source_id;
	observer->weak = obs_source_get_weak_source(it->second.filter);
	observer->name = obs_source_get_name(it->second.filter) ? obs_source_get_name(it->second.filter) : "";
	observer->enabled = obs_source_enabled(it->second.filter);
	settings_json(it->second.filter, observer->settings);
	connect_filter_observer(*observer, it->second.filter);

	bool keep = false;
	{
		std::lock_guard lock(filter_v2_state_->mutex);
		if (filter_v2_state_->accepting && filters_.contains(handle) &&
		    !filter_v2_state_->observers.contains(handle)) {
			filter_v2_state_->observers.emplace(handle, observer);
			keep = true;
		}
	}
	if (!keep)
		disconnect_filter_observer(*observer);
}

bool Engine::v2_filter_record_update_baseline(uint64_t handle, RuntimeV2Result &result)
{
	if (!filter_v2_state_)
		return false;

	std::shared_ptr<FilterV2Observer> observer;
	{
		std::lock_guard lock(filter_v2_state_->mutex);
		auto it = filter_v2_state_->observers.find(handle);
		if (it == filter_v2_state_->observers.end())
			return false;
		observer = it->second;
	}

	std::lock_guard lock(observer->update_mutex);
	if (!observer->attached)
		return false;
	result.filter_update_handle = handle;
	result.filter_update_generation = observer->update_generation;
	result.has_filter_update_baseline = true;
	return true;
}

bool Engine::v2_get_filter_parent(obs_data_t *params, uint64_t &handle, FilterEntry *&entry,
					  obs_source_t *&parent, RuntimeV2Error &error)
{
	v2_sync_filter_observers();
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto filter_it = filters_.find(handle);
	if (filter_it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(filter_it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	entry = &filter_it->second;
	parent = source_it->second;
	return true;
}

bool Engine::v2_move_filter(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error,
				    enum obs_order_movement movement)
{
	reset_result(result, error);
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;
	const int old_index = obs_source_filter_get_index(parent, entry->filter);
	if (old_index < 0)
		return fail(error, "not_found", "filter is no longer attached to its parent source");
	obs_source_filter_set_order(parent, entry->filter, movement);
	const int new_index = obs_source_filter_get_index(parent, entry->filter);
	const bool changed = new_index != old_index;
	result.data = v2_filter_order_data(entry->source_id, changed ? handle : 0, parent);
	if (changed) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

void Engine::v2_filter_forget_source(uint64_t source_id) noexcept
{
	try {
		for (auto it = filters_.begin(); it != filters_.end();) {
			if (it->second.source_id != source_id) {
				++it;
				continue;
			}

			const uint64_t handle = it->first;
			obs_source_t *filter = it->second.filter;
			remove_filter_observer(filter_v2_state_.get(), handle);
			filter_handles_.erase(filter);
			obs_source_release(filter);
			it = filters_.erase(it);
		}
	} catch (...) {
		std::fprintf(stderr, "obs-engine: failed to forget filters for a source during rollback\n");
		std::fflush(stderr);
	}
}

bool Engine::v2_sync_filter_registry(std::unordered_set<obs_source_t *> &attached)
{
	std::lock_guard lock(filter_v2_state_->mutex);
	if (!filter_v2_state_->accepting)
		return false;
	for (const auto &[source_id, source] : sources_) {
		FilterReferenceCollection collection;
		obs_source_enum_filters(source, collect_filter_ref, &collection);
		if (collection.failed) {
			for (obs_source_t *filter : collection.values)
				obs_source_release(filter);
			throw std::runtime_error("could not enumerate attached filters");
		}
		for (obs_source_t *filter : collection.values) {
			if (!filter)
				continue;
			attached.insert(filter);
			auto known = filter_handles_.find(filter);
			if (known != filter_handles_.end()) {
				auto entry = filters_.find(known->second);
				if (entry == filters_.end()) {
					filter_handles_.erase(known);
				} else {
					if (entry->second.source_id != source_id)
						entry->second.source_id = source_id;
					obs_source_release(filter);
					continue;
				}
			}
			v2_register_filter(allocate_handle(), source_id, filter);
		}
	}
	return true;
}

void Engine::v2_remove_unattached_filters(const std::unordered_set<obs_source_t *> &attached)
{
	std::vector<uint64_t> remove;
	for (const auto &[handle, entry] : filters_) {
		if (!attached.contains(entry.filter))
			remove.push_back(handle);
	}
	for (uint64_t handle : remove) {
		auto it = filters_.find(handle);
		if (it == filters_.end())
			continue;
		remove_filter_observer(filter_v2_state_.get(), handle);
		filter_handles_.erase(it->second.filter);
		obs_source_release(it->second.filter);
		filters_.erase(it);
	}
}

std::vector<uint64_t> Engine::v2_filter_observers_to_add() const
{
	std::vector<uint64_t> add;
	if (!filter_v2_state_)
		return add;
	std::lock_guard lock(filter_v2_state_->mutex);
	for (const auto &[handle, _] : filters_) {
		if (!filter_v2_state_->observers.contains(handle))
			add.push_back(handle);
	}
	return add;
}

void Engine::v2_sync_filter_observers()
{
	if (!filter_v2_state_)
		return;
	std::unordered_set<obs_source_t *> attached;
	if (!v2_sync_filter_registry(attached))
		return;
	v2_remove_unattached_filters(attached);
	for (uint64_t handle : v2_filter_observers_to_add())
		v2_add_filter_observer(handle);
}

void Engine::v2_prepare_filter_shutdown() noexcept
{
	if (filter_v2_state_) {
		std::vector<std::shared_ptr<FilterV2Observer>> observers;
		{
			std::lock_guard lock(filter_v2_state_->mutex);
			filter_v2_state_->accepting = false;
			filter_v2_state_->capture = nullptr;
			filter_v2_state_->capture_gate.end();
			clear_deferred_filter_events(*filter_v2_state_);
			clear_uncertain_filter_updates(*filter_v2_state_);
			filter_v2_state_->uncertain_tracking = false;
			filter_v2_state_->callback_cv.notify_all();
			filter_v2_state_->revisions = nullptr;
			filter_v2_state_->events = nullptr;
			for (auto &[_, observer] : filter_v2_state_->observers)
				observers.push_back(observer);
			filter_v2_state_->observers.clear();
		}
		for (auto &observer : observers) {
			observer->update_cv.notify_all();
			try {
				disconnect_filter_observer(*observer);
			} catch (...) {
			}
		}
		if (!observers.empty()) {
			std::lock_guard lock(filter_v2_state_->mutex);
			filter_v2_state_->retired.insert(filter_v2_state_->retired.end(), observers.begin(), observers.end());
		}
	}

	for (auto &[_, entry] : filters_)
		obs_source_release(entry.filter);
	filters_.clear();
	filter_handles_.clear();
}

void Engine::v2_filter_prepare_parent_removal(uint64_t source_id, RuntimeV2Result &result)
{
	auto source_it = sources_.find(source_id);
	if (source_it == sources_.end())
		return;
	v2_sync_filter_observers();
	source_it = sources_.find(source_id);
	if (source_it == sources_.end())
		return;
	v2_filter_register_source_filters(source_id, source_it->second);

	std::vector<std::pair<int, uint64_t>> ordered;
	for (const auto &[handle, entry] : filters_) {
		if (entry.source_id == source_id)
			ordered.emplace_back(obs_source_filter_get_index(source_it->second, entry.filter), handle);
	}
	std::sort(ordered.begin(), ordered.end(), [](const auto &left, const auto &right) {
		if (left.first != right.first)
			return left.first < right.first;
		return left.second < right.second;
	});

	for (const auto &[index, handle] : ordered) {
		auto it = filters_.find(handle);
		if (it == filters_.end())
			continue;
		ObsDataPtr event_data(obs_data_create());
		set_handle(event_data.get(), "filter", handle);
		set_handle(event_data.get(), "source", source_id);
		append_event(result, "filter.removed", std::move(event_data));
		remove_filter_observer(filter_v2_state_.get(), handle);
		if (index >= 0)
			obs_source_filter_remove(source_it->second, it->second.filter);
		filter_handles_.erase(it->second.filter);
		obs_source_release(it->second.filter);
		filters_.erase(it);
	}
}

bool Engine::v2_prepare_filter_settlement(obs_data_t *params, RuntimeV2Result &result, uint64_t &handle,
						 FilterEntry *&entry, RuntimeV2Error &error)
{
	if (!result.mutated || !filter_v2_state_ || !result.has_filter_update_baseline || result.filter_update_serial == 0)
		return fail(error, "internal_error", "filter settings update has no observer baseline");
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "internal_error", "filter settings update has an invalid target handle");
	if (handle != result.filter_update_handle)
		return fail(error, "internal_error", "filter settings update target changed during settlement");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was removed during settings settlement");
	entry = &it->second;
	return true;
}

bool Engine::v2_settle_filter_mutation(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	if (!v2_prepare_filter_settlement(params, result, handle, entry, error))
		return false;

	ObsDataPtr current(obs_source_get_settings(entry->filter));
	const char *json = current ? obs_data_get_json(current.get()) : nullptr;
	const std::string expected = json ? json : "";
	if (expected.empty() ||
	    !settle_deferred_filter_update(*filter_v2_state_, handle, result.filter_update_generation,
					     result.filter_update_serial, expected, result)) {
		std::fprintf(stderr,
			     "obs-engine: timed out settling deferred update for filter %llu; controller resync required\n",
			     static_cast<unsigned long long>(handle));
		std::fflush(stderr);
		mark_filter_settlement_lost(*filter_v2_state_, handle, result.filter_update_generation,
					     result.filter_update_serial);
		return fail(error, "timeout", "filter update was not observed before the settlement deadline");
	}

	if (!result.data)
		return fail(error, "internal_error", "filter settings update produced no result");
	obs_data_item_t *settings_item = obs_data_item_byname(result.data.get(), "settings");
	if (settings_item) {
		if (obs_data_item_gettype(settings_item) == OBS_DATA_OBJECT && current)
			obs_data_set_obj(result.data.get(), "settings", current.get());
		obs_data_item_release(&settings_item);
	}
	return true;
}

bool Engine::v2_filter_kind_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsArrayPtr kinds(obs_data_array_create());
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_filter_types(index, &kind); ++index) {
		if (!kind)
			continue;
		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "id", kind);
		const char *display = obs_source_get_display_name(kind);
		obs_data_set_string(entry.get(), "displayName", display ? display : kind);
		obs_data_set_int(entry.get(), "outputFlags", static_cast<long long>(obs_get_source_output_flags(kind)));
		obs_module_t *module = obs_source_get_module(kind);
		if (module) {
			const char *file = obs_get_module_file_name(module);
			if (file)
				obs_data_set_string(entry.get(), "module", file);
		}
		obs_data_set_int(entry.get(), "moduleLoadState", static_cast<long long>(obs_source_load_state(kind)));
		obs_data_array_push_back(kinds.get(), entry.get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "kinds", kinds.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_filter_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_filter_kind(params, kind, error))
		return false;
	ObsDataPtr defaults(obs_get_source_defaults(kind.c_str()));
	if (!defaults)
		return fail(error, "obs_error", "filter kind did not provide defaults");
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", defaults.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_filter_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_filter_kind(params, kind, error))
		return false;
	ObsDataPtr bridge(obs_data_create());
	ObsDataPtr target(obs_data_create());
	obs_data_set_string(target.get(), "type", "filterKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	obs_data_set_obj(bridge.get(), "target", target.get());
	return v2_properties_get(bridge.get(), result, error);
}

bool Engine::v2_create_filter_object(uint64_t source_id, obs_source_t *parent, uint64_t handle,
					     const std::string &kind, const std::string &name, ObsDataPtr &settings,
					     RuntimeV2Error &error)
{
	const std::string generated_name = "engine-filter-" + std::to_string(handle);
	const char *actual_name = name.empty() ? generated_name.c_str() : name.c_str();
	obs_source_t *filter = obs_source_create_private(kind.c_str(), actual_name, settings.get());
	if (!filter)
		return fail(error, "obs_error", "obs_source_create_private did not create the filter");
	if (obs_source_get_type(filter) != OBS_SOURCE_TYPE_FILTER) {
		obs_source_release(filter);
		return fail(error, "obs_error", "registered filter kind did not create a filter source");
	}
	obs_source_filter_add(parent, filter);
	if (obs_source_filter_get_index(parent, filter) < 0) {
		obs_source_release(filter);
		return fail(error, "incompatible_filter", "libobs did not attach the filter to the source");
	}
	v2_register_attached_filter(source_id, parent, handle, filter);
	return true;
}

bool Engine::v2_filter_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	v2_sync_filter_observers();
	uint64_t source_id = 0;
	obs_source_t *parent = nullptr;
	if (!v2_get_source(params, source_id, parent, error))
		return false;
	v2_filter_register_source_filters(source_id, parent);
	v2_sync_filter_observers();

	std::vector<std::pair<int, uint64_t>> ordered;
	for (const auto &[handle, entry] : filters_) {
		if (entry.source_id != source_id)
			continue;
		const int index = obs_source_filter_get_index(parent, entry.filter);
		if (index >= 0)
			ordered.emplace_back(index, handle);
	}
	std::sort(ordered.begin(), ordered.end());
	ObsArrayPtr filters(obs_data_array_create());
	for (const auto &[_, handle] : ordered) {
		ObsDataPtr summary = make_filter_summary(handle, filters_.at(handle), parent);
		obs_data_array_push_back(filters.get(), summary.get());
	}
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", source_id);
	obs_data_set_array(data.get(), "filters", filters.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(ordered.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_filter_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;
	result.data = make_filter_summary(handle, *entry, parent);
	return true;
}

bool Engine::v2_filter_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	v2_sync_filter_observers();
	uint64_t source_id = 0;
	obs_source_t *parent = nullptr;
	if (!v2_get_source(params, source_id, parent, error))
		return false;

	std::string kind;
	if (!read_filter_kind(params, kind, error))
		return false;

	std::string name;
	bool present = false;
	if (!read_optional_filter_name(params, name, "filter name must be a 1-256 byte string", error))
		return false;
	ObsDataPtr settings;
	if (!read_object_field(params, "settings", settings, present))
		return fail(error, "bad_request", "params.settings must be an object when present");

	const uint64_t allocated = allocate_handle();
	if (!v2_create_filter_object(source_id, parent, allocated, kind, name, settings, error))
		return false;

	result.data = make_filter_summary(allocated, filters_.at(allocated), parent);
	append_event(result, "filter.created", clone_data(result.data.get()));
	result.mutated = true;
	v2_sync_filter_observers();
	return true;
}

bool Engine::v2_filter_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;
	if (obs_source_filter_get_index(parent, entry->filter) < 0)
		return fail(error, "not_found", "filter is no longer attached to its parent source");

	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", entry->source_id);
	result.data = std::move(data);
	append_event(result, "filter.removed", clone_data(result.data.get()));
	remove_filter_observer(filter_v2_state_.get(), handle);
	obs_source_filter_remove(parent, entry->filter);
	filter_handles_.erase(entry->filter);
	obs_source_release(entry->filter);
	filters_.erase(handle);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;
	std::string name;
	bool present = false;
	if (!read_string_field(params, "name", name, present) || !present || name.empty() ||
	    !is_bounded_string(name.c_str(), kMaxFilterNameBytes))
		return fail(error, "bad_request", "params.name must be a 1-256 byte string");
	v2_sync_filter_observers();
	entry = &filters_.at(handle);
	const char *current = obs_source_get_name(entry->filter);
	if (current && name == current) {
		result.data = make_filter_summary(handle, *entry, parent);
		return true;
	}
	obs_source_set_name(entry->filter, name.c_str());
	result.data = make_filter_summary(handle, *entry, parent);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;
	std::string name;
	if (!read_optional_filter_name(params, name, "filter duplicate name must be a 1-256 byte string", error))
		return false;
	const uint64_t duplicate_handle = allocate_handle();
	const std::string generated_name = std::string(obs_source_get_name(entry->filter)) + " copy";
	const char *requested_name = name.empty() ? generated_name.c_str() : name.c_str();
	obs_source_t *duplicate = obs_source_duplicate(entry->filter, requested_name, true);
	if (!duplicate || duplicate == entry->filter) {
		if (duplicate)
			obs_source_release(duplicate);
		return fail(error, "obs_error", "libobs did not create an independent filter duplicate");
	}
	obs_source_set_enabled(duplicate, obs_source_enabled(entry->filter));
	obs_source_filter_add(parent, duplicate);
	if (obs_source_filter_get_index(parent, duplicate) < 0) {
		obs_source_release(duplicate);
		return fail(error, "incompatible_filter", "libobs did not attach the duplicated filter");
	}
	v2_register_attached_filter(entry->source_id, parent, duplicate_handle, duplicate);
	result.data = make_filter_summary(duplicate_handle, filters_.at(duplicate_handle), parent);
	set_handle(result.data.get(), "duplicateOf", handle);
	ObsDataPtr event_data = clone_data(result.data.get());
	append_event(result, "filter.created", std::move(event_data));
	result.mutated = true;
	v2_sync_filter_observers();
	return true;
}

bool Engine::v2_filter_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	v2_sync_filter_observers();
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	ObsDataPtr settings(obs_source_get_settings(it->second.filter));
	if (!settings)
		return fail(error, "obs_error", "could not read filter settings");
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", it->second.source_id);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_apply_filter_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error,
					      bool replace_settings)
{
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;

	FilterSettingsInput input;
	if (!read_filter_settings_input(params, entry->filter, replace_settings, input, error))
		return false;
	if (filter_settings_are_equal(input, replace_settings)) {
		result.data = make_filter_summary(handle, *entry, parent);
		obs_data_set_obj(result.data.get(), "settings", input.before.get());
		return true;
	}

	v2_sync_filter_observers();
	entry = &filters_.at(handle);
	if (!v2_filter_record_update_baseline(handle, result))
		return fail(error, "internal_error", "filter update observer is not available");
	uint64_t update_serial = 0;
	if (!submit_filter_settings(entry->filter, input, replace_settings, update_serial, error))
		return false;
	result.filter_update_serial = update_serial;
	ObsDataPtr settings(obs_source_get_settings(entry->filter));
	if (!settings)
		return fail(error, "obs_error", replace_settings ? "could not read filter settings after replacement"
										 : "could not read filter settings after update");
	result.data = make_filter_settings_result(handle, *entry, settings.get());
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	return v2_apply_filter_settings(params, result, error, false);
}

bool Engine::v2_filter_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	return v2_apply_filter_settings(params, result, error, true);
}

bool Engine::v2_filter_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	v2_sync_filter_observers();
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	bool enabled = false;
	bool present = false;
	if (!read_bool_field(params, "enabled", enabled, present) || !present)
		return fail(error, "bad_request", "params.enabled must be a boolean");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	if (obs_source_enabled(it->second.filter) == enabled) {
		result.data = make_filter_summary(handle, it->second, source_it->second);
		return true;
	}
	v2_sync_filter_observers();
	obs_source_set_enabled(it->second.filter, enabled);
	result.data = make_filter_summary(handle, it->second, source_it->second);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_get_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	v2_sync_filter_observers();
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", it->second.source_id);
	obs_data_set_bool(data.get(), "enabled", obs_source_enabled(it->second.filter));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_filter_set_order(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	FilterEntry *entry = nullptr;
	obs_source_t *parent = nullptr;
	if (!v2_get_filter_parent(params, handle, entry, parent, error))
		return false;
	long long index = 0;
	bool present = false;
	if (!read_integer_field(params, "index", index, present) || !present || index < 0 ||
	    static_cast<uint64_t>(index) >= obs_source_filter_count(parent))
		return fail(error, "bad_request", "params.index must be an integer within the current filter list");
	const int old_index = obs_source_filter_get_index(parent, entry->filter);
	if (old_index < 0)
		return fail(error, "not_found", "filter is no longer attached to its parent source");
	v2_sync_filter_observers();
	entry = &filters_.at(handle);
	if (old_index != index)
		obs_source_filter_set_index(parent, entry->filter, static_cast<size_t>(index));
	const int new_index = obs_source_filter_get_index(parent, entry->filter);
	const bool changed = new_index != old_index;
	result.data = v2_filter_order_data(entry->source_id, changed ? handle : 0, parent);
	if (changed) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_filter_move_up(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return v2_move_filter(params, result, error, OBS_ORDER_MOVE_UP);
}

bool Engine::v2_filter_move_down(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return v2_move_filter(params, result, error, OBS_ORDER_MOVE_DOWN);
}

bool Engine::v2_filter_move_top(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return v2_move_filter(params, result, error, OBS_ORDER_MOVE_TOP);
}

bool Engine::v2_filter_move_bottom(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return v2_move_filter(params, result, error, OBS_ORDER_MOVE_BOTTOM);
}

} // namespace obs_engine
