#include "runtime_phase2_common.hpp"

#include "events.hpp"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace obs_engine {

enum class TransitionPendingSignal {
	Started,
	Ended,
};

struct TransitionV2Observer {
	std::mutex mutex;
	std::condition_variable callback_cv;
	uint64_t handle = 0;
	obs_source_t *transition = nullptr;
	bool accepting = false;
	size_t callbacks_inflight = 0;
	bool running = false;
	bool overflow = false;
	std::deque<TransitionPendingSignal> pending;
};

struct TransitionV2State {
	std::mutex mutex;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	bool accepting = false;
	std::unordered_map<uint64_t, std::shared_ptr<TransitionV2Observer>> observers;
};

namespace {

constexpr size_t kMaxTransitionKindBytes = 128;
constexpr size_t kMaxTransitionNameBytes = 256;
constexpr size_t kMaxTransitionPendingSignals = 64;
constexpr uint32_t kMaxTransitionDurationMs = 600000;

bool safe_transition_identifier(const std::string &value)
{
	if (value.empty() || value.size() > kMaxTransitionKindBytes)
		return false;
	for (const unsigned char character : value) {
		if (!(std::isalnum(character) || character == '_' || character == '-' || character == '.'))
			return false;
	}
	return true;
}

bool transition_kind_exists(const std::string &kind)
{
	const char *candidate = nullptr;
	for (size_t index = 0; obs_enum_transition_types(index, &candidate); ++index) {
		if (candidate && kind == candidate)
			return true;
	}
	return false;
}

bool begin_transition_callback(TransitionV2Observer &observer)
{
	std::lock_guard<std::mutex> lock(observer.mutex);
	if (!observer.accepting)
		return false;
	observer.callbacks_inflight++;
	return true;
}

void end_transition_callback(TransitionV2Observer &observer)
{
	std::lock_guard<std::mutex> lock(observer.mutex);
	if (observer.callbacks_inflight > 0)
		observer.callbacks_inflight--;
	if (observer.callbacks_inflight == 0)
		observer.callback_cv.notify_all();
}

void queue_transition_signal(TransitionV2Observer &observer, TransitionPendingSignal signal)
{
	if (!begin_transition_callback(observer))
		return;
	try {
		std::lock_guard<std::mutex> lock(observer.mutex);
		if (signal == TransitionPendingSignal::Started) {
			if (!observer.running) {
				observer.running = true;
				if (observer.pending.size() < kMaxTransitionPendingSignals)
					observer.pending.push_back(signal);
				else {
					observer.pending.clear();
					observer.overflow = true;
				}
			}
		} else if (observer.running) {
			observer.running = false;
			if (observer.pending.size() < kMaxTransitionPendingSignals)
				observer.pending.push_back(signal);
			else {
				observer.pending.clear();
				observer.overflow = true;
			}
		}
	} catch (...) {
		std::lock_guard<std::mutex> lock(observer.mutex);
		observer.pending.clear();
		observer.overflow = true;
	}
	end_transition_callback(observer);
}

void transition_start_cb(void *data, calldata_t *)
{
	if (data)
		queue_transition_signal(*static_cast<TransitionV2Observer *>(data), TransitionPendingSignal::Started);
}

void transition_end_cb(void *data, calldata_t *)
{
	if (data)
		queue_transition_signal(*static_cast<TransitionV2Observer *>(data), TransitionPendingSignal::Ended);
}

void connect_transition_observer(const std::shared_ptr<TransitionV2Observer> &observer, obs_source_t *transition)
{
	if (!observer || !transition)
		return;
	observer->transition = transition;
	{
		std::lock_guard<std::mutex> lock(observer->mutex);
		observer->accepting = true;
	}
	signal_handler_t *handler = obs_source_get_signal_handler(transition);
	if (!handler)
		return;
	signal_handler_connect(handler, "transition_start", transition_start_cb, observer.get());
	signal_handler_connect(handler, "transition_stop", transition_end_cb, observer.get());
	signal_handler_connect(handler, "transition_video_stop", transition_end_cb, observer.get());
}

void disconnect_transition_observer(TransitionV2Observer &observer, obs_source_t *transition) noexcept
{
	if (transition) {
		signal_handler_t *handler = obs_source_get_signal_handler(transition);
		if (handler) {
			signal_handler_disconnect(handler, "transition_start", transition_start_cb, &observer);
			signal_handler_disconnect(handler, "transition_stop", transition_end_cb, &observer);
			signal_handler_disconnect(handler, "transition_video_stop", transition_end_cb, &observer);
		}
	}
	std::unique_lock<std::mutex> lock(observer.mutex);
	observer.accepting = false;
	observer.callback_cv.wait(lock, [&] { return observer.callbacks_inflight == 0; });
	observer.pending.clear();
	observer.overflow = false;
	observer.running = false;
	observer.transition = nullptr;
}

void set_nullable_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	if (handle != 0)
		phase2_set_handle(data, name, handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

void set_source_identity(const Engine &engine, obs_data_t *data, const char *name, obs_source_t *source)
{
	if (!source) {
		obs_data_set_obj(data, name, nullptr);
		return;
	}
	const uint64_t scene = engine.v2_scene_handle_for_pointer(source);
	if (scene != 0) {
		phase2_set_handle(data, name, scene);
		return;
	}
	const uint64_t source_handle = engine.v2_source_handle_for_pointer(source);
	if (source_handle != 0)
		phase2_set_handle(data, name, source_handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

ObsDataPtr make_transition_event_data(uint64_t handle, const char *state)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	obs_data_set_string(data.get(), "state", state ? state : "idle");
	return data;
}

ObsDataPtr make_transition_summary(const Engine &engine, uint64_t handle, const TransitionEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	obs_data_set_string(data.get(), "name",
			   entry.transition && obs_source_get_name(entry.transition) ? obs_source_get_name(entry.transition) : "");
	obs_data_set_string(data.get(), "kind",
			   entry.transition && obs_source_get_id(entry.transition) ? obs_source_get_id(entry.transition) : "");
	obs_data_set_int(data.get(), "durationMs", entry.duration_ms);
	const bool active = entry.transition && obs_transition_is_active(entry.transition);
	obs_data_set_string(data.get(), "state", active ? "running" : "idle");
	obs_data_set_bool(data.get(), "active", active);
	obs_data_set_double(data.get(), "progress", active ? obs_transition_get_time(entry.transition) : 0.0);
	if (entry.transition) {
		obs_source_t *source_a = obs_transition_get_source(entry.transition, OBS_TRANSITION_SOURCE_A);
		obs_source_t *source_b = obs_transition_get_source(entry.transition, OBS_TRANSITION_SOURCE_B);
		set_source_identity(engine, data.get(), "sourceA", source_a);
		set_source_identity(engine, data.get(), "sourceB", source_b);
		if (source_a)
			obs_source_release(source_a);
		if (source_b)
			obs_source_release(source_b);
	} else {
		set_nullable_handle(data.get(), "sourceA", 0);
		set_nullable_handle(data.get(), "sourceB", 0);
	}
	return data;
}

ObsDataPtr make_transition_kind_summary(const std::string &kind)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	const char *display_name = obs_source_get_display_name(kind.c_str());
	obs_data_set_string(data.get(), "name", display_name ? display_name : kind.c_str());
	obs_module_t *module = obs_source_get_module(kind.c_str());
	if (module) {
		const char *module_name = obs_get_module_file_name(module);
		if (module_name)
			obs_data_set_string(data.get(), "module", module_name);
	}
	obs_data_set_int(data.get(), "moduleLoadState", static_cast<long long>(obs_source_load_state(kind.c_str())));
	return data;
}

bool read_transition_name(obs_data_t *params, std::string &name, bool &present, RuntimeV2Error &error)
{
	if (!phase2_read_string(params, "name", name, present))
		return phase2_fail(error, "bad_request", "params.name must be a string when present");
	if (present && !phase2_is_bounded_string(name, kMaxTransitionNameBytes))
		return phase2_fail(error, "bad_request", "transition name must be non-empty and at most 256 bytes");
	return true;
}

} // namespace

void Engine::v2_bind_transition_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!transition_v2_state_)
		transition_v2_state_ = std::make_shared<TransitionV2State>();
	std::lock_guard<std::mutex> lock(transition_v2_state_->mutex);
	transition_v2_state_->revisions = revisions;
	transition_v2_state_->events = events;
	transition_v2_state_->accepting = revisions && events;
}

