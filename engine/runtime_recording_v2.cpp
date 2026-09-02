#include "runtime.hpp"

#include "runtime_phase2_common.hpp"
#include "validation.hpp"

#include <callback/calldata.h>
#include <callback/proc.h>
#include <callback/signal.h>

#include <algorithm>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace obs_engine {

struct RecordingV2Observer {
	Engine *engine = nullptr;
	uint64_t output_handle = 0;
	std::mutex mutex;
	std::condition_variable callback_cv;
	std::string current_path;
	std::string last_file;
	size_t callbacks_inflight = 0;
	bool accepting = true;
};

namespace {

constexpr size_t kMaxRecordingPathBytes = 4096;
constexpr size_t kMaxChapterNameBytes = 256;

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "recording operation failed";
	return false;
}

bool output_is_recording_compatible(const OutputEntry &entry, RuntimeV2Error &error)
{
	const char *kind = obs_output_get_id(entry.output);
	const bool known_file_output = kind && (std::strcmp(kind, "mp4_output") == 0 ||
								std::strcmp(kind, "mov_output") == 0 ||
								std::strcmp(kind, "ffmpeg_muxer") == 0 ||
								std::strcmp(kind, "task27_test_recording") == 0);
	if (!known_file_output)
		return fail(error, "unsupported_capability", "Output kind is not an audited recording file Output");
	const uint32_t flags = obs_output_get_flags(entry.output);
	if ((flags & OBS_OUTPUT_SERVICE) != 0)
		return fail(error, "unsupported_capability", "recording requires a non-service Output");
	if ((flags & OBS_OUTPUT_ENCODED) == 0 || (flags & OBS_OUTPUT_VIDEO) == 0)
		return fail(error, "unsupported_capability", "recording requires an encoded video Output");
	return true;
}

bool is_dangerous_recording_path(std::string_view path)
{
	std::string lowered(path);
	std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	return lowered.starts_with("//./") || lowered.starts_with("//?/") || lowered.starts_with("//?/globalroot/");
}

std::string path_to_utf8(const std::filesystem::path &path)
{
	const auto value = path.u8string();
	return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}

bool validate_recording_path_text(const std::string &input, RuntimeV2Error &error)
{
	if (input.empty() || input.size() > kMaxRecordingPathBytes || input.find('\0') != std::string::npos)
		return fail(error, "bad_request", "recording path must be a bounded UTF-8 path without NUL");
	if (input.find("://") != std::string::npos)
		return fail(error, "bad_request", "recording path must be a local filesystem path");
	return true;
}

bool make_recording_filesystem_path(const std::string &input, std::filesystem::path &requested,
					    RuntimeV2Error &error)
{
	try {
		const auto *bytes = reinterpret_cast<const char8_t *>(input.data());
		requested = std::filesystem::path(std::u8string(bytes, bytes + input.size()));
	} catch (...) {
		return fail(error, "bad_request", "recording path is not valid UTF-8");
	}
	if (!requested.is_absolute())
		return fail(error, "bad_request", "recording path must be absolute");
	return true;
}

bool prepare_recording_path_parent(const std::filesystem::path &normalized, bool create_directory,
					   RuntimeV2Error &error)
{
	if (is_dangerous_recording_path(path_to_utf8(normalized)))
		return fail(error, "permission_denied", "Windows device namespace recording paths are not supported");
	if (normalized.extension().empty())
		return fail(error, "bad_request", "recording path must include a file extension");
	const std::filesystem::path parent = normalized.parent_path();
	std::error_code filesystem_error;
	if (create_directory) {
		std::filesystem::create_directories(parent, filesystem_error);
		if (filesystem_error)
			return fail(error, "not_available", "recording parent directory could not be created");
	}
	if (!std::filesystem::is_directory(parent, filesystem_error) || filesystem_error)
		return fail(error, "not_available", "recording parent directory is not available");
	return true;
}

