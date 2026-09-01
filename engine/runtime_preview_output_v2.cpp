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

bool parse_preview_output_target_type(std::string_view type, PreviewOutputTarget &target, RuntimeV2Error &error)
{
	if (type == "program")
		target = PreviewOutputTarget::Program;
	else if (type == "preview")
		target = PreviewOutputTarget::Preview;
	else if (type == "scene")
		target = PreviewOutputTarget::Scene;
	else if (type == "source")
		target = PreviewOutputTarget::Source;
	else if (type == "canvas")
		target = PreviewOutputTarget::Canvas;
	else
		return phase2_fail(error, "unsupported_capability", "PreviewOutput target is not supported");
	return true;
}

bool read_preview_output_target_handle(obs_data_t *object, PreviewOutputTarget target, uint64_t &handle,
					      RuntimeV2Error &error)
{
	const char *field = target == PreviewOutputTarget::Scene
				    ? "scene"
				    : target == PreviewOutputTarget::Source ? "source" : target == PreviewOutputTarget::Canvas ? "canvas" : nullptr;
	if (!field)
		return true;
	if (!phase2_read_handle(object, field, handle))
		return phase2_fail(error, "bad_request", "PreviewOutput target handle is not canonical");
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
	return parse_preview_output_target_type(type, target, error) &&
	       read_preview_output_target_handle(object.get(), target, handle, error);
}

bool bind_scene_target(Engine &engine, uint64_t handle, PreviewOutputTargetBinding &binding, RuntimeV2Error &error)
{
	obs_scene_t *scene = engine.v2_scene_for_handle(handle);
	if (!scene)
		return phase2_fail(error, "not_found", "PreviewOutput scene target was not found");
	binding.source = obs_source_get_ref(obs_scene_get_source(scene));
	return binding.source || phase2_fail(error, "not_found", "PreviewOutput target source was not available");
}

bool bind_source_target(Engine &engine, uint64_t handle, PreviewOutputTargetBinding &binding, RuntimeV2Error &error)
{
	binding.source = obs_source_get_ref(engine.v2_source_for_handle(handle));
	return binding.source || phase2_fail(error, "not_found", "PreviewOutput source target was not found");
}

