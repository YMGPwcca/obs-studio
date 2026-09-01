#include "runtime_phase2_common.hpp"

#include <obs-defs.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace obs_engine {
namespace {

constexpr uint32_t kMinCanvasDimension = 16;
constexpr uint32_t kMaxCanvasDimension = 16384;
constexpr uint32_t kMaxCanvasRate = 1000;

struct CanvasFormatName {
	std::string_view name;
	enum video_format value;
};

constexpr CanvasFormatName kCanvasFormats[] = {
	{"rgba", VIDEO_FORMAT_RGBA}, {"bgra", VIDEO_FORMAT_BGRA}, {"bgrx", VIDEO_FORMAT_BGRX},
	{"y800", VIDEO_FORMAT_Y800}, {"i420", VIDEO_FORMAT_I420}, {"nv12", VIDEO_FORMAT_NV12},
	{"yvyu", VIDEO_FORMAT_YVYU}, {"yuy2", VIDEO_FORMAT_YUY2}, {"uyvy", VIDEO_FORMAT_UYVY},
	{"i444", VIDEO_FORMAT_I444}, {"bgr3", VIDEO_FORMAT_BGR3}, {"i422", VIDEO_FORMAT_I422},
	{"i40a", VIDEO_FORMAT_I40A}, {"i42a", VIDEO_FORMAT_I42A}, {"yuva", VIDEO_FORMAT_YUVA},
	{"ayuv", VIDEO_FORMAT_AYUV}, {"i010", VIDEO_FORMAT_I010}, {"p010", VIDEO_FORMAT_P010},
	{"i210", VIDEO_FORMAT_I210}, {"i412", VIDEO_FORMAT_I412}, {"ya2l", VIDEO_FORMAT_YA2L},
	{"p216", VIDEO_FORMAT_P216}, {"p416", VIDEO_FORMAT_P416}, {"v210", VIDEO_FORMAT_V210},
	{"r10l", VIDEO_FORMAT_R10L}, {"none", VIDEO_FORMAT_NONE},
};

const char *canvas_format_name(enum video_format value)
{
	for (const CanvasFormatName &entry : kCanvasFormats) {
		if (entry.value == value)
			return entry.name.data();
	}
	return "unknown";
}

bool parse_canvas_format(std::string_view value, enum video_format &out)
{
	for (const CanvasFormatName &entry : kCanvasFormats) {
		if (entry.name == value) {
			out = entry.value;
			return true;
		}
	}
	return false;
}

const char *canvas_colorspace_name(enum video_colorspace value)
{
	switch (value) {
	case VIDEO_CS_DEFAULT:
		return "default";
	case VIDEO_CS_601:
		return "rec601";
	case VIDEO_CS_709:
		return "rec709";
	case VIDEO_CS_SRGB:
		return "srgb";
	case VIDEO_CS_2100_PQ:
		return "rec2100pq";
	case VIDEO_CS_2100_HLG:
		return "rec2100hlg";
	}
	return "unknown";
}

bool parse_canvas_colorspace(std::string_view value, enum video_colorspace &out)
{
	if (value == "default")
		out = VIDEO_CS_DEFAULT;
	else if (value == "rec601")
		out = VIDEO_CS_601;
	else if (value == "rec709")
		out = VIDEO_CS_709;
	else if (value == "srgb")
		out = VIDEO_CS_SRGB;
	else if (value == "rec2100pq")
		out = VIDEO_CS_2100_PQ;
	else if (value == "rec2100hlg")
		out = VIDEO_CS_2100_HLG;
	else
		return false;
	return true;
}

const char *canvas_range_name(enum video_range_type value)
{
	switch (value) {
	case VIDEO_RANGE_DEFAULT:
		return "default";
	case VIDEO_RANGE_PARTIAL:
		return "partial";
	case VIDEO_RANGE_FULL:
		return "full";
	}
	return "unknown";
}

bool parse_canvas_range(std::string_view value, enum video_range_type &out)
{
	if (value == "default")
		out = VIDEO_RANGE_DEFAULT;
	else if (value == "partial")
		out = VIDEO_RANGE_PARTIAL;
	else if (value == "full")
		out = VIDEO_RANGE_FULL;
	else
		return false;
	return true;
}

const char *canvas_scale_name(enum obs_scale_type value)
{
	return phase2_scale_filter_name(value);
}

bool parse_canvas_scale(std::string_view value, enum obs_scale_type &out)
{
	return phase2_parse_scale_filter(value, out);
}

bool canvas_video_dimensions_equal(const obs_video_info &left, const obs_video_info &right)
{
	return left.base_width == right.base_width && left.base_height == right.base_height &&
	       left.output_width == right.output_width && left.output_height == right.output_height;
}

bool canvas_video_format_equal(const obs_video_info &left, const obs_video_info &right)
{
	return left.output_format == right.output_format && left.fps_num == right.fps_num && left.fps_den == right.fps_den;
}

bool canvas_video_options_equal(const obs_video_info &left, const obs_video_info &right)
{
	return left.adapter == right.adapter && left.gpu_conversion == right.gpu_conversion &&
	       left.colorspace == right.colorspace && left.range == right.range && left.scale_type == right.scale_type;
}

bool canvas_video_equal(const obs_video_info &left, const obs_video_info &right)
{
	return canvas_video_dimensions_equal(left, right) && canvas_video_format_equal(left, right) &&
	       canvas_video_options_equal(left, right);
}

bool read_bounded_canvas_integer(obs_data_t *settings, const char *name, long long minimum, long long maximum,
					 long long &value, bool &present, const char *message, RuntimeV2Error &error)
{
	if (!phase2_read_integer(settings, name, value, present))
		return phase2_fail(error, "bad_request", message);
	if (present && (value < minimum || value > maximum))
		return phase2_fail(error, "bad_request", message);
	return true;
}

bool read_canvas_dimensions(obs_data_t *settings, obs_video_info &video, RuntimeV2Error &error)
{
	long long value = 0;
	bool present = false;
	if (!read_bounded_canvas_integer(settings, "width", kMinCanvasDimension, kMaxCanvasDimension, value, present,
					 "videoSettings.width is outside the supported range", error))
		return false;
	if (present)
		video.base_width = video.output_width = static_cast<uint32_t>(value);
	if (!read_bounded_canvas_integer(settings, "height", kMinCanvasDimension, kMaxCanvasDimension, value, present,
					 "videoSettings.height is outside the supported range", error))
		return false;
	if (present)
		video.base_height = video.output_height = static_cast<uint32_t>(value);
	if (!read_bounded_canvas_integer(settings, "outputWidth", kMinCanvasDimension, kMaxCanvasDimension, value,
					 present, "videoSettings.outputWidth is outside the supported range", error))
		return false;
	if (present)
		video.output_width = static_cast<uint32_t>(value);
	if (!read_bounded_canvas_integer(settings, "outputHeight", kMinCanvasDimension, kMaxCanvasDimension, value,
					 present, "videoSettings.outputHeight is outside the supported range", error))
		return false;
	if (present)
		video.output_height = static_cast<uint32_t>(value);
	return true;
}

bool read_canvas_frame_rate(obs_data_t *settings, obs_video_info &video, RuntimeV2Error &error)
{
	long long value = 0;
	bool present = false;
	if (!read_bounded_canvas_integer(settings, "fpsNumerator", 1, kMaxCanvasRate, value, present,
					 "videoSettings.fpsNumerator is invalid", error))
		return false;
	if (present)
		video.fps_num = static_cast<uint32_t>(value);
	if (!read_bounded_canvas_integer(settings, "fpsDenominator", 1, kMaxCanvasRate, value, present,
					 "videoSettings.fpsDenominator is invalid", error))
		return false;
	if (present)
		video.fps_den = static_cast<uint32_t>(value);
	return true;
}

bool read_canvas_format_fields(obs_data_t *settings, obs_video_info &video, RuntimeV2Error &error)
{

	std::string value;
	bool present = false;
	if (!phase2_read_string(settings, "format", value, present))
		return phase2_fail(error, "bad_request", "videoSettings.format must be a semantic string");
	if (present && !parse_canvas_format(value, video.output_format))
		return phase2_fail(error, "bad_request", "videoSettings.format is unsupported");
	if (!phase2_read_string(settings, "colorSpace", value, present))
		return phase2_fail(error, "bad_request", "videoSettings.colorSpace must be a semantic string");
	if (present && !parse_canvas_colorspace(value, video.colorspace))
		return phase2_fail(error, "bad_request", "videoSettings.colorSpace is unsupported");
	if (!phase2_read_string(settings, "range", value, present))
		return phase2_fail(error, "bad_request", "videoSettings.range must be a semantic string");
	if (present && !parse_canvas_range(value, video.range))
		return phase2_fail(error, "bad_request", "videoSettings.range is unsupported");
	return true;
}

bool read_canvas_scale_field(obs_data_t *settings, obs_video_info &video, RuntimeV2Error &error)
{
	std::string value;
	bool present = false;
	if (!phase2_read_string(settings, "scaleType", value, present))
		return phase2_fail(error, "bad_request", "videoSettings.scaleType must be a semantic string");
	if (present && !parse_canvas_scale(value, video.scale_type))
		return phase2_fail(error, "bad_request", "videoSettings.scaleType is unsupported");
	return true;
}

bool read_canvas_device_fields(obs_data_t *settings, obs_video_info &video, RuntimeV2Error &error)
{
	long long value = 0;
	bool present = false;
	if (!read_bounded_canvas_integer(settings, "adapter", 0, 64, value, present,
					 "videoSettings.adapter is invalid", error))
		return false;
	if (present)
		video.adapter = static_cast<uint32_t>(value);
	if (!phase2_read_bool(settings, "gpuConversion", video.gpu_conversion, present))
		return phase2_fail(error, "bad_request", "videoSettings.gpuConversion must be boolean");
	return true;
}

bool canvas_video_complete(const obs_video_info &video)
{
	return video.base_width != 0 && video.base_height != 0 && video.output_width != 0 && video.output_height != 0 &&
	       video.fps_num != 0 && video.fps_den != 0 && video.output_format != VIDEO_FORMAT_NONE;
}

bool read_canvas_video_settings(obs_data_t *settings, obs_video_info &video, RuntimeV2Error &error)
{
	if (!read_canvas_dimensions(settings, video, error) || !read_canvas_frame_rate(settings, video, error) ||
	    !read_canvas_format_fields(settings, video, error) || !read_canvas_scale_field(settings, video, error) ||
	    !read_canvas_device_fields(settings, video, error))
		return false;
	if (!canvas_video_complete(video))
		return phase2_fail(error, "bad_request", "videoSettings contains an incomplete video configuration");
	return true;
}

ObsDataPtr make_canvas_video_data(uint64_t handle, obs_canvas_t *canvas)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "canvas", handle);
	obs_video_info video = {};
	if (!obs_canvas_get_video_info(canvas, &video)) {
		obs_data_set_bool(data.get(), "hasVideo", false);
		return data;
	}
	obs_data_set_bool(data.get(), "hasVideo", true);
	obs_data_set_int(data.get(), "width", video.base_width);
	obs_data_set_int(data.get(), "height", video.base_height);
	obs_data_set_int(data.get(), "outputWidth", video.output_width);
	obs_data_set_int(data.get(), "outputHeight", video.output_height);
	obs_data_set_int(data.get(), "fpsNumerator", video.fps_num);
	obs_data_set_int(data.get(), "fpsDenominator", video.fps_den);
	obs_data_set_string(data.get(), "format", canvas_format_name(video.output_format));
	obs_data_set_string(data.get(), "colorSpace", canvas_colorspace_name(video.colorspace));
	obs_data_set_string(data.get(), "range", canvas_range_name(video.range));
	obs_data_set_string(data.get(), "scaleType", canvas_scale_name(video.scale_type));
	obs_data_set_int(data.get(), "adapter", video.adapter);
	obs_data_set_bool(data.get(), "gpuConversion", video.gpu_conversion);
	return data;
}

