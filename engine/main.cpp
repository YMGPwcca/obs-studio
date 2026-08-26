#include <windows.h>

#include <obs.h>
#include <graphics/vec2.h>
#include <util/base.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kProtocolVersion = 1;
constexpr size_t kMaxMessageBytes = 256 * 1024;
constexpr size_t kMaxSourceTypeBytes = 128;
constexpr size_t kMaxSourceNameBytes = 256;
constexpr size_t kMaxPluginNameBytes = 64;
constexpr uint32_t kMinDimension = 16;
constexpr uint32_t kMaxDimension = 16384;
constexpr uint32_t kMaxFps = 240;

struct Config {
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t fps = 60;
	std::string locale = "en-US";
	std::vector<std::string> plugins = {"win-capture"};
	bool help = false;
};

struct ItemEntry {
	uint64_t scene_id = 0;
	uint64_t source_id = 0;
	obs_sceneitem_t *item = nullptr;
};

enum class ReadLineResult { Ok, Eof, TooLong };

const char *log_level_name(int level)
{
	switch (level) {
	case LOG_ERROR:
		return "error";
	case LOG_WARNING:
		return "warning";
	case LOG_INFO:
		return "info";
	case LOG_DEBUG:
		return "debug";
	default:
		return "unknown";
	}
}

void obs_log_handler(int level, const char *format, va_list args, void *)
{
	char message[4096] = {};
	std::vsnprintf(message, sizeof(message), format, args);
	std::fprintf(stderr, "[libobs:%s] %s\n", log_level_name(level), message);
	std::fflush(stderr);
}

bool pin_working_directory_to_executable()
{
	std::vector<wchar_t> path(32768);
	const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size())
		return false;

	wchar_t *slash = std::wcsrchr(path.data(), L'\\');
	if (!slash)
		slash = std::wcsrchr(path.data(), L'/');
	if (!slash)
		return false;

	*slash = L'\0';
	return SetCurrentDirectoryW(path.data()) != FALSE;
}

bool is_safe_identifier(const char *value, size_t max_bytes)
{
	if (!value || !*value)
		return false;

	size_t length = 0;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
		if (++length > max_bytes)
			return false;
		const bool alpha_num = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
				       (*p >= '0' && *p <= '9');
		if (!alpha_num && *p != '_' && *p != '-' && *p != '.')
			return false;
	}

	return true;
}

bool is_bounded_string(const char *value, size_t max_bytes)
{
	if (!value)
		return false;
	return std::strnlen(value, max_bytes + 1) <= max_bytes;
}

bool parse_u32(const char *text, uint32_t min_value, uint32_t max_value, uint32_t &out)
{
	if (!text || !*text || *text == '-')
		return false;

	errno = 0;
	char *end = nullptr;
	const unsigned long value = std::strtoul(text, &end, 10);
	if (errno != 0 || !end || *end != '\0' || value < min_value || value > max_value)
		return false;

	out = static_cast<uint32_t>(value);
	return true;
}

bool parse_args(int argc, char **argv, Config &config)
{
	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];
		if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
			config.help = true;
			continue;
		}

		constexpr const char *width_prefix = "--width=";
		constexpr const char *height_prefix = "--height=";
		constexpr const char *fps_prefix = "--fps=";
		constexpr const char *plugin_prefix = "--plugin=";
		constexpr const char *locale_prefix = "--locale=";

		if (std::strncmp(arg, width_prefix, std::strlen(width_prefix)) == 0) {
			if (!parse_u32(arg + std::strlen(width_prefix), kMinDimension, kMaxDimension, config.width))
				return false;
		} else if (std::strncmp(arg, height_prefix, std::strlen(height_prefix)) == 0) {
			if (!parse_u32(arg + std::strlen(height_prefix), kMinDimension, kMaxDimension, config.height))
				return false;
		} else if (std::strncmp(arg, fps_prefix, std::strlen(fps_prefix)) == 0) {
			if (!parse_u32(arg + std::strlen(fps_prefix), 1, kMaxFps, config.fps))
				return false;
		} else if (std::strncmp(arg, plugin_prefix, std::strlen(plugin_prefix)) == 0) {
			const char *plugin = arg + std::strlen(plugin_prefix);
			if (!is_safe_identifier(plugin, kMaxPluginNameBytes))
				return false;
			config.plugins.emplace_back(plugin);
		} else if (std::strncmp(arg, locale_prefix, std::strlen(locale_prefix)) == 0) {
			const char *locale = arg + std::strlen(locale_prefix);
			if (!is_safe_identifier(locale, 32))
				return false;
			config.locale = locale;
		} else {
			return false;
		}
	}

	std::sort(config.plugins.begin(), config.plugins.end());
	config.plugins.erase(std::unique(config.plugins.begin(), config.plugins.end()), config.plugins.end());
	return true;
}

