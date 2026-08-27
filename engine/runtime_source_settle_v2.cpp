#include "runtime.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace obs_engine {
namespace {

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

bool read_source_handle(obs_data_t *params, uint64_t &handle)
{
	if (!params)
		return false;
	obs_data_item_t *item = obs_data_item_byname(params, "source");
	if (!item)
		return false;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	const bool parsed = value && parse_handle_text(value, handle);
	obs_data_item_release(&item);
	return parsed;
}

void canonicalize_source_result(RuntimeV2Result &result, obs_source_t *source)
{
	if (!result.data || !source)
		return;

	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings)
		return;

	obs_data_item_t *settings_item = obs_data_item_byname(result.data.get(), "settings");
	if (settings_item) {
		if (obs_data_item_gettype(settings_item) == OBS_DATA_OBJECT)
			obs_data_set_obj(result.data.get(), "settings", settings.get());
		obs_data_item_release(&settings_item);
	}

	obs_data_item_t *state_item = obs_data_item_byname(result.data.get(), "state");
	if (!state_item)
		return;
	if (obs_data_item_gettype(state_item) == OBS_DATA_OBJECT) {
		ObsDataPtr state(obs_data_item_get_obj(state_item));
		if (state) {
			obs_data_set_string(state.get(), "kind", obs_source_get_id(source));
			obs_data_set_string(state.get(), "name", obs_source_get_name(source));
			obs_data_set_obj(state.get(), "settings", settings.get());
		}
	}
	obs_data_item_release(&state_item);
}

void set_semantic_source_flags(obs_data_t *data, uint32_t flags)
{
	obs_data_set_int(data, "outputFlags", static_cast<long long>(flags));
	obs_data_set_bool(data, "hasVideo", (flags & OBS_SOURCE_VIDEO) != 0);
	obs_data_set_bool(data, "hasAudio", (flags & OBS_SOURCE_AUDIO) != 0);
	obs_data_set_bool(data, "asyncVideo", (flags & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO);
	obs_data_set_bool(data, "customDraw", (flags & OBS_SOURCE_CUSTOM_DRAW) != 0);
	obs_data_set_bool(data, "interaction", (flags & OBS_SOURCE_INTERACTION) != 0);
	obs_data_set_bool(data, "composite", (flags & OBS_SOURCE_COMPOSITE) != 0);
	obs_data_set_bool(data, "doNotDuplicate", (flags & OBS_SOURCE_DO_NOT_DUPLICATE) != 0);
	obs_data_set_bool(data, "deprecated", (flags & OBS_SOURCE_DEPRECATED) != 0);
	obs_data_set_bool(data, "selfMonitorAllowed", (flags & OBS_SOURCE_DO_NOT_SELF_MONITOR) == 0);
	obs_data_set_bool(data, "disabled", (flags & OBS_SOURCE_CAP_DISABLED) != 0);
	obs_data_set_bool(data, "monitorByDefault", (flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0);
	obs_data_set_bool(data, "controllableMedia", (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0);
	obs_data_set_bool(data, "cea708", (flags & OBS_SOURCE_CEA_708) != 0);
	obs_data_set_bool(data, "srgb", (flags & OBS_SOURCE_SRGB) != 0);
	obs_data_set_bool(data, "dontShowPropertiesOnCreate", (flags & OBS_SOURCE_CAP_DONT_SHOW_PROPERTIES) != 0);
	obs_data_set_bool(data, "requiresCanvas", (flags & OBS_SOURCE_REQUIRES_CANVAS) != 0);
}

const char *find_unversioned_input_id(const char *kind)
{
	if (!kind)
		return nullptr;
	const char *id = nullptr;
	const char *unversioned_id = nullptr;
	for (size_t index = 0; obs_enum_input_types2(index, &id, &unversioned_id); ++index) {
		if (id && std::string_view(id) == kind)
			return unversioned_id ? unversioned_id : kind;
	}
	return kind;
}

void normalize_kind_entry(obs_data_t *entry)
{
	if (!entry)
		return;
	const char *kind = obs_data_get_string(entry, "id");
	if (!kind || !*kind)
		return;
	obs_data_set_string(entry, "unversionedId", find_unversioned_input_id(kind));
	const char *display_name = obs_source_get_display_name(kind);
	obs_data_set_string(entry, "displayName", display_name ? display_name : kind);
	set_semantic_source_flags(entry, obs_get_source_output_flags(kind));
	obs_module_t *module = obs_source_get_module(kind);
	if (module) {
		const char *module_file = obs_get_module_file_name(module);
		if (module_file)
			obs_data_set_string(entry, "module", module_file);
	}
	obs_data_set_int(entry, "moduleLoadState", static_cast<long long>(obs_source_load_state(kind)));
}

} // namespace

void Engine::v2_settle_source_mutation(obs_data_t *params, RuntimeV2Result &result)
{
	uint64_t handle = 0;
	if (!read_source_handle(params, handle))
		return;
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return;

	obs_source_t *source = it->second;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0) {
		const uint64_t fps = std::max<uint32_t>(config_.fps, 1u);
		const uint64_t frame_ms = (1000u + fps - 1u) / fps;
		const uint64_t settle_ms = std::clamp<uint64_t>(frame_ms * 4u + 100u, 100u, 5000u);
		std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
	}

	canonicalize_source_result(result, source);
}

void Engine::v2_normalize_source_kind_metadata(RuntimeV2Result &result)
{
	if (!result.data)
		return;

	obs_data_item_t *kinds_item = obs_data_item_byname(result.data.get(), "kinds");
	if (kinds_item) {
		if (obs_data_item_gettype(kinds_item) == OBS_DATA_ARRAY) {
			ObsArrayPtr kinds(obs_data_item_get_array(kinds_item));
			if (kinds) {
				const size_t count = obs_data_array_count(kinds.get());
				for (size_t index = 0; index < count; ++index) {
					ObsDataPtr entry(obs_data_array_item(kinds.get(), index));
					normalize_kind_entry(entry.get());
				}
			}
		}
		obs_data_item_release(&kinds_item);
		return;
	}

	normalize_kind_entry(result.data.get());
}

} // namespace obs_engine
