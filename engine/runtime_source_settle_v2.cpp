#include "runtime.hpp"

#include "events.hpp"
#include "source_event_capture.hpp"

#include <callback/signal.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obs_engine {

// SourceV2State is shared with runtime_source_v2.cpp. Keep this definition
// token-for-token equivalent to the definition there until the state is moved
// into a dedicated private header.
struct SourceV2Observer;

struct DeferredSourceEventBatch {
	uint64_t handle = 0;
	std::vector<RuntimeV2Event> events;
};

struct SourceV2State {
	std::mutex mutex;
	std::condition_variable callback_cv;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	RuntimeV2Result *capture = nullptr;
	SourceEventCaptureGate capture_gate;
	std::deque<DeferredSourceEventBatch> deferred;
	size_t deferred_event_count = 0;
	size_t direct_callbacks_inflight = 0;
	bool deferred_overflow = false;
	bool accepting = false;
	std::unordered_map<uint64_t, std::shared_ptr<SourceV2Observer>> observers;
	std::vector<std::shared_ptr<SourceV2Observer>> retired;
};

namespace {

constexpr auto kSourceUpdateSettleTimeout = std::chrono::seconds(5);

bool parse_handle_text(std::string_view text, uint64_t &out)
{
	if (text.empty() || (text.size() > 1 && text.front() == '0'))
		return false;
	uint64_t value = 0;
	const char *begin = text.data();
	const char *end = begin + text.size();
	const auto parsed = std::from_chars(begin, end, value, 10);
	if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0 ||
	    value > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
		return false;
	out = value;
	return true;
}

bool read_source_handle(obs_data_t *params, uint64_t &handle)
{
	if (!params)
		return false;
	obs_data_item_t *item = obs_data_item_byname(params, "source");
	if (!item)
		return false;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	const bool parsed = value && parse_handle_text(value, handle);
	obs_data_item_release(&item);
	return parsed;
}

bool read_settings_json(obs_data_t *data, std::string &json)
{
	if (!data)
		return false;
	obs_data_item_t *item = obs_data_item_byname(data, "settings");
	if (!item)
		return false;
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return false;
	}
	ObsDataPtr settings(obs_data_item_get_obj(item));
	obs_data_item_release(&item);
	if (!settings)
		return false;
	const char *value = obs_data_get_json(settings.get());
	if (!value)
		return false;
	json = value;
	return true;
}

bool result_has_source_event(const RuntimeV2Result &result, std::string_view name, uint64_t handle)
{
	return std::any_of(result.events.begin(), result.events.end(), [&](const RuntimeV2Event &event) {
		if (event.name != name || !event.data)
			return false;
		uint64_t event_handle = 0;
		return read_source_handle(event.data.get(), event_handle) && event_handle == handle;
	});
}

bool batch_matches_source_update(const DeferredSourceEventBatch &batch, uint64_t handle,
				 const std::string &expected_settings_json)
{
	if (batch.handle != handle)
		return false;
	return std::any_of(batch.events.begin(), batch.events.end(), [&](const RuntimeV2Event &event) {
		if (event.name != "source.settingsChanged" || !event.data)
			return false;
		uint64_t event_handle = 0;
		if (!read_source_handle(event.data.get(), event_handle) || event_handle != handle)
			return false;
		std::string settings_json;
		return read_settings_json(event.data.get(), settings_json) && settings_json == expected_settings_json;
	});
}

bool promote_deferred_source_update(SourceV2State &state, uint64_t handle,
				    const std::string &expected_settings_json, RuntimeV2Result &result)
{
	std::lock_guard lock(state.mutex);
	for (auto it = state.deferred.begin(); it != state.deferred.end(); ++it) {
		if (!batch_matches_source_update(*it, handle, expected_settings_json))
			continue;

		const size_t event_count = it->events.size();
		for (RuntimeV2Event &event : it->events) {
			if (!result_has_source_event(result, event.name, handle))
				result.events.push_back(std::move(event));
		}
		state.deferred_event_count -= event_count;
		state.deferred.erase(it);
		return true;
	}
	return false;
}

void mark_source_settlement_lost(SourceV2State &state)
{
	std::lock_guard lock(state.mutex);
	// Preserve already-normalized unrelated batches. Setting overflow makes the
	// existing flush path publish session.resyncRequired and prevents silent
	// incremental delivery after ownership can no longer be proven.
	state.deferred_overflow = true;
}