ReadLineResult read_line_limited(std::string &line)
{
	line.clear();
	bool too_long = false;
	bool saw_data = false;

	for (;;) {
		const int ch = std::fgetc(stdin);
		if (ch == EOF) {
			if (!saw_data)
				return ReadLineResult::Eof;
			break;
		}

		saw_data = true;
		if (ch == '\n')
			break;

		if (!too_long) {
			if (line.size() >= kMaxMessageBytes) {
				too_long = true;
			} else {
				line.push_back(static_cast<char>(ch));
			}
		}
	}

	if (!line.empty() && line.back() == '\r')
		line.pop_back();

	return too_long ? ReadLineResult::TooLong : ReadLineResult::Ok;
}

void write_json(obs_data_t *data)
{
	const char *json = obs_data_get_json(data);
	if (!json)
		json = "{}";
	std::fputs(json, stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

void send_error(long long request_id, const char *code, const char *message)
{
	obs_data_t *response = obs_data_create();
	obs_data_set_int(response, "id", request_id);
	obs_data_set_bool(response, "ok", false);
	obs_data_set_string(response, "error", code);
	obs_data_set_string(response, "message", message);
	write_json(response);
	obs_data_release(response);
}

void send_ok(long long request_id, obs_data_t *result = nullptr)
{
	obs_data_t *response = obs_data_create();
	obs_data_set_int(response, "id", request_id);
	obs_data_set_bool(response, "ok", true);
	if (result)
		obs_data_set_obj(response, "result", result);
	write_json(response);
	obs_data_release(response);
}

void send_ready_event(const Config &config)
{
	obs_data_t *event = obs_data_create();
	obs_data_set_string(event, "event", "ready");
	obs_data_set_int(event, "protocol", kProtocolVersion);
	obs_data_set_string(event, "libobs_version", obs_get_version_string());
	obs_data_set_int(event, "pid", GetCurrentProcessId());
	obs_data_set_int(event, "width", config.width);
	obs_data_set_int(event, "height", config.height);
	obs_data_set_int(event, "fps", config.fps);
	write_json(event);
	obs_data_release(event);
}

bool request_handle(obs_data_t *request, const char *field, uint64_t &out, bool allow_zero = false)
{
	if (!obs_data_has_user_value(request, field))
		return false;

	const long long raw = obs_data_get_int(request, field);
	if (raw < 0 || (!allow_zero && raw == 0))
		return false;

	out = static_cast<uint64_t>(raw);
	return true;
}

bool read_finite_double(obs_data_t *request, const char *field, double min_value, double max_value, double &out,
			bool &present)
{
	present = obs_data_has_user_value(request, field);
	if (!present)
		return true;

	const double value = obs_data_get_double(request, field);
	if (!std::isfinite(value) || value < min_value || value > max_value)
		return false;

	out = value;
	return true;
}

class Engine {
public:
	explicit Engine(Config config) : config_(std::move(config)) {}
	~Engine() { shutdown(); }

	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;

	bool start()
	{
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

		for (const std::string &plugin : config_.plugins) {
			if (!obs_get_module(plugin.c_str())) {
				std::fprintf(stderr, "obs-engine: required module '%s' did not load\n", plugin.c_str());
				return false;
			}
		}

		obs_log_loaded_modules();
		started_ = true;
		return true;
	}

	bool handle(obs_data_t *request)
	{
		const long long request_id = obs_data_has_user_value(request, "id") ? obs_data_get_int(request, "id") : 0;
		const char *command = obs_data_get_string(request, "cmd");
		if (!command || !*command || !is_safe_identifier(command, 64)) {
			send_error(request_id, "bad_request", "missing or invalid cmd");
			return true;
		}

		if (std::strcmp(command, "hello") == 0)
			return command_hello(request_id);
		if (std::strcmp(command, "source.types") == 0)
			return command_source_types(request_id);
		if (std::strcmp(command, "source.defaults") == 0)
			return command_source_defaults(request_id, request);
		if (std::strcmp(command, "source.create") == 0)
			return command_source_create(request_id, request);
		if (std::strcmp(command, "source.update") == 0)
			return command_source_update(request_id, request);
		if (std::strcmp(command, "source.settings") == 0)
			return command_source_settings(request_id, request);
		if (std::strcmp(command, "source.destroy") == 0)
			return command_source_destroy(request_id, request);
		if (std::strcmp(command, "scene.create") == 0)
			return command_scene_create(request_id, request);
		if (std::strcmp(command, "scene.destroy") == 0)
			return command_scene_destroy(request_id, request);
		if (std::strcmp(command, "scene.add") == 0)
			return command_scene_add(request_id, request);
		if (std::strcmp(command, "item.remove") == 0)
			return command_item_remove(request_id, request);
		if (std::strcmp(command, "item.transform") == 0)
			return command_item_transform(request_id, request);
		if (std::strcmp(command, "program.set") == 0)
			return command_program_set(request_id, request);
		if (std::strcmp(command, "shutdown") == 0) {
			send_ok(request_id);
			return false;
		}

		send_error(request_id, "unsupported_command", "command is not supported by protocol v1");
		return true;
	}

private:
	uint64_t allocate_handle()
	{
		if (next_handle_ == 0 || next_handle_ > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
			throw std::runtime_error("handle space exhausted");
		return next_handle_++;
	}

	bool input_type_exists(const char *type) const
	{
		const char *candidate = nullptr;
		for (size_t index = 0; obs_enum_input_types(index, &candidate); ++index) {
			if (candidate && std::strcmp(candidate, type) == 0)
				return true;
		}
		return false;
	}

	bool validate_source_type(long long request_id, obs_data_t *request, const char *&type) const
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

	bool command_hello(long long request_id)
	{
		obs_data_t *result = obs_data_create();
		obs_data_set_int(result, "protocol", kProtocolVersion);
		obs_data_set_string(result, "libobs_version", obs_get_version_string());
		obs_data_set_int(result, "width", config_.width);
		obs_data_set_int(result, "height", config_.height);
		obs_data_set_int(result, "fps", config_.fps);

		obs_data_array_t *modules = obs_data_array_create();
		obs_enum_modules(
			[](void *param, obs_module_t *module) {
				auto *array = static_cast<obs_data_array_t *>(param);
				obs_data_t *entry = obs_data_create();
				const char *file = obs_get_module_file_name(module);
				obs_data_set_string(entry, "name", file ? file : "");
				obs_data_array_push_back(array, entry);
				obs_data_release(entry);
			},
			modules);
		obs_data_set_array(result, "modules", modules);
		obs_data_array_release(modules);

		send_ok(request_id, result);
		obs_data_release(result);
		return true;
	}

	bool command_source_types(long long request_id)
	{
		obs_data_t *result = obs_data_create();
		obs_data_array_t *types = obs_data_array_create();

		const char *type = nullptr;
		for (size_t index = 0; obs_enum_input_types(index, &type); ++index) {
			if (!type)
				continue;
			obs_data_t *entry = obs_data_create();
			obs_data_set_string(entry, "id", type);
			const char *display_name = obs_source_get_display_name(type);
			obs_data_set_string(entry, "display_name", display_name ? display_name : type);
			obs_data_set_int(entry, "output_flags", obs_get_source_output_flags(type));
			obs_module_t *module = obs_source_get_module(type);
			if (module) {
				const char *module_file = obs_get_module_file_name(module);
				if (module_file)
					obs_data_set_string(entry, "module", module_file);
			}
			obs_data_array_push_back(types, entry);
			obs_data_release(entry);
		}

		obs_data_set_array(result, "types", types);
		obs_data_array_release(types);
		send_ok(request_id, result);
		obs_data_release(result);
		return true;
	}

	bool command_source_defaults(long long request_id, obs_data_t *request)
	{
		const char *type = nullptr;
		if (!validate_source_type(request_id, request, type))
			return true;

		obs_data_t *defaults = obs_get_source_defaults(type);
		if (!defaults) {
			send_error(request_id, "obs_error", "source type did not provide defaults");
			return true;
		}

		obs_data_t *result = obs_data_create();
		obs_data_set_obj(result, "settings", defaults);
		send_ok(request_id, result);
		obs_data_release(result);
		obs_data_release(defaults);
		return true;
	}

	bool command_source_create(long long request_id, obs_data_t *request)
	{
		const char *type = nullptr;
		if (!validate_source_type(request_id, request, type))
			return true;

		const uint64_t handle = allocate_handle();
		const char *requested_name = obs_data_get_string(request, "name");
		if (requested_name && *requested_name && !is_bounded_string(requested_name, kMaxSourceNameBytes)) {
			send_error(request_id, "bad_request", "source name is too long");
			return true;
		}

		const std::string generated_name = "engine-source-" + std::to_string(handle);
		const char *name = requested_name && *requested_name ? requested_name : generated_name.c_str();
		obs_data_t *settings = obs_data_get_obj(request, "settings");
		obs_source_t *source = obs_source_create(type, name, settings, nullptr);
		if (settings)
			obs_data_release(settings);
		if (!source) {
			send_error(request_id, "obs_error", "obs_source_create failed");
			return true;
		}

		try {
			sources_.emplace(handle, source);
		} catch (...) {
			obs_source_release(source);
			throw;
		}

		obs_data_t *result = obs_data_create();
		obs_data_set_int(result, "source", static_cast<long long>(handle));
		obs_data_set_string(result, "name", obs_source_get_name(source));
		obs_data_set_string(result, "type", obs_source_get_id(source));
		send_ok(request_id, result);
		obs_data_release(result);
		return true;
	}

	bool command_source_update(long long request_id, obs_data_t *request)
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

		obs_data_t *settings = obs_data_get_obj(request, "settings");
		if (!settings) {
			send_error(request_id, "bad_request", "settings object is required");
			return true;
		}
		obs_source_update(it->second, settings);
		obs_data_release(settings);
		send_ok(request_id);
		return true;
	}

	bool command_source_settings(long long request_id, obs_data_t *request)
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

		obs_data_t *settings = obs_source_get_settings(it->second);
		if (!settings) {
			send_error(request_id, "obs_error", "could not read source settings");
			return true;
		}
		obs_data_t *result = obs_data_create();
		obs_data_set_obj(result, "settings", settings);
		send_ok(request_id, result);
		obs_data_release(result);
		obs_data_release(settings);
		return true;
	}

	void remove_items_for_source(uint64_t source_id)
	{
		for (auto it = items_.begin(); it != items_.end();) {
			if (it->second.source_id == source_id) {
				obs_sceneitem_remove(it->second.item);
				it = items_.erase(it);
			} else {
				++it;
			}
		}
	}

	void remove_items_for_scene(uint64_t scene_id)
	{
		for (auto it = items_.begin(); it != items_.end();) {
			if (it->second.scene_id == scene_id) {
				obs_sceneitem_remove(it->second.item);
				it = items_.erase(it);
			} else {
				++it;
			}
		}
	}

	bool command_source_destroy(long long request_id, obs_data_t *request)
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

	bool command_scene_create(long long request_id, obs_data_t *request)
	{
		const uint64_t handle = allocate_handle();
		const char *requested_name = obs_data_get_string(request, "name");
		if (requested_name && *requested_name && !is_bounded_string(requested_name, kMaxSourceNameBytes)) {
			send_error(request_id, "bad_request", "scene name is too long");
			return true;
		}

		const std::string generated_name = "engine-scene-" + std::to_string(handle);
		const char *name = requested_name && *requested_name ? requested_name : generated_name.c_str();
		obs_scene_t *scene = obs_scene_create(name);
		if (!scene) {
			send_error(request_id, "obs_error", "obs_scene_create failed");
			return true;
		}

		try {
			scenes_.emplace(handle, scene);
		} catch (...) {
			obs_scene_release(scene);
			throw;
		}

		obs_data_t *result = obs_data_create();
		obs_data_set_int(result, "scene", static_cast<long long>(handle));
		obs_data_set_string(result, "name", obs_source_get_name(obs_scene_get_source(scene)));
		send_ok(request_id, result);
		obs_data_release(result);
		return true;
	}

	bool command_scene_destroy(long long request_id, obs_data_t *request)
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

	bool command_scene_add(long long request_id, obs_data_t *request)
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

		const uint64_t item_handle = allocate_handle();
		try {
			items_.emplace(item_handle, ItemEntry{scene_handle, source_handle, item});
		} catch (...) {
			obs_sceneitem_remove(item);
			throw;
		}

		obs_data_t *result = obs_data_create();
		obs_data_set_int(result, "item", static_cast<long long>(item_handle));
		send_ok(request_id, result);
		obs_data_release(result);
		return true;
	}

	bool command_item_remove(long long request_id, obs_data_t *request)
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

		obs_sceneitem_remove(it->second.item);
		items_.erase(it);
		send_ok(request_id);
		return true;
	}

	bool command_item_transform(long long request_id, obs_data_t *request)
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

		double value = 0.0;
		bool present = false;
		if (!read_finite_double(request, "x", -10000000.0, 10000000.0, value, present)) {
			send_error(request_id, "bad_request", "invalid x");
			return true;
		}
		if (present)
			info.pos.x = static_cast<float>(value);

		if (!read_finite_double(request, "y", -10000000.0, 10000000.0, value, present)) {
			send_error(request_id, "bad_request", "invalid y");
			return true;
		}
		if (present)
			info.pos.y = static_cast<float>(value);

		if (!read_finite_double(request, "scale_x", -10000.0, 10000.0, value, present)) {
			send_error(request_id, "bad_request", "invalid scale_x");
			return true;
		}
		if (present)
			info.scale.x = static_cast<float>(value);

		if (!read_finite_double(request, "scale_y", -10000.0, 10000.0, value, present)) {
			send_error(request_id, "bad_request", "invalid scale_y");
			return true;
		}
		if (present)
			info.scale.y = static_cast<float>(value);

		if (!read_finite_double(request, "rotation", -1000000.0, 1000000.0, value, present)) {
			send_error(request_id, "bad_request", "invalid rotation");
			return true;
		}
		if (present)
			info.rot = static_cast<float>(value);

		if (obs_data_has_user_value(request, "alignment")) {
			const long long alignment = obs_data_get_int(request, "alignment");
			const uint32_t allowed = OBS_ALIGN_LEFT | OBS_ALIGN_RIGHT | OBS_ALIGN_TOP | OBS_ALIGN_BOTTOM;
			if (alignment < 0 || static_cast<uint64_t>(alignment) > std::numeric_limits<uint32_t>::max() ||
			    (static_cast<uint32_t>(alignment) & ~allowed) != 0) {
				send_error(request_id, "bad_request", "invalid alignment");
				return true;
			}
			info.alignment = static_cast<uint32_t>(alignment);
		}

		obs_sceneitem_set_info2(it->second.item, &info);
		send_ok(request_id);
		return true;
	}

	bool command_program_set(long long request_id, obs_data_t *request)
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

	void shutdown()
	{
		if (!obs_initialized())
			return;

		obs_set_output_source(0, nullptr);
		program_scene_ = 0;

		for (auto &[_, entry] : items_)
			obs_sceneitem_remove(entry.item);
		items_.clear();

		for (auto &[_, scene] : scenes_)
			obs_scene_release(scene);
		scenes_.clear();

		for (auto &[_, source] : sources_)
			obs_source_release(source);
		sources_.clear();

		obs_shutdown();
		started_ = false;
	}

	Config config_;
	bool started_ = false;
	uint64_t next_handle_ = 1;
	uint64_t program_scene_ = 0;
	std::unordered_map<uint64_t, obs_source_t *> sources_;
	std::unordered_map<uint64_t, obs_scene_t *> scenes_;
	std::unordered_map<uint64_t, ItemEntry> items_;
};

