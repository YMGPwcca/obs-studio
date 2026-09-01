#include "runtime_phase2_common.hpp"

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obs_engine {
namespace {

constexpr uint32_t kMinPreviewOutputDimension = 16;
constexpr uint32_t kMaxPreviewOutputDimension = 16384;
constexpr uint32_t kPreviewAcquireTimeoutMs = 16;

enum class PreviewOutputTarget {
	Program,
	Preview,
	Scene,
	Source,
	Canvas,
};

enum class PreviewOutputScale {
	Fit,
	Fill,
	Stretch,
	OneToOne,
};

struct PreviewTextureV2Resource {
	gs_texture_t *texture = nullptr;
	gs_texrender_t *staging = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint64_t generation = 0;
	uint64_t shared_handle = 0;
	std::string adapter_luid;
	bool producer_acquired = false;
};

} // namespace

struct PreviewOutputTargetBinding {
	PreviewOutputTarget target = PreviewOutputTarget::Preview;
	uint64_t handle = 0;
	bool available = true;
	obs_source_t *source = nullptr;
	obs_canvas_t *canvas = nullptr;

	~PreviewOutputTargetBinding()
	{
		if (source)
			obs_source_release(source);
		if (canvas)
			obs_canvas_release(canvas);
	}
};

struct PreviewOutputV2State {
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputTargetBinding> target;
	PreviewOutputScale scale = PreviewOutputScale::Fit;
	bool enabled = true;
	bool consumer_attached = false;
	std::shared_ptr<PreviewTextureV2Resource> resource;
	std::atomic<uint64_t> frame_sequence{0};
};

namespace {

const char *preview_output_target_name(PreviewOutputTarget target)
{
	switch (target) {
	case PreviewOutputTarget::Program:
		return "program";
	case PreviewOutputTarget::Preview:
		return "preview";
	case PreviewOutputTarget::Scene:
		return "scene";
	case PreviewOutputTarget::Source:
		return "source";
	case PreviewOutputTarget::Canvas:
		return "canvas";
	}
	return "unavailable";
}

const char *preview_output_scale_name(PreviewOutputScale scale)
{
	switch (scale) {
	case PreviewOutputScale::Fit:
		return "fit";
	case PreviewOutputScale::Fill:
		return "fill";
	case PreviewOutputScale::Stretch:
		return "stretch";
	case PreviewOutputScale::OneToOne:
		return "oneToOne";
	}
	return "fit";
}

bool parse_preview_output_scale(std::string_view value, PreviewOutputScale &scale)
{
	if (value == "fit")
		scale = PreviewOutputScale::Fit;
	else if (value == "fill")
		scale = PreviewOutputScale::Fill;
	else if (value == "stretch")
		scale = PreviewOutputScale::Stretch;
	else if (value == "oneToOne" || value == "none")
		scale = PreviewOutputScale::OneToOne;
	else
		return false;
	return true;
}

bool read_preview_output_scale(obs_data_t *params, PreviewOutputScale fallback, PreviewOutputScale &scale,
				       RuntimeV2Error &error)
{
	std::string value;
	bool present = false;
	if (!phase2_read_string(params, "scale", value, present))
		return phase2_fail(error, "bad_request", "params.scale must be a semantic string");
	if (!present) {
		scale = fallback;
		return true;
	}
	if (!parse_preview_output_scale(value, scale))
		return phase2_fail(error, "bad_request", "params.scale is unsupported");
	return true;
}

bool read_preview_output_target(obs_data_t *params, PreviewOutputTarget &target, uint64_t &handle,
					RuntimeV2Error &error)
{
	ObsDataPtr object;
	bool present = false;
	if (!phase2_read_object(params, "target", object, present) || !present || !object)
		return phase2_fail(error, "bad_request", "params.target must be an object");
	std::string type;
	if (!phase2_read_string(object.get(), "type", type, present) || !present)
		return phase2_fail(error, "bad_request", "target.type is required");
	handle = 0;
	if (type == "program")
		target = PreviewOutputTarget::Program;
	else if (type == "preview")
		target = PreviewOutputTarget::Preview;
	else if (type == "scene") {
		target = PreviewOutputTarget::Scene;
		if (!phase2_read_handle(object.get(), "scene", handle))
			return phase2_fail(error, "bad_request", "target.scene must be a canonical scene handle string");
	} else if (type == "source") {
		target = PreviewOutputTarget::Source;
		if (!phase2_read_handle(object.get(), "source", handle))
			return phase2_fail(error, "bad_request", "target.source must be a canonical source handle string");
	} else if (type == "canvas") {
		target = PreviewOutputTarget::Canvas;
		if (!phase2_read_handle(object.get(), "canvas", handle))
			return phase2_fail(error, "bad_request", "target.canvas must be a canonical canvas handle string");
}
	else
		return phase2_fail(error, "unsupported_capability", "PreviewOutput target is not supported");
	return true;
}

std::shared_ptr<PreviewOutputTargetBinding> create_preview_output_binding(Engine &engine,
								PreviewOutputTarget target, uint64_t handle,
								RuntimeV2Error &error)
{
	auto binding = std::make_shared<PreviewOutputTargetBinding>();
	binding->target = target;
	binding->handle = handle;
	switch (target) {
	case PreviewOutputTarget::Program:
	case PreviewOutputTarget::Preview:
		return binding;
	case PreviewOutputTarget::Scene: {
		obs_scene_t *scene = engine.v2_scene_for_handle(handle);
		if (!scene) {
			phase2_fail(error, "not_found", "PreviewOutput scene target was not found");
			return {};
		}
		binding->source = obs_source_get_ref(obs_scene_get_source(scene));
		break;
	}
	case PreviewOutputTarget::Source:
		binding->source = obs_source_get_ref(engine.v2_source_for_handle(handle));
		if (!binding->source) {
			phase2_fail(error, "not_found", "PreviewOutput source target was not found");
			return {};
		}
		break;
	case PreviewOutputTarget::Canvas:
		binding->canvas = obs_canvas_get_ref(engine.v2_canvas_for_handle(handle));
		if (!binding->canvas || obs_canvas_removed(binding->canvas)) {
			if (binding->canvas) {
				obs_canvas_release(binding->canvas);
				binding->canvas = nullptr;
			}
			phase2_fail(error, "not_found", "PreviewOutput canvas target was not found");
			return {};
		}
		break;
	}
	if ((target == PreviewOutputTarget::Scene || target == PreviewOutputTarget::Source) && !binding->source) {
		phase2_fail(error, "not_found", "PreviewOutput target source was not available");
		return {};
	}
	return binding;
}

bool read_preview_output_dimension(obs_data_t *params, const char *name, uint32_t fallback, uint32_t &value,
					   RuntimeV2Error &error)
{
	long long integer = 0;
	bool present = false;
	if (!phase2_read_integer(params, name, integer, present))
		return phase2_fail(error, "bad_request", "PreviewOutput dimensions must be integers");
	if (!present) {
		value = fallback;
		return true;
	}
	if (integer < kMinPreviewOutputDimension || integer > kMaxPreviewOutputDimension)
		return phase2_fail(error, "bad_request", "PreviewOutput dimension is outside the supported range");
	value = static_cast<uint32_t>(integer);
	return true;
}

bool read_optional_transport_string(obs_data_t *params, const char *name, const char *expected,
					    RuntimeV2Error &error)
{
	std::string value;
	bool present = false;
	if (!phase2_read_string(params, name, value, present))
		return phase2_fail(error, "bad_request", "PreviewOutput transport options must be strings");
	if (present && value != expected)
		return phase2_fail(error, "unsupported_capability", "requested PreviewOutput transport option is unavailable");
	return true;
}

std::string format_adapter_luid(const LUID &luid)
{
	char value[32] = {};
	std::snprintf(value, sizeof(value), "%08X-%08X", static_cast<unsigned int>(luid.HighPart),
			      static_cast<unsigned int>(luid.LowPart));
	return value;
}

bool inspect_d3d11_shared_texture(gs_texture_t *texture, uint64_t &shared_handle, std::string &adapter_luid,
					 RuntimeV2Error &error)
{
	if (sizeof(uintptr_t) > sizeof(uint64_t))
		return phase2_fail(error, "unsupported_capability", "Windows HANDLE width exceeds protocol representation");
	void *object = gs_texture_get_obj(texture);
	if (!object)
		return phase2_fail(error, "obs_error", "D3D11 shared texture object is unavailable");
	auto *d3d_texture = static_cast<ID3D11Texture2D *>(object);
	Microsoft::WRL::ComPtr<IDXGIResource> resource;
	HRESULT hr = d3d_texture->QueryInterface(IID_PPV_ARGS(resource.GetAddressOf()));
	if (FAILED(hr))
		return phase2_fail(error, "obs_error", "D3D11 shared texture did not expose IDXGIResource");
	HANDLE raw_handle = nullptr;
	hr = resource->GetSharedHandle(&raw_handle);
	if (FAILED(hr) || !raw_handle)
		return phase2_fail(error, "obs_error", "D3D11 shared texture did not expose a legacy shared handle");
	const uintptr_t raw_value = reinterpret_cast<uintptr_t>(raw_handle);
	if (raw_value == 0 || raw_value == std::numeric_limits<uintptr_t>::max())
		return phase2_fail(error, "obs_error", "D3D11 shared texture returned an invalid shared handle");

	void *device_object = gs_get_device_obj();
	if (!device_object)
		return phase2_fail(error, "obs_error", "D3D11 device object is unavailable");
	auto *d3d_device = static_cast<ID3D11Device *>(device_object);
	Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
	hr = d3d_device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()));
	if (FAILED(hr))
		return phase2_fail(error, "obs_error", "D3D11 device did not expose IDXGIDevice");
	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	hr = dxgi_device->GetAdapter(adapter.GetAddressOf());
	if (FAILED(hr))
		return phase2_fail(error, "obs_error", "D3D11 device adapter was unavailable");
	DXGI_ADAPTER_DESC description = {};
	hr = adapter->GetDesc(&description);
	if (FAILED(hr))
		return phase2_fail(error, "obs_error", "D3D11 adapter identity was unavailable");

	shared_handle = static_cast<uint64_t>(raw_value);
	adapter_luid = format_adapter_luid(description.AdapterLuid);
	return true;
}

