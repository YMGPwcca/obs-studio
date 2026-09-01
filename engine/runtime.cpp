#include "runtime.hpp"

#include "protocol.hpp"
#include "validation.hpp"

#include <windows.h>

#include <graphics/vec2.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace obs_engine {
namespace {

constexpr size_t kMaxSourceTypeBytes = 128;
constexpr size_t kMaxSourceNameBytes = 256;
constexpr wchar_t kCaptureOnlyEnvironment[] = L"OBS_WIN_CAPTURE_CAPTURE_ONLY";

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

bool apply_legacy_transform_scalar(long long request_id, obs_data_t *request, const char *field, float &target,
					   double min_value, double max_value, const char *error_message)
{
	double value = 0.0;
	bool present = false;
	if (!read_finite_double(request, field, min_value, max_value, value, present)) {
		send_error(request_id, "bad_request", error_message);
		return false;
	}
	if (present)
		target = static_cast<float>(value);
	return true;
}

bool apply_legacy_transform_alignment(long long request_id, obs_data_t *request, obs_transform_info &info)
{
	long long alignment = 0;
	bool present = false;
	if (!read_integer(request, "alignment", alignment, present)) {
		send_error(request_id, "bad_request", "alignment must be an integer");
		return false;
	}
	if (!present)
		return true;
	const uint32_t allowed = OBS_ALIGN_LEFT | OBS_ALIGN_RIGHT | OBS_ALIGN_TOP | OBS_ALIGN_BOTTOM;
	if (alignment < 0 || static_cast<uint64_t>(alignment) > std::numeric_limits<uint32_t>::max() ||
	    (static_cast<uint32_t>(alignment) & ~allowed) != 0) {
		send_error(request_id, "bad_request", "invalid alignment");
		return false;
	}
	info.alignment = static_cast<uint32_t>(alignment);
	return true;
}

void remove_attached_scene_item(obs_sceneitem_t *item)
{
	if (!item)
		return;
	if (obs_sceneitem_get_scene(item))
		obs_sceneitem_remove(item);
}

} // namespace

Engine::Engine(Config config) : config_(std::move(config)) {}

Engine::~Engine()
{
	shutdown();
}

bool Engine::prepare_startup_environment()
{
	if (!SetEnvironmentVariableW(kCaptureOnlyEnvironment, config_.enable_game_capture ? nullptr : L"1")) {
		std::fprintf(stderr, "obs-engine: failed to configure win-capture mode\n");
		return false;
	}

	char *config_path = os_get_config_path_ptr("obs-engine/plugin-config");
	if (!config_path) {
		std::fprintf(stderr, "obs-engine: failed to determine config path\n");
		return false;
	}
	if (os_mkdirs(config_path) == MKDIR_ERROR) {
		std::fprintf(stderr, "obs-engine: failed to create config path\n");
		bfree(config_path);
		return false;
	}

	const bool started = obs_startup(config_.locale.c_str(), config_path, nullptr);
	bfree(config_path);
	if (!started) {
		std::fprintf(stderr, "obs-engine: obs_startup failed\n");
		return false;
	}

	for (const std::string &plugin : config_.plugins)
		obs_add_safe_module(plugin.c_str());
	return true;
}

bool Engine::reset_video()
{
	obs_video_info video = {};
	video.graphics_module = DL_D3D11;
	video.fps_num = config_.fps;
	video.fps_den = 1;
	video.base_width = config_.width;
	video.base_height = config_.height;
	video.output_width = config_.width;
	video.output_height = config_.height;
	video.output_format = VIDEO_FORMAT_BGRA;
	video.adapter = 0;
	video.gpu_conversion = false;
	video.colorspace = VIDEO_CS_SRGB;
	video.range = VIDEO_RANGE_FULL;
	video.scale_type = OBS_SCALE_BILINEAR;

	const int video_result = obs_reset_video(&video);
	if (video_result != OBS_VIDEO_SUCCESS) {
		std::fprintf(stderr, "obs-engine: obs_reset_video failed (%d)\n", video_result);
		return false;
	}
	return true;
}