bool canonicalize_recording_path(const std::string &input, bool create_directory, std::string &canonical,
					 RuntimeV2Error &error)
{
	if (!validate_recording_path_text(input, error))
		return false;
	std::filesystem::path requested;
	if (!make_recording_filesystem_path(input, requested, error))
		return false;
	const std::filesystem::path normalized = requested.lexically_normal();
	if (!prepare_recording_path_parent(normalized, create_directory, error))
		return false;
	std::error_code filesystem_error;
	const std::filesystem::path resolved = std::filesystem::weakly_canonical(normalized, filesystem_error);
	if (filesystem_error)
		return fail(error, "bad_request", "recording path could not be canonicalized");
	canonical = path_to_utf8(resolved);
	return !canonical.empty() && canonical.size() <= kMaxRecordingPathBytes
		       ? true
		       : fail(error, "bad_request", "canonical recording path is too long");
}

struct RecordingPathOptions {
	bool path_present = false;
	std::string path;
	bool overwrite_present = false;
	bool overwrite = false;
	bool create_directory = false;
};

bool read_recording_path_options(obs_data_t *params, RecordingPathOptions &options, RuntimeV2Error &error)
{
	bool present = false;
	std::string requested_path;
	if (!phase2_read_string(params, "path", requested_path, present))
		return fail(error, "bad_request", "params.path must be a string when present");
	options.path_present = present;
	if (!phase2_read_bool(params, "overwrite", options.overwrite, present))
		return fail(error, "bad_request", "params.overwrite must be boolean when present");
	options.overwrite_present = present;
	if (!phase2_read_bool(params, "createDirectory", options.create_directory, present))
		return fail(error, "bad_request", "params.createDirectory must be boolean when present");
	if (present && options.create_directory && !options.path_present)
		return fail(error, "bad_request", "createDirectory requires path");
	if (options.path_present && !canonicalize_recording_path(requested_path, options.create_directory, options.path, error))
		return false;
	return true;
}

ObsDataPtr make_recording_config(uint64_t output_handle, bool configured)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "configured", configured);
	if (configured)
		phase2_set_handle(data.get(), "output", output_handle);
	else
		obs_data_set_obj(data.get(), "output", nullptr);
	return data;
}

ObsDataPtr make_recording_path_result(uint64_t output_handle, const char *field, const std::string &path)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output_handle);
	if (path.empty())
		obs_data_set_obj(data.get(), field, nullptr);
	else
		obs_data_set_string(data.get(), field, path.c_str());
	return data;
}

ObsDataPtr make_recording_file_event(uint64_t output_handle, const std::string &previous,
					     const std::string &current)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output_handle);
	if (previous.empty())
		obs_data_set_obj(data.get(), "previousPath", nullptr);
	else
		obs_data_set_string(data.get(), "previousPath", previous.c_str());
	obs_data_set_string(data.get(), "currentPath", current.c_str());
	return data;
}

ObsDataPtr make_recording_finalized_event(uint64_t output_handle, const std::string &path)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output_handle);
	obs_data_set_string(data.get(), "path", path.c_str());
	return data;
}

bool output_kind_supports_proc(const char *kind, const char *proc)
{
	if (!kind || !proc)
		return false;
	if (std::strcmp(proc, "split_file") == 0)
		return std::strcmp(kind, "mp4_output") == 0 || std::strcmp(kind, "mov_output") == 0 ||
		       std::strcmp(kind, "ffmpeg_muxer") == 0 || std::strcmp(kind, "task27_test_recording") == 0;
	if (std::strcmp(proc, "add_chapter") == 0)
		return std::strcmp(kind, "mp4_output") == 0 || std::strcmp(kind, "mov_output") == 0 ||
		       std::strcmp(kind, "task27_test_recording") == 0;
	return false;
}

ObsDataPtr recording_output_params(uint64_t output_handle)
{
	ObsDataPtr params(obs_data_create());
	phase2_set_handle(params.get(), "output", output_handle);
	return params;
}

class RecordingCallbackScope {
public:
	explicit RecordingCallbackScope(RecordingV2Observer &observer) : observer_(observer)
	{
		std::lock_guard lock(observer_.mutex);
		if (!observer_.accepting)
			return;
		accepted_ = true;
		++observer_.callbacks_inflight;
	}

