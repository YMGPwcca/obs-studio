#include "runtime.hpp"

#include "runtime_phase2_common.hpp"

#include <callback/calldata.h>
#include <callback/proc.h>
#include <callback/signal.h>

#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obs_engine {

struct ReplayV2Observer {
	Engine *engine = nullptr;
	uint64_t output_handle = 0;
	obs_weak_output_t *weak = nullptr;
	std::mutex mutex;
	std::condition_variable callback_cv;
	std::string last_file;
	size_t callbacks_inflight = 0;
	bool accepting = true;
	bool pending_save = false;
};

namespace {

constexpr size_t kMaxReplayPathBytes = 4096;

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "replay buffer operation failed";
	return false;
}

bool output_is_replay_compatible(const OutputEntry &entry, RuntimeV2Error &error)
{
	const char *kind = obs_output_get_id(entry.output);
	const bool known_replay_output = kind &&
			(std::string_view(kind) == "replay_buffer" || std::string_view(kind) == "task29_test_replay");
	if (!known_replay_output)
		return fail(error, "unsupported_capability", "Output kind is not an audited replay-buffer Output");
	const uint32_t flags = obs_output_get_flags(entry.output);
	if ((flags & OBS_OUTPUT_ENCODED) == 0 || (flags & OBS_OUTPUT_VIDEO) == 0)
		return fail(error, "unsupported_capability", "replay buffer requires an encoded video Output");
	return true;
}

bool dangerous_replay_path(std::string_view path)
{
	std::string lowered(path);
	for (char &value : lowered)
		value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
	return lowered.starts_with("//./") || lowered.starts_with("//?/") ||
	       lowered.starts_with("//?/globalroot/");
}

bool make_replay_filesystem_path(std::string_view text, std::filesystem::path &path)
{
	try {
		const auto *bytes = reinterpret_cast<const char8_t *>(text.data());
		path = std::filesystem::path(std::u8string(bytes, bytes + text.size()));
		return true;
	} catch (...) {
		return false;
	}
}

std::string replay_path_to_utf8(const std::filesystem::path &path)
{
	const auto value = path.u8string();
	return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}

bool normalize_replay_path(std::string_view raw, std::string &path)
{
	if (raw.empty() || raw.size() > kMaxReplayPathBytes || raw.find('\0') != std::string_view::npos ||
	    raw.find("://") != std::string_view::npos)
		return false;
	std::filesystem::path requested;
	if (!make_replay_filesystem_path(raw, requested) || !requested.is_absolute())
		return false;
	const std::filesystem::path normalized = requested.lexically_normal();
	if (dangerous_replay_path(replay_path_to_utf8(normalized)))
		return false;
	std::error_code filesystem_error;
	const std::filesystem::path resolved = std::filesystem::weakly_canonical(normalized, filesystem_error);
	if (filesystem_error)
		return false;
	path = replay_path_to_utf8(resolved);
	return !path.empty() && path.size() <= kMaxReplayPathBytes;
}

std::optional<uint64_t> replay_file_size(std::string_view path)
{
	std::filesystem::path filesystem_path;
	if (!make_replay_filesystem_path(path, filesystem_path))
		return std::nullopt;
	std::error_code filesystem_error;
	const uintmax_t size = std::filesystem::file_size(filesystem_path, filesystem_error);
	if (filesystem_error || size > static_cast<uintmax_t>(std::numeric_limits<long long>::max()))
		return std::nullopt;
	return static_cast<uint64_t>(size);
}

ObsDataPtr make_replay_config(uint64_t output_handle, bool configured)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "configured", configured);
	if (configured)
		phase2_set_handle(data.get(), "output", output_handle);
	else
		obs_data_set_obj(data.get(), "output", nullptr);
	return data;
}

ObsDataPtr replay_output_params(uint64_t output_handle)
{
	ObsDataPtr params(obs_data_create());
	phase2_set_handle(params.get(), "output", output_handle);
	return params;
}

ObsDataPtr make_replay_path_result(uint64_t output_handle, const std::string &path)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output_handle);
	if (path.empty())
		obs_data_set_obj(data.get(), "lastFile", nullptr);
	else
		obs_data_set_string(data.get(), "lastFile", path.c_str());
	return data;
}

ObsDataPtr make_replay_saved_event(uint64_t output_handle, const std::string &path)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output_handle);
	obs_data_set_string(data.get(), "path", path.c_str());
	if (const std::optional<uint64_t> size = replay_file_size(path))
		obs_data_set_int(data.get(), "size", static_cast<long long>(*size));
	return data;
}

