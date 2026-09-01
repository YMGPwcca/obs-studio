#include "runtime.hpp"

#include "events.hpp"
#include "source_event_capture.hpp"
#include "validation.hpp"

#include <callback/calldata.h>
#include <callback/signal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace obs_engine {

enum class AudioSignal {
	Volume,
	Mute,
	Balance,
	SyncOffset,
	Monitoring,
	MonitoringCompat,
	Tracks,
	PushToTalk,
	PushToTalkDelay,
	PushToMute,
	PushToMuteDelay,
};

struct AudioV2Observer;

struct DeferredAudioEventBatch {
	uint64_t handle = 0;
	std::vector<RuntimeV2Event> events;
};

struct AudioMeterSource {
	uint64_t source_handle = 0;
	std::mutex mutex;
	obs_volmeter_t *meter = nullptr;
	std::atomic<bool> accepting{true};
	std::atomic<uint64_t> sample_sequence{0};
	std::atomic<int> channel_count{0};
	std::array<std::atomic<float>, MAX_AUDIO_CHANNELS> magnitude{};
	std::array<std::atomic<float>, MAX_AUDIO_CHANNELS> peak{};
	std::array<std::atomic<float>, MAX_AUDIO_CHANNELS> input_peak{};

	AudioMeterSource()
	{
		for (size_t index = 0; index < MAX_AUDIO_CHANNELS; ++index) {
			magnitude[index].store(-192.0f, std::memory_order_relaxed);
			peak[index].store(-192.0f, std::memory_order_relaxed);
			input_peak[index].store(-192.0f, std::memory_order_relaxed);
		}
	}
};

struct AudioMeterSubscription {
	std::mutex mutex;
	uint64_t handle = 0;
	uint32_t max_hz = 20;
	uint64_t next_publish_ns = 0;
	std::vector<std::shared_ptr<AudioMeterSource>> sources;
};

struct AudioV2State {
	std::mutex mutex;
	std::condition_variable callback_cv;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	RuntimeV2Result *capture = nullptr;
	SourceEventCaptureGate capture_gate;
	std::deque<DeferredAudioEventBatch> deferred;
	size_t deferred_event_count = 0;
	size_t direct_callbacks_inflight = 0;
	uint64_t next_meter_handle = 1;
	bool deferred_overflow = false;
	bool accepting = false;
	bool meter_tick_registered = false;
	std::unordered_map<uint64_t, std::shared_ptr<AudioV2Observer>> observers;
	std::vector<std::shared_ptr<AudioV2Observer>> retired;
	std::unordered_map<uint64_t, std::shared_ptr<AudioMeterSubscription>> meters;
};

struct AudioV2Observer {
	AudioV2State *state = nullptr;
	uint64_t handle = 0;
	obs_weak_source_t *weak = nullptr;
	std::mutex cache_mutex;
	float volume = 1.0f;
	bool muted = false;
	float balance = 0.5f;
	int64_t sync_offset = 0;
	bool monitoring = false;
	uint32_t mixers = 0;
	bool push_to_talk = false;
	uint64_t push_to_talk_delay = 0;
	bool push_to_mute = false;
	uint64_t push_to_mute_delay = 0;
};

namespace {

constexpr float kMaxVolumeMultiplier = 64.0f;
constexpr double kMinVolumeDb = -192.0;
constexpr double kMaxVolumeDb = 36.0;
constexpr long long kMaxSyncOffsetNs = 604800000000000LL;
constexpr long long kMaxGatingDelayMs = 3600000LL;
constexpr size_t kMaxMeterSources = 64;
constexpr uint32_t kMaxMeterHz = 240;
constexpr float kMeterFloorDb = -192.0f;

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "audio operation failed";
	return false;
}

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool read_string_field(obs_data_t *data, const char *name, std::string &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	out = value ? value : "";
	obs_data_item_release(&item);
	return true;
}

bool read_bool_field(obs_data_t *data, const char *name, bool &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_BOOLEAN) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_bool(item);
	obs_data_item_release(&item);
	return true;
}

bool read_object_field(obs_data_t *data, const char *name, ObsDataPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_obj(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
}

bool read_array_field(obs_data_t *data, const char *name, ObsArrayPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_ARRAY) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_array(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
}

bool read_integer_field(obs_data_t *data, const char *name, long long &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_NUMBER || obs_data_item_numtype(item) != OBS_DATA_NUM_INT) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_int(item);
	obs_data_item_release(&item);
	return true;
}

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

bool read_handle_field(obs_data_t *data, const char *name, uint64_t &out)
{
	std::string value;
	bool present = false;
	return read_string_field(data, name, value, present) && present && parse_handle_text(value, out);
}

bool read_meter_source_item(obs_data_t *item, uint64_t &handle)
{
	std::string value;
	bool present = false;
	if (!read_string_field(item, "source", value, present))
		return false;
	if (!present && !read_string_field(item, "value", value, present))
		return false;
	return present && parse_handle_text(value, handle);
}

void set_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	const std::string text = std::to_string(handle);
	obs_data_set_string(data, name, text.c_str());
}

void append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	result.events.push_back(RuntimeV2Event{name, std::move(data)});
}

bool result_has_source_event(const RuntimeV2Result &result, std::string_view name, uint64_t handle)
{
	return std::any_of(result.events.begin(), result.events.end(), [&](const RuntimeV2Event &event) {
		if (event.name != name || !event.data)
			return false;
		uint64_t event_handle = 0;
		return read_handle_field(event.data.get(), "source", event_handle) && event_handle == handle;
	});
}

const char *speaker_layout_name(enum speaker_layout layout)
{
	switch (layout) {
	case SPEAKERS_MONO:
		return "mono";
	case SPEAKERS_STEREO:
		return "stereo";
	case SPEAKERS_2POINT1:
		return "2.1";
	case SPEAKERS_4POINT0:
		return "4.0";
	case SPEAKERS_4POINT1:
		return "4.1";
	case SPEAKERS_5POINT1:
		return "5.1";
	case SPEAKERS_7POINT1:
		return "7.1";
	case SPEAKERS_UNKNOWN:
	default:
		return "unknown";
	}
}

bool finite_float(float value)
{
	return std::isfinite(static_cast<double>(value));
}

double volume_db(float volume)
{
	if (volume <= 0.0f)
		return kMinVolumeDb;
	const double db = static_cast<double>(obs_mul_to_db(volume));
	if (!std::isfinite(db))
		return kMinVolumeDb;
	return std::clamp(db, kMinVolumeDb, kMaxVolumeDb);
}

void set_tracks(obs_data_t *data, uint32_t mixers)
{
	ObsArrayPtr tracks(obs_data_array_create());
	for (size_t index = 0; index < MAX_AUDIO_MIXES; ++index) {
		if ((mixers & (static_cast<uint32_t>(1) << index)) == 0)
			continue;
		ObsDataPtr value(obs_data_create());
		obs_data_set_int(value.get(), "track", static_cast<long long>(index + 1));
		obs_data_array_push_back(tracks.get(), value.get());
	}
	obs_data_set_array(data, "tracks", tracks.get());
}

ObsDataPtr make_volume_data(uint64_t handle, float volume)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	const float safe_volume = finite_float(volume) && volume >= 0.0f ? volume : 0.0f;
	obs_data_set_double(data.get(), "volumeMul", static_cast<double>(safe_volume));
	obs_data_set_double(data.get(), "volumeDb", volume_db(safe_volume));
	obs_data_set_bool(data.get(), "volumeDbFloored", safe_volume == 0.0f);
	return data;
}

