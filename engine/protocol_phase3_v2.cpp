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

enum class EncoderGroupMethod {
	List,
	Get,
	Create,
	Remove,
	Add,
	RemoveEncoder,
	GetEncoders,
	Unknown,
};

enum class ServiceMethod {
	KindList,
	KindDefaults,
	KindProperties,
	List,
	Get,
	Create,
	Remove,
	Rename,
	GetSettings,
	PatchSettings,
	ReplaceSettings,
	GetProperties,
	GetProtocol,
	GetPreferredOutputKind,
	GetSupportedResolutions,
	GetMaxFps,
	GetMaxBitrates,
	GetSupportedVideoCodecs,
	GetSupportedAudioCodecs,
	GetEncoderRecommendations,
	CanConnect,
	Unknown,
};

enum class OutputMethod {
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
	SetService,
	GetService,
	SetVideoEncoder,
	SetAudioEncoder,
	GetEncoders,
	Start,
	Stop,
	ForceStop,
	GetState,
	SetPaused,
	GetPaused,
	SetDelay,
	GetDelay,
	SetReconnect,
	GetReconnect,
	GetStats,
	GetLastError,
	GetSupportedCodecs,
	Unknown,
};

enum class RecordingMethod {
	GetConfig,
	Configure,
	Unconfigure,
	Start,
	Stop,
	ForceStop,
	Pause,
	Resume,
	TogglePause,
	SplitFile,
	AddChapter,
	GetState,
	GetStats,
	GetCurrentPath,
	GetLastFile,
	Unknown,
};

enum class StreamingMethod {
	GetConfig,
	Configure,
	Unconfigure,
	Start,
	Stop,
	ForceStop,
	GetState,
	GetStats,
	GetService,
	SetService,
	GetReconnectState,
	GetLastError,
	Unknown,
};