ObsDataPtr make_canvas_summary(uint64_t handle, const CanvasEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "canvas", handle);
	obs_data_set_string(data.get(), "name", obs_canvas_get_name(entry.canvas) ? obs_canvas_get_name(entry.canvas) : "");
	obs_data_set_string(data.get(), "uuid", obs_canvas_get_uuid(entry.canvas) ? obs_canvas_get_uuid(entry.canvas) : "");
	obs_data_set_bool(data.get(), "isMain", entry.is_main);
	const uint32_t flags = obs_canvas_get_flags(entry.canvas);
	obs_data_set_bool(data.get(), "activate", (flags & ACTIVATE) != 0);
	obs_data_set_bool(data.get(), "mixAudio", (flags & MIX_AUDIO) != 0);
	obs_data_set_bool(data.get(), "sceneRef", (flags & SCENE_REF) != 0);
	obs_data_set_bool(data.get(), "ephemeral", (flags & EPHEMERAL) != 0);
	obs_data_set_int(data.get(), "rawFlags", static_cast<long long>(flags));
	obs_data_set_bool(data.get(), "hasVideo", obs_canvas_has_video(entry.canvas));
	return data;
}

struct CanvasTarget {
	obs_source_t *source = nullptr;
	uint64_t source_handle = 0;
	uint64_t scene_handle = 0;
	bool empty = false;
};