ObsDataPtr make_gating_data(uint64_t handle, bool ptt, uint64_t ptt_delay, bool ptm, uint64_t ptm_delay)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	ObsDataPtr talk(obs_data_create());
	obs_data_set_bool(talk.get(), "enabled", ptt);
	obs_data_set_int(talk.get(), "delayMs", static_cast<long long>(std::min<uint64_t>(ptt_delay, INT64_MAX)));
	obs_data_set_obj(data.get(), "pushToTalk", talk.get());
	ObsDataPtr mute(obs_data_create());
	obs_data_set_bool(mute.get(), "enabled", ptm);
	obs_data_set_int(mute.get(), "delayMs", static_cast<long long>(std::min<uint64_t>(ptm_delay, INT64_MAX)));
	obs_data_set_obj(data.get(), "pushToMute", mute.get());
	return data;
}

ObsDataPtr make_audio_data(uint64_t handle, obs_source_t *source)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	const float volume = obs_source_get_volume(source);
	obs_data_set_bool(data.get(), "muted", obs_source_muted(source));
	obs_data_set_double(data.get(), "volumeMul", finite_float(volume) && volume >= 0.0f ? volume : 0.0f);
	obs_data_set_double(data.get(), "volumeDb", volume_db(volume));
	obs_data_set_bool(data.get(), "volumeDbFloored", volume <= 0.0f || !finite_float(volume));
	obs_data_set_double(data.get(), "balance", std::clamp(static_cast<double>(obs_source_get_balance_value(source)), 0.0,
									 1.0));
	obs_data_set_int(data.get(), "syncOffsetNs", static_cast<long long>(obs_source_get_sync_offset(source)));
	obs_data_set_bool(data.get(), "monitoringEnabled", obs_source_get_monitoring_enabled(source));
	set_tracks(data.get(), obs_source_get_audio_mixers(source));
	obs_data_set_string(data.get(), "speakerLayout", speaker_layout_name(obs_source_get_speaker_layout(source)));
	obs_data_set_bool(data.get(), "selfMonitoringAllowed",
			 (obs_source_get_output_flags(source) & OBS_SOURCE_DO_NOT_SELF_MONITOR) == 0);
	ObsDataPtr talk(obs_data_create());
	obs_data_set_bool(talk.get(), "enabled", obs_source_push_to_talk_enabled(source));
	obs_data_set_int(talk.get(), "delayMs", static_cast<long long>(obs_source_get_push_to_talk_delay(source)));
	obs_data_set_obj(data.get(), "pushToTalk", talk.get());
	ObsDataPtr mute(obs_data_create());
	obs_data_set_bool(mute.get(), "enabled", obs_source_push_to_mute_enabled(source));
	obs_data_set_int(mute.get(), "delayMs", static_cast<long long>(obs_source_get_push_to_mute_delay(source)));
	obs_data_set_obj(data.get(), "pushToMute", mute.get());
	return data;
}

class AudioCallbackScope {
public:
	AudioCallbackScope(AudioV2State &state, uint64_t handle) : state_(state)
	{
		std::lock_guard lock(state_.mutex);
		if (!state_.accepting)
			return;
		accepted_ = true;
		route_ = state_.capture_gate.route_for_current_thread();
		if (route_ == SourceEventCaptureRoute::Direct) {
			++state_.direct_callbacks_inflight;
			counted_direct_ = true;
		} else if (route_ == SourceEventCaptureRoute::Capture && state_.capture &&
			   result_has_source_event(*state_.capture, "source.removed", handle)) {
			suppressed_ = true;
		}
	}

	~AudioCallbackScope()
	{
		if (!counted_direct_)
			return;
		std::lock_guard lock(state_.mutex);
		if (state_.direct_callbacks_inflight > 0)
			--state_.direct_callbacks_inflight;
		state_.callback_cv.notify_all();
	}

	bool accepted() const noexcept { return accepted_; }
	bool suppressed() const noexcept { return suppressed_; }

private:
	AudioV2State &state_;
	SourceEventCaptureRoute route_ = SourceEventCaptureRoute::Direct;
	bool accepted_ = false;
	bool counted_direct_ = false;
	bool suppressed_ = false;
};

struct DeferredAudioEventSnapshot {
	std::deque<DeferredAudioEventBatch> batches;
	EventDispatcher *events = nullptr;
	bool overflow = false;
};

void clear_deferred_audio_events(AudioV2State &state)
{
	state.deferred.clear();
	state.deferred_event_count = 0;
	state.deferred_overflow = false;
}

DeferredAudioEventSnapshot take_deferred_audio_events(AudioV2State &state, bool wait_for_pre_capture,
							     bool end_capture)
{
	DeferredAudioEventSnapshot snapshot;
	std::unique_lock lock(state.mutex);
	if (wait_for_pre_capture)
		state.callback_cv.wait(lock, [&] { return !state.accepting || state.direct_callbacks_inflight == 0; });
	if (!state.accepting) {
		clear_deferred_audio_events(state);
		if (end_capture) {
			state.capture = nullptr;
			state.capture_gate.end();
		}
		return snapshot;
	}
	snapshot.events = state.events;
	snapshot.overflow = state.deferred_overflow;
	state.deferred_overflow = false;
	while (!state.deferred.empty()) {
		DeferredAudioEventBatch batch = std::move(state.deferred.front());
		state.deferred.pop_front();
		state.deferred_event_count -= batch.events.size();
		snapshot.batches.push_back(std::move(batch));
	}
	if (end_capture) {
		state.capture = nullptr;
		state.capture_gate.end();
	}
	return snapshot;
}

void publish_deferred_audio_snapshot(DeferredAudioEventSnapshot snapshot, RevisionState::MutationGuard &guard)
{
	if (snapshot.overflow) {
		const uint64_t revision = guard.can_commit_mutation() ? guard.commit_mutation() : guard.current();
		if (snapshot.events)
			snapshot.events->require_resync_due_to_overflow(revision);
	}
	for (DeferredAudioEventBatch &batch : snapshot.batches) {
		if (batch.events.empty())
			continue;
		if (!guard.can_commit_mutation()) {
			if (snapshot.events)
				snapshot.events->require_resync_due_to_overflow(guard.current());
			return;
		}
		const uint64_t revision = guard.commit_mutation();
		if (snapshot.events) {
			for (RuntimeV2Event &event : batch.events)
				snapshot.events->publish(EngineEventKind::State, event.name, revision, event.data.get());
		}
	}
}

void queue_deferred_audio_events_locked(AudioV2State &state, uint64_t handle,
						std::vector<RuntimeV2Event> generated)
{
	if (state.deferred_overflow)
		return;
	if (generated.size() > kDefaultEventQueueCapacity ||
	    state.deferred_event_count > kDefaultEventQueueCapacity - generated.size()) {
		state.deferred.clear();
		state.deferred_event_count = 0;
		state.deferred_overflow = true;
		return;
	}
	state.deferred_event_count += generated.size();
	state.deferred.push_back(DeferredAudioEventBatch{handle, std::move(generated)});
}

bool capture_audio_events_locked(AudioV2State &state, uint64_t handle,
				 std::vector<RuntimeV2Event> &generated)
{
	if (state.capture_gate.route_for_current_thread() != SourceEventCaptureRoute::Capture || !state.capture)
		return false;
	if (result_has_source_event(*state.capture, "source.removed", handle))
		return true;
	for (RuntimeV2Event &event : generated) {
		if (!result_has_source_event(*state.capture, event.name, handle))
			state.capture->events.push_back(std::move(event));
	}
	return true;
}

bool commit_direct_audio_event_locked(AudioV2State &state, RevisionState *&revisions, EventDispatcher *&events,
					      uint64_t &revision)
{
	revisions = state.revisions;
	events = state.events;
	if (!revisions || !events)
		return false;
	if (!revisions->can_commit_mutation()) {
		events->require_resync_due_to_overflow(revisions->current());
		return false;
	}
	revision = revisions->commit_mutation();
	return true;
}