ObsDataPtr make_replay_save_result(uint64_t output_handle, bool pending, const std::string &last_file)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "output", output_handle);
	obs_data_set_bool(data.get(), "pending", pending);
	if (last_file.empty())
		obs_data_set_obj(data.get(), "lastFile", nullptr);
	else
		obs_data_set_string(data.get(), "lastFile", last_file.c_str());
	return data;
}

class ReplayCallbackScope {
public:
	explicit ReplayCallbackScope(ReplayV2Observer &observer) : observer_(observer)
	{
		std::lock_guard lock(observer_.mutex);
		if (!observer_.accepting)
			return;
		accepted_ = true;
		++observer_.callbacks_inflight;
	}

	~ReplayCallbackScope()
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
	ReplayV2Observer &observer_;
	bool accepted_ = false;
};

bool read_last_replay_path(obs_output_t *output, std::string &path)
{
	if (!output)
		return false;
	proc_handler_t *procedures = obs_output_get_proc_handler(output);
	if (!procedures)
		return false;
	calldata_t calldata;
	calldata_init(&calldata);
	const bool called = proc_handler_call(procedures, "get_last_replay", &calldata);
	const char *raw = called ? calldata_string(&calldata, "path") : nullptr;
	const std::string copied = raw ? raw : "";
	calldata_free(&calldata);
	return !copied.empty() && normalize_replay_path(copied, path);
}

void replay_saved_callback(void *data, calldata_t *)
{
	if (!data)
		return;
	auto &observer = *static_cast<ReplayV2Observer *>(data);
	ReplayCallbackScope scope(observer);
	if (!scope.accepted())
		return;
	obs_output_t *output = observer.weak ? obs_weak_output_get_output(observer.weak) : nullptr;
	std::string path;
	if (output) {
		read_last_replay_path(output, path);
		obs_output_release(output);
	}
	{
		std::lock_guard lock(observer.mutex);
		observer.pending_save = false;
		if (!path.empty())
			observer.last_file = path;
	}
	if (!path.empty() && observer.engine) {
		std::vector<RuntimeV2Event> events;
		events.push_back(RuntimeV2Event{"replayBuffer.saved", make_replay_saved_event(observer.output_handle, path)});
		observer.engine->v2_publish_output_callback_events(std::move(events));
	}
}

void connect_replay_observer(ReplayV2Observer &observer, obs_output_t *output)
{
	observer.weak = obs_output_get_weak_output(output);
	signal_handler_connect(obs_output_get_signal_handler(output), "saved", replay_saved_callback, &observer);
}

void disconnect_replay_observer(ReplayV2Observer &observer, obs_output_t *output)
{
	{
		std::lock_guard lock(observer.mutex);
		observer.accepting = false;
	}
	signal_handler_disconnect(obs_output_get_signal_handler(output), "saved", replay_saved_callback, &observer);
	std::unique_lock lock(observer.mutex);
	observer.callback_cv.wait(lock, [&] { return observer.callbacks_inflight == 0; });
	if (observer.weak) {
		obs_weak_output_release(observer.weak);
		observer.weak = nullptr;
	}
}

} // namespace

bool Engine::v2_get_replay_buffer_output(uint64_t &handle, OutputEntry *&entry, RuntimeV2Error &error) const
{
	if (!replay_.output)
		return fail(error, "invalid_state", "replay buffer is not configured");
	handle = replay_.output;
	const auto output = outputs_.find(handle);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "not_found", "configured replay-buffer Output was not found");
	entry = const_cast<OutputEntry *>(&output->second);
	return output_is_replay_compatible(*entry, error);
}

void Engine::v2_prepare_replay_shutdown() noexcept
{
	if (replay_.observer) {
		const auto output = outputs_.find(replay_.output);
		if (output != outputs_.end() && output->second.output)
			disconnect_replay_observer(*replay_.observer, output->second.output);
	}
	replay_ = ReplayEntry{};
}

bool Engine::v2_replay_buffer_get_config(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	result.data = make_replay_config(replay_.output, replay_.output != 0);
	return true;
}

