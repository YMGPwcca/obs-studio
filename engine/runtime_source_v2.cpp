#include "runtime.hpp"

#include "events.hpp"
#include "revision.hpp"
#include "source_event_capture.hpp"
#include "validation.hpp"

#include <callback/calldata.h>
#include <callback/signal.h>

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obs_engine {
namespace {

constexpr size_t kMaxSourceKindBytes = 128;
constexpr size_t kMaxObjectNameBytes = 256;
constexpr long long kSourceStateVersion = 1;

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "source operation failed";
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
	if (!read_string_field(data, name, text, present) || !present)
		return false;
	return parse_handle_text(text, out);
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
	if (!json)
		return {};
	return ObsDataPtr(obs_data_create_from_json(json));
}

void append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	RuntimeV2Event event;
	event.name = name;
	event.data = std::move(data);
	result.events.push_back(std::move(event));
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

void set_semantic_source_flags(obs_data_t *data, uint32_t flags)
{
	obs_data_set_int(data, "outputFlags", static_cast<long long>(flags));
	obs_data_set_bool(data, "hasVideo", (flags & OBS_SOURCE_VIDEO) != 0);
	obs_data_set_bool(data, "hasAudio", (flags & OBS_SOURCE_AUDIO) != 0);
	obs_data_set_bool(data, "asyncVideo", (flags & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO);
	obs_data_set_bool(data, "customDraw", (flags & OBS_SOURCE_CUSTOM_DRAW) != 0);
	obs_data_set_bool(data, "interaction", (flags & OBS_SOURCE_INTERACTION) != 0);
	obs_data_set_bool(data, "composite", (flags & OBS_SOURCE_COMPOSITE) != 0);
	obs_data_set_bool(data, "doNotDuplicate", (flags & OBS_SOURCE_DO_NOT_DUPLICATE) != 0);
	obs_data_set_bool(data, "deprecated", (flags & OBS_SOURCE_DEPRECATED) != 0);
	obs_data_set_bool(data, "selfMonitorAllowed", (flags & OBS_SOURCE_DO_NOT_SELF_MONITOR) == 0);
	obs_data_set_bool(data, "disabled", (flags & OBS_SOURCE_CAP_DISABLED) != 0);
	obs_data_set_bool(data, "monitorByDefault", (flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0);
	obs_data_set_bool(data, "controllableMedia", (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0);
	obs_data_set_bool(data, "cea708", (flags & OBS_SOURCE_CEA_708) != 0);
	obs_data_set_bool(data, "srgb", (flags & OBS_SOURCE_SRGB) != 0);
	obs_data_set_bool(data, "dontShowPropertiesOnCreate", (flags & OBS_SOURCE_CAP_DONT_SHOW_PROPERTIES) != 0);
	obs_data_set_bool(data, "requiresCanvas", (flags & OBS_SOURCE_REQUIRES_CANVAS) != 0);
}

ObsDataPtr make_flags_data(uint64_t handle, obs_source_t *source)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	set_semantic_source_flags(data.get(), obs_source_get_output_flags(source));
	return data;
}

ObsDataPtr make_dimensions_data(uint64_t handle, obs_source_t *source)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_int(data.get(), "width", static_cast<long long>(obs_source_get_width(source)));
	obs_data_set_int(data.get(), "height", static_cast<long long>(obs_source_get_height(source)));
	return data;
}

ObsDataPtr make_source_summary(uint64_t handle, obs_source_t *source)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_string(data.get(), "name", obs_source_get_name(source));
	obs_data_set_string(data.get(), "kind", obs_source_get_id(source));
	obs_data_set_string(data.get(), "unversionedKind", obs_source_get_unversioned_id(source));
	obs_data_set_bool(data.get(), "active", obs_source_active(source));
	obs_data_set_bool(data.get(), "showing", obs_source_showing(source));
	obs_data_set_int(data.get(), "width", static_cast<long long>(obs_source_get_width(source)));
	obs_data_set_int(data.get(), "height", static_cast<long long>(obs_source_get_height(source)));
	obs_data_set_int(data.get(), "outputFlags", static_cast<long long>(obs_source_get_output_flags(source)));
	return data;
}

ObsDataPtr make_kind_metadata(const char *kind)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "id", kind);
	const char *unversioned = obs_get_latest_input_type_id(kind);
	obs_data_set_string(data.get(), "unversionedId", unversioned ? unversioned : kind);
	const char *display_name = obs_source_get_display_name(kind);
	obs_data_set_string(data.get(), "displayName", display_name ? display_name : kind);
	const uint32_t flags = obs_get_source_output_flags(kind);
	set_semantic_source_flags(data.get(), flags);
	obs_module_t *module = obs_source_get_module(kind);
	if (module) {
		const char *module_file = obs_get_module_file_name(module);
		if (module_file)
			obs_data_set_string(data.get(), "module", module_file);
	}
	obs_data_set_int(data.get(), "moduleLoadState", static_cast<long long>(obs_source_load_state(kind)));
	return data;
}

