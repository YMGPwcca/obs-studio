#include "runtime.hpp"

#include "properties.hpp"
#include "runtime_phase2_common.hpp"
#include "validation.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <util/bmem.h>

namespace obs_engine {
namespace {

constexpr size_t kMaxServiceKindBytes = 128;
constexpr size_t kMaxServiceNameBytes = 256;
constexpr size_t kMaxSecretNameBytes = 128;
constexpr size_t kMaxServiceResolutions = 1024;
constexpr size_t kMaxServiceCodecs = 128;

struct ObsPropertiesDeleter {
	void operator()(obs_properties_t *properties) const
	{
		if (properties)
			obs_properties_destroy(properties);
	}
};

using ObsPropertiesPtr = std::unique_ptr<obs_properties_t, ObsPropertiesDeleter>;

struct ServiceSettingsDocument {
	ObsDataPtr settings;
	ObsDataPtr secrets;
};

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "service operation failed";
	return false;
}

const char *module_load_state_name(enum obs_module_load_state state)
{
	switch (state) {
	case OBS_MODULE_ENABLED:
		return "enabled";
	case OBS_MODULE_MISSING:
		return "missing";
	case OBS_MODULE_DISABLED:
		return "disabled";
	case OBS_MODULE_DISABLED_SAFE:
		return "disabledSafe";
	case OBS_MODULE_FAILED_TO_OPEN:
		return "failedToOpen";
	case OBS_MODULE_FAILED_TO_INITIALIZE:
		return "failedToInitialize";
	default:
		return "invalid";
	}
}

bool service_kind_exists(std::string_view requested)
{
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_service_types(index, &kind); ++index) {
		if (kind && requested == kind)
			return true;
	}
	return false;
}

bool read_service_kind(obs_data_t *params, std::string &kind, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "kind", kind, present) || !present ||
	    !phase2_is_bounded_string(kind, kMaxServiceKindBytes) || !is_safe_identifier(kind.c_str(), kMaxServiceKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid service kind identifier");
	if (!service_kind_exists(kind))
		return fail(error, "not_found", "service kind is not registered");
	return true;
}

ObsDataPtr make_service_kind_data(const char *kind)
{
	const enum obs_module_load_state load_state = obs_service_load_state(kind);
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "id", kind);
	obs_data_set_string(data.get(), "displayName",
			   obs_service_get_display_name(kind) ? obs_service_get_display_name(kind) : kind);
	obs_data_set_int(data.get(), "moduleLoadState", static_cast<long long>(load_state));
	obs_data_set_string(data.get(), "moduleLoadStateName", module_load_state_name(load_state));
	obs_data_set_bool(data.get(), "registered", true);
	obs_data_set_bool(data.get(), "moduleLoaded", load_state == OBS_MODULE_ENABLED);
	obs_module_t *module = obs_service_get_module(kind);
	if (module) {
		const char *file = obs_get_module_file_name(module);
		if (file)
			obs_data_set_string(data.get(), "module", file);
	}
	return data;
}

bool value_is_present(obs_data_t *settings, const char *name)
{
	if (!settings || !name)
		return false;
	const char *value = obs_data_get_string(settings, name);
	return value && *value;
}

void set_secret_metadata(obs_data_t *secrets, const char *name, bool set)
{
	ObsDataPtr metadata(obs_data_create());
	obs_data_set_bool(metadata.get(), "set", set);
	obs_data_set_obj(secrets, name, metadata.get());
}

void redact_known_service_fields(obs_data_t *settings)
{
	static constexpr std::array<const char *, 9> names = {"key", "stream_key", "stream_id", "password",
									  "username", "passphrase", "encrypt_passphrase",
									  "bearer_token", "token"};
	for (const char *name : names)
		obs_data_erase(settings, name);
}

void collect_property_secrets(obs_data_t *secrets, obs_properties_t *properties, obs_data_t *settings)
{
	for (const std::string &name : collect_sensitive_property_names(properties))
		set_secret_metadata(secrets, name.c_str(), value_is_present(settings, name.c_str()));
}

