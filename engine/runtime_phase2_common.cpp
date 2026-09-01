#include "runtime_phase2_common.hpp"

#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace obs_engine {

void phase2_reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool phase2_fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "Phase-2 runtime operation failed";
	return false;
}

bool phase2_is_bounded_string(std::string_view value, size_t max_bytes, bool allow_empty)
{
	return (allow_empty || !value.empty()) && value.size() <= max_bytes;
}

bool phase2_read_string(obs_data_t *data, const char *name, std::string &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	out = value ? value : "";
	obs_data_item_release(&item);
	return true;
}

bool phase2_read_bool(obs_data_t *data, const char *name, bool &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_BOOLEAN) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_bool(item);
	obs_data_item_release(&item);
	return true;
}

bool phase2_read_integer(obs_data_t *data, const char *name, long long &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_NUMBER || obs_data_item_numtype(item) != OBS_DATA_NUM_INT) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_int(item);
	obs_data_item_release(&item);
	return true;
}

bool phase2_read_double(obs_data_t *data, const char *name, double &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_NUMBER) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_double(item);
	obs_data_item_release(&item);
	return true;
}

bool phase2_read_object(obs_data_t *data, const char *name, ObsDataPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_obj(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
}

bool phase2_read_array(obs_data_t *data, const char *name, ObsArrayPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_ARRAY) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_array(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
}

bool phase2_parse_handle(std::string_view value, uint64_t &out)
{
	if (value.empty() || (value.size() > 1 && value.front() == '0'))
		return false;
	uint64_t parsed_value = 0;
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value, 10);
	if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || parsed_value == 0 ||
	    parsed_value > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
		return false;
	out = parsed_value;
	return true;
}

bool phase2_read_handle(obs_data_t *data, const char *name, uint64_t &out)
{
	std::string value;
	bool present = false;
	return phase2_read_string(data, name, value, present) && present && phase2_parse_handle(value, out);
}

bool phase2_read_nullable_handle(obs_data_t *data, const char *name, uint64_t &out, bool &is_null, bool &present)
{
	out = 0;
	is_null = false;
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	const enum obs_data_type type = obs_data_item_gettype(item);
	if (type == OBS_DATA_NULL) {
		is_null = true;
		obs_data_item_release(&item);
		return true;
	}
	if (type == OBS_DATA_OBJECT) {
		ObsDataPtr object(obs_data_item_get_obj(item));
		obs_data_item_release(&item);
		if (!object) {
			is_null = true;
			return true;
		}
		return false;
	}
	if (type != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	const bool valid = value && phase2_parse_handle(value, out);
	obs_data_item_release(&item);
	return valid;
}

void phase2_set_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	const std::string value = std::to_string(handle);
	obs_data_set_string(data, name, value.c_str());
}

ObsDataPtr phase2_clone_data(obs_data_t *data)
{
	if (!data)
		return {};
	const char *json = obs_data_get_json(data);
	return json ? ObsDataPtr(obs_data_create_from_json(json)) : ObsDataPtr{};
}

void phase2_append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	result.events.push_back(RuntimeV2Event{name ? name : "", std::move(data)});
}

const char *phase2_scale_filter_name(enum obs_scale_type value)
{
	switch (value) {
	case OBS_SCALE_DISABLE:
		return "disable";
	case OBS_SCALE_POINT:
		return "point";
	case OBS_SCALE_BICUBIC:
		return "bicubic";
	case OBS_SCALE_BILINEAR:
		return "bilinear";
	case OBS_SCALE_LANCZOS:
		return "lanczos";
	case OBS_SCALE_AREA:
		return "area";
	}
	return "unknown";
}

const char *phase2_blend_method_name(enum obs_blending_method value)
{
	switch (value) {
	case OBS_BLEND_METHOD_DEFAULT:
		return "default";
	case OBS_BLEND_METHOD_SRGB_OFF:
		return "srgbOff";
	}
	return "unknown";
}

