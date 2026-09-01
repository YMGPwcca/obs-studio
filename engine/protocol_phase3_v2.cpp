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

enum class HotkeyMethod {
	List,
	Get,
	GetBindings,
	SetBindings,
	ClearBindings,
	Trigger,
	GetKeyName,
	GetKeyCombinationName,
	GetConflicts,
	GetBackgroundCapture,
	SetBackgroundCapture,
	Export,
	Import,
	Unknown,
};

enum class EncoderMethod {
	KindList,
	KindGet,
	KindDefaults,
	KindProperties,
	KindCapabilities,
	List,
	Get,
	Create,
	Remove,
	Rename,
	GetSettings,
	PatchSettings,
	ReplaceSettings,
	GetProperties,
	GetVideoInput,
	SetVideoInput,
	GetCodec,
	GetType,
	GetDimensions,
	GetState,
	SetScaledSize,
	SetScaleFilter,
	RoiList,
	RoiAdd,
	RoiRemove,
	RoiClear,
	Unknown,
};

struct Phase3Dispatch {
	AudioMethod audio = AudioMethod::Unknown;
	HotkeyMethod hotkey = HotkeyMethod::Unknown;
	EncoderMethod encoder = EncoderMethod::Unknown;
	bool is_hotkey = false;
	bool is_encoder = false;
	bool mutating = false;
};

struct HotkeyMethodName {
	std::string_view name;
	HotkeyMethod method;
};

constexpr HotkeyMethodName kHotkeyMethods[] = {
	{"hotkey.list", HotkeyMethod::List},
	{"hotkey.get", HotkeyMethod::Get},
	{"hotkey.getBindings", HotkeyMethod::GetBindings},
	{"hotkey.setBindings", HotkeyMethod::SetBindings},
	{"hotkey.clearBindings", HotkeyMethod::ClearBindings},
	{"hotkey.trigger", HotkeyMethod::Trigger},
	{"hotkey.getKeyName", HotkeyMethod::GetKeyName},
	{"hotkey.getKeyCombinationName", HotkeyMethod::GetKeyCombinationName},
	{"hotkey.getConflicts", HotkeyMethod::GetConflicts},
	{"hotkey.getBackgroundCapture", HotkeyMethod::GetBackgroundCapture},
	{"hotkey.setBackgroundCapture", HotkeyMethod::SetBackgroundCapture},
	{"hotkey.export", HotkeyMethod::Export},
	{"hotkey.import", HotkeyMethod::Import},
};

struct EncoderMethodName {
	std::string_view name;
	EncoderMethod method;
};

