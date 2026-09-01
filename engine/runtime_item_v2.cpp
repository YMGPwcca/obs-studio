#include "runtime_phase2_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace obs_engine {
namespace {

constexpr double kMaxItemCoordinate = 10000000.0;
constexpr double kMaxItemScale = 10000.0;
constexpr double kMaxItemRotation = 1000000.0;
constexpr double kMaxItemBounds = 10000000.0;
constexpr long long kMaxItemCrop = 16384;

struct OrderedItems {
	const Engine *engine = nullptr;
	std::vector<uint64_t> *handles = nullptr;
};

bool collect_ordered_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ordered = static_cast<OrderedItems *>(param);
	if (!ordered || !ordered->engine || !ordered->handles || !item)
		return false;
	const uint64_t handle = ordered->engine->v2_item_handle_for_pointer(item);
	if (handle != 0)
		ordered->handles->push_back(handle);
	return true;
}

std::vector<uint64_t> ordered_parent_items(const Engine &engine, obs_scene_t *parent)
{
	std::vector<uint64_t> handles;
	OrderedItems ordered{&engine, &handles};
	obs_scene_enum_items(parent, collect_ordered_item, &ordered);
	return handles;
}

bool read_finite_field(obs_data_t *data, const char *name, double min_value, double max_value, double &out,
			       bool &present, RuntimeV2Error &error, const char *message)
{
	if (!phase2_read_double(data, name, out, present) ||
	    (present && (!std::isfinite(out) || out < min_value || out > max_value)))
		return phase2_fail(error, "bad_request", message);
	return true;
}

bool read_alignment_field(obs_data_t *data, const char *name, uint32_t &out, bool &present, RuntimeV2Error &error,
				  const char *message)
{
	long long value = 0;
	if (!phase2_read_integer(data, name, value, present))
		return phase2_fail(error, "bad_request", message);
	if (!present)
		return true;
	constexpr uint32_t allowed = OBS_ALIGN_LEFT | OBS_ALIGN_RIGHT | OBS_ALIGN_TOP | OBS_ALIGN_BOTTOM;
	if (value < 0 || static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max() ||
	    (static_cast<uint32_t>(value) & ~allowed) != 0)
		return phase2_fail(error, "bad_request", message);
	out = static_cast<uint32_t>(value);
	return true;
}

bool read_vec2_fields(obs_data_t *data, const char *name, vec2 &target, double min_value, double max_value,
			      bool &changed, RuntimeV2Error &error)
{
	ObsDataPtr object;
	bool present = false;
	if (!phase2_read_object(data, name, object, present))
		return phase2_fail(error, "bad_request", "transform vector must be an object");
	if (!present)
		return true;
	double value = 0.0;
	bool component_present = false;
	if (!read_finite_field(object.get(), "x", min_value, max_value, value, component_present, error,
				       "transform vector x is invalid"))
		return false;
	if (component_present) {
		target.x = static_cast<float>(value);
		changed = true;
	}
	if (!read_finite_field(object.get(), "y", min_value, max_value, value, component_present, error,
				       "transform vector y is invalid"))
		return false;
	if (component_present) {
		target.y = static_cast<float>(value);
		changed = true;
	}
	return true;
}

bool read_bounds_fields(obs_data_t *transform, obs_transform_info &info, bool &changed, RuntimeV2Error &error)
{
	ObsDataPtr bounds;
	bool present = false;
	if (!phase2_read_object(transform, "bounds", bounds, present))
		return phase2_fail(error, "bad_request", "transform.bounds must be an object");
	if (!present)
		return true;

	std::string type;
	bool type_present = false;
	if (!phase2_read_string(bounds.get(), "type", type, type_present) || (type_present && !phase2_parse_bounds_type(type, info.bounds_type)))
		return phase2_fail(error, "bad_request", "transform.bounds.type is invalid");
	if (type_present)
		changed = true;
	if (!read_alignment_field(bounds.get(), "alignment", info.bounds_alignment, present, error,
					  "transform.bounds.alignment is invalid"))
		return false;
	changed = changed || present;
	double value = 0.0;
	if (!read_finite_field(bounds.get(), "width", 0.0, kMaxItemBounds, value, present, error,
				       "transform.bounds.width is invalid"))
		return false;
	if (present) {
		info.bounds.x = static_cast<float>(value);
		changed = true;
	}
	if (!read_finite_field(bounds.get(), "height", 0.0, kMaxItemBounds, value, present, error,
				       "transform.bounds.height is invalid"))
		return false;
	if (present) {
		info.bounds.y = static_cast<float>(value);
		changed = true;
	}
	if (info.bounds_type != OBS_BOUNDS_NONE && (info.bounds.x <= 0.0f || info.bounds.y <= 0.0f))
		return phase2_fail(error, "bad_request", "non-none bounds require positive width and height");
	return true;
}