ObsDataPtr make_state_snapshot(uint64_t handle, obs_source_t *source, RuntimeV2Error &error)
{
	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings) {
		fail(error, "obs_error", "could not read source settings");
		return {};
	}
	ObsDataPtr settings_copy = clone_data(settings.get());
	if (!settings_copy) {
		fail(error, "internal_error", "could not clone source settings");
		return {};
	}

	ObsDataPtr state(obs_data_create());
	obs_data_set_int(state.get(), "version", kSourceStateVersion);
	obs_data_set_string(state.get(), "kind", obs_source_get_id(source));
	obs_data_set_string(state.get(), "name", obs_source_get_name(source));
	obs_data_set_obj(state.get(), "settings", settings_copy.get());

	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_obj(data.get(), "state", state.get());
	return data;
}

struct SourceStateInput {
	std::string kind;
	std::string name;
	ObsDataPtr settings;
};

bool read_source_state_version(obs_data_t *state, RuntimeV2Error &error)
{
	long long version = 0;
	bool present = false;
	if (!read_integer_field(state, "version", version, present) || !present || version != kSourceStateVersion)
		return fail(error, "bad_request", "source state version is unsupported");
	return true;
}

bool read_source_state_kind(obs_data_t *state, obs_source_t *source, std::string &kind, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(state, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "source state kind is invalid");
	if (kind != obs_source_get_id(source))
		return fail(error, "bad_request", "source state kind does not match the target source");
	return true;
}

bool read_source_state_name(obs_data_t *state, std::string &name, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(state, "name", name, present) || !present || name.empty() ||
	    !is_bounded_string(name.c_str(), kMaxObjectNameBytes))
		return fail(error, "bad_request", "source state name must be a 1-256 byte string");
	return true;
}

bool read_source_state_settings(obs_data_t *state, ObsDataPtr &settings, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_object_field(state, "settings", settings, present) || !present)
		return fail(error, "bad_request", "source state settings object is required");
	return true;
}

bool read_source_duplicate_name(obs_data_t *params, std::string &name, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (present && (name.empty() || !is_bounded_string(name.c_str(), kMaxObjectNameBytes)))
		return fail(error, "bad_request", "source duplicate name must be between 1 and 256 bytes");
	return true;
}

ObsDataPtr make_source_duplicate_data(uint64_t duplicate_handle, uint64_t original_handle, obs_source_t *duplicate)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", duplicate_handle);
	obs_data_set_string(data.get(), "name", obs_source_get_name(duplicate));
	obs_data_set_string(data.get(), "kind", obs_source_get_id(duplicate));
	set_handle(data.get(), "duplicateOf", original_handle);
	return data;
}

bool read_source_state_input(obs_data_t *params, obs_source_t *source, SourceStateInput &input,
				     RuntimeV2Error &error)
{
	ObsDataPtr state;
	bool present = false;
	if (!read_object_field(params, "state", state, present) || !present)
		return fail(error, "bad_request", "params.state object is required");
	return read_source_state_version(state.get(), error) && read_source_state_kind(state.get(), source, input.kind, error) &&
	       read_source_state_name(state.get(), input.name, error) &&
	       read_source_state_settings(state.get(), input.settings, error);
}

} // namespace

struct SourceV2Observer;

struct DeferredSourceEventBatch {
	uint64_t handle = 0;
	std::vector<RuntimeV2Event> events;
};

struct SourceV2State {
	std::mutex mutex;
	std::condition_variable callback_cv;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	RuntimeV2Result *capture = nullptr;
	SourceEventCaptureGate capture_gate;
	std::deque<DeferredSourceEventBatch> deferred;
	size_t deferred_event_count = 0;
	size_t direct_callbacks_inflight = 0;
	bool deferred_overflow = false;
	bool accepting = false;
	std::unordered_map<uint64_t, std::shared_ptr<SourceV2Observer>> observers;
	std::vector<std::shared_ptr<SourceV2Observer>> retired;
};

struct SourceV2Observer {
	SourceV2State *state = nullptr;
	uint64_t handle = 0;
	obs_weak_source_t *weak = nullptr;
	std::mutex cache_mutex;
	std::string name;
	uint32_t flags = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	bool active = false;
	bool showing = false;
};