std::shared_ptr<PreviewTextureV2Resource> create_preview_resource(uint32_t width, uint32_t height, uint64_t generation,
								 RuntimeV2Error &error)
{
	if (!obs_initialized()) {
		phase2_fail(error, "invalid_state", "libobs is not initialized");
		return {};
	}
	std::shared_ptr<PreviewTextureV2Resource> resource;
	try {
		resource = std::make_shared<PreviewTextureV2Resource>();
	} catch (...) {
		phase2_fail(error, "internal_error", "could not allocate PreviewOutput resource state");
		return {};
	}
	resource->width = width;
	resource->height = height;
	resource->generation = generation;

	obs_enter_graphics();
	bool success = false;
	if (gs_get_device_type() != GS_DEVICE_DIRECT3D_11) {
		phase2_fail(error, "unsupported_capability", "PreviewOutput requires the D3D11 graphics backend");
	} else if (!gs_shared_texture_available()) {
		phase2_fail(error, "unsupported_capability", "D3D11 shared textures are unavailable");
	} else {
		resource->texture = gs_texture_create(width, height, GS_BGRA_UNORM, 1, nullptr,
								 GS_RENDER_TARGET | GS_SHARED_KM_TEX);
		if (!resource->texture) {
			phase2_fail(error, "obs_error", "D3D11 keyed shared texture creation failed");
		} else {
			resource->staging = gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
			if (!resource->staging) {
				phase2_fail(error, "obs_error", "PreviewOutput staging render creation failed");
			} else if (!inspect_d3d11_shared_texture(resource->texture, resource->shared_handle,
										 resource->adapter_luid, error)) {
			} else {
				// gs_texture_create(GS_SHARED_KM_TEX) leaves the creator owning key 0.
				// Keep that ownership until getSharedTexture attaches a consumer.
				resource->producer_acquired = true;
				success = true;
			}
		}
	}
	if (!success) {
		if (resource->staging) {
			gs_texrender_destroy(resource->staging);
			resource->staging = nullptr;
		}
		if (resource->texture) {
			if (resource->producer_acquired)
				gs_texture_release_sync(resource->texture, 0);
			gs_texture_destroy(resource->texture);
			resource->texture = nullptr;
		}
	}
	obs_leave_graphics();
	return success ? resource : std::shared_ptr<PreviewTextureV2Resource>{};
}