void collect_connect_secrets(obs_data_t *secrets, obs_service_t *service)
{
	struct Probe {
		const char *name;
		uint32_t type;
	};
	static constexpr Probe probes[] = {
		{"streamKey", OBS_SERVICE_CONNECT_INFO_STREAM_KEY},
		{"username", OBS_SERVICE_CONNECT_INFO_USERNAME},
		{"password", OBS_SERVICE_CONNECT_INFO_PASSWORD},
		{"encryptPassphrase", OBS_SERVICE_CONNECT_INFO_ENCRYPT_PASSPHRASE},
		{"bearerToken", OBS_SERVICE_CONNECT_INFO_BEARER_TOKEN},
	};
	for (const Probe &probe : probes) {
		const char *value = obs_service_get_connect_info(service, probe.type);
		set_secret_metadata(secrets, probe.name, value && *value);
	}
}

ServiceSettingsDocument read_service_settings(obs_service_t *service)
{
	ServiceSettingsDocument document;
	ObsDataPtr settings(obs_service_get_settings(service));
	ObsPropertiesPtr properties(obs_service_properties(service));
	document.settings = sanitize_property_settings(properties.get(), settings.get());
	document.secrets.reset(obs_data_create());
	redact_known_service_fields(document.settings.get());
	collect_property_secrets(document.secrets.get(), properties.get(), settings.get());
	collect_connect_secrets(document.secrets.get(), service);
	return document;
}

ObsDataPtr make_service_summary(uint64_t handle, const ServiceEntry &entry)
{
	obs_service_t *service = entry.service;
	ServiceSettingsDocument document = read_service_settings(service);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	obs_data_set_string(data.get(), "name", obs_service_get_name(service) ? obs_service_get_name(service) : "");
	obs_data_set_string(data.get(), "kind", obs_service_get_id(service) ? obs_service_get_id(service) : "");
	obs_data_set_bool(data.get(), "initialized", obs_service_initialized(service));
	obs_data_set_bool(data.get(), "active", obs_service_active(service));
	if (entry.bound_output)
		phase2_set_handle(data.get(), "boundOutput", entry.bound_output);
	else
		obs_data_set_obj(data.get(), "boundOutput", nullptr);
	obs_data_set_obj(data.get(), "settings", document.settings.get());
	obs_data_set_obj(data.get(), "secrets", document.secrets.get());
	return data;
}

ObsDataPtr make_service_settings_result(uint64_t handle, const ServiceEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	ServiceSettingsDocument document = read_service_settings(entry.service);
	obs_data_set_obj(data.get(), "settings", document.settings.get());
	obs_data_set_obj(data.get(), "secrets", document.secrets.get());
	return data;
}

bool read_service_name_and_settings(obs_data_t *params, uint64_t handle, std::string &name, ObsDataPtr &settings,
					    RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (!present)
		name = "engine-service-" + std::to_string(handle);
	if (!phase2_is_bounded_string(name, kMaxServiceNameBytes))
		return fail(error, "bad_request", "params.name must be a non-empty service name of at most 256 bytes");
	if (!phase2_read_object(params, "settings", settings, present))
		return fail(error, "bad_request", "params.settings must be an object when present");
	return true;
}

bool read_clear_secrets(obs_data_t *params, std::vector<std::string> &names, RuntimeV2Error &error)
{
	names.clear();
	ObsArrayPtr requested;
	bool present = false;
	if (!phase2_read_array(params, "clearSecrets", requested, present))
		return fail(error, "bad_request", "params.clearSecrets must be an array when present");
	if (!present)
		return true;
	const size_t count = obs_data_array_count(requested.get());
	if (count > 64)
		return fail(error, "bad_request", "params.clearSecrets is limited to 64 names");
	for (size_t index = 0; index < count; ++index) {
		ObsDataPtr item(obs_data_array_item(requested.get(), index));
		std::string name;
		bool item_present = false;
		if (!item || !phase2_read_string(item.get(), "name", name, item_present) || !item_present ||
		    !phase2_is_bounded_string(name, kMaxSecretNameBytes))
			return fail(error, "bad_request", "each clearSecrets entry must be an object with a bounded name");
		if (std::find(names.begin(), names.end(), name) == names.end())
			names.push_back(std::move(name));
	}
	return true;
}

void erase_requested_secrets(obs_data_t *settings, const std::vector<std::string> &names)
{
	for (const std::string &name : names)
		obs_data_erase(settings, name.c_str());
}

