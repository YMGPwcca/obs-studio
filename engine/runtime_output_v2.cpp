#include "runtime.hpp"

#include "events.hpp"
#include "properties.hpp"
#include "runtime_phase2_common.hpp"
#include "source_event_capture.hpp"
#include "validation.hpp"

#include <callback/calldata.h>
#include <callback/signal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

#include <util/bmem.h>

namespace obs_engine {

enum class OutputSignalKind {
	Starting,
	Started,
	Stopping,
	Stopped,
	Activated,
	Deactivated,
	Reconnect,
	ReconnectSuccess,
	Paused,
	Unpaused,
};

struct OutputSignalHook {
	OutputV2Observer *observer = nullptr;
	OutputSignalKind kind = OutputSignalKind::Starting;
};

struct DeferredOutputEventBatch {
	std::vector<RuntimeV2Event> events;
};

struct OutputV2State {
	Engine *engine = nullptr;
	std::mutex mutex;
	std::condition_variable callback_cv;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	RuntimeV2Result *capture = nullptr;
	SourceEventCaptureGate capture_gate;
	std::deque<DeferredOutputEventBatch> deferred;
	size_t deferred_event_count = 0;
	size_t direct_callbacks_inflight = 0;
	bool deferred_overflow = false;
	bool accepting = false;
};

struct OutputV2Observer {
	std::mutex mutex;
	std::condition_variable cv;
	std::array<OutputSignalHook, 10> hooks{};
	OutputV2State *state = nullptr;
	uint64_t handle = 0;
	obs_weak_output_t *weak = nullptr;
	bool accepting = true;
	bool starting = false;
	bool stopping = false;
	bool reconnecting = false;
	bool paused = false;
	bool published_active = false;
	bool published_starting = false;
	bool published_stopping = false;
	bool published_reconnecting = false;
	bool published_paused = false;
	bool reconnect_enabled = true;
	uint64_t lifecycle_generation = 0;
	int last_stop_code = 0;
};

namespace {

constexpr size_t kMaxOutputKindBytes = 128;
constexpr size_t kMaxOutputNameBytes = 256;
constexpr size_t kMaxOutputErrorBytes = 4096;
constexpr uint32_t kMaxOutputDelaySeconds = 3600;
constexpr int kMaxReconnectRetries = 100;
constexpr int kMaxReconnectDelaySeconds = 3600;
constexpr std::chrono::milliseconds kOutputSettlementTimeout{2000};

struct ObsPropertiesDeleter {
	void operator()(obs_properties_t *properties) const
	{
		if (properties)
			obs_properties_destroy(properties);
	}
};

using ObsPropertiesPtr = std::unique_ptr<obs_properties_t, ObsPropertiesDeleter>;

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "output operation failed";
	return false;
}

const char *module_load_state_name(enum obs_module_load_state state)
{
	switch (state) {
	case OBS_MODULE_ENABLED:
		return "enabled";
	case OBS_MODULE_MISSING:
		return "missing";
	case OBS_MODULE_DISABLED:
		return "disabled";
	case OBS_MODULE_DISABLED_SAFE:
		return "disabledSafe";
	case OBS_MODULE_FAILED_TO_OPEN:
		return "failedToOpen";
	case OBS_MODULE_FAILED_TO_INITIALIZE:
		return "failedToInitialize";
	default:
		return "invalid";
	}
}

bool output_kind_exists(std::string_view requested)
{
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_output_types(index, &kind); ++index)
		if (kind && requested == kind)
			return true;
	return false;
}

bool read_output_kind(obs_data_t *params, std::string &kind, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "kind", kind, present) || !present ||
	    !phase2_is_bounded_string(kind, kMaxOutputKindBytes) || !is_safe_identifier(kind.c_str(), kMaxOutputKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid output kind identifier");
	if (!output_kind_exists(kind))
		return fail(error, "not_found", "output kind is not registered");
	return true;
}

bool output_flag(uint32_t flags, uint32_t bit)
{
	return (flags & bit) != 0;
}

ObsArrayPtr split_semicolon_values(const char *text)
{
	ObsArrayPtr values(obs_data_array_create());
	if (!text || !*text)
		return values;
	std::string_view remaining(text);
	for (size_t count = 0; count < 128 && !remaining.empty();) {
		const size_t separator = remaining.find(';');
		const std::string_view value = remaining.substr(0, separator);
		if (!value.empty() && value.size() <= kMaxOutputKindBytes) {
			ObsDataPtr item(obs_data_create());
			obs_data_set_string(item.get(), "value", std::string(value).c_str());
			obs_data_array_push_back(values.get(), item.get());
			++count;
		}
		if (separator == std::string_view::npos)
			break;
		remaining.remove_prefix(separator + 1);
	}
	return values;
}

bool output_codec_supported(const char *supported, const char *codec)
{
	if (!supported || !*supported || !codec || !*codec)
		return true;
	std::string_view remaining(supported);
	for (size_t count = 0; count < 128 && !remaining.empty(); ++count) {
		const size_t separator = remaining.find(';');
		if (remaining.substr(0, separator) == codec)
			return true;
		if (separator == std::string_view::npos)
			break;
		remaining.remove_prefix(separator + 1);
	}
	return false;
}

ObsDataPtr make_output_capabilities(uint32_t flags, const char *video_codecs, const char *audio_codecs,
					    const char *protocols)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "video", output_flag(flags, OBS_OUTPUT_VIDEO));
	obs_data_set_bool(data.get(), "audio", output_flag(flags, OBS_OUTPUT_AUDIO));
	obs_data_set_bool(data.get(), "encoded", output_flag(flags, OBS_OUTPUT_ENCODED));
	obs_data_set_bool(data.get(), "raw", !output_flag(flags, OBS_OUTPUT_ENCODED));
	obs_data_set_bool(data.get(), "requiresService", output_flag(flags, OBS_OUTPUT_SERVICE));
	obs_data_set_bool(data.get(), "multiTrackAudio", output_flag(flags, OBS_OUTPUT_MULTI_TRACK_AUDIO));
	obs_data_set_bool(data.get(), "multiTrackVideo", output_flag(flags, OBS_OUTPUT_MULTI_TRACK_VIDEO));
	obs_data_set_bool(data.get(), "canPause", output_flag(flags, OBS_OUTPUT_CAN_PAUSE));
	obs_data_set_array(data.get(), "videoCodecs", split_semicolon_values(video_codecs).get());
	obs_data_set_array(data.get(), "audioCodecs", split_semicolon_values(audio_codecs).get());
	obs_data_set_array(data.get(), "protocols", split_semicolon_values(protocols).get());
	return data;
}

ObsDataPtr make_output_kind_data(const char *kind)
{
	const uint32_t flags = obs_get_output_flags(kind);
	const enum obs_module_load_state load_state = obs_output_load_state(kind);
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "id", kind);
	obs_data_set_string(data.get(), "displayName",
			   obs_output_get_display_name(kind) ? obs_output_get_display_name(kind) : kind);
	obs_data_set_int(data.get(), "moduleLoadState", static_cast<long long>(load_state));
	obs_data_set_string(data.get(), "moduleLoadStateName", module_load_state_name(load_state));
	obs_data_set_bool(data.get(), "registered", true);
	obs_data_set_bool(data.get(), "moduleLoaded", load_state == OBS_MODULE_ENABLED);
	obs_module_t *module = obs_output_get_module(kind);
	if (module) {
		const char *file = obs_get_module_file_name(module);
		if (file)
			obs_data_set_string(data.get(), "module", file);
	}
	obs_data_set_obj(data.get(), "capabilities",
			make_output_capabilities(flags, obs_get_output_supported_video_codecs(kind),
						obs_get_output_supported_audio_codecs(kind), obs_get_output_protocols(kind))
				.get());
	return data;
}

ObsDataPtr make_output_signal_data(uint64_t handle, const char *state, int stop_code, bool clean)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	if (state)
		obs_data_set_string(data.get(), "state", state);
	obs_data_set_int(data.get(), "stopCode", stop_code);
	obs_data_set_bool(data.get(), "clean", clean);
	return data;
}

ObsDataPtr make_output_pause_data(uint64_t handle, bool paused)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_bool(data.get(), "paused", paused);
	return data;
}

void append_output_signal_event(std::vector<RuntimeV2Event> &generated, const char *name, uint64_t handle,
					const char *state, int stop_code = 0, bool clean = true)
{
	generated.push_back(RuntimeV2Event{name, make_output_signal_data(handle, state, stop_code, clean)});
}

void append_output_signal_pause_event(std::vector<RuntimeV2Event> &generated, uint64_t handle, bool paused)
{
	generated.push_back(RuntimeV2Event{"output.paused", make_output_pause_data(handle, paused)});
}