void destroy_preview_resource(const std::shared_ptr<PreviewTextureV2Resource> &resource) noexcept
{
	if (!resource || (!resource->texture && !resource->staging) || !obs_initialized())
		return;
	obs_enter_graphics();
	if (resource->texture && resource->producer_acquired) {
		if (gs_texture_release_sync(resource->texture, 0) != 0)
			blog(LOG_WARNING, "obs-engine: failed to release PreviewOutput producer keyed mutex during teardown");
		resource->producer_acquired = false;
	}
	if (resource->staging) {
		gs_texrender_destroy(resource->staging);
		resource->staging = nullptr;
	}
	if (resource->texture) {
		gs_texture_destroy(resource->texture);
		resource->texture = nullptr;
	}
	obs_leave_graphics();
}

void set_nullable_string_handle(obs_data_t *data, const char *name, uint64_t value)
{
	if (value != 0)
		phase2_set_handle(data, name, value);
	else
		obs_data_set_obj(data, name, nullptr);
}

ObsDataPtr make_preview_output_target(const PreviewOutputTargetBinding &target)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "type", preview_output_target_name(target.target));
	if (target.handle != 0) {
		const char *field = target.target == PreviewOutputTarget::Scene
				    ? "scene"
				    : target.target == PreviewOutputTarget::Source ? "source" : "canvas";
		phase2_set_handle(data.get(), field, target.handle);
	}
	return data;
}

std::shared_ptr<PreviewOutputTargetBinding> make_unavailable_target(PreviewOutputTarget target, uint64_t handle)
{
	auto binding = std::make_shared<PreviewOutputTargetBinding>();
	binding->target = target;
	binding->handle = handle;
	binding->available = false;
	return binding;
}