namespace {

enum class SourceSignal { Update, Rename, Active, Showing, Flags };

class SourceCallbackScope {
public:
	SourceCallbackScope(SourceV2State &state, uint64_t handle) : state_(state)
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
			   result_has_source_event(*state_.capture, "source.removed", handle)) {
			suppressed_ = true;
		}
	}

	~SourceCallbackScope()
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
	SourceV2State &state_;
	bool accepted_ = false;
	bool counted_direct_ = false;
	bool suppressed_ = false;
};

struct DeferredSourceEventSnapshot {
	std::deque<DeferredSourceEventBatch> batches;
	EventDispatcher *events = nullptr;
	bool overflow = false;
};

void clear_deferred_source_events(SourceV2State &state)
{
	state.deferred.clear();
	state.deferred_event_count = 0;
	state.deferred_overflow = false;
}

DeferredSourceEventSnapshot take_deferred_source_events(SourceV2State &state, bool wait_for_pre_capture,
							 bool end_capture)
{
	DeferredSourceEventSnapshot snapshot;
	std::unique_lock lock(state.mutex);
	if (wait_for_pre_capture) {
		state.callback_cv.wait(lock, [&] { return !state.accepting || state.direct_callbacks_inflight == 0; });
	}

	if (!state.accepting) {
		clear_deferred_source_events(state);
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
		DeferredSourceEventBatch batch = std::move(state.deferred.front());
		state.deferred.pop_front();
		state.deferred_event_count -= batch.events.size();
		if (end_capture && state.capture &&
		    result_has_source_event(*state.capture, "source.removed", batch.handle))
			continue;
		snapshot.batches.push_back(std::move(batch));
	}

	if (end_capture) {
		state.capture = nullptr;
		state.capture_gate.end();
	}
	return snapshot;
}

void publish_deferred_source_snapshot(DeferredSourceEventSnapshot snapshot, RevisionState::MutationGuard &guard)
{
	if (snapshot.overflow) {
		uint64_t revision = guard.current();
		if (guard.can_commit_mutation())
			revision = guard.commit_mutation();
		if (snapshot.events)
			snapshot.events->require_resync_due_to_overflow(revision);
		std::fprintf(stderr, "obs-engine: deferred source event queue overflowed; controller resync required\n");
		std::fflush(stderr);
	}

	for (DeferredSourceEventBatch &batch : snapshot.batches) {
		if (!guard.can_commit_mutation()) {
			if (snapshot.events)
				snapshot.events->require_resync_due_to_overflow(guard.current());
			std::fprintf(stderr,
				     "obs-engine: deferred source events require resync because revision space is exhausted\n");
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

bool capture_source_events_locked(SourceV2State &state, uint64_t handle, std::vector<RuntimeV2Event> &generated)
{
	if (!state.capture)
		return false;
	if (result_has_source_event(*state.capture, "source.removed", handle))
		return true;
	for (RuntimeV2Event &event : generated) {
		if (!result_has_source_event(*state.capture, event.name, handle))
			state.capture->events.push_back(std::move(event));
	}
	state.capture->mutated = true;
	return true;
}

bool queue_deferred_source_events_locked(SourceV2State &state, uint64_t handle,
					       std::vector<RuntimeV2Event> generated)
{
	if (state.deferred_overflow)
		return true;
	if (generated.size() > kDefaultEventQueueCapacity ||
	    state.deferred_event_count > kDefaultEventQueueCapacity - generated.size()) {
		state.deferred.clear();
		state.deferred_event_count = 0;
		state.deferred_overflow = true;
		return true;
	}
	state.deferred_event_count += generated.size();
	state.deferred.push_back(DeferredSourceEventBatch{handle, std::move(generated)});
	return true;
}

bool commit_direct_source_event_locked(SourceV2State &state, RevisionState *&revisions, EventDispatcher *&events,
					       uint64_t &revision)
{
	revisions = state.revisions;
	events = state.events;
	if (!revisions || !events)
		return false;
	if (!revisions->can_commit_mutation()) {
		std::fprintf(stderr, "obs-engine: source event requires resync because revision space is exhausted\n");
		std::fflush(stderr);
		events->require_resync_due_to_overflow(revisions->current());
		return false;
	}

	// Keep the source bridge mutex across the revision commit. Runtime mutating
	// requests establish the capture gate before taking the revision guard. A
	// callback therefore either commits before capture or observes capture and
	// defers; it never holds a libobs signal mutex waiting behind the request.
	revision = revisions->commit_mutation();
	return true;
}

void publish_source_events(SourceV2State &state, uint64_t handle, std::vector<RuntimeV2Event> generated)
{
	if (generated.empty())
		return;

	EventDispatcher *events = nullptr;
	RevisionState *revisions = nullptr;
	uint64_t revision = 0;
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting)
			return;

		const SourceEventCaptureRoute route = state.capture_gate.route_for_current_thread();
		if (route == SourceEventCaptureRoute::Capture && capture_source_events_locked(state, handle, generated))
			return;
		if (route == SourceEventCaptureRoute::Defer) {
			queue_deferred_source_events_locked(state, handle, std::move(generated));
			return;
		}

		if (!commit_direct_source_event_locked(state, revisions, events, revision))
			return;
	}

	for (RuntimeV2Event &event : generated)
		events->publish(EngineEventKind::State, event.name, revision, event.data.get());
}

ObsDataPtr make_source_event_data(const SourceV2Observer &observer)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", observer.handle);
	return data;
}

