#include "properties.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace obs_engine {
namespace {

const char *safe_string(const char *value)
{
	return value ? value : "";
}

const char *property_type_name(enum obs_property_type type)
{
	struct Entry {
		enum obs_property_type type;
		const char *name;
	};
	constexpr Entry entries[] = {
		{OBS_PROPERTY_BOOL, "bool"},
		{OBS_PROPERTY_INT, "int"},
		{OBS_PROPERTY_FLOAT, "float"},
		{OBS_PROPERTY_TEXT, "text"},
		{OBS_PROPERTY_PATH, "path"},
		{OBS_PROPERTY_LIST, "list"},
		{OBS_PROPERTY_COLOR, "color"},
		{OBS_PROPERTY_BUTTON, "button"},
		{OBS_PROPERTY_FONT, "font"},
		{OBS_PROPERTY_EDITABLE_LIST, "editableList"},
		{OBS_PROPERTY_FRAME_RATE, "frameRate"},
		{OBS_PROPERTY_GROUP, "group"},
		{OBS_PROPERTY_COLOR_ALPHA, "colorAlpha"},
	};
	for (const Entry &entry : entries) {
		if (entry.type == type)
			return entry.name;
	}
	return "invalid";
}

const char *number_type_name(enum obs_number_type type)
{
	return type == OBS_NUMBER_SLIDER ? "slider" : "scroller";
}

const char *text_type_name(enum obs_text_type type)
{
	switch (type) {
	case OBS_TEXT_PASSWORD:
		return "password";
	case OBS_TEXT_MULTILINE:
		return "multiline";
	case OBS_TEXT_INFO:
		return "info";
	default:
		return "default";
	}
}

const char *text_info_type_name(enum obs_text_info_type type)
{
	switch (type) {
	case OBS_TEXT_INFO_WARNING:
		return "warning";
	case OBS_TEXT_INFO_ERROR:
		return "error";
	default:
		return "normal";
	}
}

const char *path_type_name(enum obs_path_type type)
{
	switch (type) {
	case OBS_PATH_FILE_SAVE:
		return "saveFile";
	case OBS_PATH_DIRECTORY:
		return "directory";
	default:
		return "openFile";
	}
}

const char *combo_type_name(enum obs_combo_type type)
{
	switch (type) {
	case OBS_COMBO_TYPE_EDITABLE:
		return "editable";
	case OBS_COMBO_TYPE_RADIO:
		return "radio";
	case OBS_COMBO_TYPE_LIST:
		return "list";
	default:
		return "invalid";
	}
}

const char *combo_format_name(enum obs_combo_format format)
{
	switch (format) {
	case OBS_COMBO_FORMAT_INT:
		return "int";
	case OBS_COMBO_FORMAT_FLOAT:
		return "float";
	case OBS_COMBO_FORMAT_STRING:
		return "string";
	case OBS_COMBO_FORMAT_BOOL:
		return "bool";
	default:
		return "invalid";
	}
}

const char *editable_list_type_name(enum obs_editable_list_type type)
{
	switch (type) {
	case OBS_EDITABLE_LIST_TYPE_FILES:
		return "files";
	case OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS:
		return "filesAndUrls";
	default:
		return "strings";
	}
}

const char *group_type_name(enum obs_group_type type)
{
	return type == OBS_GROUP_CHECKABLE ? "checkable" : "normal";
}

const char *button_type_name(enum obs_button_type type)
{
	return type == OBS_BUTTON_URL ? "url" : "default";
}

void set_nonempty_string(obs_data_t *data, const char *name, const char *value)
{
	if (value && *value)
		obs_data_set_string(data, name, value);
}

void set_frame_rate(obs_data_t *data, const char *name, struct media_frames_per_second fps)
{
	ObsDataPtr value(obs_data_create());
	obs_data_set_int(value.get(), "numerator", static_cast<long long>(fps.numerator));
	obs_data_set_int(value.get(), "denominator", static_cast<long long>(fps.denominator));
	obs_data_set_obj(data, name, value.get());
}

void erase_password_settings(obs_properties_t *properties, obs_data_t *settings)
{
	for (obs_property_t *property = obs_properties_first(properties); property;) {
		const enum obs_property_type type = obs_property_get_type(property);
		if (type == OBS_PROPERTY_TEXT && obs_property_text_type(property) == OBS_TEXT_PASSWORD) {
			obs_data_erase(settings, obs_property_name(property));
		} else if (type == OBS_PROPERTY_GROUP) {
			erase_password_settings(obs_property_group_content(property), settings);
		}
		if (!obs_property_next(&property))
			break;
	}
}

ObsDataPtr serialize_property(obs_property_t *property, obs_data_t *settings, const char *refresh_property);
ObsArrayPtr serialize_property_array(obs_properties_t *properties, obs_data_t *settings, const char *refresh_property);

using PropertySerializer = void (*)(obs_data_t *, obs_property_t *, obs_data_t *, const char *);

struct PropertySerializerEntry {
	enum obs_property_type type;
	PropertySerializer serializer;
};

void serialize_int_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	obs_data_set_int(data, "min", obs_property_int_min(property));
	obs_data_set_int(data, "max", obs_property_int_max(property));
	obs_data_set_int(data, "step", obs_property_int_step(property));
	obs_data_set_string(data, "numberType", number_type_name(obs_property_int_type(property)));
	set_nonempty_string(data, "suffix", obs_property_int_suffix(property));
}