	~RecordingCallbackScope()
	{
		if (!accepted_)
			return;
		std::lock_guard lock(observer_.mutex);
		if (observer_.callbacks_inflight > 0)
			--observer_.callbacks_inflight;
		observer_.callback_cv.notify_all();
	}

	bool accepted() const noexcept
	{
		return accepted_;
	}

private:
	RecordingV2Observer &observer_;
	bool accepted_ = false;
};

void recording_file_changed_callback(void *data, calldata_t *calldata)
{
	if (!data || !calldata)
		return;
	auto &observer = *static_cast<RecordingV2Observer *>(data);
	RecordingCallbackScope scope(observer);
	if (!scope.accepted())
		return;
	const char *next_file = calldata_string(calldata, "next_file");
	if (!next_file || !*next_file)
		return;
	const std::string next(next_file);
	if (next.size() > kMaxRecordingPathBytes)
		return;
	std::string previous;
	{
		std::lock_guard lock(observer.mutex);
		previous = observer.current_path;
		observer.last_file = previous;
		observer.current_path = next;
	}
	if (observer.engine) {
		std::vector<RuntimeV2Event> events;
		events.push_back(RuntimeV2Event{"recording.fileChanged",
							make_recording_file_event(observer.output_handle, previous, next)});
		observer.engine->v2_publish_output_callback_events(std::move(events));
	}
}

void connect_recording_observer(RecordingV2Observer &observer, obs_output_t *output)
{
	signal_handler_connect(obs_output_get_signal_handler(output), "file_changed", recording_file_changed_callback,
				       &observer);
}

void disconnect_recording_observer(RecordingV2Observer &observer, obs_output_t *output)
{
	{
		std::lock_guard lock(observer.mutex);
		observer.accepting = false;
	}
	signal_handler_disconnect(obs_output_get_signal_handler(output), "file_changed", recording_file_changed_callback,
					  &observer);
	std::unique_lock lock(observer.mutex);
	observer.callback_cv.wait(lock, [&] { return observer.callbacks_inflight == 0; });
}

} // namespace

bool Engine::v2_get_recording_output(uint64_t &handle, OutputEntry *&entry, RuntimeV2Error &error) const
{
	if (!recording_.output)
		return fail(error, "invalid_state", "recording is not configured");
	handle = recording_.output;
	const auto output = outputs_.find(handle);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "not_found", "configured recording Output was not found");
	entry = const_cast<OutputEntry *>(&output->second);
	return output_is_recording_compatible(*entry, error);
}

void Engine::v2_prepare_recording_shutdown() noexcept
{
	if (recording_.observer) {
		const auto output = outputs_.find(recording_.output);
		if (output != outputs_.end() && output->second.output)
			disconnect_recording_observer(*recording_.observer, output->second.output);
	}
	recording_ = RecordingEntry{};
}

bool Engine::v2_recording_get_config(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	result.data = make_recording_config(recording_.output, recording_.output != 0);
	return true;
}

bool Engine::v2_validate_recording_configuration(obs_data_t *params, uint64_t &handle, OutputEntry *&entry,
							RuntimeV2Error &error) const
{
	if (!v2_get_output_entry(params, handle, entry, error))
		return false;
	if (!output_is_recording_compatible(*entry, error))
		return false;
	if (recording_.output && recording_.output != handle)
		return fail(error, "object_in_use", "another Output is already assigned to recording");
	if (streaming_.output == handle || replay_.output == handle || virtual_camera_.output == handle)
		return fail(error, "object_in_use", "Output is already assigned to another convenience role");
	return v2_output_is_inactive(*entry, error);
}

