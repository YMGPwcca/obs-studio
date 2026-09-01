#pragma once

#include "config.hpp"
#include "protocol.hpp"
#include "revision.hpp"

#include <obs.h>
#include <obs-properties.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace obs_engine {

class EventDispatcher;
struct InteractionV2State;
struct MediaV2State;
struct MediaV2Observer;
struct FilterV2State;
struct SourceV2Observer;
struct SourceV2State;

struct ItemEntry {
	uint64_t scene_id = 0;
	uint64_t source_id = 0;
	obs_sceneitem_t *item = nullptr;
	uint64_t parent_group_id = 0;
	bool is_group = false;
};

struct CanvasEntry {
	obs_canvas_t *canvas = nullptr;
	bool is_main = false;
};

struct TransitionEntry {
	obs_source_t *transition = nullptr;
};

struct PreviewOutputV2State;

struct FilterEntry {
	uint64_t source_id = 0;
	obs_source_t *filter = nullptr;
};

struct RuntimeV2Error {
	std::string code;
	std::string message;
};

struct RuntimeV2Event {
	std::string name;
	ObsDataPtr data;
	// Private evidence for filter.settingsChanged events. The values come from
	// the libobs deferred-update signal and are never serialized on the wire.
	uint64_t filter_update_serial_begin = 0;
	uint64_t filter_update_serial_end = 0;
};

struct RuntimeV2Result {
	ObsDataPtr data;
	std::vector<RuntimeV2Event> events;
	bool mutated = false;
	// Private settlement evidence for one filter settings request.  The
	// generation is captured immediately before the libobs update is submitted
	// and must advance before a deferred update can be command-owned.
	uint64_t filter_update_handle = 0;
	uint64_t filter_update_generation = 0;
	uint64_t filter_update_serial = 0;
	bool has_filter_update_baseline = false;
};

class Engine {
public:
	explicit Engine(Config config);
	~Engine();

	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;

	bool start();
	bool handle(obs_data_t *request);

