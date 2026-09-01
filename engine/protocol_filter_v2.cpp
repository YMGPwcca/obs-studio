#include "protocol_v2.hpp"

#include "events.hpp"
#include "protocol_phase2_v2.hpp"
#include "revision.hpp"
#include "runtime.hpp"

#include <windows.h>

#include <obs.h>

#include <cstdio>
#include <optional>
#include <string_view>

namespace obs_engine {

// protocol_v2.cpp is compiled with a source-local preprocessor rename so its
// accepted Task-10 implementation remains byte-for-byte unchanged while this
// file adds the Task-11 routing layer.
bool handle_v2_request_core(Engine &engine, const Config &config, RevisionState &revisions, EventDispatcher &events,
			    const V2Request &request);

namespace {

struct CapabilityDescriptor {
	const char *name;
	bool experimental;
};

// Keep this list equal to the accepted Protocol-v2 capability set plus the
// Task-11 filter namespace. Controllers feature-detect these names rather than
// infer behavior from the libobs version.
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
	{"item.createGroup.v1", false},
	{"item.addToGroup.v1", false},
	{"item.removeFromGroup.v1", false},
	{"item.get.v1", false},
	{"item.getChildren.v1", false},
	{"item.getTransform.v1", false},
	{"item.remove.v1", false},
	{"item.duplicate.v1", false},
	{"item.setTransform.v1", false},
	{"item.setPosition.v1", false},
	{"item.setScale.v1", false},
	{"item.setRotation.v1", false},
	{"item.setAlignment.v1", false},
	{"item.setBounds.v1", false},
	{"item.setBoundsAlignment.v1", false},
	{"item.setCrop.v1", false},
	{"item.setCropToBounds.v1", false},
	{"item.setVisible.v1", false},
	{"item.setLocked.v1", false},
	{"item.setOrder.v1", false},
	{"item.moveUp.v1", false},
	{"item.moveDown.v1", false},
	{"item.moveTop.v1", false},
	{"item.moveBottom.v1", false},
	{"item.setScaleFilter.v1", false},
	{"item.setBlendMode.v1", false},
	{"item.setBlendMethod.v1", false},
	{"item.ungroup.v1", false},
	{"filter.create.v1", false},
	{"filter.duplicate.v1", false},
	{"filter.get.v1", false},
	{"filter.getEnabled.v1", false},
	{"filter.getSettings.v1", false},
	{"filter.kindDefaults.v1", false},
	{"filter.kindList.v1", false},
	{"filter.kindProperties.v1", false},
	{"filter.list.v1", false},
	{"filter.moveBottom.v1", false},
	{"filter.moveDown.v1", false},
	{"filter.moveTop.v1", false},
	{"filter.moveUp.v1", false},
	{"filter.patchSettings.v1", false},
	{"filter.remove.v1", false},
	{"filter.rename.v1", false},
	{"filter.replaceSettings.v1", false},
	{"filter.setEnabled.v1", false},
	{"filter.setOrder.v1", false},
	{"filter.v1", false},
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
	{"properties.v1", false},
	{"properties.get.v1", false},
	{"properties.getListItems.v1", false},
	{"properties.invokeButton.v1", false},
	{"properties.refresh.v1", false},
	{"properties.resolve.v1", false},
	{"properties.validate.v1", false},
	{"scene.create.v1", false},
	{"scene.duplicate.v1", false},
	{"scene.get.v1", false},
	{"scene.getItems.v1", false},
	{"scene.getState.v1", false},
	{"scene.list.v1", false},
	{"scene.remove.v1", false},
	{"scene.rename.v1", false},
	{"canvas.v1.experimental", true},
	{"canvas.create.v1", true},
	{"canvas.get.v1", true},
	{"canvas.getMain.v1", true},
	{"canvas.getFlags.v1", true},
	{"canvas.getVideoSettings.v1", true},
	{"canvas.setVideoSettings.v1", true},
	{"canvas.list.v1", true},
	{"canvas.listScenes.v1", true},
	{"canvas.remove.v1", true},
	{"canvas.rename.v1", true},
	{"canvas.getChannel.v1", true},
	{"canvas.setChannel.v1", true},
	{"program.getScene.v1", false},
	{"program.setScene.v1", false},
	{"program.v1", false},
	{"preview.getScene.v1", false},
	{"preview.setScene.v1", false},
	{"preview.getInfo.v1", false},
	{"preview.v1", false},
	{"preview.d3d11SharedTexture.v1", true},
	{"previewOutput.create.v1", true},
	{"previewOutput.destroy.v1", true},
	{"previewOutput.getInfo.v1", true},
	{"previewOutput.setEnabled.v1", true},
	{"previewOutput.resize.v1", true},
	{"previewOutput.getSharedTexture.v1", true},
	{"previewOutput.releaseSharedTexture.v1", true},
	{"transition.kindList.v1", false},
	{"transition.kindDefaults.v1", false},
	{"transition.kindProperties.v1", false},
	{"transition.list.v1", false},
	{"transition.get.v1", false},
	{"transition.create.v1", false},
	{"transition.remove.v1", false},
	{"transition.rename.v1", false},
	{"transition.getSettings.v1", false},
	{"transition.patchSettings.v1", false},
	{"transition.replaceSettings.v1", false},
	{"transition.getProperties.v1", false},
	{"transition.getDuration.v1", false},
	{"transition.setDuration.v1", false},
	{"transition.getState.v1", false},
	{"transition.v1", false},
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

enum class FilterMethod {
	KindList,
	KindDefaults,
	KindProperties,
	List,
	Get,
	Create,
	Remove,
	Rename,
	Duplicate,
	GetSettings,
	PatchSettings,
	ReplaceSettings,
	SetEnabled,
	GetEnabled,
	SetOrder,
	MoveUp,
	MoveDown,
	MoveTop,
	MoveBottom,
	Unknown,
};

struct FilterMethodName {
	std::string_view name;
	FilterMethod method;
};

constexpr FilterMethodName kFilterMethodNames[] = {
	{"filter.kindList", FilterMethod::KindList},
	{"filter.kindDefaults", FilterMethod::KindDefaults},
	{"filter.kindProperties", FilterMethod::KindProperties},
	{"filter.list", FilterMethod::List},
	{"filter.get", FilterMethod::Get},
	{"filter.create", FilterMethod::Create},
	{"filter.remove", FilterMethod::Remove},
	{"filter.rename", FilterMethod::Rename},
	{"filter.duplicate", FilterMethod::Duplicate},
	{"filter.getSettings", FilterMethod::GetSettings},
	{"filter.patchSettings", FilterMethod::PatchSettings},
	{"filter.replaceSettings", FilterMethod::ReplaceSettings},
	{"filter.setEnabled", FilterMethod::SetEnabled},
	{"filter.getEnabled", FilterMethod::GetEnabled},
	{"filter.setOrder", FilterMethod::SetOrder},
	{"filter.moveUp", FilterMethod::MoveUp},
	{"filter.moveDown", FilterMethod::MoveDown},
	{"filter.moveTop", FilterMethod::MoveTop},
	{"filter.moveBottom", FilterMethod::MoveBottom},
};

constexpr FilterMethod kMutatingFilterMethods[] = {
	FilterMethod::Create,
	FilterMethod::Remove,
	FilterMethod::Rename,
	FilterMethod::Duplicate,
	FilterMethod::PatchSettings,
	FilterMethod::ReplaceSettings,
	FilterMethod::SetEnabled,
	FilterMethod::SetOrder,
	FilterMethod::MoveUp,
	FilterMethod::MoveDown,
	FilterMethod::MoveTop,
	FilterMethod::MoveBottom,
};

using FilterMethodHandler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct FilterMethodDescriptor {
	FilterMethod method;
	FilterMethodHandler handler;
};

constexpr FilterMethodDescriptor kFilterMethods[] = {
	{FilterMethod::KindList, &Engine::v2_filter_kind_list},
	{FilterMethod::KindDefaults, &Engine::v2_filter_kind_defaults},
	{FilterMethod::KindProperties, &Engine::v2_filter_kind_properties},
	{FilterMethod::List, &Engine::v2_filter_list},
	{FilterMethod::Get, &Engine::v2_filter_get},
	{FilterMethod::Create, &Engine::v2_filter_create},
	{FilterMethod::Remove, &Engine::v2_filter_remove},
	{FilterMethod::Rename, &Engine::v2_filter_rename},
	{FilterMethod::Duplicate, &Engine::v2_filter_duplicate},
	{FilterMethod::GetSettings, &Engine::v2_filter_get_settings},
	{FilterMethod::PatchSettings, &Engine::v2_filter_patch_settings},
	{FilterMethod::ReplaceSettings, &Engine::v2_filter_replace_settings},
	{FilterMethod::SetEnabled, &Engine::v2_filter_set_enabled},
	{FilterMethod::GetEnabled, &Engine::v2_filter_get_enabled},
	{FilterMethod::SetOrder, &Engine::v2_filter_set_order},
	{FilterMethod::MoveUp, &Engine::v2_filter_move_up},
	{FilterMethod::MoveDown, &Engine::v2_filter_move_down},
	{FilterMethod::MoveTop, &Engine::v2_filter_move_top},
	{FilterMethod::MoveBottom, &Engine::v2_filter_move_bottom},
};

FilterMethod classify_filter_method(std::string_view method)
{
	for (const FilterMethodName &entry : kFilterMethodNames) {
		if (entry.name == method)
			return entry.method;
	}
	return FilterMethod::Unknown;
}

bool is_mutating(FilterMethod method)
{
	for (const FilterMethod candidate : kMutatingFilterMethods) {
		if (candidate == method)
			return true;
	}
	return false;
}

bool needs_settings_settle(FilterMethod method)
{
	return method == FilterMethod::PatchSettings || method == FilterMethod::ReplaceSettings;
}

bool execute_filter_method(Engine &engine, FilterMethod method, obs_data_t *params, RuntimeV2Result &result,
			   RuntimeV2Error &error)
{
	for (const FilterMethodDescriptor &entry : kFilterMethods) {
		if (entry.method == method)
			return (engine.*entry.handler)(params, result, error);
	}
	error.code = "internal_error";
	error.message = "filter method dispatch failed";
	return false;
}

bool requires_preview_output_transport(std::string_view name)
{
	return name == "preview.d3d11SharedTexture.v1" || name.starts_with("previewOutput.");
}

ObsArrayPtr make_capabilities_array(const Engine &engine)
{
	ObsArrayPtr capabilities(obs_data_array_create());
	for (const CapabilityDescriptor &descriptor : kCapabilities) {
		if (requires_preview_output_transport(descriptor.name) && !engine.v2_preview_output_capable())
			continue;
		ObsDataPtr capability(obs_data_create());
		obs_data_set_string(capability.get(), "name", descriptor.name);
		obs_data_set_bool(capability.get(), "experimental", descriptor.experimental);
		obs_data_array_push_back(capabilities.get(), capability.get());
	}
	return capabilities;
}

void set_capabilities(obs_data_t *data, const Engine &engine)
{
	ObsArrayPtr capabilities = make_capabilities_array(engine);
	obs_data_set_array(data, "capabilities", capabilities.get());
}

bool reject_guard_on_read(const V2Request &request, uint64_t revision)
{
	if (!request.has_if_revision)
		return false;
	send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
		      revision);
	return true;
}

bool validate_mutation_guard(const V2Request &request, RevisionState::MutationGuard &guard)
{
	if (!request.has_if_revision)
		return true;
	const uint64_t expected = static_cast<uint64_t>(request.if_revision);
	if (guard.matches(expected))
		return true;
	ObsDataPtr details(obs_data_create());
	obs_data_set_int(details.get(), "expectedRevision", request.if_revision);
	obs_data_set_int(details.get(), "actualRevision", static_cast<long long>(guard.current()));
	send_v2_error(request.id, "revision_conflict", "ifRevision does not match the current engine revision",
		      details.get(), guard.current());
	return false;
}

void publish_runtime_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events) {
		const EventPublishResult publish_result =
			events.publish(EngineEventKind::State, event.name, revision, event.data.get());
		if (publish_result == EventPublishResult::InvalidEvent) {
			std::fprintf(stderr, "obs-engine: invalid internal filter event name '%s'\n", event.name.c_str());
			std::fflush(stderr);
		}
	}
}

