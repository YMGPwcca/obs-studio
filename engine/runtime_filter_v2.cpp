#include "runtime.hpp"

#include "events.hpp"
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
};

namespace {

constexpr size_t kMaxFilterKindBytes = 128;
constexpr size_t kMaxFilterNameBytes = 256;
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
		if (end_capture && state.capture &&
		    result_has_filter_event(*state.capture, "filter.removed", batch.handle))
			continue;
		snapshot.batches.push_back(std::move(batch));
	}

	if (end_capture) {
		state.capture = nullptr;
		state.capture_gate.end();
	}
	return snapshot;
}

void publish_deferred_filter_snapshot(DeferredFilterEventSnapshot snapshot, RevisionState::MutationGuard &guard)
{
	if (snapshot.overflow) {
		uint64_t revision = guard.current();
		if (guard.can_commit_mutation())
			revision = guard.commit_mutation();
		if (snapshot.events)
			snapshot.events->require_resync_due_to_overflow(revision);
		std::fprintf(stderr, "obs-engine: deferred filter event queue overflowed; controller resync required\n");
		std::fflush(stderr);
	}

	for (DeferredFilterEventBatch &batch : snapshot.batches) {
		if (!guard.can_commit_mutation()) {
			if (snapshot.events)
				snapshot.events->require_resync_due_to_overflow(guard.current());
			std::fprintf(stderr,
				     "obs-engine: deferred filter events require resync because revision space is exhausted\n");
			std::fflush(stderr);
			return;
		}
		const uint64_t revision = guard.commit_mutation();
		if (snapshot.events) {
			for (RuntimeV2Event &event : batch.events)
				snapshot.events->publish(EngineEventKind::State, event.name, revision, event.data.get());
		}
	}
}

void publish_filter_events(FilterV2State &state, uint64_t handle, std::vector<RuntimeV2Event> generated)
{
	if (generated.empty())
		return;

	EventDispatcher *events = nullptr;
	uint64_t revision = 0;
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting)
			return;

		const SourceEventCaptureRoute route = state.capture_gate.route_for_current_thread();
		if (route == SourceEventCaptureRoute::Capture && state.capture) {
			if (result_has_filter_event(*state.capture, "filter.removed", handle))
				return;
			for (RuntimeV2Event &event : generated) {
				if (!result_has_filter_event(*state.capture, event.name, handle))
					state.capture->events.push_back(std::move(event));
			}
			state.capture->mutated = true;
			return;
		}

		if (route == SourceEventCaptureRoute::Defer) {
			if (state.deferred_overflow)
				return;
			if (generated.size() > kDefaultEventQueueCapacity ||
			    state.deferred_event_count > kDefaultEventQueueCapacity - generated.size()) {
				state.deferred.clear();
				state.deferred_event_count = 0;
				state.deferred_overflow = true;
				return;
			}
			state.deferred_event_count += generated.size();
			state.deferred.push_back(DeferredFilterEventBatch{handle, std::move(generated)});
			return;
		}

		RevisionState *revisions = state.revisions;
		events = state.events;
		if (!revisions || !events)
			return;
		if (!revisions->can_commit_mutation()) {
			std::fprintf(stderr, "obs-engine: filter event requires resync because revision space is exhausted\n");
			std::fflush(stderr);
			events->require_resync_due_to_overflow(revisions->current());
			return;
		}
		revision = revisions->commit_mutation();
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