bool Engine::load_runtime_modules()
{
	obs_module_failure_info failures = {};
	obs_load_all_modules2(&failures);
	if (failures.count != 0) {
		std::fprintf(stderr, "obs-engine: %zu module(s) failed while scanning\n", failures.count);
		for (size_t i = 0; i < failures.count; ++i) {
			if (failures.failed_modules && failures.failed_modules[i])
				std::fprintf(stderr, "  - %s\n", failures.failed_modules[i]);
		}
	}
	obs_module_failure_info_free(&failures);
	obs_post_load_modules();

	for (const std::string &plugin : config_.required_plugins) {
		if (!obs_get_module(plugin.c_str())) {
			std::fprintf(stderr, "obs-engine: required module '%s' did not load\n", plugin.c_str());
			return false;
		}
	}

	obs_log_loaded_modules();
	return true;
}

bool Engine::start()
{
	return start_runtime();
}

bool Engine::start_runtime()
{
	return prepare_startup_environment() && reset_video() && load_runtime_modules() && initialize_phase2_runtime();
}

bool Engine::handle(obs_data_t *request)
{
	long long request_id = 0;
	bool id_present = false;
	if (!read_integer(request, "id", request_id, id_present)) {
		send_error(0, "bad_request", "id must be an integer");
		return true;
	}

	const char *command = obs_data_get_string(request, "cmd");
	if (!is_safe_identifier(command, 64)) {
		send_error(id_present ? request_id : 0, "bad_request", "missing or invalid cmd");
		return true;
	}

	using LegacyCommandHandler = bool (Engine::*)(long long, obs_data_t *);
	struct LegacyCommand {
		const char *name;
		LegacyCommandHandler handler;
	};
	constexpr LegacyCommand commands[] = {
		{"hello", &Engine::command_hello},
		{"source.types", &Engine::command_source_types},
		{"source.defaults", &Engine::command_source_defaults},
		{"source.create", &Engine::command_source_create},
		{"source.update", &Engine::command_source_update},
		{"source.settings", &Engine::command_source_settings},
		{"source.destroy", &Engine::command_source_destroy},
		{"scene.create", &Engine::command_scene_create},
		{"scene.destroy", &Engine::command_scene_destroy},
		{"scene.add", &Engine::command_scene_add},
		{"item.remove", &Engine::command_item_remove},
		{"item.transform", &Engine::command_item_transform},
		{"program.set", &Engine::command_program_set},
	};
	for (const LegacyCommand &entry : commands) {
		if (std::strcmp(command, entry.name) == 0)
			return (this->*entry.handler)(request_id, request);
	}
	if (std::strcmp(command, "shutdown") == 0) {
		send_ok(request_id);
		return false;
	}

	send_error(request_id, "unsupported_command", "command is not supported by protocol v1");
	return true;
}

