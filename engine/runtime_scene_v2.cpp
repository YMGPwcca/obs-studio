#include "runtime_phase2_common.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace obs_engine {
namespace {

constexpr size_t kMaxSceneNameBytes = 256;

void set_nullable_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	if (handle != 0)
		phase2_set_handle(data, name, handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

struct SceneItemDiscovery {
	Engine *engine = nullptr;
	uint64_t scene_id = 0;
	uint64_t parent_group_id = 0;
	std::vector<uint64_t> *added = nullptr;
	RuntimeV2Error *error = nullptr;
	bool failed = false;
};

bool collect_scene_item(obs_scene_t *scene, obs_sceneitem_t *item, void *param);

bool collect_group_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *discovery = static_cast<SceneItemDiscovery *>(param);
	if (!discovery || !discovery->engine || !discovery->added || !discovery->error || !item)
		return false;
	if (obs_sceneitem_is_group(item)) {
		discovery->failed = true;
		phase2_fail(*discovery->error, "invalid_state", "nested scene item groups are not supported");
		return false;
	}
	if (!discovery->engine->v2_register_scene_item(discovery->scene_id, discovery->parent_group_id, item,
								*discovery->added, *discovery->error)) {
		discovery->failed = true;
		return false;
	}
	return true;
}

bool collect_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *discovery = static_cast<SceneItemDiscovery *>(param);
	if (!discovery || !discovery->engine || !discovery->added || !discovery->error || !item)
		return false;
	const uint64_t group_id = discovery->parent_group_id;
	if (!discovery->engine->v2_register_scene_item(discovery->scene_id, group_id, item, *discovery->added,
								*discovery->error)) {
		discovery->failed = true;
		return false;
	}
	if (group_id == 0 && obs_sceneitem_is_group(item)) {
		SceneItemDiscovery children{discovery->engine, discovery->scene_id,
								    discovery->engine->v2_item_handle_for_pointer(item), discovery->added,
								    discovery->error};
		obs_sceneitem_group_enum_items(item, collect_group_item, &children);
		if (children.failed)
			discovery->failed = true;
	}
	return !discovery->failed;
}

struct SceneOrderDiscovery {
	const Engine *engine = nullptr;
	std::vector<uint64_t> *handles = nullptr;
};

bool collect_scene_order(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *discovery = static_cast<SceneOrderDiscovery *>(param);
	if (!discovery || !discovery->engine || !discovery->handles || !item)
		return false;
	const uint64_t handle = discovery->engine->v2_item_handle_for_pointer(item);
	if (handle != 0)
		discovery->handles->push_back(handle);
	return true;
}

bool read_scene_name(obs_data_t *params, const char *field, std::string &name, bool &present, RuntimeV2Error &error)
{
	if (!phase2_read_string(params, field, name, present))
		return phase2_fail(error, "bad_request", "scene name must be a string when present");
	if (present && !phase2_is_bounded_string(name, kMaxSceneNameBytes))
		return phase2_fail(error, "bad_request", "scene name must be a non-empty string of at most 256 bytes");
	return true;
}

bool read_optional_canvas(obs_data_t *params, uint64_t main_handle, uint64_t &canvas_handle, RuntimeV2Error &error)
{
	std::string requested;
	bool present = false;
	if (!phase2_read_string(params, "canvas", requested, present))
		return phase2_fail(error, "bad_request", "params.canvas must be a canonical decimal canvas handle string");
	if (!present) {
		canvas_handle = main_handle;
		return true;
	}
	if (!phase2_parse_handle(requested, canvas_handle))
		return phase2_fail(error, "bad_request", "params.canvas must be a canonical decimal canvas handle string");
	return true;
}