bool Engine::v2_get_transition_entry(obs_data_t *params, uint64_t &handle, TransitionEntry *&entry,
					     RuntimeV2Error &error)
{
	if (!phase2_read_handle(params, "transition", handle))
		return phase2_fail(error, "bad_request", "params.transition must be a canonical transition handle string");
	const auto it = transitions_.find(handle);
	if (it == transitions_.end() || !it->second.transition)
		return phase2_fail(error, "not_found", "transition handle was not found");
	entry = &it->second;
	return true;
}

bool Engine::v2_transition_kind_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	ObsArrayPtr kinds(obs_data_array_create());
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_transition_types(index, &kind); ++index)
		if (kind)
			obs_data_array_push_back(kinds.get(), make_transition_kind_summary(kind).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "kinds", kinds.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(obs_data_array_count(kinds.get())));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_transition_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::string kind;
	bool present = false;
	if (!phase2_read_string(params, "kind", kind, present) || !present || !safe_transition_identifier(kind))
		return phase2_fail(error, "bad_request", "params.kind must be a valid transition kind identifier");
	if (!transition_kind_exists(kind))
		return phase2_fail(error, "not_found", "transition kind is not registered");
	ObsDataPtr defaults(obs_get_source_defaults(kind.c_str()));
	if (!defaults)
		return phase2_fail(error, "obs_error", "transition kind did not provide defaults");
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", defaults.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_transition_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::string kind;
	bool present = false;
	if (!phase2_read_string(params, "kind", kind, present) || !present || !safe_transition_identifier(kind))
		return phase2_fail(error, "bad_request", "params.kind must be a valid transition kind identifier");
	if (!transition_kind_exists(kind))
		return phase2_fail(error, "not_found", "transition kind is not registered");
	ObsDataPtr bridge(obs_data_create());
	ObsDataPtr target(obs_data_create());
	obs_data_set_string(target.get(), "type", "transitionKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	obs_data_set_obj(bridge.get(), "target", target.get());
	return v2_properties_get(bridge.get(), result, error);
}