uint64_t Engine::allocate_handle()
{
	if (next_handle_ == 0 || next_handle_ > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
		throw std::runtime_error("handle space exhausted");
	return next_handle_++;
}

bool Engine::input_type_exists(const char *type) const
{
	const char *candidate = nullptr;
	for (size_t index = 0; obs_enum_input_types(index, &candidate); ++index) {
		if (candidate && std::strcmp(candidate, type) == 0)
			return true;
	}
	return false;
}

bool Engine::validate_source_type(long long request_id, obs_data_t *request, const char *&type) const
{
	type = obs_data_get_string(request, "type");
	if (!is_safe_identifier(type, kMaxSourceTypeBytes)) {
		send_error(request_id, "bad_request", "invalid source type");
		return false;
	}
	if (!input_type_exists(type)) {
		send_error(request_id, "not_found", "source type is not registered");
		return false;
	}
	return true;
}

bool Engine::command_hello(long long request_id, obs_data_t *)
{
	ObsDataPtr result(obs_data_create());
	obs_data_set_int(result.get(), "protocol", kProtocolVersion);
	obs_data_set_string(result.get(), "libobs_version", obs_get_version_string());
	obs_data_set_int(result.get(), "width", config_.width);
	obs_data_set_int(result.get(), "height", config_.height);
	obs_data_set_int(result.get(), "fps", config_.fps);
	obs_data_set_bool(result.get(), "game_capture_enabled", config_.enable_game_capture);

	ObsArrayPtr modules(obs_data_array_create());
	obs_enum_modules(
		[](void *param, obs_module_t *module) {
			auto *array = static_cast<obs_data_array_t *>(param);
			ObsDataPtr entry(obs_data_create());
			const char *file = obs_get_module_file_name(module);
			obs_data_set_string(entry.get(), "name", file ? file : "");
			obs_data_array_push_back(array, entry.get());
		},
		modules.get());
	obs_data_set_array(result.get(), "modules", modules.get());
	send_ok(request_id, result.get());
	return true;
}

bool Engine::command_source_types(long long request_id, obs_data_t *)
{
	ObsDataPtr result(obs_data_create());
	ObsArrayPtr types(obs_data_array_create());
	const char *type = nullptr;
	for (size_t index = 0; obs_enum_input_types(index, &type); ++index) {
		if (!type)
			continue;
		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "id", type);
		const char *display_name = obs_source_get_display_name(type);
		obs_data_set_string(entry.get(), "display_name", display_name ? display_name : type);
		obs_data_set_int(entry.get(), "output_flags", obs_get_source_output_flags(type));
		obs_module_t *module = obs_source_get_module(type);
		if (module) {
			const char *module_file = obs_get_module_file_name(module);
			if (module_file)
				obs_data_set_string(entry.get(), "module", module_file);
		}
		obs_data_array_push_back(types.get(), entry.get());
	}
	obs_data_set_array(result.get(), "types", types.get());
	send_ok(request_id, result.get());
	return true;
}

bool Engine::command_source_defaults(long long request_id, obs_data_t *request)
{
	const char *type = nullptr;
	if (!validate_source_type(request_id, request, type))
		return true;
	ObsDataPtr defaults(obs_get_source_defaults(type));
	if (!defaults) {
		send_error(request_id, "obs_error", "source type did not provide defaults");
		return true;
	}
	ObsDataPtr result(obs_data_create());
	obs_data_set_obj(result.get(), "settings", defaults.get());
	send_ok(request_id, result.get());
	return true;
}

bool Engine::command_source_create(long long request_id, obs_data_t *request)
{
	const char *type = nullptr;
	if (!validate_source_type(request_id, request, type))
		return true;

	const char *requested_name = obs_data_get_string(request, "name");
	if (requested_name && *requested_name && !is_bounded_string(requested_name, kMaxSourceNameBytes)) {
		send_error(request_id, "bad_request", "source name is too long");
		return true;
	}

	const uint64_t handle = allocate_handle();
	const std::string generated_name = "engine-source-" + std::to_string(handle);
	const char *name = requested_name && *requested_name ? requested_name : generated_name.c_str();
	ObsDataPtr settings(obs_data_get_obj(request, "settings"));
	obs_source_t *source = obs_source_create_private(type, name, settings.get());
	if (!source) {
		send_error(request_id, "obs_error", "obs_source_create_private failed");
		return true;
	}

	try {
		sources_.emplace(handle, source);
	} catch (...) {
		obs_source_release(source);
		throw;
	}

	ObsDataPtr result(obs_data_create());
	obs_data_set_int(result.get(), "source", static_cast<long long>(handle));
	obs_data_set_string(result.get(), "name", obs_source_get_name(source));
	obs_data_set_string(result.get(), "type", obs_source_get_id(source));
	send_ok(request_id, result.get());
	return true;
}

bool Engine::command_source_update(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "source", handle)) {
		send_error(request_id, "bad_request", "invalid source handle");
		return true;
	}
	auto it = sources_.find(handle);
	if (it == sources_.end()) {
		send_error(request_id, "not_found", "source handle was not found");
		return true;
	}
	ObsDataPtr settings(obs_data_get_obj(request, "settings"));
	if (!settings) {
		send_error(request_id, "bad_request", "settings object is required");
		return true;
	}
	obs_source_update(it->second, settings.get());
	send_ok(request_id);
	return true;
}