void collect_filter_signal(FilterV2Observer &observer, FilterSignal signal)
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

	std::vector<RuntimeV2Event> generated;
	try {
		std::lock_guard cache_lock(observer.cache_mutex);
		const std::string current_name = obs_source_get_name(filter) ? obs_source_get_name(filter) : "";
		const bool current_enabled = obs_source_enabled(filter);
		std::string current_settings;
		const bool have_settings = settings_json(filter, current_settings);

		if (signal == FilterSignal::Update && have_settings && current_settings != observer.settings) {
			ObsDataPtr data(obs_data_create());
			set_handle(data.get(), "filter", observer.handle);
			set_handle(data.get(), "source", observer.source_id);
			ObsDataPtr settings(obs_source_get_settings(filter));
			if (settings)
				obs_data_set_obj(data.get(), "settings", settings.get());
			generated.push_back(RuntimeV2Event{"filter.settingsChanged", std::move(data)});
		}

		if (signal == FilterSignal::Rename && current_name != observer.name) {
			ObsDataPtr data(obs_data_create());
			set_handle(data.get(), "filter", observer.handle);
			set_handle(data.get(), "source", observer.source_id);
			obs_data_set_string(data.get(), "name", current_name.c_str());
			obs_data_set_string(data.get(), "previousName", observer.name.c_str());
			generated.push_back(RuntimeV2Event{"filter.renamed", std::move(data)});
		}

		if (signal == FilterSignal::Enabled && current_enabled != observer.enabled) {
			ObsDataPtr data(obs_data_create());
			set_handle(data.get(), "filter", observer.handle);
			set_handle(data.get(), "source", observer.source_id);
			obs_data_set_bool(data.get(), "enabled", current_enabled);
			generated.push_back(RuntimeV2Event{"filter.enabledChanged", std::move(data)});
		}

		observer.name = current_name;
		observer.enabled = current_enabled;
		if (have_settings)
			observer.settings = current_settings;
	} catch (...) {
		obs_source_release(filter);
		throw;
	}

	obs_source_release(filter);
	publish_filter_events(*state, observer.handle, std::move(generated));
}

void filter_update_cb(void *data, calldata_t *)
{
	try {
		collect_filter_signal(*static_cast<FilterV2Observer *>(data), FilterSignal::Update);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: filter update event normalization failed\n");
	}
}

void filter_rename_cb(void *data, calldata_t *)
{
	try {
		collect_filter_signal(*static_cast<FilterV2Observer *>(data), FilterSignal::Rename);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: filter rename event normalization failed\n");
	}
}

void filter_enabled_cb(void *data, calldata_t *)
{
	try {
		collect_filter_signal(*static_cast<FilterV2Observer *>(data), FilterSignal::Enabled);
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
	}
	disconnect_filter_observer(*observer);
	{
		std::lock_guard lock(state->mutex);
		state->retired.push_back(std::move(observer));
	}
}

struct FilterUpdateWaiter {
	std::mutex mutex;
	std::condition_variable cv;
	uint64_t signal_count = 0;
};

void filter_update_settle_cb(void *data, calldata_t *)
{
	auto *waiter = static_cast<FilterUpdateWaiter *>(data);
	{
		std::lock_guard lock(waiter->mutex);
		++waiter->signal_count;
	}
	waiter->cv.notify_one();
}

bool batch_matches_filter_update(const DeferredFilterEventBatch &batch, uint64_t handle,
					 const std::string &expected_settings)
{
	if (batch.handle != handle)
		return false;
	return std::any_of(batch.events.begin(), batch.events.end(), [&](const RuntimeV2Event &event) {
		if (event.name != "filter.settingsChanged" || !event.data)
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
	});
}