bool bind_canvas_target(Engine &engine, uint64_t handle, PreviewOutputTargetBinding &binding, RuntimeV2Error &error)
{
	binding.canvas = obs_canvas_get_ref(engine.v2_canvas_for_handle(handle));
	if (binding.canvas && !obs_canvas_removed(binding.canvas))
		return true;
	if (binding.canvas) {
		obs_canvas_release(binding.canvas);
		binding.canvas = nullptr;
	}
	return phase2_fail(error, "not_found", "PreviewOutput canvas target was not found");
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
		break;
	case PreviewOutputTarget::Scene:
		if (!bind_scene_target(engine, handle, *binding, error))
			return {};
		break;
	case PreviewOutputTarget::Source:
		if (!bind_source_target(engine, handle, *binding, error))
			return {};
		break;
	case PreviewOutputTarget::Canvas:
		if (!bind_canvas_target(engine, handle, *binding, error))
			return {};
		break;
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

bool read_preview_output_resize_dimensions(obs_data_t *params, uint32_t &width, uint32_t &height,
						 RuntimeV2Error &error)
{
	if (!read_preview_output_dimension(params, "width", 0, width, error) ||
	    !read_preview_output_dimension(params, "height", 0, height, error))
		return false;
	if (width < kMinPreviewOutputDimension || height < kMinPreviewOutputDimension)
		return phase2_fail(error, "bad_request", "PreviewOutput resize dimensions are required");
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

bool inspect_shared_texture_handle(gs_texture_t *texture, uint64_t &shared_handle, RuntimeV2Error &error)
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

	shared_handle = static_cast<uint64_t>(raw_value);
	return true;
}

bool inspect_d3d11_adapter(std::string &adapter_luid, RuntimeV2Error &error)
{
	void *device_object = gs_get_device_obj();
	if (!device_object)
		return phase2_fail(error, "obs_error", "D3D11 device object is unavailable");
	auto *d3d_device = static_cast<ID3D11Device *>(device_object);
	Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
	HRESULT hr = S_OK;
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

	adapter_luid = format_adapter_luid(description.AdapterLuid);
	return true;
}

bool inspect_d3d11_shared_texture(gs_texture_t *texture, uint64_t &shared_handle, std::string &adapter_luid,
					 RuntimeV2Error &error)
{
	return inspect_shared_texture_handle(texture, shared_handle, error) && inspect_d3d11_adapter(adapter_luid, error);
}

std::shared_ptr<PreviewTextureV2Resource> allocate_preview_resource_state(uint32_t width, uint32_t height,
									   uint64_t generation, RuntimeV2Error &error)
{
	if (!obs_initialized())
		return phase2_fail(error, "invalid_state", "libobs is not initialized"),
		       std::shared_ptr<PreviewTextureV2Resource>{};
	try {
		auto resource = std::make_shared<PreviewTextureV2Resource>();
		resource->width = width;
		resource->height = height;
		resource->generation = generation;
		return resource;
	} catch (...) {
		phase2_fail(error, "internal_error", "could not allocate PreviewOutput resource state");
		return {};
	}
}

void destroy_preview_resource_objects(PreviewTextureV2Resource &resource)
{
	if (resource.texture) {
		if (resource.producer_acquired)
			gs_texture_release_sync(resource.texture, 0);
		resource.producer_acquired = false;
	}
	if (resource.staging) {
		gs_texrender_destroy(resource.staging);
		resource.staging = nullptr;
	}
	if (resource.texture) {
		gs_texture_destroy(resource.texture);
		resource.texture = nullptr;
	}
}

bool initialize_preview_resource_graphics(PreviewTextureV2Resource &resource, RuntimeV2Error &error)
{
	if (gs_get_device_type() != GS_DEVICE_DIRECT3D_11)
		return phase2_fail(error, "unsupported_capability", "PreviewOutput requires the D3D11 graphics backend");
	if (!gs_shared_texture_available())
		return phase2_fail(error, "unsupported_capability", "D3D11 shared textures are unavailable");
	resource.texture = gs_texture_create(resource.width, resource.height, GS_BGRA_UNORM, 1, nullptr,
						    GS_RENDER_TARGET | GS_SHARED_KM_TEX);
	if (!resource.texture)
		return phase2_fail(error, "obs_error", "D3D11 keyed shared texture creation failed");
	resource.staging = gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	if (!resource.staging)
		return phase2_fail(error, "obs_error", "PreviewOutput staging render creation failed");
	if (!inspect_d3d11_shared_texture(resource.texture, resource.shared_handle, resource.adapter_luid, error))
		return false;
	resource.producer_acquired = true;
	return true;
}

std::shared_ptr<PreviewTextureV2Resource> create_preview_resource(uint32_t width, uint32_t height, uint64_t generation,
								 RuntimeV2Error &error)
{
	std::shared_ptr<PreviewTextureV2Resource> resource = allocate_preview_resource_state(width, height, generation, error);
	if (!resource)
		return {};

	obs_enter_graphics();
	const bool success = initialize_preview_resource_graphics(*resource, error);
	if (!success)
		destroy_preview_resource_objects(*resource);
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
	destroy_preview_resource_objects(*resource);
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

struct PreviewRenderBatch {
	std::vector<PreviewRenderJob> jobs;
	bool need_program = false;
	bool need_preview = false;
};

void append_preview_render_job(const std::shared_ptr<PreviewOutputV2State> &state, PreviewRenderBatch &batch)
{
	batch.jobs.push_back(PreviewRenderJob{state, state->resource, state->target, state->scale});
	batch.need_program = batch.need_program ||
			     (state->target && state->target->target == PreviewOutputTarget::Program);
	batch.need_preview = batch.need_preview ||
			    (state->target && state->target->target == PreviewOutputTarget::Preview);
}

PreviewRenderBatch collect_preview_render_batch(
		const std::unordered_map<uint64_t, std::shared_ptr<PreviewOutputV2State>> &outputs, std::mutex &mutex)
{
	PreviewRenderBatch batch;
	std::lock_guard<std::mutex> lock(mutex);
	for (const auto &[_, state] : outputs) {
		if (!state || !state->enabled || !state->consumer_attached || !state->resource || !state->resource->texture)
			continue;
		append_preview_render_job(state, batch);
	}
	return batch;
}

void resolve_preview_render_target(const PreviewRenderJob &job, obs_source_t *program_source,
					   obs_source_t *preview_source, obs_source_t *&source, obs_canvas_t *&canvas)
{
	if (!job.target || !job.target->available)
		return;
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

void preview_source_dimensions(obs_source_t *source, obs_canvas_t *canvas, uint32_t fallback_width,
				       uint32_t fallback_height, uint32_t &width, uint32_t &height)
{
	width = source ? obs_source_get_width(source) : 0;
	height = source ? obs_source_get_height(source) : 0;
	if (!source && canvas) {
		obs_video_info video = {};
		if (obs_canvas_get_video_info(canvas, &video)) {
			width = video.base_width;
			height = video.base_height;
		}
	}
	if (width == 0)
		width = fallback_width;
	if (height == 0)
		height = fallback_height;
}

gs_texture_t *render_preview_stage(PreviewTextureV2Resource &resource, obs_source_t *source, obs_canvas_t *canvas,
					   uint32_t width, uint32_t height)
{
	if (!(source || canvas) || width == 0 || height == 0 || !resource.staging)
		return nullptr;
	gs_texrender_reset(resource.staging);
	if (!gs_texrender_begin_with_color_space(resource.staging, width, height, GS_CS_SRGB))
		return nullptr;
	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 1.0f, 0);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
	gs_matrix_identity();
	if (source)
		obs_source_video_render(source);
	else
		obs_canvas_render(canvas);
	gs_texrender_end(resource.staging);
	return gs_texrender_get_texture(resource.staging);
}

struct PreviewDrawSize {
	uint32_t width = 0;
	uint32_t height = 0;
};

PreviewDrawSize preview_draw_size(const PreviewTextureV2Resource &resource, uint32_t source_width,
					  uint32_t source_height, PreviewOutputScale scale)
{
	if (scale == PreviewOutputScale::Stretch)
		return {resource.width, resource.height};
	double scale_factor = 1.0;
	if (scale == PreviewOutputScale::Fit)
		scale_factor = std::min(static_cast<double>(resource.width) / source_width,
					 static_cast<double>(resource.height) / source_height);
	else if (scale == PreviewOutputScale::Fill)
		scale_factor = std::max(static_cast<double>(resource.width) / source_width,
					 static_cast<double>(resource.height) / source_height);
	return {std::max<uint32_t>(1, static_cast<uint32_t>(source_width * scale_factor + 0.5)),
		std::max<uint32_t>(1, static_cast<uint32_t>(source_height * scale_factor + 0.5))};
}

void draw_preview_stage(const PreviewTextureV2Resource &resource, gs_texture_t *stage, PreviewOutputScale scale,
				uint32_t source_width, uint32_t source_height)
{
	if (!stage || source_width == 0 || source_height == 0)
		return;
	gs_reset_blend_state();
	const PreviewDrawSize size = preview_draw_size(resource, source_width, source_height, scale);
	gs_matrix_translate3f((static_cast<float>(resource.width) - size.width) * 0.5f,
				      (static_cast<float>(resource.height) - size.height) * 0.5f, 0.0f);
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!effect)
		return;
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), stage);
	gs_enable_framebuffer_srgb(true);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(stage, 0, size.width, size.height);
	gs_enable_framebuffer_srgb(false);
}

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
	uint32_t source_width = 0;
	uint32_t source_height = 0;
	preview_source_dimensions(source, canvas, fallback_width, fallback_height, source_width, source_height);
	gs_texture_t *stage = render_preview_stage(resource, source, canvas, source_width, source_height);

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
	draw_preview_stage(resource, stage, scale, source_width, source_height);
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