void append_source_settings_event(SourceV2Observer &observer, SourceSignal signal, obs_source_t *source,
					  std::vector<RuntimeV2Event> &generated)
{
	if (signal != SourceSignal::Update)
		return;
	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings)
		return;
	ObsDataPtr data = make_source_event_data(observer);
	obs_data_set_obj(data.get(), "settings", settings.get());
	generated.push_back(RuntimeV2Event{"source.settingsChanged", std::move(data)});
}

void append_source_rename_event(SourceV2Observer &observer, SourceSignal signal, const std::string &current_name,
					std::vector<RuntimeV2Event> &generated)
{
	if (signal != SourceSignal::Rename || current_name == observer.name)
		return;
	ObsDataPtr data = make_source_event_data(observer);
	obs_data_set_string(data.get(), "name", current_name.c_str());
	obs_data_set_string(data.get(), "previousName", observer.name.c_str());
	generated.push_back(RuntimeV2Event{"source.renamed", std::move(data)});
}

void append_source_flags_event(SourceV2Observer &observer, obs_source_t *source, uint32_t current_flags,
					std::vector<RuntimeV2Event> &generated)
{
	if (current_flags != observer.flags)
		generated.push_back(RuntimeV2Event{"source.flagsChanged", make_flags_data(observer.handle, source)});
}

void append_source_active_event(SourceV2Observer &observer, bool current_active,
					std::vector<RuntimeV2Event> &generated)
{
	if (current_active == observer.active)
		return;
	ObsDataPtr data = make_source_event_data(observer);
	obs_data_set_bool(data.get(), "active", current_active);
	generated.push_back(RuntimeV2Event{"source.activeChanged", std::move(data)});
}

void append_source_showing_event(SourceV2Observer &observer, bool current_showing,
					 std::vector<RuntimeV2Event> &generated)
{
	if (current_showing == observer.showing)
		return;
	ObsDataPtr data = make_source_event_data(observer);
	obs_data_set_bool(data.get(), "showing", current_showing);
	generated.push_back(RuntimeV2Event{"source.showingChanged", std::move(data)});
}

void append_source_dimensions_event(SourceV2Observer &observer, obs_source_t *source, uint32_t current_width,
					    uint32_t current_height, std::vector<RuntimeV2Event> &generated)
{
	if (current_width != observer.width || current_height != observer.height)
		generated.push_back(RuntimeV2Event{"source.dimensionsChanged", make_dimensions_data(observer.handle, source)});
}

void collect_source_state_events(SourceV2Observer &observer, SourceSignal signal, obs_source_t *source,
					 std::vector<RuntimeV2Event> &generated)
{
	std::lock_guard cache_lock(observer.cache_mutex);
	const char *name = obs_source_get_name(source);
	const std::string current_name = name ? name : "";
	const uint32_t current_flags = obs_source_get_output_flags(source);
	const uint32_t current_width = obs_source_get_width(source);
	const uint32_t current_height = obs_source_get_height(source);
	const bool current_active = obs_source_active(source);
	const bool current_showing = obs_source_showing(source);

	append_source_settings_event(observer, signal, source, generated);
	append_source_rename_event(observer, signal, current_name, generated);
	append_source_flags_event(observer, source, current_flags, generated);
	append_source_active_event(observer, current_active, generated);
	append_source_showing_event(observer, current_showing, generated);
	append_source_dimensions_event(observer, source, current_width, current_height, generated);

	observer.name = current_name;
	observer.flags = current_flags;
	observer.width = current_width;
	observer.height = current_height;
	observer.active = current_active;
	observer.showing = current_showing;
}

void collect_source_signal(SourceV2Observer &observer, SourceSignal signal)
{
	SourceV2State *state = observer.state;
	if (!state)
		return;

	SourceCallbackScope callback_scope(*state, observer.handle);
	if (!callback_scope.accepted() || callback_scope.suppressed())
		return;

	obs_source_t *source = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (!source)
		return;

	std::vector<RuntimeV2Event> generated;
	try {
		collect_source_state_events(observer, signal, source, generated);
	} catch (...) {
		obs_source_release(source);
		throw;
	}

	obs_source_release(source);
	publish_source_events(*state, observer.handle, std::move(generated));
}