class FilterCaptureScope {
public:
	FilterCaptureScope(Engine &engine, RuntimeV2Result &result) : engine_(engine)
	{
		engine_.v2_begin_event_capture(result);
		// This is deliberately before the caller acquires the mutation lock.
		// It preserves the accepted Task-10 signal-mutex/revision-mutex ordering.
		engine_.v2_wait_for_event_capture_callbacks();
	}

	~FilterCaptureScope()
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

	FilterCaptureScope(const FilterCaptureScope &) = delete;
	FilterCaptureScope &operator=(const FilterCaptureScope &) = delete;

private:
	Engine &engine_;
	bool active_ = true;
};

bool prepare_filter_mutation(const V2Request &request, bool mutating, Engine &engine, RevisionState &revisions,
				     RuntimeV2Result &result,
				     std::optional<FilterCaptureScope> &capture,
				     std::optional<RevisionState::MutationGuard> &guard)
{
	if (!mutating)
		return true;
	capture.emplace(engine, result);
	guard.emplace(revisions.lock_mutation());
	engine.v2_drain_deferred_source_events(*guard);

	if (!validate_mutation_guard(request, *guard)) {
		capture->flush(*guard);
		return false;
	}
	if (guard->can_commit_mutation())
		return true;
	send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr, guard->current());
	capture->flush(*guard);
	return false;
}