ObsDataPtr make_scene_item_transform(const ItemEntry &entry)
{
	ObsDataPtr data(obs_data_create());
	ObsDataPtr position(obs_data_create());
	ObsDataPtr scale(obs_data_create());
	ObsDataPtr bounds(obs_data_create());
	ObsDataPtr crop(obs_data_create());
	obs_transform_info info = {};
	obs_sceneitem_crop crop_value = {};
	obs_sceneitem_get_info2(entry.item, &info);
	obs_sceneitem_get_crop(entry.item, &crop_value);

	obs_data_set_double(position.get(), "x", static_cast<double>(info.pos.x));
	obs_data_set_double(position.get(), "y", static_cast<double>(info.pos.y));
	obs_data_set_double(scale.get(), "x", static_cast<double>(info.scale.x));
	obs_data_set_double(scale.get(), "y", static_cast<double>(info.scale.y));
	obs_data_set_string(bounds.get(), "type", phase2_bounds_type_name(info.bounds_type));
	obs_data_set_int(bounds.get(), "alignment", static_cast<long long>(info.bounds_alignment));
	obs_data_set_double(bounds.get(), "width", static_cast<double>(info.bounds.x));
	obs_data_set_double(bounds.get(), "height", static_cast<double>(info.bounds.y));
	obs_data_set_int(crop.get(), "left", crop_value.left);
	obs_data_set_int(crop.get(), "top", crop_value.top);
	obs_data_set_int(crop.get(), "right", crop_value.right);
	obs_data_set_int(crop.get(), "bottom", crop_value.bottom);

	obs_data_set_obj(data.get(), "position", position.get());
	obs_data_set_obj(data.get(), "scale", scale.get());
	obs_data_set_double(data.get(), "rotation", static_cast<double>(info.rot));
	obs_data_set_int(data.get(), "alignment", static_cast<long long>(info.alignment));
	obs_data_set_obj(data.get(), "bounds", bounds.get());
	obs_data_set_obj(data.get(), "crop", crop.get());
	obs_data_set_bool(data.get(), "cropToBounds", info.crop_to_bounds);
	return data;
}

} // namespace

bool Engine::initialize_phase2_runtime()
{
	obs_canvas_t *main = obs_get_main_canvas();
	if (!main)
		return false;
	try {
		const uint64_t handle = allocate_handle();
		const auto [_, inserted] = canvases_.emplace(handle, CanvasEntry{main, true});
		if (!inserted) {
			obs_canvas_release(main);
			return false;
		}
		main_canvas_ = handle;
		obs_add_main_render_callback(&Engine::v2_preview_output_render_callback, this);
		preview_render_callback_registered_ = true;
		obs_enter_graphics();
		preview_output_capable_ = gs_get_device_type() == GS_DEVICE_DIRECT3D_11 && gs_shared_texture_available();
		obs_leave_graphics();
		return true;
	} catch (...) {
		obs_canvas_release(main);
		throw;
	}
}

void Engine::shutdown_phase2_runtime() noexcept
{
	try {
		auto main_it = canvases_.find(main_canvas_);
		if (main_it != canvases_.end() && main_it->second.canvas)
			obs_canvas_set_channel(main_it->second.canvas, 0, nullptr);
		program_scene_ = 0;
		v2_shutdown_preview_outputs();
		v2_clear_preview_source();
		studio_enabled_ = false;
		studio_transition_ = 0;
		studio_transition_active_ = false;
		studio_transition_from_scene_ = 0;
		studio_transition_destination_scene_ = 0;

		for (auto &[_, entry] : transitions_)
			obs_source_release(entry.transition);
		transitions_.clear();
	} catch (...) {
		// Destruction must remain noexcept; the protocol bridge is already being
		// detached by the host scope before Engine teardown.
	}
}

void Engine::v2_release_canvas_registry() noexcept
{
	for (auto &[_, entry] : canvases_) {
		if (entry.canvas) {
			if (!entry.is_main && !obs_canvas_removed(entry.canvas))
				obs_canvas_remove(entry.canvas);
			obs_canvas_release(entry.canvas);
			entry.canvas = nullptr;
		}
	}
	canvases_.clear();
	main_canvas_ = 0;
}

bool Engine::v2_get_canvas_entry(obs_data_t *params, uint64_t &handle, CanvasEntry *&entry, RuntimeV2Error &error)
{
	if (!phase2_read_handle(params, "canvas", handle))
		return phase2_fail(error, "bad_request", "params.canvas must be a canonical decimal canvas handle string");
	auto it = canvases_.find(handle);
	if (it == canvases_.end() || !it->second.canvas || obs_canvas_removed(it->second.canvas))
		return phase2_fail(error, "not_found", "canvas handle was not found");
	entry = &it->second;
	return true;
}

bool Engine::v2_get_scene_entry(obs_data_t *params, uint64_t &handle, obs_scene_t *&scene, RuntimeV2Error &error) const
{
	if (!phase2_read_handle(params, "scene", handle))
		return phase2_fail(error, "bad_request", "params.scene must be a canonical decimal scene handle string");
	auto it = scenes_.find(handle);
	if (it == scenes_.end() || !it->second)
		return phase2_fail(error, "not_found", "scene handle was not found");
	scene = it->second;
	return true;
}