const char *phase2_blend_mode_name(enum obs_blending_type value)
{
	switch (value) {
	case OBS_BLEND_NORMAL:
		return "normal";
	case OBS_BLEND_ADDITIVE:
		return "additive";
	case OBS_BLEND_SUBTRACT:
		return "subtract";
	case OBS_BLEND_SCREEN:
		return "screen";
	case OBS_BLEND_MULTIPLY:
		return "multiply";
	case OBS_BLEND_LIGHTEN:
		return "lighten";
	case OBS_BLEND_DARKEN:
		return "darken";
	}
	return "unknown";
}

const char *phase2_bounds_type_name(enum obs_bounds_type value)
{
	switch (value) {
	case OBS_BOUNDS_NONE:
		return "none";
	case OBS_BOUNDS_STRETCH:
		return "stretch";
	case OBS_BOUNDS_SCALE_INNER:
		return "scaleInner";
	case OBS_BOUNDS_SCALE_OUTER:
		return "scaleOuter";
	case OBS_BOUNDS_SCALE_TO_WIDTH:
		return "scaleToWidth";
	case OBS_BOUNDS_SCALE_TO_HEIGHT:
		return "scaleToHeight";
	case OBS_BOUNDS_MAX_ONLY:
		return "maxOnly";
	}
	return "unknown";
}

bool phase2_parse_scale_filter(std::string_view value, enum obs_scale_type &out)
{
	constexpr std::pair<std::string_view, enum obs_scale_type> values[] = {
		{"disable", OBS_SCALE_DISABLE}, {"point", OBS_SCALE_POINT},       {"bicubic", OBS_SCALE_BICUBIC},
		{"bilinear", OBS_SCALE_BILINEAR}, {"lanczos", OBS_SCALE_LANCZOS}, {"area", OBS_SCALE_AREA},
	};
	for (const auto &[name, parsed] : values) {
		if (name == value) {
			out = parsed;
			return true;
		}
	}
	return false;
}

bool phase2_parse_blend_method(std::string_view value, enum obs_blending_method &out)
{
	if (value == "default") {
		out = OBS_BLEND_METHOD_DEFAULT;
		return true;
	}
	if (value == "srgbOff") {
		out = OBS_BLEND_METHOD_SRGB_OFF;
		return true;
	}
	return false;
}

bool phase2_parse_blend_mode(std::string_view value, enum obs_blending_type &out)
{
	constexpr std::pair<std::string_view, enum obs_blending_type> values[] = {
		{"normal", OBS_BLEND_NORMAL},       {"additive", OBS_BLEND_ADDITIVE}, {"subtract", OBS_BLEND_SUBTRACT},
		{"screen", OBS_BLEND_SCREEN},       {"multiply", OBS_BLEND_MULTIPLY}, {"lighten", OBS_BLEND_LIGHTEN},
		{"darken", OBS_BLEND_DARKEN},
	};
	for (const auto &[name, parsed] : values) {
		if (name == value) {
			out = parsed;
			return true;
		}
	}
	return false;
}

bool phase2_parse_bounds_type(std::string_view value, enum obs_bounds_type &out)
{
	constexpr std::pair<std::string_view, enum obs_bounds_type> values[] = {
		{"none", OBS_BOUNDS_NONE},           {"stretch", OBS_BOUNDS_STRETCH},
		{"scaleInner", OBS_BOUNDS_SCALE_INNER}, {"scaleOuter", OBS_BOUNDS_SCALE_OUTER},
		{"scaleToWidth", OBS_BOUNDS_SCALE_TO_WIDTH}, {"scaleToHeight", OBS_BOUNDS_SCALE_TO_HEIGHT},
		{"maxOnly", OBS_BOUNDS_MAX_ONLY},
	};
	for (const auto &[name, parsed] : values) {
		if (name == value) {
			out = parsed;
			return true;
		}
	}
	return false;
}

} // namespace obs_engine