void publish_audio_events(AudioV2State &state, uint64_t handle, std::vector<RuntimeV2Event> generated)
{
	if (generated.empty())
		return;
	EventDispatcher *events = nullptr;
	RevisionState *revisions = nullptr;
	uint64_t revision = 0;
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting)
			return;
		if (capture_audio_events_locked(state, handle, generated))
			return;
		if (state.capture_gate.route_for_current_thread() == SourceEventCaptureRoute::Defer) {
			queue_deferred_audio_events_locked(state, handle, std::move(generated));
			return;
		}
		if (!commit_direct_audio_event_locked(state, revisions, events, revision))
			return;
	}
	for (RuntimeV2Event &event : generated)
		events->publish(EngineEventKind::State, event.name, revision, event.data.get());
}

bool set_cached_bool(bool &cached, bool value)
{
	const bool changed = cached != value;
	cached = value;
	return changed;
}

bool set_cached_delay(uint64_t &cached, calldata_t *calldata)
{
	const uint64_t value = static_cast<uint64_t>(std::max<long long>(0, calldata_int(calldata, "delay")));
	const bool changed = cached != value;
	cached = value;
	return changed;
}

bool is_gating_signal(AudioSignal signal)
{
	return signal == AudioSignal::PushToTalk || signal == AudioSignal::PushToTalkDelay ||
	       signal == AudioSignal::PushToMute || signal == AudioSignal::PushToMuteDelay;
}

bool update_volume_audio_observer(AudioV2Observer &observer, calldata_t *calldata)
{
	const float value = static_cast<float>(calldata_float(calldata, "volume"));
	if (!finite_float(value) || value < 0.0f)
		return false;
	const bool changed = observer.volume != value;
	observer.volume = value;
	return changed;
}

bool update_balance_audio_observer(AudioV2Observer &observer, calldata_t *calldata)
{
	const float value = static_cast<float>(calldata_float(calldata, "balance"));
	if (!finite_float(value))
		return false;
	const bool changed = observer.balance != value;
	observer.balance = value;
	return changed;
}

bool update_scalar_audio_observer(AudioV2Observer &observer, AudioSignal signal, calldata_t *calldata)
{
	switch (signal) {
	case AudioSignal::Volume:
		return update_volume_audio_observer(observer, calldata);
	case AudioSignal::Mute:
		return set_cached_bool(observer.muted, calldata_bool(calldata, "muted"));
	case AudioSignal::Balance:
		return update_balance_audio_observer(observer, calldata);
	case AudioSignal::SyncOffset: {
		const int64_t value = static_cast<int64_t>(calldata_int(calldata, "offset"));
		const bool changed = observer.sync_offset != value;
		observer.sync_offset = value;
		return changed;
	}
	case AudioSignal::Monitoring:
	case AudioSignal::MonitoringCompat:
		return set_cached_bool(observer.monitoring,
				       signal == AudioSignal::MonitoringCompat
					       ? calldata_int(calldata, "type") != OBS_MONITORING_TYPE_NONE
					       : calldata_bool(calldata, "monitor"));
	case AudioSignal::Tracks: {
		const uint32_t value = static_cast<uint32_t>(calldata_int(calldata, "mixers"));
		const bool changed = observer.mixers != value;
		observer.mixers = value;
		return changed;
	}
	default:
		return false;
	}
}

bool update_gating_audio_observer(AudioV2Observer &observer, AudioSignal signal, calldata_t *calldata)
{
	switch (signal) {
	case AudioSignal::PushToTalk:
		return set_cached_bool(observer.push_to_talk, calldata_bool(calldata, "enabled"));
	case AudioSignal::PushToTalkDelay:
		return set_cached_delay(observer.push_to_talk_delay, calldata);
	case AudioSignal::PushToMute:
		return set_cached_bool(observer.push_to_mute, calldata_bool(calldata, "enabled"));
	case AudioSignal::PushToMuteDelay:
		return set_cached_delay(observer.push_to_mute_delay, calldata);
	default:
		return false;
	}
}

bool update_audio_observer(AudioV2Observer &observer, AudioSignal signal, calldata_t *calldata)
{
	return is_gating_signal(signal) ? update_gating_audio_observer(observer, signal, calldata)
					: update_scalar_audio_observer(observer, signal, calldata);
}

void append_scalar_audio_event(AudioV2Observer &observer, AudioSignal signal,
			       std::vector<RuntimeV2Event> &generated)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", observer.handle);
	switch (signal) {
	case AudioSignal::Volume:
		generated.push_back(RuntimeV2Event{"audio.volumeChanged", make_volume_data(observer.handle, observer.volume)});
		return;
	case AudioSignal::Mute:
		obs_data_set_bool(data.get(), "muted", observer.muted);
		generated.push_back(RuntimeV2Event{"audio.muteChanged", std::move(data)});
		return;
	case AudioSignal::Balance:
		obs_data_set_double(data.get(), "balance", std::clamp(static_cast<double>(observer.balance), 0.0, 1.0));
		generated.push_back(RuntimeV2Event{"audio.balanceChanged", std::move(data)});
		return;
	case AudioSignal::SyncOffset:
		obs_data_set_int(data.get(), "syncOffsetNs", static_cast<long long>(observer.sync_offset));
		generated.push_back(RuntimeV2Event{"audio.syncOffsetChanged", std::move(data)});
		return;
	case AudioSignal::Monitoring:
	case AudioSignal::MonitoringCompat:
		obs_data_set_bool(data.get(), "monitoringEnabled", observer.monitoring);
		generated.push_back(RuntimeV2Event{"audio.monitoringChanged", std::move(data)});
		return;
	case AudioSignal::Tracks:
		set_tracks(data.get(), observer.mixers);
		generated.push_back(RuntimeV2Event{"audio.tracksChanged", std::move(data)});
		return;
	default:
		return;
	}
}

void append_audio_signal_event(AudioV2Observer &observer, AudioSignal signal, bool changed,
			       std::vector<RuntimeV2Event> &generated)
{
	if (!changed)
		return;
	if (is_gating_signal(signal)) {
		generated.push_back(RuntimeV2Event{"audio.gatingChanged",
					  make_gating_data(observer.handle, observer.push_to_talk,
							       observer.push_to_talk_delay, observer.push_to_mute,
							       observer.push_to_mute_delay)});
		return;
	}
	append_scalar_audio_event(observer, signal, generated);
}

void collect_audio_signal(AudioV2Observer &observer, AudioSignal signal, calldata_t *calldata)
{
	AudioV2State *state = observer.state;
	if (!state)
		return;
	AudioCallbackScope callback_scope(*state, observer.handle);
	if (!callback_scope.accepted() || callback_scope.suppressed())
		return;

	std::vector<RuntimeV2Event> generated;
	{
		std::lock_guard lock(observer.cache_mutex);
		const bool changed = update_audio_observer(observer, signal, calldata);
		append_audio_signal_event(observer, signal, changed, generated);
	}
	publish_audio_events(*state, observer.handle, std::move(generated));
}

#define AUDIO_SIGNAL_CALLBACK(function_name, signal_name) \
	void function_name(void *data, calldata_t *calldata) \
	{ \
		try { \
			collect_audio_signal(*static_cast<AudioV2Observer *>(data), signal_name, calldata); \
		} catch (...) { \
			std::fprintf(stderr, "obs-engine: audio signal normalization failed\\n"); \
		} \
	}

AUDIO_SIGNAL_CALLBACK(audio_volume_cb, AudioSignal::Volume)
AUDIO_SIGNAL_CALLBACK(audio_mute_cb, AudioSignal::Mute)
AUDIO_SIGNAL_CALLBACK(audio_balance_cb, AudioSignal::Balance)
AUDIO_SIGNAL_CALLBACK(audio_sync_cb, AudioSignal::SyncOffset)
AUDIO_SIGNAL_CALLBACK(audio_monitor_compat_cb, AudioSignal::MonitoringCompat)
AUDIO_SIGNAL_CALLBACK(audio_tracks_cb, AudioSignal::Tracks)
AUDIO_SIGNAL_CALLBACK(audio_ptt_cb, AudioSignal::PushToTalk)
AUDIO_SIGNAL_CALLBACK(audio_ptt_delay_cb, AudioSignal::PushToTalkDelay)
AUDIO_SIGNAL_CALLBACK(audio_ptm_cb, AudioSignal::PushToMute)
AUDIO_SIGNAL_CALLBACK(audio_ptm_delay_cb, AudioSignal::PushToMuteDelay)