struct PreviewOutputCreateOptions {
	PreviewOutputTarget target = PreviewOutputTarget::Preview;
	uint64_t target_handle = 0;
	std::shared_ptr<PreviewOutputTargetBinding> binding;
	uint32_t width = 0;
	uint32_t height = 0;
	PreviewOutputScale scale = PreviewOutputScale::Fit;
	bool enabled = true;
};

bool read_preview_output_create_format(obs_data_t *params, obs_canvas_t *dimension_canvas,
					       const PreviewOutputTargetBinding &binding, PreviewOutputCreateOptions &options,
					       RuntimeV2Error &error);
bool prepare_preview_output_target(Engine &engine, obs_data_t *params, PreviewOutputCreateOptions &options,
					   RuntimeV2Error &error);

obs_canvas_t *preview_output_scene_canvas(uint64_t scene_handle,
					      const std::unordered_map<uint64_t, uint64_t> &scene_canvases,
					      const std::unordered_map<uint64_t, CanvasEntry> &canvases)
{
	const auto scene_it = scene_canvases.find(scene_handle);
	if (scene_it == scene_canvases.end())
		return nullptr;
	const auto canvas_it = canvases.find(scene_it->second);
	return canvas_it == canvases.end() ? nullptr : canvas_it->second.canvas;
}