class OutputCallbackScope {
public:
	explicit OutputCallbackScope(OutputV2State &state) : state_(state)
	{
		std::lock_guard lock(state_.mutex);
		if (!state_.accepting)
			return;
		accepted_ = true;
		++state_.direct_callbacks_inflight;
		counted_direct_ = true;
	}

	~OutputCallbackScope()
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

private:
	OutputV2State &state_;
	bool accepted_ = false;
	bool counted_direct_ = false;
};

bool capture_output_events_locked(OutputV2State &state, std::vector<RuntimeV2Event> &generated)
{
	if (!state.capture)
		return false;
	for (RuntimeV2Event &event : generated)
		state.capture->events.push_back(std::move(event));
	state.capture->mutated = true;
	return true;
}

void queue_deferred_output_events_locked(OutputV2State &state, std::vector<RuntimeV2Event> generated)
{
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
	state.deferred.push_back(DeferredOutputEventBatch{std::move(generated)});
}

bool commit_direct_output_events_locked(OutputV2State &state, RevisionState *&revisions,
						EventDispatcher *&events, uint64_t &revision)
{
	revisions = state.revisions;
	events = state.events;
	if (!revisions || !events)
		return false;
	if (!revisions->can_commit_mutation()) {
		events->require_resync_due_to_overflow(revisions->current());
		return false;
	}
	revision = revisions->commit_mutation();
	return true;
}

void publish_output_events(OutputV2State &state, std::vector<RuntimeV2Event> generated)
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
		if (route == SourceEventCaptureRoute::Capture && capture_output_events_locked(state, generated))
			return;
		if (route == SourceEventCaptureRoute::Defer) {
			queue_deferred_output_events_locked(state, std::move(generated));
			return;
		}
		if (!commit_direct_output_events_locked(state, revisions, events, revision))
			return;
	}

	for (RuntimeV2Event &event : generated)
		events->publish(EngineEventKind::State, event.name, revision, event.data.get());
}

int output_signal_stop_code(OutputV2Observer &observer, OutputSignalKind kind, calldata_t *calldata)
{
	int stop_code = observer.last_stop_code;
	const bool carries_code = kind == OutputSignalKind::Stopped || kind == OutputSignalKind::Deactivated;
	if (!calldata || !carries_code)
		return stop_code;
	const int callback_code = static_cast<int>(calldata_int(calldata, "code"));
	if (callback_code != OBS_OUTPUT_SUCCESS)
		stop_code = callback_code;
	observer.last_stop_code = stop_code;
	return stop_code;
}

void collect_output_start_signal(OutputV2Observer &observer, OutputSignalKind kind, bool logically_active,
					 std::vector<RuntimeV2Event> &generated, bool &defer_dependency_sync)
{
	if (kind == OutputSignalKind::Starting) {
		observer.starting = !logically_active;
		observer.stopping = false;
		if (!logically_active && !observer.published_starting && !observer.published_active) {
			++observer.lifecycle_generation;
			observer.published_starting = true;
			observer.published_stopping = false;
			append_output_signal_event(generated, "output.starting", observer.handle, "starting");
		}
		return;
	}
	if (kind != OutputSignalKind::Started && kind != OutputSignalKind::Activated)
		return;
	observer.starting = false;
	observer.stopping = false;
	if (!logically_active)
		return;
	const bool was_active = observer.published_active;
	const bool was_reconnecting = observer.published_reconnecting;
	defer_dependency_sync = was_reconnecting;
	observer.published_active = true;
	observer.published_starting = false;
	observer.published_stopping = false;
	if (!was_reconnecting && !was_active)
		append_output_signal_event(generated, "output.started", observer.handle, "active");
}

void collect_output_stopping_signal(OutputV2Observer &observer, OutputSignalKind kind,
					    std::vector<RuntimeV2Event> &generated)
{
	if (kind != OutputSignalKind::Stopping)
		return;
	observer.stopping = true;
	if (!observer.published_stopping &&
	    (observer.published_active || observer.published_starting || observer.published_reconnecting)) {
		observer.published_stopping = true;
		observer.published_starting = false;
		append_output_signal_event(generated, "output.stopping", observer.handle, "stopping");
	}
}

bool output_reconnect_expected(const OutputV2Observer &observer, bool reconnecting_now, int stop_code)
{
	return reconnecting_now || observer.published_reconnecting ||
	       (observer.reconnect_enabled && stop_code == OBS_OUTPUT_DISCONNECTED);
}

bool mark_output_reconnecting(OutputV2Observer &observer, bool reconnecting_now, int stop_code,
					      std::vector<RuntimeV2Event> &generated)
{
	if (!output_reconnect_expected(observer, reconnecting_now, stop_code))
		return false;
	observer.published_active = false;
	observer.published_starting = false;
	observer.published_stopping = false;
	if (observer.published_reconnecting)
		return true;
	observer.published_reconnecting = true;
	observer.reconnecting = true;
	append_output_signal_event(generated, "output.reconnecting", observer.handle, "reconnecting", stop_code, false);
	return true;
}

void collect_output_final_stop(OutputV2Observer &observer, int stop_code, std::vector<RuntimeV2Event> &generated)
{
	const bool had_lifecycle = observer.published_active || observer.published_starting || observer.published_stopping ||
					   observer.published_reconnecting;
	observer.published_active = false;
	observer.published_starting = false;
	observer.published_stopping = false;
	observer.published_reconnecting = false;
	observer.reconnecting = false;
	if (!had_lifecycle)
		return;
	append_output_signal_event(generated, "output.stopped", observer.handle, "idle", stop_code,
					   stop_code == OBS_OUTPUT_SUCCESS);
	if (stop_code != OBS_OUTPUT_SUCCESS)
		append_output_signal_event(generated, "output.error", observer.handle, "idle", stop_code, false);
}

void collect_output_stopped_signal(OutputV2Observer &observer, OutputSignalKind kind, bool active_now,
					   bool reconnecting_now, int stop_code, std::vector<RuntimeV2Event> &generated)
{
	if (kind != OutputSignalKind::Stopped && kind != OutputSignalKind::Deactivated)
		return;
	observer.starting = false;
	observer.stopping = false;
	if (mark_output_reconnecting(observer, reconnecting_now, stop_code, generated))
		return;
	if (kind != OutputSignalKind::Deactivated && active_now)
		return;
	collect_output_final_stop(observer, stop_code, generated);
}

void collect_output_reconnect_signal(OutputV2Observer &observer, OutputSignalKind kind,
					     std::vector<RuntimeV2Event> &generated)
{
	if (kind == OutputSignalKind::Reconnect) {
		observer.reconnecting = true;
		observer.published_active = false;
		observer.published_starting = false;
		observer.published_stopping = false;
		return;
	}
	if (kind != OutputSignalKind::ReconnectSuccess)
		return;
	observer.reconnecting = false;
	observer.published_reconnecting = false;
	observer.published_active = true;
	observer.published_starting = false;
	observer.published_stopping = false;
	append_output_signal_event(generated, "output.reconnected", observer.handle, "active");
}

void collect_output_pause_signal(OutputV2Observer &observer, OutputSignalKind kind, obs_output_t *output,
					 std::vector<RuntimeV2Event> &generated)
{
	if (kind != OutputSignalKind::Paused && kind != OutputSignalKind::Unpaused)
		return;
	const bool paused = obs_output_paused(output);
	observer.paused = paused;
	if (observer.published_paused == paused)
		return;
	observer.published_paused = paused;
	append_output_signal_pause_event(generated, observer.handle, paused);
}

void normalize_output_signal(OutputV2Observer &observer, OutputSignalKind kind, obs_output_t *output,
					 calldata_t *calldata, std::vector<RuntimeV2Event> &generated,
					 bool &defer_dependency_sync)
{
	const bool active_now = obs_output_active(output);
	const bool reconnecting_now = obs_output_reconnecting(output);
	const bool logically_active = kind == OutputSignalKind::Activated || active_now;
	const int stop_code = output_signal_stop_code(observer, kind, calldata);
	collect_output_start_signal(observer, kind, logically_active, generated, defer_dependency_sync);
	collect_output_stopping_signal(observer, kind, generated);
	collect_output_stopped_signal(observer, kind, active_now, reconnecting_now, stop_code, generated);
	collect_output_reconnect_signal(observer, kind, generated);
	collect_output_pause_signal(observer, kind, output, generated);
	observer.cv.notify_all();
}