#undef AUDIO_SIGNAL_CALLBACK

void connect_audio_observer(AudioV2Observer &observer, obs_source_t *source)
{
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	signal_handler_connect(handler, "volume", audio_volume_cb, &observer);
	signal_handler_connect(handler, "mute", audio_mute_cb, &observer);
	signal_handler_connect(handler, "audio_balance", audio_balance_cb, &observer);
	signal_handler_connect(handler, "audio_sync", audio_sync_cb, &observer);
	signal_handler_connect(handler, "audio_monitoring", audio_monitor_compat_cb, &observer);
	signal_handler_connect(handler, "audio_mixers", audio_tracks_cb, &observer);
	signal_handler_connect(handler, "push_to_talk_changed", audio_ptt_cb, &observer);
	signal_handler_connect(handler, "push_to_talk_delay", audio_ptt_delay_cb, &observer);
	signal_handler_connect(handler, "push_to_mute_changed", audio_ptm_cb, &observer);
	signal_handler_connect(handler, "push_to_mute_delay", audio_ptm_delay_cb, &observer);
}

void disconnect_audio_observer(AudioV2Observer &observer)
{
	obs_source_t *source = observer.weak ? obs_weak_source_get_source(observer.weak) : nullptr;
	if (source) {
		signal_handler_t *handler = obs_source_get_signal_handler(source);
		signal_handler_disconnect(handler, "volume", audio_volume_cb, &observer);
		signal_handler_disconnect(handler, "mute", audio_mute_cb, &observer);
		signal_handler_disconnect(handler, "audio_balance", audio_balance_cb, &observer);
		signal_handler_disconnect(handler, "audio_sync", audio_sync_cb, &observer);
		signal_handler_disconnect(handler, "audio_monitoring", audio_monitor_compat_cb, &observer);
		signal_handler_disconnect(handler, "audio_mixers", audio_tracks_cb, &observer);
		signal_handler_disconnect(handler, "push_to_talk_changed", audio_ptt_cb, &observer);
		signal_handler_disconnect(handler, "push_to_talk_delay", audio_ptt_delay_cb, &observer);
		signal_handler_disconnect(handler, "push_to_mute_changed", audio_ptm_cb, &observer);
		signal_handler_disconnect(handler, "push_to_mute_delay", audio_ptm_delay_cb, &observer);
		obs_source_release(source);
	}
	if (observer.weak) {
		obs_weak_source_release(observer.weak);
		observer.weak = nullptr;
	}
}

void audio_meter_callback(void *data, const float magnitude[MAX_AUDIO_CHANNELS],
				  const float peak[MAX_AUDIO_CHANNELS], const float input_peak[MAX_AUDIO_CHANNELS])
{
	auto *meter = static_cast<AudioMeterSource *>(data);
	if (!meter || !meter->accepting.load(std::memory_order_relaxed))
		return;
	for (size_t index = 0; index < MAX_AUDIO_CHANNELS; ++index) {
		const float safe_magnitude = finite_float(magnitude[index]) ? magnitude[index] : kMeterFloorDb;
		const float safe_peak = finite_float(peak[index]) ? peak[index] : kMeterFloorDb;
		const float safe_input_peak = finite_float(input_peak[index]) ? input_peak[index] : kMeterFloorDb;
		meter->magnitude[index].store(safe_magnitude, std::memory_order_relaxed);
		meter->peak[index].store(safe_peak, std::memory_order_relaxed);
		meter->input_peak[index].store(safe_input_peak, std::memory_order_relaxed);
	}
	meter->sample_sequence.fetch_add(1, std::memory_order_release);
}

uint64_t steady_now_ns()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now().time_since_epoch())
							.count());
}

bool take_due_meter_sources(AudioMeterSubscription &subscription, uint64_t now,
				   std::vector<std::shared_ptr<AudioMeterSource>> &sources)
{
	std::lock_guard lock(subscription.mutex);
	const uint64_t interval = 1000000000ULL / std::max<uint32_t>(subscription.max_hz, 1);
	if (subscription.next_publish_ns != 0 && now < subscription.next_publish_ns)
		return false;
	subscription.next_publish_ns = now + interval;
	sources = subscription.sources;
	return true;
}

int update_meter_channel_count(AudioMeterSource &source)
{
	if (!source.accepting.load(std::memory_order_acquire))
		return 0;
	std::lock_guard lock(source.mutex);
	if (!source.accepting.load(std::memory_order_acquire) || !source.meter)
		return 0;
	const int channels = std::clamp(obs_volmeter_get_nr_channels(source.meter), 0,
				       static_cast<int>(MAX_AUDIO_CHANNELS));
	if (channels > 0)
		source.channel_count.store(channels, std::memory_order_relaxed);
	return channels;
}

ObsDataPtr make_meter_payload(uint64_t subscription_handle, AudioMeterSource &source, int channels)
{
	ObsDataPtr payload(obs_data_create());
	set_handle(payload.get(), "meterSubscription", subscription_handle);
	set_handle(payload.get(), "source", source.source_handle);
	obs_data_set_int(payload.get(), "channelCount", channels);
	ObsArrayPtr magnitude(obs_data_array_create());
	ObsArrayPtr peak(obs_data_array_create());
	ObsArrayPtr input_peak(obs_data_array_create());
	for (int index = 0; index < channels; ++index) {
		ObsDataPtr magnitude_value(obs_data_create());
		ObsDataPtr peak_value(obs_data_create());
		ObsDataPtr input_value(obs_data_create());
		obs_data_set_double(magnitude_value.get(), "value",
				    static_cast<double>(source.magnitude[index].load(std::memory_order_relaxed)));
		obs_data_set_double(peak_value.get(), "value",
				    static_cast<double>(source.peak[index].load(std::memory_order_relaxed)));
		obs_data_set_double(input_value.get(), "value",
				    static_cast<double>(source.input_peak[index].load(std::memory_order_relaxed)));
		obs_data_array_push_back(magnitude.get(), magnitude_value.get());
		obs_data_array_push_back(peak.get(), peak_value.get());
		obs_data_array_push_back(input_peak.get(), input_value.get());
	}
	obs_data_set_array(payload.get(), "magnitudeDb", magnitude.get());
	obs_data_set_array(payload.get(), "peakDb", peak.get());
	obs_data_set_array(payload.get(), "inputPeakDb", input_peak.get());
	return payload;
}

void publish_meter_sample(const AudioMeterSubscription &subscription, AudioMeterSource &source,
			  RevisionState &revisions, EventDispatcher &events)
{
	if (!source.accepting.load(std::memory_order_acquire))
		return;
	const int channels = update_meter_channel_count(source);
	if (channels < 1)
		return;
	const uint64_t first = source.sample_sequence.load(std::memory_order_acquire);
	if (first == 0)
		return;
	ObsDataPtr payload = make_meter_payload(subscription.handle, source, channels);
	const uint64_t second = source.sample_sequence.load(std::memory_order_acquire);
	if (first != second)
		return;
	events.try_publish_telemetry("audio.meter", revisions.current(), payload.get());
}

void audio_meter_tick(void *data, float)
{
	auto *state = static_cast<AudioV2State *>(data);
	if (!state)
		return;
	std::vector<std::shared_ptr<AudioMeterSubscription>> subscriptions;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	{
		std::lock_guard lock(state->mutex);
		if (!state->accepting)
			return;
		revisions = state->revisions;
		events = state->events;
		for (const auto &[_, subscription] : state->meters)
			subscriptions.push_back(subscription);
	}
	if (!events || !revisions)
		return;
	const uint64_t now = steady_now_ns();
	for (const auto &subscription : subscriptions) {
		std::vector<std::shared_ptr<AudioMeterSource>> sources;
		if (!take_due_meter_sources(*subscription, now, sources))
			continue;
		for (const auto &source : sources)
			publish_meter_sample(*subscription, *source, *revisions, *events);
	}
}