struct SourceUpdateWaiter {
	std::mutex mutex;
	std::condition_variable cv;
	uint64_t signal_count = 0;
};

void source_update_settle_cb(void *data, calldata_t *)
{
	auto *waiter = static_cast<SourceUpdateWaiter *>(data);
	{
		std::lock_guard lock(waiter->mutex);
		++waiter->signal_count;
	}
	waiter->cv.notify_one();
}

bool settle_deferred_source_update(SourceV2State &state, uint64_t handle, obs_source_t *source,
				   const std::string &expected_settings_json, RuntimeV2Result &result)
{
	// Callbacks that predate capture are drained by protocol_v2.cpp before the
	// mutation executes. A matching batch here therefore belongs to the active
	// capture window, and matching canonical settings protects against claiming
	// an unrelated same-source update.
	if (promote_deferred_source_update(state, handle, expected_settings_json, result))
		return true;

	signal_handler_t *handler = obs_source_get_signal_handler(source);
	if (!handler)
		return false;

	SourceUpdateWaiter waiter;
	signal_handler_connect(handler, "update", source_update_settle_cb, &waiter);

	// signal_handler_connect() serializes on the signal mutex. If the update
	// raced with registration, the already-connected Source observer has
	// finished normalizing its batch before this call can return.
	if (promote_deferred_source_update(state, handle, expected_settings_json, result)) {
		signal_handler_disconnect(handler, "update", source_update_settle_cb, &waiter);
		return true;
	}

	const auto deadline = std::chrono::steady_clock::now() + kSourceUpdateSettleTimeout;
	uint64_t observed_signals = 0;
	for (;;) {
		{
			std::unique_lock lock(waiter.mutex);
			if (!waiter.cv.wait_until(lock, deadline, [&] { return waiter.signal_count != observed_signals; }))
				break;
			observed_signals = waiter.signal_count;
		}

		if (promote_deferred_source_update(state, handle, expected_settings_json, result)) {
			signal_handler_disconnect(handler, "update", source_update_settle_cb, &waiter);
			return true;
		}
	}

	// Disconnecting serializes with an in-flight signal. One final scan closes
	// the timeout-boundary race without claiming a mismatched same-source batch.
	signal_handler_disconnect(handler, "update", source_update_settle_cb, &waiter);
	return promote_deferred_source_update(state, handle, expected_settings_json, result);
}

void canonicalize_source_result(RuntimeV2Result &result, obs_source_t *source)
{
	if (!result.data || !source)
		return;

	ObsDataPtr settings(obs_source_get_settings(source));
	if (!settings)
		return;

	obs_data_item_t *settings_item = obs_data_item_byname(result.data.get(), "settings");
	if (settings_item) {
		if (obs_data_item_gettype(settings_item) == OBS_DATA_OBJECT)
			obs_data_set_obj(result.data.get(), "settings", settings.get());
		obs_data_item_release(&settings_item);
	}

	obs_data_item_t *state_item = obs_data_item_byname(result.data.get(), "state");
	if (!state_item)
		return;
	if (obs_data_item_gettype(state_item) == OBS_DATA_OBJECT) {
		ObsDataPtr state(obs_data_item_get_obj(state_item));
		if (state) {
			obs_data_set_string(state.get(), "kind", obs_source_get_id(source));
			obs_data_set_string(state.get(), "name", obs_source_get_name(source));
			obs_data_set_obj(state.get(), "settings", settings.get());
		}
	}
	obs_data_item_release(&state_item);
}