void source_update_cb(void *data, calldata_t *)
{
	try {
		collect_source_signal(*static_cast<SourceV2Observer *>(data), SourceSignal::Update);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: source update event normalization failed\n");
	}
}

void source_rename_cb(void *data, calldata_t *)
{
	try {
		collect_source_signal(*static_cast<SourceV2Observer *>(data), SourceSignal::Rename);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: source rename event normalization failed\n");
	}
}

void source_active_cb(void *data, calldata_t *)
{
	try {
		collect_source_signal(*static_cast<SourceV2Observer *>(data), SourceSignal::Active);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: source active event normalization failed\n");
	}
}

void source_showing_cb(void *data, calldata_t *)
{
	try {
		collect_source_signal(*static_cast<SourceV2Observer *>(data), SourceSignal::Showing);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: source showing event normalization failed\n");
	}
}

void source_flags_cb(void *data, calldata_t *)
{
	try {
		collect_source_signal(*static_cast<SourceV2Observer *>(data), SourceSignal::Flags);
	} catch (...) {
		std::fprintf(stderr, "obs-engine: source flags event normalization failed\n");
	}
}

void connect_observer(SourceV2Observer &observer, obs_source_t *source)
{
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	signal_handler_connect(handler, "update", source_update_cb, &observer);
	signal_handler_connect(handler, "rename", source_rename_cb, &observer);
	signal_handler_connect(handler, "activate", source_active_cb, &observer);
	signal_handler_connect(handler, "deactivate", source_active_cb, &observer);
	signal_handler_connect(handler, "show", source_showing_cb, &observer);
	signal_handler_connect(handler, "hide", source_showing_cb, &observer);
	signal_handler_connect(handler, "update_flags", source_flags_cb, &observer);
}

void disconnect_observer(SourceV2Observer &observer)
{
	obs_source_t *source = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (source) {
		signal_handler_t *handler = obs_source_get_signal_handler(source);
		signal_handler_disconnect(handler, "update", source_update_cb, &observer);
		signal_handler_disconnect(handler, "rename", source_rename_cb, &observer);
		signal_handler_disconnect(handler, "activate", source_active_cb, &observer);
		signal_handler_disconnect(handler, "deactivate", source_active_cb, &observer);
		signal_handler_disconnect(handler, "show", source_showing_cb, &observer);
		signal_handler_disconnect(handler, "hide", source_showing_cb, &observer);
		signal_handler_disconnect(handler, "update_flags", source_flags_cb, &observer);
		obs_source_release(source);
	}
	if (observer.weak) {
		obs_weak_source_release(observer.weak);
		observer.weak = nullptr;
	}
}

} // namespace

bool Engine::v2_get_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
				   RuntimeV2Error &error) const
{
	if (!read_handle_field(params, "source", handle))
		return fail(error, "bad_request", "params.source must be a canonical decimal handle string");
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return fail(error, "not_found", "source handle was not found");
	source = it->second;
	return true;
}

bool Engine::v2_store_source_duplicate(uint64_t handle, obs_source_t *duplicate, RuntimeV2Error &error)
{
	const auto [_, inserted] = sources_.emplace(handle, duplicate);
	if (!inserted) {
		obs_source_release(duplicate);
		return fail(error, "internal_error", "source handle collision");
	}
	// libobs duplicates attached filters as nested source state. Register
	// those copies for later filter.list discovery, but do not synthesize
	// filter.created events into the already-accepted source.duplicate wire
	// contract. The caller owns rollback if registration throws.
	v2_filter_register_source_filters(handle, duplicate);
	return true;
}

void Engine::v2_add_source_observer(uint64_t handle, obs_source_t *source,
					    std::vector<std::shared_ptr<SourceV2Observer>> &retire)
{
	auto observer = std::make_shared<SourceV2Observer>();
	observer->state = source_v2_state_.get();
	observer->handle = handle;
	observer->weak = obs_source_get_weak_source(source);
	observer->name = obs_source_get_name(source) ? obs_source_get_name(source) : "";
	observer->flags = obs_source_get_output_flags(source);
	observer->width = obs_source_get_width(source);
	observer->height = obs_source_get_height(source);
	observer->active = obs_source_active(source);
	observer->showing = obs_source_showing(source);
	connect_observer(*observer, source);

	bool keep = false;
	{
		std::lock_guard lock(source_v2_state_->mutex);
		if (source_v2_state_->accepting && sources_.contains(handle) &&
		    !source_v2_state_->observers.contains(handle)) {
			source_v2_state_->observers.emplace(handle, observer);
			keep = true;
		}
	}
	if (!keep) {
		disconnect_observer(*observer);
		retire.push_back(std::move(observer));
	}
}

