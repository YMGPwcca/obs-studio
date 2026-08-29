#include "runtime.hpp"

#include "validation.hpp"

#include <graphics/vec2.h>

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

constexpr size_t kMaxSourceKindBytes = 128;
constexpr size_t kMaxObjectNameBytes = 256;

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "runtime operation failed";
	return false;
}

bool is_bounded_string(const char *value, size_t max_bytes)
{
	if (!value)
		return false;
	for (size_t index = 0; index <= max_bytes; ++index) {
		if (value[index] == '\0')
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

ObsDataPtr make_item_identity(uint64_t item, uint64_t scene, uint64_t source)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "item", item);
	set_handle(data.get(), "scene", scene);
	set_handle(data.get(), "source", source);
	return data;
}

void append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	RuntimeV2Event event;
	event.name = name;
	event.data = std::move(data);
	result.events.push_back(std::move(event));
}

ObsDataPtr make_transform_data(uint64_t item_handle, const obs_transform_info &info)
{
	ObsDataPtr data(obs_data_create());
	ObsDataPtr transform(obs_data_create());
	ObsDataPtr position(obs_data_create());
	ObsDataPtr scale(obs_data_create());

	set_handle(data.get(), "item", item_handle);
	obs_data_set_double(position.get(), "x", static_cast<double>(info.pos.x));
	obs_data_set_double(position.get(), "y", static_cast<double>(info.pos.y));
	obs_data_set_double(scale.get(), "x", static_cast<double>(info.scale.x));
	obs_data_set_double(scale.get(), "y", static_cast<double>(info.scale.y));
	obs_data_set_obj(transform.get(), "position", position.get());
	obs_data_set_obj(transform.get(), "scale", scale.get());
	obs_data_set_double(transform.get(), "rotation", static_cast<double>(info.rot));
	obs_data_set_int(transform.get(), "alignment", static_cast<long long>(info.alignment));
	obs_data_set_obj(data.get(), "transform", transform.get());
	return data;
}

} // namespace