	void v2_bind_source_events(RevisionState *revisions, EventDispatcher *events);
	void v2_begin_event_capture(RuntimeV2Result &result);
	void v2_wait_for_event_capture_callbacks();
	void v2_end_event_capture() noexcept;
	void v2_drain_deferred_source_events(RevisionState::MutationGuard &guard);
	void v2_flush_deferred_source_events(RevisionState::MutationGuard &guard);
	void v2_sync_source_observers();
	void v2_prepare_shutdown() noexcept;
	void v2_settle_source_mutation(obs_data_t *params, RuntimeV2Result &result);
	void v2_normalize_source_kind_metadata(RuntimeV2Result &result);
	void v2_bind_media_events(RevisionState *revisions, EventDispatcher *events);
	void v2_begin_media_event_capture(RuntimeV2Result &result);
	void v2_wait_for_media_event_callbacks();
	void v2_end_media_event_capture() noexcept;
	void v2_drain_deferred_media_events(RevisionState::MutationGuard &guard);
	void v2_flush_deferred_media_events(RevisionState::MutationGuard &guard);
	void v2_sync_media_observers();
	void v2_prepare_media_shutdown() noexcept;
	void v2_bind_filter_events(RevisionState *revisions, EventDispatcher *events);
	void v2_begin_filter_event_capture(RuntimeV2Result &result);
	void v2_wait_for_filter_event_callbacks();
	void v2_end_filter_event_capture() noexcept;
	void v2_drain_deferred_filter_events(RevisionState::MutationGuard &guard);
	void v2_flush_deferred_filter_events(RevisionState::MutationGuard &guard);
	void v2_sync_filter_observers();
	void v2_prepare_filter_shutdown() noexcept;
	bool v2_settle_filter_mutation(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_record_update_baseline(uint64_t handle, RuntimeV2Result &result);
	void v2_filter_forget_source(uint64_t source_id) noexcept;
	void v2_filter_prepare_parent_removal(uint64_t source_id, RuntimeV2Result &result);
	void v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source);
	bool v2_get_filter_parent(obs_data_t *params, uint64_t &handle, FilterEntry *&entry,
					  obs_source_t *&parent, RuntimeV2Error &error);
	bool v2_move_filter(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error,
				    enum obs_order_movement movement);
	bool v2_apply_filter_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error,
				      bool replace_settings);
	bool v2_create_filter_object(uint64_t source_id, obs_source_t *parent, uint64_t handle,
				     const std::string &kind, const std::string &name, ObsDataPtr &settings,
				     RuntimeV2Error &error);
	void v2_register_attached_filter(uint64_t source_id, obs_source_t *parent, uint64_t handle,
					 obs_source_t *filter);

	bool v2_source_kind_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_kind_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_reset_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_flags(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_dimensions(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_active(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_showing(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_get_missing_files(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_refresh(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_save_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_source_load_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_interaction_focus(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_interaction_mouse_move(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_interaction_mouse_button(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_interaction_mouse_wheel(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_interaction_key(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_interaction_text(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_interaction_reset(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_media_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_play(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_pause(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_toggle_pause(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_stop(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_restart(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_next(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_previous(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_get_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_get_position(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_media_set_position(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_filter_kind_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_get_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_set_order(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_move_up(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_move_down(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_move_top(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_filter_move_bottom(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_scene_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_get_items(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_item_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_duplicate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_get_transform(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_transform(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_position(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_scale(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_rotation(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_alignment(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_bounds(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_bounds_alignment(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_crop(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_crop_to_bounds(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_visible(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_locked(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_order(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_move_up(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_move_down(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_move_top(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_move_bottom(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_scale_filter(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_blend_mode(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_blend_method(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_create_group(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_ungroup(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_add_to_group(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_remove_from_group(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_get_children(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_canvas_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_get_main(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_get_video_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_set_video_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_list_scenes(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_get_channel(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_set_channel(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_canvas_get_flags(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_program_get_scene(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_program_set_scene(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_get_scene(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_set_scene(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_get_info(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_transition_kind_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_get_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_set_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_transition_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_studio_get_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_studio_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_studio_get_transition(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_studio_set_transition(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_studio_get_transition_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_studio_set_transition_duration(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_studio_transition(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_preview_output_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_destroy(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_set_target(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_resize(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_set_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_get_info(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_get_shared_texture(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_preview_output_release_shared_texture(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_properties_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_resolve(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_get_list_items(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_invoke_button(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_validate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_refresh(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

private:
	using ItemMap = std::unordered_map<uint64_t, ItemEntry>;
	using FilterMap = std::unordered_map<uint64_t, FilterEntry>;
	using CanvasMap = std::unordered_map<uint64_t, CanvasEntry>;
	using TransitionMap = std::unordered_map<uint64_t, TransitionEntry>;
	using PreviewOutputMap = std::unordered_map<uint64_t, std::shared_ptr<PreviewOutputV2State>>;

	uint64_t allocate_handle();
	bool input_type_exists(const char *type) const;
	bool validate_source_type(long long request_id, obs_data_t *request, const char *&type) const;
	struct PropertyButtonContext {
		~PropertyButtonContext();
		ObsDataPtr target;
		ObsDataPtr settings;
		obs_properties_t *properties = nullptr;
		obs_source_t *source = nullptr;
		std::vector<std::string> previous_sensitive;
		std::string property_name;
		std::string changed_property;
		bool changed_present = false;
		bool resolved_refresh = false;
		obs_property_t *property = nullptr;
	};
	bool v2_build_source_property_target(obs_data_t *requested_target, ObsDataPtr &target,
					    ObsDataPtr &base_settings, obs_properties_t *&properties,
					    obs_source_t *&source, RuntimeV2Error &error);
	bool v2_build_source_kind_property_target(obs_data_t *requested_target, ObsDataPtr &target,
						 ObsDataPtr &base_settings, obs_properties_t *&properties,
						 obs_source_t *&source, RuntimeV2Error &error);
	bool v2_build_filter_property_target(obs_data_t *requested_target, ObsDataPtr &target,
					    ObsDataPtr &base_settings, obs_properties_t *&properties,
					    obs_source_t *&source, RuntimeV2Error &error);
	bool v2_build_filter_kind_property_target(obs_data_t *requested_target, ObsDataPtr &target,
						 ObsDataPtr &base_settings, obs_properties_t *&properties,
						 obs_source_t *&source, RuntimeV2Error &error);
	bool v2_prepare_property_button(obs_data_t *params, PropertyButtonContext &context, RuntimeV2Error &error);
	bool v2_get_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source, RuntimeV2Error &error) const;
	bool v2_read_source_create_options(obs_data_t *params, std::string &kind, std::string &name,
					   ObsDataPtr &settings, RuntimeV2Error &error) const;
	bool v2_store_source_duplicate(uint64_t handle, obs_source_t *duplicate, RuntimeV2Error &error);
	bool v2_build_property_target(obs_data_t *params, ObsDataPtr &target, ObsDataPtr &settings,
				      obs_properties_t *&properties, obs_source_t *&source, RuntimeV2Error &error);
	ObsDataPtr v2_filter_order_data(uint64_t source_id, uint64_t changed, obs_source_t *parent) const;
	void v2_register_filter(uint64_t handle, uint64_t source_id, obs_source_t *filter);
	void v2_add_filter_observer(uint64_t handle);
	void v2_add_source_observer(uint64_t handle, obs_source_t *source,
					 std::vector<std::shared_ptr<SourceV2Observer>> &retire);
	void v2_add_media_observer(uint64_t handle, obs_source_t *source,
					 std::vector<std::shared_ptr<MediaV2Observer>> &retire);
	bool v2_collect_media_observer_changes(std::vector<std::pair<uint64_t, obs_source_t *>> &add,
					       std::vector<std::shared_ptr<MediaV2Observer>> &retire);
	bool v2_prepare_filter_settlement(obs_data_t *params, RuntimeV2Result &result, uint64_t &handle,
					 FilterEntry *&entry, RuntimeV2Error &error);
	bool v2_sync_filter_registry(std::unordered_set<obs_source_t *> &attached);
	void v2_remove_unattached_filters(const std::unordered_set<obs_source_t *> &attached);
	std::vector<uint64_t> v2_filter_observers_to_add() const;
	bool v2_get_media_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
				 RuntimeV2Error &error) const;
	bool v2_get_interaction_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
				       RuntimeV2Error &error);

	bool command_hello(long long request_id, obs_data_t *request);
	bool command_source_types(long long request_id, obs_data_t *request);
	bool command_source_defaults(long long request_id, obs_data_t *request);
	bool command_source_create(long long request_id, obs_data_t *request);
	bool command_source_update(long long request_id, obs_data_t *request);
	bool command_source_settings(long long request_id, obs_data_t *request);
	bool command_source_destroy(long long request_id, obs_data_t *request);
	bool command_scene_create(long long request_id, obs_data_t *request);
	bool command_scene_destroy(long long request_id, obs_data_t *request);
	bool command_scene_add(long long request_id, obs_data_t *request);
	bool command_item_remove(long long request_id, obs_data_t *request);
	bool command_item_transform(long long request_id, obs_data_t *request);
	bool command_program_set(long long request_id, obs_data_t *request);

	void release_item(ItemMap::iterator &it);
	std::vector<uint64_t> v2_item_handles_for_scene(uint64_t scene_id) const;
	bool v2_append_item_removal_events(const std::vector<uint64_t> &item_handles, RuntimeV2Result &result,
					   RuntimeV2Error &error) const;

	public:
	bool v2_get_canvas_entry(obs_data_t *params, uint64_t &handle, CanvasEntry *&entry, RuntimeV2Error &error);
	bool v2_get_scene_entry(obs_data_t *params, uint64_t &handle, obs_scene_t *&scene, RuntimeV2Error &error) const;
	bool v2_get_item_entry(obs_data_t *params, uint64_t &handle, ItemEntry *&entry, RuntimeV2Error &error);
	bool v2_register_scene_item(uint64_t scene_id, uint64_t parent_group_id, obs_sceneitem_t *item,
					std::vector<uint64_t> &added, RuntimeV2Error &error);
	bool v2_register_scene_items(uint64_t scene_id, obs_scene_t *scene, std::vector<uint64_t> &added,
					RuntimeV2Error &error);
	std::vector<uint64_t> v2_scene_ordered_item_handles(uint64_t scene_id, obs_scene_t *scene) const;
	uint64_t v2_item_handle_for_pointer(const obs_sceneitem_t *item) const;
	uint64_t v2_source_handle_for_pointer(const obs_source_t *source) const;
	obs_source_t *v2_source_for_handle(uint64_t handle) const;
	obs_scene_t *v2_scene_for_handle(uint64_t handle) const;
	uint64_t v2_scene_handle_for_pointer(const obs_source_t *source) const;
	ObsDataPtr v2_scene_summary(uint64_t handle, obs_scene_t *scene) const;
	ObsDataPtr v2_item_summary(uint64_t handle, const ItemEntry &entry) const;
	void v2_release_canvas_registry() noexcept;

	private:
	void remove_items_for_source(uint64_t source_id);
	void remove_items_for_scene(uint64_t scene_id);
	bool initialize_phase2_runtime();
	void shutdown_phase2_runtime() noexcept;
	bool prepare_startup_environment();
	bool reset_video();
	bool load_runtime_modules();
	void shutdown();

	Config config_;
	uint64_t next_handle_ = 1;
	uint64_t program_scene_ = 0;
	uint64_t preview_scene_ = 0;
	uint64_t main_canvas_ = 0;
	bool studio_enabled_ = false;
	uint64_t studio_transition_ = 0;
	uint32_t studio_transition_duration_ = 0;
	std::unordered_map<uint64_t, obs_source_t *> sources_;
	std::unordered_map<uint64_t, obs_scene_t *> scenes_;
	ItemMap items_;
	CanvasMap canvases_;
	TransitionMap transitions_;
	PreviewOutputMap preview_outputs_;
	std::unordered_map<uint64_t, uint64_t> scene_canvases_;
	std::unordered_map<obs_sceneitem_t *, uint64_t> item_handles_;
	FilterMap filters_;
	std::unordered_map<obs_source_t *, uint64_t> filter_handles_;
	std::shared_ptr<SourceV2State> source_v2_state_;
	std::shared_ptr<InteractionV2State> interaction_v2_state_;
	std::shared_ptr<MediaV2State> media_v2_state_;
	std::shared_ptr<FilterV2State> filter_v2_state_;
};

} // namespace obs_engine