ObsDataPtr make_service_candidate(obs_data_t *current, obs_data_t *requested, bool replace,
					  const std::vector<std::string> &clears)
{
	ObsDataPtr candidate = replace ? phase2_clone_data(requested) : clone_property_settings(current);
	if (!candidate)
		candidate.reset(obs_data_create());
	if (!replace)
		obs_data_apply(candidate.get(), requested);
	erase_requested_secrets(candidate.get(), clears);
	return candidate;
}

bool validate_service_candidate(obs_service_t *service, obs_data_t *candidate, RuntimeV2Error &error)
{
	ObsPropertiesPtr properties(obs_service_properties(service));
	if (!properties)
		return true;
	ObsArrayPtr issues = validate_property_patch(properties.get(), candidate);
	if (obs_data_array_count(issues.get()) != 0)
		return fail(error, "bad_request", "service settings failed property validation");
	return true;
}

bool service_settings_equal(obs_data_t *left, obs_data_t *right)
{
	const char *left_json = left ? obs_data_get_json(left) : nullptr;
	const char *right_json = right ? obs_data_get_json(right) : nullptr;
	return left_json && right_json && std::strcmp(left_json, right_json) == 0;
}

void apply_service_settings(ServiceEntry &entry, obs_data_t *requested, obs_data_t *candidate, bool replace,
				    const std::vector<std::string> &clears)
{
	ObsDataPtr live(obs_service_get_settings(entry.service));
	if (replace) {
		obs_data_clear(live.get());
		obs_service_update(entry.service, candidate);
		return;
	}
	erase_requested_secrets(live.get(), clears);
	obs_service_update(entry.service, requested);
}

ObsDataPtr make_codec_array(const char **codecs)
{
	ObsArrayPtr array(obs_data_array_create());
	if (!codecs)
		return ObsDataPtr(obs_data_create());
	for (size_t index = 0; index < kMaxServiceCodecs; ++index) {
		const char *codec = codecs[index];
		if (!codec)
			break;
		if (!phase2_is_bounded_string(codec, kMaxServiceKindBytes))
			break;
		ObsDataPtr item(obs_data_create());
		obs_data_set_string(item.get(), "codec", codec);
		obs_data_array_push_back(array.get(), item.get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "codecs", array.get());
	return data;
}

ObsDataPtr make_service_info_result(uint64_t handle, const char *field, const char *value)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	if (value)
		obs_data_set_string(data.get(), field, value);
	else
		obs_data_set_obj(data.get(), field, nullptr);
	return data;
}

} // namespace

bool Engine::v2_get_service_entry(obs_data_t *params, uint64_t &handle, ServiceEntry *&entry,
					RuntimeV2Error &error) const
{
	if (!phase2_read_handle(params, "service", handle))
		return fail(error, "bad_request", "params.service must be a canonical decimal service handle string");
	const auto it = services_.find(handle);
	if (it == services_.end() || !it->second.service)
		return fail(error, "not_found", "service handle was not found");
	entry = const_cast<ServiceEntry *>(&it->second);
	return true;
}

void Engine::v2_prepare_service_shutdown() noexcept
{
	for (auto &[_, entry] : services_) {
		if (entry.service && obs_service_active(entry.service))
			std::fprintf(stderr, "obs-engine: active service retained until its Output owner stops it\n");
		if (entry.service)
			obs_service_release(entry.service);
		entry.service = nullptr;
	}
	services_.clear();
}

