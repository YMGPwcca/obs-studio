#include "runtime_phase2_common.hpp"

#include <utility>

namespace obs_engine {
namespace {

void set_nullable_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	if (handle != 0)
		phase2_set_handle(data, name, handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

bool read_preview_scene_request(obs_data_t *params, const std::unordered_map<uint64_t, obs_scene_t *> &scenes,
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

ObsDataPtr Engine::v2_preview_data() const
{
	ObsDataPtr data(obs_data_create());
	uint64_t preview_scene = 0;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_scene = preview_scene_;
	}
	set_nullable_handle(data.get(), "scene", preview_scene);
	uint64_t canvas_handle = 0;
	obs_canvas_t *canvas = nullptr;
	if (preview_scene != 0) {
		const auto scene_canvas = scene_canvases_.find(preview_scene);
		const auto scene = scenes_.find(preview_scene);
		if (scene_canvas != scene_canvases_.end() && scene != scenes_.end()) {
			canvas_handle = scene_canvas->second;
			const auto canvas_it = canvases_.find(canvas_handle);
			if (canvas_it != canvases_.end())
				canvas = canvas_it->second.canvas;
		}
	}
	set_nullable_handle(data.get(), "canvas", canvas_handle);
	obs_data_set_bool(data.get(), "hasScene", preview_scene != 0 && canvas != nullptr);

	if (!canvas) {
		const auto main_it = canvases_.find(main_canvas_);
		if (main_it != canvases_.end())
			canvas = main_it->second.canvas;
	}
	obs_video_info video = {};
	if (canvas && obs_canvas_get_video_info(canvas, &video)) {
		obs_data_set_int(data.get(), "renderWidth", video.output_width);
		obs_data_set_int(data.get(), "renderHeight", video.output_height);
	} else {
		obs_data_set_int(data.get(), "renderWidth", 0);
		obs_data_set_int(data.get(), "renderHeight", 0);
	}
	return data;
}

void Engine::v2_clear_preview_source() noexcept
{
	obs_source_t *previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_scene_ = 0;
		previous = preview_source_;
		preview_source_ = nullptr;
	}
	if (previous)
		obs_source_release(previous);
}

bool Engine::v2_preview_get_scene(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	result.data = v2_preview_data();
	return true;
}

bool Engine::v2_preview_set_scene(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t requested = 0;
	if (!read_preview_scene_request(params, scenes_, requested, error))
		return false;
	bool unchanged = false;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		unchanged = preview_scene_ == requested;
	}
	if (unchanged) {
		result.data = v2_preview_data();
		return true;
	}

	obs_source_t *next_source = nullptr;
	if (requested != 0)
		next_source = obs_source_get_ref(obs_scene_get_source(scenes_.at(requested)));
	if (requested != 0 && !next_source)
		return phase2_fail(error, "not_found", "scene source was not available");

	uint64_t previous = 0;
	obs_source_t *previous_source = nullptr;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		previous = preview_scene_;
		previous_source = preview_source_;
		preview_scene_ = requested;
		preview_source_ = next_source;
	}
	if (previous_source)
		obs_source_release(previous_source);

	result.data = v2_preview_data();
	ObsDataPtr event_data(obs_data_create());
	set_nullable_handle(event_data.get(), "scene", requested);
	set_nullable_handle(event_data.get(), "previousScene", previous);
	if (requested != 0) {
		const auto scene_canvas = scene_canvases_.find(requested);
		if (scene_canvas != scene_canvases_.end())
			phase2_set_handle(event_data.get(), "canvas", scene_canvas->second);
	}
	phase2_append_event(result, "preview.sceneChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_preview_get_info(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	result.data = v2_preview_data();
	return true;
}

} // namespace obs_engine
