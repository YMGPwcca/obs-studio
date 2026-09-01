#include "runtime.hpp"

#include "properties.hpp"
#include "validation.hpp"

#include <charconv>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace obs_engine {
namespace {

constexpr size_t kMaxPropertyTargetTypeBytes = 32;
constexpr size_t kMaxPropertyNameBytes = 512;
constexpr size_t kMaxSourceKindBytes = 128;

struct ObsPropertiesDeleter {
	void operator()(obs_properties_t *value) const
	{
		if (value)
			obs_properties_destroy(value);
	}
};

using ObsPropertiesPtr = std::unique_ptr<obs_properties_t, ObsPropertiesDeleter>;

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "properties operation failed";
	return false;
}

bool is_bounded_string(const std::string &value, size_t max_bytes)
{
	return !value.empty() && value.size() <= max_bytes;
}

bool filter_type_exists(const char *kind)
{
	const char *candidate = nullptr;
	for (size_t index = 0; obs_enum_filter_types(index, &candidate); ++index) {
		if (candidate && std::strcmp(candidate, kind) == 0)
			return true;
	}
	return false;
}

bool read_string_field(obs_data_t *data, const char *name, std::string &out, bool &present)
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

bool read_object_field(obs_data_t *data, const char *name, ObsDataPtr &out, bool &present)
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

bool parse_handle_text(std::string_view text, uint64_t &out)
{
	if (text.empty() || (text.size() > 1 && text.front() == '0'))
		return false;
	uint64_t value = 0;
	const char *begin = text.data();
	const char *end = begin + text.size();
	const auto parsed = std::from_chars(begin, end, value, 10);
	if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0 ||
	    value > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
		return false;
	out = value;
	return true;
}

bool read_handle_field(obs_data_t *data, const char *name, uint64_t &out)
{
	std::string text;
	bool present = false;
	if (!read_string_field(data, name, text, present) || !present)
		return false;
	return parse_handle_text(text, out);
}

void set_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	const std::string text = std::to_string(handle);
	obs_data_set_string(data, name, text.c_str());
}

void strip_inline_list_items(obs_data_array_t *schema)
{
	if (!schema)
		return;

	const size_t count = obs_data_array_count(schema);
	for (size_t index = 0; index < count; ++index) {
		ObsDataPtr entry(obs_data_array_item(schema, index));
		if (!entry)
			continue;
		const char *type = obs_data_get_string(entry.get(), "type");
		if (type && std::strcmp(type, "list") == 0) {
			obs_data_erase(entry.get(), "items");
		} else if (type && std::strcmp(type, "group") == 0) {
			obs_data_array_t *children = obs_data_get_array(entry.get(), "children");
			if (children) {
				strip_inline_list_items(children);
				obs_data_array_release(children);
			}
		}
	}
}

ObsDataPtr make_properties_document(obs_data_t *target, obs_properties_t *properties, obs_data_t *settings,
				    bool requires_refresh, const char *refresh_property = nullptr,
				    const SensitivePropertyNames *previous_sensitive = nullptr)
{
	ObsDataPtr data(obs_data_create());
	ObsDataPtr safe_settings = sanitize_property_settings(properties, settings);
	if (previous_sensitive)
		redact_sensitive_property_names(safe_settings.get(), *previous_sensitive);
	ObsArrayPtr schema = serialize_properties(properties, settings, requires_refresh ? refresh_property : nullptr);
	// Keep the base form compact for device/window/plugin lists that can contain
	// hundreds or thousands of entries. The full list remains part of the
	// internal schema fingerprint; callers retrieve choices via getListItems.
	strip_inline_list_items(schema.get());
	obs_data_set_obj(data.get(), "target", target);
	obs_data_set_obj(data.get(), "settings", safe_settings.get());
	obs_data_set_array(data.get(), "properties", schema.get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  (obs_properties_get_flags(properties) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	obs_data_set_bool(data.get(), "requiresRefresh", requires_refresh);
	return data;
}

bool read_candidate_params(obs_data_t *params, bool required, ObsDataPtr &candidate, bool &present,
			   std::string &changed_property, bool &changed_present, RuntimeV2Error &error)
{
	if (!read_object_field(params, "settings", candidate, present))
		return fail(error, "bad_request", "params.settings must be an object when present");
	if (required && !present)
		return fail(error, "bad_request", "params.settings object is required");
	if (!read_string_field(params, "changedProperty", changed_property, changed_present))
		return fail(error, "bad_request", "params.changedProperty must be a string when present");
	if (changed_present && !is_bounded_string(changed_property, kMaxPropertyNameBytes))
		return fail(error, "bad_request", "params.changedProperty must be a non-empty string of at most 512 bytes");
	if (changed_present && !present)
		return fail(error, "bad_request", "params.changedProperty requires params.settings");
	return true;
}

bool apply_candidate(obs_properties_t *properties, obs_data_t *working_settings, obs_data_t *candidate, bool present,
		     const std::string &changed_property, bool changed_present, bool &requires_refresh,
		     RuntimeV2Error &error)
{
	requires_refresh = false;
	if (!present)
		return true;
	obs_data_apply(working_settings, candidate);
	const char *changed = changed_present ? changed_property.c_str() : nullptr;
	if (!resolve_property_schema(properties, working_settings, changed, requires_refresh))
		return fail(error, "bad_request", "params.changedProperty does not identify a property in the target schema");
	return true;
}

bool read_property_name(obs_data_t *params, std::string &property, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "property", property, present) || !present ||
	    !is_bounded_string(property, kMaxPropertyNameBytes))
		return fail(error, "bad_request", "params.property must be a non-empty string of at most 512 bytes");
	return true;
}