bool Engine::v2_source_kind_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsDataPtr data(obs_data_create());
	ObsArrayPtr kinds(obs_data_array_create());

	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_input_types(index, &kind); ++index) {
		if (!kind)
			continue;

		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "id", kind);
		const char *display_name = obs_source_get_display_name(kind);
		obs_data_set_string(entry.get(), "displayName", display_name ? display_name : kind);
		obs_data_set_int(entry.get(), "outputFlags", obs_get_source_output_flags(kind));
		obs_module_t *module = obs_source_get_module(kind);
		if (module) {
			const char *module_file = obs_get_module_file_name(module);
			if (module_file)
				obs_data_set_string(entry.get(), "module", module_file);
		}
		obs_data_array_push_back(kinds.get(), entry.get());
	}

	obs_data_set_array(data.get(), "kinds", kinds.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	std::string kind;
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid source kind identifier");
	if (!input_type_exists(kind.c_str()))
		return fail(error, "not_found", "source kind is not registered");

	ObsDataPtr defaults(obs_get_source_defaults(kind.c_str()));
	if (!defaults)
		return fail(error, "obs_error", "source kind did not provide defaults");

	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", defaults.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	std::string kind;
	bool present = false;
	if (!read_string_field(params, "kind", kind, present) || !present ||
	    !is_safe_identifier(kind.c_str(), kMaxSourceKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid source kind identifier");
	if (!input_type_exists(kind.c_str()))
		return fail(error, "not_found", "source kind is not registered");

	std::string requested_name;
	if (!read_string_field(params, "name", requested_name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (present && !is_bounded_string(requested_name.c_str(), kMaxObjectNameBytes))
		return fail(error, "bad_request", "source name is too long");

	ObsDataPtr settings;
	if (!read_object_field(params, "settings", settings, present))
		return fail(error, "bad_request", "params.settings must be an object when present");

	const uint64_t handle = allocate_handle();
	const std::string generated_name = "engine-source-" + std::to_string(handle);
	const char *name = requested_name.empty() ? generated_name.c_str() : requested_name.c_str();
	obs_source_t *source = obs_source_create_private(kind.c_str(), name, settings.get());
	if (!source)
		return fail(error, "obs_error", "obs_source_create_private failed");

	try {
		ObsDataPtr data(obs_data_create());
		set_handle(data.get(), "source", handle);
		obs_data_set_string(data.get(), "name", obs_source_get_name(source));
		obs_data_set_string(data.get(), "kind", obs_source_get_id(source));

		ObsDataPtr event_data(obs_data_create());
		set_handle(event_data.get(), "source", handle);
		obs_data_set_string(event_data.get(), "name", obs_source_get_name(source));
		obs_data_set_string(event_data.get(), "kind", obs_source_get_id(source));
		append_event(result, "source.created", std::move(event_data));
		result.data = std::move(data);

		const auto [_, inserted] = sources_.emplace(handle, source);
		if (!inserted) {
			obs_source_release(source);
			return fail(error, "internal_error", "source handle collision");
		}
	} catch (...) {
		obs_source_release(source);
		throw;
	}

	result.mutated = true;
	return true;
}

bool Engine::v2_source_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t handle = 0;
	if (!read_handle_field(params, "source", handle))
		return fail(error, "bad_request", "params.source must be a canonical decimal handle string");
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return fail(error, "not_found", "source handle was not found");

	ObsDataPtr settings(obs_source_get_settings(it->second));
	if (!settings)
		return fail(error, "obs_error", "could not read source settings");

	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_obj(data.get(), "settings", settings.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_source_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t handle = 0;
	if (!read_handle_field(params, "source", handle))
		return fail(error, "bad_request", "params.source must be a canonical decimal handle string");
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return fail(error, "not_found", "source handle was not found");

	ObsDataPtr patch;
	bool present = false;
	if (!read_object_field(params, "settings", patch, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");

	// libobs keeps source settings in one context object and obs_source_update()
	// applies the patch to that object in place. Hold the object before updating
	// so there is no fallible post-mutation settings lookup.
	ObsDataPtr settings(obs_source_get_settings(it->second));
	if (!settings)
		return fail(error, "obs_error", "could not read source settings before update");

	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_obj(data.get(), "settings", settings.get());

	ObsDataPtr event_data(obs_data_create());
	set_handle(event_data.get(), "source", handle);
	obs_data_set_obj(event_data.get(), "settings", settings.get());
	append_event(result, "source.settingsChanged", std::move(event_data));
	result.data = std::move(data);

	obs_source_update(it->second, patch.get());
	result.mutated = true;
	return true;
}

bool Engine::v2_source_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t handle = 0;
	if (!read_handle_field(params, "source", handle))
		return fail(error, "bad_request", "params.source must be a canonical decimal handle string");
	auto source_it = sources_.find(handle);
	if (source_it == sources_.end())
		return fail(error, "not_found", "source handle was not found");

	std::vector<uint64_t> item_handles;
	for (const auto &[item_handle, entry] : items_) {
		if (entry.source_id == handle)
			item_handles.push_back(item_handle);
	}
	std::sort(item_handles.begin(), item_handles.end());

	// Prepare every externally visible artifact before detaching anything.
	result.events.reserve(item_handles.size() + filters_.size() + 1);
	for (uint64_t item_handle : item_handles) {
		auto item_it = items_.find(item_handle);
		if (item_it == items_.end())
			return fail(error, "internal_error", "source item map changed during removal preparation");
		append_event(result, "item.removed",
			     make_item_identity(item_handle, item_it->second.scene_id, item_it->second.source_id));
	}
	v2_filter_prepare_parent_removal(handle, result);
	ObsDataPtr source_event(obs_data_create());
	set_handle(source_event.get(), "source", handle);
	append_event(result, "source.removed", std::move(source_event));
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	result.data = std::move(data);

	for (uint64_t item_handle : item_handles) {
		auto item_it = items_.find(item_handle);
		if (item_it != items_.end())
			release_item(item_it);
	}
	obs_source_release(source_it->second);
	sources_.erase(source_it);

	result.mutated = true;
	return true;
}

bool Engine::v2_scene_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	std::string requested_name;
	bool present = false;
	if (!read_string_field(params, "name", requested_name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (present && !is_bounded_string(requested_name.c_str(), kMaxObjectNameBytes))
		return fail(error, "bad_request", "scene name is too long");

	const uint64_t handle = allocate_handle();
	const std::string generated_name = "engine-scene-" + std::to_string(handle);
	const char *name = requested_name.empty() ? generated_name.c_str() : requested_name.c_str();
	obs_scene_t *scene = obs_scene_create_private(name);
	if (!scene)
		return fail(error, "obs_error", "obs_scene_create_private failed");

	try {
		const char *actual_name = obs_source_get_name(obs_scene_get_source(scene));
		ObsDataPtr data(obs_data_create());
		set_handle(data.get(), "scene", handle);
		obs_data_set_string(data.get(), "name", actual_name ? actual_name : name);

		ObsDataPtr event_data(obs_data_create());
		set_handle(event_data.get(), "scene", handle);
		obs_data_set_string(event_data.get(), "name", actual_name ? actual_name : name);
		append_event(result, "scene.created", std::move(event_data));
		result.data = std::move(data);

		const auto [_, inserted] = scenes_.emplace(handle, scene);
		if (!inserted) {
			obs_scene_release(scene);
			return fail(error, "internal_error", "scene handle collision");
		}
	} catch (...) {
		obs_scene_release(scene);
		throw;
	}

	result.mutated = true;
	return true;
}

bool Engine::v2_scene_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t handle = 0;
	if (!read_handle_field(params, "scene", handle))
		return fail(error, "bad_request", "params.scene must be a canonical decimal handle string");
	auto scene_it = scenes_.find(handle);
	if (scene_it == scenes_.end())
		return fail(error, "not_found", "scene handle was not found");

	std::vector<uint64_t> item_handles;
	for (const auto &[item_handle, entry] : items_) {
		if (entry.scene_id == handle)
			item_handles.push_back(item_handle);
	}
	std::sort(item_handles.begin(), item_handles.end());

	result.events.reserve(item_handles.size() + 1);
	for (uint64_t item_handle : item_handles) {
		auto item_it = items_.find(item_handle);
		if (item_it == items_.end())
			return fail(error, "internal_error", "scene item map changed during removal preparation");
		append_event(result, "item.removed",
			     make_item_identity(item_handle, item_it->second.scene_id, item_it->second.source_id));
	}
	ObsDataPtr scene_event(obs_data_create());
	set_handle(scene_event.get(), "scene", handle);
	append_event(result, "scene.removed", std::move(scene_event));
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "scene", handle);
	result.data = std::move(data);

	for (uint64_t item_handle : item_handles) {
		auto item_it = items_.find(item_handle);
		if (item_it != items_.end())
			release_item(item_it);
	}

	if (program_scene_ == handle) {
		obs_set_output_source(0, nullptr);
		program_scene_ = 0;
	}

	obs_scene_release(scene_it->second);
	scenes_.erase(scene_it);

	result.mutated = true;
	return true;
}

bool Engine::v2_item_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t scene_handle = 0;
	uint64_t source_handle = 0;
	if (!read_handle_field(params, "scene", scene_handle) || !read_handle_field(params, "source", source_handle))
		return fail(error, "bad_request", "params.scene and params.source must be canonical decimal handle strings");

	auto scene_it = scenes_.find(scene_handle);
	auto source_it = sources_.find(source_handle);
	if (scene_it == scenes_.end() || source_it == sources_.end())
		return fail(error, "not_found", "scene or source handle was not found");

	const uint64_t item_handle = allocate_handle();
	// Build the wire artifacts before obs_scene_add() mutates the scene.
	result.data = make_item_identity(item_handle, scene_handle, source_handle);
	append_event(result, "item.created", make_item_identity(item_handle, scene_handle, source_handle));

	obs_sceneitem_t *item = obs_scene_add(scene_it->second, source_it->second);
	if (!item)
		return fail(error, "obs_error", "obs_scene_add failed");
	obs_sceneitem_addref(item);

	try {
		const auto [_, inserted] = items_.emplace(item_handle, ItemEntry{scene_handle, source_handle, item});
		if (!inserted) {
			obs_sceneitem_remove(item);
			obs_sceneitem_release(item);
			return fail(error, "internal_error", "item handle collision");
		}
	} catch (...) {
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
		throw;
	}

	result.mutated = true;
	return true;
}

bool Engine::v2_item_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t handle = 0;
	if (!read_handle_field(params, "item", handle))
		return fail(error, "bad_request", "params.item must be a canonical decimal handle string");
	auto it = items_.find(handle);
	if (it == items_.end())
		return fail(error, "not_found", "item handle was not found");

	const uint64_t scene_handle = it->second.scene_id;
	const uint64_t source_handle = it->second.source_id;
	result.data = make_item_identity(handle, scene_handle, source_handle);
	append_event(result, "item.removed", make_item_identity(handle, scene_handle, source_handle));

	release_item(it);
	result.mutated = true;
	return true;
}

bool Engine::v2_item_set_transform(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);

	uint64_t handle = 0;
	if (!read_handle_field(params, "item", handle))
		return fail(error, "bad_request", "params.item must be a canonical decimal handle string");
	auto it = items_.find(handle);
	if (it == items_.end())
		return fail(error, "not_found", "item handle was not found");

	ObsDataPtr transform;
	bool present = false;
	if (!read_object_field(params, "transform", transform, present) || !present)
		return fail(error, "bad_request", "params.transform object is required");

	obs_transform_info info = {};
	obs_sceneitem_get_info2(it->second.item, &info);
	bool changed = false;
	double value = 0.0;
	bool value_present = false;

	ObsDataPtr position;
	if (!read_object_field(transform.get(), "position", position, present))
		return fail(error, "bad_request", "transform.position must be an object when present");
	if (present) {
		if (!read_finite_double(position.get(), "x", -10000000.0, 10000000.0, value, value_present))
			return fail(error, "bad_request", "invalid transform.position.x");
		if (value_present) {
			info.pos.x = static_cast<float>(value);
			changed = true;
		}
		if (!read_finite_double(position.get(), "y", -10000000.0, 10000000.0, value, value_present))
			return fail(error, "bad_request", "invalid transform.position.y");
		if (value_present) {
			info.pos.y = static_cast<float>(value);
			changed = true;
		}
	}

	ObsDataPtr scale;
	if (!read_object_field(transform.get(), "scale", scale, present))
		return fail(error, "bad_request", "transform.scale must be an object when present");
	if (present) {
		if (!read_finite_double(scale.get(), "x", -10000.0, 10000.0, value, value_present))
			return fail(error, "bad_request", "invalid transform.scale.x");
		if (value_present) {
			info.scale.x = static_cast<float>(value);
			changed = true;
		}
		if (!read_finite_double(scale.get(), "y", -10000.0, 10000.0, value, value_present))
			return fail(error, "bad_request", "invalid transform.scale.y");
		if (value_present) {
			info.scale.y = static_cast<float>(value);
			changed = true;
		}
	}

	if (!read_finite_double(transform.get(), "rotation", -1000000.0, 1000000.0, value, value_present))
		return fail(error, "bad_request", "invalid transform.rotation");
	if (value_present) {
		info.rot = static_cast<float>(value);
		changed = true;
	}

	long long alignment = 0;
	bool alignment_present = false;
	if (!read_integer(transform.get(), "alignment", alignment, alignment_present))
		return fail(error, "bad_request", "transform.alignment must be an integer");
	if (alignment_present) {
		const uint32_t allowed = OBS_ALIGN_LEFT | OBS_ALIGN_RIGHT | OBS_ALIGN_TOP | OBS_ALIGN_BOTTOM;
		if (alignment < 0 || static_cast<uint64_t>(alignment) > std::numeric_limits<uint32_t>::max() ||
		    (static_cast<uint32_t>(alignment) & ~allowed) != 0)
			return fail(error, "bad_request", "invalid transform.alignment");
		info.alignment = static_cast<uint32_t>(alignment);
		changed = true;
	}

	if (!changed)
		return fail(error, "bad_request", "params.transform must contain at least one supported transform field");

	obs_sceneitem_set_info2(it->second.item, &info);
	obs_sceneitem_get_info2(it->second.item, &info);

	result.data = make_transform_data(handle, info);
	append_event(result, "item.transformChanged", make_transform_data(handle, info));
	result.mutated = true;
	return true;
}

} // namespace obs_engine
