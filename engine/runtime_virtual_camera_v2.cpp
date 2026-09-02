#include "runtime.hpp"

#include "runtime_phase2_common.hpp"

#include <obs.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace obs_engine {
namespace {

constexpr char kVirtualCameraOutputKind[] = "virtualcam_output";

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "virtual camera operation failed";
	return false;
}

const char *virtual_target_name(VirtualCameraTargetKind target)
{
	switch (target) {
	case VirtualCameraTargetKind::Program:
		return "program";
	case VirtualCameraTargetKind::Preview:
		return "preview";
	case VirtualCameraTargetKind::Scene:
		return "scene";
	case VirtualCameraTargetKind::Source:
		return "source";
	case VirtualCameraTargetKind::Canvas:
		return "canvas";
	}
	return "unavailable";
}

bool parse_virtual_target_name(std::string_view value, VirtualCameraTargetKind &target)
{
	if (value == "program")
		target = VirtualCameraTargetKind::Program;
	else if (value == "preview")
		target = VirtualCameraTargetKind::Preview;
	else if (value == "scene")
		target = VirtualCameraTargetKind::Scene;
	else if (value == "source")
		target = VirtualCameraTargetKind::Source;
	else if (value == "canvas")
		target = VirtualCameraTargetKind::Canvas;
	else
		return false;
	return true;
}

bool read_virtual_target_object(obs_data_t *params, VirtualCameraTargetKind &target, ObsDataPtr &object,
					RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_object(params, "target", object, present) || !present || !object)
		return fail(error, "bad_request", "params.target must be an object");
	std::string type;
	if (!phase2_read_string(object.get(), "type", type, present) || !present ||
	    !parse_virtual_target_name(type, target))
		return fail(error, "bad_request", "target.type is unsupported");
	return true;
}

const char *virtual_target_handle_field(VirtualCameraTargetKind target)
{
	if (target == VirtualCameraTargetKind::Scene)
		return "scene";
	if (target == VirtualCameraTargetKind::Source)
		return "source";
	if (target == VirtualCameraTargetKind::Canvas)
		return "canvas";
	return nullptr;
}

bool read_virtual_target_handle(obs_data_t *object, VirtualCameraTargetKind target, uint64_t &handle,
					RuntimeV2Error &error)
{
	handle = 0;
	const char *field = virtual_target_handle_field(target);
	if (field && !phase2_read_handle(object, field, handle))
		return fail(error, "bad_request", "target handle must be canonical");
	return true;
}

bool read_virtual_target(obs_data_t *params, VirtualCameraTargetKind &target, uint64_t &handle,
				 RuntimeV2Error &error)
{
	ObsDataPtr object;
	return read_virtual_target_object(params, target, object, error) &&
	       read_virtual_target_handle(object.get(), target, handle, error);
}

struct VirtualTargetMedia {
	video_t *video = nullptr;
	obs_canvas_t *private_canvas = nullptr;
	obs_canvas_t *held_canvas = nullptr;
};

void release_virtual_target_media(VirtualTargetMedia &media)
{
	if (media.private_canvas)
	{
		obs_canvas_remove(media.private_canvas);
		obs_canvas_release(media.private_canvas);
	}
	if (media.held_canvas)
		obs_canvas_release(media.held_canvas);
	media = VirtualTargetMedia{};
}

bool bind_virtual_program_media(VirtualTargetMedia &media, RuntimeV2Error &error)
{
	media.video = obs_get_video();
	return media.video || fail(error, "not_available", "program video context is unavailable");
}

bool bind_virtual_canvas_media(Engine &engine, uint64_t handle, VirtualTargetMedia &media, RuntimeV2Error &error)
{
	obs_canvas_t *canvas = engine.v2_canvas_for_handle(handle);
	if (!canvas || obs_canvas_removed(canvas))
		return fail(error, "not_found", "Virtual Camera canvas target was not found");
	media.held_canvas = obs_canvas_get_ref(canvas);
	media.video = media.held_canvas ? obs_canvas_get_video(media.held_canvas) : nullptr;
	if (!media.video) {
		release_virtual_target_media(media);
		return fail(error, "not_available", "Virtual Camera canvas has no video context");
	}
	return true;
}

obs_source_t *virtual_target_source(Engine &engine, VirtualCameraTargetKind target, uint64_t handle,
					    obs_source_t *preview_source)
{
	if (target == VirtualCameraTargetKind::Preview)
		return preview_source;
	if (target == VirtualCameraTargetKind::Scene) {
		obs_scene_t *scene = engine.v2_scene_for_handle(handle);
		return scene ? obs_scene_get_source(scene) : nullptr;
	}
	return target == VirtualCameraTargetKind::Source ? engine.v2_source_for_handle(handle) : nullptr;
}