void Engine::v2_bind_source_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!source_v2_state_)
		source_v2_state_ = std::make_shared<SourceV2State>();
	{
		std::lock_guard lock(source_v2_state_->mutex);
		source_v2_state_->revisions = revisions;
		source_v2_state_->events = events;
		source_v2_state_->accepting = revisions && events;
	}
	v2_bind_media_events(revisions, events);
	v2_bind_audio_events(revisions, events);
	v2_bind_hotkey_events(revisions, events);
	v2_bind_encoder_events(revisions, events);
	v2_bind_output_events(revisions, events);
	v2_bind_filter_events(revisions, events);
	v2_bind_transition_events(revisions, events);
}

void Engine::v2_begin_event_capture(RuntimeV2Result &result)
{
	if (source_v2_state_) {
		std::lock_guard lock(source_v2_state_->mutex);
		source_v2_state_->capture = &result;
		source_v2_state_->capture_gate.begin();
	}
	v2_begin_media_event_capture(result);
	v2_begin_audio_event_capture(result);
	v2_begin_filter_event_capture(result);
	v2_begin_output_event_capture(result);
}

void Engine::v2_wait_for_event_capture_callbacks()
{
	if (source_v2_state_) {
		std::unique_lock lock(source_v2_state_->mutex);
		source_v2_state_->callback_cv.wait(lock, [&] {
			return !source_v2_state_->accepting || source_v2_state_->direct_callbacks_inflight == 0;
		});
	}
	v2_wait_for_media_event_callbacks();
	v2_wait_for_audio_event_callbacks();
	v2_wait_for_filter_event_callbacks();
	v2_wait_for_output_event_callbacks();
}

void Engine::v2_end_event_capture() noexcept
{
	if (source_v2_state_) {
		RevisionState *revisions = nullptr;
		EventDispatcher *events = nullptr;
		bool lost_deferred = false;
		{
			std::lock_guard lock(source_v2_state_->mutex);
			source_v2_state_->capture = nullptr;
			source_v2_state_->capture_gate.end();
			lost_deferred = source_v2_state_->deferred_overflow || !source_v2_state_->deferred.empty();
			if (lost_deferred) {
				clear_deferred_source_events(*source_v2_state_);
				revisions = source_v2_state_->revisions;
				events = source_v2_state_->events;
			}
		}

		if (lost_deferred && revisions && events) {
			std::fprintf(stderr, "obs-engine: deferred source events abandoned; forcing controller resync\n");
			std::fflush(stderr);
			events->require_resync_due_to_overflow(revisions->current());
		}
	}
	v2_end_media_event_capture();
	v2_end_audio_event_capture();
	v2_end_filter_event_capture();
	v2_end_output_event_capture();
}

void Engine::v2_drain_deferred_source_events(RevisionState::MutationGuard &guard)
{
	if (source_v2_state_) {
		DeferredSourceEventSnapshot snapshot = take_deferred_source_events(*source_v2_state_, true, false);
		publish_deferred_source_snapshot(std::move(snapshot), guard);
	}
	v2_drain_deferred_media_events(guard);
	v2_drain_deferred_audio_events(guard);
	v2_drain_deferred_filter_events(guard);
	v2_drain_deferred_output_events(guard);
}

void Engine::v2_flush_deferred_source_events(RevisionState::MutationGuard &guard)
{
	if (source_v2_state_) {
		DeferredSourceEventSnapshot snapshot = take_deferred_source_events(*source_v2_state_, false, true);
		publish_deferred_source_snapshot(std::move(snapshot), guard);
	}
	v2_flush_deferred_media_events(guard);
	v2_flush_deferred_audio_events(guard);
	v2_flush_deferred_filter_events(guard);
	v2_flush_deferred_output_events(guard);
}

void Engine::v2_sync_source_observers()
{
	if (!source_v2_state_)
		return;

	std::vector<std::shared_ptr<SourceV2Observer>> retire;
	std::vector<std::pair<uint64_t, obs_source_t *>> add;
	{
		std::lock_guard lock(source_v2_state_->mutex);
		if (!source_v2_state_->accepting)
			return;
		for (const auto &[handle, source] : sources_) {
			if (!source_v2_state_->observers.contains(handle))
				add.emplace_back(handle, source);
		}
		for (auto it = source_v2_state_->observers.begin(); it != source_v2_state_->observers.end();) {
			if (!sources_.contains(it->first)) {
				retire.push_back(it->second);
				it = source_v2_state_->observers.erase(it);
			} else {
				++it;
			}
		}
	}

	for (const auto &[handle, source] : add) {
		v2_add_source_observer(handle, source, retire);
	}

	for (auto &observer : retire)
		disconnect_observer(*observer);
	if (!retire.empty()) {
		std::lock_guard lock(source_v2_state_->mutex);
		source_v2_state_->retired.insert(source_v2_state_->retired.end(), retire.begin(), retire.end());
	}
	v2_sync_media_observers();
	v2_sync_audio_observers();
	v2_sync_filter_observers();
}