void detach_meter_source(AudioMeterSource &source)
{
	source.accepting.store(false, std::memory_order_release);
	obs_volmeter_t *meter = nullptr;
	{
		std::lock_guard lock(source.mutex);
		meter = source.meter;
		source.meter = nullptr;
	}
	if (!meter)
		return;
	obs_volmeter_remove_callback(meter, audio_meter_callback, &source);
	obs_volmeter_detach_source(meter);
	obs_volmeter_destroy(meter);
}

bool attach_meter_source(uint64_t handle, obs_source_t *source, enum obs_peak_meter_type peak_type,
			 std::shared_ptr<AudioMeterSource> &out)
{
	auto meter_source = std::make_shared<AudioMeterSource>();
	meter_source->source_handle = handle;
	meter_source->meter = obs_volmeter_create(OBS_FADER_IEC);
	if (!meter_source->meter)
		return false;
	if (!obs_volmeter_attach_source(meter_source->meter, source)) {
		detach_meter_source(*meter_source);
		return false;
	}
	obs_volmeter_set_peak_meter_type(meter_source->meter, peak_type);
	meter_source->channel_count.store(
		std::clamp(obs_volmeter_get_nr_channels(meter_source->meter), 0, static_cast<int>(MAX_AUDIO_CHANNELS)),
		std::memory_order_relaxed);
	obs_volmeter_add_callback(meter_source->meter, audio_meter_callback, meter_source.get());
	out = std::move(meter_source);
	return true;
}

void detach_meter_sources(std::vector<std::shared_ptr<AudioMeterSource>> &sources) noexcept
{
	for (auto &source : sources)
		detach_meter_source(*source);
}

bool attach_meter_sources(const std::vector<uint64_t> &handles, enum obs_peak_meter_type peak_type,
			  std::vector<std::shared_ptr<AudioMeterSource>> &attached, RuntimeV2Error &error,
			  const std::unordered_map<uint64_t, obs_source_t *> &sources)
{
	try {
		for (uint64_t handle : handles) {
			auto source_it = sources.find(handle);
			if (source_it == sources.end()) {
				detach_meter_sources(attached);
				return fail(error, "not_found", "audio meter source handle was not found");
			}
			std::shared_ptr<AudioMeterSource> meter_source;
			if (!attach_meter_source(handle, source_it->second, peak_type, meter_source)) {
				detach_meter_sources(attached);
				return fail(error, "obs_error", "could not attach audio meter");
			}
			attached.push_back(std::move(meter_source));
		}
	} catch (...) {
		detach_meter_sources(attached);
		throw;
	}
	return true;
}

bool make_meter_subscription(const std::vector<uint64_t> &handles, uint32_t max_hz, enum obs_peak_meter_type peak_type,
			     std::shared_ptr<AudioMeterSubscription> &out, RuntimeV2Error &error,
			     const std::unordered_map<uint64_t, obs_source_t *> &sources)
{
	if (handles.empty() || handles.size() > kMaxMeterSources)
		return fail(error, "bad_request", "sources must contain between 1 and 64 audio source handles");
	if (max_hz == 0 || max_hz > kMaxMeterHz)
		return fail(error, "bad_request", "maxHz must be between 1 and 240");

	auto subscription = std::make_shared<AudioMeterSubscription>();
	subscription->max_hz = max_hz;
	std::vector<std::shared_ptr<AudioMeterSource>> attached;
	if (!attach_meter_sources(handles, peak_type, attached, error, sources))
		return false;
	subscription->sources = std::move(attached);
	out = std::move(subscription);
	return true;
}

bool read_audio_source(Engine &engine, obs_data_t *params, uint64_t &handle, obs_source_t *&source,
			       RuntimeV2Error &error)
{
	return engine.v2_get_audio_source(params, handle, source, error);
}

bool read_gating_fields(obs_data_t *params, bool &enabled, uint64_t &delay, RuntimeV2Error &error)
{
	bool present = false;
	long long raw_delay = 0;
	if (!read_bool_field(params, "enabled", enabled, present) || !present)
		return fail(error, "bad_request", "enabled must be a boolean");
	if (!read_integer_field(params, "delayMs", raw_delay, present) || !present || raw_delay < 0 ||
	    raw_delay > kMaxGatingDelayMs)
		return fail(error, "bad_request", "delayMs must be between 0 and 3600000");
	delay = static_cast<uint64_t>(raw_delay);
	return true;
}

bool read_track_mixers(obs_data_t *params, uint32_t &mixers, RuntimeV2Error &error)
{
	ObsArrayPtr tracks;
	bool present = false;
	if (!read_array_field(params, "tracks", tracks, present) || !present)
		return fail(error, "bad_request", "params.tracks must be an array");
	mixers = 0;
	for (size_t index = 0; index < obs_data_array_count(tracks.get()); ++index) {
		ObsDataPtr item(obs_data_array_item(tracks.get(), index));
		if (!item)
			return fail(error, "bad_request", "each track must be an integer");
		long long track = 0;
		if (!read_integer_field(item.get(), "track", track, present) || !present || track < 1 ||
		    track > MAX_AUDIO_MIXES)
			return fail(error, "bad_request", "tracks must contain unique integers from 1 through 6");
		const uint32_t bit = static_cast<uint32_t>(1) << (track - 1);
		if ((mixers & bit) != 0)
			return fail(error, "bad_request", "tracks must not contain duplicates");
		mixers |= bit;
	}
	return true;
}

void replace_gating_event(RuntimeV2Result &result, uint64_t handle, obs_source_t *source)
{
	const bool ptt = obs_source_push_to_talk_enabled(source);
	const uint64_t ptt_delay = obs_source_get_push_to_talk_delay(source);
	const bool ptm = obs_source_push_to_mute_enabled(source);
	const uint64_t ptm_delay = obs_source_get_push_to_mute_delay(source);
	for (RuntimeV2Event &event : result.events) {
		if (event.name != "audio.gatingChanged" || !event.data)
			continue;
		uint64_t event_handle = 0;
		if (read_handle_field(event.data.get(), "source", event_handle) && event_handle == handle) {
			event.data = make_gating_data(handle, ptt, ptt_delay, ptm, ptm_delay);
			return;
		}
	}
	append_event(result, "audio.gatingChanged", make_gating_data(handle, ptt, ptt_delay, ptm, ptm_delay));
}

} // namespace

bool Engine::v2_get_audio_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
				 RuntimeV2Error &error) const
{
	if (!v2_get_source(params, handle, source, error))
		return false;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
		return fail(error, "unsupported_capability", "source does not advertise audio support");
	return true;
}

void Engine::v2_bind_audio_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!audio_v2_state_)
		audio_v2_state_ = std::make_shared<AudioV2State>();
	{
		std::lock_guard lock(audio_v2_state_->mutex);
		audio_v2_state_->revisions = revisions;
		audio_v2_state_->events = events;
		audio_v2_state_->accepting = revisions && events;
	}
	if (audio_v2_state_->accepting && !audio_v2_state_->meter_tick_registered) {
		obs_add_tick_callback(audio_meter_tick, audio_v2_state_.get());
		audio_v2_state_->meter_tick_registered = true;
	}
}

void Engine::v2_begin_audio_event_capture(RuntimeV2Result &result)
{
	if (!audio_v2_state_)
		return;
	std::lock_guard lock(audio_v2_state_->mutex);
	if (!audio_v2_state_->accepting)
		return;
	audio_v2_state_->capture = &result;
	audio_v2_state_->capture_gate.begin();
}

void Engine::v2_wait_for_audio_event_callbacks()
{
	if (!audio_v2_state_)
		return;
	std::unique_lock lock(audio_v2_state_->mutex);
	audio_v2_state_->callback_cv.wait(lock, [&] {
		return !audio_v2_state_->accepting || audio_v2_state_->direct_callbacks_inflight == 0;
	});
}

