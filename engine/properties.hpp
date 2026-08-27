#pragma once

#include "protocol.hpp"

#include <obs-properties.h>

#include <string>
#include <vector>

namespace obs_engine {

struct PropertyButtonResult {
	bool invoked = false;
	bool is_url = false;
	bool requires_refresh = false;
	std::string url;
};

using SensitivePropertyNames = std::vector<std::string>;

ObsDataPtr clone_property_settings(obs_data_t *settings);
ObsDataPtr sanitize_property_settings(obs_properties_t *properties, obs_data_t *settings);
SensitivePropertyNames collect_sensitive_property_names(obs_properties_t *properties);
void redact_sensitive_property_names(obs_data_t *settings, const SensitivePropertyNames &names);
ObsArrayPtr serialize_properties(obs_properties_t *properties, obs_data_t *settings,
				 const char *refresh_property = nullptr);
ObsArrayPtr serialize_property_list_items(obs_property_t *property);
ObsArrayPtr validate_property_patch(obs_properties_t *properties, obs_data_t *candidate);

bool resolve_property_schema(obs_properties_t *properties, obs_data_t *settings, const char *changed_property,
			     bool &requires_refresh);
bool invoke_property_button(obs_property_t *property, void *object, PropertyButtonResult &result);

} // namespace obs_engine