bool Engine::command_source_settings(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "source", handle)) {
		send_error(request_id, "bad_request", "invalid source handle");
		return true;
	}
	auto it = sources_.find(handle);
	if (it == sources_.end()) {
		send_error(request_id, "not_found", "source handle was not found");
		return true;
	}
	ObsDataPtr settings(obs_source_get_settings(it->second));
	if (!settings) {
		send_error(request_id, "obs_error", "could not read source settings");
		return true;
	}
	ObsDataPtr result(obs_data_create());
	obs_data_set_obj(result.get(), "settings", settings.get());
	send_ok(request_id, result.get());
	return true;
}

void Engine::release_item(ItemMap::iterator &it)
{
	item_handles_.erase(it->second.item);
	remove_attached_scene_item(it->second.item);
	obs_sceneitem_release(it->second.item);
	it = items_.erase(it);
}

void Engine::remove_items_for_source(uint64_t source_id)
{
	for (auto it = items_.begin(); it != items_.end();) {
		if (it->second.source_id == source_id)
			release_item(it);
		else
			++it;
	}
}

void Engine::remove_items_for_scene(uint64_t scene_id)
{
	for (auto it = items_.begin(); it != items_.end();) {
		if (it->second.scene_id == scene_id)
			release_item(it);
		else
			++it;
	}
}

bool Engine::command_source_destroy(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "source", handle)) {
		send_error(request_id, "bad_request", "invalid source handle");
		return true;
	}
	auto it = sources_.find(handle);
	if (it == sources_.end()) {
		send_error(request_id, "not_found", "source handle was not found");
		return true;
	}
	remove_items_for_source(handle);
	obs_source_release(it->second);
	sources_.erase(it);
	send_ok(request_id);
	return true;
}

bool Engine::command_scene_create(long long request_id, obs_data_t *request)
{
	const char *requested_name = obs_data_get_string(request, "name");
	if (requested_name && *requested_name && !is_bounded_string(requested_name, kMaxSourceNameBytes)) {
		send_error(request_id, "bad_request", "scene name is too long");
		return true;
	}

	const uint64_t handle = allocate_handle();
	const std::string generated_name = "engine-scene-" + std::to_string(handle);
	const char *name = requested_name && *requested_name ? requested_name : generated_name.c_str();
	obs_scene_t *scene = obs_scene_create_private(name);
	if (!scene) {
		send_error(request_id, "obs_error", "obs_scene_create_private failed");
		return true;
	}

	try {
		scenes_.emplace(handle, scene);
	} catch (...) {
		obs_scene_release(scene);
		throw;
	}

	ObsDataPtr result(obs_data_create());
	obs_data_set_int(result.get(), "scene", static_cast<long long>(handle));
	obs_data_set_string(result.get(), "name", obs_source_get_name(obs_scene_get_source(scene)));
	send_ok(request_id, result.get());
	return true;
}

bool Engine::command_scene_destroy(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "scene", handle)) {
		send_error(request_id, "bad_request", "invalid scene handle");
		return true;
	}
	auto it = scenes_.find(handle);
	if (it == scenes_.end()) {
		send_error(request_id, "not_found", "scene handle was not found");
		return true;
	}
	if (program_scene_ == handle) {
		obs_set_output_source(0, nullptr);
		program_scene_ = 0;
	}
	remove_items_for_scene(handle);
	obs_scene_release(it->second);
	scenes_.erase(it);
	send_ok(request_id);
	return true;
}