bool Engine::v2_transition_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::vector<uint64_t> handles;
	for (const auto &[handle, _] : transitions_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr values(obs_data_array_create());
	for (const uint64_t handle : handles)
		obs_data_array_push_back(values.get(), make_transition_summary(*this, handle, transitions_.at(handle)).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "transitions", values.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(handles.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_transition_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	result.data = make_transition_summary(*this, handle, *entry);
	return true;
}

bool Engine::v2_transition_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::string kind;
	bool kind_present = false;
	if (!phase2_read_string(params, "kind", kind, kind_present) || !kind_present || !safe_transition_identifier(kind))
		return phase2_fail(error, "bad_request", "params.kind must be a valid transition kind identifier");
	if (!transition_kind_exists(kind))
		return phase2_fail(error, "not_found", "transition kind is not registered");
	std::string requested_name;
	bool name_present = false;
	if (!read_transition_name(params, requested_name, name_present, error))
		return false;
	ObsDataPtr settings;
	bool settings_present = false;
	if (!phase2_read_object(params, "settings", settings, settings_present))
		return phase2_fail(error, "bad_request", "params.settings must be an object when present");
	if (!settings_present) {
		settings.reset(obs_get_source_defaults(kind.c_str()));
		if (!settings)
			return phase2_fail(error, "obs_error", "transition kind did not provide defaults");
	}

	const uint64_t handle = allocate_handle();
	const std::string generated_name = "engine-transition-" + std::to_string(handle);
	const std::string name = name_present ? requested_name : generated_name;
	obs_source_t *transition = obs_source_create_private(kind.c_str(), name.c_str(), settings.get());
	if (!transition)
		return phase2_fail(error, "obs_error", "obs_source_create_private did not create the transition");
	if (obs_source_get_type(transition) != OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_release(transition);
		return phase2_fail(error, "invalid_state", "registered transition kind did not create a transition source");
	}

	std::shared_ptr<TransitionV2Observer> observer;
	try {
		observer = std::make_shared<TransitionV2Observer>();
		observer->handle = handle;
		connect_transition_observer(observer, transition);
		const auto [_, inserted] = transitions_.emplace(handle, TransitionEntry{transition, observer, 300});
		if (!inserted)
			throw std::runtime_error("transition handle collision");
		if (!transition_v2_state_)
			transition_v2_state_ = std::make_shared<TransitionV2State>();
		std::lock_guard<std::mutex> lock(transition_v2_state_->mutex);
		transition_v2_state_->observers.emplace(handle, observer);
	} catch (...) {
		transitions_.erase(handle);
		if (observer)
			disconnect_transition_observer(*observer, transition);
		obs_source_release(transition);
		return phase2_fail(error, "internal_error", "could not register transition runtime state");
	}

	result.data = make_transition_summary(*this, handle, transitions_.at(handle));
	phase2_append_event(result, "transition.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_transition_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	if (studio_transition_ == handle)
		return phase2_fail(error, "object_in_use", "Transition is selected by Studio");
	if (obs_transition_is_active(entry->transition))
		return phase2_fail(error, "busy", "Transition is currently running");
	const auto observer = entry->observer;
	obs_source_t *transition = entry->transition;
	if (transition_v2_state_) {
		std::lock_guard<std::mutex> lock(transition_v2_state_->mutex);
		transition_v2_state_->observers.erase(handle);
	}
	disconnect_transition_observer(*observer, transition);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	result.data = std::move(data);
	obs_source_release(transition);
	transitions_.erase(handle);
	phase2_append_event(result, "transition.removed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_transition_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	std::string name;
	bool present = false;
	if (!read_transition_name(params, name, present, error) || !present)
		return phase2_fail(error, "bad_request", "params.name must be a non-empty transition name");
	const std::string previous = obs_source_get_name(entry->transition) ? obs_source_get_name(entry->transition) : "";
	if (previous == name) {
		result.data = make_transition_summary(*this, handle, *entry);
		return true;
	}
	obs_source_set_name(entry->transition, name.c_str());
	result.data = make_transition_summary(*this, handle, *entry);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "transition", handle);
	obs_data_set_string(event_data.get(), "name", name.c_str());
	obs_data_set_string(event_data.get(), "previousName", previous.c_str());
	phase2_append_event(result, "transition.renamed", std::move(event_data));
	result.mutated = true;
	return true;
}

bool read_transition_settings(obs_data_t *params, ObsDataPtr &settings, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_object(params, "settings", settings, present) || !present)
		return phase2_fail(error, "bad_request", "params.settings object is required");
	return true;
}

bool transition_settings_equal(obs_data_t *left, obs_data_t *right)
{
	const char *left_json = left ? obs_data_get_json(left) : nullptr;
	const char *right_json = right ? obs_data_get_json(right) : nullptr;
	return std::string(left_json ? left_json : "") == std::string(right_json ? right_json : "");
}

bool Engine::v2_transition_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	ObsDataPtr settings(obs_source_get_settings(entry->transition));
	if (!settings)
		return phase2_fail(error, "obs_error", "could not read transition settings");
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_transition_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	ObsDataPtr patch;
	if (!read_transition_settings(params, patch, error))
		return false;
	ObsDataPtr before(obs_source_get_settings(entry->transition));
	if (!before)
		return phase2_fail(error, "obs_error", "could not read transition settings before update");
	ObsDataPtr proposed = phase2_clone_data(before.get());
	if (!proposed)
		return phase2_fail(error, "internal_error", "could not clone transition settings before update");
	obs_data_apply(proposed.get(), patch.get());
	if (transition_settings_equal(before.get(), proposed.get())) {
		ObsDataPtr data(obs_data_create());
		phase2_set_handle(data.get(), "transition", handle);
		obs_data_set_obj(data.get(), "settings", before.get());
		result.data = std::move(data);
		return true;
	}
	obs_source_update(entry->transition, proposed.get());
	ObsDataPtr after(obs_source_get_settings(entry->transition));
	if (!after)
		return phase2_fail(error, "obs_error", "could not read transition settings after update");
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	obs_data_set_obj(data.get(), "settings", after.get());
	result.data = std::move(data);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "transition", handle);
	obs_data_set_obj(event_data.get(), "settings", after.get());
	phase2_append_event(result, "transition.settingsChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_transition_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	ObsDataPtr replacement;
	if (!read_transition_settings(params, replacement, error))
		return false;
	ObsDataPtr before(obs_source_get_settings(entry->transition));
	if (!before)
		return phase2_fail(error, "obs_error", "could not read transition settings before replacement");
	if (transition_settings_equal(before.get(), replacement.get())) {
		ObsDataPtr data(obs_data_create());
		phase2_set_handle(data.get(), "transition", handle);
		obs_data_set_obj(data.get(), "settings", before.get());
		result.data = std::move(data);
		return true;
	}
	obs_source_reset_settings(entry->transition, replacement.get());
	ObsDataPtr after(obs_source_get_settings(entry->transition));
	if (!after)
		return phase2_fail(error, "obs_error", "could not read transition settings after replacement");
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	obs_data_set_obj(data.get(), "settings", after.get());
	result.data = std::move(data);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "transition", handle);
	obs_data_set_obj(event_data.get(), "settings", after.get());
	phase2_append_event(result, "transition.settingsChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_transition_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	ObsDataPtr bridge(obs_data_create());
	ObsDataPtr target(obs_data_create());
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	obs_data_set_string(target.get(), "type", "transition");
	phase2_set_handle(target.get(), "transition", handle);
	obs_data_set_obj(bridge.get(), "target", target.get());
	return v2_properties_get(bridge.get(), result, error);
}

