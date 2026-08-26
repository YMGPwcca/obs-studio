#include "config.hpp"
#include "validation.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace obs_engine {
namespace {

constexpr size_t kMaxPluginNameBytes = 64;
constexpr uint32_t kMinDimension = 16;
constexpr uint32_t kMaxDimension = 16384;
constexpr uint32_t kMaxFps = 240;

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

} // namespace

bool parse_args(int argc, char **argv, Config &config)
{
	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];
		if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
			config.help = true;
			continue;
		}
		if (std::strcmp(arg, "--enable-game-capture") == 0) {
			config.enable_game_capture = true;
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
			config.required_plugins.emplace_back(plugin);
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
	std::sort(config.required_plugins.begin(), config.required_plugins.end());
	config.required_plugins.erase(std::unique(config.required_plugins.begin(), config.required_plugins.end()),
				      config.required_plugins.end());
	return true;
}

void print_help()
{
	std::fputs("obs-engine - minimal libobs host\n"
		   "  --width=N              Base/output width (16..16384)\n"
		   "  --height=N             Base/output height (16..16384)\n"
		   "  --fps=N                Frame rate (1..240)\n"
		   "  --locale=NAME          OBS module locale (default en-US)\n"
		   "  --plugin=NAME          Add/require an OBS module in the safe-module allowlist\n"
		   "  --enable-game-capture  Enable win-capture hook/update initialization\n"
		   "\nAll Windows runtime modules built by this branch are allowlisted by default.\n"
		   "Game Capture is disabled by default.\n"
		   "Protocol: one JSON object per line on stdin/stdout. Logs go to stderr.\n",
		   stderr);
}

} // namespace obs_engine