bool read_canvas_source_target(Engine &engine, obs_data_t *object, CanvasTarget &target, RuntimeV2Error &error)
{
	if (!phase2_read_handle(object, "source", target.source_handle))
		return phase2_fail(error, "bad_request", "target.source must be a canonical source handle");
	target.source = engine.v2_source_for_handle(target.source_handle);
	return target.source || phase2_fail(error, "not_found", "target source handle was not found");
}

bool read_canvas_scene_target(Engine &engine, obs_data_t *object, CanvasTarget &target, RuntimeV2Error &error)
{
	if (!phase2_read_handle(object, "scene", target.scene_handle))
		return phase2_fail(error, "bad_request", "target.scene must be a canonical scene handle");
	obs_scene_t *scene = engine.v2_scene_for_handle(target.scene_handle);
	if (!scene)
		return phase2_fail(error, "not_found", "target scene handle was not found");
	target.source = obs_scene_get_source(scene);
	return true;
}

bool read_canvas_target_type(Engine &engine, obs_data_t *object, const std::string &type, CanvasTarget &target,
				     RuntimeV2Error &error)
{
	if (type == "source")
		return read_canvas_source_target(engine, object, target, error);
	if (type == "scene")
		return read_canvas_scene_target(engine, object, target, error);
	return phase2_fail(error, "bad_request", "canvas channel target type must be source, scene, or null");
}