void serialize_float_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	obs_data_set_double(data, "min", obs_property_float_min(property));
	obs_data_set_double(data, "max", obs_property_float_max(property));
	obs_data_set_double(data, "step", obs_property_float_step(property));
	obs_data_set_string(data, "numberType", number_type_name(obs_property_float_type(property)));
	set_nonempty_string(data, "suffix", obs_property_float_suffix(property));
}

void serialize_text_property(obs_data_t *data, obs_property_t *property, obs_data_t *settings, const char *)
{
	const enum obs_text_type text_type = obs_property_text_type(property);
	const char *name = safe_string(obs_property_name(property));
	obs_data_set_string(data, "textType", text_type_name(text_type));
	obs_data_set_bool(data, "monospace", obs_property_text_monospace(property));
	if (text_type == OBS_TEXT_INFO) {
		obs_data_set_string(data, "infoType", text_info_type_name(obs_property_text_info_type(property)));
		obs_data_set_bool(data, "wordWrap", obs_property_text_info_word_wrap(property));
	} else if (text_type == OBS_TEXT_PASSWORD) {
		obs_data_set_bool(data, "sensitive", true);
		const char *value = settings ? obs_data_get_string(settings, name) : nullptr;
		obs_data_set_bool(data, "hasValue", value && *value);
	}
}

void serialize_path_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	obs_data_set_string(data, "pathType", path_type_name(obs_property_path_type(property)));
	set_nonempty_string(data, "filter", obs_property_path_filter(property));
	set_nonempty_string(data, "defaultPath", obs_property_path_default_path(property));
}

void serialize_list_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	obs_data_set_string(data, "comboType", combo_type_name(obs_property_list_type(property)));
	obs_data_set_string(data, "valueType", combo_format_name(obs_property_list_format(property)));
	ObsArrayPtr items = serialize_property_list_items(property);
	obs_data_set_int(data, "itemCount", static_cast<long long>(obs_data_array_count(items.get())));
	obs_data_set_array(data, "items", items.get());
}

void serialize_color_property(obs_data_t *data, obs_property_t *, obs_data_t *, const char *)
{
	obs_data_set_string(data, "encoding", "obsColorInteger");
}

void serialize_button_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	const enum obs_button_type button_type = obs_property_button_type(property);
	obs_data_set_string(data, "buttonType", button_type_name(button_type));
	if (button_type == OBS_BUTTON_URL)
		set_nonempty_string(data, "url", obs_property_button_url(property));
}

void serialize_editable_list_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	obs_data_set_string(data, "listType", editable_list_type_name(obs_property_editable_list_type(property)));
	set_nonempty_string(data, "filter", obs_property_editable_list_filter(property));
	set_nonempty_string(data, "defaultPath", obs_property_editable_list_default_path(property));
}