bool Engine::v2_get_item_entry(obs_data_t *params, uint64_t &handle, ItemEntry *&entry, RuntimeV2Error &error)
{
	if (!phase2_read_handle(params, "item", handle))
		return phase2_fail(error, "bad_request", "params.item must be a canonical decimal item handle string");
	auto it = items_.find(handle);
	if (it == items_.end() || !it->second.item || !obs_sceneitem_get_scene(it->second.item))
		return phase2_fail(error, "not_found", "item handle was not found");
	entry = &it->second;
	return true;
}

uint64_t Engine::v2_item_handle_for_pointer(const obs_sceneitem_t *item) const
{
	const auto it = item_handles_.find(const_cast<obs_sceneitem_t *>(item));
	return it == item_handles_.end() ? 0 : it->second;
}

uint64_t Engine::v2_source_handle_for_pointer(const obs_source_t *source) const
{
	for (const auto &[handle, candidate] : sources_) {
		if (candidate == source)
			return handle;
	}
	return 0;
}

obs_source_t *Engine::v2_source_for_handle(uint64_t handle) const
{
	const auto it = sources_.find(handle);
	return it == sources_.end() ? nullptr : it->second;
}

obs_scene_t *Engine::v2_scene_for_handle(uint64_t handle) const
{
	const auto it = scenes_.find(handle);
	return it == scenes_.end() ? nullptr : it->second;
}

uint64_t Engine::v2_scene_handle_for_pointer(const obs_source_t *source) const
{
	for (const auto &[handle, scene] : scenes_) {
		if (scene && obs_scene_get_source(scene) == source)
			return handle;
	}
	return 0;
}

bool Engine::v2_register_scene_item(uint64_t scene_id, uint64_t parent_group_id, obs_sceneitem_t *item,
					std::vector<uint64_t> &added, RuntimeV2Error &error)
{
	if (!item)
		return phase2_fail(error, "internal_error", "scene enumeration returned a null item");
	const auto known = item_handles_.find(item);
	if (known != item_handles_.end()) {
		auto item_it = items_.find(known->second);
		if (item_it == items_.end())
			return phase2_fail(error, "internal_error", "scene item handle registry is inconsistent");
		if (item_it->second.scene_id != scene_id)
			return phase2_fail(error, "invalid_state", "one libobs scene item belongs to multiple protocol Scenes");
		item_it->second.parent_group_id = parent_group_id;
		item_it->second.source_id = v2_source_handle_for_pointer(obs_sceneitem_get_source(item));
		item_it->second.is_group = obs_sceneitem_is_group(item);
		return true;
	}

	const uint64_t handle = allocate_handle();
	const uint64_t source_id = v2_source_handle_for_pointer(obs_sceneitem_get_source(item));
	const bool is_group = obs_sceneitem_is_group(item);
	bool retained = false;
	bool inserted_item = false;
	bool inserted_pointer = false;
	try {
		obs_sceneitem_addref(item);
		retained = true;
		const auto [item_it, item_inserted] = items_.emplace(
			handle, ItemEntry{scene_id, source_id, item, parent_group_id, is_group});
		if (!item_inserted)
			throw std::runtime_error("scene item handle collision");
		inserted_item = true;
		const auto [pointer_it, pointer_inserted] = item_handles_.emplace(item, handle);
		if (!pointer_inserted)
			throw std::runtime_error("scene item pointer collision");
		(void)item_it;
		(void)pointer_it;
		inserted_pointer = true;
		added.push_back(handle);
		return true;
	} catch (...) {
		if (inserted_pointer)
			item_handles_.erase(item);
		if (inserted_item)
			items_.erase(handle);
		if (retained)
			obs_sceneitem_release(item);
		return phase2_fail(error, "internal_error", "could not register scene item handle");
	}
}

bool Engine::v2_register_scene_items(uint64_t scene_id, obs_scene_t *scene, std::vector<uint64_t> &added,
					RuntimeV2Error &error)
{
	if (!scene)
		return phase2_fail(error, "internal_error", "scene pointer is unavailable");
	SceneItemDiscovery discovery{this, scene_id, 0, &added, &error, false};
	obs_scene_enum_items(scene, collect_scene_item, &discovery);
	return !discovery.failed;
}

std::vector<uint64_t> Engine::v2_scene_ordered_item_handles(uint64_t, obs_scene_t *scene) const
{
	std::vector<uint64_t> handles;
	SceneOrderDiscovery discovery{this, &handles};
	obs_scene_enum_items(scene, collect_scene_order, &discovery);
	return handles;
}

