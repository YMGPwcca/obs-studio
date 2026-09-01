#include "protocol_v2.hpp"

#include "events.hpp"
#include "revision.hpp"
#include "runtime.hpp"
#include "validation.hpp"

#include <windows.h>

#include <obs.h>

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obs_engine {
namespace {

struct CapabilityDescriptor {
	const char *name;
	bool experimental;
};

constexpr CapabilityDescriptor kCapabilities[] = {
	{"engine.capabilities.v1", false},
	{"event.delivery.v1", false},
	{"interaction.v1", false},
	{"interaction.focus.v1", false},
	{"interaction.key.v1", false},
	{"interaction.mouseButton.v1", false},
	{"interaction.mouseMove.v1", false},
	{"interaction.mouseWheel.v1", false},
	{"interaction.reset.v1", false},
	{"interaction.text.v1", false},
	{"item.create.v1", false},
	{"item.remove.v1", false},
	{"item.setTransform.v1", false},
	{"media.getDuration.v1", false},
	{"media.getPosition.v1", false},
	{"media.getState.v1", false},
	{"media.next.v1", false},
	{"media.pause.v1", false},
	{"media.play.v1", false},
	{"media.previous.v1", false},
	{"media.restart.v1", false},
	{"media.setPosition.v1", false},
	{"media.stop.v1", false},
	{"media.togglePause.v1", false},
	{"media.v1", false},
	{"audio.v1", false},
	{"audio.get.v1", false},
	{"audio.setMute.v1", false},
	{"audio.toggleMute.v1", false},
	{"audio.getVolume.v1", false},
	{"audio.setVolume.v1", false},
	{"audio.setVolumeDb.v1", false},
	{"audio.setBalance.v1", false},
	{"audio.setSyncOffset.v1", false},
	{"audio.getMonitoringEnabled.v1", false},
	{"audio.setMonitoringEnabled.v1", false},
	{"audio.getTracks.v1", false},
	{"audio.setTracks.v1", false},
	{"audio.getPushToTalk.v1", false},
	{"audio.setPushToTalk.v1", false},
	{"audio.getPushToMute.v1", false},
	{"audio.setPushToMute.v1", false},
	{"audio.listMonitoringDevices.v1", false},
	{"audio.getMonitoringDevice.v1", false},
	{"audio.setMonitoringDevice.v1", false},
	{"audio.subscribeMeters.v1", false},
	{"audio.unsubscribeMeters.v1", false},
	{"properties.v1", false},
	{"properties.get.v1", false},
	{"properties.getListItems.v1", false},
	{"properties.invokeButton.v1", false},
	{"properties.refresh.v1", false},
	{"properties.resolve.v1", false},
	{"properties.validate.v1", false},
	{"scene.create.v1", false},
	{"scene.remove.v1", false},
	{"session.close.v1", false},
	{"session.getSubscriptions.v1", false},
	{"session.hello.v1", false},
	{"session.ping.v1", false},
	{"session.subscribe.v1", false},
	{"session.unsubscribe.v1", false},
	{"source.v1", false},
	{"source.create.v1", false},
	{"source.duplicate.v1", false},
	{"source.get.v1", false},
	{"source.getActive.v1", false},
	{"source.getDimensions.v1", false},
	{"source.getFlags.v1", false},
	{"source.getMissingFiles.v1", false},
	{"source.getProperties.v1", false},
	{"source.getSettings.v1", false},
	{"source.getShowing.v1", false},
	{"source.getState.v1", false},
	{"source.kindDefaults.v1", false},
	{"source.kindGet.v1", false},
	{"source.kindList.v1", false},
	{"source.kindProperties.v1", false},
	{"source.list.v1", false},
	{"source.loadState.v1", false},
	{"source.patchSettings.v1", false},
	{"source.refresh.v1", false},
	{"source.remove.v1", false},
	{"source.rename.v1", false},
	{"source.replaceSettings.v1", false},
	{"source.resetSettings.v1", false},
	{"source.saveState.v1", false},
};

enum class V2Method {
	SessionHello,
	SessionPing,
	SessionSubscribe,
	SessionUnsubscribe,
	SessionGetSubscriptions,
	SessionClose,
	EngineGetCapabilities,
	PropertiesGet,
	PropertiesResolve,
	PropertiesGetListItems,
	PropertiesInvokeButton,
	PropertiesValidate,
	PropertiesRefresh,
	SourceKindList,
	SourceKindGet,
	SourceKindDefaults,
	SourceKindProperties,
	SourceList,
	SourceGet,
	SourceCreate,
	SourceDuplicate,
	SourceRemove,
	SourceRename,
	SourceGetSettings,
	SourcePatchSettings,
	SourceReplaceSettings,
	SourceResetSettings,
	SourceGetProperties,
	SourceGetFlags,
	SourceGetDimensions,
	SourceGetState,
	SourceGetActive,
	SourceGetShowing,
	SourceGetMissingFiles,
	SourceRefresh,
	SourceSaveState,
	SourceLoadState,
	InteractionFocus,
	InteractionMouseMove,
	InteractionMouseButton,
	InteractionMouseWheel,
	InteractionKey,
	InteractionText,
	InteractionReset,
	SceneCreate,
	SceneRemove,
	ItemCreate,
	ItemRemove,
	ItemSetTransform,
	MediaGetState,
	MediaPlay,
	MediaPause,
	MediaTogglePause,
	MediaStop,
	MediaRestart,
	MediaNext,
	MediaPrevious,
	MediaGetDuration,
	MediaGetPosition,
	MediaSetPosition,
	Unknown,
};

struct MethodName {
	std::string_view name;
	V2Method method;
};

constexpr MethodName kMethodNames[] = {
	{"session.hello", V2Method::SessionHello},
	{"session.ping", V2Method::SessionPing},
	{"session.subscribe", V2Method::SessionSubscribe},
	{"session.unsubscribe", V2Method::SessionUnsubscribe},
	{"session.getSubscriptions", V2Method::SessionGetSubscriptions},
	{"session.close", V2Method::SessionClose},
	{"engine.getCapabilities", V2Method::EngineGetCapabilities},
	{"properties.get", V2Method::PropertiesGet},
	{"properties.resolve", V2Method::PropertiesResolve},
	{"properties.getListItems", V2Method::PropertiesGetListItems},
	{"properties.invokeButton", V2Method::PropertiesInvokeButton},
	{"properties.validate", V2Method::PropertiesValidate},
	{"properties.refresh", V2Method::PropertiesRefresh},
	{"source.kindList", V2Method::SourceKindList},
	{"source.kindGet", V2Method::SourceKindGet},
	{"source.kindDefaults", V2Method::SourceKindDefaults},
	{"source.kindProperties", V2Method::SourceKindProperties},
	{"source.list", V2Method::SourceList},
	{"source.get", V2Method::SourceGet},
	{"source.create", V2Method::SourceCreate},
	{"source.duplicate", V2Method::SourceDuplicate},
	{"source.remove", V2Method::SourceRemove},
	{"source.rename", V2Method::SourceRename},
	{"source.getSettings", V2Method::SourceGetSettings},
	{"source.patchSettings", V2Method::SourcePatchSettings},
	{"source.replaceSettings", V2Method::SourceReplaceSettings},
	{"source.resetSettings", V2Method::SourceResetSettings},
	{"source.getProperties", V2Method::SourceGetProperties},
	{"source.getFlags", V2Method::SourceGetFlags},
	{"source.getDimensions", V2Method::SourceGetDimensions},
	{"source.getState", V2Method::SourceGetState},
	{"source.getActive", V2Method::SourceGetActive},
	{"source.getShowing", V2Method::SourceGetShowing},
	{"source.getMissingFiles", V2Method::SourceGetMissingFiles},
	{"source.refresh", V2Method::SourceRefresh},
	{"source.saveState", V2Method::SourceSaveState},
	{"source.loadState", V2Method::SourceLoadState},
	{"interaction.focus", V2Method::InteractionFocus},
	{"interaction.mouseMove", V2Method::InteractionMouseMove},
	{"interaction.mouseButton", V2Method::InteractionMouseButton},
	{"interaction.mouseWheel", V2Method::InteractionMouseWheel},
	{"interaction.key", V2Method::InteractionKey},
	{"interaction.text", V2Method::InteractionText},
	{"interaction.reset", V2Method::InteractionReset},
	{"scene.create", V2Method::SceneCreate},
	{"scene.remove", V2Method::SceneRemove},
	{"item.create", V2Method::ItemCreate},
	{"item.remove", V2Method::ItemRemove},
	{"item.setTransform", V2Method::ItemSetTransform},
	{"media.getState", V2Method::MediaGetState},
	{"media.play", V2Method::MediaPlay},
	{"media.pause", V2Method::MediaPause},
	{"media.togglePause", V2Method::MediaTogglePause},
	{"media.stop", V2Method::MediaStop},
	{"media.restart", V2Method::MediaRestart},
	{"media.next", V2Method::MediaNext},
	{"media.previous", V2Method::MediaPrevious},
	{"media.getDuration", V2Method::MediaGetDuration},
	{"media.getPosition", V2Method::MediaGetPosition},
	{"media.setPosition", V2Method::MediaSetPosition},
};

constexpr V2Method kMutatingMethods[] = {
	V2Method::SessionClose,
	V2Method::PropertiesInvokeButton,
	V2Method::SourceCreate,
	V2Method::SourceDuplicate,
	V2Method::SourceRemove,
	V2Method::SourceRename,
	V2Method::SourcePatchSettings,
	V2Method::SourceReplaceSettings,
	V2Method::SourceResetSettings,
	V2Method::SourceLoadState,
	V2Method::SceneCreate,
	V2Method::SceneRemove,
	V2Method::ItemCreate,
	V2Method::ItemRemove,
	V2Method::ItemSetTransform,
	V2Method::MediaPlay,
	V2Method::MediaPause,
	V2Method::MediaTogglePause,
	V2Method::MediaStop,
	V2Method::MediaRestart,
	V2Method::MediaNext,
	V2Method::MediaPrevious,
	V2Method::MediaSetPosition,
};

constexpr V2Method kSourceSettlementMethods[] = {
	V2Method::SourcePatchSettings,
	V2Method::SourceReplaceSettings,
	V2Method::SourceResetSettings,
	V2Method::SourceLoadState,
};

using RuntimeMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct RuntimeMethodDescriptor {
	V2Method method;
	RuntimeMethodHandler handler;
};

constexpr RuntimeMethodDescriptor kRuntimeMethods[] = {
	{V2Method::PropertiesGet, &Engine::v2_properties_get},
	{V2Method::PropertiesResolve, &Engine::v2_properties_resolve},
	{V2Method::PropertiesGetListItems, &Engine::v2_properties_get_list_items},
	{V2Method::PropertiesInvokeButton, &Engine::v2_properties_invoke_button},
	{V2Method::PropertiesValidate, &Engine::v2_properties_validate},
	{V2Method::PropertiesRefresh, &Engine::v2_properties_refresh},
	{V2Method::SourceKindList, &Engine::v2_source_kind_list},
	{V2Method::SourceKindGet, &Engine::v2_source_kind_get},
	{V2Method::SourceKindDefaults, &Engine::v2_source_kind_defaults},
	{V2Method::SourceKindProperties, &Engine::v2_source_kind_properties},
	{V2Method::SourceList, &Engine::v2_source_list},
	{V2Method::SourceGet, &Engine::v2_source_get},
	{V2Method::SourceCreate, &Engine::v2_source_create},
	{V2Method::SourceDuplicate, &Engine::v2_source_duplicate},
	{V2Method::SourceRemove, &Engine::v2_source_remove},
	{V2Method::SourceRename, &Engine::v2_source_rename},
	{V2Method::SourceGetSettings, &Engine::v2_source_get_settings},
	{V2Method::SourcePatchSettings, &Engine::v2_source_patch_settings},
	{V2Method::SourceReplaceSettings, &Engine::v2_source_replace_settings},
	{V2Method::SourceResetSettings, &Engine::v2_source_reset_settings},
	{V2Method::SourceGetProperties, &Engine::v2_source_get_properties},
	{V2Method::SourceGetFlags, &Engine::v2_source_get_flags},
	{V2Method::SourceGetDimensions, &Engine::v2_source_get_dimensions},
	{V2Method::SourceGetState, &Engine::v2_source_get_state},
	{V2Method::SourceGetActive, &Engine::v2_source_get_active},
	{V2Method::SourceGetShowing, &Engine::v2_source_get_showing},
	{V2Method::SourceGetMissingFiles, &Engine::v2_source_get_missing_files},
	{V2Method::SourceRefresh, &Engine::v2_source_refresh},
	{V2Method::SourceSaveState, &Engine::v2_source_save_state},
	{V2Method::SourceLoadState, &Engine::v2_source_load_state},
	{V2Method::InteractionFocus, &Engine::v2_interaction_focus},
	{V2Method::InteractionMouseMove, &Engine::v2_interaction_mouse_move},
	{V2Method::InteractionMouseButton, &Engine::v2_interaction_mouse_button},
	{V2Method::InteractionMouseWheel, &Engine::v2_interaction_mouse_wheel},
	{V2Method::InteractionKey, &Engine::v2_interaction_key},
	{V2Method::InteractionText, &Engine::v2_interaction_text},
	{V2Method::InteractionReset, &Engine::v2_interaction_reset},
	{V2Method::SceneCreate, &Engine::v2_scene_create},
	{V2Method::SceneRemove, &Engine::v2_scene_remove},
	{V2Method::ItemCreate, &Engine::v2_item_create},
	{V2Method::ItemRemove, &Engine::v2_item_remove},
	{V2Method::ItemSetTransform, &Engine::v2_item_set_transform},
	{V2Method::MediaGetState, &Engine::v2_media_get_state},
	{V2Method::MediaPlay, &Engine::v2_media_play},
	{V2Method::MediaPause, &Engine::v2_media_pause},
	{V2Method::MediaTogglePause, &Engine::v2_media_toggle_pause},
	{V2Method::MediaStop, &Engine::v2_media_stop},
	{V2Method::MediaRestart, &Engine::v2_media_restart},
	{V2Method::MediaNext, &Engine::v2_media_next},
	{V2Method::MediaPrevious, &Engine::v2_media_previous},
	{V2Method::MediaGetDuration, &Engine::v2_media_get_duration},
	{V2Method::MediaGetPosition, &Engine::v2_media_get_position},
	{V2Method::MediaSetPosition, &Engine::v2_media_set_position},
};

V2Method classify_method(std::string_view method)
{
	for (const MethodName &entry : kMethodNames) {
		if (entry.name == method)
			return entry.method;
	}
	return V2Method::Unknown;
}

bool method_is_mutating(V2Method method)
{
	for (const V2Method candidate : kMutatingMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool method_is_runtime(V2Method method)
{
	for (const RuntimeMethodDescriptor &entry : kRuntimeMethods) {
		if (entry.method == method)
			return true;
	}
	return false;
}

bool method_needs_source_settle(V2Method method)
{
	for (const V2Method candidate : kSourceSettlementMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool execute_runtime_method(Engine &engine, V2Method method, obs_data_t *params, RuntimeV2Result &result,
			    RuntimeV2Error &error)
{
	for (const RuntimeMethodDescriptor &entry : kRuntimeMethods) {
		if (entry.method == method)
			return (engine.*entry.handler)(params, result, error);
	}
	error.code = "internal_error";
	error.message = "runtime method dispatch failed";
	return false;
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

void set_parse_error(V2ParseError &error, const std::string &id, const char *code, const char *message)
{
	error.id = id;
	error.code = code;
	error.message = message;
}

ObsArrayPtr make_capabilities_array()
{
	ObsArrayPtr capabilities(obs_data_array_create());
	for (const CapabilityDescriptor &descriptor : kCapabilities) {
		ObsDataPtr capability(obs_data_create());
		obs_data_set_string(capability.get(), "name", descriptor.name);
		obs_data_set_bool(capability.get(), "experimental", descriptor.experimental);
		obs_data_array_push_back(capabilities.get(), capability.get());
	}
	return capabilities;
}

void set_capabilities(obs_data_t *data)
{
	ObsArrayPtr capabilities = make_capabilities_array();
	obs_data_set_array(data, "capabilities", capabilities.get());
}

void set_subscriptions(obs_data_t *data, const std::vector<EventSubscription> &subscriptions)
{
	ObsArrayPtr array(obs_data_array_create());
	for (const EventSubscription &subscription : subscriptions) {
		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "pattern", subscription.pattern.c_str());
		obs_data_set_bool(entry.get(), "telemetry", subscription.telemetry);
		obs_data_array_push_back(array.get(), entry.get());
	}
	obs_data_set_array(data, "subscriptions", array.get());
}

bool read_subscription_entry(obs_data_t *entry, EventSubscription &subscription, std::string &error)
{
	bool present = false;
	if (!read_string_field(entry, "pattern", subscription.pattern, present) || !present ||
	    !is_valid_event_pattern(subscription.pattern)) {
		error = "subscription pattern must be an exact event name or a namespace wildcard such as 'source.*'";
		return false;
	}
	bool telemetry = false;
	if (!read_bool_field(entry, "telemetry", telemetry, present)) {
		error = "subscription telemetry must be a boolean when present";
		return false;
	}
	subscription.telemetry = present && telemetry;
	return true;
}

bool parse_subscription_list(obs_data_t *params, std::vector<EventSubscription> &subscriptions, std::string &error)
{
	subscriptions.clear();
	error.clear();
	ObsArrayPtr array;
	bool present = false;
	if (!read_array_field(params, "subscriptions", array, present) || !present) {
		error = "params.subscriptions must be an array";
		return false;
	}
	const size_t count = obs_data_array_count(array.get());
	if (count == 0 || count > kMaxEventSubscriptions) {
		error = "params.subscriptions must contain between 1 and 256 entries";
		return false;
	}
	subscriptions.reserve(count);
	for (size_t index = 0; index < count; ++index) {
		ObsDataPtr entry(obs_data_array_item(array.get(), index));
		if (!entry) {
			error = "each subscription must be an object";
			return false;
		}
		EventSubscription subscription;
		if (!read_subscription_entry(entry.get(), subscription, error))
			return false;
		subscriptions.push_back(std::move(subscription));
	}
	return true;
}

void send_subscription_state(const V2Request &request, RevisionState &revisions, EventDispatcher &events)
{
	ObsDataPtr data(obs_data_create());
	set_subscriptions(data.get(), events.subscriptions());
	send_v2_ok(request.id, data.get(), revisions.current());
}

bool validate_revision_guard(const V2Request &request, V2Method method, uint64_t current_revision)
{
	if (!request.has_if_revision)
		return true;
	if (!method_is_mutating(method)) {
		send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
			      current_revision);
		return false;
	}
	const uint64_t expected_revision = static_cast<uint64_t>(request.if_revision);
	if (expected_revision == current_revision)
		return true;
	ObsDataPtr details(obs_data_create());
	obs_data_set_int(details.get(), "expectedRevision", request.if_revision);
	obs_data_set_int(details.get(), "actualRevision", static_cast<long long>(current_revision));
	send_v2_error(request.id, "revision_conflict", "ifRevision does not match the current engine revision",
		      details.get(), current_revision);
	return false;
}

void publish_runtime_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events) {
		const EventPublishResult publish_result =
			events.publish(EngineEventKind::State, event.name, revision, event.data.get());
		if (publish_result == EventPublishResult::InvalidEvent) {
			std::fprintf(stderr, "obs-engine: invalid internal event name '%s'\n", event.name.c_str());
			std::fflush(stderr);
		}
	}
}

class RuntimeEventCaptureScope {
public:
	RuntimeEventCaptureScope(Engine &engine, RuntimeV2Result &result) : engine_(engine)
	{
		engine_.v2_begin_event_capture(result);
	}

	~RuntimeEventCaptureScope()
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

	RuntimeEventCaptureScope(const RuntimeEventCaptureScope &) = delete;
	RuntimeEventCaptureScope &operator=(const RuntimeEventCaptureScope &) = delete;
	RuntimeEventCaptureScope(RuntimeEventCaptureScope &&) = delete;
	RuntimeEventCaptureScope &operator=(RuntimeEventCaptureScope &&) = delete;

private:
	Engine &engine_;
	bool active_ = true;
};

void send_session_hello(const V2Request &request, RevisionState &revisions)
{
	const uint64_t revision = revisions.current();
	ObsDataPtr data(obs_data_create());
	ObsDataPtr protocol(obs_data_create());
	obs_data_set_int(protocol.get(), "major", kProtocolV2Major);
	obs_data_set_int(protocol.get(), "minor", kProtocolV2Minor);
	obs_data_set_obj(data.get(), "protocol", protocol.get());
	obs_data_set_string(data.get(), "engineVersion", obs_get_version_string());
	obs_data_set_string(data.get(), "libobsVersion", obs_get_version_string());
	obs_data_set_string(data.get(), "platform", "windows");
	obs_data_set_int(data.get(), "pid", static_cast<long long>(GetCurrentProcessId()));
	obs_data_set_string(data.get(), "encoding", "utf-8");
	obs_data_set_int(data.get(), "maxMessageBytes", static_cast<long long>(kMaxMessageBytes));
	set_capabilities(data.get());
	obs_data_set_int(data.get(), "revision", static_cast<long long>(revision));
	send_v2_ok(request.id, data.get(), revision);
}

void send_session_ping(const V2Request &request, RevisionState &revisions)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "pong", true);
	send_v2_ok(request.id, data.get(), revisions.current());
}

bool handle_session_subscribe(const V2Request &request, RevisionState &revisions, EventDispatcher &events)
{
	std::vector<EventSubscription> requested;
	std::string error;
	if (!parse_subscription_list(request.params.get(), requested, error) || !events.subscribe(requested, error)) {
		send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
		return true;
	}
	send_subscription_state(request, revisions, events);
	return true;
}

bool handle_session_unsubscribe(const V2Request &request, RevisionState &revisions, EventDispatcher &events)
{
	std::vector<EventSubscription> requested;
	std::string error;
	if (!parse_subscription_list(request.params.get(), requested, error)) {
		send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
		return true;
	}
	std::vector<std::string> patterns;
	patterns.reserve(requested.size());
	for (const EventSubscription &subscription : requested)
		patterns.push_back(subscription.pattern);
	if (!events.unsubscribe(patterns, error)) {
		send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
		return true;
	}
	send_subscription_state(request, revisions, events);
	return true;
}

bool handle_session_close(const V2Request &request, RevisionState::MutationGuard &mutation_guard,
				  EventDispatcher &events)
{
	const uint64_t revision = mutation_guard.commit_mutation();
	send_v2_ok(request.id, nullptr, revision);
	ObsDataPtr event_data(obs_data_create());
	obs_data_set_string(event_data.get(), "reason", "session.close");
	events.publish(EngineEventKind::State, "engine.stopping", revision, event_data.get());
	return false;
}

std::optional<bool> handle_session_method(const V2Request &request, RevisionState &revisions,
					  EventDispatcher &events,
					  std::optional<RevisionState::MutationGuard> &mutation_guard)
{
	switch (classify_method(request.method)) {
	case V2Method::SessionHello:
		send_session_hello(request, revisions);
		return true;
	case V2Method::SessionPing:
		send_session_ping(request, revisions);
		return true;
	case V2Method::SessionSubscribe:
		return handle_session_subscribe(request, revisions, events);
	case V2Method::SessionUnsubscribe:
		return handle_session_unsubscribe(request, revisions, events);
	case V2Method::SessionGetSubscriptions:
		send_subscription_state(request, revisions, events);
		return true;
	case V2Method::SessionClose:
		return handle_session_close(request, *mutation_guard, events);
	default:
		return std::nullopt;
	}
}

bool handle_engine_capabilities(const V2Request &request, RevisionState &revisions)
{
	ObsDataPtr data(obs_data_create());
	set_capabilities(data.get());
	send_v2_ok(request.id, data.get(), revisions.current());
	return true;
}

void prepare_runtime_result(Engine &engine, V2Method method, const V2Request &request, RuntimeV2Result &result,
				     bool succeeded, const std::optional<RuntimeEventCaptureScope> &capture)
{
	if (!succeeded)
		return;
	if (method == V2Method::SourceKindList || method == V2Method::SourceKindGet)
		engine.v2_normalize_source_kind_metadata(result);
	if (capture && method_needs_source_settle(method))
		engine.v2_settle_source_mutation(request.params.get(), result);
}

bool commit_runtime_result(const V2Request &request, RuntimeV2Result &result, RevisionState &revisions,
				   std::optional<RevisionState::MutationGuard> &mutation_guard, uint64_t &revision)
{
	revision = mutation_guard ? mutation_guard->current() : revisions.current();
	if (!result.mutated)
		return true;
	if (mutation_guard) {
		revision = mutation_guard->commit_mutation();
		return true;
	}
	send_v2_error(request.id, "internal_error", "read-only runtime method unexpectedly mutated engine state", nullptr,
			      revisions.current());
	return false;
}

bool handle_runtime_method(Engine &engine, RevisionState &revisions, EventDispatcher &events,
				   const V2Request &request, V2Method method, RuntimeV2Result &result,
				   std::optional<RevisionState::MutationGuard> &mutation_guard,
				   std::optional<RuntimeEventCaptureScope> &capture)
{
	RuntimeV2Error error;
	const bool succeeded = execute_runtime_method(engine, method, request.params.get(), result, error);
	prepare_runtime_result(engine, method, request, result, succeeded, capture);

	// Observer connect/disconnect can take libobs signal mutexes. Keep the
	// capture gate active until synchronization is complete so cross-thread
	// callbacks defer instead of waiting on the revision mutex.
	engine.v2_sync_source_observers();
	if (!succeeded) {
		send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
			      mutation_guard ? mutation_guard->current() : revisions.current());
		if (capture)
			capture->flush(*mutation_guard);
		return true;
	}

	uint64_t revision = 0;
	if (!commit_runtime_result(request, result, revisions, mutation_guard, revision))
		return true;
	send_v2_ok(request.id, result.data.get(), revision);
	publish_runtime_events(events, revision, result);
	if (capture)
		capture->flush(*mutation_guard);
	return true;
}

