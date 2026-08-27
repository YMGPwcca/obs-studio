#include "properties.hpp"

#include <obs-properties.h>

#include <cstdio>
#include <cstring>

namespace {

int fail(const char *message)
{
	std::fprintf(stderr, "properties-test: %s\n", message);
	return 1;
}

obs_engine::ObsDataPtr find_schema_entry(obs_data_array_t *schema, const char *name)
{
	const size_t count = obs_data_array_count(schema);
	for (size_t index = 0; index < count; ++index) {
		obs_engine::ObsDataPtr entry(obs_data_array_item(schema, index));
		if (entry && std::strcmp(obs_data_get_string(entry.get(), "name"), name) == 0)
			return entry;
	}
	return {};
}

bool toggle_modified(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool enabled = obs_data_get_bool(settings, "dynamic_toggle");
	obs_property_t *dependent = obs_properties_get(props, "dependent");
	if (dependent)
		obs_property_set_visible(dependent, enabled);
	if (enabled)
		obs_properties_remove_by_name(props, "secret");
	return true;
}

bool button_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	int *count = static_cast<int *>(data);
	if (count)
		++(*count);
	return true;
}

} // namespace

int main()
{
	obs_properties_t *props = obs_properties_create();
	if (!props)
		return fail("could not create properties");

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);
	obs_properties_add_bool(props, "enabled", "Enabled");
	obs_properties_add_int_slider(props, "count", "Count", 0, 100, 1);
	obs_properties_add_float(props, "gain", "Gain", -10.0, 10.0, 0.5);
	obs_property_t *password = obs_properties_add_text(props, "secret", "Secret", OBS_TEXT_PASSWORD);
	obs_property_text_set_monospace(password, true);
	obs_properties_add_path(props, "file", "File", OBS_PATH_FILE, "Images (*.png)", "C:/");

	obs_property_t *mode =
		obs_properties_add_list(props, "mode", "Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, "Alpha", "a");
	obs_property_list_add_string(mode, "Beta", "b");
	obs_property_list_item_disable(mode, 1, true);

	obs_properties_add_color(props, "color", "Color");
	obs_properties_add_color_alpha(props, "color_alpha", "Color Alpha");
	obs_properties_add_font(props, "font", "Font");
	obs_properties_add_editable_list(props, "files", "Files", OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS,
					 "Images (*.png)", "C:/");

	obs_property_t *frame_rate = obs_properties_add_frame_rate(props, "fps", "Frame rate");
	obs_property_frame_rate_option_add(frame_rate, "match", "Match source");
	media_frames_per_second min_fps{24, 1};
	media_frames_per_second max_fps{60, 1};
	obs_property_frame_rate_fps_range_add(frame_rate, min_fps, max_fps);

	obs_properties_t *group_content = obs_properties_create();
	obs_properties_add_bool(group_content, "group_child", "Group child");
	obs_properties_add_group(props, "advanced", "Advanced", OBS_GROUP_CHECKABLE, group_content);

	obs_property_t *dynamic_toggle = obs_properties_add_bool(props, "dynamic_toggle", "Dynamic toggle");
	obs_property_t *dependent = obs_properties_add_text(props, "dependent", "Dependent", OBS_TEXT_DEFAULT);
	obs_property_set_visible(dependent, false);
	obs_property_set_modified_callback2(dynamic_toggle, toggle_modified, nullptr);

	int button_count = 0;
	obs_properties_add_button2(props, "do_it", "Do it", button_clicked, &button_count);
	obs_property_t *url_button = obs_properties_add_button2(props, "docs", "Docs", button_clicked, &button_count);
	obs_property_button_set_type(url_button, OBS_BUTTON_URL);
	char url[] = "https://example.invalid/docs";
	obs_property_button_set_url(url_button, url);

	obs_engine::ObsDataPtr settings(obs_data_create());
	obs_data_set_bool(settings.get(), "enabled", true);
	obs_data_set_int(settings.get(), "count", 5);
	obs_data_set_double(settings.get(), "gain", 1.5);
	obs_data_set_string(settings.get(), "secret", "super-secret");
	obs_data_set_string(settings.get(), "mode", "a");
	obs_data_set_bool(settings.get(), "dynamic_toggle", false);

	obs_engine::ObsDataPtr sanitized = obs_engine::sanitize_property_settings(props, settings.get());
	if (!sanitized || obs_data_has_user_value(sanitized.get(), "secret")) {
		obs_properties_destroy(props);
		return fail("password value was not redacted from returned settings");
	}

	obs_engine::ObsArrayPtr schema = obs_engine::serialize_properties(props, settings.get());
	if (!schema || obs_data_array_count(schema.get()) < 10) {
		obs_properties_destroy(props);
		return fail("schema serialization lost properties");
	}

	auto password_schema = find_schema_entry(schema.get(), "secret");
	if (!password_schema || std::strcmp(obs_data_get_string(password_schema.get(), "textType"), "password") != 0 ||
	    !obs_data_get_bool(password_schema.get(), "sensitive") ||
	    !obs_data_get_bool(password_schema.get(), "hasValue")) {
		obs_properties_destroy(props);
		return fail("password schema metadata was incorrect");
	}

	auto list_schema = find_schema_entry(schema.get(), "mode");
	obs_data_array_t *schema_items = list_schema ? obs_data_get_array(list_schema.get(), "items") : nullptr;
	if (!schema_items || obs_data_array_count(schema_items) != 2) {
		if (schema_items)
			obs_data_array_release(schema_items);
		obs_properties_destroy(props);
		return fail("list items were not serialized for internal schema inspection");
	}
	obs_engine::ObsDataPtr disabled_item(obs_data_array_item(schema_items, 1));
	const bool disabled_ok = disabled_item && obs_data_get_bool(disabled_item.get(), "disabled") &&
				 std::strcmp(obs_data_get_string(disabled_item.get(), "value"), "b") == 0;
	obs_data_array_release(schema_items);
	if (!disabled_ok) {
		obs_properties_destroy(props);
		return fail("disabled list item metadata was incorrect");
	}

	auto fps_schema = find_schema_entry(schema.get(), "fps");
	obs_data_array_t *ranges = fps_schema ? obs_data_get_array(fps_schema.get(), "ranges") : nullptr;
	if (!ranges || obs_data_array_count(ranges) != 1) {
		if (ranges)
			obs_data_array_release(ranges);
		obs_properties_destroy(props);
		return fail("frame-rate range was not serialized");
	}
	obs_engine::ObsDataPtr range(obs_data_array_item(ranges, 0));
	obs_engine::ObsDataPtr min_value(range ? obs_data_get_obj(range.get(), "min") : nullptr);
	obs_engine::ObsDataPtr max_value(range ? obs_data_get_obj(range.get(), "max") : nullptr);
	const bool fps_ok = min_value && max_value && obs_data_get_int(min_value.get(), "numerator") == 24 &&
			    obs_data_get_int(min_value.get(), "denominator") == 1 &&
			    obs_data_get_int(max_value.get(), "numerator") == 60 &&
			    obs_data_get_int(max_value.get(), "denominator") == 1;
	obs_data_array_release(ranges);
	if (!fps_ok) {
		obs_properties_destroy(props);
		return fail("frame-rate rational metadata was incorrect");
	}

	auto group_schema = find_schema_entry(schema.get(), "advanced");
	obs_data_array_t *children = group_schema ? obs_data_get_array(group_schema.get(), "children") : nullptr;
	const bool group_ok = children && obs_data_array_count(children) == 1;
	if (children)
		obs_data_array_release(children);
	if (!group_ok) {
		obs_properties_destroy(props);
		return fail("group children were not serialized");
	}

	obs_engine::ObsDataPtr candidate(obs_data_create());
	obs_data_set_int(candidate.get(), "count", 101);
	obs_data_set_string(candidate.get(), "mode", "b");
	obs_engine::ObsArrayPtr issues = obs_engine::validate_property_patch(props, candidate.get());
	if (!issues || obs_data_array_count(issues.get()) < 2) {
		obs_properties_destroy(props);
		return fail("validation did not reject range and disabled-choice errors");
	}

	const obs_engine::SensitivePropertyNames sensitive_before = obs_engine::collect_sensitive_property_names(props);
	if (sensitive_before.size() != 1 || sensitive_before.front() != "secret") {
		obs_properties_destroy(props);
		return fail("sensitive property discovery did not capture the password field");
	}

	obs_data_set_bool(settings.get(), "dynamic_toggle", true);
	bool requires_refresh = false;
	if (!obs_engine::resolve_property_schema(props, settings.get(), "dynamic_toggle", requires_refresh) ||
	    !requires_refresh || !obs_property_visible(dependent) || obs_properties_get(props, "secret") != nullptr) {
		obs_properties_destroy(props);
		return fail("modified callback did not refresh dynamic schema and remove the password property");
	}

	obs_engine::ObsDataPtr post_callback = obs_engine::sanitize_property_settings(props, settings.get());
	if (!post_callback || !obs_data_has_user_value(post_callback.get(), "secret")) {
		obs_properties_destroy(props);
		return fail("test setup did not preserve the removed password value in working settings");
	}
	obs_engine::redact_sensitive_property_names(post_callback.get(), sensitive_before);
	if (obs_data_has_user_value(post_callback.get(), "secret")) {
		obs_properties_destroy(props);
		return fail("historical password redaction failed after the property disappeared from the dynamic schema");
	}

	obs_engine::PropertyButtonResult button_result;
	obs_property_t *default_button = obs_properties_get(props, "do_it");
	if (!obs_engine::invoke_property_button(default_button, nullptr, button_result) || !button_result.invoked ||
	    button_result.is_url || !button_result.requires_refresh || button_count != 1) {
		obs_properties_destroy(props);
		return fail("default button callback did not execute correctly");
	}

	obs_engine::PropertyButtonResult url_result;
	if (!obs_engine::invoke_property_button(url_button, nullptr, url_result) || url_result.invoked ||
	    !url_result.is_url || url_result.url != "https://example.invalid/docs" || button_count != 1) {
		obs_properties_destroy(props);
		return fail("URL button was executed instead of being returned as metadata");
	}

	obs_properties_destroy(props);
	std::puts("properties-test: passed");
	return 0;
}