bool Engine::command_scene_add(long long request_id, obs_data_t *request)
{
	uint64_t scene_handle = 0;
	uint64_t source_handle = 0;
	if (!request_handle(request, "scene", scene_handle) || !request_handle(request, "source", source_handle)) {
		send_error(request_id, "bad_request", "scene and source handles are required");
		return true;
	}
	auto scene_it = scenes_.find(scene_handle);
	auto source_it = sources_.find(source_handle);
	if (scene_it == scenes_.end() || source_it == sources_.end()) {
		send_error(request_id, "not_found", "scene or source handle was not found");
		return true;
	}

	obs_sceneitem_t *item = obs_scene_add(scene_it->second, source_it->second);
	if (!item) {
		send_error(request_id, "obs_error", "obs_scene_add failed");
		return true;
	}
	obs_sceneitem_addref(item);

	const uint64_t item_handle = allocate_handle();
	try {
		items_.emplace(item_handle, ItemEntry{scene_handle, source_handle, item});
	} catch (...) {
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
		throw;
	}

	ObsDataPtr result(obs_data_create());
	obs_data_set_int(result.get(), "item", static_cast<long long>(item_handle));
	send_ok(request_id, result.get());
	return true;
}

bool Engine::command_item_remove(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "item", handle)) {
		send_error(request_id, "bad_request", "invalid item handle");
		return true;
	}
	auto it = items_.find(handle);
	if (it == items_.end()) {
		send_error(request_id, "not_found", "item handle was not found");
		return true;
	}
	release_item(it);
	send_ok(request_id);
	return true;
}

bool Engine::command_item_transform(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "item", handle)) {
		send_error(request_id, "bad_request", "invalid item handle");
		return true;
	}
	auto it = items_.find(handle);
	if (it == items_.end()) {
		send_error(request_id, "not_found", "item handle was not found");
		return true;
	}

	obs_transform_info info = {};
	obs_sceneitem_get_info2(it->second.item, &info);
	if (!apply_legacy_transform_scalar(request_id, request, "x", info.pos.x, -10000000.0, 10000000.0,
					   "invalid x") ||
	    !apply_legacy_transform_scalar(request_id, request, "y", info.pos.y, -10000000.0, 10000000.0,
					   "invalid y") ||
	    !apply_legacy_transform_scalar(request_id, request, "scale_x", info.scale.x, -10000.0, 10000.0,
					   "invalid scale_x") ||
	    !apply_legacy_transform_scalar(request_id, request, "scale_y", info.scale.y, -10000.0, 10000.0,
					   "invalid scale_y") ||
	    !apply_legacy_transform_scalar(request_id, request, "rotation", info.rot, -1000000.0, 1000000.0,
					   "invalid rotation") ||
	    !apply_legacy_transform_alignment(request_id, request, info))
		return true;

	obs_sceneitem_set_info2(it->second.item, &info);
	send_ok(request_id);
	return true;
}

bool Engine::command_program_set(long long request_id, obs_data_t *request)
{
	uint64_t handle = 0;
	if (!request_handle(request, "scene", handle, true)) {
		send_error(request_id, "bad_request", "scene handle is required");
		return true;
	}
	if (handle == 0) {
		obs_set_output_source(0, nullptr);
		program_scene_ = 0;
		send_ok(request_id);
		return true;
	}
	auto it = scenes_.find(handle);
	if (it == scenes_.end()) {
		send_error(request_id, "not_found", "scene handle was not found");
		return true;
	}
	obs_set_output_source(0, obs_scene_get_source(it->second));
	program_scene_ = handle;
	send_ok(request_id);
	return true;
}

void Engine::shutdown()
{
	if (!obs_initialized())
		return;

	shutdown_phase2_runtime();

	obs_set_output_source(0, nullptr);
	program_scene_ = 0;

	for (auto &[_, entry] : items_) {
		obs_sceneitem_remove(entry.item);
		obs_sceneitem_release(entry.item);
	}
	items_.clear();
	item_handles_.clear();

	for (auto &[_, scene] : scenes_)
		obs_scene_release(scene);
	scenes_.clear();

	for (auto &[_, source] : sources_)
		obs_source_release(source);
	sources_.clear();
	scene_canvases_.clear();
	v2_release_canvas_registry();

	obs_shutdown();
}

} // namespace obs_engine