bool parse_v2_request_id(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	bool present = false;
	if (read_string_field(request, "id", out.id, present) && present &&
	    is_safe_identifier(out.id.c_str(), kMaxV2RequestIdBytes))
		return true;
	set_parse_error(error, "", "bad_request",
			"id must be a 1-128 byte token using only ASCII letters, digits, '_', '-' or '.'");
	return false;
}

bool parse_v2_request_operation(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	bool present = false;
	std::string op;
	if (read_string_field(request, "op", op, present) && present && op == "request")
		return true;
	set_parse_error(error, out.id, "bad_request", "op must be the string 'request'");
	return false;
}

bool parse_v2_request_method(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	bool present = false;
	if (read_string_field(request, "method", out.method, present) && present &&
	    is_safe_identifier(out.method.c_str(), kMaxV2MethodBytes))
		return true;
	set_parse_error(error, out.id, "bad_request", "method must be a 1-128 byte protocol identifier");
	return false;
}

bool parse_v2_request_params(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	bool present = false;
	if (!read_object_field(request, "params", out.params, present)) {
		set_parse_error(error, out.id, "bad_request", "params must be an object when present");
		return false;
	}
	if (!present)
		out.params.reset(obs_data_create());
	return true;
}

bool parse_v2_request_options(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	if (read_integer(request, "ifRevision", out.if_revision, out.has_if_revision) &&
	    (!out.has_if_revision || out.if_revision >= 0)) {
		if (read_integer(request, "timeoutMs", out.timeout_ms, out.has_timeout_ms) &&
		    (!out.has_timeout_ms || out.timeout_ms > 0))
			return true;
		set_parse_error(error, out.id, "bad_request", "timeoutMs must be a positive integer");
		return false;
	}
	set_parse_error(error, out.id, "bad_request", "ifRevision must be a non-negative integer");
	return false;
}