ObsDataPtr make_preview_output_info(const PreviewOutputV2State &state)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "previewOutput", state.handle);
	if (state.target) {
		obs_data_set_obj(data.get(), "target", make_preview_output_target(*state.target).get());
		obs_data_set_bool(data.get(), "targetAvailable", state.target->available);
	} else {
		ObsDataPtr target(obs_data_create());
		obs_data_set_string(target.get(), "type", "unavailable");
		obs_data_set_obj(data.get(), "target", target.get());
		obs_data_set_bool(data.get(), "targetAvailable", false);
	}
	obs_data_set_bool(data.get(), "enabled", state.enabled);
	obs_data_set_string(data.get(), "scale", preview_output_scale_name(state.scale));
	const auto resource = state.resource;
	if (resource) {
		phase2_set_handle(data.get(), "resourceGeneration", resource->generation);
		obs_data_set_int(data.get(), "width", resource->width);
		obs_data_set_int(data.get(), "height", resource->height);
		obs_data_set_string(data.get(), "pixelFormat", "bgra8");
		obs_data_set_string(data.get(), "colorSpace", "srgb");
		obs_data_set_string(data.get(), "range", "full");
		obs_data_set_string(data.get(), "adapterLuid", resource->adapter_luid.c_str());
		obs_data_set_bool(data.get(), "hasSharedTexture", resource->texture != nullptr);
	} else {
		set_nullable_string_handle(data.get(), "resourceGeneration", 0);
		obs_data_set_int(data.get(), "width", 0);
		obs_data_set_int(data.get(), "height", 0);
		obs_data_set_string(data.get(), "pixelFormat", "bgra8");
		obs_data_set_string(data.get(), "colorSpace", "srgb");
		obs_data_set_string(data.get(), "range", "full");
		obs_data_set_string(data.get(), "adapterLuid", "");
		obs_data_set_bool(data.get(), "hasSharedTexture", false);
	}
	obs_data_set_int(data.get(), "frameSequence",
			static_cast<long long>(std::min<uint64_t>(state.frame_sequence.load(std::memory_order_relaxed),
									 std::numeric_limits<long long>::max())));
	obs_data_set_bool(data.get(), "consumerAttached", state.consumer_attached);
	return data;
}

ObsDataPtr make_shared_texture_descriptor(const PreviewOutputV2State &state)
{
	ObsDataPtr data = make_preview_output_info(state);
	if (!state.resource || !state.resource->texture)
		return data;

	ObsDataPtr shared(obs_data_create());
	obs_data_set_string(shared.get(), "type", "d3d11LegacySharedHandle");
	phase2_set_handle(shared.get(), "handle", state.resource->shared_handle);
	obs_data_set_string(shared.get(), "ownership", "engineOwnedUntilResourceChange");
	obs_data_set_bool(shared.get(), "controllerMustNotClose", true);
	obs_data_set_string(shared.get(), "openApi", "ID3D11Device::OpenSharedResource");
	obs_data_set_obj(data.get(), "sharedTexture", shared.get());

	ObsDataPtr synchronization(obs_data_create());
	obs_data_set_string(synchronization.get(), "type", "keyedMutex");
	obs_data_set_int(synchronization.get(), "producerAcquireKey", 0);
	obs_data_set_int(synchronization.get(), "producerReleaseKey", 1);
	obs_data_set_int(synchronization.get(), "consumerAcquireKey", 1);
	obs_data_set_int(synchronization.get(), "consumerReleaseKey", 0);
	obs_data_set_int(synchronization.get(), "acquireTimeoutMs", kPreviewAcquireTimeoutMs);
	obs_data_set_obj(data.get(), "synchronization", synchronization.get());
	return data;
}

struct PreviewRenderJob {
	std::shared_ptr<PreviewOutputV2State> state;
	std::shared_ptr<PreviewTextureV2Resource> resource;
	std::shared_ptr<PreviewOutputTargetBinding> target;
	PreviewOutputScale scale = PreviewOutputScale::Fit;
};

