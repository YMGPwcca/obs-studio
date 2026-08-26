#pragma once

#include "config.hpp"

#include <obs.h>

#include <cstdint>
#include <unordered_map>

namespace obs_engine {

struct ItemEntry {
	uint64_t scene_id = 0;
	uint64_t source_id = 0;
	obs_sceneitem_t *item = nullptr;
};

class Engine {
public:
	explicit Engine(Config config);
	~Engine();

	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;

	bool start();
	bool handle(obs_data_t *request);

private:
	using ItemMap = std::unordered_map<uint64_t, ItemEntry>;

	uint64_t allocate_handle();
	bool input_type_exists(const char *type) const;
	bool validate_source_type(long long request_id, obs_data_t *request, const char *&type) const;

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
};

} // namespace obs_engine