bool commit_filter_result(const V2Request &request, RuntimeV2Result &result, RevisionState &revisions,
				  std::optional<RevisionState::MutationGuard> &guard, uint64_t &revision)
{
	revision = guard ? guard->current() : revisions.current();
	if (!result.mutated)
		return true;
	if (guard) {
		revision = guard->commit_mutation();
		return true;
	}
	send_v2_error(request.id, "internal_error", "read-only filter method unexpectedly mutated engine state", nullptr,
			      revisions.current());
	return false;
}

bool execute_filter_request(Engine &engine, FilterMethod method, const V2Request &request, RuntimeV2Result &result,
				    RuntimeV2Error &error, const std::optional<FilterCaptureScope> &capture)
{
	bool succeeded = execute_filter_method(engine, method, request.params.get(), result, error);
	if (succeeded && capture && needs_settings_settle(method) && result.mutated)
		succeeded = engine.v2_settle_filter_mutation(request.params.get(), result, error);
	return succeeded;
}

bool handle_capability_request(Engine &engine, RevisionState &revisions, const V2Request &request)
{
	if (reject_guard_on_read(request, revisions.current()))
		return true;

	if (request.method == "session.hello") {
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
		set_capabilities(data.get(), engine);
		obs_data_set_int(data.get(), "revision", static_cast<long long>(revision));
		send_v2_ok(request.id, data.get(), revision);
		return true;
	}

	ObsDataPtr data(obs_data_create());
	set_capabilities(data.get(), engine);
	send_v2_ok(request.id, data.get(), revisions.current());
	return true;
}