bool render_preview_resource(PreviewTextureV2Resource &resource, obs_source_t *source, obs_canvas_t *canvas,
				     PreviewOutputScale scale, uint32_t fallback_width, uint32_t fallback_height)
{
	if (!resource.texture)
		return false;
	if (gs_texture_acquire_sync(resource.texture, 0, kPreviewAcquireTimeoutMs) != 0)
		return false;
	resource.producer_acquired = true;

	gs_texture_t *previous_target = gs_get_render_target();
	gs_zstencil_t *previous_zstencil = gs_get_zstencil_target();
	const enum gs_color_space previous_space = gs_get_color_space();
	gs_texture_t *stage = nullptr;
	uint32_t source_width = source ? obs_source_get_width(source) : 0;
	uint32_t source_height = source ? obs_source_get_height(source) : 0;
	if (!source && canvas) {
		obs_video_info video = {};
		if (obs_canvas_get_video_info(canvas, &video)) {
			source_width = video.base_width;
			source_height = video.base_height;
		}
	}
	if (source_width == 0)
		source_width = fallback_width;
	if (source_height == 0)
		source_height = fallback_height;

	if ((source || canvas) && source_width != 0 && source_height != 0 && resource.staging) {
		gs_texrender_reset(resource.staging);
		if (gs_texrender_begin_with_color_space(resource.staging, source_width, source_height, GS_CS_SRGB)) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 1.0f, 0);
			gs_ortho(0.0f, static_cast<float>(source_width), 0.0f, static_cast<float>(source_height), -100.0f,
				 100.0f);
			gs_matrix_identity();
			if (source)
				obs_source_video_render(source);
			else
				obs_canvas_render(canvas);
			gs_texrender_end(resource.staging);
			stage = gs_texrender_get_texture(resource.staging);
		}
	}

	gs_set_render_target_with_color_space(resource.texture, nullptr, GS_CS_SRGB);
	gs_viewport_push();
	gs_projection_push();
	gs_matrix_push();
	gs_blend_state_push();
	gs_set_viewport(0, 0, resource.width, resource.height);
	gs_ortho(0.0f, static_cast<float>(resource.width), 0.0f, static_cast<float>(resource.height), -100.0f,
		 100.0f);
	gs_matrix_identity();
	struct vec4 output_clear;
	vec4_zero(&output_clear);
	gs_clear(GS_CLEAR_COLOR, &output_clear, 1.0f, 0);
	if (stage && source_width != 0 && source_height != 0) {
		gs_reset_blend_state();
		double scale_factor = 1.0;
		if (scale == PreviewOutputScale::Fit)
			scale_factor = std::min(static_cast<double>(resource.width) / source_width,
						 static_cast<double>(resource.height) / source_height);
		else if (scale == PreviewOutputScale::Fill)
			scale_factor = std::max(static_cast<double>(resource.width) / source_width,
						 static_cast<double>(resource.height) / source_height);
		else if (scale == PreviewOutputScale::Stretch) {
			scale_factor = 1.0;
		}
		const uint32_t draw_width = scale == PreviewOutputScale::Stretch
					    ? resource.width
					    : std::max<uint32_t>(1, static_cast<uint32_t>(source_width * scale_factor + 0.5));
		const uint32_t draw_height = scale == PreviewOutputScale::Stretch
					     ? resource.height
					     : std::max<uint32_t>(1, static_cast<uint32_t>(source_height * scale_factor + 0.5));
		gs_matrix_translate3f((static_cast<float>(resource.width) - draw_width) * 0.5f,
				      (static_cast<float>(resource.height) - draw_height) * 0.5f, 0.0f);
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		if (effect) {
			gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), stage);
			gs_enable_framebuffer_srgb(true);
			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(stage, 0, draw_width, draw_height);
			gs_enable_framebuffer_srgb(false);
		}
	}
	gs_blend_state_pop();
	gs_matrix_pop();
	gs_projection_pop();
	gs_viewport_pop();
	gs_set_render_target_with_color_space(previous_target, previous_zstencil, previous_space);

	const int release_result = gs_texture_release_sync(resource.texture, 1);
	if (release_result == 0)
		resource.producer_acquired = false;
	else
		blog(LOG_WARNING, "obs-engine: failed to release PreviewOutput keyed mutex after rendering");
	return release_result == 0;
}

} // namespace

bool Engine::v2_preview_output_capable() const
{
	return preview_output_capable_;
}

bool Engine::v2_get_preview_output_entry(obs_data_t *params, uint64_t &handle,
						std::shared_ptr<PreviewOutputV2State> &entry, RuntimeV2Error &error) const
{
	if (!phase2_read_handle(params, "previewOutput", handle))
		return phase2_fail(error, "bad_request",
				   "params.previewOutput must be a canonical preview output handle string");
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	const auto it = preview_outputs_.find(handle);
	if (it == preview_outputs_.end() || !it->second)
		return phase2_fail(error, "not_found", "PreviewOutput handle was not found");
	entry = it->second;
	return true;
}