bool read_canvas_target(Engine &engine, obs_data_t *params, CanvasTarget &target, RuntimeV2Error &error)
{
	obs_data_item_t *item = obs_data_item_byname(params, "target");
	if (!item)
		return phase2_fail(error, "bad_request", "params.target must be an object or null");
	if (obs_data_item_gettype(item) == OBS_DATA_NULL) {
		target.empty = true;
		obs_data_item_release(&item);
		return true;
	}
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return phase2_fail(error, "bad_request", "params.target must be an object or null");
	}
	ObsDataPtr object(obs_data_item_get_obj(item));
	obs_data_item_release(&item);
	if (!object) {
		target.empty = true;
		return true;
	}
	std::string type;
	bool present = false;
	if (!phase2_read_string(object.get(), "type", type, present) || !present)
		return phase2_fail(error, "bad_request", "target.type is required");
	return read_canvas_target_type(engine, object.get(), type, target, error);
}

ObsDataPtr make_channel_target(const Engine &engine, obs_source_t *source)
{
	ObsDataPtr target(obs_data_create());
	if (!source) {
		obs_data_set_string(target.get(), "type", "none");
		return target;
	}
	const uint64_t source_handle = engine.v2_source_handle_for_pointer(source);
	if (source_handle != 0) {
		obs_data_set_string(target.get(), "type", "source");
		phase2_set_handle(target.get(), "source", source_handle);
		return target;
	}
	const uint64_t scene_handle = engine.v2_scene_handle_for_pointer(source);
	if (scene_handle != 0) {
		obs_data_set_string(target.get(), "type", "scene");
		phase2_set_handle(target.get(), "scene", scene_handle);
		return target;
	}
	obs_data_set_string(target.get(), "type", "unavailable");
	return target;
}