void set_semantic_source_flags(obs_data_t *data, uint32_t flags)
{
	obs_data_set_int(data, "outputFlags", static_cast<long long>(flags));
	obs_data_set_bool(data, "hasVideo", (flags & OBS_SOURCE_VIDEO) != 0);
	obs_data_set_bool(data, "hasAudio", (flags & OBS_SOURCE_AUDIO) != 0);
	obs_data_set_bool(data, "asyncVideo", (flags & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO);
	obs_data_set_bool(data, "customDraw", (flags & OBS_SOURCE_CUSTOM_DRAW) != 0);
	obs_data_set_bool(data, "interaction", (flags & OBS_SOURCE_INTERACTION) != 0);
	obs_data_set_bool(data, "composite", (flags & OBS_SOURCE_COMPOSITE) != 0);
	obs_data_set_bool(data, "doNotDuplicate", (flags & OBS_SOURCE_DO_NOT_DUPLICATE) != 0);
	obs_data_set_bool(data, "deprecated", (flags & OBS_SOURCE_DEPRECATED) != 0);
	obs_data_set_bool(data, "selfMonitorAllowed", (flags & OBS_SOURCE_DO_NOT_SELF_MONITOR) == 0);
	obs_data_set_bool(data, "disabled", (flags & OBS_SOURCE_CAP_DISABLED) != 0);
	obs_data_set_bool(data, "monitorByDefault", (flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0);
	obs_data_set_bool(data, "controllableMedia", (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0);
	obs_data_set_bool(data, "cea708", (flags & OBS_SOURCE_CEA_708) != 0);
	obs_data_set_bool(data, "srgb", (flags & OBS_SOURCE_SRGB) != 0);
	obs_data_set_bool(data, "dontShowPropertiesOnCreate", (flags & OBS_SOURCE_CAP_DONT_SHOW_PROPERTIES) != 0);
	obs_data_set_bool(data, "requiresCanvas", (flags & OBS_SOURCE_REQUIRES_CANVAS) != 0);
}

const char *find_unversioned_input_id(const char *kind)
{
	if (!kind)
		return nullptr;
	const char *id = nullptr;
	const char *unversioned_id = nullptr;
	for (size_t index = 0; obs_enum_input_types2(index, &id, &unversioned_id); ++index) {
		if (id && std::string_view(id) == kind)
			return unversioned_id ? unversioned_id : kind;
	}
	return kind;
}

void normalize_kind_entry(obs_data_t *entry)
{
	if (!entry)
		return;
	const char *kind = obs_data_get_string(entry, "id");
	if (!kind || !*kind)
		return;
	obs_data_set_string(entry, "unversionedId", find_unversioned_input_id(kind));
	const char *display_name = obs_source_get_display_name(kind);
	obs_data_set_string(entry, "displayName", display_name ? display_name : kind);
	set_semantic_source_flags(entry, obs_get_source_output_flags(kind));
	obs_module_t *module = obs_source_get_module(kind);
	if (module) {
		const char *module_file = obs_get_module_file_name(module);
		if (module_file)
			obs_data_set_string(entry, "module", module_file);
	}
	obs_data_set_int(entry, "moduleLoadState", static_cast<long long>(obs_source_load_state(kind)));
}

} // namespace

void Engine::v2_settle_source_mutation(obs_data_t *params, RuntimeV2Result &result)
{
	uint64_t handle = 0;
	if (!read_source_handle(params, handle))
		return;
	auto it = sources_.find(handle);
	if (it == sources_.end())
		return;

	obs_source_t *source = it->second;
	const bool deferred_video_update = (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0;
	if (deferred_video_update && source_v2_state_) {
		ObsDataPtr expected_settings(obs_source_get_settings(source));
		const char *expected_json = expected_settings ? obs_data_get_json(expected_settings.get()) : nullptr;
		const std::string expected_settings_json = expected_json ? expected_json : "";
		if (expected_settings_json.empty() ||
		    !settle_deferred_source_update(*source_v2_state_, handle, source, expected_settings_json, result)) {
			std::fprintf(stderr,
				     "obs-engine: timed out settling deferred update for source %llu; controller resync required\n",
				     static_cast<unsigned long long>(handle));
			std::fflush(stderr);
			mark_source_settlement_lost(*source_v2_state_);
		}
	}

	canonicalize_source_result(result, source);
}

void Engine::v2_normalize_source_kind_metadata(RuntimeV2Result &result)
{
	if (!result.data)
		return;

	obs_data_item_t *kinds_item = obs_data_item_byname(result.data.get(), "kinds");
	if (kinds_item) {
		if (obs_data_item_gettype(kinds_item) == OBS_DATA_ARRAY) {
			ObsArrayPtr kinds(obs_data_item_get_array(kinds_item));
			if (kinds) {
				const size_t count = obs_data_array_count(kinds.get());
				for (size_t index = 0; index < count; ++index) {
					ObsDataPtr entry(obs_data_array_item(kinds.get(), index));
					normalize_kind_entry(entry.get());
				}
			}
		}
		obs_data_item_release(&kinds_item);
		return;
	}

	normalize_kind_entry(result.data.get());
}

} // namespace obs_engine