bool read_crop_fields(obs_data_t *transform, const ItemEntry &entry, obs_sceneitem_crop &crop, bool &changed,
			      RuntimeV2Error &error)
{
	ObsDataPtr crop_object;
	bool present = false;
	if (!phase2_read_object(transform, "crop", crop_object, present))
		return phase2_fail(error, "bad_request", "transform.crop must be an object");
	if (!present)
		return true;

	long long value = 0;
	bool component_present = false;
	const struct {
		const char *name;
		int *target;
	} fields[] = {{"left", &crop.left}, {"top", &crop.top}, {"right", &crop.right}, {"bottom", &crop.bottom}};
	for (const auto &field : fields) {
		if (!phase2_read_integer(crop_object.get(), field.name, value, component_present))
			return phase2_fail(error, "bad_request", "transform.crop components must be integers");
		if (!component_present)
			continue;
		if (value < 0 || value > kMaxItemCrop)
			return phase2_fail(error, "bad_request", "transform.crop component is outside the supported range");
		*field.target = static_cast<int>(value);
		changed = true;
	}

	obs_source_t *source = obs_sceneitem_get_source(entry.item);
	if (source) {
		const uint32_t width = obs_source_get_width(source);
		const uint32_t height = obs_source_get_height(source);
		if (width != 0 && static_cast<uint64_t>(crop.left) + crop.right > width)
			return phase2_fail(error, "bad_request", "transform.crop exceeds source width");
		if (height != 0 && static_cast<uint64_t>(crop.top) + crop.bottom > height)
			return phase2_fail(error, "bad_request", "transform.crop exceeds source height");
	}
	return true;
}

bool parse_transform(obs_data_t *transform, const ItemEntry &entry, obs_transform_info &info,
			     obs_sceneitem_crop &crop, bool &changed, RuntimeV2Error &error)
{
	if (!read_vec2_fields(transform, "position", info.pos, -kMaxItemCoordinate, kMaxItemCoordinate, changed, error) ||
	    !read_vec2_fields(transform, "scale", info.scale, -kMaxItemScale, kMaxItemScale, changed, error))
		return false;
	double value = 0.0;
	bool present = false;
	if (!read_finite_field(transform, "rotation", -kMaxItemRotation, kMaxItemRotation, value, present, error,
				       "transform.rotation is invalid"))
		return false;
	if (present) {
		info.rot = static_cast<float>(value);
		changed = true;
	}
	if (!read_alignment_field(transform, "alignment", info.alignment, present, error,
				  "transform.alignment is invalid"))
		return false;
	changed = changed || present;
	if (!read_bounds_fields(transform, info, changed, error) || !read_crop_fields(transform, entry, crop, changed, error))
		return false;
	bool crop_to_bounds = false;
	if (!phase2_read_bool(transform, "cropToBounds", crop_to_bounds, present))
		return phase2_fail(error, "bad_request", "transform.cropToBounds must be a boolean");
	if (present) {
		info.crop_to_bounds = crop_to_bounds;
		changed = true;
	}
	return true;
}

bool transform_equal(const obs_transform_info &left, const obs_transform_info &right,
			     const obs_sceneitem_crop &left_crop, const obs_sceneitem_crop &right_crop)
{
	return left.pos.x == right.pos.x && left.pos.y == right.pos.y && left.rot == right.rot &&
	       left.scale.x == right.scale.x && left.scale.y == right.scale.y && left.alignment == right.alignment &&
	       left.bounds_type == right.bounds_type && left.bounds_alignment == right.bounds_alignment &&
	       left.bounds.x == right.bounds.x && left.bounds.y == right.bounds.y &&
	       left.crop_to_bounds == right.crop_to_bounds && left_crop.left == right_crop.left &&
	       left_crop.top == right_crop.top && left_crop.right == right_crop.right && left_crop.bottom == right_crop.bottom;
}