bool Engine::v2_transition_get_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", handle);
	obs_data_set_int(data.get(), "durationMs", entry->duration_ms);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_transition_set_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	long long duration = 0;
	bool present = false;
	if (!phase2_read_integer(params, "durationMs", duration, present) || !present || duration < 0 ||
	    duration > kMaxTransitionDurationMs)
		return phase2_fail(error, "bad_request", "params.durationMs is outside the supported transition range");
	if (entry->duration_ms == static_cast<uint32_t>(duration)) {
		result.data = make_transition_summary(*this, handle, *entry);
		return true;
	}
	entry->duration_ms = static_cast<uint32_t>(duration);
	result.data = make_transition_summary(*this, handle, *entry);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "transition", handle);
	obs_data_set_int(event_data.get(), "durationMs", entry->duration_ms);
	phase2_append_event(result, "transition.durationChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_transition_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	TransitionEntry *entry = nullptr;
	if (!v2_get_transition_entry(params, handle, entry, error))
		return false;
	result.data = make_transition_summary(*this, handle, *entry);
	return true;
}

bool Engine::v2_start_transition(uint64_t handle, obs_source_t *from, obs_source_t *destination,
					 uint32_t duration_ms, RuntimeV2Result &result, RuntimeV2Error &error)
{
	const auto it = transitions_.find(handle);
	if (it == transitions_.end() || !it->second.transition)
		return phase2_fail(error, "not_found", "transition handle was not found");
	TransitionEntry &entry = it->second;
	if (obs_transition_is_active(entry.transition))
		return phase2_fail(error, "busy", "transition is already running");
	if (!destination)
		return phase2_fail(error, "invalid_state", "transition destination is required");
	obs_transition_set(entry.transition, from);
	const uint32_t width = std::max(from ? obs_source_get_width(from) : 0, obs_source_get_width(destination));
	const uint32_t height = std::max(from ? obs_source_get_height(from) : 0, obs_source_get_height(destination));
	obs_transition_set_size(entry.transition, width, height);
	obs_transition_set_alignment(entry.transition, OBS_ALIGN_CENTER);
	obs_transition_set_scale_type(entry.transition, OBS_TRANSITION_SCALE_ASPECT);
	if (!obs_transition_start(entry.transition, OBS_TRANSITION_MODE_AUTO, duration_ms, destination))
		return phase2_fail(error, "invalid_state", "libobs did not start the transition");
	if (entry.observer) {
		std::lock_guard<std::mutex> lock(entry.observer->mutex);
		entry.observer->running = true;
		entry.observer->pending.erase(
			std::remove(entry.observer->pending.begin(), entry.observer->pending.end(), TransitionPendingSignal::Started),
			entry.observer->pending.end());
	}
	phase2_append_event(result, "transition.started", make_transition_event_data(handle, "running"));
	result.mutated = true;
	return true;
}