void Engine::v2_end_audio_event_capture() noexcept
{
	if (!audio_v2_state_)
		return;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	bool lost = false;
	{
		std::lock_guard lock(audio_v2_state_->mutex);
		audio_v2_state_->capture = nullptr;
		audio_v2_state_->capture_gate.end();
		lost = audio_v2_state_->deferred_overflow || !audio_v2_state_->deferred.empty();
		if (lost) {
			clear_deferred_audio_events(*audio_v2_state_);
			revisions = audio_v2_state_->revisions;
			events = audio_v2_state_->events;
		}
	}
	if (lost && revisions && events)
		events->require_resync_due_to_overflow(revisions->current());
}

void Engine::v2_drain_deferred_audio_events(RevisionState::MutationGuard &guard)
{
	if (audio_v2_state_)
		publish_deferred_audio_snapshot(take_deferred_audio_events(*audio_v2_state_, true, false), guard);
}

void Engine::v2_flush_deferred_audio_events(RevisionState::MutationGuard &guard)
{
	if (audio_v2_state_)
		publish_deferred_audio_snapshot(take_deferred_audio_events(*audio_v2_state_, false, true), guard);
}

bool is_audio_source(obs_source_t *source)
{
	return source && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

void collect_audio_observer_changes(AudioV2State &state, const std::unordered_map<uint64_t, obs_source_t *> &sources,
					std::vector<std::pair<uint64_t, obs_source_t *>> &add,
					std::vector<std::shared_ptr<AudioV2Observer>> &retire)
{
	std::lock_guard lock(state.mutex);
	if (!state.accepting)
		return;
	for (const auto &[handle, source] : sources) {
		if (is_audio_source(source) && !state.observers.contains(handle))
			add.emplace_back(handle, source);
	}
	for (auto it = state.observers.begin(); it != state.observers.end();) {
		auto source_it = sources.find(it->first);
		if (source_it != sources.end() && is_audio_source(source_it->second)) {
			++it;
			continue;
		}
		retire.push_back(it->second);
		it = state.observers.erase(it);
	}
}

std::shared_ptr<AudioV2Observer> create_audio_observer(AudioV2State &state, uint64_t handle, obs_source_t *source)
{
	auto observer = std::make_shared<AudioV2Observer>();
	observer->state = &state;
	observer->handle = handle;
	observer->weak = obs_source_get_weak_source(source);
	observer->volume = obs_source_get_volume(source);
	observer->muted = obs_source_muted(source);
	observer->balance = obs_source_get_balance_value(source);
	observer->sync_offset = obs_source_get_sync_offset(source);
	observer->monitoring = obs_source_get_monitoring_enabled(source);
	observer->mixers = obs_source_get_audio_mixers(source);
	observer->push_to_talk = obs_source_push_to_talk_enabled(source);
	observer->push_to_talk_delay = obs_source_get_push_to_talk_delay(source);
	observer->push_to_mute = obs_source_push_to_mute_enabled(source);
	observer->push_to_mute_delay = obs_source_get_push_to_mute_delay(source);
	connect_audio_observer(*observer, source);
	return observer;
}

void add_audio_observer(AudioV2State &state, const std::unordered_map<uint64_t, obs_source_t *> &sources,
				uint64_t handle, obs_source_t *source,
				std::vector<std::shared_ptr<AudioV2Observer>> &retire)
{
	auto observer = create_audio_observer(state, handle, source);
	bool keep = false;
	{
		std::lock_guard lock(state.mutex);
		if (state.accepting && sources.contains(handle) && !state.observers.contains(handle)) {
			state.observers.emplace(handle, observer);
			keep = true;
		}
	}
	if (keep)
		return;
	retire.push_back(std::move(observer));
}

void Engine::v2_sync_audio_observers()
{
	if (!audio_v2_state_)
		return;
	std::vector<std::shared_ptr<AudioV2Observer>> retire;
	std::vector<std::pair<uint64_t, obs_source_t *>> add;
	collect_audio_observer_changes(*audio_v2_state_, sources_, add, retire);
	for (const auto &[handle, source] : add)
		add_audio_observer(*audio_v2_state_, sources_, handle, source, retire);
	for (auto &observer : retire)
		disconnect_audio_observer(*observer);
	if (!retire.empty()) {
		std::lock_guard lock(audio_v2_state_->mutex);
		audio_v2_state_->retired.insert(audio_v2_state_->retired.end(), retire.begin(), retire.end());
	}
}

void Engine::v2_audio_forget_source(uint64_t handle) noexcept
{
	if (!audio_v2_state_)
		return;
	std::shared_ptr<AudioV2Observer> observer;
	{
		std::lock_guard lock(audio_v2_state_->mutex);
		auto observer_it = audio_v2_state_->observers.find(handle);
		if (observer_it != audio_v2_state_->observers.end()) {
			observer = observer_it->second;
			audio_v2_state_->observers.erase(observer_it);
		}
	}
	if (observer)
		disconnect_audio_observer(*observer);
	for (;;) {
		std::shared_ptr<AudioMeterSource> meter;
		{
			std::lock_guard lock(audio_v2_state_->mutex);
			for (const auto &[_, subscription] : audio_v2_state_->meters) {
				std::lock_guard subscription_lock(subscription->mutex);
				auto source_it = std::find_if(subscription->sources.begin(), subscription->sources.end(),
							 [&](const auto &source) { return source->source_handle == handle; });
				if (source_it == subscription->sources.end())
					continue;
				meter = *source_it;
				subscription->sources.erase(source_it);
				break;
			}
		}
		if (!meter)
			break;
		detach_meter_source(*meter);
	}
}

void Engine::v2_prepare_audio_shutdown() noexcept
{
	if (!audio_v2_state_)
		return;
	if (audio_v2_state_->meter_tick_registered) {
		obs_remove_tick_callback(audio_meter_tick, audio_v2_state_.get());
		audio_v2_state_->meter_tick_registered = false;
	}
	std::vector<std::shared_ptr<AudioV2Observer>> observers;
	std::vector<std::shared_ptr<AudioMeterSubscription>> meters;
	{
		std::lock_guard lock(audio_v2_state_->mutex);
		audio_v2_state_->accepting = false;
		audio_v2_state_->capture = nullptr;
		audio_v2_state_->capture_gate.end();
		clear_deferred_audio_events(*audio_v2_state_);
		audio_v2_state_->callback_cv.notify_all();
		audio_v2_state_->revisions = nullptr;
		audio_v2_state_->events = nullptr;
		for (auto &[_, observer] : audio_v2_state_->observers)
			observers.push_back(observer);
		audio_v2_state_->observers.clear();
		for (auto &[_, subscription] : audio_v2_state_->meters)
			meters.push_back(subscription);
		audio_v2_state_->meters.clear();
	}
	for (auto &observer : observers) {
		try {
			disconnect_audio_observer(*observer);
		} catch (...) {
		}
	}
	for (auto &subscription : meters)
		for (auto &source : subscription->sources)
			detach_meter_source(*source);
}

bool Engine::v2_audio_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	result.data = make_audio_data(handle, source);
	return true;
}

bool Engine::v2_audio_set_mute(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	bool muted = false;
	bool present = false;
	if (!read_bool_field(params, "muted", muted, present) || !present)
		return fail(error, "bad_request", "params.muted must be a boolean");
	const bool before = obs_source_muted(source);
	if (before != muted) {
		obs_source_set_muted(source, muted);
		result.mutated = obs_source_muted(source) != before;
	}
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "muted", obs_source_muted(source));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_toggle_mute(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	const bool before = obs_source_muted(source);
	obs_source_set_muted(source, !before);
	result.mutated = obs_source_muted(source) != before;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "muted", obs_source_muted(source));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_get_volume(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	result.data = make_volume_data(handle, obs_source_get_volume(source));
	return true;
}