void serialize_frame_rate_property(obs_data_t *data, obs_property_t *property, obs_data_t *, const char *)
{
	ObsArrayPtr options(obs_data_array_create());
	const size_t option_count = obs_property_frame_rate_options_count(property);
	for (size_t index = 0; index < option_count; ++index) {
		ObsDataPtr option(obs_data_create());
		obs_data_set_string(option.get(), "name", safe_string(obs_property_frame_rate_option_name(property, index)));
		obs_data_set_string(option.get(), "description",
				    safe_string(obs_property_frame_rate_option_description(property, index)));
		obs_data_array_push_back(options.get(), option.get());
	}
	obs_data_set_array(data, "options", options.get());

	ObsArrayPtr ranges(obs_data_array_create());
	const size_t range_count = obs_property_frame_rate_fps_ranges_count(property);
	for (size_t index = 0; index < range_count; ++index) {
		ObsDataPtr range(obs_data_create());
		set_frame_rate(range.get(), "min", obs_property_frame_rate_fps_range_min(property, index));
		set_frame_rate(range.get(), "max", obs_property_frame_rate_fps_range_max(property, index));
		obs_data_array_push_back(ranges.get(), range.get());
	}
	obs_data_set_array(data, "ranges", ranges.get());
}

void serialize_group_property(obs_data_t *data, obs_property_t *property, obs_data_t *settings,
				      const char *refresh_property)
{
	obs_data_set_string(data, "groupType", group_type_name(obs_property_group_type(property)));
	ObsArrayPtr children = serialize_property_array(obs_property_group_content(property), settings, refresh_property);
	obs_data_set_array(data, "children", children.get());
}

constexpr PropertySerializerEntry kPropertySerializers[] = {
	{OBS_PROPERTY_INT, &serialize_int_property},
	{OBS_PROPERTY_FLOAT, &serialize_float_property},
	{OBS_PROPERTY_TEXT, &serialize_text_property},
	{OBS_PROPERTY_PATH, &serialize_path_property},
	{OBS_PROPERTY_LIST, &serialize_list_property},
	{OBS_PROPERTY_COLOR, &serialize_color_property},
	{OBS_PROPERTY_COLOR_ALPHA, &serialize_color_property},
	{OBS_PROPERTY_BUTTON, &serialize_button_property},
	{OBS_PROPERTY_EDITABLE_LIST, &serialize_editable_list_property},
	{OBS_PROPERTY_FRAME_RATE, &serialize_frame_rate_property},
	{OBS_PROPERTY_GROUP, &serialize_group_property},
};

void serialize_property_details(obs_data_t *data, obs_property_t *property, obs_data_t *settings,
				       const char *refresh_property)
{
	const enum obs_property_type type = obs_property_get_type(property);
	for (const PropertySerializerEntry &entry : kPropertySerializers) {
		if (entry.type == type) {
			entry.serializer(data, property, settings, refresh_property);
			return;
		}
	}
}

ObsArrayPtr serialize_property_array(obs_properties_t *properties, obs_data_t *settings, const char *refresh_property)
{
	ObsArrayPtr array(obs_data_array_create());
	for (obs_property_t *property = obs_properties_first(properties); property;) {
		ObsDataPtr entry = serialize_property(property, settings, refresh_property);
		obs_data_array_push_back(array.get(), entry.get());
		if (!obs_property_next(&property))
			break;
	}
	return array;
}

ObsDataPtr serialize_property(obs_property_t *property, obs_data_t *settings, const char *refresh_property)
{
	ObsDataPtr data(obs_data_create());
	const char *name = safe_string(obs_property_name(property));
	const enum obs_property_type type = obs_property_get_type(property);

	obs_data_set_string(data.get(), "name", name);
	obs_data_set_string(data.get(), "type", property_type_name(type));
	obs_data_set_string(data.get(), "description", safe_string(obs_property_description(property)));
	set_nonempty_string(data.get(), "longDescription", obs_property_long_description(property));
	obs_data_set_bool(data.get(), "visible", obs_property_visible(property));
	obs_data_set_bool(data.get(), "enabled", obs_property_enabled(property));
	if (refresh_property && std::strcmp(name, refresh_property) == 0)
		obs_data_set_bool(data.get(), "requiresRefresh", true);

	serialize_property_details(data.get(), property, settings, refresh_property);

	return data;
}