void Engine::v2_prepare_shutdown() noexcept
{
	v2_prepare_transition_shutdown();
	v2_prepare_filter_shutdown();
	v2_prepare_media_shutdown();
	v2_prepare_output_shutdown();
	v2_prepare_service_shutdown();
	v2_prepare_encoder_group_shutdown();
	v2_prepare_encoder_shutdown();
	v2_prepare_hotkey_shutdown();
	v2_prepare_audio_shutdown();
	if (!source_v2_state_)
		return;

	std::vector<std::shared_ptr<SourceV2Observer>> observers;
	{
		std::lock_guard lock(source_v2_state_->mutex);
		source_v2_state_->accepting = false;
		source_v2_state_->capture = nullptr;
		source_v2_state_->capture_gate.end();
		clear_deferred_source_events(*source_v2_state_);
		source_v2_state_->callback_cv.notify_all();
		source_v2_state_->revisions = nullptr;
		source_v2_state_->events = nullptr;
		for (auto &[_, observer] : source_v2_state_->observers)
			observers.push_back(observer);
		source_v2_state_->observers.clear();
	}

	for (auto &observer : observers) {
		try {
			disconnect_observer(*observer);
		} catch (...) {
		}
	}
	if (!observers.empty()) {
		std::lock_guard lock(source_v2_state_->mutex);
		source_v2_state_->retired.insert(source_v2_state_->retired.end(), observers.begin(), observers.end());
	}
}

bool Engine::v2_source_kind_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid source kind identifier");
	if (!input_type_exists(kind.c_str()))
		return fail(error, "not_found", "source kind is not registered");
	result.data = make_kind_metadata(kind.c_str());
	return true;
}

bool Engine::v2_source_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid source kind identifier");
	if (!input_type_exists(kind.c_str()))
		return fail(error, "not_found", "source kind is not registered");

	ObsDataPtr bridge(obs_data_create());
	ObsDataPtr target(obs_data_create());
	obs_data_set_string(target.get(), "type", "sourceKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	obs_data_set_obj(bridge.get(), "target", target.get());
	return v2_properties_get(bridge.get(), result, error);
}

bool Engine::v2_source_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<uint64_t> handles;
	handles.reserve(sources_.size());
	for (const auto &[handle, _] : sources_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());

	ObsArrayPtr sources(obs_data_array_create());
	for (uint64_t handle : handles) {
		auto it = sources_.find(handle);
		if (it != sources_.end()) {
			ObsDataPtr entry = make_source_summary(handle, it->second);
			obs_data_array_push_back(sources.get(), entry.get());
		}
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "sources", sources.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	result.data = make_source_summary(handle, source);
	return true;
}

bool Engine::v2_source_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_DO_NOT_DUPLICATE) != 0)
		return fail(error, "not_available", "source kind does not support independent duplication");

	std::string requested_name;
	if (!read_source_duplicate_name(params, requested_name, error))
		return false;

	const uint64_t duplicate_handle = allocate_handle();
	const std::string generated_name = std::string(obs_source_get_name(source)) + " copy";
	const char *name = requested_name.empty() ? generated_name.c_str() : requested_name.c_str();
	obs_source_t *duplicate = obs_source_duplicate(source, name, true);
	if (!duplicate || duplicate == source) {
		if (duplicate)
			obs_source_release(duplicate);
		return fail(error, "obs_error", "libobs did not create an independent source duplicate");
	}

	try {
		result.data = make_source_duplicate_data(duplicate_handle, handle, duplicate);
		append_event(result, "source.created", clone_data(result.data.get()));
		if (!v2_store_source_duplicate(duplicate_handle, duplicate, error))
			return false;
	} catch (...) {
		v2_filter_forget_source(duplicate_handle);
		sources_.erase(duplicate_handle);
		obs_source_release(duplicate);
		throw;
	}

	result.mutated = true;
	return true;
}

bool Engine::v2_source_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	std::string name;
	bool present = false;
	if (!read_string_field(params, "name", name, present) || !present || name.empty() ||
	    !is_bounded_string(name.c_str(), kMaxObjectNameBytes))
		return fail(error, "bad_request", "params.name must be a 1-256 byte string");

	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_string(data.get(), "name", name.c_str());
	result.data = std::move(data);

	const char *current = obs_source_get_name(source);
	if (current && name == current)
		return true;
	obs_source_set_name(source, name.c_str());
	result.mutated = true;
	return true;
}