obs_canvas_t *preview_output_dimension_canvas(PreviewOutputTarget target, uint64_t target_handle,
						 const PreviewOutputTargetBinding &binding, uint64_t main_canvas,
						 uint64_t preview_scene,
						 const std::unordered_map<uint64_t, uint64_t> &scene_canvases,
						 const std::unordered_map<uint64_t, CanvasEntry> &canvases)
{
	if (target == PreviewOutputTarget::Canvas)
		return binding.canvas;
	if (target == PreviewOutputTarget::Scene)
		return preview_output_scene_canvas(target_handle, scene_canvases, canvases);
	if (target == PreviewOutputTarget::Source)
		return nullptr;
	if (target == PreviewOutputTarget::Preview) {
		obs_canvas_t *canvas = preview_output_scene_canvas(preview_scene, scene_canvases, canvases);
		if (canvas)
			return canvas;
	}
	const auto main_it = canvases.find(main_canvas);
	return main_it == canvases.end() ? nullptr : main_it->second.canvas;
}

bool read_preview_output_create_options(Engine &engine, obs_data_t *params, uint64_t main_canvas,
						uint64_t preview_scene,
						const std::unordered_map<uint64_t, uint64_t> &scene_canvases,
						const std::unordered_map<uint64_t, CanvasEntry> &canvases,
						PreviewOutputCreateOptions &options, RuntimeV2Error &error)
{
	if (!prepare_preview_output_target(engine, params, options, error))
		return false;
	obs_canvas_t *dimension_canvas = preview_output_dimension_canvas(options.target, options.target_handle,
										 *options.binding, main_canvas, preview_scene,
										 scene_canvases, canvases);
	return read_preview_output_create_format(params, dimension_canvas, *options.binding, options, error);
}

bool prepare_preview_output_target(Engine &engine, obs_data_t *params, PreviewOutputCreateOptions &options,
					   RuntimeV2Error &error)
{
	if (!read_preview_output_target(params, options.target, options.target_handle, error))
		return false;
	options.binding = create_preview_output_binding(engine, options.target, options.target_handle, error);
	return options.binding != nullptr;
}

bool read_preview_output_create_dimensions(obs_data_t *params, obs_canvas_t *dimension_canvas,
						  const PreviewOutputTargetBinding &binding, PreviewOutputCreateOptions &options,
						  RuntimeV2Error &error)
{
	obs_video_info video = {};
	if (dimension_canvas)
		obs_canvas_get_video_info(dimension_canvas, &video);
	if ((video.output_width == 0 || video.output_height == 0) && binding.source) {
		video.output_width = obs_source_get_width(binding.source);
		video.output_height = obs_source_get_height(binding.source);
	}
	if (video.output_width == 0 || video.output_height == 0)
		return phase2_fail(error, "not_available", "PreviewOutput target video settings are unavailable");
	if (!read_preview_output_dimension(params, "width", video.output_width, options.width, error) ||
	    !read_preview_output_dimension(params, "height", video.output_height, options.height, error))
		return false;
	return true;
}

bool read_preview_output_create_transport(obs_data_t *params, PreviewOutputCreateOptions &options,
						  RuntimeV2Error &error)
{
	if (!read_optional_transport_string(params, "pixelFormat", "bgra8", error) ||
	    !read_optional_transport_string(params, "colorSpace", "srgb", error) ||
	    !read_optional_transport_string(params, "range", "full", error))
		return false;
	if (!read_preview_output_scale(params, PreviewOutputScale::Fit, options.scale, error))
		return false;
	return true;
}

bool read_preview_output_create_format(obs_data_t *params, obs_canvas_t *dimension_canvas,
					       const PreviewOutputTargetBinding &binding, PreviewOutputCreateOptions &options,
					       RuntimeV2Error &error)
{
	if (!read_preview_output_create_dimensions(params, dimension_canvas, binding, options, error))
		return false;
	if (!read_preview_output_create_transport(params, options, error))
		return false;
	bool present = false;
	if (!phase2_read_bool(params, "enabled", options.enabled, present))
		return phase2_fail(error, "bad_request", "params.enabled must be boolean");
	return true;
}

