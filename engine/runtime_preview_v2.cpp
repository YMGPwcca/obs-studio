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

} // namespace

ObsDataPtr Engine::v2_preview_data() const
{
	ObsDataPtr data(obs_data_create());
	set_nullable_handle(data.get(), "scene", preview_scene_);
	uint64_t canvas_handle = 0;
	obs_canvas_t *canvas = nullptr;
	if (preview_scene_ != 0) {
		const auto scene_canvas = scene_canvases_.find(preview_scene_);
		const auto scene = scenes_.find(preview_scene_);
		if (scene_canvas != scene_canvases_.end() && scene != scenes_.end()) {
			canvas_handle = scene_canvas->second;
			const auto canvas_it = canvases_.find(canvas_handle);
			if (canvas_it != canvases_.end())
				canvas = canvas_it->second.canvas;
		}
	}
	set_nullable_handle(data.get(), "canvas", canvas_handle);
	obs_data_set_bool(data.get(), "hasScene", preview_scene_ != 0 && canvas != nullptr);

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
	bool is_null = false;
	bool present = false;
	if (!phase2_read_nullable_handle(params, "scene", requested, is_null, present) || !present)
		return phase2_fail(error, "bad_request", "params.scene must be a canonical scene handle string or null");
	if (is_null)
		requested = 0;
	if (requested != 0 && !scenes_.contains(requested))
		return phase2_fail(error, "not_found", "scene handle was not found");
	if (preview_scene_ == requested) {
		result.data = v2_preview_data();
		return true;
	}

	const uint64_t previous = preview_scene_;
	preview_scene_ = requested;
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