void print_help()
{
	std::fputs("obs-engine - minimal libobs host\n"
		   "  --width=N       Base/output width (16..16384)\n"
		   "  --height=N      Base/output height (16..16384)\n"
		   "  --fps=N         Frame rate (1..240)\n"
		   "  --locale=NAME   OBS module locale (default en-US)\n"
		   "  --plugin=NAME   Add an OBS module to the safe-module allowlist\n"
		   "\nThe default and mandatory first module is win-capture.\n"
		   "Protocol: one JSON object per line on stdin/stdout. Logs go to stderr.\n",
		   stderr);
}

} // namespace

int main(int argc, char **argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	if (!pin_working_directory_to_executable()) {
		std::fprintf(stderr, "obs-engine: failed to pin working directory to executable directory\n");
		return 2;
	}

	// Remove the current working directory from the implicit DLL dependency
	// search path. The application directory remains available to Windows.
	if (!SetDllDirectoryW(L"")) {
		std::fprintf(stderr, "obs-engine: failed to harden DLL search path\n");
		return 2;
	}

	base_set_log_handler(obs_log_handler, nullptr);
	obs_set_cmdline_args(argc, const_cast<const char *const *>(argv));

	Config config;
	if (!parse_args(argc, argv, config)) {
		std::fprintf(stderr, "obs-engine: invalid command-line arguments\n");
		print_help();
		return 2;
	}
	if (config.help) {
		print_help();
		return 0;
	}

	try {
		Engine engine(config);
		if (!engine.start())
			return 3;

		send_ready_event(config);

		std::string line;
		for (;;) {
			const ReadLineResult read_result = read_line_limited(line);
			if (read_result == ReadLineResult::Eof)
				break;
			if (read_result == ReadLineResult::TooLong) {
				send_error(0, "message_too_large", "request exceeds the protocol size limit");
				continue;
			}
			if (line.empty())
				continue;

			obs_data_t *request = obs_data_create_from_json(line.c_str());
			if (!request) {
				send_error(0, "invalid_json", "request is not valid JSON");
				continue;
			}

			bool keep_running = true;
			try {
				keep_running = engine.handle(request);
			} catch (const std::exception &error) {
				const long long request_id =
					obs_data_has_user_value(request, "id") ? obs_data_get_int(request, "id") : 0;
				std::fprintf(stderr, "obs-engine: command failed internally: %s\n", error.what());
				send_error(request_id, "internal_error", "command failed internally");
			} catch (...) {
				const long long request_id =
					obs_data_has_user_value(request, "id") ? obs_data_get_int(request, "id") : 0;
				std::fprintf(stderr, "obs-engine: command failed with an unknown exception\n");
				send_error(request_id, "internal_error", "command failed internally");
			}

			obs_data_release(request);
			if (!keep_running)
				break;
		}
	} catch (const std::exception &error) {
		std::fprintf(stderr, "obs-engine: fatal error: %s\n", error.what());
		return 4;
	} catch (...) {
		std::fprintf(stderr, "obs-engine: fatal unknown error\n");
		return 4;
	}

	return 0;
}