bool apply_item_transform(Engine &engine, uint64_t handle, ItemEntry &entry, obs_data_t *transform,
			      RuntimeV2Result &result, RuntimeV2Error &error)
{
	obs_transform_info before = {};
	obs_sceneitem_crop before_crop = {};
	obs_sceneitem_get_info2(entry.item, &before);
	obs_sceneitem_get_crop(entry.item, &before_crop);
	obs_transform_info proposed = before;
	obs_sceneitem_crop proposed_crop = before_crop;
	bool supplied = false;
	if (!parse_transform(transform, entry, proposed, proposed_crop, supplied, error))
		return false;
	if (!supplied)
		return phase2_fail(error, "bad_request", "params.transform has no supported fields");
	if (transform_equal(before, proposed, before_crop, proposed_crop)) {
		result.data = engine.v2_item_summary(handle, entry);
		return true;
	}

	obs_sceneitem_defer_update_begin(entry.item);
	obs_sceneitem_set_info2(entry.item, &proposed);
	obs_sceneitem_set_crop(entry.item, &proposed_crop);
	obs_sceneitem_defer_update_end(entry.item);
	obs_sceneitem_force_update_transform(entry.item);

	result.data = engine.v2_item_summary(handle, entry);
	phase2_append_event(result, "item.transformChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool make_single_transform(obs_data_t *params, const char *field, ObsDataPtr &transform, RuntimeV2Error &error)
{
	transform.reset(obs_data_create());
	ObsDataPtr value;
	bool present = false;
	if (std::strcmp(field, "rotation") == 0 || std::strcmp(field, "alignment") == 0 ||
	    std::strcmp(field, "cropToBounds") == 0) {
		obs_data_item_t *item = obs_data_item_byname(params, field);
		if (!item)
			return phase2_fail(error, "bad_request", "the requested transform field is required");
		const obs_data_type type = obs_data_item_gettype(item);
		if ((std::strcmp(field, "cropToBounds") == 0 && type != OBS_DATA_BOOLEAN) ||
		    (std::strcmp(field, "cropToBounds") != 0 &&
		     (type != OBS_DATA_NUMBER || obs_data_item_numtype(item) != OBS_DATA_NUM_INT) &&
	     std::strcmp(field, "rotation") != 0) ||
		    (std::strcmp(field, "rotation") == 0 && type != OBS_DATA_NUMBER)) {
			obs_data_item_release(&item);
			return phase2_fail(error, "bad_request", "the requested transform field has the wrong type");
		}
		if (type == OBS_DATA_BOOLEAN)
			obs_data_set_bool(transform.get(), field, obs_data_item_get_bool(item));
		else if (std::strcmp(field, "rotation") == 0)
			obs_data_set_double(transform.get(), field, obs_data_item_get_double(item));
		else
			obs_data_set_int(transform.get(), field, obs_data_item_get_int(item));
		obs_data_item_release(&item);
		return true;
	}
	if (!phase2_read_object(params, field, value, present) || !present)
		return phase2_fail(error, "bad_request", "the requested transform object is required");
	obs_data_set_obj(transform.get(), field, value.get());
	return true;
}

ObsDataPtr make_order_data(const Engine &engine, uint64_t handle, const ItemEntry &entry,
				   const std::vector<uint64_t> &ordered)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "item", handle);
	phase2_set_handle(data.get(), "scene", entry.scene_id);
	if (entry.parent_group_id != 0)
		phase2_set_handle(data.get(), "parentGroup", entry.parent_group_id);
	ObsArrayPtr items(obs_data_array_create());
	for (const uint64_t item_handle : ordered) {
		ObsDataPtr value(obs_data_create());
		phase2_set_handle(value.get(), "item", item_handle);
		obs_data_array_push_back(items.get(), value.get());
	}
	obs_data_set_array(data.get(), "items", items.get());
	const auto position = std::find(ordered.begin(), ordered.end(), handle);
	obs_data_set_int(data.get(), "index", position == ordered.end() ? -1 : static_cast<long long>(position - ordered.begin()));
	return data;
}