bool handle_filter_request(Engine &engine, RevisionState &revisions, EventDispatcher &events, const V2Request &request,
			   FilterMethod method)
{
	const bool mutating = is_mutating(method);
	if (!mutating && reject_guard_on_read(request, revisions.current()))
		return true;

	RuntimeV2Result result;
	RuntimeV2Error error;
	std::optional<FilterCaptureScope> capture;
	std::optional<RevisionState::MutationGuard> guard;

	if (!prepare_filter_mutation(request, mutating, engine, revisions, result, capture, guard))
		return true;

	const bool succeeded = execute_filter_request(engine, method, request, result, error, capture);

	// This cascades source -> media -> filter observer synchronization while the
	// capture gate is still active.
	engine.v2_sync_source_observers();

	if (!succeeded) {
		send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
		      guard ? guard->current() : revisions.current());
		if (capture)
			capture->flush(*guard);
		return true;
	}

	uint64_t revision = 0;
	if (!commit_filter_result(request, result, revisions, guard, revision))
		return true;

	send_v2_ok(request.id, result.data.get(), revision);
	publish_runtime_events(events, revision, result);
	if (capture)
		capture->flush(*guard);
	return true;
}

} // namespace

bool handle_v2_request(Engine &engine, const Config &config, RevisionState &revisions, EventDispatcher &events,
			       const V2Request &request)
{
	engine.v2_sync_transition_observers();
	if (request.method == "session.hello" || request.method == "engine.getCapabilities")
		return handle_capability_request(engine, revisions, request);

	const FilterMethod filter_method = classify_filter_method(request.method);
	if (filter_method != FilterMethod::Unknown)
		return handle_filter_request(engine, revisions, events, request, filter_method);

	if (is_phase2_method(request.method))
		return handle_phase2_request(engine, revisions, events, request);

	return handle_v2_request_core(engine, config, revisions, events, request);
}

} // namespace obs_engine