bool prepare_v2_request(Engine &engine, RevisionState &revisions, const V2Request &request, V2Method method,
				RuntimeV2Result &runtime_result, std::optional<RevisionState::MutationGuard> &mutation_guard,
				std::optional<RuntimeEventCaptureScope> &capture)
{
	if (method_is_mutating(method)) {
		if (method_is_runtime(method)) {
			capture.emplace(engine, runtime_result);
			engine.v2_wait_for_event_capture_callbacks();
		}
		mutation_guard.emplace(revisions.lock_mutation());
		if (capture)
			engine.v2_drain_deferred_source_events(*mutation_guard);
	}

	const uint64_t guarded_revision = mutation_guard ? mutation_guard->current() : revisions.current();
	if (!validate_revision_guard(request, method, guarded_revision)) {
		if (capture)
			capture->flush(*mutation_guard);
		return false;
	}
	if (!mutation_guard || mutation_guard->can_commit_mutation())
		return true;
	send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr,
			      mutation_guard->current());
	if (capture)
		capture->flush(*mutation_guard);
	return false;
}

} // namespace

bool parse_v2_request(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	out = V2Request{};
	error = V2ParseError{};
	return parse_v2_request_id(request, out, error) && parse_v2_request_operation(request, out, error) &&
	       parse_v2_request_method(request, out, error) && parse_v2_request_params(request, out, error) &&
	       parse_v2_request_options(request, out, error);
}

