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

enum class ArgumentResult { NotMatched, Parsed, Invalid };

struct NumericArgument {
	const char *prefix;
	uint32_t min_value;
	uint32_t max_value;
	uint32_t Config::*field;
};

constexpr NumericArgument kNumericArguments[] = {
	{"--width=", kMinDimension, kMaxDimension, &Config::width},
	{"--height=", kMinDimension, kMaxDimension, &Config::height},
	{"--fps=", 1, kMaxFps, &Config::fps},
};

ArgumentResult parse_numeric_argument(const char *arg, Config &config)
{
	for (const NumericArgument &option : kNumericArguments) {
		const size_t prefix_length = std::strlen(option.prefix);
		if (std::strncmp(arg, option.prefix, prefix_length) == 0)
			return parse_u32(arg + prefix_length, option.min_value, option.max_value, config.*(option.field))
				       ? ArgumentResult::Parsed
				       : ArgumentResult::Invalid;
	}
	return ArgumentResult::NotMatched;
}

ArgumentResult parse_plugin_argument(const char *arg, Config &config)
{
	constexpr const char *prefix = "--plugin=";
	const size_t prefix_length = std::strlen(prefix);
	if (std::strncmp(arg, prefix, prefix_length) != 0)
		return ArgumentResult::NotMatched;
	const char *plugin = arg + prefix_length;
	if (!is_safe_identifier(plugin, kMaxPluginNameBytes))
		return ArgumentResult::Invalid;
	config.plugins.emplace_back(plugin);
	config.required_plugins.emplace_back(plugin);
	return ArgumentResult::Parsed;
}

ArgumentResult parse_locale_argument(const char *arg, Config &config)
{
	constexpr const char *prefix = "--locale=";
	const size_t prefix_length = std::strlen(prefix);
	if (std::strncmp(arg, prefix, prefix_length) != 0)
		return ArgumentResult::NotMatched;
	const char *locale = arg + prefix_length;
	if (!is_safe_identifier(locale, 32))
		return ArgumentResult::Invalid;
	config.locale = locale;
	return ArgumentResult::Parsed;
}

ArgumentResult parse_value_argument(const char *arg, Config &config)
{
	const ArgumentResult numeric = parse_numeric_argument(arg, config);
	if (numeric != ArgumentResult::NotMatched)
		return numeric;
	const ArgumentResult plugin = parse_plugin_argument(arg, config);
	if (plugin != ArgumentResult::NotMatched)
		return plugin;
	return parse_locale_argument(arg, config);
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
#ifdef OBS_PHASE2_TEST_HOOKS
		if (std::strcmp(arg, "--test-fail-next-canvas-reset") == 0) {
			config.test_fail_next_canvas_reset = true;
			continue;
		}
#endif

		const ArgumentResult value = parse_value_argument(arg, config);
		if (value == ArgumentResult::Parsed)
			continue;
		return false;
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
