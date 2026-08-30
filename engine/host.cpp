#include "config.hpp"
#include "events.hpp"
#include "protocol.hpp"
#include "protocol_v2.hpp"
#include "revision.hpp"
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

class ProtocolWriterScope {
public:
	ProtocolWriterScope() = default;

	bool start()
	{
		started_ = obs_engine::start_protocol_writer();
		return started_;
	}

	~ProtocolWriterScope()
	{
		if (started_)
			obs_engine::stop_protocol_writer();
	}

	ProtocolWriterScope(const ProtocolWriterScope &) = delete;
	ProtocolWriterScope &operator=(const ProtocolWriterScope &) = delete;

private:
	bool started_ = false;
};

class SourceEventBridgeScope {
public:
	SourceEventBridgeScope(obs_engine::Engine &engine, obs_engine::RevisionState &revisions,
			       obs_engine::EventDispatcher &events)
		: engine_(engine)
	{
		engine_.v2_bind_source_events(&revisions, &events);
		engine_.v2_sync_source_observers();
	}

	~SourceEventBridgeScope()
	{
		engine_.v2_prepare_shutdown();
	}

	SourceEventBridgeScope(const SourceEventBridgeScope &) = delete;
	SourceEventBridgeScope &operator=(const SourceEventBridgeScope &) = delete;

private:
	obs_engine::Engine &engine_;
};

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

void send_ready_event(const obs_engine::Config &config)
{
	obs_engine::ObsDataPtr event(obs_data_create());
	obs_data_set_string(event.get(), "event", "ready");
	obs_data_set_int(event.get(), "protocol", obs_engine::kProtocolVersion);
	obs_data_set_string(event.get(), "libobs_version", obs_get_version_string());
	obs_data_set_int(event.get(), "pid", static_cast<long long>(GetCurrentProcessId()));
	obs_data_set_int(event.get(), "width", config.width);
	obs_data_set_int(event.get(), "height", config.height);
	obs_data_set_int(event.get(), "fps", config.fps);
	obs_data_set_bool(event.get(), "game_capture_enabled", config.enable_game_capture);
	obs_engine::write_json(event.get());
}

bool has_field(obs_data_t *request, const char *name)
{
	obs_data_item_t *item = obs_data_item_byname(request, name);
	if (!item)
		return false;
	obs_data_item_release(&item);
	return true;
}

bool field_is_string(obs_data_t *request, const char *name)
{
	obs_data_item_t *item = obs_data_item_byname(request, name);
	if (!item)
		return false;
	const bool is_string = obs_data_item_gettype(item) == OBS_DATA_STRING;
	obs_data_item_release(&item);
	return is_string;
}

bool looks_like_v2_request(obs_data_t *request)
{
	if (has_field(request, "cmd"))
		return false;
	return has_field(request, "op") || has_field(request, "method") || field_is_string(request, "id");
}

bool dispatch_request(obs_engine::Engine &engine, const obs_engine::Config &config, obs_engine::RevisionState &revisions,
		      obs_engine::EventDispatcher &events, obs_data_t *request)
{
	if (!looks_like_v2_request(request)) {
		try {
			return engine.handle(request);
		} catch (const std::exception &error) {
			std::fprintf(stderr, "obs-engine: command failed internally: %s\n", error.what());
			obs_engine::send_error(0, "internal_error", "command failed internally");
		} catch (...) {
			std::fprintf(stderr, "obs-engine: command failed with an unknown exception\n");
			obs_engine::send_error(0, "internal_error", "command failed internally");
		}
		return true;
	}

	obs_engine::V2Request v2_request;
	obs_engine::V2ParseError parse_error;
	try {
		if (!obs_engine::parse_v2_request(request, v2_request, parse_error)) {
			obs_engine::send_v2_error(parse_error.id, parse_error.code.c_str(), parse_error.message.c_str(), nullptr,
						  revisions.current());
			return true;
		}
		return obs_engine::handle_v2_request(engine, config, revisions, events, v2_request);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "obs-engine: protocol v2 request failed internally: %s\n", error.what());
		obs_engine::send_v2_error(v2_request.id, "internal_error", "request failed internally", nullptr,
				      revisions.current());
	} catch (...) {
		std::fprintf(stderr, "obs-engine: protocol v2 request failed with an unknown exception\n");
		obs_engine::send_v2_error(v2_request.id, "internal_error", "request failed internally", nullptr,
				      revisions.current());
	}
	return true;
}

void run_protocol_loop(obs_engine::Engine &engine, const obs_engine::Config &config,
			       obs_engine::RevisionState &revisions, obs_engine::EventDispatcher &events)
{
	std::string line;
	for (;;) {
		const obs_engine::ReadLineResult read_result = obs_engine::read_line_limited(line);
		if (read_result == obs_engine::ReadLineResult::Eof)
			return;
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
		if (!dispatch_request(engine, config, revisions, events, request.get()))
			return;
	}
}

int run_engine(const obs_engine::Config &config)
{
	obs_engine::Engine engine(config);
	if (!engine.start())
		return 3;

	ProtocolWriterScope writer;
	if (!writer.start()) {
		std::fprintf(stderr, "obs-engine: failed to start protocol writer\n");
		return 5;
	}

	obs_engine::EventDispatcher events;
	events.start();
	obs_engine::RevisionState revisions;
	SourceEventBridgeScope source_events(engine, revisions, events);
	send_ready_event(config);
	run_protocol_loop(engine, config, revisions, events);
	return 0;
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
		return run_engine(config);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "obs-engine: fatal error: %s\n", error.what());
		return 4;
	} catch (...) {
		std::fprintf(stderr, "obs-engine: fatal unknown error\n");
		return 4;
	}

	return 0;
}