struct Phase3Dispatch {
	AudioMethod audio = AudioMethod::Unknown;
	HotkeyMethod hotkey = HotkeyMethod::Unknown;
	EncoderMethod encoder = EncoderMethod::Unknown;
	EncoderGroupMethod encoder_group = EncoderGroupMethod::Unknown;
	ServiceMethod service = ServiceMethod::Unknown;
	OutputMethod output = OutputMethod::Unknown;
	RecordingMethod recording = RecordingMethod::Unknown;
	StreamingMethod streaming = StreamingMethod::Unknown;
	bool is_hotkey = false;
	bool is_encoder = false;
	bool is_encoder_group = false;
	bool is_service = false;
	bool is_output = false;
	bool is_recording = false;
	bool is_streaming = false;
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

struct EncoderGroupMethodName {
	std::string_view name;
	EncoderGroupMethod method;
};

constexpr EncoderGroupMethodName kEncoderGroupMethods[] = {
	{"encoderGroup.list", EncoderGroupMethod::List},
	{"encoderGroup.get", EncoderGroupMethod::Get},
	{"encoderGroup.create", EncoderGroupMethod::Create},
	{"encoderGroup.remove", EncoderGroupMethod::Remove},
	{"encoderGroup.add", EncoderGroupMethod::Add},
	{"encoderGroup.removeEncoder", EncoderGroupMethod::RemoveEncoder},
	{"encoderGroup.getEncoders", EncoderGroupMethod::GetEncoders},
};

struct ServiceMethodName {
	std::string_view name;
	ServiceMethod method;
};

constexpr ServiceMethodName kServiceMethods[] = {
	{"service.kindList", ServiceMethod::KindList},
	{"service.kindDefaults", ServiceMethod::KindDefaults},
	{"service.kindProperties", ServiceMethod::KindProperties},
	{"service.list", ServiceMethod::List},
	{"service.get", ServiceMethod::Get},
	{"service.create", ServiceMethod::Create},
	{"service.remove", ServiceMethod::Remove},
	{"service.rename", ServiceMethod::Rename},
	{"service.getSettings", ServiceMethod::GetSettings},
	{"service.patchSettings", ServiceMethod::PatchSettings},
	{"service.replaceSettings", ServiceMethod::ReplaceSettings},
	{"service.getProperties", ServiceMethod::GetProperties},
	{"service.getProtocol", ServiceMethod::GetProtocol},
	{"service.getPreferredOutputKind", ServiceMethod::GetPreferredOutputKind},
	{"service.getSupportedResolutions", ServiceMethod::GetSupportedResolutions},
	{"service.getMaxFps", ServiceMethod::GetMaxFps},
	{"service.getMaxBitrates", ServiceMethod::GetMaxBitrates},
	{"service.getSupportedVideoCodecs", ServiceMethod::GetSupportedVideoCodecs},
	{"service.getSupportedAudioCodecs", ServiceMethod::GetSupportedAudioCodecs},
	{"service.getEncoderRecommendations", ServiceMethod::GetEncoderRecommendations},
	{"service.canConnect", ServiceMethod::CanConnect},
};

struct OutputMethodName {
	std::string_view name;
	OutputMethod method;
};

constexpr OutputMethodName kOutputMethods[] = {
	{"output.kindList", OutputMethod::KindList},
	{"output.kindGet", OutputMethod::KindGet},
	{"output.kindDefaults", OutputMethod::KindDefaults},
	{"output.kindProperties", OutputMethod::KindProperties},
	{"output.kindCapabilities", OutputMethod::KindCapabilities},
	{"output.list", OutputMethod::List},
	{"output.get", OutputMethod::Get},
	{"output.create", OutputMethod::Create},
	{"output.remove", OutputMethod::Remove},
	{"output.rename", OutputMethod::Rename},
	{"output.getSettings", OutputMethod::GetSettings},
	{"output.patchSettings", OutputMethod::PatchSettings},
	{"output.replaceSettings", OutputMethod::ReplaceSettings},
	{"output.getProperties", OutputMethod::GetProperties},
	{"output.setService", OutputMethod::SetService},
	{"output.getService", OutputMethod::GetService},
	{"output.setVideoEncoder", OutputMethod::SetVideoEncoder},
	{"output.setAudioEncoder", OutputMethod::SetAudioEncoder},
	{"output.getEncoders", OutputMethod::GetEncoders},
	{"output.start", OutputMethod::Start},
	{"output.stop", OutputMethod::Stop},
	{"output.forceStop", OutputMethod::ForceStop},
	{"output.getState", OutputMethod::GetState},
	{"output.setPaused", OutputMethod::SetPaused},
	{"output.getPaused", OutputMethod::GetPaused},
	{"output.setDelay", OutputMethod::SetDelay},
	{"output.getDelay", OutputMethod::GetDelay},
	{"output.setReconnect", OutputMethod::SetReconnect},
	{"output.getReconnect", OutputMethod::GetReconnect},
	{"output.getStats", OutputMethod::GetStats},
	{"output.getLastError", OutputMethod::GetLastError},
	{"output.getSupportedCodecs", OutputMethod::GetSupportedCodecs},
};

struct RecordingMethodName {
	std::string_view name;
	RecordingMethod method;
};

constexpr RecordingMethodName kRecordingMethods[] = {
	{"recording.getConfig", RecordingMethod::GetConfig},
	{"recording.configure", RecordingMethod::Configure},
	{"recording.unconfigure", RecordingMethod::Unconfigure},
	{"recording.start", RecordingMethod::Start},
	{"recording.stop", RecordingMethod::Stop},
	{"recording.forceStop", RecordingMethod::ForceStop},
	{"recording.pause", RecordingMethod::Pause},
	{"recording.resume", RecordingMethod::Resume},
	{"recording.togglePause", RecordingMethod::TogglePause},
	{"recording.splitFile", RecordingMethod::SplitFile},
	{"recording.addChapter", RecordingMethod::AddChapter},
	{"recording.getState", RecordingMethod::GetState},
	{"recording.getStats", RecordingMethod::GetStats},
	{"recording.getCurrentPath", RecordingMethod::GetCurrentPath},
	{"recording.getLastFile", RecordingMethod::GetLastFile},
};

struct StreamingMethodName {
	std::string_view name;
	StreamingMethod method;
};

constexpr StreamingMethodName kStreamingMethods[] = {
	{"streaming.getConfig", StreamingMethod::GetConfig},
	{"streaming.configure", StreamingMethod::Configure},
	{"streaming.unconfigure", StreamingMethod::Unconfigure},
	{"streaming.start", StreamingMethod::Start},
	{"streaming.stop", StreamingMethod::Stop},
	{"streaming.forceStop", StreamingMethod::ForceStop},
	{"streaming.getState", StreamingMethod::GetState},
	{"streaming.getStats", StreamingMethod::GetStats},
	{"streaming.getService", StreamingMethod::GetService},
	{"streaming.setService", StreamingMethod::SetService},
	{"streaming.getReconnectState", StreamingMethod::GetReconnectState},
	{"streaming.getLastError", StreamingMethod::GetLastError},
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

constexpr EncoderGroupMethod kEncoderGroupMutatingMethods[] = {
	EncoderGroupMethod::Create,
	EncoderGroupMethod::Remove,
	EncoderGroupMethod::Add,
	EncoderGroupMethod::RemoveEncoder,
};

constexpr ServiceMethod kServiceMutatingMethods[] = {
	ServiceMethod::Create,
	ServiceMethod::Remove,
	ServiceMethod::Rename,
	ServiceMethod::PatchSettings,
	ServiceMethod::ReplaceSettings,
};

constexpr OutputMethod kOutputMutatingMethods[] = {
	OutputMethod::Create,
	OutputMethod::Remove,
	OutputMethod::Rename,
	OutputMethod::PatchSettings,
	OutputMethod::ReplaceSettings,
	OutputMethod::SetService,
	OutputMethod::SetVideoEncoder,
	OutputMethod::SetAudioEncoder,
	OutputMethod::Start,
	OutputMethod::Stop,
	OutputMethod::ForceStop,
	OutputMethod::SetPaused,
	OutputMethod::SetDelay,
	OutputMethod::SetReconnect,
};

constexpr RecordingMethod kRecordingMutatingMethods[] = {
	RecordingMethod::Configure,
	RecordingMethod::Unconfigure,
	RecordingMethod::Start,
	RecordingMethod::Stop,
	RecordingMethod::ForceStop,
	RecordingMethod::Pause,
	RecordingMethod::Resume,
	RecordingMethod::TogglePause,
	RecordingMethod::SplitFile,
	RecordingMethod::AddChapter,
};

constexpr StreamingMethod kStreamingMutatingMethods[] = {
	StreamingMethod::Configure,
	StreamingMethod::Unconfigure,
	StreamingMethod::Start,
	StreamingMethod::Stop,
	StreamingMethod::ForceStop,
	StreamingMethod::SetService,
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

using EncoderGroupMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct EncoderGroupHandlerEntry {
	EncoderGroupMethod method;
	EncoderGroupMethodHandler handler;
};

constexpr EncoderGroupHandlerEntry kEncoderGroupHandlers[] = {
	{EncoderGroupMethod::List, &Engine::v2_encoder_group_list},
	{EncoderGroupMethod::Get, &Engine::v2_encoder_group_get},
	{EncoderGroupMethod::Create, &Engine::v2_encoder_group_create},
	{EncoderGroupMethod::Remove, &Engine::v2_encoder_group_remove},
	{EncoderGroupMethod::Add, &Engine::v2_encoder_group_add},
	{EncoderGroupMethod::RemoveEncoder, &Engine::v2_encoder_group_remove_encoder},
	{EncoderGroupMethod::GetEncoders, &Engine::v2_encoder_group_get_encoders},
};

using ServiceMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct ServiceHandlerEntry {
	ServiceMethod method;
	ServiceMethodHandler handler;
};

constexpr ServiceHandlerEntry kServiceHandlers[] = {
	{ServiceMethod::KindList, &Engine::v2_service_kind_list},
	{ServiceMethod::KindDefaults, &Engine::v2_service_kind_defaults},
	{ServiceMethod::KindProperties, &Engine::v2_service_kind_properties},
	{ServiceMethod::List, &Engine::v2_service_list},
	{ServiceMethod::Get, &Engine::v2_service_get},
	{ServiceMethod::Create, &Engine::v2_service_create},
	{ServiceMethod::Remove, &Engine::v2_service_remove},
	{ServiceMethod::Rename, &Engine::v2_service_rename},
	{ServiceMethod::GetSettings, &Engine::v2_service_get_settings},
	{ServiceMethod::PatchSettings, &Engine::v2_service_patch_settings},
	{ServiceMethod::ReplaceSettings, &Engine::v2_service_replace_settings},
	{ServiceMethod::GetProperties, &Engine::v2_service_get_properties},
	{ServiceMethod::GetProtocol, &Engine::v2_service_get_protocol},
	{ServiceMethod::GetPreferredOutputKind, &Engine::v2_service_get_preferred_output_kind},
	{ServiceMethod::GetSupportedResolutions, &Engine::v2_service_get_supported_resolutions},
	{ServiceMethod::GetMaxFps, &Engine::v2_service_get_max_fps},
	{ServiceMethod::GetMaxBitrates, &Engine::v2_service_get_max_bitrates},
	{ServiceMethod::GetSupportedVideoCodecs, &Engine::v2_service_get_supported_video_codecs},
	{ServiceMethod::GetSupportedAudioCodecs, &Engine::v2_service_get_supported_audio_codecs},
	{ServiceMethod::GetEncoderRecommendations, &Engine::v2_service_get_encoder_recommendations},
	{ServiceMethod::CanConnect, &Engine::v2_service_can_connect},
};

using OutputMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct OutputHandlerEntry {
	OutputMethod method;
	OutputMethodHandler handler;
};

constexpr OutputHandlerEntry kOutputHandlers[] = {
	{OutputMethod::KindList, &Engine::v2_output_kind_list},
	{OutputMethod::KindGet, &Engine::v2_output_kind_get},
	{OutputMethod::KindDefaults, &Engine::v2_output_kind_defaults},
	{OutputMethod::KindProperties, &Engine::v2_output_kind_properties},
	{OutputMethod::KindCapabilities, &Engine::v2_output_kind_capabilities},
	{OutputMethod::List, &Engine::v2_output_list},
	{OutputMethod::Get, &Engine::v2_output_get},
	{OutputMethod::Create, &Engine::v2_output_create},
	{OutputMethod::Remove, &Engine::v2_output_remove},
	{OutputMethod::Rename, &Engine::v2_output_rename},
	{OutputMethod::GetSettings, &Engine::v2_output_get_settings},
	{OutputMethod::PatchSettings, &Engine::v2_output_patch_settings},
	{OutputMethod::ReplaceSettings, &Engine::v2_output_replace_settings},
	{OutputMethod::GetProperties, &Engine::v2_output_get_properties},
	{OutputMethod::SetService, &Engine::v2_output_set_service},
	{OutputMethod::GetService, &Engine::v2_output_get_service},
	{OutputMethod::SetVideoEncoder, &Engine::v2_output_set_video_encoder},
	{OutputMethod::SetAudioEncoder, &Engine::v2_output_set_audio_encoder},
	{OutputMethod::GetEncoders, &Engine::v2_output_get_encoders},
	{OutputMethod::Start, &Engine::v2_output_start},
	{OutputMethod::Stop, &Engine::v2_output_stop},
	{OutputMethod::ForceStop, &Engine::v2_output_force_stop},
	{OutputMethod::GetState, &Engine::v2_output_get_state},
	{OutputMethod::SetPaused, &Engine::v2_output_set_paused},
	{OutputMethod::GetPaused, &Engine::v2_output_get_paused},
	{OutputMethod::SetDelay, &Engine::v2_output_set_delay},
	{OutputMethod::GetDelay, &Engine::v2_output_get_delay},
	{OutputMethod::SetReconnect, &Engine::v2_output_set_reconnect},
	{OutputMethod::GetReconnect, &Engine::v2_output_get_reconnect},
	{OutputMethod::GetStats, &Engine::v2_output_get_stats},
	{OutputMethod::GetLastError, &Engine::v2_output_get_last_error},
	{OutputMethod::GetSupportedCodecs, &Engine::v2_output_get_supported_codecs},
};

using RecordingMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct RecordingHandlerEntry {
	RecordingMethod method;
	RecordingMethodHandler handler;
};

constexpr RecordingHandlerEntry kRecordingHandlers[] = {
	{RecordingMethod::GetConfig, &Engine::v2_recording_get_config},
	{RecordingMethod::Configure, &Engine::v2_recording_configure},
	{RecordingMethod::Unconfigure, &Engine::v2_recording_unconfigure},
	{RecordingMethod::Start, &Engine::v2_recording_start},
	{RecordingMethod::Stop, &Engine::v2_recording_stop},
	{RecordingMethod::ForceStop, &Engine::v2_recording_force_stop},
	{RecordingMethod::Pause, &Engine::v2_recording_pause},
	{RecordingMethod::Resume, &Engine::v2_recording_resume},
	{RecordingMethod::TogglePause, &Engine::v2_recording_toggle_pause},
	{RecordingMethod::SplitFile, &Engine::v2_recording_split_file},
	{RecordingMethod::AddChapter, &Engine::v2_recording_add_chapter},
	{RecordingMethod::GetState, &Engine::v2_recording_get_state},
	{RecordingMethod::GetStats, &Engine::v2_recording_get_stats},
	{RecordingMethod::GetCurrentPath, &Engine::v2_recording_get_current_path},
	{RecordingMethod::GetLastFile, &Engine::v2_recording_get_last_file},
};

using StreamingMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct StreamingHandlerEntry {
	StreamingMethod method;
	StreamingMethodHandler handler;
};

constexpr StreamingHandlerEntry kStreamingHandlers[] = {
	{StreamingMethod::GetConfig, &Engine::v2_streaming_get_config},
	{StreamingMethod::Configure, &Engine::v2_streaming_configure},
	{StreamingMethod::Unconfigure, &Engine::v2_streaming_unconfigure},
	{StreamingMethod::Start, &Engine::v2_streaming_start},
	{StreamingMethod::Stop, &Engine::v2_streaming_stop},
	{StreamingMethod::ForceStop, &Engine::v2_streaming_force_stop},
	{StreamingMethod::GetState, &Engine::v2_streaming_get_state},
	{StreamingMethod::GetStats, &Engine::v2_streaming_get_stats},
	{StreamingMethod::GetService, &Engine::v2_streaming_get_service},
	{StreamingMethod::SetService, &Engine::v2_streaming_set_service},
	{StreamingMethod::GetReconnectState, &Engine::v2_streaming_get_reconnect_state},
	{StreamingMethod::GetLastError, &Engine::v2_streaming_get_last_error},
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

EncoderGroupMethod classify_encoder_group(std::string_view name)
{
	for (const EncoderGroupMethodName &entry : kEncoderGroupMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return EncoderGroupMethod::Unknown;
}

ServiceMethod classify_service(std::string_view name)
{
	for (const ServiceMethodName &entry : kServiceMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return ServiceMethod::Unknown;
}

OutputMethod classify_output(std::string_view name)
{
	for (const OutputMethodName &entry : kOutputMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return OutputMethod::Unknown;
}

RecordingMethod classify_recording(std::string_view name)
{
	for (const RecordingMethodName &entry : kRecordingMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return RecordingMethod::Unknown;
}

StreamingMethod classify_streaming(std::string_view name)
{
	for (const StreamingMethodName &entry : kStreamingMethods) {
		if (entry.name == name)
			return entry.method;
	}
	return StreamingMethod::Unknown;
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

bool is_mutating(EncoderGroupMethod method)
{
	for (const EncoderGroupMethod candidate : kEncoderGroupMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool is_mutating(ServiceMethod method)
{
	for (const ServiceMethod candidate : kServiceMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool is_mutating(OutputMethod method)
{
	for (const OutputMethod candidate : kOutputMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool is_mutating(RecordingMethod method)
{
	for (const RecordingMethod candidate : kRecordingMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool is_mutating(StreamingMethod method)
{
	for (const StreamingMethod candidate : kStreamingMutatingMethods) {
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

EncoderGroupMethodHandler encoder_group_handler_for(EncoderGroupMethod method)
{
	for (const EncoderGroupHandlerEntry &entry : kEncoderGroupHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

ServiceMethodHandler service_handler_for(ServiceMethod method)
{
	for (const ServiceHandlerEntry &entry : kServiceHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

OutputMethodHandler output_handler_for(OutputMethod method)
{
	for (const OutputHandlerEntry &entry : kOutputHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

RecordingMethodHandler recording_handler_for(RecordingMethod method)
{
	for (const RecordingHandlerEntry &entry : kRecordingHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

StreamingMethodHandler streaming_handler_for(StreamingMethod method)
{
	for (const StreamingHandlerEntry &entry : kStreamingHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

bool phase3_dispatch_empty(const Phase3Dispatch &dispatch)
{
	return dispatch.audio == AudioMethod::Unknown && dispatch.hotkey == HotkeyMethod::Unknown &&
	       dispatch.encoder == EncoderMethod::Unknown && dispatch.encoder_group == EncoderGroupMethod::Unknown &&
	       dispatch.service == ServiceMethod::Unknown && dispatch.output == OutputMethod::Unknown &&
	       dispatch.recording == RecordingMethod::Unknown && dispatch.streaming == StreamingMethod::Unknown;
}

bool phase3_dispatch_is_mutating(const Phase3Dispatch &dispatch)
{
	if (dispatch.is_output)
		return is_mutating(dispatch.output);
	if (dispatch.is_recording)
		return is_mutating(dispatch.recording);
	if (dispatch.is_streaming)
		return is_mutating(dispatch.streaming);
	if (dispatch.is_service)
		return is_mutating(dispatch.service);
	if (dispatch.is_encoder_group)
		return is_mutating(dispatch.encoder_group);
	if (dispatch.is_encoder)
		return is_mutating(dispatch.encoder);
	if (dispatch.is_hotkey)
		return is_mutating(dispatch.hotkey);
	return is_mutating(dispatch.audio);
}

bool classify_phase3(std::string_view name, Phase3Dispatch &dispatch)
{
	dispatch.audio = classify(name);
	dispatch.hotkey = classify_hotkey(name);
	dispatch.encoder = classify_encoder(name);
	dispatch.encoder_group = classify_encoder_group(name);
	dispatch.service = classify_service(name);
	dispatch.output = classify_output(name);
	dispatch.recording = classify_recording(name);
	dispatch.streaming = classify_streaming(name);
	if (phase3_dispatch_empty(dispatch))
		return false;
	dispatch.is_hotkey = dispatch.hotkey != HotkeyMethod::Unknown;
	dispatch.is_encoder = dispatch.encoder != EncoderMethod::Unknown;
	dispatch.is_encoder_group = dispatch.encoder_group != EncoderGroupMethod::Unknown;
	dispatch.is_service = dispatch.service != ServiceMethod::Unknown;
	dispatch.is_output = dispatch.output != OutputMethod::Unknown;
	dispatch.is_recording = dispatch.recording != RecordingMethod::Unknown;
	dispatch.is_streaming = dispatch.streaming != StreamingMethod::Unknown;
	dispatch.mutating = phase3_dispatch_is_mutating(dispatch);
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

bool execute_encoder_group(Engine &engine, EncoderGroupMethod method, const V2Request &request,
				   RuntimeV2Result &result, RuntimeV2Error &error)
{
	const EncoderGroupMethodHandler handler = encoder_group_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "encoder group method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_service(Engine &engine, ServiceMethod method, const V2Request &request, RuntimeV2Result &result,
			     RuntimeV2Error &error)
{
	const ServiceMethodHandler handler = service_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "service method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_output(Engine &engine, OutputMethod method, const V2Request &request, RuntimeV2Result &result,
			    RuntimeV2Error &error)
{
	const OutputMethodHandler handler = output_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "output method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_recording(Engine &engine, RecordingMethod method, const V2Request &request, RuntimeV2Result &result,
			       RuntimeV2Error &error)
{
	const RecordingMethodHandler handler = recording_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "recording method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_streaming(Engine &engine, StreamingMethod method, const V2Request &request, RuntimeV2Result &result,
			       RuntimeV2Error &error)
{
	const StreamingMethodHandler handler = streaming_handler_for(method);
	if (!handler) {
		error.code = "internal_error";
		error.message = "streaming method dispatch failed";
		return false;
	}
	return (engine.*handler)(request.params.get(), result, error);
}

bool execute_phase3(Engine &engine, const Phase3Dispatch &dispatch, const V2Request &request,
			   RuntimeV2Result &result, RuntimeV2Error &error)
{
	if (dispatch.is_output)
		return execute_output(engine, dispatch.output, request, result, error);
	if (dispatch.is_recording)
		return execute_recording(engine, dispatch.recording, request, result, error);
	if (dispatch.is_streaming)
		return execute_streaming(engine, dispatch.streaming, request, result, error);
	if (dispatch.is_service)
		return execute_service(engine, dispatch.service, request, result, error);
	if (dispatch.is_encoder_group)
		return execute_encoder_group(engine, dispatch.encoder_group, request, result, error);
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
	       classify_encoder(method) != EncoderMethod::Unknown ||
	       classify_encoder_group(method) != EncoderGroupMethod::Unknown ||
	       classify_service(method) != ServiceMethod::Unknown ||
	       classify_output(method) != OutputMethod::Unknown ||
	       classify_recording(method) != RecordingMethod::Unknown ||
	       classify_streaming(method) != StreamingMethod::Unknown;
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
