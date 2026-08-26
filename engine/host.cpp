#include "config.hpp"
#include "protocol.hpp"
#include "runtime.hpp"

#include <windows.h>

#include <obs.h>
#include <util/base.h>

#include <cstdio>
#include <cwchar>
#include <exception>
#include <string>
#include <vector>

namespace {

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
	const DWORD capacity = static_cast<DWORD>(path.size());
	const DWORD length = GetModuleFileNameW(nullptr, path.data(), capacity);
	if (length == 0 || length >= capacity)
		return false;

	wchar_t *slash = std::wcsrchr(path.data(), L'\\');
	if (!slash)
		slash = std::wcsrchr(path.data(), L'/');
	if (!slash)
		return false;

	*slash = L'\0';
	return SetCurrentDirectoryW(path.data()) != FALSE;
}

bool harden_dll_search_path()
{
	if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 |
				      LOAD_LIBRARY_SEARCH_USER_DIRS))
		return false;
	return SetDllDirectoryW(L"") != FALSE;
}

} // namespace

int main(int argc, char **argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	if (!pin_working_directory_to_executable()) {
		std::fprintf(stderr, "obs-engine: failed to pin working directory to executable directory\n");
		return 2;
	}
	if (!harden_dll_search_path()) {
		std::fprintf(stderr, "obs-engine: failed to harden DLL search path\n");
		return 2;
	}

	base_set_log_handler(obs_log_handler, nullptr);

	obs_engine::Config config;
	if (!obs_engine::parse_args(argc, argv, config)) {
		std::fprintf(stderr, "obs-engine: invalid command-line arguments\n");
		obs_engine::print_help();
		return 2;
	}
	if (config.help) {
		obs_engine::print_help();
		return 0;
	}

	std::vector<const char *> obs_args;
	obs_args.reserve(static_cast<size_t>(argc));
	for (int i = 0; i < argc; ++i)
		obs_args.push_back(argv[i]);
	obs_set_cmdline_args(argc, obs_args.data());

	try {
		obs_engine::Engine engine(config);
		if (!engine.start())
			return 3;

		obs_engine::send_ready_event(config);
		std::string line;
		for (;;) {
			const obs_engine::ReadLineResult read_result = obs_engine::read_line_limited(line);
			if (read_result == obs_engine::ReadLineResult::Eof)
				break;
			if (read_result == obs_engine::ReadLineResult::TooLong) {
				obs_engine::send_error(0, "message_too_large", "request exceeds the protocol size limit");
				continue;
			}
			if (line.empty())
				continue;

			obs_engine::ObsDataPtr request(obs_data_create_from_json(line.c_str()));
			if (!request) {
				obs_engine::send_error(0, "invalid_json", "request is not valid JSON");
				continue;
			}

			bool keep_running = true;
			try {
				keep_running = engine.handle(request.get());
			} catch (const std::exception &error) {
				std::fprintf(stderr, "obs-engine: command failed internally: %s\n", error.what());
				obs_engine::send_error(0, "internal_error", "command failed internally");
			} catch (...) {
				std::fprintf(stderr, "obs-engine: command failed with an unknown exception\n");
				obs_engine::send_error(0, "internal_error", "command failed internally");
			}
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
