#include "properties.hpp"

namespace obs_engine {
namespace {

void collect_sensitive_recursive(obs_properties_t *properties, SensitivePropertyNames &names)
{
	if (!properties)
		return;
	for (obs_property_t *property = obs_properties_first(properties); property;) {
		const enum obs_property_type type = obs_property_get_type(property);
		if (type == OBS_PROPERTY_TEXT && obs_property_text_type(property) == OBS_TEXT_PASSWORD) {
			const char *name = obs_property_name(property);
			if (name && *name)
				names.emplace_back(name);
		} else if (type == OBS_PROPERTY_GROUP) {
			collect_sensitive_recursive(obs_property_group_content(property), names);
		}
		if (!obs_property_next(&property))
			break;
	}
}

} // namespace

SensitivePropertyNames collect_sensitive_property_names(obs_properties_t *properties)
{
	SensitivePropertyNames names;
	collect_sensitive_recursive(properties, names);
	return names;
}

void redact_sensitive_property_names(obs_data_t *settings, const SensitivePropertyNames &names)
{
	if (!settings)
		return;
	for (const std::string &name : names)
		obs_data_erase(settings, name.c_str());
}

} // namespace obs_engine