bool Engine::v2_service_kind_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsArrayPtr kinds(obs_data_array_create());
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_service_types(index, &kind); ++index)
		if (kind)
			obs_data_array_push_back(kinds.get(), make_service_kind_data(kind).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "kinds", kinds.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_service_kind(params, kind, error))
		return false;
	ObsDataPtr defaults(obs_service_defaults(kind.c_str()));
	if (!defaults)
		return fail(error, "obs_error", "service kind did not provide defaults");
	ObsPropertiesPtr properties(obs_get_service_properties(kind.c_str()));
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	ObsDataPtr safe_defaults = sanitize_property_settings(properties.get(), defaults.get());
	ObsDataPtr secrets(obs_data_create());
	collect_property_secrets(secrets.get(), properties.get(), defaults.get());
	redact_known_service_fields(safe_defaults.get());
	obs_data_set_obj(data.get(), "settings", safe_defaults.get());
	obs_data_set_obj(data.get(), "secrets", secrets.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_service_kind(params, kind, error))
		return false;
	ObsPropertiesPtr properties(obs_get_service_properties(kind.c_str()));
	ObsDataPtr defaults(obs_service_defaults(kind.c_str()));
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), defaults.get()).get());
	obs_data_set_array(data.get(), "properties", serialize_properties(properties.get(), defaults.get()).get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  properties && (obs_properties_get_flags(properties.get()) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<uint64_t> handles;
	for (const auto &[handle, _] : services_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr values(obs_data_array_create());
	for (const uint64_t handle : handles)
		obs_data_array_push_back(values.get(), make_service_summary(handle, services_.at(handle)).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "services", values.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	result.data = make_service_summary(handle, *entry);
	return true;
}

bool Engine::v2_service_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_service_kind(params, kind, error))
		return false;
	const uint64_t handle = allocate_handle();
	std::string name;
	ObsDataPtr settings;
	if (!read_service_name_and_settings(params, handle, name, settings, error))
		return false;
	if (!settings)
		settings.reset(obs_service_defaults(kind.c_str()));
	if (!settings)
		settings.reset(obs_data_create());
	obs_service_t *service = obs_service_create(kind.c_str(), name.c_str(), settings.get(), nullptr);
	if (!service)
		return fail(error, "obs_error", "libobs service creation failed");
	if (!obs_service_initialized(service)) {
		obs_service_release(service);
		return fail(error, "obs_error", "service plugin context initialization failed");
	}
	ServiceEntry entry;
	entry.service = service;
	try {
		if (!services_.emplace(handle, std::move(entry)).second)
			throw std::runtime_error("service handle collision");
	} catch (...) {
		obs_service_release(service);
		throw;
	}
	result.data = make_service_summary(handle, services_.at(handle));
	phase2_append_event(result, "service.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_service_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	if (entry->bound_output || obs_service_active(entry->service))
		return fail(error, "object_in_use", "service is still bound to an Output or active");
	obs_service_release(entry->service);
	services_.erase(handle);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	result.data = std::move(data);
	phase2_append_event(result, "service.removed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_service_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	std::string name;
	bool present = false;
	if (!phase2_read_string(params, "name", name, present) || !present ||
	    !phase2_is_bounded_string(name, kMaxServiceNameBytes))
		return fail(error, "bad_request", "params.name must be a non-empty service name of at most 256 bytes");
	const std::string old_name = obs_service_get_name(entry->service) ? obs_service_get_name(entry->service) : "";
	if (old_name == name) {
		result.data = make_service_summary(handle, *entry);
		return true;
	}
	obs_service_set_name(entry->service, name.c_str());
	const char *actual = obs_service_get_name(entry->service);
	if (!actual || name != actual)
		return fail(error, "obs_error", "libobs did not accept the service name");
	result.data = make_service_summary(handle, *entry);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "service", handle);
	obs_data_set_string(event_data.get(), "name", actual);
	phase2_append_event(result, "service.renamed", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_service_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	result.data = make_service_settings_result(handle, *entry);
	return true;
}

bool update_service_settings(Engine &engine, ServiceEntry &entry, uint64_t handle, obs_data_t *requested,
				     bool replace, const std::vector<std::string> &clears, RuntimeV2Result &result,
				     RuntimeV2Error &error)
{
	ObsDataPtr current(obs_service_get_settings(entry.service));
	ObsDataPtr candidate = make_service_candidate(current.get(), requested, replace, clears);
	if (!validate_service_candidate(entry.service, candidate.get(), error))
		return false;
	if (service_settings_equal(current.get(), candidate.get())) {
		result.data = make_service_settings_result(handle, entry);
		return true;
	}
	apply_service_settings(entry, requested, candidate.get(), replace, clears);
	result.data = make_service_settings_result(handle, entry);
	phase2_append_event(result, "service.settingsChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_service_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsDataPtr requested;
	bool present = false;
	if (!phase2_read_object(params, "settings", requested, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	std::vector<std::string> clears;
	if (!read_clear_secrets(params, clears, error))
		return false;
	return update_service_settings(*this, *entry, handle, requested.get(), false, clears, result, error);
}

bool Engine::v2_service_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsDataPtr requested;
	bool present = false;
	if (!phase2_read_object(params, "settings", requested, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	std::vector<std::string> clears;
	if (!read_clear_secrets(params, clears, error))
		return false;
	return update_service_settings(*this, *entry, handle, requested.get(), true, clears, result, error);
}

bool Engine::v2_service_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsPropertiesPtr properties(obs_service_properties(entry->service));
	ObsDataPtr settings(obs_service_get_settings(entry->service));
	ServiceSettingsDocument document = read_service_settings(entry->service);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	obs_data_set_obj(data.get(), "settings", document.settings.get());
	obs_data_set_obj(data.get(), "secrets", document.secrets.get());
	obs_data_set_array(data.get(), "properties", serialize_properties(properties.get(), settings.get()).get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  properties && (obs_properties_get_flags(properties.get()) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get_protocol(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	const char *protocol = obs_service_get_protocol(entry->service);
	if (!protocol)
		return fail(error, "not_available", "service does not expose a protocol");
	result.data = make_service_info_result(handle, "protocol", protocol);
	return true;
}

bool Engine::v2_service_get_preferred_output_kind(obs_data_t *params, RuntimeV2Result &result,
							 RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	result.data = make_service_info_result(handle, "outputKind", obs_service_get_preferred_output_type(entry->service));
	return true;
}

bool Engine::v2_service_get_supported_resolutions(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	obs_service_resolution *resolutions = nullptr;
	size_t count = 0;
	obs_service_get_supported_resolutions(entry->service, &resolutions, &count);
	ObsArrayPtr values(obs_data_array_create());
	const size_t limit = std::min(count, kMaxServiceResolutions);
	for (size_t index = 0; index < limit; ++index) {
		if (resolutions[index].cx <= 0 || resolutions[index].cy <= 0)
			continue;
		ObsDataPtr value(obs_data_create());
		obs_data_set_int(value.get(), "width", resolutions[index].cx);
		obs_data_set_int(value.get(), "height", resolutions[index].cy);
		obs_data_array_push_back(values.get(), value.get());
	}
	if (resolutions)
		bfree(resolutions);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	obs_data_set_array(data.get(), "resolutions", values.get());
	obs_data_set_bool(data.get(), "truncated", count > limit);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get_max_fps(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	int fps = 0;
	obs_service_get_max_fps(entry->service, &fps);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	obs_data_set_int(data.get(), "maxFps", std::max(fps, 0));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get_max_bitrates(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	int video = 0;
	int audio = 0;
	obs_service_get_max_bitrate(entry->service, &video, &audio);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	obs_data_set_int(data.get(), "video", std::max(video, 0));
	obs_data_set_int(data.get(), "audio", std::max(audio, 0));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get_supported_video_codecs(obs_data_t *params, RuntimeV2Result &result,
							   RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data = make_codec_array(obs_service_get_supported_video_codecs(entry->service));
	phase2_set_handle(data.get(), "service", handle);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get_supported_audio_codecs(obs_data_t *params, RuntimeV2Result &result,
							  RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data = make_codec_array(obs_service_get_supported_audio_codecs(entry->service));
	phase2_set_handle(data.get(), "service", handle);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_get_encoder_recommendations(obs_data_t *params, RuntimeV2Result &result,
							    RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsDataPtr video;
	ObsDataPtr audio;
	bool video_present = false;
	bool audio_present = false;
	if (!phase2_read_object(params, "videoSettings", video, video_present) ||
	    !phase2_read_object(params, "audioSettings", audio, audio_present))
		return fail(error, "bad_request", "candidate encoder settings must be objects when present");
	if (video_present)
		video = clone_property_settings(video.get());
	if (audio_present)
		audio = clone_property_settings(audio.get());
	obs_service_apply_encoder_settings(entry->service, video.get(), audio.get());
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	if (video)
		obs_data_set_obj(data.get(), "videoSettings", video.get());
	if (audio)
		obs_data_set_obj(data.get(), "audioSettings", audio.get());
	obs_data_set_bool(data.get(), "liveEncodersMutated", false);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_service_can_connect(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	ServiceEntry *entry = nullptr;
	if (!v2_get_service_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "service", handle);
	obs_data_set_bool(data.get(), "canConnect", obs_service_can_try_to_connect(entry->service));
	result.data = std::move(data);
	return true;
}

} // namespace obs_engine