bool promote_deferred_filter_update(FilterV2State &state, uint64_t handle, const std::string &expected_settings,
					    RuntimeV2Result &result)
{
	std::lock_guard lock(state.mutex);
	for (auto it = state.deferred.begin(); it != state.deferred.end(); ++it) {
		if (!batch_matches_filter_update(*it, handle, expected_settings))
			continue;
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

bool settle_deferred_filter_update(FilterV2State &state, uint64_t handle, obs_source_t *filter,
					   const std::string &expected_settings, RuntimeV2Result &result)
{
	if (promote_deferred_filter_update(state, handle, expected_settings, result))
		return true;

	signal_handler_t *handler = obs_source_get_signal_handler(filter);
	if (!handler)
		return false;

	FilterUpdateWaiter waiter;
	signal_handler_connect(handler, "update", filter_update_settle_cb, &waiter);
	if (promote_deferred_filter_update(state, handle, expected_settings, result)) {
		signal_handler_disconnect(handler, "update", filter_update_settle_cb, &waiter);
		return true;
	}

	const auto deadline = std::chrono::steady_clock::now() + kFilterUpdateSettleTimeout;
	uint64_t observed_signals = 0;
	for (;;) {
		{
			std::unique_lock lock(waiter.mutex);
			if (!waiter.cv.wait_until(lock, deadline, [&] { return waiter.signal_count != observed_signals; }))
				break;
			observed_signals = waiter.signal_count;
		}
		if (promote_deferred_filter_update(state, handle, expected_settings, result)) {
			signal_handler_disconnect(handler, "update", filter_update_settle_cb, &waiter);
			return true;
		}
	}

	signal_handler_disconnect(handler, "update", filter_update_settle_cb, &waiter);
	return promote_deferred_filter_update(state, handle, expected_settings, result);
}

void mark_filter_settlement_lost(FilterV2State &state)
{
	std::lock_guard lock(state.mutex);
	state.deferred_overflow = true;
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
	filter_v2_state_->capture = &result;
	filter_v2_state_->capture_gate.begin();
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

void Engine::v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source, RuntimeV2Result *result,
					       uint64_t duplicate_of)
{
	(void)duplicate_of;
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
		if (filter_handles_.contains(filter)) {
			obs_source_release(filter);
			continue;
		}

		const uint64_t handle = allocate_handle();
		try {
			filters_.emplace(handle, FilterEntry{source_id, filter});
			filter_handles_.emplace(filter, handle);
		} catch (...) {
			obs_source_release(filter);
			throw;
		}

		if (result) {
			ObsDataPtr event_data = make_filter_summary(handle, filters_.at(handle), source);
			append_event(*result, "filter.created", std::move(event_data));
		}
	}
}

void Engine::v2_sync_filter_observers()
{
	std::unordered_set<obs_source_t *> attached;
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
			if (filter_handles_.contains(filter)) {
				obs_source_release(filter);
				continue;
			}
			const uint64_t handle = allocate_handle();
			filters_.emplace(handle, FilterEntry{source_id, filter});
			filter_handles_.emplace(filter, handle);
		}
	}

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

	if (!filter_v2_state_)
		return;

	std::vector<uint64_t> retire;
	{
		std::lock_guard lock(filter_v2_state_->mutex);
		for (const auto &[handle, _] : filters_) {
			if (!filter_v2_state_->observers.contains(handle))
				retire.push_back(handle);
		}
	}
	for (uint64_t handle : retire) {
		auto it = filters_.find(handle);
		if (it == filters_.end())
			continue;
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
			filter_v2_state_->callback_cv.notify_all();
			filter_v2_state_->revisions = nullptr;
			filter_v2_state_->events = nullptr;
			for (auto &[_, observer] : filter_v2_state_->observers)
				observers.push_back(observer);
			filter_v2_state_->observers.clear();
		}
		for (auto &observer : observers) {
			try {
				disconnect_filter_observer(*observer);
			} catch (...) {
			}
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

void Engine::v2_settle_filter_mutation(obs_data_t *params, RuntimeV2Result &result)
{
	if (!result.mutated || !filter_v2_state_)
		return;
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return;
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return;

	ObsDataPtr current(obs_source_get_settings(it->second.filter));
	const char *json = current ? obs_data_get_json(current.get()) : nullptr;
	const std::string expected = json ? json : "";
	const bool deferred_video = (obs_source_get_output_flags(it->second.filter) & OBS_SOURCE_VIDEO) != 0;
	if (deferred_video &&
	    (expected.empty() || !settle_deferred_filter_update(*filter_v2_state_, handle, it->second.filter, expected,
									result))) {
		std::fprintf(stderr,
			     "obs-engine: timed out settling deferred update for filter %llu; controller resync required\n",
			     static_cast<unsigned long long>(handle));
		std::fflush(stderr);
		mark_filter_settlement_lost(*filter_v2_state_);
	}

	if (!result.data)
		return;
	obs_data_item_t *settings_item = obs_data_item_byname(result.data.get(), "settings");
	if (settings_item) {
		if (obs_data_item_gettype(settings_item) == OBS_DATA_OBJECT && current)
			obs_data_set_obj(result.data.get(), "settings", current.get());
		obs_data_item_release(&settings_item);
	}
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
		obs_data_set_string(entry.get(), "unversionedId", kind);
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
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxFilterKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid filter kind identifier");
	if (!filter_type_exists(kind.c_str()))
		return fail(error, "not_found", "filter kind is not registered");
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
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxFilterKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid filter kind identifier");
	if (!filter_type_exists(kind.c_str()))
		return fail(error, "not_found", "filter kind is not registered");
	ObsDataPtr bridge(obs_data_create());
	ObsDataPtr target(obs_data_create());
	obs_data_set_string(target.get(), "type", "filterKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	obs_data_set_obj(bridge.get(), "target", target.get());
	return v2_properties_get(bridge.get(), result, error);
}

bool Engine::v2_filter_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t source_id = 0;
	if (!read_handle_field(params, "source", source_id))
		return fail(error, "bad_request", "params.source must be a canonical decimal source handle string");
	auto source_it = sources_.find(source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "source handle was not found");
	v2_filter_register_source_filters(source_id, source_it->second);
	v2_sync_filter_observers();

	std::vector<std::pair<int, uint64_t>> ordered;
	for (const auto &[handle, entry] : filters_) {
		if (entry.source_id != source_id)
			continue;
		const int index = obs_source_filter_get_index(source_it->second, entry.filter);
		if (index >= 0)
			ordered.emplace_back(index, handle);
	}
	std::sort(ordered.begin(), ordered.end());
	ObsArrayPtr filters(obs_data_array_create());
	for (const auto &[_, handle] : ordered) {
		ObsDataPtr summary = make_filter_summary(handle, filters_.at(handle), source_it->second);
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
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	result.data = make_filter_summary(handle, it->second, source_it->second);
	return true;
}

bool Engine::v2_filter_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t source_id = 0;
	if (!read_handle_field(params, "source", source_id))
		return fail(error, "bad_request", "params.source must be a canonical decimal source handle string");
	auto source_it = sources_.find(source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "source handle was not found");

	std::string kind;
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxFilterKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid filter kind identifier");
	if (!filter_type_exists(kind.c_str()))
		return fail(error, "not_found", "filter kind is not registered");

	std::string name;
	if (!read_string_field(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (present && (name.empty() || !is_bounded_string(name.c_str(), kMaxFilterNameBytes)))
		return fail(error, "bad_request", "filter name must be a 1-256 byte string");
	ObsDataPtr settings;
	if (!read_object_field(params, "settings", settings, present))
		return fail(error, "bad_request", "params.settings must be an object when present");

	const uint64_t handle = next_handle_;
	const std::string generated_name = "engine-filter-" + std::to_string(handle);
	const char *actual_name = name.empty() ? generated_name.c_str() : name.c_str();
	obs_source_t *filter = obs_source_create_private(kind.c_str(), actual_name, settings.get());
	if (!filter)
		return fail(error, "obs_error", "obs_source_create_private did not create the filter");
	if (obs_source_get_type(filter) != OBS_SOURCE_TYPE_FILTER) {
		obs_source_release(filter);
		return fail(error, "obs_error", "registered filter kind did not create a filter source");
	}
	obs_source_filter_add(source_it->second, filter);
	if (obs_source_filter_get_index(source_it->second, filter) < 0) {
		obs_source_release(filter);
		return fail(error, "incompatible_filter", "libobs did not attach the filter to the source");
	}

	const uint64_t allocated = allocate_handle();
	try {
		filters_.emplace(allocated, FilterEntry{source_id, filter});
		filter_handles_.emplace(filter, allocated);
	} catch (...) {
		obs_source_filter_remove(source_it->second, filter);
		obs_source_release(filter);
		throw;
	}

	result.data = make_filter_summary(allocated, filters_.at(allocated), source_it->second);
	append_event(result, "filter.created", clone_data(result.data.get()));
	result.mutated = true;
	v2_sync_filter_observers();
	return true;
}

bool Engine::v2_filter_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	if (obs_source_filter_get_index(source_it->second, it->second.filter) < 0)
		return fail(error, "not_found", "filter is no longer attached to its parent source");

	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", it->second.source_id);
	result.data = std::move(data);
	append_event(result, "filter.removed", clone_data(result.data.get()));
	remove_filter_observer(filter_v2_state_.get(), handle);
	obs_source_filter_remove(source_it->second, it->second.filter);
	filter_handles_.erase(it->second.filter);
	obs_source_release(it->second.filter);
	filters_.erase(it);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	std::string name;
	bool present = false;
	if (!read_string_field(params, "name", name, present) || !present || name.empty() ||
	    !is_bounded_string(name.c_str(), kMaxFilterNameBytes))
		return fail(error, "bad_request", "params.name must be a 1-256 byte string");
	v2_sync_filter_observers();
	const char *current = obs_source_get_name(it->second.filter);
	if (current && name == current) {
		result.data = make_filter_summary(handle, it->second, source_it->second);
		return true;
	}
	obs_source_set_name(it->second.filter, name.c_str());
	result.data = make_filter_summary(handle, it->second, source_it->second);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	std::string name;
	bool present = false;
	if (!read_string_field(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (present && (name.empty() || !is_bounded_string(name.c_str(), kMaxFilterNameBytes)))
		return fail(error, "bad_request", "filter duplicate name must be a 1-256 byte string");
	const std::string generated_name = std::string(obs_source_get_name(it->second.filter)) + " copy";
	const char *requested_name = name.empty() ? generated_name.c_str() : name.c_str();
	obs_source_t *duplicate = obs_source_duplicate(it->second.filter, requested_name, true);
	if (!duplicate || duplicate == it->second.filter) {
		if (duplicate)
			obs_source_release(duplicate);
		return fail(error, "obs_error", "libobs did not create an independent filter duplicate");
	}
	obs_source_set_enabled(duplicate, obs_source_enabled(it->second.filter));
	obs_source_filter_add(source_it->second, duplicate);
	if (obs_source_filter_get_index(source_it->second, duplicate) < 0) {
		obs_source_release(duplicate);
		return fail(error, "incompatible_filter", "libobs did not attach the duplicated filter");
	}
	const uint64_t duplicate_handle = allocate_handle();
	try {
		filters_.emplace(duplicate_handle, FilterEntry{it->second.source_id, duplicate});
		filter_handles_.emplace(duplicate, duplicate_handle);
	} catch (...) {
		obs_source_filter_remove(source_it->second, duplicate);
		obs_source_release(duplicate);
		throw;
	}
	result.data = make_filter_summary(duplicate_handle, filters_.at(duplicate_handle), source_it->second);
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

bool Engine::v2_filter_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	ObsDataPtr patch;
	bool present = false;
	if (!read_object_field(params, "settings", patch, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	ObsDataPtr before(obs_source_get_settings(it->second.filter));
	ObsDataPtr proposed = clone_data(before.get());
	if (!before || !proposed)
		return fail(error, "obs_error", "could not read filter settings before update");
	obs_data_apply(proposed.get(), patch.get());
	const char *before_json = obs_data_get_json(before.get());
	const char *proposed_json = obs_data_get_json(proposed.get());
	if (before_json && proposed_json && std::strcmp(before_json, proposed_json) == 0) {
		result.data = make_filter_summary(handle, it->second, sources_.at(it->second.source_id));
		obs_data_set_obj(result.data.get(), "settings", before.get());
		return true;
	}
	obs_source_update(it->second.filter, patch.get());
	ObsDataPtr settings(obs_source_get_settings(it->second.filter));
	if (!settings)
		return fail(error, "obs_error", "could not read filter settings after update");
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", it->second.source_id);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	ObsDataPtr replacement;
	bool present = false;
	if (!read_object_field(params, "settings", replacement, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	ObsDataPtr before(obs_source_get_settings(it->second.filter));
	if (!before)
		return fail(error, "obs_error", "could not read filter settings before replacement");
	const char *before_json = obs_data_get_json(before.get());
	const char *replacement_json = obs_data_get_json(replacement.get());
	if (before_json && replacement_json && std::strcmp(before_json, replacement_json) == 0) {
		result.data = make_filter_summary(handle, it->second, sources_.at(it->second.source_id));
		obs_data_set_obj(result.data.get(), "settings", before.get());
		return true;
	}
	obs_source_reset_settings(it->second.filter, replacement.get());
	ObsDataPtr settings(obs_source_get_settings(it->second.filter));
	if (!settings)
		return fail(error, "obs_error", "could not read filter settings after replacement");
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", it->second.source_id);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);
	result.mutated = true;
	return true;
}

bool Engine::v2_filter_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
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
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	long long index = 0;
	bool present = false;
	if (!read_integer_field(params, "index", index, present) || !present || index < 0 ||
	    static_cast<uint64_t>(index) >= obs_source_filter_count(source_it->second))
		return fail(error, "bad_request", "params.index must be an integer within the current filter list");
	const int old_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	if (old_index < 0)
		return fail(error, "not_found", "filter is no longer attached to its parent source");
	v2_sync_filter_observers();
	if (old_index != index)
		obs_source_filter_set_index(source_it->second, it->second.filter, static_cast<size_t>(index));
	const int new_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	result.data = v2_filter_order_data(it->second.source_id, handle, source_it->second);
	if (new_index != old_index) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_filter_move_up(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	const int old_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	if (old_index < 0)
		return fail(error, "not_found", "filter is no longer attached to its parent source");
	obs_source_filter_set_order(source_it->second, it->second.filter, OBS_ORDER_MOVE_UP);
	const int new_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	result.data = v2_filter_order_data(it->second.source_id, handle, source_it->second);
	if (new_index != old_index) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_filter_move_down(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	const int old_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	obs_source_filter_set_order(source_it->second, it->second.filter, OBS_ORDER_MOVE_DOWN);
	const int new_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	result.data = v2_filter_order_data(it->second.source_id, handle, source_it->second);
	if (new_index != old_index) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_filter_move_top(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	const int old_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	obs_source_filter_set_order(source_it->second, it->second.filter, OBS_ORDER_MOVE_TOP);
	const int new_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	result.data = v2_filter_order_data(it->second.source_id, handle, source_it->second);
	if (new_index != old_index) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_filter_move_bottom(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	if (!read_handle_field(params, "filter", handle))
		return fail(error, "bad_request", "params.filter must be a canonical decimal filter handle string");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter handle was not found");
	auto source_it = sources_.find(it->second.source_id);
	if (source_it == sources_.end())
		return fail(error, "not_found", "filter parent source was not found");
	const int old_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	obs_source_filter_set_order(source_it->second, it->second.filter, OBS_ORDER_MOVE_BOTTOM);
	const int new_index = obs_source_filter_get_index(source_it->second, it->second.filter);
	result.data = v2_filter_order_data(it->second.source_id, handle, source_it->second);
	if (new_index != old_index) {
		append_event(result, "filter.orderChanged", clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

} // namespace obs_engine