void collect_output_signal(OutputV2Observer &observer, OutputSignalKind kind, calldata_t *calldata)
{
	OutputV2State *state = observer.state;
	if (!state)
		return;
	OutputCallbackScope callback_scope(*state);
	if (!callback_scope.accepted())
		return;

	obs_output_t *output = observer.weak ? obs_weak_output_get_output(observer.weak) : nullptr;
	if (!output)
		return;

	std::vector<RuntimeV2Event> generated;
	bool defer_dependency_sync = false;
	{
		std::lock_guard lock(observer.mutex);
		if (observer.accepting)
			normalize_output_signal(observer, kind, output, calldata, generated, defer_dependency_sync);
	}

	obs_output_release(output);
	if (state->engine && !defer_dependency_sync)
		state->engine->v2_append_output_dependency_events(observer.handle, generated);
	if (state->engine)
		state->engine->v2_append_recording_output_events(observer.handle, generated);
	publish_output_events(*state, std::move(generated));
}

void note_output_signal(OutputSignalHook &hook, calldata_t *calldata)
{
	if (hook.observer)
		collect_output_signal(*hook.observer, hook.kind, calldata);
}

void output_signal_callback(void *data, calldata_t *calldata)
{
	if (data)
		note_output_signal(*static_cast<OutputSignalHook *>(data), calldata);
}

constexpr std::array<const char *, 10> kOutputSignalNames = {
	"starting", "start", "stopping", "stop", "activate", "deactivate", "reconnect", "reconnect_success",
	"pause", "unpause"};

constexpr std::array<OutputSignalKind, 10> kOutputSignalKinds = {
	OutputSignalKind::Starting, OutputSignalKind::Started, OutputSignalKind::Stopping, OutputSignalKind::Stopped,
	OutputSignalKind::Activated, OutputSignalKind::Deactivated, OutputSignalKind::Reconnect,
	OutputSignalKind::ReconnectSuccess, OutputSignalKind::Paused, OutputSignalKind::Unpaused};

void connect_output_observer(OutputV2Observer &observer, obs_output_t *output)
{
	signal_handler_t *signals = obs_output_get_signal_handler(output);
	for (size_t index = 0; index < observer.hooks.size(); ++index) {
		observer.hooks[index].observer = &observer;
		observer.hooks[index].kind = kOutputSignalKinds[index];
		signal_handler_connect(signals, kOutputSignalNames[index], output_signal_callback, &observer.hooks[index]);
	}
}

void disconnect_output_observer(OutputV2Observer &observer, obs_output_t *output)
{
	{
		std::lock_guard lock(observer.mutex);
		observer.accepting = false;
	}
	signal_handler_t *signals = obs_output_get_signal_handler(output);
	for (size_t index = 0; index < observer.hooks.size(); ++index)
		signal_handler_disconnect(signals, kOutputSignalNames[index], output_signal_callback, &observer.hooks[index]);
}

void release_output_observer_weak(OutputV2Observer &observer)
{
	if (observer.weak) {
		obs_weak_output_release(observer.weak);
		observer.weak = nullptr;
	}
}

bool output_starting(const OutputEntry &entry)
{
	std::lock_guard lock(entry.observer->mutex);
	return entry.observer->starting;
}

bool output_stopping(const OutputEntry &entry)
{
	std::lock_guard lock(entry.observer->mutex);
	return entry.observer->stopping;
}

void set_output_starting(OutputEntry &entry, bool value)
{
	std::lock_guard lock(entry.observer->mutex);
	entry.observer->starting = value;
	entry.observer->stopping = false;
	if (value)
		entry.observer->last_stop_code = OBS_OUTPUT_SUCCESS;
}

void set_output_stopping(OutputEntry &entry, bool value)
{
	std::lock_guard lock(entry.observer->mutex);
	entry.observer->stopping = value;
	if (value)
		entry.observer->starting = false;
}

int output_stop_code(const OutputEntry &entry)
{
	std::lock_guard lock(entry.observer->mutex);
	return entry.observer->last_stop_code;
}

bool wait_for_output(OutputEntry &entry, const std::function<bool()> &predicate)
{
	std::unique_lock lock(entry.observer->mutex);
	return entry.observer->cv.wait_for(lock, kOutputSettlementTimeout, predicate);
}

const char *output_state_name(const OutputEntry &entry)
{
	if (output_stopping(entry))
		return "stopping";
	if (obs_output_reconnecting(entry.output))
		return "reconnecting";
	if (obs_output_active(entry.output))
		return "active";
	if (output_starting(entry))
		return "starting";
	return "idle";
}

void append_output_slots(obs_data_t *data, const OutputEntry &entry)
{
	ObsArrayPtr video(obs_data_array_create());
	for (size_t slot = 0; slot < entry.video_encoders.size(); ++slot) {
		if (!entry.video_encoders[slot])
			continue;
		ObsDataPtr item(obs_data_create());
		obs_data_set_int(item.get(), "slot", static_cast<long long>(slot));
		phase2_set_handle(item.get(), "encoder", entry.video_encoders[slot]);
		obs_data_array_push_back(video.get(), item.get());
	}
	ObsArrayPtr audio(obs_data_array_create());
	for (size_t slot = 0; slot < entry.audio_encoders.size(); ++slot) {
		if (!entry.audio_encoders[slot])
			continue;
		ObsDataPtr item(obs_data_create());
		obs_data_set_int(item.get(), "slot", static_cast<long long>(slot));
		phase2_set_handle(item.get(), "encoder", entry.audio_encoders[slot]);
		obs_data_array_push_back(audio.get(), item.get());
	}
	obs_data_set_array(data, "videoEncoders", video.get());
	obs_data_set_array(data, "audioEncoders", audio.get());
}

bool output_is_inactive(const OutputEntry &entry, RuntimeV2Error &error)
{
	if (obs_output_active(entry.output) || output_starting(entry) || output_stopping(entry))
		return fail(error, "busy", "output configuration requires an inactive output");
	return true;
}

bool read_output_name_and_settings(obs_data_t *params, uint64_t handle, std::string &name, ObsDataPtr &settings,
					   RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (!present)
		name = "engine-output-" + std::to_string(handle);
	if (!phase2_is_bounded_string(name, kMaxOutputNameBytes))
		return fail(error, "bad_request", "params.name must be a non-empty output name of at most 256 bytes");
	if (!phase2_read_object(params, "settings", settings, present))
		return fail(error, "bad_request", "params.settings must be an object when present");
	return true;
}

bool output_slot_supports_index(uint32_t flags, bool video, long long requested)
{
	if (requested == 0)
		return true;
	return output_flag(flags, video ? OBS_OUTPUT_MULTI_TRACK_VIDEO : OBS_OUTPUT_MULTI_TRACK_AUDIO);
}

bool output_accepts_encoded_media(uint32_t flags, bool video)
{
	const uint32_t media_flag = video ? OBS_OUTPUT_VIDEO : OBS_OUTPUT_AUDIO;
	return output_flag(flags, OBS_OUTPUT_ENCODED) && output_flag(flags, media_flag);
}

bool read_output_slot_request(obs_data_t *params, size_t maximum, size_t &slot, RuntimeV2Error &error)
{
	long long requested = 0;
	bool present = false;
	if (!phase2_read_integer(params, "slot", requested, present) || !present)
		return fail(error, "bad_request", "params.slot must be a supported non-negative output encoder slot");
	if (requested < 0 || static_cast<uint64_t>(requested) >= maximum)
		return fail(error, "bad_request", "params.slot must be a supported non-negative output encoder slot");
	slot = static_cast<size_t>(requested);
	return true;
}

bool read_output_slot(obs_data_t *params, const OutputEntry &entry, bool video, size_t &slot, RuntimeV2Error &error)
{
	const size_t maximum = video ? entry.video_encoders.size() : entry.audio_encoders.size();
	if (!read_output_slot_request(params, maximum, slot, error))
		return false;
	const uint32_t flags = obs_output_get_flags(entry.output);
	if (!output_slot_supports_index(flags, video, static_cast<long long>(slot)))
		return fail(error, "unsupported_capability", "output kind does not support multiple encoder slots");
	return true;
}

bool read_nullable_encoder(obs_data_t *params, uint64_t &handle, bool &is_null, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_nullable_handle(params, "encoder", handle, is_null, present) || !present)
		return fail(error, "bad_request", "params.encoder must be a canonical encoder handle string or null");
	if (is_null)
		handle = 0;
	return true;
}

} // namespace