bool create_private_source_media(obs_source_t *source, VirtualTargetMedia &media, RuntimeV2Error &error)
{
	if (!source || obs_source_removed(source))
		return fail(error, "not_found", "Virtual Camera source target was not found");
	obs_video_info video = {};
	if (!obs_get_video_info(&video))
		return fail(error, "not_available", "Virtual Camera target video settings are unavailable");
	media.private_canvas = obs_canvas_create_private("engine-virtual-camera-target", &video, DEVICE);
	if (!media.private_canvas)
		return fail(error, "obs_error", "could not create the Virtual Camera target Canvas");
	obs_canvas_set_channel(media.private_canvas, 0, source);
	media.video = obs_canvas_get_video(media.private_canvas);
	if (!media.video) {
		release_virtual_target_media(media);
		return fail(error, "not_available", "Virtual Camera target Canvas has no video context");
	}
	return true;
}

bool create_private_target_media(Engine &engine, VirtualCameraTargetKind target, uint64_t handle,
					 obs_source_t *preview_source, VirtualTargetMedia &media, RuntimeV2Error &error)
{
	if (target == VirtualCameraTargetKind::Program)
		return bind_virtual_program_media(media, error);
	if (target == VirtualCameraTargetKind::Canvas)
		return bind_virtual_canvas_media(engine, handle, media, error);
	return create_private_source_media(virtual_target_source(engine, target, handle, preview_source), media, error);
}

ObsDataPtr make_virtual_target_data(VirtualCameraTargetKind target, uint64_t handle, bool available)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "type", virtual_target_name(target));
	if (target == VirtualCameraTargetKind::Scene)
		phase2_set_handle(data.get(), "scene", handle);
	else if (target == VirtualCameraTargetKind::Source)
		phase2_set_handle(data.get(), "source", handle);
	else if (target == VirtualCameraTargetKind::Canvas)
		phase2_set_handle(data.get(), "canvas", handle);
	obs_data_set_bool(data.get(), "available", available);
	return data;
}

ObsDataPtr make_virtual_config(const VirtualCameraEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "configured", entry.output != 0);
	if (entry.output != 0)
		phase2_set_handle(data.get(), "output", entry.output);
	else
		obs_data_set_obj(data.get(), "output", nullptr);
	obs_data_set_obj(data.get(), "target",
			make_virtual_target_data(entry.target, entry.target_handle, entry.target_available).get());
	obs_data_set_bool(data.get(), "targetAvailable", entry.target_available);
	return data;
}

bool virtual_camera_output_registered()
{
	return obs_output_load_state(kVirtualCameraOutputKind) == OBS_MODULE_ENABLED &&
	       (obs_get_output_flags(kVirtualCameraOutputKind) & OBS_OUTPUT_VIDEO) != 0;
}

ObsDataPtr virtual_output_params(uint64_t output_handle)
{
	ObsDataPtr params(obs_data_create());
	phase2_set_handle(params.get(), "output", output_handle);
	return params;
}

} // namespace

bool Engine::v2_virtual_camera_target_in_use(VirtualCameraTargetKind kind, uint64_t handle) const
{
	return virtual_camera_.output != 0 && virtual_camera_.target == kind && virtual_camera_.target_handle == handle;
}

bool Engine::v2_read_virtual_camera_target(obs_data_t *params, VirtualCameraTargetKind &target, uint64_t &handle,
						   obs_source_t *&preview_source, RuntimeV2Error &error) const
{
	preview_source = nullptr;
	if (!read_virtual_target(params, target, handle, error))
		return false;
	if (target != VirtualCameraTargetKind::Preview)
		return true;
	std::lock_guard lock(preview_outputs_mutex_);
	handle = preview_scene_;
	if (preview_source_)
		preview_source = obs_source_get_ref(preview_source_);
	return preview_source || fail(error, "not_available", "Preview has no selected Scene target");
}

bool Engine::v2_virtual_camera_output_idle(const OutputEntry &entry, RuntimeV2Error &error) const
{
	ObsDataPtr state = v2_output_state(virtual_camera_.output, entry);
	const char *state_name = obs_data_get_string(state.get(), "state");
	return state_name && std::string_view(state_name) == "idle"
		       ? true
		       : fail(error, "busy", "Virtual Camera operation requires an idle Output");
}

bool Engine::v2_virtual_camera_scene_target_in_use(uint64_t handle) const
{
	return v2_virtual_camera_target_in_use(VirtualCameraTargetKind::Scene, handle) ||
	       v2_virtual_camera_target_in_use(VirtualCameraTargetKind::Preview, handle);
}