ObsDataPtr Engine::v2_scene_summary(uint64_t handle, obs_scene_t *scene) const
{
	ObsDataPtr data(obs_data_create());
	obs_source_t *source = obs_scene_get_source(scene);
	phase2_set_handle(data.get(), "scene", handle);
	obs_data_set_string(data.get(), "name", source && obs_source_get_name(source) ? obs_source_get_name(source) : "");
	const auto canvas_it = scene_canvases_.find(handle);
	if (canvas_it != scene_canvases_.end())
		phase2_set_handle(data.get(), "canvas", canvas_it->second);
	obs_data_set_int(data.get(), "width", source ? static_cast<long long>(obs_source_get_width(source)) : 0);
	obs_data_set_int(data.get(), "height", source ? static_cast<long long>(obs_source_get_height(source)) : 0);
	const std::vector<uint64_t> ordered = v2_scene_ordered_item_handles(handle, scene);
	obs_data_set_int(data.get(), "itemCount", static_cast<long long>(ordered.size()));
	return data;
}

ObsDataPtr Engine::v2_item_summary(uint64_t handle, const ItemEntry &entry) const
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "item", handle);
	phase2_set_handle(data.get(), "scene", entry.scene_id);
	if (entry.source_id != 0)
		phase2_set_handle(data.get(), "source", entry.source_id);
	if (entry.parent_group_id != 0)
		phase2_set_handle(data.get(), "parentGroup", entry.parent_group_id);
	obs_data_set_bool(data.get(), "isGroup", entry.is_group);
	obs_source_t *source = obs_sceneitem_get_source(entry.item);
	if (source) {
		obs_data_set_string(data.get(), "name", obs_source_get_name(source) ? obs_source_get_name(source) : "");
		obs_data_set_string(data.get(), "kind", obs_source_get_id(source) ? obs_source_get_id(source) : "");
	}
	obs_data_set_int(data.get(), "order", obs_sceneitem_get_order_position(entry.item));
	obs_data_set_obj(data.get(), "transform", make_scene_item_transform(entry).get());
	obs_data_set_bool(data.get(), "visible", obs_sceneitem_visible(entry.item));
	obs_data_set_bool(data.get(), "locked", obs_sceneitem_locked(entry.item));
	obs_data_set_string(data.get(), "scaleFilter", phase2_scale_filter_name(obs_sceneitem_get_scale_filter(entry.item)));
	obs_data_set_string(data.get(), "blendMode", phase2_blend_mode_name(obs_sceneitem_get_blending_mode(entry.item)));
	obs_data_set_string(data.get(), "blendMethod", phase2_blend_method_name(obs_sceneitem_get_blending_method(entry.item)));
	return data;
}

