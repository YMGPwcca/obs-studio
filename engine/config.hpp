#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace obs_engine {

struct Config {
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t fps = 60;
	std::string locale = "en-US";
	std::vector<std::string> plugins = {
		"aja",
		"coreaudio-encoder",
		"decklink",
		"image-source",
		"nv-filters",
		"obs-browser",
		"obs-ffmpeg",
		"obs-filters",
		"obs-libfdk",
		"obs-nvenc",
		"obs-outputs",
		"obs-qsv11",
		"obs-text",
		"obs-transitions",
		"obs-vst",
		"obs-webrtc",
		"obs-websocket",
		"obs-x264",
		"rtmp-services",
		"text-freetype2",
		"vlc-video",
		"win-capture",
		"win-dshow",
		"win-wasapi",
	};
	std::vector<std::string> required_plugins = {"win-capture"};
	bool enable_game_capture = false;
	bool help = false;
};

bool parse_args(int argc, char **argv, Config &config);
void print_help();

} // namespace obs_engine