namespace {

struct DeferredOutputEventSnapshot {
	std::deque<DeferredOutputEventBatch> batches;
	EventDispatcher *events = nullptr;
	bool overflow = false;
};

void clear_deferred_output_events(OutputV2State &state)
{
	state.deferred.clear();
	state.deferred_event_count = 0;
	state.deferred_overflow = false;
}

DeferredOutputEventSnapshot take_deferred_output_events(OutputV2State &state, bool wait_for_callbacks, bool end_capture)
{
	DeferredOutputEventSnapshot snapshot;
	std::unique_lock lock(state.mutex);
	if (wait_for_callbacks) {
		state.callback_cv.wait(lock, [&] { return !state.accepting || state.direct_callbacks_inflight == 0; });
	}
	if (!state.accepting) {
		clear_deferred_output_events(state);
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
		DeferredOutputEventBatch batch = std::move(state.deferred.front());
		state.deferred.pop_front();
		state.deferred_event_count -= batch.events.size();
		snapshot.batches.push_back(std::move(batch));
	}
	if (end_capture) {
		state.capture = nullptr;
		state.capture_gate.end();
	}
	return snapshot;
}

void publish_deferred_output_snapshot(DeferredOutputEventSnapshot snapshot, RevisionState::MutationGuard &guard)
{
	if (snapshot.overflow) {
		uint64_t revision = guard.current();
		if (guard.can_commit_mutation())
			revision = guard.commit_mutation();
		if (snapshot.events)
			snapshot.events->require_resync_due_to_overflow(revision);
	}
	for (DeferredOutputEventBatch &batch : snapshot.batches) {
		if (!guard.can_commit_mutation()) {
			if (snapshot.events)
				snapshot.events->require_resync_due_to_overflow(guard.current());
			return;
		}
		const uint64_t revision = guard.commit_mutation();
		if (snapshot.events) {
			for (RuntimeV2Event &event : batch.events)
				snapshot.events->publish(EngineEventKind::State, event.name, revision, event.data.get());
		}
	}
}

} // namespace

std::string Engine::v2_sanitize_output_error(const OutputEntry &entry) const
{
	const char *raw = obs_output_get_last_error(entry.output);
	std::string sanitized = raw ? raw : "";
	if (sanitized.size() > kMaxOutputErrorBytes)
		sanitized.resize(kMaxOutputErrorBytes);
	for (const auto &[_, service_entry] : services_) {
		if (!service_entry.service)
			continue;
		static constexpr uint32_t kSecretTypes[] = {
			OBS_SERVICE_CONNECT_INFO_STREAM_KEY,
			OBS_SERVICE_CONNECT_INFO_USERNAME,
			OBS_SERVICE_CONNECT_INFO_PASSWORD,
			OBS_SERVICE_CONNECT_INFO_ENCRYPT_PASSPHRASE,
			OBS_SERVICE_CONNECT_INFO_BEARER_TOKEN,
		};
		for (const uint32_t type : kSecretTypes) {
			const char *secret = obs_service_get_connect_info(service_entry.service, type);
			if (!secret || !*secret)
				continue;
			std::string::size_type position = 0;
			while ((position = sanitized.find(secret, position)) != std::string::npos) {
				sanitized.replace(position, std::strlen(secret), "[redacted]");
				position += 10;
			}
		}
	}
	return sanitized;
}

ObsDataPtr Engine::v2_output_state(uint64_t handle, const OutputEntry &entry) const
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_string(data.get(), "state", output_state_name(entry));
	obs_data_set_bool(data.get(), "initialized", obs_output_initialized(entry.output));
	obs_data_set_bool(data.get(), "active", obs_output_active(entry.output));
	obs_data_set_bool(data.get(), "reconnecting", obs_output_reconnecting(entry.output));
	obs_data_set_bool(data.get(), "starting", output_starting(entry));
	obs_data_set_bool(data.get(), "stopping", output_stopping(entry));
	obs_data_set_bool(data.get(), "paused", obs_output_paused(entry.output));
	if (entry.service)
		phase2_set_handle(data.get(), "service", entry.service);
	else
		obs_data_set_obj(data.get(), "service", nullptr);
	append_output_slots(data.get(), entry);
	if (recording_.output == handle) {
		obs_data_set_string(data.get(), "role", "recording");
		obs_data_set_string(data.get(), "managedBy", "recording");
	}
	ObsDataPtr delay(obs_data_create());
	obs_data_set_int(delay.get(), "seconds", obs_output_get_delay(entry.output));
	obs_data_set_int(delay.get(), "activeSeconds", obs_output_get_active_delay(entry.output));
	obs_data_set_bool(delay.get(), "preserve", (entry.delay_flags & OBS_OUTPUT_DELAY_PRESERVE) != 0);
	obs_data_set_obj(data.get(), "delay", delay.get());
	ObsDataPtr reconnect(obs_data_create());
	obs_data_set_bool(reconnect.get(), "enabled", entry.reconnect_enabled);
	obs_data_set_int(reconnect.get(), "retryCount", entry.retry_count);
	obs_data_set_int(reconnect.get(), "retryDelaySeconds", entry.retry_delay_seconds);
	obs_data_set_obj(data.get(), "reconnectPolicy", reconnect.get());
	obs_data_set_int(data.get(), "lastStopCode", output_stop_code(entry));
	obs_data_set_string(data.get(), "sanitizedLastError", v2_sanitize_output_error(entry).c_str());
	return data;
}

ObsDataPtr Engine::v2_output_summary(uint64_t handle, const OutputEntry &entry) const
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_string(data.get(), "name", obs_output_get_name(entry.output) ? obs_output_get_name(entry.output) : "");
	obs_data_set_string(data.get(), "kind", obs_output_get_id(entry.output) ? obs_output_get_id(entry.output) : "");
	obs_data_set_int(data.get(), "flags", obs_output_get_flags(entry.output));
	obs_data_set_obj(data.get(), "state", v2_output_state(handle, entry).get());
	append_output_slots(data.get(), entry);
	return data;
}

bool Engine::v2_get_output_entry(obs_data_t *params, uint64_t &handle, OutputEntry *&entry,
					RuntimeV2Error &error) const
{
	if (!phase2_read_handle(params, "output", handle))
		return fail(error, "bad_request", "params.output must be a canonical decimal output handle string");
	const auto it = outputs_.find(handle);
	if (it == outputs_.end() || !it->second.output)
		return fail(error, "not_found", "output handle was not found");
	entry = const_cast<OutputEntry *>(&it->second);
	return true;
}

bool Engine::v2_output_is_inactive(const OutputEntry &entry, RuntimeV2Error &error) const
{
	return output_is_inactive(entry, error);
}

void Engine::v2_bind_output_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!output_v2_state_)
		output_v2_state_ = std::make_shared<OutputV2State>();
	{
		std::lock_guard lock(output_v2_state_->mutex);
		output_v2_state_->engine = this;
		output_v2_state_->revisions = revisions;
		output_v2_state_->events = events;
		output_v2_state_->accepting = revisions && events;
	}
	output_revisions_ = revisions;
	output_events_ = events;
}

void Engine::v2_publish_output_callback_events(std::vector<RuntimeV2Event> events)
{
	if (output_v2_state_)
		publish_output_events(*output_v2_state_, std::move(events));
}

void Engine::v2_begin_output_event_capture(RuntimeV2Result &result)
{
	if (!output_v2_state_)
		return;
	std::lock_guard lock(output_v2_state_->mutex);
	output_v2_state_->capture = &result;
	output_v2_state_->capture_gate.begin();
}

void Engine::v2_wait_for_output_event_callbacks()
{
	if (!output_v2_state_)
		return;
	std::unique_lock lock(output_v2_state_->mutex);
	output_v2_state_->callback_cv.wait(lock, [&] {
		return !output_v2_state_->accepting || output_v2_state_->direct_callbacks_inflight == 0;
	});
}

void Engine::v2_end_output_event_capture() noexcept
{
	if (!output_v2_state_)
		return;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	bool lost_deferred = false;
	{
		std::lock_guard lock(output_v2_state_->mutex);
		output_v2_state_->capture = nullptr;
		output_v2_state_->capture_gate.end();
		lost_deferred = output_v2_state_->deferred_overflow || !output_v2_state_->deferred.empty();
		if (lost_deferred) {
			clear_deferred_output_events(*output_v2_state_);
			revisions = output_v2_state_->revisions;
			events = output_v2_state_->events;
		}
	}
	if (lost_deferred && revisions && events)
		events->require_resync_due_to_overflow(revisions->current());
}

void Engine::v2_drain_deferred_output_events(RevisionState::MutationGuard &guard)
{
	if (output_v2_state_)
		publish_deferred_output_snapshot(take_deferred_output_events(*output_v2_state_, true, false), guard);
}

void Engine::v2_flush_deferred_output_events(RevisionState::MutationGuard &guard)
{
	if (output_v2_state_)
		publish_deferred_output_snapshot(take_deferred_output_events(*output_v2_state_, false, true), guard);
}

void Engine::v2_sync_output_observers()
{
	for (auto &[_, entry] : outputs_) {
		if (!entry.output || !entry.observer)
			continue;
		std::lock_guard lock(entry.observer->mutex);
		entry.observer->cv.notify_all();
	}
}