void Engine::v2_sync_transition_observers()
{
	if (!transition_v2_state_)
		return;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	std::vector<std::shared_ptr<TransitionV2Observer>> observers;
	{
		std::lock_guard<std::mutex> lock(transition_v2_state_->mutex);
		if (!transition_v2_state_->accepting)
			return;
		revisions = transition_v2_state_->revisions;
		events = transition_v2_state_->events;
		for (const auto &[_, observer] : transition_v2_state_->observers)
			observers.push_back(observer);
	}
	if (!revisions || !events)
		return;

	for (const auto &observer : observers) {
		std::deque<TransitionPendingSignal> pending;
		bool overflow = false;
		{
			std::lock_guard<std::mutex> lock(observer->mutex);
			pending = std::move(observer->pending);
			observer->pending.clear();
			overflow = observer->overflow;
			observer->overflow = false;
		}
		if (overflow) {
			if (revisions->can_commit_mutation())
				revisions->commit_mutation();
			events->require_resync_due_to_overflow(revisions->current());
		}
		for (const TransitionPendingSignal signal : pending) {
			if (!revisions->can_commit_mutation()) {
				events->require_resync_due_to_overflow(revisions->current());
				break;
			}
			const uint64_t revision = revisions->commit_mutation();
			const char *name = signal == TransitionPendingSignal::Started ? "transition.started" : "transition.ended";
			const char *state = signal == TransitionPendingSignal::Started ? "running" : "idle";
			if (signal == TransitionPendingSignal::Ended && studio_transition_active_ &&
			    observer->handle == studio_transition_) {
				// This headless engine drives video only. libobs can leave the
				// audio half of a transition active after video_stop when no audio
				// consumer exists; clear both halves before allowing the selected
				// Transition to be reused.
				if (observer->transition)
					obs_transition_clear(observer->transition);
				const uint64_t previous_scene = studio_transition_from_scene_;
				const uint64_t destination_scene = studio_transition_destination_scene_;
				auto main_it = canvases_.find(main_canvas_);
				obs_source_t *destination = nullptr;
				auto destination_it = scenes_.find(destination_scene);
				if (destination_it != scenes_.end())
					destination = obs_scene_get_source(destination_it->second);
				if (main_it != canvases_.end() && main_it->second.canvas)
					obs_canvas_set_channel(main_it->second.canvas, 0, destination);
				program_scene_ = destination ? destination_scene : 0;
				studio_transition_active_ = false;
				studio_transition_from_scene_ = 0;
				studio_transition_destination_scene_ = 0;
				ObsDataPtr program_data = v2_program_data(program_scene_);
				set_nullable_handle(program_data.get(), "previousScene", previous_scene);
				events->publish(EngineEventKind::State, "program.sceneChanged", revision, program_data.get());
			}
			events->publish(EngineEventKind::State, name, revision,
					make_transition_event_data(observer->handle, state).get());
		}
	}
}

