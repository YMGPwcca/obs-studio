#include "runtime.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

bool read_handle_field(obs_data_t *data, const char *name, uint64_t &out)
{
	if (!data)
		return false;
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item)
		return false;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	const bool parsed = value && parse_handle_text(value, out);
	obs_data_item_release(&item);
	return parsed;
}

void set_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	const std::string text = std::to_string(handle);
	obs_data_set_string(data, name, text.c_str());
}

ObsDataPtr make_filter_summary(uint64_t handle, const FilterEntry &entry, obs_source_t *parent)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "filter", handle);
	set_handle(data.get(), "source", entry.source_id);
	obs_data_set_string(data.get(), "name", obs_source_get_name(entry.filter));
	obs_data_set_string(data.get(), "kind", obs_source_get_id(entry.filter));
	obs_data_set_string(data.get(), "unversionedKind", obs_source_get_unversioned_id(entry.filter));
	obs_data_set_bool(data.get(), "enabled", obs_source_enabled(entry.filter));
	obs_data_set_int(data.get(), "index", static_cast<long long>(obs_source_filter_get_index(parent, entry.filter)));
	obs_data_set_int(data.get(), "outputFlags", static_cast<long long>(obs_source_get_output_flags(entry.filter)));
	return data;
}

} // namespace

void Engine::v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source, RuntimeV2Result *result,
					       uint64_t duplicate_of)
{
	(void)duplicate_of;
	v2_filter_register_source_filters(source_id, source, result);
}

void Engine::v2_filter_emit_source_created_filters(RuntimeV2Result &result)
{
	uint64_t source_id = 0;
	if (!read_handle_field(result.data.get(), "source", source_id))
		return;
	auto source_it = sources_.find(source_id);
	if (source_it == sources_.end())
		return;

	std::vector<std::pair<int, uint64_t>> ordered;
	for (const auto &[handle, entry] : filters_) {
		if (entry.source_id != source_id)
			continue;
		const int index = obs_source_filter_get_index(source_it->second, entry.filter);
		if (index >= 0)
			ordered.emplace_back(index, handle);
	}
	std::sort(ordered.begin(), ordered.end());

	for (const auto &[_, handle] : ordered) {
		auto it = filters_.find(handle);
		if (it == filters_.end())
			continue;
		result.events.push_back(RuntimeV2Event{"filter.created", make_filter_summary(handle, it->second, source_it->second)});
	}
}

} // namespace obs_engine