bool preview_output_uses_canvas(const PreviewOutputV2State &state, uint64_t canvas_handle, uint64_t preview_scene,
					const std::unordered_map<uint64_t, uint64_t> &scene_canvases)
{
	if (!state.target || !state.target->available)
		return false;
	if (state.target->target == PreviewOutputTarget::Canvas)
		return state.target->handle == canvas_handle;
	if (state.target->target == PreviewOutputTarget::Scene) {
		const auto scene_it = scene_canvases.find(state.target->handle);
		return scene_it != scene_canvases.end() && scene_it->second == canvas_handle;
	}
	if (state.target->target == PreviewOutputTarget::Preview) {
		const auto scene_it = scene_canvases.find(preview_scene);
		return scene_it != scene_canvases.end() && scene_it->second == canvas_handle;
	}
	return false;
}

std::vector<std::shared_ptr<PreviewOutputV2State>> collect_canvas_video_outputs(
		uint64_t canvas_handle, uint64_t preview_scene,
		const std::unordered_map<uint64_t, uint64_t> &scene_canvases,
		const std::unordered_map<uint64_t, std::shared_ptr<PreviewOutputV2State>> &outputs, std::mutex &mutex)
{
	std::vector<std::shared_ptr<PreviewOutputV2State>> affected;
	std::lock_guard<std::mutex> lock(mutex);
	for (const auto &[_, state] : outputs) {
		if (state && state->resource && preview_output_uses_canvas(*state, canvas_handle, preview_scene, scene_canvases))
			affected.push_back(state);
	}
	return affected;
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

bool Engine::replace_preview_output_resource(const std::shared_ptr<PreviewOutputV2State> &state, uint32_t width,
							     uint32_t height, RuntimeV2Result &result, RuntimeV2Error &error)
{
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

bool Engine::v2_preview_output_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	if (!preview_output_capable_)
		return phase2_fail(error, "unsupported_capability", "D3D11 PreviewOutput is unavailable");
	uint64_t preview_scene = 0;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_scene = preview_scene_;
	}
	PreviewOutputCreateOptions options;
	if (!read_preview_output_create_options(*this, params, main_canvas_, preview_scene, scene_canvases_, canvases_,
							options, error))
		return false;

	const uint64_t handle = allocate_handle();
	RuntimeV2Error resource_error;
	std::shared_ptr<PreviewTextureV2Resource> resource = create_preview_resource(options.width, options.height, 1, resource_error);
	if (!resource) {
		error = std::move(resource_error);
		return false;
	}
	std::shared_ptr<PreviewOutputV2State> state;
	try {
		state = std::make_shared<PreviewOutputV2State>();
		state->handle = handle;
		state->target = std::move(options.binding);
		state->scale = options.scale;
		state->enabled = options.enabled;
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
	uint32_t width = 0;
	uint32_t height = 0;
	if (!read_preview_output_resize_dimensions(params, width, height, error))
		return false;
	return replace_preview_output_resource(state, width, height, result, error);
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
	const auto affected = collect_canvas_video_outputs(canvas_handle, preview_scene, scene_canvases_, preview_outputs_,
									    preview_outputs_mutex_);
	for (const auto &state : affected)
		refresh_preview_output_after_canvas_video(state, result);
}

void Engine::refresh_preview_output_after_canvas_video(const std::shared_ptr<PreviewOutputV2State> &state,
									       RuntimeV2Result &result)
{
		std::shared_ptr<PreviewTextureV2Resource> old_resource;
	{
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		old_resource = state->resource;
	}
	if (!old_resource || old_resource->generation == std::numeric_limits<uint64_t>::max())
		return;
	RuntimeV2Error replacement_error;
	std::shared_ptr<PreviewTextureV2Resource> replacement =
		create_preview_resource(old_resource->width, old_resource->height, old_resource->generation + 1,
						 replacement_error);
	if (!replacement) {
		blog(LOG_WARNING, "obs-engine: PreviewOutput resource invalidation failed after Canvas video reset: %s",
		     replacement_error.message.c_str());
		return;
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

	const PreviewRenderBatch batch = collect_preview_render_batch(preview_outputs_, preview_outputs_mutex_);
	if (batch.jobs.empty())
		return;

	obs_source_t *program_source = batch.need_program ? obs_get_output_source(0) : nullptr;
	obs_source_t *preview_source = nullptr;
	if (batch.need_preview) {
		std::lock_guard<std::mutex> lock(preview_outputs_mutex_);
		preview_source = obs_source_get_ref(preview_source_);
	}
	for (const PreviewRenderJob &job : batch.jobs) {
		obs_source_t *source = nullptr;
		obs_canvas_t *canvas = nullptr;
		resolve_preview_render_target(job, program_source, preview_source, source, canvas);
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