bool read_channel_index(obs_data_t *params, uint32_t &channel, RuntimeV2Error &error)
{
	long long value = 0;
	bool present = false;
	if (!phase2_read_integer(params, "channel", value, present) || !present || value < 0 || value >= MAX_CHANNELS)
		return phase2_fail(error, "bad_request", "params.channel must be an integer in the supported channel range");
	channel = static_cast<uint32_t>(value);
	return true;
}

bool read_canvas_flag(obs_data_t *flags, const char *name, uint32_t bit, uint32_t &value, const char *message,
				      RuntimeV2Error &error)
{
	bool flag = false;
	bool present = false;
	if (!phase2_read_bool(flags, name, flag, present))
		return phase2_fail(error, "bad_request", message);
	if (present) {
		if (flag)
			value |= bit;
		else
			value &= ~bit;
	}
	return true;
}

bool read_canvas_name_and_video(obs_data_t *params, std::string &name, bool &name_present, obs_video_info &video,
						RuntimeV2Error &error)
{
	if (!phase2_read_string(params, "name", name, name_present))
		return phase2_fail(error, "bad_request", "params.name must be a string when present");
	if (name_present && !phase2_is_bounded_string(name, 256))
		return phase2_fail(error, "bad_request", "canvas name must be a non-empty string of at most 256 bytes");
	if (!obs_get_video_info(&video))
		return phase2_fail(error, "not_available", "Main Canvas video settings are unavailable");
	ObsDataPtr video_settings;
	bool video_present = false;
	if (!phase2_read_object(params, "videoSettings", video_settings, video_present))
		return phase2_fail(error, "bad_request", "params.videoSettings must be an object when present");
	if (video_present && !read_canvas_video_settings(video_settings.get(), video, error))
		return false;
	return true;
}

bool read_canvas_flags(obs_data_t *params, uint32_t &flags, RuntimeV2Error &error)
{
	ObsDataPtr requested_flags;
	bool flags_present = false;
	if (!phase2_read_object(params, "flags", requested_flags, flags_present))
		return phase2_fail(error, "bad_request", "params.flags must be an object when present");
	flags = ACTIVATE | MIX_AUDIO | SCENE_REF | EPHEMERAL;
	if (!flags_present)
		return true;
	if (!read_canvas_flag(requested_flags.get(), "activate", ACTIVATE, flags, "flags.activate must be boolean", error))
		return false;
	if (!read_canvas_flag(requested_flags.get(), "mixAudio", MIX_AUDIO, flags, "flags.mixAudio must be boolean", error))
		return false;
	if (!read_canvas_flag(requested_flags.get(), "sceneRef", SCENE_REF, flags, "flags.sceneRef must be boolean", error))
		return false;
	return read_canvas_flag(requested_flags.get(), "ephemeral", EPHEMERAL, flags, "flags.ephemeral must be boolean", error);
}

bool read_canvas_create_options(obs_data_t *params, std::string &name, bool &name_present, obs_video_info &video,
					 uint32_t &flags, RuntimeV2Error &error)
{
	if (!read_canvas_name_and_video(params, name, name_present, video, error))
		return false;
	return read_canvas_flags(params, flags, error);
}