constexpr EncoderMethodName kEncoderMethods[] = {
	{"encoder.kindList", EncoderMethod::KindList},
	{"encoder.kindGet", EncoderMethod::KindGet},
	{"encoder.kindDefaults", EncoderMethod::KindDefaults},
	{"encoder.kindProperties", EncoderMethod::KindProperties},
	{"encoder.kindCapabilities", EncoderMethod::KindCapabilities},
	{"encoder.list", EncoderMethod::List},
	{"encoder.get", EncoderMethod::Get},
	{"encoder.create", EncoderMethod::Create},
	{"encoder.remove", EncoderMethod::Remove},
	{"encoder.rename", EncoderMethod::Rename},
	{"encoder.getSettings", EncoderMethod::GetSettings},
	{"encoder.patchSettings", EncoderMethod::PatchSettings},
	{"encoder.replaceSettings", EncoderMethod::ReplaceSettings},
	{"encoder.getProperties", EncoderMethod::GetProperties},
	{"encoder.getVideoInput", EncoderMethod::GetVideoInput},
	{"encoder.setVideoInput", EncoderMethod::SetVideoInput},
	{"encoder.getCodec", EncoderMethod::GetCodec},
	{"encoder.getType", EncoderMethod::GetType},
	{"encoder.getDimensions", EncoderMethod::GetDimensions},
	{"encoder.getState", EncoderMethod::GetState},
	{"encoder.setScaledSize", EncoderMethod::SetScaledSize},
	{"encoder.setScaleFilter", EncoderMethod::SetScaleFilter},
	{"encoder.roi.list", EncoderMethod::RoiList},
	{"encoder.roi.add", EncoderMethod::RoiAdd},
	{"encoder.roi.remove", EncoderMethod::RoiRemove},
	{"encoder.roi.clear", EncoderMethod::RoiClear},
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

constexpr HotkeyMethod kHotkeyMutatingMethods[] = {
	HotkeyMethod::SetBindings,
	HotkeyMethod::ClearBindings,
	HotkeyMethod::Trigger,
	HotkeyMethod::SetBackgroundCapture,
	HotkeyMethod::Import,
};

constexpr EncoderMethod kEncoderMutatingMethods[] = {
	EncoderMethod::Create,
	EncoderMethod::Remove,
	EncoderMethod::Rename,
	EncoderMethod::PatchSettings,
	EncoderMethod::ReplaceSettings,
	EncoderMethod::SetVideoInput,
	EncoderMethod::SetScaledSize,
	EncoderMethod::SetScaleFilter,
	EncoderMethod::RoiAdd,
	EncoderMethod::RoiRemove,
	EncoderMethod::RoiClear,
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

using HotkeyMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct HotkeyHandlerEntry {
	HotkeyMethod method;
	HotkeyMethodHandler handler;
};

constexpr HotkeyHandlerEntry kHotkeyHandlers[] = {
	{HotkeyMethod::List, &Engine::v2_hotkey_list},
	{HotkeyMethod::Get, &Engine::v2_hotkey_get},
	{HotkeyMethod::GetBindings, &Engine::v2_hotkey_get_bindings},
	{HotkeyMethod::SetBindings, &Engine::v2_hotkey_set_bindings},
	{HotkeyMethod::ClearBindings, &Engine::v2_hotkey_clear_bindings},
	{HotkeyMethod::Trigger, &Engine::v2_hotkey_trigger},
	{HotkeyMethod::GetKeyName, &Engine::v2_hotkey_get_key_name},
	{HotkeyMethod::GetKeyCombinationName, &Engine::v2_hotkey_get_key_combination_name},
	{HotkeyMethod::GetConflicts, &Engine::v2_hotkey_get_conflicts},
	{HotkeyMethod::GetBackgroundCapture, &Engine::v2_hotkey_get_background_capture},
	{HotkeyMethod::SetBackgroundCapture, &Engine::v2_hotkey_set_background_capture},
	{HotkeyMethod::Export, &Engine::v2_hotkey_export},
	{HotkeyMethod::Import, &Engine::v2_hotkey_import},
};

using EncoderMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct EncoderHandlerEntry {
	EncoderMethod method;
	EncoderMethodHandler handler;
};

constexpr EncoderHandlerEntry kEncoderHandlers[] = {
	{EncoderMethod::KindList, &Engine::v2_encoder_kind_list},
	{EncoderMethod::KindGet, &Engine::v2_encoder_kind_get},
	{EncoderMethod::KindDefaults, &Engine::v2_encoder_kind_defaults},
	{EncoderMethod::KindProperties, &Engine::v2_encoder_kind_properties},
	{EncoderMethod::KindCapabilities, &Engine::v2_encoder_kind_capabilities},
	{EncoderMethod::List, &Engine::v2_encoder_list},
	{EncoderMethod::Get, &Engine::v2_encoder_get},
	{EncoderMethod::Create, &Engine::v2_encoder_create},
	{EncoderMethod::Remove, &Engine::v2_encoder_remove},
	{EncoderMethod::Rename, &Engine::v2_encoder_rename},
	{EncoderMethod::GetSettings, &Engine::v2_encoder_get_settings},
	{EncoderMethod::PatchSettings, &Engine::v2_encoder_patch_settings},
	{EncoderMethod::ReplaceSettings, &Engine::v2_encoder_replace_settings},
	{EncoderMethod::GetProperties, &Engine::v2_encoder_get_properties},
	{EncoderMethod::GetVideoInput, &Engine::v2_encoder_get_video_input},
	{EncoderMethod::SetVideoInput, &Engine::v2_encoder_set_video_input},
	{EncoderMethod::GetCodec, &Engine::v2_encoder_get_codec},
	{EncoderMethod::GetType, &Engine::v2_encoder_get_type},
	{EncoderMethod::GetDimensions, &Engine::v2_encoder_get_dimensions},
	{EncoderMethod::GetState, &Engine::v2_encoder_get_state},
	{EncoderMethod::SetScaledSize, &Engine::v2_encoder_set_scaled_size},
	{EncoderMethod::SetScaleFilter, &Engine::v2_encoder_set_scale_filter},
	{EncoderMethod::RoiList, &Engine::v2_encoder_roi_list},
	{EncoderMethod::RoiAdd, &Engine::v2_encoder_roi_add},
	{EncoderMethod::RoiRemove, &Engine::v2_encoder_roi_remove},
	{EncoderMethod::RoiClear, &Engine::v2_encoder_roi_clear},
};

AudioMethod classify(std::string_view name)
{
	for (const AudioMethodName &entry : kAudioMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return AudioMethod::Unknown;
}

HotkeyMethod classify_hotkey(std::string_view name)
{
	for (const HotkeyMethodName &entry : kHotkeyMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return HotkeyMethod::Unknown;
}

EncoderMethod classify_encoder(std::string_view name)
{
	for (const EncoderMethodName &entry : kEncoderMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return EncoderMethod::Unknown;
}

bool is_mutating(AudioMethod method)
{
	for (const AudioMethod candidate : kMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool is_mutating(HotkeyMethod method)
{
	for (const HotkeyMethod candidate : kHotkeyMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool is_mutating(EncoderMethod method)
{
	for (const EncoderMethod candidate : kEncoderMutatingMethods) {
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

HotkeyMethodHandler hotkey_handler_for(HotkeyMethod method)
{
	for (const HotkeyHandlerEntry &entry : kHotkeyHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

EncoderMethodHandler encoder_handler_for(EncoderMethod method)
{
	for (const EncoderHandlerEntry &entry : kEncoderHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

bool classify_phase3(std::string_view name, Phase3Dispatch &dispatch)
{
	dispatch.audio = classify(name);
	dispatch.hotkey = classify_hotkey(name);
	dispatch.encoder = classify_encoder(name);
	if (dispatch.audio == AudioMethod::Unknown && dispatch.hotkey == HotkeyMethod::Unknown &&
	    dispatch.encoder == EncoderMethod::Unknown)
		return false;
	dispatch.is_hotkey = dispatch.hotkey != HotkeyMethod::Unknown;
	dispatch.is_encoder = dispatch.encoder != EncoderMethod::Unknown;
	dispatch.mutating = dispatch.is_encoder ? is_mutating(dispatch.encoder)
						: dispatch.is_hotkey ? is_mutating(dispatch.hotkey) : is_mutating(dispatch.audio);
	return true;
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

bool prepare(const V2Request &request, bool mutating, Engine &engine, RevisionState &revisions,
		     RuntimeV2Result &result, std::optional<Phase3CaptureScope> &capture,
		     std::optional<RevisionState::MutationGuard> &guard)
{
	if (mutating) {
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

bool execute_audio(Engine &engine, AudioMethod method, const V2Request &request, RuntimeV2Result &result,
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

bool execute_hotkey(Engine &engine, HotkeyMethod method, const V2Request &request, RuntimeV2Result &result,
			    RuntimeV2Error &error)
{
	const HotkeyMethodHandler handler = hotkey_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "hotkey method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_encoder(Engine &engine, EncoderMethod method, const V2Request &request, RuntimeV2Result &result,
			     RuntimeV2Error &error)
{
	const EncoderMethodHandler handler = encoder_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "encoder method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_phase3(Engine &engine, const Phase3Dispatch &dispatch, const V2Request &request,
			   RuntimeV2Result &result, RuntimeV2Error &error)
{
	if (dispatch.is_encoder)
		return execute_encoder(engine, dispatch.encoder, request, result, error);
	if (dispatch.is_hotkey)
		return execute_hotkey(engine, dispatch.hotkey, request, result, error);
	return execute_audio(engine, dispatch.audio, request, result, error);
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
		error.message = "Phase 3 dispatch failed";
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
		error.message = "read-only Phase 3 method mutated engine state";
		return false;
	}
	revision = guard->commit_mutation();
	return true;
}

} // namespace

bool is_phase3_method(std::string_view method)
{
	return classify(method) != AudioMethod::Unknown || classify_hotkey(method) != HotkeyMethod::Unknown ||
	       classify_encoder(method) != EncoderMethod::Unknown;
}

bool handle_phase3_request(Engine &engine, RevisionState &revisions, EventDispatcher &events,
			   const V2Request &request)
{
	Phase3Dispatch dispatch;
	if (!classify_phase3(request.method, dispatch))
		return false;

	RuntimeV2Result result;
	RuntimeV2Error error;
	std::optional<Phase3CaptureScope> capture;
	std::optional<RevisionState::MutationGuard> guard;
	if (!prepare(request, dispatch.mutating, engine, revisions, result, capture, guard))
		return true;

	const bool succeeded = execute_phase3(engine, dispatch, request, result, error);
	if (dispatch.hotkey == HotkeyMethod::Trigger && !result.events.empty())
		result.mutated = true;
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