bool Engine::v2_scene_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::vector<uint64_t> handles;
	handles.reserve(scenes_.size());
	for (const auto &[handle, _] : scenes_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr scenes(obs_data_array_create());
	for (const uint64_t handle : handles) {
		auto it = scenes_.find(handle);
		if (it != scenes_.end())
			obs_data_array_push_back(scenes.get(), v2_scene_summary(handle, it->second).get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "scenes", scenes.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(obs_data_array_count(scenes.get())));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_scene_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	obs_scene_t *scene = nullptr;
	if (!v2_get_scene_entry(params, handle, scene, error))
		return false;
	std::vector<uint64_t> added;
	if (!v2_register_scene_items(handle, scene, added, error))
		return false;
	result.data = v2_scene_summary(handle, scene);
	return true;
}

bool Engine::v2_scene_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	std::string requested_name;
	bool name_present = false;
	if (!read_scene_name(params, "name", requested_name, name_present, error))
		return false;
	uint64_t canvas_handle = 0;
	if (!read_optional_canvas(params, main_canvas_, canvas_handle, error))
		return false;
	CanvasEntry *canvas = nullptr;
	auto canvas_it = canvases_.find(canvas_handle);
	if (canvas_it == canvases_.end() || !canvas_it->second.canvas || obs_canvas_removed(canvas_it->second.canvas))
		return phase2_fail(error, "not_found", "scene canvas handle was not found");
	canvas = &canvas_it->second;

	const uint64_t handle = allocate_handle();
	const std::string generated_name = "engine-scene-" + std::to_string(handle);
	const std::string name = name_present ? requested_name : generated_name;
	obs_scene_t *scene = obs_canvas_scene_create(canvas->canvas, name.c_str());
	if (!scene)
		return phase2_fail(error, "obs_error", "canvas-aware scene creation failed");

	try {
		if (!scenes_.emplace(handle, scene).second)
			throw std::runtime_error("scene handle collision");
		if (!scene_canvases_.emplace(handle, canvas_handle).second) {
			scenes_.erase(handle);
			throw std::runtime_error("scene canvas mapping collision");
		}
	} catch (...) {
		scene_canvases_.erase(handle);
		scenes_.erase(handle);
		obs_canvas_scene_remove(scene);
		obs_scene_release(scene);
		return phase2_fail(error, "internal_error", "could not register scene handle");
	}

	result.data = v2_scene_summary(handle, scene);
	phase2_append_event(result, "scene.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_scene_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	obs_scene_t *scene = nullptr;
	if (!v2_get_scene_entry(params, handle, scene, error))
		return false;
	std::string requested_name;
	bool present = false;
	if (!read_scene_name(params, "name", requested_name, present, error) || !present)
		return phase2_fail(error, "bad_request", "params.name must be a non-empty scene name");
	obs_source_t *source = obs_scene_get_source(scene);
	const std::string previous = source && obs_source_get_name(source) ? obs_source_get_name(source) : "";
	if (previous == requested_name) {
		result.data = v2_scene_summary(handle, scene);
		return true;
	}
	obs_source_set_name(source, requested_name.c_str());
	result.data = v2_scene_summary(handle, scene);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "scene", handle);
	obs_data_set_string(event_data.get(), "previousName", previous.c_str());
	obs_data_set_string(event_data.get(), "name", obs_source_get_name(source));
	phase2_append_event(result, "scene.renamed", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_scene_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t source_handle = 0;
	obs_scene_t *source_scene = nullptr;
	if (!v2_get_scene_entry(params, source_handle, source_scene, error))
		return false;
	std::string requested_name;
	bool name_present = false;
	if (!read_scene_name(params, "name", requested_name, name_present, error))
		return false;
	std::string mode;
	bool mode_present = false;
	if (!phase2_read_string(params, "mode", mode, mode_present))
		return phase2_fail(error, "bad_request", "params.mode must be a string when present");
	if (mode_present && mode != "references")
		return phase2_fail(error, "bad_request", "scene duplication mode must be 'references'");
	const auto canvas_it = scene_canvases_.find(source_handle);
	if (canvas_it == scene_canvases_.end())
		return phase2_fail(error, "internal_error", "scene has no canvas mapping");
	const uint64_t duplicate_handle = allocate_handle();
	const std::string generated_name = std::string(obs_source_get_name(obs_scene_get_source(source_scene))) + " copy";
	const std::string name = name_present ? requested_name : generated_name;
	obs_scene_t *duplicate = obs_scene_duplicate(source_scene, name.c_str(), OBS_SCENE_DUP_REFS);
	if (!duplicate)
		return phase2_fail(error, "obs_error", "libobs scene duplication failed");

	try {
		if (!scenes_.emplace(duplicate_handle, duplicate).second)
			throw std::runtime_error("scene handle collision");
		if (!scene_canvases_.emplace(duplicate_handle, canvas_it->second).second) {
			scenes_.erase(duplicate_handle);
			throw std::runtime_error("scene canvas mapping collision");
		}
	} catch (...) {
		scene_canvases_.erase(duplicate_handle);
		scenes_.erase(duplicate_handle);
		obs_canvas_scene_remove(duplicate);
		obs_scene_release(duplicate);
		return phase2_fail(error, "internal_error", "could not register duplicated scene handle");
	}

	std::vector<uint64_t> added;
	if (!v2_register_scene_items(duplicate_handle, duplicate, added, error)) {
		for (const uint64_t item_handle : added) {
			auto item_it = items_.find(item_handle);
			if (item_it != items_.end())
				release_item(item_it);
		}
		scene_canvases_.erase(duplicate_handle);
		scenes_.erase(duplicate_handle);
		obs_canvas_scene_remove(duplicate);
		obs_scene_release(duplicate);
		return false;
	}

	result.data = v2_scene_summary(duplicate_handle, duplicate);
	phase2_set_handle(result.data.get(), "duplicateOf", source_handle);
	ObsDataPtr scene_event = phase2_clone_data(result.data.get());
	phase2_append_event(result, "scene.created", std::move(scene_event));
	for (const uint64_t item_handle : added) {
		auto item_it = items_.find(item_handle);
		if (item_it != items_.end())
			phase2_append_event(result, "item.created", v2_item_summary(item_handle, item_it->second));
	}
	result.mutated = true;
	return true;
}

bool Engine::v2_scene_get_items(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	obs_scene_t *scene = nullptr;
	if (!v2_get_scene_entry(params, handle, scene, error))
		return false;
	std::vector<uint64_t> added;
	if (!v2_register_scene_items(handle, scene, added, error))
		return false;
	const std::vector<uint64_t> ordered = v2_scene_ordered_item_handles(handle, scene);
	ObsArrayPtr items(obs_data_array_create());
	for (const uint64_t item_handle : ordered) {
		auto item_it = items_.find(item_handle);
		if (item_it != items_.end() && item_it->second.scene_id == handle) {
			ObsDataPtr item = v2_item_summary(item_handle, item_it->second);
			obs_data_array_push_back(items.get(), item.get());
		}
	}
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "scene", handle);
	obs_data_set_array(data.get(), "items", items.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(obs_data_array_count(items.get())));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_scene_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	obs_scene_t *scene = nullptr;
	if (!v2_get_scene_entry(params, handle, scene, error))
		return false;
	std::vector<uint64_t> added;
	if (!v2_register_scene_items(handle, scene, added, error))
		return false;
	ObsDataPtr data = v2_scene_summary(handle, scene);
	const std::vector<uint64_t> ordered = v2_scene_ordered_item_handles(handle, scene);
	ObsArrayPtr items(obs_data_array_create());
	for (const uint64_t item_handle : ordered) {
		auto item_it = items_.find(item_handle);
		if (item_it != items_.end() && item_it->second.scene_id == handle) {
			ObsDataPtr item = v2_item_summary(item_handle, item_it->second);
			obs_data_array_push_back(items.get(), item.get());
		}
	}
	obs_data_set_array(data.get(), "items", items.get());
	obs_data_set_int(data.get(), "totalItemCount", static_cast<long long>(v2_item_handles_for_scene(handle).size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_scene_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	obs_scene_t *scene = nullptr;
	if (!v2_get_scene_entry(params, handle, scene, error))
		return false;
	const uint64_t canvas_handle = scene_canvases_.contains(handle) ? scene_canvases_.at(handle) : 0;
	const std::vector<uint64_t> all_items = v2_item_handles_for_scene(handle);
	const bool program_was_scene = v2_current_program_scene() == handle;
	const bool preview_was_scene = preview_scene_ == handle;
	std::vector<uint64_t> item_handles = all_items;
	std::stable_sort(item_handles.begin(), item_handles.end(), [&](uint64_t left, uint64_t right) {
		const bool left_group = items_.contains(left) && items_.at(left).is_group;
		const bool right_group = items_.contains(right) && items_.at(right).is_group;
		if (left_group != right_group)
			return !left_group;
		return left < right;
	});

	result.data = v2_scene_summary(handle, scene);
	if (program_was_scene) {
		auto main_it = canvases_.find(main_canvas_);
		if (main_it != canvases_.end() && main_it->second.canvas)
			obs_canvas_set_channel(main_it->second.canvas, 0, nullptr);
		program_scene_ = 0;
		ObsDataPtr event_data(obs_data_create());
		set_nullable_handle(event_data.get(), "scene", 0);
		set_nullable_handle(event_data.get(), "previousScene", handle);
		if (main_canvas_ != 0)
			phase2_set_handle(event_data.get(), "canvas", main_canvas_);
		phase2_append_event(result, "program.sceneChanged", std::move(event_data));
	}
	if (preview_was_scene) {
		v2_clear_preview_source();
		ObsDataPtr event_data(obs_data_create());
		set_nullable_handle(event_data.get(), "scene", 0);
		set_nullable_handle(event_data.get(), "previousScene", handle);
		phase2_append_event(result, "preview.sceneChanged", std::move(event_data));
	}
	if (!v2_append_item_removal_events(item_handles, result, error))
		return false;
	ObsDataPtr removed_event(obs_data_create());
	phase2_set_handle(removed_event.get(), "scene", handle);
	if (canvas_handle != 0)
		phase2_set_handle(removed_event.get(), "canvas", canvas_handle);
	phase2_append_event(result, "scene.removed", std::move(removed_event));

	for (const uint64_t item_handle : item_handles) {
		auto item_it = items_.find(item_handle);
		if (item_it != items_.end())
			release_item(item_it);
	}
	obs_canvas_scene_remove(scene);
	obs_scene_release(scene);
	scene_canvases_.erase(handle);
	scenes_.erase(handle);
	result.mutated = true;
	return true;
}

} // namespace obs_engine