void add_validation_issue(obs_data_array_t *issues, const char *property, const char *code, const char *message)
{
	ObsDataPtr issue(obs_data_create());
	obs_data_set_string(issue.get(), "property", safe_string(property));
	obs_data_set_string(issue.get(), "code", code);
	obs_data_set_string(issue.get(), "message", message);
	obs_data_array_push_back(issues, issue.get());
}

bool is_number(obs_data_item_t *item)
{
	return item && obs_data_item_gettype(item) == OBS_DATA_NUMBER;
}

bool is_integer(obs_data_item_t *item)
{
	return is_number(item) && obs_data_item_numtype(item) == OBS_DATA_NUM_INT;
}

using ListValueMatcher = bool (*)(obs_property_t *, obs_data_item_t *, size_t);

bool list_string_value_matches(obs_property_t *property, obs_data_item_t *item, size_t index)
{
	return obs_data_item_gettype(item) == OBS_DATA_STRING &&
	       std::strcmp(obs_data_item_get_string(item), safe_string(obs_property_list_item_string(property, index))) == 0;
}

bool list_int_value_matches(obs_property_t *property, obs_data_item_t *item, size_t index)
{
	return is_integer(item) && obs_data_item_get_int(item) == obs_property_list_item_int(property, index);
}

bool list_float_value_matches(obs_property_t *property, obs_data_item_t *item, size_t index)
{
	return is_number(item) && obs_data_item_get_double(item) == obs_property_list_item_float(property, index);
}

bool list_bool_value_matches(obs_property_t *property, obs_data_item_t *item, size_t index)
{
	return obs_data_item_gettype(item) == OBS_DATA_BOOLEAN &&
	       obs_data_item_get_bool(item) == obs_property_list_item_bool(property, index);
}

struct ListValueMatcherEntry {
	enum obs_combo_format format;
	ListValueMatcher matcher;
};

constexpr ListValueMatcherEntry kListValueMatchers[] = {
	{OBS_COMBO_FORMAT_STRING, &list_string_value_matches},
	{OBS_COMBO_FORMAT_INT, &list_int_value_matches},
	{OBS_COMBO_FORMAT_FLOAT, &list_float_value_matches},
	{OBS_COMBO_FORMAT_BOOL, &list_bool_value_matches},
};

ListValueMatcher find_list_value_matcher(enum obs_combo_format format)
{
	for (const ListValueMatcherEntry &entry : kListValueMatchers) {
		if (entry.format == format)
			return entry.matcher;
	}
	return nullptr;
}

bool list_value_is_enabled(obs_property_t *property, obs_data_item_t *item)
{
	const size_t count = obs_property_list_item_count(property);
	if (count == 0)
		return true;

	const ListValueMatcher matcher = find_list_value_matcher(obs_property_list_format(property));
	if (!matcher)
		return true;
	for (size_t index = 0; index < count; ++index) {
		if (matcher(property, item, index))
			return !obs_property_list_item_disabled(property, index);
	}
	return false;
}

void validate_bool_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (obs_data_item_gettype(item) != OBS_DATA_BOOLEAN)
		add_validation_issue(issues, obs_property_name(property), "type", "value must be a boolean");
}

void validate_int_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (!is_integer(item)) {
		add_validation_issue(issues, obs_property_name(property), "type", "value must be an integer");
		return;
	}
	const long long value = obs_data_item_get_int(item);
	if (value < obs_property_int_min(property) || value > obs_property_int_max(property))
		add_validation_issue(issues, obs_property_name(property), "range", "integer value is outside the property range");
}

void validate_float_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (!is_number(item)) {
		add_validation_issue(issues, obs_property_name(property), "type", "value must be a number");
		return;
	}
	const double value = obs_data_item_get_double(item);
	if (!std::isfinite(value) || value < obs_property_float_min(property) ||
	    value > obs_property_float_max(property))
		add_validation_issue(issues, obs_property_name(property), "range", "numeric value is outside the property range");
}