bool Engine::v2_apply_recording_configuration_settings(OutputEntry &entry, uint64_t handle, obs_data_t *params,
							RuntimeV2Result &result, RuntimeV2Error &error)
{
	RecordingPathOptions options;
	if (!read_recording_path_options(params, options, error))
		return false;
	ObsDataPtr patch(obs_data_create());
	if (options.path_present)
		obs_data_set_string(patch.get(), "path", options.path.c_str());
	if (options.overwrite_present)
		obs_data_set_bool(patch.get(), "allow_overwrite", options.overwrite);
	if (options.path_present || options.overwrite_present) {
		if (!v2_update_output_settings(entry, handle, patch.get(), false, result, error))
			return false;
	}
	return true;
}

void Engine::v2_register_recording_role(uint64_t handle, OutputEntry &entry)
{
	auto observer = std::make_shared<RecordingV2Observer>();
	observer->engine = this;
	observer->output_handle = handle;
	connect_recording_observer(*observer, entry.output);
	recording_.output = handle;
	recording_.observer = std::move(observer);
}

bool Engine::v2_recording_configure(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_validate_recording_configuration(params, output_handle, entry, error))
		return false;
	if (!v2_apply_recording_configuration_settings(*entry, output_handle, params, result, error))
		return false;
	const bool newly_configured = recording_.output == 0;
	if (newly_configured)
		v2_register_recording_role(output_handle, *entry);
	ObsDataPtr live(obs_output_get_settings(entry->output));
	const char *live_path = live ? obs_data_get_string(live.get(), "path") : nullptr;
	if (recording_.observer && live_path && *live_path) {
		std::lock_guard lock(recording_.observer->mutex);
		recording_.observer->current_path = live_path;
	}
	result.data = make_recording_config(output_handle, true);
	if (newly_configured) {
		phase2_append_event(result, "recording.configChanged", phase2_clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_recording_unconfigure(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	if (!v2_output_is_inactive(*entry, error))
		return false;
	if (recording_.observer)
		disconnect_recording_observer(*recording_.observer, entry->output);
	recording_ = RecordingEntry{};
	result.data = make_recording_config(0, false);
	phase2_append_event(result, "recording.configChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_recording_start(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	return v2_output_start(recording_output_params(output_handle).get(), result, error);
}

bool Engine::v2_recording_stop(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	return v2_output_stop(recording_output_params(output_handle).get(), result, error);
}

bool Engine::v2_recording_force_stop(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	return v2_output_force_stop(recording_output_params(output_handle).get(), result, error);
}

bool Engine::v2_recording_pause(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	ObsDataPtr params = recording_output_params(output_handle);
	obs_data_set_bool(params.get(), "paused", true);
	return v2_output_set_paused(params.get(), result, error);
}

bool Engine::v2_recording_resume(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	ObsDataPtr params = recording_output_params(output_handle);
	obs_data_set_bool(params.get(), "paused", false);
	return v2_output_set_paused(params.get(), result, error);
}

bool Engine::v2_recording_toggle_pause(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	ObsDataPtr params = recording_output_params(output_handle);
	obs_data_set_bool(params.get(), "paused", !obs_output_paused(entry->output));
	return v2_output_set_paused(params.get(), result, error);
}

bool Engine::v2_recording_split_file(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	if (!obs_output_active(entry->output))
		return fail(error, "invalid_state", "recording must be active to split the current file");
	const char *kind = obs_output_get_id(entry->output);
	if (!output_kind_supports_proc(kind, "split_file"))
		return fail(error, "unsupported_capability", "selected recording Output does not support file splitting");
	ObsDataPtr settings(obs_output_get_settings(entry->output));
	if (!settings || !obs_data_get_bool(settings.get(), "split_file"))
		return fail(error, "unsupported_capability", "file splitting is not enabled for the recording Output");
	proc_handler_t *procedures = obs_output_get_proc_handler(entry->output);
	if (!procedures)
		return fail(error, "unsupported_capability", "recording Output has no split procedure");
	calldata_t calldata;
	calldata_init(&calldata);
	const bool called = proc_handler_call(procedures, "split_file", &calldata);
	const bool enabled = calldata_bool(&calldata, "split_file_enabled");
	calldata_free(&calldata);
	if (!called || !enabled)
		return fail(error, "unsupported_capability", "recording Output rejected file splitting");
	result.data = v2_output_state(output_handle, *entry);
	result.mutated = true;
	return true;
}

bool Engine::v2_recording_add_chapter(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	if (!obs_output_active(entry->output))
		return fail(error, "invalid_state", "recording must be active to add a chapter");
	const char *kind = obs_output_get_id(entry->output);
	if (!output_kind_supports_proc(kind, "add_chapter"))
		return fail(error, "unsupported_capability", "selected recording Output does not support chapters");
	std::string name;
	bool present = false;
	if (!phase2_read_string(params, "name", name, present) || (present && name.size() > kMaxChapterNameBytes))
		return fail(error, "bad_request", "params.name must be a bounded chapter name when present");
	if (!present)
		name.clear();
	proc_handler_t *procedures = obs_output_get_proc_handler(entry->output);
	if (!procedures)
		return fail(error, "unsupported_capability", "recording Output has no chapter procedure");
	calldata_t calldata;
	calldata_init(&calldata);
	calldata_set_string(&calldata, "chapter_name", name.c_str());
	const bool called = proc_handler_call(procedures, "add_chapter", &calldata);
	calldata_free(&calldata);
	if (!called)
		return fail(error, "unsupported_capability", "recording Output rejected the chapter");
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "output", output_handle);
	obs_data_set_string(event_data.get(), "name", name.c_str());
	obs_data_set_bool(event_data.get(), "queued", true);
	result.data = v2_output_state(output_handle, *entry);
	phase2_append_event(result, "recording.chapterAdded", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_recording_get_state(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	ObsDataPtr data(v2_output_state(output_handle, *entry));
	obs_data_set_bool(data.get(), "configured", true);
	if (recording_.observer) {
		std::lock_guard lock(recording_.observer->mutex);
		if (recording_.observer->current_path.empty())
			obs_data_set_obj(data.get(), "currentPath", nullptr);
		else
			obs_data_set_string(data.get(), "currentPath", recording_.observer->current_path.c_str());
		if (recording_.observer->last_file.empty())
			obs_data_set_obj(data.get(), "lastFile", nullptr);
		else
			obs_data_set_string(data.get(), "lastFile", recording_.observer->last_file.c_str());
	}
	result.data = std::move(data);
	return true;
}

bool Engine::v2_recording_get_stats(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	ObsDataPtr params = recording_output_params(output_handle);
	if (!v2_output_get_stats(params.get(), result, error))
		return false;
	if (result.data)
		phase2_set_handle(result.data.get(), "recording", output_handle);
	return true;
}

bool Engine::v2_recording_get_current_path(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	std::string path;
	if (recording_.observer) {
		std::lock_guard lock(recording_.observer->mutex);
		path = recording_.observer->current_path;
	}
	result.data = make_recording_path_result(output_handle, "currentPath", path);
	return true;
}

bool Engine::v2_recording_get_last_file(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_recording_output(output_handle, entry, error))
		return false;
	std::string path;
	if (recording_.observer) {
		std::lock_guard lock(recording_.observer->mutex);
		path = recording_.observer->last_file;
	}
	result.data = make_recording_path_result(output_handle, "lastFile", path);
	return true;
}

void Engine::v2_append_recording_output_events(uint64_t output_handle, std::vector<RuntimeV2Event> &events)
{
	if (!recording_.observer || recording_.output != output_handle)
		return;
	const bool stopped = std::any_of(events.begin(), events.end(), [](const RuntimeV2Event &event) {
		return event.name == "output.stopped";
	});
	if (!stopped)
		return;
	std::string path;
	{
		std::lock_guard lock(recording_.observer->mutex);
		if (!recording_.observer->accepting || recording_.observer->current_path.empty())
			return;
		path = recording_.observer->current_path;
		recording_.observer->last_file = path;
	}
	events.push_back(RuntimeV2Event{"recording.fileFinalized", make_recording_finalized_event(output_handle, path)});
}

} // namespace obs_engine
