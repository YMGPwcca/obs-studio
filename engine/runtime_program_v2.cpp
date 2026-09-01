#include "runtime_phase2_common.hpp"

#include <string>
#include <utility>

namespace obs_engine {
namespace {

ObsDataPtr make_cancelled_transition_event_data(uint64_t transition)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "transition", transition);
	obs_data_set_string(data.get(), "state", "idle");
	return data;
}

void set_nullable_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	if (handle != 0)
		phase2_set_handle(data, name, handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

bool read_program_scene_request(obs_data_t *params, const std::unordered_map<uint64_t, obs_scene_t *> &scenes,
					uint64_t &requested, RuntimeV2Error &error)
{
	bool is_null = false;
	bool present = false;
	if (!phase2_read_nullable_handle(params, "scene", requested, is_null, present) || !present)
		return phase2_fail(error, "bad_request", "params.scene must be a canonical scene handle string or null");
	if (is_null)
		requested = 0;
	if (requested != 0 && !scenes.contains(requested))
		return phase2_fail(error, "not_found", "scene handle was not found");
	return true;
}

} // namespace

uint64_t Engine::v2_current_program_scene() const
{
	if (studio_transition_active_)
		return program_scene_;
	const auto main_it = canvases_.find(main_canvas_);
	if (main_it == canvases_.end() || !main_it->second.canvas)
		return 0;
	obs_source_t *source = obs_canvas_get_channel(main_it->second.canvas, 0);
	const uint64_t scene = v2_scene_handle_for_pointer(source);
	if (source)
		obs_source_release(source);
	return scene;
}

ObsDataPtr Engine::v2_program_data(uint64_t scene_handle) const
{
	ObsDataPtr data(obs_data_create());
	set_nullable_handle(data.get(), "scene", scene_handle);
	if (main_canvas_ != 0)
		phase2_set_handle(data.get(), "canvas", main_canvas_);
	obs_data_set_bool(data.get(), "transitioning", studio_transition_active_);
	if (studio_transition_active_ && studio_transition_ != 0)
		phase2_set_handle(data.get(), "transition", studio_transition_);
	return data;
}

bool Engine::v2_program_get_scene(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	program_scene_ = v2_current_program_scene();
	result.data = v2_program_data(program_scene_);
	return true;
}

bool Engine::v2_program_set_scene(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t requested = 0;
	if (!read_program_scene_request(params, scenes_, requested, error))
		return false;
	uint64_t cancelled_transition = 0;
	const bool was_transitioning = studio_transition_active_;
	const uint64_t logical_previous_scene = program_scene_;
	if (was_transitioning) {
		const auto main_it = canvases_.find(main_canvas_);
		if (main_it == canvases_.end() || !main_it->second.canvas)
			return phase2_fail(error, "internal_error", "Main Canvas is not registered");
	}
	if (was_transitioning && !v2_cancel_studio_transition(cancelled_transition))
		return phase2_fail(error, "invalid_state", "Studio transition cancellation lost its owner");
	if (!apply_program_scene_route(requested, result, error, logical_previous_scene, was_transitioning))
		return false;
	if (cancelled_transition != 0) {
		result.mutated = true;
		phase2_append_event(result, "transition.ended", make_cancelled_transition_event_data(cancelled_transition));
	}
	return true;
}

bool Engine::apply_program_scene_route(uint64_t requested, RuntimeV2Result &result, RuntimeV2Error &error,
				       uint64_t previous_scene_override, bool has_previous_scene_override)
{
	const auto main_it = canvases_.find(main_canvas_);
	if (main_it == canvases_.end() || !main_it->second.canvas)
		return phase2_fail(error, "internal_error", "Main Canvas is not registered");
	obs_source_t *desired = nullptr;
	if (requested != 0)
		desired = obs_scene_get_source(scenes_.at(requested));
	obs_source_t *current = obs_canvas_get_channel(main_it->second.canvas, 0);
	const bool unchanged = current == desired;
	const uint64_t previous = has_previous_scene_override ? previous_scene_override : v2_scene_handle_for_pointer(current);
	if (current)
		obs_source_release(current);
	if (unchanged) {
		program_scene_ = previous;
		result.data = v2_program_data(program_scene_);
		return true;
	}

	obs_canvas_set_channel(main_it->second.canvas, 0, desired);
	obs_source_t *actual = obs_canvas_get_channel(main_it->second.canvas, 0);
	const bool applied = actual == desired;
	const uint64_t actual_scene = v2_scene_handle_for_pointer(actual);
	if (actual)
		obs_source_release(actual);
	if (!applied)
		return phase2_fail(error, "obs_error", "Main Canvas did not accept the requested Program scene");

	program_scene_ = actual_scene;
	result.data = v2_program_data(program_scene_);
	ObsDataPtr event_data(obs_data_create());
	set_nullable_handle(event_data.get(), "scene", program_scene_);
	set_nullable_handle(event_data.get(), "previousScene", previous);
	if (main_canvas_ != 0)
		phase2_set_handle(event_data.get(), "canvas", main_canvas_);
	phase2_append_event(result, "program.sceneChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

} // namespace obs_engine