bool Engine::v2_preview_output_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	if (!preview_output_capable_)
		return phase2_fail(error, "unsupported_capability", "D3D11 PreviewOutput is unavailable");
	PreviewOutputTarget target = PreviewOutputTarget::Preview;
	uint64_t target_handle = 0;
	if (!read_preview_output_target(params, target, target_handle, error))
		return false;
	auto binding = create_preview_output_binding(*this, target, target_handle, error);
	if (!binding)
		return false;

	obs_canvas_t *dimension_canvas = nullptr;
	if (target == PreviewOutputTarget::Program) {
		const auto main_it = canvases_.find(main_canvas_);
		if (main_it != canvases_.end())
			dimension_canvas = main_it->second.canvas;
	} else if (target == PreviewOutputTarget::Canvas) {
		dimension_canvas = binding->canvas;
	} else if (target == PreviewOutputTarget::Scene) {
		const auto scene_canvas = scene_canvases_.find(target_handle);
		if (scene_canvas != scene_canvases_.end())
			dimension_canvas = v2_canvas_for_handle(scene_canvas->second);
	} else if (target == PreviewOutputTarget::Preview) {
		uint64_t preview_scene = 0;
		{
			std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
			preview_scene = preview_scene_;
		}
		if (preview_scene != 0) {
			const auto scene_canvas = scene_canvases_.find(preview_scene);
			if (scene_canvas != scene_canvases_.end()) {
				const auto canvas_it = canvases_.find(scene_canvas->second);
				if (canvas_it != canvases_.end())
					dimension_canvas = canvas_it->second.canvas;
			}
		}
		if (!dimension_canvas) {
			const auto main_it = canvases_.find(main_canvas_);
			if (main_it != canvases_.end())
				dimension_canvas = main_it->second.canvas;
		}
	}
	obs_video_info video = {};
	if (dimension_canvas)
		obs_canvas_get_video_info(dimension_canvas, &video);
	if (video.output_width == 0 || video.output_height == 0) {
		if (binding->source) {
			video.output_width = obs_source_get_width(binding->source);
			video.output_height = obs_source_get_height(binding->source);
		}
	}
	if (video.output_width == 0 || video.output_height == 0)
		return phase2_fail(error, "not_available", "PreviewOutput target video settings are unavailable");
	uint32_t width = 0;
	uint32_t height = 0;
	if (!read_preview_output_dimension(params, "width", video.output_width, width, error) ||
	    !read_preview_output_dimension(params, "height", video.output_height, height, error))
		return false;
	if (!read_optional_transport_string(params, "pixelFormat", "bgra8", error) ||
	    !read_optional_transport_string(params, "colorSpace", "srgb", error) ||
	    !read_optional_transport_string(params, "range", "full", error))
		return false;
	PreviewOutputScale scale = PreviewOutputScale::Fit;
	if (!read_preview_output_scale(params, PreviewOutputScale::Fit, scale, error))
		return false;
	bool enabled = true;
	bool enabled_present = false;
	if (!phase2_read_bool(params, "enabled", enabled, enabled_present))
		return phase2_fail(error, "bad_request", "params.enabled must be boolean");

	const uint64_t handle = allocate_handle();
	RuntimeV2Error resource_error;
	std::shared_ptr<PreviewTextureV2Resource> resource = create_preview_resource(width, height, 1, resource_error);
	if (!resource) {
		error = std::move(resource_error);
		return false;
	}
	std::shared_ptr<PreviewOutputV2State> state;
	try {
		state = std::make_shared<PreviewOutputV2State>();
		state->handle = handle;
		state->target = std::move(binding);
		state->scale = scale;
		state->enabled = enabled;
		state->resource = resource;
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		if (!preview_outputs_.emplace(handle, state).second)
			throw std::runtime_error("PreviewOutput handle collision");
	} catch (...) {
		destroy_preview_resource(resource);
		return phase2_fail(error, "internal_error", "could not register PreviewOutput handle");
	}

	result.data = make_shared_texture_descriptor(*state);
	phase2_append_event(result, "previewOutput.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_preview_output_destroy(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	std::shared_ptr<PreviewTextureV2Resource> resource;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		const auto it = preview_outputs_.find(handle);
		if (it == preview_outputs_.end())
			return phase2_fail(error, "not_found", "PreviewOutput handle was not found");
		resource = it->second->resource;
		preview_outputs_.erase(it);
	}
	destroy_preview_resource(resource);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "previewOutput", handle);
	result.data = std::move(data);
	phase2_append_event(result, "previewOutput.destroyed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_preview_output_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	bool enabled = false;
	bool present = false;
	if (!phase2_read_bool(params, "enabled", enabled, present) || !present)
		return phase2_fail(error, "bad_request", "params.enabled must be boolean");
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		if (state->enabled == enabled) {
			result.data = make_preview_output_info(*state);
			return true;
		}
		state->enabled = enabled;
		result.data = make_preview_output_info(*state);
	}
	phase2_append_event(result, "previewOutput.enabledChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_preview_output_set_target(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	PreviewOutputTarget target = PreviewOutputTarget::Preview;
	uint64_t target_handle = 0;
	if (!read_preview_output_target(params, target, target_handle, error))
		return false;
	std::shared_ptr<PreviewOutputTargetBinding> binding =
		create_preview_output_binding(*this, target, target_handle, error);
	if (!binding)
		return false;
	PreviewOutputScale scale = PreviewOutputScale::Fit;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		scale = state->scale;
	}
	if (!read_preview_output_scale(params, scale, scale, error))
		return false;

	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		const auto &previous = state->target;
		changed = !previous || previous->target != target || previous->handle != target_handle ||
			  !previous->available || state->scale != scale;
		if (!changed) {
			result.data = make_preview_output_info(*state);
			return true;
		}
		state->target = std::move(binding);
		state->scale = scale;
		result.data = make_preview_output_info(*state);
	}
	phase2_append_event(result, "previewOutput.targetChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_preview_output_resize(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	long long integer = 0;
	bool present = false;
	if (!phase2_read_integer(params, "width", integer, present) || !present ||
	    integer < kMinPreviewOutputDimension || integer > kMaxPreviewOutputDimension)
		return phase2_fail(error, "bad_request", "params.width must be within the supported PreviewOutput range");
	const uint32_t width = static_cast<uint32_t>(integer);
	if (!phase2_read_integer(params, "height", integer, present) || !present ||
	    integer < kMinPreviewOutputDimension || integer > kMaxPreviewOutputDimension)
		return phase2_fail(error, "bad_request", "params.height must be within the supported PreviewOutput range");
	const uint32_t height = static_cast<uint32_t>(integer);

	std::shared_ptr<PreviewTextureV2Resource> old_resource;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		old_resource = state->resource;
	}
	if (!old_resource)
		return phase2_fail(error, "not_available", "PreviewOutput has no resource to resize");
	if (old_resource->width == width && old_resource->height == height) {
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		result.data = make_preview_output_info(*state);
		return true;
	}
	if (old_resource->generation == std::numeric_limits<uint64_t>::max())
		return phase2_fail(error, "internal_error", "PreviewOutput resource generation is exhausted");

	RuntimeV2Error resource_error;
	std::shared_ptr<PreviewTextureV2Resource> replacement =
		create_preview_resource(width, height, old_resource->generation + 1, resource_error);
	if (!replacement) {
		error = std::move(resource_error);
		return false;
	}
	bool swapped = false;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		if (state->resource == old_resource) {
			state->resource = replacement;
			state->consumer_attached = false;
			result.data = make_shared_texture_descriptor(*state);
			swapped = true;
		}
	}
	if (!swapped) {
		destroy_preview_resource(replacement);
		return phase2_fail(error, "busy", "PreviewOutput changed while resize was being prepared");
	}
	destroy_preview_resource(old_resource);
	phase2_append_event(result, "previewOutput.resourceChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_preview_output_get_info(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	result.data = make_preview_output_info(*state);
	return true;
}

