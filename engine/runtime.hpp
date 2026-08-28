#pragma once

#include "config.hpp"
#include "protocol.hpp"
#include "revision.hpp"

#include <obs.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obs_engine {

class EventDispatcher;
struct InteractionV2State;
struct MediaV2State;
struct SourceV2State;

struct ItemEntry {
	uint64_t scene_id = 0;
	uint64_t source_id = 0;
	obs_sceneitem_t *item = nullptr;
};

struct RuntimeV2Error {
	std::string code;
	std::string message;
};

struct RuntimeV2Event {
	std::string name;
	ObsDataPtr data;
};

struct RuntimeV2Result {
	ObsDataPtr data;
	std::vector<RuntimeV2Event> events;
	bool mutated = false;
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

	bool v2_scene_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_scene_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_item_set_transform(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

	bool v2_properties_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_resolve(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_get_list_items(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_invoke_button(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_validate(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);
	bool v2_properties_refresh(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error);

private:
	using ItemMap = std::unordered_map<uint64_t, ItemEntry>;

	uint64_t allocate_handle();
	bool input_type_exists(const char *type) const;
	bool validate_source_type(long long request_id, obs_data_t *request, const char *&type) const;
	bool v2_build_property_target(obs_data_t *params, ObsDataPtr &target, ObsDataPtr &settings,
				      obs_properties_t *&properties, obs_source_t *&source, RuntimeV2Error &error);
	bool v2_get_interaction_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
				       RuntimeV2Error &error);

	bool command_hello(long long request_id);
	bool command_source_types(long long request_id);
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
	void remove_items_for_source(uint64_t source_id);
	void remove_items_for_scene(uint64_t scene_id);
	void shutdown();

	Config config_;
	uint64_t next_handle_ = 1;
	uint64_t program_scene_ = 0;
	std::unordered_map<uint64_t, obs_source_t *> sources_;
	std::unordered_map<uint64_t, obs_scene_t *> scenes_;
	ItemMap items_;
	std::shared_ptr<SourceV2State> source_v2_state_;
	std::shared_ptr<InteractionV2State> interaction_v2_state_;
	std::shared_ptr<MediaV2State> media_v2_state_;
};

} // namespace obs_engine
