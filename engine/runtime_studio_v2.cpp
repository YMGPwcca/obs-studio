#include "runtime_phase2_common.hpp"

#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace obs_engine {
namespace {

constexpr uint32_t kMaxStudioTransitionDurationMs = 600000;

void set_nullable_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	if (handle != 0)
		phase2_set_handle(data, name, handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

ObsDataPtr make_studio_transition_data(uint64_t transition, uint32_t duration_ms, uint64_t from_scene,
					       uint64_t destination_scene, const char *state)
{
	ObsDataPtr data(obs_data_create());
	set_nullable_handle(data.get(), "transition", transition);
	set_nullable_handle(data.get(), "fromScene", from_scene);
	set_nullable_handle(data.get(), "destinationScene", destination_scene);
	obs_data_set_int(data.get(), "durationMs", duration_ms);
	obs_data_set_string(data.get(), "state", state ? state : "idle");
	return data;
}

bool validate_studio_transition_request(bool enabled, uint64_t transition,
					       bool active,
					       const std::unordered_map<uint64_t, TransitionEntry> &transitions,
					       RuntimeV2Error &error)
{
	if (!enabled)
		return phase2_fail(error, "invalid_state", "Studio is disabled");
	if (transition == 0)
		return phase2_fail(error, "not_available", "Studio has no selected Transition");
	const auto transition_it = transitions.find(transition);
	if (transition_it == transitions.end() || !transition_it->second.transition)
		return phase2_fail(error, "not_found", "selected Transition was not found");
	if (active || obs_transition_is_active(transition_it->second.transition))
		return phase2_fail(error, "busy", "Studio Transition is already running");
	return true;
}

} // namespace

bool Engine::v2_studio_get_enabled(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "enabled", studio_enabled_);
	obs_data_set_bool(data.get(), "transitioning", studio_transition_active_);
	set_nullable_handle(data.get(), "transition", studio_transition_);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_studio_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	bool enabled = false;
	bool present = false;
	if (!phase2_read_bool(params, "enabled", enabled, present) || !present)
		return phase2_fail(error, "bad_request", "params.enabled must be boolean");
	if (studio_enabled_ == enabled) {
		return v2_studio_get_enabled(params, result, error);
	}
	studio_enabled_ = enabled;
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "enabled", studio_enabled_);
	obs_data_set_bool(data.get(), "transitioning", studio_transition_active_);
	result.data = std::move(data);
	ObsDataPtr event_data(obs_data_create());
	obs_data_set_bool(event_data.get(), "enabled", studio_enabled_);
	phase2_append_event(result, "studio.enabledChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_studio_get_transition(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	ObsDataPtr data(obs_data_create());
	set_nullable_handle(data.get(), "transition", studio_transition_);
	obs_data_set_bool(data.get(), "enabled", studio_enabled_);
	obs_data_set_bool(data.get(), "transitioning", studio_transition_active_);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_studio_set_transition(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t requested = 0;
	bool is_null = false;
	bool present = false;
	if (!phase2_read_nullable_handle(params, "transition", requested, is_null, present) || !present)
		return phase2_fail(error, "bad_request", "params.transition must be a canonical transition handle string or null");
	if (is_null)
		requested = 0;
	if (studio_transition_active_)
		return phase2_fail(error, "busy", "Studio cannot change Transition while a transition is running");
	if (requested != 0 && !transitions_.contains(requested))
		return phase2_fail(error, "not_found", "transition handle was not found");
	if (studio_transition_ == requested)
		return v2_studio_get_transition(params, result, error);
	studio_transition_ = requested;
	ObsDataPtr data(obs_data_create());
	set_nullable_handle(data.get(), "transition", studio_transition_);
	result.data = std::move(data);
	ObsDataPtr event_data(obs_data_create());
	set_nullable_handle(event_data.get(), "transition", studio_transition_);
	phase2_append_event(result, "studio.transitionChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_studio_get_transition_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	if (studio_transition_ == 0)
		return phase2_fail(error, "not_available", "Studio has no selected Transition");
	const auto it = transitions_.find(studio_transition_);
	if (it == transitions_.end() || !it->second.transition)
		return phase2_fail(error, "not_found", "selected Transition was not found");
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", studio_transition_);
	obs_data_set_int(data.get(), "durationMs", it->second.duration_ms);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_studio_set_transition_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	if (studio_transition_ == 0)
		return phase2_fail(error, "not_available", "Studio has no selected Transition");
	const auto it = transitions_.find(studio_transition_);
	if (it == transitions_.end() || !it->second.transition)
		return phase2_fail(error, "not_found", "selected Transition was not found");
	long long duration = 0;
	bool present = false;
	if (!phase2_read_integer(params, "durationMs", duration, present) || !present || duration < 0 ||
	    duration > kMaxStudioTransitionDurationMs)
		return phase2_fail(error, "bad_request", "params.durationMs is outside the supported Studio range");
	if (it->second.duration_ms == static_cast<uint32_t>(duration)) {
		return v2_studio_get_transition_duration(params, result, error);
	}
	it->second.duration_ms = static_cast<uint32_t>(duration);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", studio_transition_);
	obs_data_set_int(data.get(), "durationMs", it->second.duration_ms);
	result.data = std::move(data);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "transition", studio_transition_);
	obs_data_set_int(event_data.get(), "durationMs", it->second.duration_ms);
	phase2_append_event(result, "studio.transitionDurationChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::begin_studio_transition(uint64_t program_scene, uint64_t preview_scene, obs_source_t *preview_source,
					     RuntimeV2Result &result, RuntimeV2Error &error)
{
	const auto transition_it = transitions_.find(studio_transition_);
	if (transition_it == transitions_.end() || !transition_it->second.transition) {
		obs_source_release(preview_source);
		return phase2_fail(error, "not_found", "selected Transition was not found");
	}
	obs_source_t *program_source = obs_get_output_source(0);
	const uint32_t duration = transition_it->second.duration_ms;
	if (!v2_start_transition(studio_transition_, program_source, preview_source, duration, result, error)) {
		if (program_source)
			obs_source_release(program_source);
		obs_source_release(preview_source);
		return false;
	}
	studio_transition_active_ = true;
	studio_transition_from_scene_ = program_scene;
	studio_transition_destination_scene_ = preview_scene;
	const auto main_it = canvases_.find(main_canvas_);
	if (main_it == canvases_.end() || !main_it->second.canvas) {
		v2_cancel_studio_transition();
		if (program_source)
			obs_source_release(program_source);
		obs_source_release(preview_source);
		return phase2_fail(error, "internal_error", "Main Canvas is not registered");
	}
	obs_canvas_set_channel(main_it->second.canvas, 0, transition_it->second.transition);
	if (program_source)
		obs_source_release(program_source);
	obs_source_release(preview_source);
	result.data = make_studio_transition_data(studio_transition_, duration, program_scene, preview_scene, "running");
	return true;
}

bool Engine::v2_studio_transition(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	if (!validate_studio_transition_request(studio_enabled_, studio_transition_, studio_transition_active_, transitions_, error))
		return false;

	uint64_t preview_scene = 0;
	obs_source_t *preview_source = nullptr;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_scene = preview_scene_;
		preview_source = obs_source_get_ref(preview_source_);
	}
	if (preview_scene == 0 || !preview_source) {
		if (preview_source)
			obs_source_release(preview_source);
		return phase2_fail(error, "invalid_state", "Preview must contain a live Scene");
	}
	const uint64_t program_scene = v2_current_program_scene();
	if (program_scene != 0 && program_scene == preview_scene) {
		const auto transition_it = transitions_.find(studio_transition_);
		obs_source_release(preview_source);
		result.data = make_studio_transition_data(studio_transition_, transition_it->second.duration_ms, program_scene,
							 preview_scene, "idle");
		return true;
	}
	return begin_studio_transition(program_scene, preview_scene, preview_source, result, error);
}

} // namespace obs_engine