void send_v2_error(const std::string &request_id, const char *code, const char *message, obs_data_t *details,
		   uint64_t revision)
{
	ObsDataPtr response(obs_data_create());
	ObsDataPtr status(obs_data_create());
	ObsDataPtr empty_details;
	obs_data_set_string(response.get(), "op", "response");
	obs_data_set_string(response.get(), "id", request_id.c_str());
	obs_data_set_bool(status.get(), "ok", false);
	obs_data_set_string(status.get(), "code", code ? code : "internal_error");
	obs_data_set_string(status.get(), "message", message ? message : "request failed");
	if (!details) {
		empty_details.reset(obs_data_create());
		details = empty_details.get();
	}
	obs_data_set_obj(status.get(), "details", details);
	obs_data_set_obj(response.get(), "status", status.get());
	obs_data_set_int(response.get(), "revision", static_cast<long long>(revision));
	write_json(response.get());
}

void send_v2_ok(const std::string &request_id, obs_data_t *data, uint64_t revision)
{
	ObsDataPtr response(obs_data_create());
	ObsDataPtr status(obs_data_create());
	ObsDataPtr empty_data;
	obs_data_set_string(response.get(), "op", "response");
	obs_data_set_string(response.get(), "id", request_id.c_str());
	obs_data_set_bool(status.get(), "ok", true);
	obs_data_set_obj(response.get(), "status", status.get());
	obs_data_set_int(response.get(), "revision", static_cast<long long>(revision));
	if (!data) {
		empty_data.reset(obs_data_create());
		data = empty_data.get();
	}
	obs_data_set_obj(response.get(), "data", data);
	write_json(response.get());
}