bool Engine::v2_preview_output_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return v2_preview_output_get_info(params, result, error);
}

bool Engine::v2_preview_output_get_shared_texture(obs_data_t *params, RuntimeV2Result &result,
							  RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	if (!state->resource || !state->resource->texture)
		return phase2_fail(error, "not_available", "PreviewOutput has no shared texture resource");
	state->consumer_attached = true;
	result.data = make_shared_texture_descriptor(*state);
	return true;
}

bool Engine::v2_preview_output_release_shared_texture(obs_data_t *params, RuntimeV2Result &result,
							    RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	std::shared_ptr<PreviewOutputV2State> state;
	if (!v2_get_preview_output_entry(params, handle, state, error))
		return false;
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	state->consumer_attached = false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "previewOutput", handle);
	obs_data_set_bool(data.get(), "released", true);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_preview_output_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::vector<std::shared_ptr<PreviewOutputV2State>> entries;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		entries.reserve(preview_outputs_.size());
		for (const auto &[_, state] : preview_outputs_)
			entries.push_back(state);
	}
	std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
		return left->handle < right->handle;
	});
	ObsArrayPtr values(obs_data_array_create());
	for (const auto &state : entries) {
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		obs_data_array_push_back(values.get(), make_preview_output_info(*state).get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "previewOutputs", values.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(entries.size()));
	result.data = std::move(data);
	return true;
}

void Engine::v2_preview_output_invalidate_scene(uint64_t scene_handle, RuntimeV2Result &result)
{
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	for (const auto &[_, state] : preview_outputs_) {
		if (!state || !state->target || !state->target->available || state->target->target != PreviewOutputTarget::Scene ||
		    state->target->handle != scene_handle)
			continue;
		state->target = make_unavailable_target(PreviewOutputTarget::Scene, scene_handle);
		result.events.push_back(RuntimeV2Event{"previewOutput.targetChanged", make_preview_output_info(*state)});
	}
}

void Engine::v2_preview_output_invalidate_source(uint64_t source_handle, RuntimeV2Result &result)
{
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	for (const auto &[_, state] : preview_outputs_) {
		if (!state || !state->target || !state->target->available || state->target->target != PreviewOutputTarget::Source ||
		    state->target->handle != source_handle)
			continue;
		state->target = make_unavailable_target(PreviewOutputTarget::Source, source_handle);
		result.events.push_back(RuntimeV2Event{"previewOutput.targetChanged", make_preview_output_info(*state)});
	}
}

void Engine::v2_preview_output_invalidate_canvas(uint64_t canvas_handle, RuntimeV2Result &result)
{
	std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
	for (const auto &[_, state] : preview_outputs_) {
		if (!state || !state->target || !state->target->available || state->target->target != PreviewOutputTarget::Canvas ||
		    state->target->handle != canvas_handle)
			continue;
		state->target = make_unavailable_target(PreviewOutputTarget::Canvas, canvas_handle);
		result.events.push_back(RuntimeV2Event{"previewOutput.targetChanged", make_preview_output_info(*state)});
	}
}