bool read_property_target(obs_data_t *params, ObsDataPtr &target, std::string &type, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_object_field(params, "target", target, present) || !present)
		return fail(error, "bad_request", "params.target must be an object");
	if (!read_string_field(target.get(), "type", type, present) || !present ||
	    !is_safe_identifier(type.c_str(), kMaxPropertyTargetTypeBytes))
		return fail(error, "bad_request", "params.target.type must be a valid target identifier");
	return true;
}

bool validate_button_target(obs_property_t *property, obs_source_t *source, RuntimeV2Error &error)
{
	if (obs_property_get_type(property) != OBS_PROPERTY_BUTTON)
		return fail(error, "bad_request", "params.property does not identify a button property");
	if (!obs_property_visible(property) || !obs_property_enabled(property))
		return fail(error, "invalid_state", "button property is not currently visible and enabled");
	if (obs_property_button_type(property) != OBS_BUTTON_URL && !source)
		return fail(error, "not_available", "non-URL property buttons require a live source target in Task 7");
	return true;
}

bool finalize_property_target(ObsDataPtr &settings, ObsDataPtr &base_settings, obs_properties_t *&properties,
				     RuntimeV2Error &error)
{
	settings = clone_property_settings(base_settings.get());
	if (settings)
		return true;
	obs_properties_destroy(properties);
	properties = nullptr;
	return fail(error, "internal_error", "could not clone target settings for property resolution");
}

} // namespace

Engine::PropertyButtonContext::~PropertyButtonContext()
{
	if (properties)
		obs_properties_destroy(properties);
}

bool Engine::v2_build_source_property_target(obs_data_t *requested_target, ObsDataPtr &target,
					     ObsDataPtr &base_settings, obs_properties_t *&properties,
					     obs_source_t *&source, RuntimeV2Error &error)
{
	uint64_t handle = 0;
	if (!read_handle_field(requested_target, "source", handle))
		return fail(error, "bad_request", "source property targets require a canonical decimal target.source handle");
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return fail(error, "not_found", "source property target was not found");
	source = it->second;
	properties = obs_source_properties(source);
	if (!properties)
		return fail(error, "not_available", "source does not expose configurable libobs properties");
	base_settings.reset(obs_source_get_settings(source));
	target.reset(obs_data_create());
	obs_data_set_string(target.get(), "type", "source");
	set_handle(target.get(), "source", handle);
	return true;
}