bool handle_v2_request(Engine &engine, const Config &, RevisionState &revisions, EventDispatcher &events,
		       const V2Request &request)
{
	const V2Method method = classify_method(request.method);
	if (method == V2Method::Unknown) {
		send_v2_error(request.id, "unsupported_method", "method is not implemented by protocol v2 yet", nullptr,
			      revisions.current());
		return true;
	}

	RuntimeV2Result runtime_result;
	std::optional<RevisionState::MutationGuard> mutation_guard;
	std::optional<RuntimeEventCaptureScope> capture;

	// Source callbacks run while libobs owns its signal mutex. The preparation
	// helper keeps capture setup and the pre-lock callback barrier together so
	// this ordering remains visible in one place.
	if (!prepare_v2_request(engine, revisions, request, method, runtime_result, mutation_guard, capture))
		return true;

	if (const std::optional<bool> session_result =
			handle_session_method(request, revisions, events, mutation_guard))
		return *session_result;
	if (method == V2Method::EngineGetCapabilities)
		return handle_engine_capabilities(request, revisions);
	if (method_is_runtime(method)) {
		return handle_runtime_method(engine, revisions, events, request, method, runtime_result, mutation_guard,
					    capture);
	}
	send_v2_error(request.id, "internal_error", "method dispatch failed internally", nullptr, revisions.current());
	return true;
}

} // namespace obs_engine