bool prepare_canvas_video_update(obs_data_t *params, obs_canvas_t *canvas, obs_video_info &current,
					obs_video_info &proposed, bool &changed, RuntimeV2Error &error)
{
	ObsDataPtr settings;
	bool present = false;
	if (!phase2_read_object(params, "videoSettings", settings, present) || !present)
		return phase2_fail(error, "bad_request", "params.videoSettings object is required");
	if (!obs_canvas_get_video_info(canvas, &current))
		return phase2_fail(error, "not_available", "Canvas has no video settings");
	proposed = current;
	if (!read_canvas_video_settings(settings.get(), proposed, error))
		return false;
	changed = !canvas_video_equal(current, proposed);
	return true;
}

} // namespace

bool Engine::v2_canvas_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::vector<uint64_t> handles;
	for (const auto &[handle, _] : canvases_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr canvases(obs_data_array_create());
	for (const uint64_t handle : handles)
		obs_data_array_push_back(canvases.get(), make_canvas_summary(handle, canvases_.at(handle)).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "canvases", canvases.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(handles.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_canvas_get_main(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	auto it = canvases_.find(main_canvas_);
	if (it == canvases_.end())
		return phase2_fail(error, "internal_error", "Main Canvas is not registered");
	result.data = make_canvas_summary(main_canvas_, it->second);
	return true;
}

bool Engine::v2_canvas_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	result.data = make_canvas_summary(handle, *entry);
	obs_data_set_obj(result.data.get(), "video", make_canvas_video_data(handle, entry->canvas).get());
	return true;
}

bool Engine::v2_canvas_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::string name;
	bool name_present = false;
	obs_video_info video = {};
	uint32_t flags = 0;
	if (!read_canvas_create_options(params, name, name_present, video, flags, error))
		return false;
	const uint64_t handle = allocate_handle();
	const std::string generated = "engine-canvas-" + std::to_string(handle);
	obs_canvas_t *canvas = obs_canvas_create_private(name_present ? name.c_str() : generated.c_str(), &video, flags);
	if (!canvas)
		return phase2_fail(error, "obs_error", "obs_canvas_create_private failed");
	try {
		if (!canvases_.emplace(handle, CanvasEntry{canvas, false}).second)
			throw std::runtime_error("canvas handle collision");
	} catch (...) {
		obs_canvas_remove(canvas);
		obs_canvas_release(canvas);
		return phase2_fail(error, "internal_error", "could not register Canvas handle");
	}
	result.data = make_canvas_summary(handle, canvases_.at(handle));
	obs_data_set_obj(result.data.get(), "video", make_canvas_video_data(handle, canvas).get());
	phase2_append_event(result, "canvas.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_canvas_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	if (entry->is_main)
		return phase2_fail(error, "invalid_state", "Main Canvas cannot be removed");
	for (const auto &[scene_handle, canvas_handle] : scene_canvases_)
		if (canvas_handle == handle)
			return phase2_fail(error, "object_in_use", "Canvas still owns one or more Scenes");
	for (uint32_t channel = 0; channel < MAX_CHANNELS; ++channel) {
		obs_source_t *source = obs_canvas_get_channel(entry->canvas, channel);
		if (source) {
			obs_source_release(source);
			return phase2_fail(error, "object_in_use", "Canvas still has a routed channel source");
		}
	}
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "canvas", handle);
	result.data = std::move(data);
	v2_preview_output_invalidate_canvas(handle, result);
	obs_canvas_remove(entry->canvas);
	obs_canvas_release(entry->canvas);
	canvases_.erase(handle);
	phase2_append_event(result, "canvas.removed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_canvas_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	if (entry->is_main)
		return phase2_fail(error, "invalid_state", "Main Canvas cannot be renamed");
	std::string name;
	bool present = false;
	if (!phase2_read_string(params, "name", name, present) || !present || !phase2_is_bounded_string(name, 256))
		return phase2_fail(error, "bad_request", "params.name must be a non-empty canvas name of at most 256 bytes");
	const std::string previous = obs_canvas_get_name(entry->canvas) ? obs_canvas_get_name(entry->canvas) : "";
	if (previous == name) {
		result.data = make_canvas_summary(handle, *entry);
		return true;
	}
	obs_canvas_set_name(entry->canvas, name.c_str());
	result.data = make_canvas_summary(handle, *entry);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "canvas", handle);
	obs_data_set_string(event_data.get(), "previousName", previous.c_str());
	obs_data_set_string(event_data.get(), "name", obs_canvas_get_name(entry->canvas));
	phase2_append_event(result, "canvas.renamed", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_canvas_get_video_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	result.data = make_canvas_video_data(handle, entry->canvas);
	return true;
}

bool Engine::v2_canvas_set_video_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	if (entry->is_main)
		return phase2_fail(error, "invalid_state", "Main Canvas video settings are controlled by engine startup");
	obs_video_info current = {};
	obs_video_info proposed = current;
	bool changed = false;
	if (!prepare_canvas_video_update(params, entry->canvas, current, proposed, changed, error))
		return false;
	if (!changed) {
		result.data = make_canvas_video_data(handle, entry->canvas);
		return true;
	}
	if (!obs_canvas_reset_video(entry->canvas, &proposed))
		return phase2_fail(error, obs_video_active() ? "busy" : "obs_error",
				   obs_video_active() ? "Canvas video reset is busy while video is active" : "Canvas video reset failed");
	result.data = make_canvas_video_data(handle, entry->canvas);
	phase2_append_event(result, "canvas.videoSettingsChanged", phase2_clone_data(result.data.get()));
	v2_preview_output_invalidate_canvas_video(handle, result);
	result.mutated = true;
	return true;
}

bool Engine::v2_canvas_list_scenes(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	std::vector<uint64_t> scenes;
	for (const auto &[scene_handle, canvas_handle] : scene_canvases_)
		if (canvas_handle == handle)
			scenes.push_back(scene_handle);
	std::sort(scenes.begin(), scenes.end());
	ObsArrayPtr values(obs_data_array_create());
	for (const uint64_t scene_handle : scenes)
		if (scenes_.contains(scene_handle))
			obs_data_array_push_back(values.get(), v2_scene_summary(scene_handle, scenes_.at(scene_handle)).get());
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "canvas", handle);
	obs_data_set_array(data.get(), "scenes", values.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(scenes.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_canvas_get_channel(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	uint32_t channel = 0;
	if (!read_channel_index(params, channel, error))
		return false;
	obs_source_t *source = obs_canvas_get_channel(entry->canvas, channel);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "canvas", handle);
	obs_data_set_int(data.get(), "channel", channel);
	obs_data_set_obj(data.get(), "target", make_channel_target(*this, source).get());
	if (source)
		obs_source_release(source);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_canvas_set_channel(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	uint32_t channel = 0;
	if (!read_channel_index(params, channel, error))
		return false;
	if (entry->is_main && channel == 0)
		return phase2_fail(error, "invalid_state", "Main Canvas channel 0 is owned by program.setScene");
	CanvasTarget target;
	if (!read_canvas_target(*this, params, target, error))
		return false;
	obs_source_t *previous = obs_canvas_get_channel(entry->canvas, channel);
	if (previous == target.source) {
		if (previous)
			obs_source_release(previous);
		return v2_canvas_get_channel(params, result, error);
	}
	if (previous)
		obs_source_release(previous);
	obs_canvas_set_channel(entry->canvas, channel, target.source);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "canvas", handle);
	obs_data_set_int(data.get(), "channel", channel);
	obs_source_t *actual = obs_canvas_get_channel(entry->canvas, channel);
	obs_data_set_obj(data.get(), "target", make_channel_target(*this, actual).get());
	if (actual)
		obs_source_release(actual);
	result.data = std::move(data);
	phase2_append_event(result, "canvas.channelChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_canvas_get_flags(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	CanvasEntry *entry = nullptr;
	if (!v2_get_canvas_entry(params, handle, entry, error))
		return false;
	result.data = make_canvas_summary(handle, *entry);
	return true;
}

} // namespace obs_engine