void validate_string_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (obs_data_item_gettype(item) != OBS_DATA_STRING)
		add_validation_issue(issues, obs_property_name(property), "type", "value must be a string");
}

bool list_value_type_matches(enum obs_combo_format format, obs_data_item_t *item)
{
	switch (format) {
	case OBS_COMBO_FORMAT_STRING:
		return obs_data_item_gettype(item) == OBS_DATA_STRING;
	case OBS_COMBO_FORMAT_INT:
		return is_integer(item);
	case OBS_COMBO_FORMAT_FLOAT:
		return is_number(item);
	case OBS_COMBO_FORMAT_BOOL:
		return obs_data_item_gettype(item) == OBS_DATA_BOOLEAN;
	default:
		return true;
	}
}

void validate_list_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	const char *name = obs_property_name(property);
	const bool type_ok = list_value_type_matches(obs_property_list_format(property), item);
	if (!type_ok) {
		add_validation_issue(issues, name, "type", "value type does not match the list property format");
	} else if (obs_property_list_type(property) != OBS_COMBO_TYPE_EDITABLE &&
		   !list_value_is_enabled(property, item)) {
		add_validation_issue(issues, name, "choice", "value is not an enabled item in the list property");
	}
}

void validate_color_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	const char *name = obs_property_name(property);
	if (!is_integer(item)) {
		add_validation_issue(issues, name, "type", "color value must be an integer");
		return;
	}
	const long long value = obs_data_item_get_int(item);
	if (value < 0 || static_cast<unsigned long long>(value) > std::numeric_limits<uint32_t>::max())
		add_validation_issue(issues, name, "range", "color value is outside the 32-bit OBS color range");
}

void validate_object_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT)
		add_validation_issue(issues, obs_property_name(property), "type", "font value must be an object");
}

void validate_array_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (obs_data_item_gettype(item) != OBS_DATA_ARRAY)
		add_validation_issue(issues, obs_property_name(property), "type", "editable-list value must be an array");
}

void validate_group_property(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	if (obs_property_group_type(property) == OBS_GROUP_CHECKABLE &&
	    obs_data_item_gettype(item) != OBS_DATA_BOOLEAN)
		add_validation_issue(issues, obs_property_name(property), "type", "checkable-group value must be a boolean");
}

using PropertyValidator = void (*)(obs_property_t *, obs_data_item_t *, obs_data_array_t *);

struct PropertyValidatorEntry {
	enum obs_property_type type;
	PropertyValidator validator;
};

constexpr PropertyValidatorEntry kPropertyValidators[] = {
	{OBS_PROPERTY_BOOL, &validate_bool_property},
	{OBS_PROPERTY_INT, &validate_int_property},
	{OBS_PROPERTY_FLOAT, &validate_float_property},
	{OBS_PROPERTY_TEXT, &validate_string_property},
	{OBS_PROPERTY_PATH, &validate_string_property},
	{OBS_PROPERTY_LIST, &validate_list_property},
	{OBS_PROPERTY_COLOR, &validate_color_property},
	{OBS_PROPERTY_COLOR_ALPHA, &validate_color_property},
	{OBS_PROPERTY_FONT, &validate_object_property},
	{OBS_PROPERTY_EDITABLE_LIST, &validate_array_property},
	{OBS_PROPERTY_GROUP, &validate_group_property},
};

void validate_property_item(obs_property_t *property, obs_data_item_t *item, obs_data_array_t *issues)
{
	const enum obs_property_type type = obs_property_get_type(property);
	for (const PropertyValidatorEntry &entry : kPropertyValidators) {
		if (entry.type == type) {
			entry.validator(property, item, issues);
			return;
		}
	}
}

std::string schema_fingerprint(obs_properties_t *properties, obs_data_t *settings)
{
	ObsDataPtr wrapper(obs_data_create());
	ObsArrayPtr schema = serialize_properties(properties, settings);
	obs_data_set_array(wrapper.get(), "properties", schema.get());
	const char *json = obs_data_get_json(wrapper.get());
	return json ? json : "{}";
}

} // namespace