void Engine::v2_detach_output_video_encoders(uint64_t output_handle, OutputEntry &entry) noexcept
{
	for (size_t slot = 0; slot < entry.video_encoders.size(); ++slot) {
		const uint64_t encoder = entry.video_encoders[slot];
		if (!encoder)
			continue;
		obs_output_set_video_encoder2(entry.output, nullptr, slot);
		if (auto it = encoders_.find(encoder); it != encoders_.end())
			it->second.bound_outputs.erase(output_handle);
		entry.video_encoders[slot] = 0;
	}
}

void Engine::v2_detach_output_audio_encoders(uint64_t output_handle, OutputEntry &entry) noexcept
{
	for (size_t slot = 0; slot < entry.audio_encoders.size(); ++slot) {
		const uint64_t encoder = entry.audio_encoders[slot];
		if (!encoder)
			continue;
		obs_output_set_audio_encoder(entry.output, nullptr, slot);
		if (auto it = encoders_.find(encoder); it != encoders_.end())
			it->second.bound_outputs.erase(output_handle);
		entry.audio_encoders[slot] = 0;
	}
}

void Engine::v2_detach_output_encoders(uint64_t output_handle, OutputEntry &entry) noexcept
{
	v2_detach_output_video_encoders(output_handle, entry);
	v2_detach_output_audio_encoders(output_handle, entry);
}

void Engine::v2_shutdown_output_entry(uint64_t output_handle, OutputEntry &entry) noexcept
{
	if (!entry.output)
		return;
	if (obs_output_active(entry.output) || obs_output_reconnecting(entry.output) || output_starting(entry)) {
		obs_output_force_stop(entry.output);
		wait_for_output(entry, [&] {
			return !obs_output_active(entry.output) && !obs_output_reconnecting(entry.output);
		});
	}
	if (entry.observer)
		disconnect_output_observer(*entry.observer, entry.output);
	if (entry.service) {
		obs_output_set_service(entry.output, nullptr);
		const auto service = services_.find(entry.service);
		if (service != services_.end() && service->second.bound_output == output_handle)
			service->second.bound_output = 0;
	}
	v2_detach_output_encoders(output_handle, entry);
	obs_output_release(entry.output);
	entry.output = nullptr;
}

void Engine::v2_prepare_output_shutdown() noexcept
{
	if (output_v2_state_) {
		std::lock_guard lock(output_v2_state_->mutex);
		output_v2_state_->accepting = false;
		output_v2_state_->capture = nullptr;
		output_v2_state_->capture_gate.end();
		clear_deferred_output_events(*output_v2_state_);
		output_v2_state_->callback_cv.notify_all();
	}
	for (auto &[output_handle, entry] : outputs_)
		v2_shutdown_output_entry(output_handle, entry);
	if (output_v2_state_) {
		std::unique_lock lock(output_v2_state_->mutex);
		output_v2_state_->callback_cv.wait(lock, [&] { return output_v2_state_->direct_callbacks_inflight == 0; });
	}
	for (auto &[_, entry] : outputs_)
		if (entry.observer)
			release_output_observer_weak(*entry.observer);
	outputs_.clear();
	if (output_v2_state_) {
		std::lock_guard lock(output_v2_state_->mutex);
		output_v2_state_->engine = nullptr;
		output_v2_state_->revisions = nullptr;
		output_v2_state_->events = nullptr;
	}
	output_revisions_ = nullptr;
	output_events_ = nullptr;
}

void Engine::v2_sync_encoder_active_events(RuntimeV2Result &result)
{
	std::unique_lock<std::mutex> output_lock;
	if (output_v2_state_)
		output_lock = std::unique_lock<std::mutex>(output_v2_state_->mutex);
	for (auto &[handle, entry] : encoders_) {
		if (!entry.encoder)
			continue;
		const bool active = obs_encoder_active(entry.encoder);
		if (active == entry.observed_active)
			continue;
		entry.observed_active = active;
		ObsDataPtr data(obs_data_create());
		phase2_set_handle(data.get(), "encoder", handle);
		obs_data_set_bool(data.get(), "active", active);
		phase2_append_event(result, "encoder.activeChanged", std::move(data));
	}
}

void Engine::v2_sync_service_active_events(RuntimeV2Result &result)
{
	std::unique_lock<std::mutex> output_lock;
	if (output_v2_state_)
		output_lock = std::unique_lock<std::mutex>(output_v2_state_->mutex);
	for (auto &[handle, entry] : services_) {
		if (!entry.service)
			continue;
		const bool active = obs_service_active(entry.service);
		if (active == entry.observed_active)
			continue;
		entry.observed_active = active;
		ObsDataPtr data(obs_data_create());
		phase2_set_handle(data.get(), "service", handle);
		obs_data_set_bool(data.get(), "active", active);
		phase2_append_event(result, "service.activeChanged", std::move(data));
	}
}

void Engine::v2_append_output_dependency_events(uint64_t output_handle, std::vector<RuntimeV2Event> &generated)
{
	std::unique_lock<std::mutex> output_lock;
	if (output_v2_state_)
		output_lock = std::unique_lock<std::mutex>(output_v2_state_->mutex);
	const auto output = outputs_.find(output_handle);
	if (output == outputs_.end())
		return;

	std::unordered_set<uint64_t> seen_encoders;
	v2_append_output_encoder_dependency_events(output->second, generated, seen_encoders);
	v2_append_output_service_dependency_event(output->second, generated);
}

void Engine::v2_append_output_encoder_dependency_events(const OutputEntry &output,
								std::vector<RuntimeV2Event> &generated,
								std::unordered_set<uint64_t> &seen)
{
		const auto append_encoder = [&](uint64_t encoder_handle) {
			if (!encoder_handle || !seen.insert(encoder_handle).second)
				return;
			const auto encoder = encoders_.find(encoder_handle);
			if (encoder == encoders_.end() || !encoder->second.encoder)
				return;
			const bool active = obs_encoder_active(encoder->second.encoder);
			if (active == encoder->second.observed_active)
				return;
			encoder->second.observed_active = active;
			ObsDataPtr data(obs_data_create());
			phase2_set_handle(data.get(), "encoder", encoder_handle);
			obs_data_set_bool(data.get(), "active", active);
			generated.push_back(RuntimeV2Event{"encoder.activeChanged", std::move(data)});
		};
	for (const uint64_t encoder : output.video_encoders)
		append_encoder(encoder);
	for (const uint64_t encoder : output.audio_encoders)
		append_encoder(encoder);
}

void Engine::v2_append_output_service_dependency_event(const OutputEntry &output,
								 std::vector<RuntimeV2Event> &generated)
{
	if (!output.service)
		return;
	const auto service = services_.find(output.service);
	if (service == services_.end() || !service->second.service)
		return;
	const bool active = obs_service_active(service->second.service);
	if (active == service->second.observed_active)
		return;
	service->second.observed_active = active;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", output.service);
	obs_data_set_bool(data.get(), "active", active);
	generated.push_back(RuntimeV2Event{"service.activeChanged", std::move(data)});
}

bool validate_output_start_slots(const OutputEntry &entry, uint32_t flags, RuntimeV2Error &error)
{
	if (output_flag(flags, OBS_OUTPUT_SERVICE) && !entry.service)
		return fail(error, "invalid_state", "output requires a bound Service");
	const bool needs_video = output_flag(flags, OBS_OUTPUT_VIDEO) && output_flag(flags, OBS_OUTPUT_ENCODED);
	if (needs_video && !entry.video_encoders[0])
		return fail(error, "invalid_state", "encoded video output requires slot 0");
	const bool needs_audio = output_flag(flags, OBS_OUTPUT_AUDIO) && output_flag(flags, OBS_OUTPUT_ENCODED);
	if (needs_audio && !entry.audio_encoders[0])
		return fail(error, "invalid_state", "encoded audio output requires slot 0");
	return true;
}

bool Engine::v2_validate_output_start(const OutputEntry &entry, RuntimeV2Error &error) const
{
	const uint32_t flags = obs_output_get_flags(entry.output);
	if (!validate_output_start_slots(entry, flags, error))
		return false;
	if (entry.service) {
		const auto service = services_.find(entry.service);
		if (service == services_.end() || !service->second.service)
			return fail(error, "not_found", "bound Service handle was not found");
		if (!obs_service_can_try_to_connect(service->second.service))
			return fail(error, "invalid_state", "Service cannot currently connect");
	}
	return true;
}

bool Engine::v2_start_output_entry(OutputEntry &entry, uint64_t handle, RuntimeV2Result &result,
					RuntimeV2Error &error)
{
	if (obs_output_active(entry.output) || output_starting(entry) || output_stopping(entry))
		return fail(error, "invalid_state", "output is already active or transitioning");
	if (!v2_validate_output_start(entry, error))
		return false;
	if (!obs_output_can_begin_data_capture(entry.output, 0))
		return fail(error, "invalid_state", "output cannot begin data capture in its current state");
	set_output_starting(entry, true);
	if (!obs_output_start(entry.output)) {
		set_output_starting(entry, false);
		return fail(error, "obs_error", "output plugin rejected start");
	}
	if (!obs_output_active(entry.output))
		wait_for_output(entry, [&] { return obs_output_active(entry.output); });
	v2_wait_for_output_event_callbacks();
	const bool active = obs_output_active(entry.output);
	if (active)
		set_output_starting(entry, false);
	result.data = v2_output_summary(handle, entry);
	v2_sync_encoder_active_events(result);
	v2_sync_service_active_events(result);
	result.mutated = true;
	return true;
}

