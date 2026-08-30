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

bool check_property(bool condition, const char *message)
{
	if (!condition) {
		fail(message);
		return false;
	}
	return true;
}

struct PropertyFixture {
	obs_properties_t *props = nullptr;
	obs_property_t *dependent = nullptr;
	obs_property_t *url_button = nullptr;
	int button_count = 0;
	obs_engine::ObsDataPtr settings;
	obs_engine::ObsArrayPtr schema;
	obs_engine::SensitivePropertyNames sensitive_before;

	PropertyFixture() = default;

	~PropertyFixture()
	{
		if (props)
			obs_properties_destroy(props);
	}

	PropertyFixture(const PropertyFixture &) = delete;
	PropertyFixture &operator=(const PropertyFixture &) = delete;
};

bool setup_fixture(PropertyFixture &fixture)
{
	fixture.props = obs_properties_create();
	if (!fixture.props)
		return false;

	obs_properties_set_flags(fixture.props, OBS_PROPERTIES_DEFER_UPDATE);
	obs_properties_add_bool(fixture.props, "enabled", "Enabled");
	obs_properties_add_int_slider(fixture.props, "count", "Count", 0, 100, 1);
	obs_properties_add_float(fixture.props, "gain", "Gain", -10.0, 10.0, 0.5);
	obs_property_t *password = obs_properties_add_text(fixture.props, "secret", "Secret", OBS_TEXT_PASSWORD);
	obs_property_text_set_monospace(password, true);
	obs_properties_add_path(fixture.props, "file", "File", OBS_PATH_FILE, "Images (*.png)", "C:/");

	obs_property_t *mode =
		obs_properties_add_list(fixture.props, "mode", "Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, "Alpha", "a");
	obs_property_list_add_string(mode, "Beta", "b");
	obs_property_list_item_disable(mode, 1, true);

	obs_properties_add_color(fixture.props, "color", "Color");
	obs_properties_add_color_alpha(fixture.props, "color_alpha", "Color Alpha");
	obs_properties_add_font(fixture.props, "font", "Font");
	obs_properties_add_editable_list(fixture.props, "files", "Files", OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS,
						 "Images (*.png)", "C:/");

	obs_property_t *frame_rate = obs_properties_add_frame_rate(fixture.props, "fps", "Frame rate");
	obs_property_frame_rate_option_add(frame_rate, "match", "Match source");
	media_frames_per_second min_fps{24, 1};
	media_frames_per_second max_fps{60, 1};
	obs_property_frame_rate_fps_range_add(frame_rate, min_fps, max_fps);

	obs_properties_t *group_content = obs_properties_create();
	obs_properties_add_bool(group_content, "group_child", "Group child");
	obs_properties_add_group(fixture.props, "advanced", "Advanced", OBS_GROUP_CHECKABLE, group_content);

	obs_property_t *dynamic_toggle = obs_properties_add_bool(fixture.props, "dynamic_toggle", "Dynamic toggle");
	fixture.dependent = obs_properties_add_text(fixture.props, "dependent", "Dependent", OBS_TEXT_DEFAULT);
	obs_property_set_visible(fixture.dependent, false);
	obs_property_set_modified_callback2(dynamic_toggle, toggle_modified, nullptr);

	obs_properties_add_button2(fixture.props, "do_it", "Do it", button_clicked, &fixture.button_count);
	fixture.url_button = obs_properties_add_button2(fixture.props, "docs", "Docs", button_clicked,
								 &fixture.button_count);
	obs_property_button_set_type(fixture.url_button, OBS_BUTTON_URL);
	char url[] = "https://example.invalid/docs";
	obs_property_button_set_url(fixture.url_button, url);

	fixture.settings.reset(obs_data_create());
	obs_data_set_bool(fixture.settings.get(), "enabled", true);
	obs_data_set_int(fixture.settings.get(), "count", 5);
	obs_data_set_double(fixture.settings.get(), "gain", 1.5);
	obs_data_set_string(fixture.settings.get(), "secret", "super-secret");
	obs_data_set_string(fixture.settings.get(), "mode", "a");
	obs_data_set_bool(fixture.settings.get(), "dynamic_toggle", false);
	return true;
}

bool check_sanitized_settings(PropertyFixture &fixture)
{
	obs_engine::ObsDataPtr sanitized = obs_engine::sanitize_property_settings(fixture.props, fixture.settings.get());
	return check_property(sanitized && !obs_data_has_user_value(sanitized.get(), "secret"),
				      "password value was not redacted from returned settings");
}

bool build_schema(PropertyFixture &fixture)
{
	fixture.schema = obs_engine::serialize_properties(fixture.props, fixture.settings.get());
	return check_property(fixture.schema && obs_data_array_count(fixture.schema.get()) >= 10,
				      "schema serialization lost properties");
}

bool check_password_schema(PropertyFixture &fixture)
{
	auto password_schema = find_schema_entry(fixture.schema.get(), "secret");
	return check_property(password_schema && std::strcmp(obs_data_get_string(password_schema.get(), "textType"), "password") == 0 &&
				      obs_data_get_bool(password_schema.get(), "sensitive") &&
				      obs_data_get_bool(password_schema.get(), "hasValue"),
				      "password schema metadata was incorrect");
}

bool check_list_schema(PropertyFixture &fixture)
{
	auto list_schema = find_schema_entry(fixture.schema.get(), "mode");
	obs_data_array_t *schema_items = list_schema ? obs_data_get_array(list_schema.get(), "items") : nullptr;
	if (!schema_items || obs_data_array_count(schema_items) != 2) {
		if (schema_items)
			obs_data_array_release(schema_items);
		return check_property(false, "list items were not serialized for internal schema inspection");
	}
	obs_engine::ObsDataPtr disabled_item(obs_data_array_item(schema_items, 1));
	const bool disabled_ok = disabled_item && obs_data_get_bool(disabled_item.get(), "disabled") &&
					 std::strcmp(obs_data_get_string(disabled_item.get(), "value"), "b") == 0;
	obs_data_array_release(schema_items);
	return check_property(disabled_ok, "disabled list item metadata was incorrect");
}

bool check_frame_rate_endpoint(obs_data_t *value, long long numerator)
{
	return value && obs_data_get_int(value, "numerator") == numerator &&
	       obs_data_get_int(value, "denominator") == 1;
}

bool check_frame_rate_schema(PropertyFixture &fixture)
{
	auto fps_schema = find_schema_entry(fixture.schema.get(), "fps");
	obs_data_array_t *ranges = fps_schema ? obs_data_get_array(fps_schema.get(), "ranges") : nullptr;
	if (!ranges || obs_data_array_count(ranges) != 1) {
		if (ranges)
			obs_data_array_release(ranges);
		return check_property(false, "frame-rate range was not serialized");
	}
	obs_engine::ObsDataPtr range(obs_data_array_item(ranges, 0));
	obs_engine::ObsDataPtr min_value(range ? obs_data_get_obj(range.get(), "min") : nullptr);
	obs_engine::ObsDataPtr max_value(range ? obs_data_get_obj(range.get(), "max") : nullptr);
	const bool fps_ok = check_frame_rate_endpoint(min_value.get(), 24) &&
			    check_frame_rate_endpoint(max_value.get(), 60);
	obs_data_array_release(ranges);
	return check_property(fps_ok, "frame-rate rational metadata was incorrect");
}

bool check_group_schema(PropertyFixture &fixture)
{
	auto group_schema = find_schema_entry(fixture.schema.get(), "advanced");
	obs_data_array_t *children = group_schema ? obs_data_get_array(group_schema.get(), "children") : nullptr;
	const bool group_ok = children && obs_data_array_count(children) == 1;
	if (children)
		obs_data_array_release(children);
	return check_property(group_ok, "group children were not serialized");
}

bool check_validation(PropertyFixture &fixture)
{
	obs_engine::ObsDataPtr candidate(obs_data_create());
	obs_data_set_int(candidate.get(), "count", 101);
	obs_data_set_string(candidate.get(), "mode", "b");
	obs_engine::ObsArrayPtr issues = obs_engine::validate_property_patch(fixture.props, candidate.get());
	return check_property(issues && obs_data_array_count(issues.get()) >= 2,
				      "validation did not reject range and disabled-choice errors");
}

bool check_dynamic_properties(PropertyFixture &fixture)
{
	fixture.sensitive_before = obs_engine::collect_sensitive_property_names(fixture.props);
	if (!check_property(fixture.sensitive_before.size() == 1 && fixture.sensitive_before.front() == "secret",
				    "sensitive property discovery did not capture the password field"))
		return false;

	obs_data_set_bool(fixture.settings.get(), "dynamic_toggle", true);
	bool requires_refresh = false;
	if (!obs_engine::resolve_property_schema(fixture.props, fixture.settings.get(), "dynamic_toggle", requires_refresh) ||
	    !requires_refresh || !obs_property_visible(fixture.dependent) ||
	    obs_properties_get(fixture.props, "secret") != nullptr)
		return check_property(false, "modified callback did not refresh dynamic schema and remove the password property");

	obs_engine::ObsDataPtr post_callback =
		obs_engine::sanitize_property_settings(fixture.props, fixture.settings.get());
	if (!check_property(post_callback && obs_data_has_user_value(post_callback.get(), "secret"),
				    "test setup did not preserve the removed password value in working settings"))
		return false;
	obs_engine::redact_sensitive_property_names(post_callback.get(), fixture.sensitive_before);
	return check_property(!obs_data_has_user_value(post_callback.get(), "secret"),
				      "historical password redaction failed after the property disappeared from the dynamic schema");
}

bool check_default_button(PropertyFixture &fixture)
{
	obs_engine::PropertyButtonResult button_result;
	obs_property_t *default_button = obs_properties_get(fixture.props, "do_it");
	if (!obs_engine::invoke_property_button(default_button, nullptr, button_result) || !button_result.invoked ||
	    button_result.is_url || !button_result.requires_refresh || fixture.button_count != 1)
		return check_property(false, "default button callback did not execute correctly");
	return true;
}

bool check_url_button(PropertyFixture &fixture)
{
	obs_engine::PropertyButtonResult url_result;
	if (!obs_engine::invoke_property_button(fixture.url_button, nullptr, url_result) || url_result.invoked ||
	    !url_result.is_url || url_result.url != "https://example.invalid/docs" || fixture.button_count != 1)
		return check_property(false, "URL button was executed instead of being returned as metadata");
	return true;
}

} // namespace

int main()
{
	PropertyFixture fixture;
	if (!setup_fixture(fixture))
		return fail("could not create properties");
	using PropertyTest = bool (*)(PropertyFixture &);
	constexpr PropertyTest tests[] = {
		&check_sanitized_settings,
		&build_schema,
		&check_password_schema,
		&check_list_schema,
		&check_frame_rate_schema,
		&check_group_schema,
		&check_validation,
		&check_dynamic_properties,
		&check_default_button,
		&check_url_button,
	};
	for (PropertyTest test : tests) {
		if (!test(fixture))
			return 1;
	}
	std::puts("properties-test: passed");
	return 0;
}