ObsDataPtr clone_property_settings(obs_data_t *settings)
{
	if (!settings)
		return ObsDataPtr(obs_data_create());
	const char *json = obs_data_get_json_with_defaults(settings);
	ObsDataPtr copy(obs_data_create_from_json(json ? json : "{}"));
	if (!copy)
		copy.reset(obs_data_create());
	return copy;
}

ObsDataPtr sanitize_property_settings(obs_properties_t *properties, obs_data_t *settings)
{
	ObsDataPtr sanitized = clone_property_settings(settings);
	if (properties && sanitized)
		erase_password_settings(properties, sanitized.get());
	return sanitized;
}

ObsArrayPtr serialize_properties(obs_properties_t *properties, obs_data_t *settings, const char *refresh_property)
{
	if (!properties)
		return ObsArrayPtr(obs_data_array_create());
	return serialize_property_array(properties, settings, refresh_property);
}

ObsArrayPtr serialize_property_list_items(obs_property_t *property)
{
	ObsArrayPtr items(obs_data_array_create());
	if (!property || obs_property_get_type(property) != OBS_PROPERTY_LIST)
		return items;

	const enum obs_combo_format format = obs_property_list_format(property);
	const size_t count = obs_property_list_item_count(property);
	for (size_t index = 0; index < count; ++index) {
		ObsDataPtr item(obs_data_create());
		obs_data_set_string(item.get(), "name", safe_string(obs_property_list_item_name(property, index)));
		obs_data_set_bool(item.get(), "disabled", obs_property_list_item_disabled(property, index));
		switch (format) {
		case OBS_COMBO_FORMAT_STRING:
			obs_data_set_string(item.get(), "value", safe_string(obs_property_list_item_string(property, index)));
			break;
		case OBS_COMBO_FORMAT_INT:
			obs_data_set_int(item.get(), "value", obs_property_list_item_int(property, index));
			break;
		case OBS_COMBO_FORMAT_FLOAT:
			obs_data_set_double(item.get(), "value", obs_property_list_item_float(property, index));
			break;
		case OBS_COMBO_FORMAT_BOOL:
			obs_data_set_bool(item.get(), "value", obs_property_list_item_bool(property, index));
			break;
		default:
			break;
		}
		obs_data_array_push_back(items.get(), item.get());
	}
	return items;
}

ObsArrayPtr validate_property_patch(obs_properties_t *properties, obs_data_t *candidate)
{
	ObsArrayPtr issues(obs_data_array_create());
	if (!properties || !candidate)
		return issues;

	obs_data_item_t *item = obs_data_first(candidate);
	while (item) {
		const char *name = obs_data_item_get_name(item);
		obs_property_t *property = name ? obs_properties_get(properties, name) : nullptr;
		if (property)
			validate_property_item(property, item, issues.get());
		if (!obs_data_item_next(&item))
			break;
	}
	obs_data_item_release(&item);
	return issues;
}

bool resolve_property_schema(obs_properties_t *properties, obs_data_t *settings, const char *changed_property,
			     bool &requires_refresh)
{
	requires_refresh = false;
	if (!properties || !settings)
		return false;

	const std::string before = schema_fingerprint(properties, settings);
	bool callback_refresh = false;
	if (changed_property && *changed_property) {
		obs_property_t *property = obs_properties_get(properties, changed_property);
		if (!property)
			return false;
		callback_refresh = obs_property_modified(property, settings);
	} else {
		obs_properties_apply_settings(properties, settings);
	}
	const std::string after = schema_fingerprint(properties, settings);
	requires_refresh = callback_refresh || before != after;
	return true;
}

bool invoke_property_button(obs_property_t *property, void *object, PropertyButtonResult &result)
{
	result = PropertyButtonResult{};
	if (!property || obs_property_get_type(property) != OBS_PROPERTY_BUTTON)
		return false;

	if (obs_property_button_type(property) == OBS_BUTTON_URL) {
		result.is_url = true;
		const char *url = obs_property_button_url(property);
		result.url = url ? url : "";
		return true;
	}

	result.invoked = true;
	result.requires_refresh = obs_property_button_clicked(property, object);
	return true;
}

} // namespace obs_engine