void Engine::v2_preview_output_invalidate_canvas_video(uint64_t canvas_handle, RuntimeV2Result &result)
{
	uint64_t preview_scene = 0;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_scene = preview_scene_;
	}
	std::vector<std::shared_ptr<PreviewOutputV2State>> affected;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		for (const auto &[_, state] : preview_outputs_) {
			if (!state || !state->resource || !state->target || !state->target->available)
				continue;
			bool affected_canvas = state->target->target == PreviewOutputTarget::Canvas &&
					       state->target->handle == canvas_handle;
			if (!affected_canvas && state->target->target == PreviewOutputTarget::Scene) {
				const auto scene_canvas = scene_canvases_.find(state->target->handle);
				affected_canvas = scene_canvas != scene_canvases_.end() && scene_canvas->second == canvas_handle;
			}
			if (!affected_canvas && state->target->target == PreviewOutputTarget::Preview) {
				const auto scene_canvas = scene_canvases_.find(preview_scene);
				affected_canvas = scene_canvas != scene_canvases_.end() && scene_canvas->second == canvas_handle;
			}
			if (affected_canvas)
				affected.push_back(state);
		}
	}

	for (const auto &state : affected) {
		std::shared_ptr<PreviewTextureV2Resource> old_resource;
		{
			std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
			old_resource = state->resource;
		}
		if (!old_resource || old_resource->generation == std::numeric_limits<uint64_t>::max())
			continue;
		RuntimeV2Error replacement_error;
		std::shared_ptr<PreviewTextureV2Resource> replacement =
			create_preview_resource(old_resource->width, old_resource->height, old_resource->generation + 1,
						 replacement_error);
		if (!replacement) {
			blog(LOG_WARNING, "obs-engine: PreviewOutput resource invalidation failed after Canvas video reset: %s",
			     replacement_error.message.c_str());
			continue;
		}
		bool swapped = false;
		{
			std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
			if (state->resource == old_resource) {
				state->resource = replacement;
				state->consumer_attached = false;
				RuntimeV2Event event{"previewOutput.resourceChanged", make_shared_texture_descriptor(*state)};
				result.events.push_back(std::move(event));
				swapped = true;
			}
		}
		if (swapped)
			destroy_preview_resource(old_resource);
		else
			destroy_preview_resource(replacement);
	}
}

void Engine::v2_preview_output_render_callback(void *param, uint32_t base_width, uint32_t base_height)
{
	if (param)
		static_cast<Engine *>(param)->v2_render_preview_outputs(base_width, base_height);
}

void Engine::v2_render_preview_outputs(uint32_t base_width, uint32_t base_height)
{
	const uint64_t frame = static_cast<uint64_t>(obs_get_total_frames());
	if (preview_render_frame_ == frame)
		return;
	preview_render_frame_ = frame;

	std::vector<PreviewRenderJob> jobs;
	bool need_program = false;
	bool need_preview = false;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		for (const auto &[_, state] : preview_outputs_) {
			if (!state || !state->enabled || !state->consumer_attached || !state->resource ||
			    !state->resource->texture)
				continue;
			jobs.push_back(PreviewRenderJob{state, state->resource, state->target, state->scale});
			need_program = need_program || (state->target && state->target->target == PreviewOutputTarget::Program);
			need_preview = need_preview || (state->target && state->target->target == PreviewOutputTarget::Preview);
		}
	}
	if (jobs.empty())
		return;

	obs_source_t *program_source = need_program ? obs_get_output_source(0) : nullptr;
	obs_source_t *preview_source = nullptr;
	if (need_preview) {
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_source = obs_source_get_ref(preview_source_);
	}
	for (const PreviewRenderJob &job : jobs) {
		obs_source_t *source = nullptr;
		obs_canvas_t *canvas = nullptr;
		if (job.target && job.target->available) {
			switch (job.target->target) {
			case PreviewOutputTarget::Program:
				source = program_source;
				break;
			case PreviewOutputTarget::Preview:
				source = preview_source;
				break;
			case PreviewOutputTarget::Scene:
			case PreviewOutputTarget::Source:
				source = job.target->source;
				break;
			case PreviewOutputTarget::Canvas:
				canvas = job.target->canvas;
				break;
			}
		}
		if (render_preview_resource(*job.resource, source, canvas, job.scale, base_width, base_height))
			job.state->frame_sequence.fetch_add(1, std::memory_order_relaxed);
	}
	if (program_source)
		obs_source_release(program_source);
	if (preview_source)
		obs_source_release(preview_source);
}

void Engine::v2_shutdown_preview_outputs() noexcept
{
	try {
		if (preview_render_callback_registered_) {
			obs_remove_main_render_callback(&Engine::v2_preview_output_render_callback, this);
			preview_render_callback_registered_ = false;
		}
		std::vector<std::shared_ptr<PreviewTextureV2Resource>> resources;
		{
			std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
			resources.reserve(preview_outputs_.size());
			for (auto &[_, state] : preview_outputs_) {
				if (state)
					resources.push_back(state->resource);
			}
			preview_outputs_.clear();
		}
		for (const auto &resource : resources)
			destroy_preview_resource(resource);
		preview_render_frame_ = UINT64_MAX;
	} catch (...) {
		// Shutdown remains best-effort and must not throw through Engine teardown.
	}
}

} // namespace obs_engine