ObsDataPtr make_items_changed_data(const Engine &engine, uint64_t scene_handle, obs_scene_t *scene, uint64_t group_handle = 0)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "scene", scene_handle);
	if (group_handle != 0)
		phase2_set_handle(data.get(), "group", group_handle);
	const std::vector<uint64_t> ordered = engine.v2_scene_ordered_item_handles(scene_handle, scene);
	ObsArrayPtr items(obs_data_array_create());
	for (const uint64_t handle : ordered) {
		ObsDataPtr item(obs_data_create());
		phase2_set_handle(item.get(), "item", handle);
		obs_data_array_push_back(items.get(), item.get());
	}
	obs_data_set_array(data.get(), "items", items.get());
	return data;
}

bool read_item_boolean(obs_data_t *params, const char *name, bool &value, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_bool(params, name, value, present) || !present)
		return phase2_fail(error, "bad_request", "boolean item property is required");
	return true;
}

bool read_item_enum(obs_data_t *params, const char *name, std::string &value, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, name, value, present) || !present || value.empty())
		return phase2_fail(error, "bad_request", "semantic item enum is required");
	return true;
}

} // namespace

bool Engine::v2_item_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	result.data = v2_item_summary(handle, *entry);
	return true;
}

bool Engine::v2_item_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t scene_handle = 0;
	uint64_t source_handle = 0;
	if (!phase2_read_handle(params, "scene", scene_handle) || !phase2_read_handle(params, "source", source_handle))
		return phase2_fail(error, "bad_request", "params.scene and params.source must be canonical decimal handles");
	auto scene_it = scenes_.find(scene_handle);
	auto source_it = sources_.find(source_handle);
	if (scene_it == scenes_.end() || source_it == sources_.end())
		return phase2_fail(error, "not_found", "scene or source handle was not found");
	obs_sceneitem_t *item = obs_scene_add(scene_it->second, source_it->second);
	if (!item)
		return phase2_fail(error, "obs_error", "obs_scene_add failed");
	std::vector<uint64_t> added;
	if (!v2_register_scene_item(scene_handle, 0, item, added, error)) {
		obs_sceneitem_remove(item);
		return false;
	}
	const uint64_t handle = v2_item_handle_for_pointer(item);
	auto item_it = items_.find(handle);
	if (handle == 0 || item_it == items_.end())
		return phase2_fail(error, "internal_error", "new scene item was not registered");
	result.data = v2_item_summary(handle, item_it->second);
	phase2_append_event(result, "item.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	if (entry->is_group)
		return phase2_fail(error, "not_available", "group item duplication is not supported by the references-only item API");

	uint64_t target_scene_handle = entry->scene_id;
	std::string requested_scene;
	bool scene_present = false;
	if (!phase2_read_string(params, "scene", requested_scene, scene_present))
		return phase2_fail(error, "bad_request", "params.scene must be a canonical decimal scene handle string");
	if (scene_present && !phase2_parse_handle(requested_scene, target_scene_handle))
		return phase2_fail(error, "bad_request", "params.scene must be a canonical decimal scene handle string");
	auto target_it = scenes_.find(target_scene_handle);
	if (target_it == scenes_.end())
		return phase2_fail(error, "not_found", "target scene handle was not found");
	if (!scene_canvases_.contains(entry->scene_id) || !scene_canvases_.contains(target_scene_handle) ||
	    scene_canvases_.at(entry->scene_id) != scene_canvases_.at(target_scene_handle))
		return phase2_fail(error, "invalid_state", "item duplication requires Scenes on the same Canvas");

	obs_sceneitem_t *duplicate = obs_scene_add(target_it->second, obs_sceneitem_get_source(entry->item));
	if (!duplicate)
		return phase2_fail(error, "obs_error", "obs_scene_add failed while duplicating item");
	obs_transform_info info = {};
	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_info2(entry->item, &info);
	obs_sceneitem_get_crop(entry->item, &crop);
	obs_sceneitem_defer_update_begin(duplicate);
	obs_sceneitem_set_info2(duplicate, &info);
	obs_sceneitem_set_crop(duplicate, &crop);
	obs_sceneitem_set_scale_filter(duplicate, obs_sceneitem_get_scale_filter(entry->item));
	obs_sceneitem_set_blending_mode(duplicate, obs_sceneitem_get_blending_mode(entry->item));
	obs_sceneitem_set_blending_method(duplicate, obs_sceneitem_get_blending_method(entry->item));
	obs_sceneitem_defer_update_end(duplicate);
	obs_sceneitem_set_visible(duplicate, obs_sceneitem_visible(entry->item));
	obs_sceneitem_set_locked(duplicate, obs_sceneitem_locked(entry->item));

	std::vector<uint64_t> added;
	if (!v2_register_scene_item(target_scene_handle, 0, duplicate, added, error)) {
		obs_sceneitem_remove(duplicate);
		return false;
	}
	const uint64_t duplicate_handle = v2_item_handle_for_pointer(duplicate);
	auto duplicate_it = items_.find(duplicate_handle);
	if (duplicate_handle == 0 || duplicate_it == items_.end())
		return phase2_fail(error, "internal_error", "duplicated scene item was not registered");
	result.data = v2_item_summary(duplicate_handle, duplicate_it->second);
	phase2_set_handle(result.data.get(), "duplicateOf", handle);
	phase2_append_event(result, "item.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	const uint64_t scene_handle = entry->scene_id;
	obs_scene_t *scene = scenes_.contains(scene_handle) ? scenes_.at(scene_handle) : nullptr;
	if (!scene)
		return phase2_fail(error, "not_found", "item parent scene was not found");
	std::vector<uint64_t> children;
	if (entry->is_group) {
		for (const auto &[candidate, candidate_entry] : items_) {
			if (candidate_entry.parent_group_id == handle)
				children.push_back(candidate);
		}
		std::sort(children.begin(), children.end());
	}
	result.data = v2_item_summary(handle, *entry);
	for (const uint64_t child : children) {
		auto child_it = items_.find(child);
		if (child_it != items_.end())
			phase2_append_event(result, "item.removed", v2_item_summary(child, child_it->second));
	}
	phase2_append_event(result, "item.removed", phase2_clone_data(result.data.get()));
	for (const uint64_t child : children) {
		auto child_it = items_.find(child);
		if (child_it != items_.end())
			release_item(child_it);
	}
	auto item_it = items_.find(handle);
	if (item_it != items_.end())
		release_item(item_it);
	phase2_append_event(result, "scene.itemsChanged", make_items_changed_data(*this, scene_handle, scene));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_get_transform(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return v2_item_get(params, result, error);
}

bool Engine::v2_item_set_transform(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	bool present = false;
	if (!phase2_read_object(params, "transform", transform, present) || !present)
		return phase2_fail(error, "bad_request", "params.transform object is required");
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_position(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "position", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_scale(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "scale", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_rotation(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "rotation", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_alignment(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "alignment", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_bounds(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "bounds", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_bounds_alignment(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	long long alignment = 0;
	bool present = false;
	if (!phase2_read_integer(params, "alignment", alignment, present) || !present)
		return phase2_fail(error, "bad_request", "params.alignment must be an integer");
	ObsDataPtr bounds(obs_data_create());
	obs_data_set_int(bounds.get(), "alignment", alignment);
	ObsDataPtr transform(obs_data_create());
	obs_data_set_obj(transform.get(), "bounds", bounds.get());
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_crop(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "crop", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_crop_to_bounds(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	ObsDataPtr transform;
	if (!make_single_transform(params, "cropToBounds", transform, error))
		return false;
	return apply_item_transform(*this, handle, *entry, transform.get(), result, error);
}

bool Engine::v2_item_set_visible(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	bool visible = false;
	if (!read_item_boolean(params, "visible", visible, error))
		return false;
	if (obs_sceneitem_visible(entry->item) == visible) {
		result.data = v2_item_summary(handle, *entry);
		return true;
	}
	if (!obs_sceneitem_set_visible(entry->item, visible) && obs_sceneitem_visible(entry->item) != visible)
		return phase2_fail(error, "obs_error", "libobs rejected the item visibility change");
	result.data = v2_item_summary(handle, *entry);
	phase2_append_event(result, "item.visibilityChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_set_locked(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	bool locked = false;
	if (!read_item_boolean(params, "locked", locked, error))
		return false;
	if (obs_sceneitem_locked(entry->item) == locked) {
		result.data = v2_item_summary(handle, *entry);
		return true;
	}
	if (!obs_sceneitem_set_locked(entry->item, locked) && obs_sceneitem_locked(entry->item) != locked)
		return phase2_fail(error, "obs_error", "libobs rejected the item lock change");
	result.data = v2_item_summary(handle, *entry);
	phase2_append_event(result, "item.lockedChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_set_order(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	long long requested = 0;
	bool present = false;
	if (!phase2_read_integer(params, "index", requested, present) || !present || requested < 0)
		return phase2_fail(error, "bad_request", "params.index must be a non-negative integer");
	obs_scene_t *parent = obs_sceneitem_get_scene(entry->item);
	if (!parent)
		return phase2_fail(error, "not_found", "item is no longer attached to a Scene");
	const std::vector<uint64_t> before = ordered_parent_items(*this, parent);
	if (static_cast<uint64_t>(requested) >= before.size())
		return phase2_fail(error, "bad_request", "params.index is outside the item order");
	const int old_index = obs_sceneitem_get_order_position(entry->item);
	if (old_index == requested) {
		result.data = make_order_data(*this, handle, *entry, before);
		return true;
	}
	obs_sceneitem_set_order_position(entry->item, static_cast<int>(requested));
	const std::vector<uint64_t> after = ordered_parent_items(*this, parent);
	result.data = make_order_data(*this, handle, *entry, after);
	phase2_append_event(result, "item.orderChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool move_item_order(Engine &engine, obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error,
			    enum obs_order_movement movement)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!engine.v2_get_item_entry(params, handle, entry, error))
		return false;
	obs_scene_t *parent = obs_sceneitem_get_scene(entry->item);
	if (!parent)
		return phase2_fail(error, "not_found", "item is no longer attached to a Scene");
	const int before_index = obs_sceneitem_get_order_position(entry->item);
	obs_sceneitem_set_order(entry->item, movement);
	const int after_index = obs_sceneitem_get_order_position(entry->item);
	const std::vector<uint64_t> after = ordered_parent_items(engine, parent);
	result.data = make_order_data(engine, handle, *entry, after);
	if (before_index != after_index) {
		phase2_append_event(result, "item.orderChanged", phase2_clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_item_move_up(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return move_item_order(*this, params, result, error, OBS_ORDER_MOVE_UP);
}

bool Engine::v2_item_move_down(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return move_item_order(*this, params, result, error, OBS_ORDER_MOVE_DOWN);
}

bool Engine::v2_item_move_top(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return move_item_order(*this, params, result, error, OBS_ORDER_MOVE_TOP);
}

bool Engine::v2_item_move_bottom(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	return move_item_order(*this, params, result, error, OBS_ORDER_MOVE_BOTTOM);
}

bool Engine::v2_item_set_scale_filter(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	std::string value;
	if (!read_item_enum(params, "scaleFilter", value, error))
		return false;
	enum obs_scale_type filter = OBS_SCALE_DISABLE;
	if (!phase2_parse_scale_filter(value, filter))
		return phase2_fail(error, "bad_request", "scaleFilter is not supported");
	if (obs_sceneitem_get_scale_filter(entry->item) == filter) {
		result.data = v2_item_summary(handle, *entry);
		return true;
	}
	obs_sceneitem_set_scale_filter(entry->item, filter);
	result.data = v2_item_summary(handle, *entry);
	phase2_append_event(result, "item.transformChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_set_blend_mode(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	std::string value;
	if (!read_item_enum(params, "blendMode", value, error))
		return false;
	enum obs_blending_type mode = OBS_BLEND_NORMAL;
	if (!phase2_parse_blend_mode(value, mode))
		return phase2_fail(error, "bad_request", "blendMode is not supported");
	if (obs_sceneitem_get_blending_mode(entry->item) == mode) {
		result.data = v2_item_summary(handle, *entry);
		return true;
	}
	obs_sceneitem_set_blending_mode(entry->item, mode);
	result.data = v2_item_summary(handle, *entry);
	phase2_append_event(result, "item.blendChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_set_blend_method(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	std::string value;
	if (!read_item_enum(params, "blendMethod", value, error))
		return false;
	enum obs_blending_method method = OBS_BLEND_METHOD_DEFAULT;
	if (!phase2_parse_blend_method(value, method))
		return phase2_fail(error, "bad_request", "blendMethod is not supported");
	if (obs_sceneitem_get_blending_method(entry->item) == method) {
		result.data = v2_item_summary(handle, *entry);
		return true;
	}
	obs_sceneitem_set_blending_method(entry->item, method);
	result.data = v2_item_summary(handle, *entry);
	phase2_append_event(result, "item.blendChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_get_children(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t handle = 0;
	ItemEntry *entry = nullptr;
	if (!v2_get_item_entry(params, handle, entry, error))
		return false;
	if (!entry->is_group)
		return phase2_fail(error, "invalid_state", "item is not a group");
	auto scene_it = scenes_.find(entry->scene_id);
	if (scene_it == scenes_.end())
		return phase2_fail(error, "not_found", "group parent Scene was not found");
	std::vector<uint64_t> added;
	if (!v2_register_scene_items(entry->scene_id, scene_it->second, added, error))
		return false;
	std::vector<uint64_t> children;
	for (const auto &[candidate, candidate_entry] : items_) {
		if (candidate_entry.parent_group_id == handle)
			children.push_back(candidate);
	}
	std::sort(children.begin(), children.end(), [&](uint64_t left, uint64_t right) {
		return obs_sceneitem_get_order_position(items_.at(left).item) < obs_sceneitem_get_order_position(items_.at(right).item);
	});
	ObsArrayPtr values(obs_data_array_create());
	for (const uint64_t child : children)
		obs_data_array_push_back(values.get(), v2_item_summary(child, items_.at(child)).get());
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "group", handle);
	phase2_set_handle(data.get(), "scene", entry->scene_id);
	obs_data_set_array(data.get(), "children", values.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(children.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_item_create_group(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t scene_handle = 0;
	obs_scene_t *scene = nullptr;
	if (!v2_get_scene_entry(params, scene_handle, scene, error))
		return false;
	std::string name;
	bool name_present = false;
	if (!phase2_read_string(params, "name", name, name_present))
		return phase2_fail(error, "bad_request", "params.name must be a string when present");
	if (name_present && !phase2_is_bounded_string(name, 256))
		return phase2_fail(error, "bad_request", "group name must be a non-empty string of at most 256 bytes");

	ObsArrayPtr requested;
	bool items_present = false;
	if (!phase2_read_array(params, "items", requested, items_present))
		return phase2_fail(error, "bad_request", "params.items must be an array when present");
	std::vector<obs_sceneitem_t *> items;
	std::vector<uint64_t> handles;
	if (items_present) {
		const size_t count = obs_data_array_count(requested.get());
		if (count == 0 || count > 1024)
			return phase2_fail(error, "bad_request", "params.items must contain between 1 and 1024 items");
		for (size_t index = 0; index < count; ++index) {
			ObsDataPtr value(obs_data_array_item(requested.get(), index));
			if (!value)
				return phase2_fail(error, "bad_request", "each group item must be an object containing item");
			uint64_t item_handle = 0;
			if (!phase2_read_handle(value.get(), "item", item_handle))
				return phase2_fail(error, "bad_request", "each group item handle must be canonical");
			auto item_it = items_.find(item_handle);
			if (item_it == items_.end() || item_it->second.scene_id != scene_handle || item_it->second.is_group ||
				item_it->second.parent_group_id != 0)
				return phase2_fail(error, "invalid_state", "group items must be ungrouped items from the target Scene");
			if (std::find(handles.begin(), handles.end(), item_handle) != handles.end())
				return phase2_fail(error, "bad_request", "group items must be unique");
			handles.push_back(item_handle);
			items.push_back(item_it->second.item);
		}
	}
	const std::string actual_name = name_present ? name : "engine-group-" + std::to_string(next_handle_);
	obs_sceneitem_t *group = items_present ? obs_scene_insert_group2(scene, actual_name.c_str(), items.data(), items.size(), false)
					       : obs_scene_add_group2(scene, actual_name.c_str(), false);
	if (!group)
		return phase2_fail(error, "obs_error", "libobs group creation failed");
	std::vector<uint64_t> added;
	if (!v2_register_scene_items(scene_handle, scene, added, error))
		return false;
	const uint64_t group_handle = v2_item_handle_for_pointer(group);
	if (group_handle == 0 || !items_.contains(group_handle))
		return phase2_fail(error, "internal_error", "created group item was not registered");
	result.data = v2_item_summary(group_handle, items_.at(group_handle));
	phase2_append_event(result, "item.created", phase2_clone_data(result.data.get()));
	phase2_append_event(result, "scene.itemsChanged", make_items_changed_data(*this, scene_handle, scene, group_handle));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_add_to_group(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t item_handle = 0;
	ItemEntry *item = nullptr;
	if (!v2_get_item_entry(params, item_handle, item, error))
		return false;
	uint64_t group_handle = 0;
	ItemEntry *group = nullptr;
	if (!phase2_read_handle(params, "group", group_handle))
		return phase2_fail(error, "bad_request", "params.group must be a canonical group handle string");
	auto group_it = items_.find(group_handle);
	if (group_it == items_.end())
		return phase2_fail(error, "not_found", "group handle was not found");
	group = &group_it->second;
	if (!group->is_group || item->is_group || item->scene_id != group->scene_id || item->parent_group_id != 0)
		return phase2_fail(error, "invalid_state", "item and group must be compatible ungrouped items in one Scene");
	obs_sceneitem_group_add_item(group->item, item->item);
	item->parent_group_id = group_handle;
	result.data = v2_item_summary(item_handle, *item);
	phase2_append_event(result, "scene.itemsChanged", make_items_changed_data(*this, item->scene_id, scenes_.at(item->scene_id), group_handle));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_remove_from_group(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t item_handle = 0;
	ItemEntry *item = nullptr;
	if (!v2_get_item_entry(params, item_handle, item, error))
		return false;
	if (item->parent_group_id == 0)
		return phase2_fail(error, "invalid_state", "item is not a group child");
	uint64_t group_handle = item->parent_group_id;
	std::string requested_group;
	bool present = false;
	if (!phase2_read_string(params, "group", requested_group, present))
		return phase2_fail(error, "bad_request", "params.group must be a canonical group handle string");
	if (present && (!phase2_parse_handle(requested_group, group_handle) || group_handle != item->parent_group_id))
		return phase2_fail(error, "invalid_state", "params.group is not the item's current parent group");
	auto group_it = items_.find(group_handle);
	if (group_it == items_.end() || !group_it->second.is_group)
		return phase2_fail(error, "not_found", "group handle was not found");
	obs_sceneitem_group_remove_item(group_it->second.item, item->item);
	item->parent_group_id = 0;
	result.data = v2_item_summary(item_handle, *item);
	phase2_append_event(result, "scene.itemsChanged", make_items_changed_data(*this, item->scene_id, scenes_.at(item->scene_id), group_handle));
	result.mutated = true;
	return true;
}

bool Engine::v2_item_ungroup(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	phase2_reset_result(result, error);
	uint64_t group_handle = 0;
	ItemEntry *group = nullptr;
	if (!v2_get_item_entry(params, group_handle, group, error))
		return false;
	if (!group->is_group)
		return phase2_fail(error, "invalid_state", "item is not a group");
	const uint64_t scene_handle = group->scene_id;
	obs_scene_t *scene = scenes_.contains(scene_handle) ? scenes_.at(scene_handle) : nullptr;
	if (!scene)
		return phase2_fail(error, "not_found", "group parent Scene was not found");
	std::vector<uint64_t> old_children;
	for (const auto &[handle, entry] : items_)
		if (entry.parent_group_id == group_handle)
			old_children.push_back(handle);
	std::sort(old_children.begin(), old_children.end());
	obs_sceneitem_group_ungroup2(group->item, false);

	for (const uint64_t child_handle : old_children) {
		auto child_it = items_.find(child_handle);
		if (child_it != items_.end()) {
			phase2_append_event(result, "item.removed", v2_item_summary(child_handle, child_it->second));
			release_item(child_it);
		}
	}
	auto group_it = items_.find(group_handle);
	if (group_it != items_.end()) {
		ObsDataPtr removed(obs_data_create());
		phase2_set_handle(removed.get(), "item", group_handle);
		phase2_set_handle(removed.get(), "scene", scene_handle);
		phase2_append_event(result, "item.removed", std::move(removed));
		release_item(group_it);
	}
	std::vector<uint64_t> added;
	if (!v2_register_scene_items(scene_handle, scene, added, error))
		return false;
	for (const uint64_t added_handle : added)
		phase2_append_event(result, "item.created", v2_item_summary(added_handle, items_.at(added_handle)));
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "scene", scene_handle);
	obs_data_set_bool(data.get(), "ungrouped", true);
	result.data = std::move(data);
	phase2_append_event(result, "scene.itemsChanged", make_items_changed_data(*this, scene_handle, scene));
	result.mutated = true;
	return true;
}

} // namespace obs_engine