bool Engine::v2_audio_set_volume(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	double requested = 0.0;
	bool present = false;
	if (!read_finite_double(params, "volumeMul", 0.0, kMaxVolumeMultiplier, requested, present) || !present)
		return fail(error, "bad_request", "params.volumeMul must be finite and between 0 and 64");
	const float before = obs_source_get_volume(source);
	obs_source_set_volume(source, static_cast<float>(requested));
	const float after = obs_source_get_volume(source);
	result.mutated = before != after;
	result.data = make_volume_data(handle, after);
	return true;
}

bool Engine::v2_audio_set_volume_db(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	double requested = 0.0;
	bool present = false;
	if (!read_finite_double(params, "volumeDb", kMinVolumeDb, kMaxVolumeDb, requested, present) || !present)
		return fail(error, "bad_request", "params.volumeDb must be finite and between -192 and 36");
	const float before = obs_source_get_volume(source);
	const double multiplier = static_cast<double>(obs_db_to_mul(static_cast<float>(requested)));
	if (!std::isfinite(multiplier) || multiplier < 0.0 || multiplier > kMaxVolumeMultiplier)
		return fail(error, "bad_request", "params.volumeDb converts to an unsupported multiplier");
	obs_source_set_volume(source, static_cast<float>(multiplier));
	const float after = obs_source_get_volume(source);
	result.mutated = before != after;
	result.data = make_volume_data(handle, after);
	return true;
}