bool Engine::v2_source_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	ObsDataPtr replacement;
	bool present = false;
	if (!read_object_field(params, "settings", replacement, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");

	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings)
		return fail(error, "obs_error", "could not read source settings before replacement");
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);

	obs_source_reset_settings(source, replacement.get());
	result.mutated = true;
	return true;
}

bool Engine::v2_source_reset_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	ObsDataPtr defaults(obs_get_source_defaults(obs_source_get_id(source)));
	if (!defaults)
		return fail(error, "obs_error", "source kind did not provide defaults");
	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings)
		return fail(error, "obs_error", "could not read source settings before reset");
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);

	obs_source_reset_settings(source, defaults.get());
	result.mutated = true;
	return true;
}

bool Engine::v2_source_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	ObsDataPtr bridge(obs_data_create());
	ObsDataPtr target(obs_data_create());
	obs_data_set_string(target.get(), "type", "source");
	set_handle(target.get(), "source", handle);
	obs_data_set_obj(bridge.get(), "target", target.get());
	return v2_properties_get(bridge.get(), result, error);
}

bool Engine::v2_source_get_flags(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	result.data = make_flags_data(handle, source);
	return true;
}

bool Engine::v2_source_get_dimensions(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	result.data = make_dimensions_data(handle, source);
	return true;
}

bool Engine::v2_source_get_active(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "active", obs_source_active(source));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_get_showing(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "showing", obs_source_showing(source));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings)
		return fail(error, "obs_error", "could not read source settings");
	ObsDataPtr data = make_source_summary(handle, source);
	ObsDataPtr flags = make_flags_data(handle, source);
	obs_data_erase(flags.get(), "source");
	ObsDataPtr dimensions = make_dimensions_data(handle, source);
	obs_data_erase(dimensions.get(), "source");
	obs_data_set_obj(data.get(), "flags", flags.get());
	obs_data_set_obj(data.get(), "dimensions", dimensions.get());
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_get_missing_files(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	obs_missing_files_t *missing = obs_source_get_missing_files(source);
	if (!missing)
		return fail(error, "obs_error", "could not query source missing files");

	ObsDataPtr data(obs_data_create());
	ObsArrayPtr files(obs_data_array_create());
	try {
		const size_t count = obs_missing_files_count(missing);
		for (size_t index = 0; index < count; ++index) {
			obs_missing_file_t *file = obs_missing_files_get_file(missing, static_cast<int>(index));
			if (!file)
				continue;
			ObsDataPtr entry(obs_data_create());
			const char *path = obs_missing_file_get_path(file);
			const char *source_name = obs_missing_file_get_source_name(file);
			obs_data_set_string(entry.get(), "path", path ? path : "");
			if (source_name)
				obs_data_set_string(entry.get(), "sourceName", source_name);
			obs_data_array_push_back(files.get(), entry.get());
		}
		set_handle(data.get(), "source", handle);
		obs_data_set_array(data.get(), "files", files.get());
		obs_data_set_int(data.get(), "count", static_cast<long long>(obs_data_array_count(files.get())));
	} catch (...) {
		obs_missing_files_destroy(missing);
		throw;
	}
	obs_missing_files_destroy(missing);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_refresh(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	obs_source_update_properties(source);
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "refreshed", true);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_save_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;
	result.data = make_state_snapshot(handle, source, error);
	return static_cast<bool>(result.data);
}

bool Engine::v2_source_load_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_source(params, handle, source, error))
		return false;

	SourceStateInput input;
	if (!read_source_state_input(params, source, input, error))
		return false;

	ObsDataPtr live_settings(obs_source_get_settings(source));
	if (!live_settings)
		return fail(error, "obs_error", "could not read source settings before state restore");
	ObsDataPtr state_response(obs_data_create());
	obs_data_set_int(state_response.get(), "version", kSourceStateVersion);
	obs_data_set_string(state_response.get(), "kind", input.kind.c_str());
	obs_data_set_string(state_response.get(), "name", input.name.c_str());
	obs_data_set_obj(state_response.get(), "settings", live_settings.get());
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_obj(data.get(), "state", state_response.get());
	result.data = std::move(data);

	const char *current_name = obs_source_get_name(source);
	if (!current_name || input.name != current_name)
		obs_source_set_name(source, input.name.c_str());
	obs_source_reset_settings(source, input.settings.get());
	result.mutated = true;
	return true;
}

} // namespace obs_engine