bool Engine::v2_stop_output_entry(OutputEntry &entry, uint64_t handle, bool force, RuntimeV2Result &result,
				   RuntimeV2Error &error)
{
	if (!obs_output_active(entry.output) && !output_starting(entry) && !output_stopping(entry)) {
		result.data = v2_output_summary(handle, entry);
		return true;
	}
	set_output_stopping(entry, true);
	if (force)
		obs_output_force_stop(entry.output);
	else
		obs_output_stop(entry.output);
	if (!wait_for_output(entry, [&] { return !obs_output_active(entry.output); })) {
		if (output_events_ && output_revisions_)
			output_events_->require_resync_after_queued_events(output_revisions_->current());
		return fail(error, "timeout", "output stop was not settled before the deadline");
	}
	v2_wait_for_output_event_callbacks();
	set_output_stopping(entry, false);
	const int stop_code = output_stop_code(entry);
	result.data = v2_output_summary(handle, entry);
	v2_sync_encoder_active_events(result);
	v2_sync_service_active_events(result);
	result.mutated = true;
	return true;
}

namespace {

ObsDataPtr output_settings(obs_output_t *output)
{
	ObsDataPtr settings(obs_output_get_settings(output));
	return settings ? clone_property_settings(settings.get()) : ObsDataPtr(obs_data_create());
}

bool validate_output_settings(obs_output_t *output, obs_data_t *candidate, RuntimeV2Error &error)
{
	ObsPropertiesPtr properties(obs_output_properties(output));
	if (!properties)
		return true;
	ObsArrayPtr issues = validate_property_patch(properties.get(), candidate);
	return obs_data_array_count(issues.get()) == 0 ||
	       fail(error, "bad_request", "output settings failed property validation");
}

bool output_settings_equal(obs_data_t *left, obs_data_t *right)
{
	const char *left_json = left ? obs_data_get_json(left) : nullptr;
	const char *right_json = right ? obs_data_get_json(right) : nullptr;
	return left_json && right_json && std::strcmp(left_json, right_json) == 0;
}

ObsDataPtr make_output_settings_result(uint64_t handle, obs_output_t *output)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	ObsPropertiesPtr properties(obs_output_properties(output));
	ObsDataPtr settings(obs_output_get_settings(output));
	obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), settings.get()).get());
	return data;
}

ObsDataPtr make_output_service_change(uint64_t output, uint64_t service)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output);
	if (service)
		phase2_set_handle(data.get(), "service", service);
	else
		obs_data_set_obj(data.get(), "service", nullptr);
	return data;
}

ObsDataPtr make_encoder_binding_change(uint64_t output, uint64_t encoder, size_t slot, bool video)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output);
	if (encoder)
		phase2_set_handle(data.get(), "encoder", encoder);
	else
		obs_data_set_obj(data.get(), "encoder", nullptr);
	obs_data_set_int(data.get(), "slot", static_cast<long long>(slot));
	obs_data_set_string(data.get(), "type", video ? "video" : "audio");
	return data;
}

bool read_delay_request(obs_data_t *params, uint32_t &seconds, uint32_t &flags, RuntimeV2Error &error)
{
	long long requested = 0;
	bool present = false;
	if (!phase2_read_integer(params, "seconds", requested, present) || !present || requested < 0 ||
	    requested > kMaxOutputDelaySeconds)
		return fail(error, "bad_request", "params.seconds must be an integer from 0 through 3600");
	seconds = static_cast<uint32_t>(requested);
	flags = 0;
	bool preserve = false;
	if (!phase2_read_bool(params, "preserve", preserve, present))
		return fail(error, "bad_request", "params.preserve must be boolean when present");
	if (present && preserve)
		flags |= OBS_OUTPUT_DELAY_PRESERVE;
	return true;
}

bool read_bounded_output_integer(obs_data_t *params, const char *name, long long minimum, long long maximum,
					 long long &value, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_integer(params, name, value, present) || !present || value < minimum || value > maximum)
		return fail(error, "bad_request", "output reconnect integer is outside its supported range");
	return true;
}

bool read_reconnect_request(obs_data_t *params, bool &enabled, int &retry_count, int &retry_delay,
					RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_bool(params, "enabled", enabled, present) || !present)
		return fail(error, "bad_request", "params.enabled must be a boolean");
	long long value = 0;
	if (!read_bounded_output_integer(params, "retryCount", 0, kMaxReconnectRetries, value, error))
		return false;
	retry_count = static_cast<int>(value);
	if (!read_bounded_output_integer(params, "retryDelaySeconds", 1, kMaxReconnectDelaySeconds, value, error))
		return false;
	retry_delay = static_cast<int>(value);
	return true;
}

ObsDataPtr make_output_delay_result(uint64_t handle, const OutputEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_int(data.get(), "seconds", obs_output_get_delay(entry.output));
	obs_data_set_int(data.get(), "activeSeconds", obs_output_get_active_delay(entry.output));
	obs_data_set_bool(data.get(), "preserve", (entry.delay_flags & OBS_OUTPUT_DELAY_PRESERVE) != 0);
	return data;
}

ObsDataPtr make_output_reconnect_result(uint64_t handle, const OutputEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_bool(data.get(), "enabled", entry.reconnect_enabled);
	obs_data_set_int(data.get(), "retryCount", entry.retry_count);
	obs_data_set_int(data.get(), "retryDelaySeconds", entry.retry_delay_seconds);
	return data;
}

} // namespace

bool Engine::v2_output_kind_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsArrayPtr kinds(obs_data_array_create());
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_output_types(index, &kind); ++index)
		if (kind)
			obs_data_array_push_back(kinds.get(), make_output_kind_data(kind).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "kinds", kinds.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_kind_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_output_kind(params, kind, error))
		return false;
	result.data = make_output_kind_data(kind.c_str());
	return true;
}