bool Engine::v2_audio_set_balance(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	if (obs_source_get_speaker_layout(source) != SPEAKERS_STEREO)
		return fail(error, "unsupported_capability", "audio balance requires a stereo source layout");
	double requested = 0.0;
	bool present = false;
	if (!read_finite_double(params, "balance", 0.0, 1.0, requested, present) || !present)
		return fail(error, "bad_request", "params.balance must be finite and between 0 and 1");
	const float before = obs_source_get_balance_value(source);
	obs_source_set_balance_value(source, static_cast<float>(requested));
	const float after = obs_source_get_balance_value(source);
	result.mutated = before != after;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_double(data.get(), "balance", std::clamp(static_cast<double>(after), 0.0, 1.0));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_set_sync_offset(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	long long requested = 0;
	bool present = false;
	if (!read_integer_field(params, "syncOffsetNs", requested, present) || !present ||
	    requested < -kMaxSyncOffsetNs || requested > kMaxSyncOffsetNs)
		return fail(error, "bad_request", "syncOffsetNs is outside the supported signed nanosecond bound");
	const int64_t before = obs_source_get_sync_offset(source);
	obs_source_set_sync_offset(source, static_cast<int64_t>(requested));
	const int64_t after = obs_source_get_sync_offset(source);
	result.mutated = before != after;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_int(data.get(), "syncOffsetNs", static_cast<long long>(after));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_get_monitoring_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "monitoringEnabled", obs_source_get_monitoring_enabled(source));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_set_monitoring_enabled(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	bool enabled = false;
	bool present = false;
	if (!read_bool_field(params, "monitoringEnabled", enabled, present) || !present)
		return fail(error, "bad_request", "params.monitoringEnabled must be a boolean");
	if (enabled && (obs_source_get_output_flags(source) & OBS_SOURCE_DO_NOT_SELF_MONITOR) != 0)
		return fail(error, "unsupported_capability", "source does not allow self-monitoring");
	const bool before = obs_source_get_monitoring_enabled(source);
	obs_source_set_monitoring_enabled(source, enabled);
	const bool after = obs_source_get_monitoring_enabled(source);
	result.mutated = before != after;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "monitoringEnabled", after);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_get_tracks(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	set_tracks(data.get(), obs_source_get_audio_mixers(source));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_set_tracks(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	uint32_t mixers = 0;
	if (!read_track_mixers(params, mixers, error))
		return false;
	const uint32_t before = obs_source_get_audio_mixers(source);
	obs_source_set_audio_mixers(source, mixers);
	const uint32_t after = obs_source_get_audio_mixers(source);
	result.mutated = before != after;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	set_tracks(data.get(), after);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_get_push_to_talk(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	ObsDataPtr talk(obs_data_create());
	obs_data_set_bool(talk.get(), "enabled", obs_source_push_to_talk_enabled(source));
	obs_data_set_int(talk.get(), "delayMs", static_cast<long long>(obs_source_get_push_to_talk_delay(source)));
	obs_data_set_obj(data.get(), "pushToTalk", talk.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_set_push_to_talk(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	bool enabled = false;
	uint64_t delay = 0;
	if (!read_gating_fields(params, enabled, delay, error))
		return false;
	const bool before_enabled = obs_source_push_to_talk_enabled(source);
	const uint64_t before_delay = obs_source_get_push_to_talk_delay(source);
	obs_source_set_push_to_talk_delay(source, delay);
	obs_source_enable_push_to_talk(source, enabled);
	const bool after_enabled = obs_source_push_to_talk_enabled(source);
	const uint64_t after_delay = obs_source_get_push_to_talk_delay(source);
	result.mutated = before_enabled != after_enabled || before_delay != after_delay;
	replace_gating_event(result, handle, source);
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "enabled", after_enabled);
	obs_data_set_int(data.get(), "delayMs", static_cast<long long>(after_delay));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_get_push_to_mute(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	ObsDataPtr mute(obs_data_create());
	obs_data_set_bool(mute.get(), "enabled", obs_source_push_to_mute_enabled(source));
	obs_data_set_int(mute.get(), "delayMs", static_cast<long long>(obs_source_get_push_to_mute_delay(source)));
	obs_data_set_obj(data.get(), "pushToMute", mute.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_set_push_to_mute(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!read_audio_source(*this, params, handle, source, error))
		return false;
	bool enabled = false;
	uint64_t delay = 0;
	if (!read_gating_fields(params, enabled, delay, error))
		return false;
	const bool before_enabled = obs_source_push_to_mute_enabled(source);
	const uint64_t before_delay = obs_source_get_push_to_mute_delay(source);
	obs_source_set_push_to_mute_delay(source, delay);
	obs_source_enable_push_to_mute(source, enabled);
	const bool after_enabled = obs_source_push_to_mute_enabled(source);
	const uint64_t after_delay = obs_source_get_push_to_mute_delay(source);
	result.mutated = before_enabled != after_enabled || before_delay != after_delay;
	replace_gating_event(result, handle, source);
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	obs_data_set_bool(data.get(), "enabled", after_enabled);
	obs_data_set_int(data.get(), "delayMs", static_cast<long long>(after_delay));
	result.data = std::move(data);
	return true;
}

struct MonitoringDevice {
	std::string name;
	std::string id;
};

bool collect_monitoring_device(void *data, const char *name, const char *id)
{
	auto *devices = static_cast<std::vector<MonitoringDevice> *>(data);
	if (!devices || !name || !id || std::strlen(name) > 1024 || std::strlen(id) > 1024)
		return false;
	devices->push_back(MonitoringDevice{name, id});
	return true;
}

bool find_monitoring_device(const std::string &id, MonitoringDevice &found)
{
	if (id == "default") {
		found = MonitoringDevice{"Default", "default"};
		return true;
	}
	std::vector<MonitoringDevice> devices;
	obs_enum_audio_monitoring_devices(collect_monitoring_device, &devices);
	const auto it = std::find_if(devices.begin(), devices.end(), [&](const MonitoringDevice &device) {
		return device.id == id;
	});
	if (it == devices.end())
		return false;
	found = *it;
	return true;
}

bool read_requested_monitoring_device(obs_data_t *params, MonitoringDevice &device, RuntimeV2Error &error)
{
	std::string requested_id;
	bool present = false;
	if (!read_string_field(params, "deviceId", requested_id, present) || !present || requested_id.empty() ||
	    requested_id.size() > 1024)
		return fail(error, "bad_request", "deviceId must be a non-empty bounded string");
	if (!find_monitoring_device(requested_id, device))
		return fail(error, "not_found", "monitoring device was not found");
	return true;
}

ObsDataPtr make_monitoring_device_data(const char *name, const char *id)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "deviceId", id ? id : "");
	obs_data_set_string(data.get(), "name", name ? name : "");
	obs_data_set_bool(data.get(), "isDefault", id && std::strcmp(id, "default") == 0);
	return data;
}

struct MeterRequest {
	std::vector<uint64_t> handles;
	uint32_t max_hz = 20;
	std::string peak_mode = "sample";
	enum obs_peak_meter_type peak_type = SAMPLE_PEAK_METER;
};

bool read_meter_source_handles(obs_data_t *params, std::vector<uint64_t> &handles, RuntimeV2Error &error)
{
	ObsArrayPtr source_array;
	bool present = false;
	if (!read_array_field(params, "sources", source_array, present) || !present)
		return fail(error, "bad_request", "params.sources must be an array");
	std::unordered_set<uint64_t> unique;
	for (size_t index = 0; index < obs_data_array_count(source_array.get()); ++index) {
		ObsDataPtr item(obs_data_array_item(source_array.get(), index));
		uint64_t handle = 0;
		if (!item || !read_meter_source_item(item.get(), handle))
			return fail(error, "bad_request", "each meter source must be a canonical handle string");
		if (!unique.insert(handle).second)
			return fail(error, "bad_request", "meter sources must be unique");
		handles.push_back(handle);
	}
	if (handles.empty() || handles.size() > kMaxMeterSources)
		return fail(error, "bad_request", "sources must contain between 1 and 64 audio source handles");
	return true;
}

bool read_meter_options(obs_data_t *params, MeterRequest &request, RuntimeV2Error &error)
{
	long long raw_hz = 20;
	bool present = false;
	if (!read_integer_field(params, "maxHz", raw_hz, present))
		return fail(error, "bad_request", "maxHz must be an integer");
	if (present && (raw_hz < 1 || raw_hz > kMaxMeterHz))
		return fail(error, "bad_request", "maxHz must be between 1 and 240");
	request.max_hz = static_cast<uint32_t>(raw_hz);
	if (!read_string_field(params, "peakMode", request.peak_mode, present))
		return fail(error, "bad_request", "peakMode must be a string when present");
	if (present && request.peak_mode != "sample" && request.peak_mode != "truePeak")
		return fail(error, "bad_request", "peakMode must be sample or truePeak");
	request.peak_type = request.peak_mode == "truePeak" ? TRUE_PEAK_METER : SAMPLE_PEAK_METER;
	return true;
}

bool read_meter_request(obs_data_t *params, MeterRequest &request, RuntimeV2Error &error)
{
	return read_meter_source_handles(params, request.handles, error) && read_meter_options(params, request, error);
}

bool collect_audio_meter_sources(const std::unordered_map<uint64_t, obs_source_t *> &available,
				 const std::vector<uint64_t> &handles, std::unordered_map<uint64_t, obs_source_t *> &sources,
				 RuntimeV2Error &error)
{
	for (uint64_t handle : handles) {
		auto source_it = available.find(handle);
		if (source_it == available.end())
			return fail(error, "not_found", "audio meter source handle was not found");
		if (!is_audio_source(source_it->second))
			return fail(error, "unsupported_capability", "source does not advertise audio support");
		sources.emplace(handle, source_it->second);
	}
	return true;
}

bool Engine::v2_audio_list_monitoring_devices(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!obs_audio_monitoring_available())
		return fail(error, "unsupported_capability", "audio monitoring is not available on this build");
	std::vector<MonitoringDevice> devices;
	devices.push_back(MonitoringDevice{"Default", "default"});
	obs_enum_audio_monitoring_devices(collect_monitoring_device, &devices);
	ObsArrayPtr array(obs_data_array_create());
	for (const MonitoringDevice &device : devices) {
		ObsDataPtr entry = make_monitoring_device_data(device.name.c_str(), device.id.c_str());
		obs_data_array_push_back(array.get(), entry.get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "devices", array.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(devices.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_get_monitoring_device(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!obs_audio_monitoring_available())
		return fail(error, "unsupported_capability", "audio monitoring is not available on this build");
	const char *name = nullptr;
	const char *id = nullptr;
	obs_get_audio_monitoring_device(&name, &id);
	result.data = make_monitoring_device_data(name, id);
	return true;
}

bool Engine::v2_audio_set_monitoring_device(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!obs_audio_monitoring_available())
		return fail(error, "unsupported_capability", "audio monitoring is not available on this build");
	MonitoringDevice device;
	if (!read_requested_monitoring_device(params, device, error))
		return false;
	const char *old_name = nullptr;
	const char *old_id = nullptr;
	obs_get_audio_monitoring_device(&old_name, &old_id);
	if (old_id && device.id == old_id) {
		result.data = make_monitoring_device_data(old_name, old_id);
		return true;
	}
	if (!obs_set_audio_monitoring_device(device.name.c_str(), device.id.c_str()))
		return fail(error, "obs_error", "libobs rejected the monitoring device");
	const char *new_name = nullptr;
	const char *new_id = nullptr;
	obs_get_audio_monitoring_device(&new_name, &new_id);
	if (!new_id || device.id != new_id)
		return fail(error, "obs_error", "monitoring device did not settle to the requested device");
	result.data = make_monitoring_device_data(new_name, new_id);
	append_event(result, "audio.monitoringDeviceChanged", make_monitoring_device_data(new_name, new_id));
	result.mutated = true;
	return true;
}

bool Engine::v2_audio_subscribe_meters(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!audio_v2_state_)
		return fail(error, "not_available", "audio meter bridge is not available");
	MeterRequest request;
	if (!read_meter_request(params, request, error))
		return false;
	std::unordered_map<uint64_t, obs_source_t *> source_map;
	if (!collect_audio_meter_sources(sources_, request.handles, source_map, error))
		return false;
	auto subscription = std::make_shared<AudioMeterSubscription>();
	subscription->max_hz = request.max_hz;
	if (!make_meter_subscription(request.handles, subscription->max_hz, request.peak_type, subscription, error,
				     source_map))
		return false;
	uint64_t token = 0;
	bool token_space_exhausted = false;
	{
		std::lock_guard lock(audio_v2_state_->mutex);
		if (audio_v2_state_->next_meter_handle == 0) {
			token_space_exhausted = true;
		} else {
			token = audio_v2_state_->next_meter_handle++;
			subscription->handle = token;
			audio_v2_state_->meters.emplace(token, subscription);
		}
	}
	if (token_space_exhausted) {
		detach_meter_sources(subscription->sources);
		return fail(error, "internal_error", "meter subscription space is exhausted");
	}
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "meterSubscription", token);
	obs_data_set_int(data.get(), "maxHz", subscription->max_hz);
	obs_data_set_string(data.get(), "peakMode", request.peak_mode.c_str());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_audio_unsubscribe_meters(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!audio_v2_state_)
		return fail(error, "not_available", "audio meter bridge is not available");
	uint64_t token = 0;
	if (!read_handle_field(params, "meterSubscription", token))
		return fail(error, "bad_request", "meterSubscription must be a canonical handle string");
	std::shared_ptr<AudioMeterSubscription> subscription;
	{
		std::lock_guard lock(audio_v2_state_->mutex);
		auto it = audio_v2_state_->meters.find(token);
		if (it == audio_v2_state_->meters.end())
			return fail(error, "not_found", "meter subscription was not found");
		subscription = it->second;
		audio_v2_state_->meters.erase(it);
	}
	std::vector<std::shared_ptr<AudioMeterSource>> sources;
	{
		std::lock_guard lock(subscription->mutex);
		sources = std::move(subscription->sources);
	}
	for (auto &source : sources)
		detach_meter_source(*source);
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "meterSubscription", token);
	result.data = std::move(data);
	return true;
}

} // namespace obs_engine