bool Engine::v2_build_source_kind_property_target(obs_data_t *requested_target, ObsDataPtr &target,
						  ObsDataPtr &base_settings, obs_properties_t *&properties,
						  obs_source_t *&source, RuntimeV2Error &error)
{
	std::string kind;
	bool present = false;
	if (!read_string_field(requested_target, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "sourceKind property targets require a valid target.kind identifier");
	if (!input_type_exists(kind.c_str()))
		return fail(error, "not_found", "source kind property target is not registered");
	properties = obs_get_source_properties(kind.c_str());
	if (!properties)
		return fail(error, "not_available", "source kind does not expose configurable libobs properties");
	base_settings.reset(obs_get_source_defaults(kind.c_str()));
	target.reset(obs_data_create());
	obs_data_set_string(target.get(), "type", "sourceKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	source = nullptr;
	return true;
}

bool Engine::v2_build_filter_property_target(obs_data_t *requested_target, ObsDataPtr &target,
					     ObsDataPtr &base_settings, obs_properties_t *&properties,
					     obs_source_t *&source, RuntimeV2Error &error)
{
	uint64_t handle = 0;
	if (!read_handle_field(requested_target, "filter", handle))
		return fail(error, "bad_request", "filter property targets require a canonical decimal target.filter handle");
	auto it = filters_.find(handle);
	if (it == filters_.end())
		return fail(error, "not_found", "filter property target was not found");
	source = it->second.filter;
	properties = obs_source_properties(source);
	if (!properties)
		return fail(error, "not_available", "filter does not expose configurable libobs properties");
	base_settings.reset(obs_source_get_settings(source));
	target.reset(obs_data_create());
	obs_data_set_string(target.get(), "type", "filter");
	set_handle(target.get(), "filter", handle);
	set_handle(target.get(), "source", it->second.source_id);
	return true;
}

bool Engine::v2_build_filter_kind_property_target(obs_data_t *requested_target, ObsDataPtr &target,
						  ObsDataPtr &base_settings, obs_properties_t *&properties,
						  obs_source_t *&source, RuntimeV2Error &error)
{
	std::string kind;
	bool present = false;
	if (!read_string_field(requested_target, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "filterKind property targets require a valid target.kind identifier");
	if (!filter_type_exists(kind.c_str()))
		return fail(error, "not_found", "filter kind property target is not registered");
	properties = obs_get_source_properties(kind.c_str());
	if (!properties)
		return fail(error, "not_available", "filter kind does not expose configurable libobs properties");
	base_settings.reset(obs_get_source_defaults(kind.c_str()));
	target.reset(obs_data_create());
	obs_data_set_string(target.get(), "type", "filterKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	source = nullptr;
	return true;
}

bool transition_kind_exists(const char *kind)
{
	const char *candidate = nullptr;
	for (size_t index = 0; obs_enum_transition_types(index, &candidate); ++index) {
		if (candidate && std::strcmp(candidate, kind) == 0)
			return true;
	}
	return false;
}

bool Engine::v2_build_transition_property_target(obs_data_t *requested_target, ObsDataPtr &target,
							 ObsDataPtr &base_settings, obs_properties_t *&properties,
							 obs_source_t *&source, RuntimeV2Error &error)
{
	uint64_t handle = 0;
	if (!read_handle_field(requested_target, "transition", handle))
		return fail(error, "bad_request",
				   "transition property targets require a canonical decimal target.transition handle");
	const auto it = transitions_.find(handle);
	if (it == transitions_.end() || !it->second.transition)
		return fail(error, "not_found", "transition property target was not found");
	source = it->second.transition;
	properties = obs_source_properties(source);
	if (!properties)
		return fail(error, "not_available", "transition does not expose configurable libobs properties");
	base_settings.reset(obs_source_get_settings(source));
	target.reset(obs_data_create());
	obs_data_set_string(target.get(), "type", "transition");
	set_handle(target.get(), "transition", handle);
	return true;
}

bool Engine::v2_build_transition_kind_property_target(obs_data_t *requested_target, ObsDataPtr &target,
							     ObsDataPtr &base_settings, obs_properties_t *&properties,
							     obs_source_t *&source, RuntimeV2Error &error)
{
	std::string kind;
	bool present = false;
	if (!read_string_field(requested_target, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "transitionKind property targets require a valid target.kind identifier");
	if (!transition_kind_exists(kind.c_str()))
		return fail(error, "not_found", "transition kind property target is not registered");
	properties = obs_get_source_properties(kind.c_str());
	if (!properties)
		return fail(error, "not_available", "transition kind does not expose configurable libobs properties");
	base_settings.reset(obs_get_source_defaults(kind.c_str()));
	target.reset(obs_data_create());
	obs_data_set_string(target.get(), "type", "transitionKind");
	obs_data_set_string(target.get(), "kind", kind.c_str());
	source = nullptr;
	return true;
}

bool Engine::v2_prepare_property_button(obs_data_t *params, PropertyButtonContext &context, RuntimeV2Error &error)
{
	if (!v2_build_property_target(params, context.target, context.settings, context.properties, context.source, error))
		return false;
	context.previous_sensitive = collect_sensitive_property_names(context.properties);

	ObsDataPtr candidate;
	bool candidate_present = false;
	if (!read_candidate_params(params, false, candidate, candidate_present, context.changed_property,
					   context.changed_present, error))
		return false;
	if (!apply_candidate(context.properties, context.settings.get(), candidate.get(), candidate_present,
				     context.changed_property, context.changed_present, context.resolved_refresh, error))
		return false;
	if (!read_property_name(params, context.property_name, error))
		return false;
	context.property = obs_properties_get(context.properties, context.property_name.c_str());
	if (!context.property)
		return fail(error, "not_found", "button property was not found in the resolved target schema");
	return validate_button_target(context.property, context.source, error);
}

bool Engine::v2_build_property_target(obs_data_t *params, ObsDataPtr &target, ObsDataPtr &settings,
				      obs_properties_t *&properties, obs_source_t *&source, RuntimeV2Error &error)
{
	target.reset();
	settings.reset();
	properties = nullptr;
	source = nullptr;

	ObsDataPtr requested_target;
	std::string type;
	if (!read_property_target(params, requested_target, type, error))
		return false;

	using TargetBuilder = bool (Engine::*)(obs_data_t *, ObsDataPtr &, ObsDataPtr &, obs_properties_t *&,
						      obs_source_t *&, RuntimeV2Error &);
		struct TargetDescriptor {
			const char *type;
			TargetBuilder builder;
		};
		constexpr TargetDescriptor builders[] = {
			{"source", &Engine::v2_build_source_property_target},
			{"sourceKind", &Engine::v2_build_source_kind_property_target},
			{"filter", &Engine::v2_build_filter_property_target},
			{"filterKind", &Engine::v2_build_filter_kind_property_target},
			{"transition", &Engine::v2_build_transition_property_target},
			{"transitionKind", &Engine::v2_build_transition_kind_property_target},
		};
	TargetBuilder builder = nullptr;
	for (const TargetDescriptor &descriptor : builders) {
		if (type == descriptor.type) {
			builder = descriptor.builder;
			break;
		}
	}
	if (!builder)
		return fail(error, "unsupported_capability", "properties target type is not supported by the current engine");

	ObsDataPtr base_settings;
	if (!(this->*builder)(requested_target.get(), target, base_settings, properties, source, error))
		return false;

	return finalize_property_target(settings, base_settings, properties, error);
}

bool Engine::v2_properties_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr target;
	ObsDataPtr settings;
	obs_properties_t *raw_properties = nullptr;
	obs_source_t *source = nullptr;
	if (!v2_build_property_target(params, target, settings, raw_properties, source, error))
		return false;
	ObsPropertiesPtr properties(raw_properties);
	result.data = make_properties_document(target.get(), properties.get(), settings.get(), false);
	return true;
}

bool Engine::v2_properties_resolve(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr target;
	ObsDataPtr settings;
	obs_properties_t *raw_properties = nullptr;
	obs_source_t *source = nullptr;
	if (!v2_build_property_target(params, target, settings, raw_properties, source, error))
		return false;
	ObsPropertiesPtr properties(raw_properties);
	const SensitivePropertyNames previous_sensitive = collect_sensitive_property_names(properties.get());

	ObsDataPtr candidate;
	bool candidate_present = false;
	std::string changed_property;
	bool changed_present = false;
	if (!read_candidate_params(params, true, candidate, candidate_present, changed_property, changed_present, error))
		return false;

	bool requires_refresh = false;
	if (!apply_candidate(properties.get(), settings.get(), candidate.get(), candidate_present, changed_property,
			     changed_present, requires_refresh, error))
		return false;

	result.data = make_properties_document(target.get(), properties.get(), settings.get(), requires_refresh,
					       changed_present ? changed_property.c_str() : nullptr, &previous_sensitive);
	return true;
}

bool Engine::v2_properties_get_list_items(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr target;
	ObsDataPtr settings;
	obs_properties_t *raw_properties = nullptr;
	obs_source_t *source = nullptr;
	if (!v2_build_property_target(params, target, settings, raw_properties, source, error))
		return false;
	ObsPropertiesPtr properties(raw_properties);
	const SensitivePropertyNames previous_sensitive = collect_sensitive_property_names(properties.get());

	ObsDataPtr candidate;
	bool candidate_present = false;
	std::string changed_property;
	bool changed_present = false;
	if (!read_candidate_params(params, false, candidate, candidate_present, changed_property, changed_present, error))
		return false;
	bool requires_refresh = false;
	if (!apply_candidate(properties.get(), settings.get(), candidate.get(), candidate_present, changed_property,
			     changed_present, requires_refresh, error))
		return false;

	std::string property_name;
	if (!read_property_name(params, property_name, error))
		return false;
	obs_property_t *property = obs_properties_get(properties.get(), property_name.c_str());
	if (!property)
		return fail(error, "not_found", "property was not found in the resolved target schema");
	if (obs_property_get_type(property) != OBS_PROPERTY_LIST)
		return fail(error, "bad_request", "params.property does not identify a list property");

	result.data = make_properties_document(target.get(), properties.get(), settings.get(), requires_refresh,
					       changed_present ? changed_property.c_str() : nullptr, &previous_sensitive);
	ObsArrayPtr items = serialize_property_list_items(property);
	obs_data_set_string(result.data.get(), "property", property_name.c_str());
	obs_data_set_array(result.data.get(), "items", items.get());
	obs_data_set_int(result.data.get(), "itemCount", static_cast<long long>(obs_data_array_count(items.get())));
	return true;
}

bool Engine::v2_properties_validate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr target;
	ObsDataPtr settings;
	obs_properties_t *raw_properties = nullptr;
	obs_source_t *source = nullptr;
	if (!v2_build_property_target(params, target, settings, raw_properties, source, error))
		return false;
	ObsPropertiesPtr properties(raw_properties);
	const SensitivePropertyNames previous_sensitive = collect_sensitive_property_names(properties.get());

	ObsDataPtr candidate;
	bool candidate_present = false;
	std::string changed_property;
	bool changed_present = false;
	if (!read_candidate_params(params, true, candidate, candidate_present, changed_property, changed_present, error))
		return false;

	// Reject structurally invalid values before invoking arbitrary plugin
	// modification callbacks. If the initial shape is valid, resolve dynamic
	// properties and validate once more against the resulting form.
	ObsArrayPtr issues = validate_property_patch(properties.get(), candidate.get());
	bool requires_refresh = false;
	if (obs_data_array_count(issues.get()) == 0) {
		if (!apply_candidate(properties.get(), settings.get(), candidate.get(), candidate_present, changed_property,
				     changed_present, requires_refresh, error))
			return false;
		issues = validate_property_patch(properties.get(), candidate.get());
	} else {
		obs_data_apply(settings.get(), candidate.get());
	}

	result.data = make_properties_document(target.get(), properties.get(), settings.get(), requires_refresh,
					       changed_present ? changed_property.c_str() : nullptr, &previous_sensitive);
	obs_data_set_bool(result.data.get(), "valid", obs_data_array_count(issues.get()) == 0);
	obs_data_set_array(result.data.get(), "issues", issues.get());
	return true;
}

bool Engine::v2_properties_refresh(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr target;
	ObsDataPtr settings;
	obs_properties_t *raw_properties = nullptr;
	obs_source_t *source = nullptr;
	if (!v2_build_property_target(params, target, settings, raw_properties, source, error))
		return false;
	ObsPropertiesPtr properties(raw_properties);
	result.data = make_properties_document(target.get(), properties.get(), settings.get(), false);
	obs_data_set_bool(result.data.get(), "refreshed", true);
	return true;
}

bool Engine::v2_properties_invoke_button(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	PropertyButtonContext context;
	if (!v2_prepare_property_button(params, context, error))
		return false;
	ObsPropertiesPtr properties(context.properties);
	context.properties = nullptr;

	PropertyButtonResult button;
	if (!invoke_property_button(context.property, context.source, button))
		return fail(error, "internal_error", "property button dispatch failed");

	if (button.is_url) {
		result.data = make_properties_document(context.target.get(), properties.get(), context.settings.get(),
						       context.resolved_refresh,
						       context.changed_present ? context.changed_property.c_str() : nullptr,
						       &context.previous_sensitive);
		obs_data_set_string(result.data.get(), "property", context.property_name.c_str());
		obs_data_set_string(result.data.get(), "buttonType", "url");
		obs_data_set_bool(result.data.get(), "invoked", false);
		obs_data_set_string(result.data.get(), "url", button.url.c_str());
		return true;
	}

	// From this point onward the plugin callback has executed and may have
	// changed instance state. Do not introduce a recoverable failure path that
	// could lose revision accounting. Refresh opportunistically and fall back to
	// the already-valid form/settings if the plugin no longer exposes a new one.
	result.mutated = true;
	if (button.requires_refresh) {
		obs_properties_t *refreshed = obs_source_properties(context.source);
		if (refreshed)
			properties.reset(refreshed);
	}
	ObsDataPtr refreshed_settings(obs_source_get_settings(context.source));
	if (refreshed_settings)
		context.settings = clone_property_settings(refreshed_settings.get());

	result.data = make_properties_document(context.target.get(), properties.get(), context.settings.get(),
					       button.requires_refresh,
					       button.requires_refresh ? context.property_name.c_str() : nullptr,
					       &context.previous_sensitive);
	obs_data_set_string(result.data.get(), "property", context.property_name.c_str());
	obs_data_set_string(result.data.get(), "buttonType", "default");
	obs_data_set_bool(result.data.get(), "invoked", true);
	return true;
}

} // namespace obs_engine