bool Engine::v2_output_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_output_kind(params, kind, error))
		return false;
	ObsDataPtr defaults(obs_output_defaults(kind.c_str()));
	if (!defaults)
		return fail(error, "obs_error", "output kind did not provide defaults");
	ObsPropertiesPtr properties(obs_get_output_properties(kind.c_str()));
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), defaults.get()).get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_output_kind(params, kind, error))
		return false;
	ObsPropertiesPtr properties(obs_get_output_properties(kind.c_str()));
	ObsDataPtr defaults(obs_output_defaults(kind.c_str()));
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), defaults.get()).get());
	obs_data_set_array(data.get(), "properties", serialize_properties(properties.get(), defaults.get()).get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  properties && (obs_properties_get_flags(properties.get()) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_kind_capabilities(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_output_kind(params, kind, error))
		return false;
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	ObsDataPtr kind_data = make_output_kind_data(kind.c_str());
	ObsDataPtr capabilities(obs_data_get_obj(kind_data.get(), "capabilities"));
	obs_data_set_obj(data.get(), "capabilities", capabilities.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<uint64_t> handles;
	for (const auto &[handle, _] : outputs_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr values(obs_data_array_create());
	for (const uint64_t handle : handles)
		obs_data_array_push_back(values.get(), v2_output_summary(handle, outputs_.at(handle)).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "outputs", values.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	result.data = v2_output_summary(handle, *entry);
	return true;
}

bool Engine::v2_register_output_entry(uint64_t output_handle, obs_output_t *output, RuntimeV2Error &error)
{
	OutputEntry entry;
	entry.output = output;
	entry.observer = std::make_shared<OutputV2Observer>();
	entry.observer->state = output_v2_state_.get();
	entry.observer->handle = output_handle;
	entry.observer->weak = obs_output_get_weak_output(output);
	try {
		connect_output_observer(*entry.observer, output);
		if (!outputs_.emplace(output_handle, std::move(entry)).second)
			throw std::runtime_error("output handle collision");
	} catch (...) {
		if (entry.observer)
			disconnect_output_observer(*entry.observer, output);
		if (entry.observer)
			release_output_observer_weak(*entry.observer);
		obs_output_release(output);
		return fail(error, "internal_error", "could not register the output handle");
	}
	return true;
}

bool Engine::v2_output_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_output_kind(params, kind, error))
		return false;
	const uint64_t handle = allocate_handle();
	std::string name;
	ObsDataPtr settings;
	if (!read_output_name_and_settings(params, handle, name, settings, error))
		return false;
	if (!settings)
		settings.reset(obs_output_defaults(kind.c_str()));
	if (!settings)
		settings.reset(obs_data_create());
	obs_output_t *output = obs_output_create(kind.c_str(), name.c_str(), settings.get(), nullptr);
	if (!output)
		return fail(error, "obs_error", "libobs output creation failed");
	if (!obs_output_initialized(output)) {
		obs_output_release(output);
		return fail(error, "obs_error", "output plugin context initialization failed");
	}
	if (!v2_register_output_entry(handle, output, error))
		return false;
	result.data = v2_output_summary(handle, outputs_.at(handle));
	phase2_append_event(result, "output.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_output_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	if (obs_output_active(entry->output) || output_starting(*entry) || output_stopping(*entry))
		return fail(error, "object_in_use", "output must be stopped before removal");
	if (entry->service || std::any_of(entry->video_encoders.begin(), entry->video_encoders.end(), [](uint64_t v) { return v != 0; }) ||
	    std::any_of(entry->audio_encoders.begin(), entry->audio_encoders.end(), [](uint64_t v) { return v != 0; }))
		return fail(error, "object_in_use", "output has bound Service or Encoder objects");
	if (recording_.output == handle)
		return fail(error, "object_in_use", "output is assigned to the recording role");
	if (entry->observer) {
		disconnect_output_observer(*entry->observer, entry->output);
		v2_wait_for_output_event_callbacks();
		release_output_observer_weak(*entry->observer);
	}
	obs_output_release(entry->output);
	outputs_.erase(handle);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	result.data = std::move(data);
	phase2_append_event(result, "output.removed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_output_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	std::string name;
	bool present = false;
	if (!phase2_read_string(params, "name", name, present) || !present ||
	    !phase2_is_bounded_string(name, kMaxOutputNameBytes))
		return fail(error, "bad_request", "params.name must be a non-empty output name of at most 256 bytes");
	const std::string old_name = obs_output_get_name(entry->output) ? obs_output_get_name(entry->output) : "";
	if (old_name == name) {
		result.data = v2_output_summary(handle, *entry);
		return true;
	}
	obs_output_set_name(entry->output, name.c_str());
	const char *actual = obs_output_get_name(entry->output);
	if (!actual || name != actual)
		return fail(error, "obs_error", "libobs did not accept the output name");
	result.data = v2_output_summary(handle, *entry);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "output", handle);
	obs_data_set_string(event_data.get(), "name", actual);
	phase2_append_event(result, "output.renamed", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_update_output_settings(OutputEntry &entry, uint64_t handle, obs_data_t *requested, bool replace,
					       RuntimeV2Result &result, RuntimeV2Error &error)
{
	if (!output_is_inactive(entry, error))
		return false;
	ObsDataPtr current = output_settings(entry.output);
	ObsDataPtr candidate = replace ? phase2_clone_data(requested) : clone_property_settings(current.get());
	if (!candidate)
		return fail(error, "internal_error", "could not clone output settings");
	if (!replace)
		obs_data_apply(candidate.get(), requested);
	if (!validate_output_settings(entry.output, candidate.get(), error))
		return false;
	if (output_settings_equal(current.get(), candidate.get())) {
		result.data = make_output_settings_result(handle, entry.output);
		return true;
	}
	ObsDataPtr live(obs_output_get_settings(entry.output));
	if (replace)
		obs_data_clear(live.get());
	obs_output_update(entry.output, replace ? candidate.get() : requested);
	result.data = make_output_settings_result(handle, entry.output);
	phase2_append_event(result, "output.configurationChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_output_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	result.data = make_output_settings_result(handle, entry->output);
	return true;
}

bool Engine::v2_output_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr requested;
	bool present = false;
	if (!phase2_read_object(params, "settings", requested, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	return v2_update_output_settings(*entry, handle, requested.get(), false, result, error);
}

bool Engine::v2_output_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr requested;
	bool present = false;
	if (!phase2_read_object(params, "settings", requested, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	return v2_update_output_settings(*entry, handle, requested.get(), true, result, error);
}

bool Engine::v2_output_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsPropertiesPtr properties(obs_output_properties(entry->output));
	ObsDataPtr settings(obs_output_get_settings(entry->output));
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), settings.get()).get());
	obs_data_set_array(data.get(), "properties", serialize_properties(properties.get(), settings.get()).get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  properties && (obs_properties_get_flags(properties.get()) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	result.data = std::move(data);
	return true;
}

void Engine::v2_record_output_encoder_slot(OutputEntry &entry, uint64_t output_handle, bool video, size_t slot,
					   uint64_t previous, uint64_t actual)
{
	if (video)
		entry.video_encoders[slot] = actual;
	else
		entry.audio_encoders[slot] = actual;
	const bool previous_still_bound =
		std::find(entry.video_encoders.begin(), entry.video_encoders.end(), previous) != entry.video_encoders.end() ||
		std::find(entry.audio_encoders.begin(), entry.audio_encoders.end(), previous) != entry.audio_encoders.end();
	if (previous && !previous_still_bound) {
		auto old_encoder = encoders_.find(previous);
		if (old_encoder != encoders_.end())
			old_encoder->second.bound_outputs.erase(output_handle);
	}
	if (actual) {
		auto new_encoder = encoders_.find(actual);
		if (new_encoder != encoders_.end())
			new_encoder->second.bound_outputs.insert(output_handle);
	}
}

bool validate_output_encoder_target(const OutputEntry &entry, bool video, const EncoderEntry &target,
					   RuntimeV2Error &error)
{
	if (video ? !target.video_canvas : !target.audio_track)
		return fail(error, "bad_request", "Encoder type does not match the requested output media slot");
	const char *codec = obs_encoder_get_codec(target.encoder);
	const char *supported = video ? obs_output_get_supported_video_codecs(entry.output)
				       : obs_output_get_supported_audio_codecs(entry.output);
	if (!output_codec_supported(supported, codec))
		return fail(error, "unsupported_capability", "Output does not support the Encoder codec");
	if (video ? !obs_encoder_parent_video(target.encoder) : !obs_encoder_audio(target.encoder))
		return fail(error, "not_available", "Encoder media input is unavailable");
	return true;
}

bool Engine::v2_read_output_encoder_binding(OutputEntry &entry, obs_data_t *params, bool video, size_t &slot,
					    uint64_t &requested, EncoderEntry *&target, RuntimeV2Error &error)
{
	if (!read_output_slot(params, entry, video, slot, error))
		return false;
	bool is_null = false;
	if (!read_nullable_encoder(params, requested, is_null, error))
		return false;
	target = nullptr;
	if (is_null)
		return true;
	if (!v2_get_encoder_entry(params, requested, target, error))
		return false;
	return validate_output_encoder_target(entry, video, *target, error);
}

uint64_t Engine::v2_apply_output_encoder_binding(OutputEntry &entry, EncoderEntry *target, bool video, size_t slot)
{
	if (video)
		obs_output_set_video_encoder2(entry.output, target ? target->encoder : nullptr, slot);
	else
		obs_output_set_audio_encoder(entry.output, target ? target->encoder : nullptr, slot);
	const obs_encoder_t *actual = video ? obs_output_get_video_encoder2(entry.output, slot)
					   : obs_output_get_audio_encoder(entry.output, slot);
	return v2_encoder_handle_for_pointer(actual);
}

bool Engine::v2_set_output_encoder_slot(OutputEntry &entry, uint64_t output_handle, obs_data_t *params, bool video,
					RuntimeV2Result &result, RuntimeV2Error &error)
{
	if (!output_is_inactive(entry, error))
		return false;
	const uint32_t flags = obs_output_get_flags(entry.output);
	if (!output_accepts_encoded_media(flags, video))
		return fail(error, "unsupported_capability", "output does not accept the requested encoded media type");
	size_t slot = 0;
	uint64_t requested = 0;
	EncoderEntry *target = nullptr;
	if (!v2_read_output_encoder_binding(entry, params, video, slot, requested, target, error))
		return false;
	const uint64_t previous = video ? entry.video_encoders[slot] : entry.audio_encoders[slot];
	if (previous == requested) {
		result.data = v2_output_summary(output_handle, entry);
		return true;
	}
	const uint64_t actual = v2_apply_output_encoder_binding(entry, target, video, slot);
	v2_record_output_encoder_slot(entry, output_handle, video, slot, previous, actual);
	if (actual != requested) {
		if (output_events_ && output_revisions_)
			output_events_->require_resync_after_queued_events(output_revisions_->current());
		return fail(error, "obs_error", "libobs did not accept the Encoder binding");
	}
	result.data = v2_output_summary(output_handle, entry);
	phase2_append_event(result, "encoder.bindingChanged",
				   make_encoder_binding_change(output_handle, actual, slot, video));
	phase2_append_event(result, "output.configurationChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

uint64_t Engine::v2_service_handle_for_pointer(obs_service_t *service) const
{
	if (!service)
		return 0;
	for (const auto &[handle, candidate] : services_)
		if (candidate.service == service)
			return handle;
	return 0;
}

bool Engine::v2_apply_output_service_binding(OutputEntry &entry, uint64_t output_handle, uint64_t requested,
						     ServiceEntry *service_entry, RuntimeV2Error &error)
{
	obs_output_set_service(entry.output, service_entry ? service_entry->service : nullptr);
	const uint64_t actual = v2_service_handle_for_pointer(obs_output_get_service(entry.output));
	if (actual != requested)
		return fail(error, "obs_error", "libobs did not accept the Service binding");
	const uint64_t previous = entry.service;
	entry.service = actual;
	if (previous) {
		const auto old_service = services_.find(previous);
		if (old_service != services_.end() && old_service->second.bound_output == output_handle)
			old_service->second.bound_output = 0;
	}
	if (actual)
		service_entry->bound_output = output_handle;
	return true;
}

bool Engine::v2_output_set_service(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	if (!output_is_inactive(*entry, error))
		return false;
	if (!output_flag(obs_output_get_flags(entry->output), OBS_OUTPUT_SERVICE))
		return fail(error, "unsupported_capability", "output kind does not accept a Service");
	uint64_t requested = 0;
	ServiceEntry *service_entry = nullptr;
	if (!v2_read_output_service_binding(*entry, handle, params, requested, service_entry, error))
		return false;
	if (requested == entry->service) {
		result.data = v2_output_summary(handle, *entry);
		return true;
	}
	if (!v2_apply_output_service_binding(*entry, handle, requested, service_entry, error))
		return false;
	result.data = v2_output_summary(handle, *entry);
	phase2_append_event(result, "service.bindingChanged", make_output_service_change(handle, entry->service));
	phase2_append_event(result, "output.configurationChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_output_get_service(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	if (entry->service)
		phase2_set_handle(data.get(), "service", entry->service);
	else
		obs_data_set_obj(data.get(), "service", nullptr);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_set_video_encoder(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	return v2_set_output_encoder_slot(*entry, handle, params, true, result, error);
}

bool Engine::v2_output_set_audio_encoder(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	return v2_set_output_encoder_slot(*entry, handle, params, false, result, error);
}

bool Engine::v2_output_get_encoders(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	append_output_slots(data.get(), *entry);
	result.data = std::move(data);
	return true;
}

bool read_output_service_handle(obs_data_t *params, uint64_t &requested, bool &is_null, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_nullable_handle(params, "service", requested, is_null, present) || !present)
		return fail(error, "bad_request", "params.service must be a canonical service handle string or null");
	if (is_null)
		requested = 0;
	return true;
}

bool validate_output_service_target(const OutputEntry &entry, uint64_t output_handle, ServiceEntry &service_entry,
					    RuntimeV2Error &error)
{
	if (service_entry.bound_output && service_entry.bound_output != output_handle)
		return fail(error, "object_in_use", "service is already bound to another Output");
	if (obs_service_active(service_entry.service))
		return fail(error, "busy", "active service cannot be rebound");
	const char *service_protocol = obs_service_get_protocol(service_entry.service);
	const char *output_protocols = obs_output_get_protocols(entry.output);
	if (output_protocols && *output_protocols &&
	    (!service_protocol || !output_codec_supported(output_protocols, service_protocol)))
		return fail(error, "unsupported_capability", "Service protocol is incompatible with the Output");
	return true;
}

bool Engine::v2_read_output_service_binding(OutputEntry &entry, uint64_t output_handle, obs_data_t *params,
						uint64_t &requested, ServiceEntry *&service_entry, RuntimeV2Error &error)
{
	bool is_null = false;
	if (!read_output_service_handle(params, requested, is_null, error))
		return false;
	service_entry = nullptr;
	if (is_null)
		return true;
	const auto service = services_.find(requested);
	if (service == services_.end() || !service->second.service)
		return fail(error, "not_found", "service handle was not found");
	service_entry = &service->second;
	return validate_output_service_target(entry, output_handle, *service_entry, error);
}

bool Engine::v2_output_start(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	return v2_start_output_entry(*entry, handle, result, error);
}

bool Engine::v2_output_stop(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	return v2_stop_output_entry(*entry, handle, false, result, error);
}

bool Engine::v2_output_force_stop(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	return v2_stop_output_entry(*entry, handle, true, result, error);
}

bool Engine::v2_output_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	result.data = v2_output_state(handle, *entry);
	return true;
}

bool Engine::v2_output_set_paused(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	if (!obs_output_can_pause(entry->output))
		return fail(error, "unsupported_capability", "output does not support pause");
	bool paused = false;
	bool present = false;
	if (!phase2_read_bool(params, "paused", paused, present) || !present)
		return fail(error, "bad_request", "params.paused must be a boolean");
	if (obs_output_paused(entry->output) == paused) {
		result.data = v2_output_summary(handle, *entry);
		return true;
	}
	if (!obs_output_pause(entry->output, paused) || obs_output_paused(entry->output) != paused)
		return fail(error, "invalid_state", "output pause operation was rejected");
	result.data = v2_output_summary(handle, *entry);
	result.mutated = true;
	return true;
}

bool Engine::v2_output_get_paused(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_bool(data.get(), "paused", obs_output_paused(entry->output));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_set_delay(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	if (!output_is_inactive(*entry, error))
		return false;
	uint32_t seconds = 0;
	uint32_t flags = 0;
	if (!read_delay_request(params, seconds, flags, error))
		return false;
	if (obs_output_get_delay(entry->output) == seconds && entry->delay_flags == flags) {
		result.data = make_output_delay_result(handle, *entry);
		return true;
	}
	obs_output_set_delay(entry->output, seconds, flags);
	if (obs_output_get_delay(entry->output) != seconds)
		return fail(error, "obs_error", "libobs did not accept the output delay");
	entry->delay_flags = flags;
	result.data = v2_output_summary(handle, *entry);
	phase2_append_event(result, "output.configurationChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_output_get_delay(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	result.data = make_output_delay_result(handle, *entry);
	return true;
}

bool Engine::v2_output_set_reconnect(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	if (!output_is_inactive(*entry, error))
		return false;
	bool enabled = false;
	int retry_count = 0;
	int retry_delay = 0;
	if (!read_reconnect_request(params, enabled, retry_count, retry_delay, error))
		return false;
	if (entry->reconnect_enabled == enabled && entry->retry_count == retry_count &&
	    entry->retry_delay_seconds == retry_delay) {
		result.data = make_output_reconnect_result(handle, *entry);
		return true;
	}
	obs_output_set_reconnect_settings(entry->output, enabled ? retry_count : 0, retry_delay);
	entry->reconnect_enabled = enabled;
	entry->retry_count = retry_count;
	entry->retry_delay_seconds = retry_delay;
	if (entry->observer) {
		std::lock_guard lock(entry->observer->mutex);
		entry->observer->reconnect_enabled = enabled;
	}
	result.data = v2_output_summary(handle, *entry);
	phase2_append_event(result, "output.configurationChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_output_get_reconnect(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	result.data = make_output_reconnect_result(handle, *entry);
	return true;
}

bool Engine::v2_output_get_stats(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	const float congestion = obs_output_get_congestion(entry->output);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_int(data.get(), "totalBytes",
			static_cast<long long>(std::min<uint64_t>(obs_output_get_total_bytes(entry->output),
									 static_cast<uint64_t>(std::numeric_limits<long long>::max()))));
	obs_data_set_int(data.get(), "totalFrames", std::max(obs_output_get_total_frames(entry->output), 0));
	obs_data_set_int(data.get(), "droppedFrames", std::max(obs_output_get_frames_dropped(entry->output), 0));
	if (std::isfinite(congestion))
		obs_data_set_double(data.get(), "congestion", congestion);
	else
		obs_data_set_obj(data.get(), "congestion", nullptr);
	obs_data_set_int(data.get(), "connectTimeMs", std::max(obs_output_get_connect_time_ms(entry->output), 0));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_get_last_error(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_string(data.get(), "message", v2_sanitize_output_error(*entry).c_str());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_output_get_supported_codecs(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", handle);
	obs_data_set_array(data.get(), "video", split_semicolon_values(obs_output_get_supported_video_codecs(entry->output)).get());
	obs_data_set_array(data.get(), "audio", split_semicolon_values(obs_output_get_supported_audio_codecs(entry->output)).get());
	obs_data_set_array(data.get(), "protocols", split_semicolon_values(obs_output_get_protocols(entry->output)).get());
	result.data = std::move(data);
	return true;
}

} // namespace obs_engine
