#include "protocol_phase3_v2.hpp"

#include "events.hpp"
#include "protocol_v2.hpp"
#include "revision.hpp"
#include "runtime.hpp"

#include <obs.h>

#include <optional>
#include <string_view>

namespace obs_engine {
namespace {

enum class AudioMethod {
	Get,
	SetMute,
	ToggleMute,
	GetVolume,
	SetVolume,
	SetVolumeDb,
	SetBalance,
	SetSyncOffset,
	GetMonitoringEnabled,
	SetMonitoringEnabled,
	GetTracks,
	SetTracks,
	GetPushToTalk,
	SetPushToTalk,
	GetPushToMute,
	SetPushToMute,
	ListMonitoringDevices,
	GetMonitoringDevice,
	SetMonitoringDevice,
	SubscribeMeters,
	UnsubscribeMeters,
	Unknown,
};

struct AudioMethodName {
	std::string_view name;
	AudioMethod method;
};

constexpr AudioMethodName kAudioMethods[] = {
	{"audio.get", AudioMethod::Get},
	{"audio.setMute", AudioMethod::SetMute},
	{"audio.toggleMute", AudioMethod::ToggleMute},
	{"audio.getVolume", AudioMethod::GetVolume},
	{"audio.setVolume", AudioMethod::SetVolume},
	{"audio.setVolumeDb", AudioMethod::SetVolumeDb},
	{"audio.setBalance", AudioMethod::SetBalance},
	{"audio.setSyncOffset", AudioMethod::SetSyncOffset},
	{"audio.getMonitoringEnabled", AudioMethod::GetMonitoringEnabled},
	{"audio.setMonitoringEnabled", AudioMethod::SetMonitoringEnabled},
	{"audio.getTracks", AudioMethod::GetTracks},
	{"audio.setTracks", AudioMethod::SetTracks},
	{"audio.getPushToTalk", AudioMethod::GetPushToTalk},
	{"audio.setPushToTalk", AudioMethod::SetPushToTalk},
	{"audio.getPushToMute", AudioMethod::GetPushToMute},
	{"audio.setPushToMute", AudioMethod::SetPushToMute},
	{"audio.listMonitoringDevices", AudioMethod::ListMonitoringDevices},
	{"audio.getMonitoringDevice", AudioMethod::GetMonitoringDevice},
	{"audio.setMonitoringDevice", AudioMethod::SetMonitoringDevice},
	{"audio.subscribeMeters", AudioMethod::SubscribeMeters},
	{"audio.unsubscribeMeters", AudioMethod::UnsubscribeMeters},
};

constexpr AudioMethod kMutatingMethods[] = {
	AudioMethod::SetMute,
	AudioMethod::ToggleMute,
	AudioMethod::SetVolume,
	AudioMethod::SetVolumeDb,
	AudioMethod::SetBalance,
	AudioMethod::SetSyncOffset,
	AudioMethod::SetMonitoringEnabled,
	AudioMethod::SetTracks,
	AudioMethod::SetPushToTalk,
	AudioMethod::SetPushToMute,
	AudioMethod::SetMonitoringDevice,
};

using AudioMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct AudioHandlerEntry {
	AudioMethod method;
	AudioMethodHandler handler;
};

constexpr AudioHandlerEntry kHandlers[] = {
	{AudioMethod::Get, &Engine::v2_audio_get},
	{AudioMethod::SetMute, &Engine::v2_audio_set_mute},
	{AudioMethod::ToggleMute, &Engine::v2_audio_toggle_mute},
	{AudioMethod::GetVolume, &Engine::v2_audio_get_volume},
	{AudioMethod::SetVolume, &Engine::v2_audio_set_volume},
	{AudioMethod::SetVolumeDb, &Engine::v2_audio_set_volume_db},
	{AudioMethod::SetBalance, &Engine::v2_audio_set_balance},
	{AudioMethod::SetSyncOffset, &Engine::v2_audio_set_sync_offset},
	{AudioMethod::GetMonitoringEnabled, &Engine::v2_audio_get_monitoring_enabled},
	{AudioMethod::SetMonitoringEnabled, &Engine::v2_audio_set_monitoring_enabled},
	{AudioMethod::GetTracks, &Engine::v2_audio_get_tracks},
	{AudioMethod::SetTracks, &Engine::v2_audio_set_tracks},
	{AudioMethod::GetPushToTalk, &Engine::v2_audio_get_push_to_talk},
	{AudioMethod::SetPushToTalk, &Engine::v2_audio_set_push_to_talk},
	{AudioMethod::GetPushToMute, &Engine::v2_audio_get_push_to_mute},
	{AudioMethod::SetPushToMute, &Engine::v2_audio_set_push_to_mute},
	{AudioMethod::ListMonitoringDevices, &Engine::v2_audio_list_monitoring_devices},
	{AudioMethod::GetMonitoringDevice, &Engine::v2_audio_get_monitoring_device},
	{AudioMethod::SetMonitoringDevice, &Engine::v2_audio_set_monitoring_device},
	{AudioMethod::SubscribeMeters, &Engine::v2_audio_subscribe_meters},
	{AudioMethod::UnsubscribeMeters, &Engine::v2_audio_unsubscribe_meters},
};

AudioMethod classify(std::string_view name)
{
	for (const AudioMethodName &entry : kAudioMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return AudioMethod::Unknown;
}

bool is_mutating(AudioMethod method)
{
	for (const AudioMethod candidate : kMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

AudioMethodHandler handler_for(AudioMethod method)
{
	for (const AudioHandlerEntry &entry : kHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

bool reject_read_guard(const V2Request &request, uint64_t revision)
{
	if (!request.has_if_revision)
		return false;
	send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
			      revision);
	return true;
}

bool validate_mutation_guard(const V2Request &request, RevisionState::MutationGuard &guard)
{
	if (!request.has_if_revision || guard.matches(static_cast<uint64_t>(request.if_revision)))
		return true;
	ObsDataPtr details(obs_data_create());
	obs_data_set_int(details.get(), "expectedRevision", request.if_revision);
	obs_data_set_int(details.get(), "actualRevision", static_cast<long long>(guard.current()));
	send_v2_error(request.id, "revision_conflict", "ifRevision does not match the current engine revision",
		      details.get(), guard.current());
	return false;
}

class Phase3CaptureScope {
public:
	Phase3CaptureScope(Engine &engine, RuntimeV2Result &result) : engine_(engine)
	{
		engine_.v2_begin_event_capture(result);
		engine_.v2_wait_for_event_capture_callbacks();
	}

	~Phase3CaptureScope()
	{
		if (active_)
			engine_.v2_end_event_capture();
	}

	void flush(RevisionState::MutationGuard &guard)
	{
		if (!active_)
			return;
		engine_.v2_flush_deferred_source_events(guard);
		active_ = false;
	}

	Phase3CaptureScope(const Phase3CaptureScope &) = delete;
	Phase3CaptureScope &operator=(const Phase3CaptureScope &) = delete;

private:
	Engine &engine_;
	bool active_ = true;
};

bool prepare(const V2Request &request, AudioMethod method, Engine &engine, RevisionState &revisions,
		     RuntimeV2Result &result, std::optional<Phase3CaptureScope> &capture,
		     std::optional<RevisionState::MutationGuard> &guard)
{
	if (is_mutating(method)) {
		capture.emplace(engine, result);
		guard.emplace(revisions.lock_mutation());
		engine.v2_drain_deferred_source_events(*guard);
		if (!validate_mutation_guard(request, *guard)) {
			capture->flush(*guard);
			return false;
		}
		if (!guard->can_commit_mutation()) {
			send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr,
				      guard->current());
			capture->flush(*guard);
			return false;
		}
	} else if (reject_read_guard(request, revisions.current())) {
		return false;
	}
	return true;
}

bool execute(Engine &engine, AudioMethod method, const V2Request &request, RuntimeV2Result &result,
		      RuntimeV2Error &error)
{
	const AudioMethodHandler handler = handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "audio method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

void publish_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events)
		events.publish(EngineEventKind::State, event.name, revision, event.data.get());
}

void fail_phase3_request(const V2Request &request, RevisionState &revisions, RuntimeV2Error &error,
				 std::optional<Phase3CaptureScope> &capture,
				 std::optional<RevisionState::MutationGuard> &guard)
{
	if (error.code.empty()) {
		error.code = "internal_error";
		error.message = "audio dispatch failed";
	}
	send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
		      guard ? guard->current() : revisions.current());
	if (capture)
		capture->flush(*guard);
}

bool commit_phase3_result(RuntimeV2Result &result, std::optional<RevisionState::MutationGuard> &guard,
				 RevisionState &revisions, uint64_t &revision, RuntimeV2Error &error)
{
	revision = guard ? guard->current() : revisions.current();
	if (!result.mutated)
		return true;
	if (!guard) {
		error.code = "internal_error";
		error.message = "read-only audio method mutated engine state";
		return false;
	}
	revision = guard->commit_mutation();
	return true;
}

} // namespace

bool is_phase3_method(std::string_view method)
{
	return classify(method) != AudioMethod::Unknown;
}

bool handle_phase3_request(Engine &engine, RevisionState &revisions, EventDispatcher &events,
			   const V2Request &request)
{
	const AudioMethod method = classify(request.method);
	if (method == AudioMethod::Unknown)
		return false;

	RuntimeV2Result result;
	RuntimeV2Error error;
	std::optional<Phase3CaptureScope> capture;
	std::optional<RevisionState::MutationGuard> guard;
	if (!prepare(request, method, engine, revisions, result, capture, guard))
		return true;

	const bool succeeded = execute(engine, method, request, result, error);
	engine.v2_sync_source_observers();
	if (!succeeded) {
		fail_phase3_request(request, revisions, error, capture, guard);
		return true;
	}

	uint64_t revision = 0;
	if (!commit_phase3_result(result, guard, revisions, revision, error)) {
		fail_phase3_request(request, revisions, error, capture, guard);
		return true;
	}

	send_v2_ok(request.id, result.data.get(), revision);
	publish_events(events, revision, result);
	if (capture)
		capture->flush(*guard);
	return true;
}

} // namespace obs_engine