bool Engine::v2_replay_buffer_configure(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, output_handle, entry, error))
		return false;
	if (!output_is_replay_compatible(*entry, error))
		return false;
	if (replay_.output && replay_.output != output_handle)
		return fail(error, "object_in_use", "another Output is already assigned to replayBuffer");
	if (recording_.output == output_handle || streaming_.output == output_handle)
		return fail(error, "object_in_use", "Output is already assigned to another convenience role");
	if (!v2_output_is_inactive(*entry, error))
		return false;
	const bool newly_configured = replay_.output == 0;
	if (newly_configured) {
		auto observer = std::make_shared<ReplayV2Observer>();
		observer->engine = this;
		observer->output_handle = output_handle;
		connect_replay_observer(*observer, entry->output);
		replay_.output = output_handle;
		replay_.observer = std::move(observer);
	}
	result.data = make_replay_config(output_handle, true);
	if (newly_configured) {
		phase2_append_event(result, "replayBuffer.configChanged", phase2_clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_replay_buffer_unconfigure(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	if (!v2_output_is_inactive(*entry, error))
		return false;
	if (replay_.observer) {
		std::lock_guard lock(replay_.observer->mutex);
		if (replay_.observer->pending_save)
			return fail(error, "busy", "replay buffer save is still pending");
	}
	if (replay_.observer)
		disconnect_replay_observer(*replay_.observer, entry->output);
	replay_ = ReplayEntry{};
	result.data = make_replay_config(0, false);
	phase2_append_event(result, "replayBuffer.configChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_replay_buffer_start(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	return v2_output_start(replay_output_params(output_handle).get(), result, error);
}

bool Engine::v2_replay_buffer_stop(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	return v2_output_stop(replay_output_params(output_handle).get(), result, error);
}

bool Engine::v2_replay_buffer_save(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	ObsDataPtr output_state = v2_output_state(output_handle, *entry);
	const char *state = obs_data_get_string(output_state.get(), "state");
	if (!state || std::string_view(state) != "active")
		return fail(error, "invalid_state", "replay buffer must be active and idle to save");
	if (obs_output_paused(entry->output))
		return fail(error, "invalid_state", "replay buffer cannot save while paused");
	if (!replay_.observer)
		return fail(error, "invalid_state", "replay buffer observer is unavailable");
	{
		std::lock_guard lock(replay_.observer->mutex);
		if (replay_.observer->pending_save)
			return fail(error, "busy", "replay buffer save is already pending");
		replay_.observer->pending_save = true;
	}
	proc_handler_t *procedures = obs_output_get_proc_handler(entry->output);
	if (!procedures) {
		std::lock_guard lock(replay_.observer->mutex);
		replay_.observer->pending_save = false;
		return fail(error, "unsupported_capability", "replay buffer has no save procedure");
	}
	calldata_t calldata;
	calldata_init(&calldata);
	const bool called = proc_handler_call(procedures, "save", &calldata);
	calldata_free(&calldata);
	if (!called) {
		std::lock_guard lock(replay_.observer->mutex);
		replay_.observer->pending_save = false;
		return fail(error, "obs_error", "replay buffer save procedure failed");
	}
	bool pending = false;
	std::string last_file;
	{
		std::lock_guard lock(replay_.observer->mutex);
		pending = replay_.observer->pending_save;
		last_file = replay_.observer->last_file;
	}
	result.data = make_replay_save_result(output_handle, pending, last_file);
	result.mutated = true;
	return true;
}

bool Engine::v2_replay_buffer_get_state(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	ObsDataPtr data(v2_output_state(output_handle, *entry));
	obs_data_set_bool(data.get(), "configured", true);
	bool pending = false;
	std::string last_file;
	if (replay_.observer) {
		std::lock_guard lock(replay_.observer->mutex);
		pending = replay_.observer->pending_save;
		last_file = replay_.observer->last_file;
	}
	obs_data_set_bool(data.get(), "pendingSave", pending);
	if (last_file.empty())
		obs_data_set_obj(data.get(), "lastFile", nullptr);
	else
		obs_data_set_string(data.get(), "lastFile", last_file.c_str());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_replay_buffer_get_stats(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	if (!v2_output_get_stats(replay_output_params(output_handle).get(), result, error))
		return false;
	if (result.data)
		phase2_set_handle(result.data.get(), "replayBuffer", output_handle);
	return true;
}

bool Engine::v2_replay_buffer_get_last_file(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_replay_buffer_output(output_handle, entry, error))
		return false;
	std::string last_file;
	if (replay_.observer) {
		std::lock_guard lock(replay_.observer->mutex);
		last_file = replay_.observer->last_file;
	}
	result.data = make_replay_path_result(output_handle, last_file);
	return true;
}

} // namespace obs_engine