bool Engine::v2_apply_virtual_camera_target(OutputEntry &output, VirtualCameraTargetKind target, uint64_t handle,
						    obs_source_t *preview_source, RuntimeV2Result &result,
						    RuntimeV2Error &error)
{
	if (target == virtual_camera_.target && handle == virtual_camera_.target_handle) {
		if (preview_source)
			obs_source_release(preview_source);
		result.data = make_virtual_config(virtual_camera_);
		return true;
	}
	if (!v2_virtual_camera_output_idle(output, error)) {
		if (preview_source)
			obs_source_release(preview_source);
		return false;
	}
	VirtualTargetMedia media;
	const bool bound = create_private_target_media(*this, target, handle, preview_source, media, error);
	if (preview_source)
		obs_source_release(preview_source);
	if (!bound)
		return false;
	obs_output_set_media(output.output, media.video, nullptr);
	VirtualTargetMedia old{nullptr, virtual_camera_.private_target_canvas, virtual_camera_.held_target_canvas};
	virtual_camera_.target = target;
	virtual_camera_.target_handle = handle;
	virtual_camera_.private_target_canvas = media.private_canvas;
	virtual_camera_.held_target_canvas = media.held_canvas;
	virtual_camera_.target_available = true;
	media.private_canvas = nullptr;
	media.held_canvas = nullptr;
	release_virtual_target_media(old);
	result.data = make_virtual_config(virtual_camera_);
	ObsDataPtr event_data = make_virtual_target_data(target, handle, true);
	phase2_set_handle(event_data.get(), "output", virtual_camera_.output);
	phase2_append_event(result, "virtualCamera.targetChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

void Engine::v2_prepare_virtual_camera_shutdown() noexcept
{
	VirtualTargetMedia media{nullptr, virtual_camera_.private_target_canvas, virtual_camera_.held_target_canvas};
	virtual_camera_.private_target_canvas = nullptr;
	virtual_camera_.held_target_canvas = nullptr;
	release_virtual_target_media(media);
	virtual_camera_ = VirtualCameraEntry{};
}

bool Engine::v2_virtual_camera_get_capabilities(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	const bool output_registered = virtual_camera_output_registered();
	bool active = false;
	bool busy = false;
	if (virtual_camera_.output) {
		const auto output = outputs_.find(virtual_camera_.output);
		if (output != outputs_.end() && output->second.output) {
			ObsDataPtr state(v2_output_state(virtual_camera_.output, output->second));
			const char *state_name = obs_data_get_string(state.get(), "state");
			active = state_name && std::string_view(state_name) == "active";
			busy = state_name && std::string_view(state_name) != "idle";
		}
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "apiImplemented", true);
	obs_data_set_bool(data.get(), "outputModulePresent", obs_get_module("win-dshow") != nullptr);
	obs_data_set_bool(data.get(), "backendModulePresent", obs_get_module("obs-virtualcam-module") != nullptr);
	obs_data_set_bool(data.get(), "outputRegistered", output_registered);
	obs_data_set_bool(data.get(), "backendReady", output_registered);
	obs_data_set_bool(data.get(), "available", output_registered);
	obs_data_set_bool(data.get(), "active", active);
	obs_data_set_bool(data.get(), "busy", busy);
	obs_data_set_string(data.get(), "outputKind", kVirtualCameraOutputKind);
	ObsArrayPtr targets(obs_data_array_create());
	for (const char *target : {"program", "preview", "scene", "source", "canvas"}) {
		ObsDataPtr item(obs_data_create());
		obs_data_set_string(item.get(), "type", target);
		obs_data_array_push_back(targets.get(), item.get());
	}
	obs_data_set_array(data.get(), "targetTypes", targets.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_virtual_camera_configure(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (virtual_camera_.output) {
		result.data = make_virtual_config(virtual_camera_);
		return true;
	}
	if (!virtual_camera_output_registered())
		return fail(error, "unsupported_capability", "packaged Windows Virtual Camera backend is unavailable");
	ObsDataPtr params(obs_data_create());
	obs_data_set_string(params.get(), "kind", kVirtualCameraOutputKind);
	obs_data_set_string(params.get(), "name", "virtual-camera");
	RuntimeV2Result created;
	RuntimeV2Error create_error;
	if (!v2_output_create(params.get(), created, create_error)) {
		error = std::move(create_error);
		return false;
	}
	uint64_t output_handle = 0;
	if (!created.data || !phase2_read_handle(created.data.get(), "output", output_handle)) {
		ObsDataPtr remove_params(obs_data_create());
		phase2_set_handle(remove_params.get(), "output", output_handle);
		RuntimeV2Result ignored;
		RuntimeV2Error ignored_error;
		if (output_handle)
			v2_output_remove(remove_params.get(), ignored, ignored_error);
		return fail(error, "internal_error", "Virtual Camera Output handle was not returned");
	}
	const auto output = outputs_.find(output_handle);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "internal_error", "Virtual Camera Output registration was lost");
	VirtualTargetMedia media;
	if (!create_private_target_media(*this, VirtualCameraTargetKind::Program, 0, nullptr, media, error)) {
		ObsDataPtr remove_params(obs_data_create());
		phase2_set_handle(remove_params.get(), "output", output_handle);
		RuntimeV2Result ignored;
		RuntimeV2Error ignored_error;
		v2_output_remove(remove_params.get(), ignored, ignored_error);
		return false;
	}
	obs_output_set_media(output->second.output, media.video, nullptr);
	virtual_camera_.output = output_handle;
	virtual_camera_.target = VirtualCameraTargetKind::Program;
	virtual_camera_.target_handle = 0;
	virtual_camera_.private_target_canvas = media.private_canvas;
	virtual_camera_.held_target_canvas = media.held_canvas;
	virtual_camera_.target_available = true;
	media.private_canvas = nullptr;
	media.held_canvas = nullptr;
	result.events = std::move(created.events);
	result.data = make_virtual_config(virtual_camera_);
	phase2_append_event(result, "virtualCamera.configChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_virtual_camera_unconfigure(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!virtual_camera_.output)
		return fail(error, "invalid_state", "Virtual Camera is not configured");
	const uint64_t output_handle = virtual_camera_.output;
	const auto output = outputs_.find(output_handle);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "not_found", "configured Virtual Camera Output was not found");
	if (!v2_output_is_inactive(output->second, error))
		return false;
	virtual_camera_.output = 0;
	ObsDataPtr remove_params(obs_data_create());
	phase2_set_handle(remove_params.get(), "output", output_handle);
	RuntimeV2Result removed;
	RuntimeV2Error remove_error;
	if (!v2_output_remove(remove_params.get(), removed, remove_error)) {
		virtual_camera_.output = output_handle;
		error = std::move(remove_error);
		return false;
	}
	result.events = std::move(removed.events);
	VirtualTargetMedia media{nullptr, virtual_camera_.private_target_canvas, virtual_camera_.held_target_canvas};
	virtual_camera_.private_target_canvas = nullptr;
	virtual_camera_.held_target_canvas = nullptr;
	release_virtual_target_media(media);
	virtual_camera_ = VirtualCameraEntry{};
	result.data = make_virtual_config(virtual_camera_);
	phase2_append_event(result, "virtualCamera.configChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_virtual_camera_start(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!virtual_camera_.output)
		return fail(error, "invalid_state", "Virtual Camera is not configured");
	if (!virtual_camera_.target_available)
		return fail(error, "not_available", "Virtual Camera target is unavailable");
	return v2_output_start(virtual_output_params(virtual_camera_.output).get(), result, error);
}

bool Engine::v2_virtual_camera_stop(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!virtual_camera_.output)
		return fail(error, "invalid_state", "Virtual Camera is not configured");
	return v2_output_stop(virtual_output_params(virtual_camera_.output).get(), result, error);
}

bool Engine::v2_virtual_camera_get_state(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!virtual_camera_.output)
		return fail(error, "invalid_state", "Virtual Camera is not configured");
	const auto output = outputs_.find(virtual_camera_.output);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "not_found", "configured Virtual Camera Output was not found");
	ObsDataPtr data(v2_output_state(virtual_camera_.output, output->second));
	obs_data_set_bool(data.get(), "configured", true);
	obs_data_set_bool(data.get(), "targetAvailable", virtual_camera_.target_available);
	obs_data_set_obj(data.get(), "target",
			make_virtual_target_data(virtual_camera_.target, virtual_camera_.target_handle,
						 virtual_camera_.target_available)
				.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_virtual_camera_set_target(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!virtual_camera_.output)
		return fail(error, "invalid_state", "Virtual Camera is not configured");
	const auto output = outputs_.find(virtual_camera_.output);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "not_found", "configured Virtual Camera Output was not found");
	VirtualCameraTargetKind target;
	uint64_t handle = 0;
	obs_source_t *preview_source = nullptr;
	if (!v2_read_virtual_camera_target(params, target, handle, preview_source, error))
		return false;
	return v2_apply_virtual_camera_target(output->second, target, handle, preview_source, result, error);
}

bool Engine::v2_virtual_camera_get_target(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "configured", virtual_camera_.output != 0);
	if (virtual_camera_.output)
		phase2_set_handle(data.get(), "output", virtual_camera_.output);
	else
		obs_data_set_obj(data.get(), "output", nullptr);
	obs_data_set_bool(data.get(), "targetAvailable", virtual_camera_.target_available);
	obs_data_set_obj(data.get(), "target",
			make_virtual_target_data(virtual_camera_.target, virtual_camera_.target_handle,
						 virtual_camera_.target_available)
				.get());
	result.data = std::move(data);
	return true;
}

} // namespace obs_engine