void Engine::v2_cancel_studio_transition() noexcept
{
	if (!studio_transition_active_)
		return;
	const auto it = transitions_.find(studio_transition_);
	if (it != transitions_.end() && it->second.transition) {
		if (obs_transition_is_active(it->second.transition))
			obs_transition_force_stop(it->second.transition);
		obs_transition_clear(it->second.transition);
		if (it->second.observer) {
			std::lock_guard<std::mutex> lock(it->second.observer->mutex);
			it->second.observer->running = false;
			it->second.observer->pending.clear();
		}
	}
	studio_transition_active_ = false;
	studio_transition_from_scene_ = 0;
	studio_transition_destination_scene_ = 0;
}

void Engine::v2_prepare_transition_shutdown() noexcept
{
	if (!transition_v2_state_)
		return;
	std::vector<std::shared_ptr<TransitionV2Observer>> observers;
	{
		std::lock_guard<std::mutex> lock(transition_v2_state_->mutex);
		transition_v2_state_->accepting = false;
		transition_v2_state_->revisions = nullptr;
		transition_v2_state_->events = nullptr;
		for (auto &[_, observer] : transition_v2_state_->observers)
			observers.push_back(observer);
		transition_v2_state_->observers.clear();
	}
	for (const auto &observer : observers)
		disconnect_transition_observer(*observer, observer->transition);
}

} // namespace obs_engine
