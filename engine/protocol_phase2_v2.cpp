#include "protocol_phase2_v2.hpp"

#include "events.hpp"
#include "runtime.hpp"

#include <obs.h>

#include <algorithm>
#include <optional>
#include <string_view>

namespace obs_engine {
namespace {

enum class Phase2Method {
	SceneList,
	SceneGet,
	SceneCreate,
	SceneRemove,
	SceneRename,
	SceneDuplicate,
	SceneGetItems,
	SceneGetState,
	ItemGet,
	ItemCreate,
	ItemRemove,
	ItemDuplicate,
	ItemGetTransform,
	ItemSetTransform,
	ItemSetPosition,
	ItemSetScale,
	ItemSetRotation,
	ItemSetAlignment,
	ItemSetBounds,
	ItemSetBoundsAlignment,
	ItemSetCrop,
	ItemSetCropToBounds,
	ItemSetVisible,
	ItemSetLocked,
	ItemSetOrder,
	ItemMoveUp,
	ItemMoveDown,
	ItemMoveTop,
	ItemMoveBottom,
	ItemSetScaleFilter,
	ItemSetBlendMode,
	ItemSetBlendMethod,
	ItemCreateGroup,
	ItemUngroup,
	ItemAddToGroup,
	ItemRemoveFromGroup,
	ItemGetChildren,
	CanvasList,
	CanvasGetMain,
	CanvasGet,
	CanvasCreate,
	CanvasRemove,
	CanvasRename,
	CanvasGetVideoSettings,
	CanvasSetVideoSettings,
	CanvasListScenes,
	CanvasGetChannel,
	CanvasSetChannel,
	CanvasGetFlags,
	ProgramGetScene,
	ProgramSetScene,
	PreviewGetScene,
	PreviewSetScene,
	PreviewGetInfo,
	PreviewOutputCreate,
	PreviewOutputDestroy,
	PreviewOutputList,
	PreviewOutputGet,
	PreviewOutputSetTarget,
	PreviewOutputGetInfo,
	PreviewOutputSetEnabled,
	PreviewOutputResize,
	PreviewOutputGetSharedTexture,
	PreviewOutputReleaseSharedTexture,
	TransitionKindList,
	TransitionKindDefaults,
	TransitionKindProperties,
	TransitionList,
	TransitionGet,
	TransitionCreate,
	TransitionRemove,
	TransitionRename,
	TransitionGetSettings,
	TransitionPatchSettings,
	TransitionReplaceSettings,
	TransitionGetProperties,
	TransitionGetDuration,
	TransitionSetDuration,
	TransitionGetState,
	StudioGetEnabled,
	StudioSetEnabled,
	StudioGetTransition,
	StudioSetTransition,
	StudioGetTransitionDuration,
	StudioSetTransitionDuration,
	StudioTransition,
	Unknown,
};

using Handler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

struct Phase2MethodName {
	std::string_view name;
	Phase2Method method;
};

constexpr Phase2MethodName kMethods[] = {
	{"scene.list", Phase2Method::SceneList},       {"scene.get", Phase2Method::SceneGet},
	{"scene.create", Phase2Method::SceneCreate},   {"scene.remove", Phase2Method::SceneRemove},
	{"scene.rename", Phase2Method::SceneRename},   {"scene.duplicate", Phase2Method::SceneDuplicate},
	{"scene.getItems", Phase2Method::SceneGetItems}, {"scene.getState", Phase2Method::SceneGetState},
	{"item.get", Phase2Method::ItemGet}, {"item.create", Phase2Method::ItemCreate},
	{"item.remove", Phase2Method::ItemRemove}, {"item.duplicate", Phase2Method::ItemDuplicate},
	{"item.getTransform", Phase2Method::ItemGetTransform}, {"item.setTransform", Phase2Method::ItemSetTransform},
	{"item.setPosition", Phase2Method::ItemSetPosition}, {"item.setScale", Phase2Method::ItemSetScale},
	{"item.setRotation", Phase2Method::ItemSetRotation}, {"item.setAlignment", Phase2Method::ItemSetAlignment},
	{"item.setBounds", Phase2Method::ItemSetBounds}, {"item.setBoundsAlignment", Phase2Method::ItemSetBoundsAlignment},
	{"item.setCrop", Phase2Method::ItemSetCrop}, {"item.setCropToBounds", Phase2Method::ItemSetCropToBounds},
	{"item.setVisible", Phase2Method::ItemSetVisible}, {"item.setLocked", Phase2Method::ItemSetLocked},
	{"item.setOrder", Phase2Method::ItemSetOrder}, {"item.moveUp", Phase2Method::ItemMoveUp},
	{"item.moveDown", Phase2Method::ItemMoveDown}, {"item.moveTop", Phase2Method::ItemMoveTop},
	{"item.moveBottom", Phase2Method::ItemMoveBottom}, {"item.setScaleFilter", Phase2Method::ItemSetScaleFilter},
	{"item.setBlendMode", Phase2Method::ItemSetBlendMode}, {"item.setBlendMethod", Phase2Method::ItemSetBlendMethod},
	{"item.createGroup", Phase2Method::ItemCreateGroup}, {"item.ungroup", Phase2Method::ItemUngroup},
	{"item.addToGroup", Phase2Method::ItemAddToGroup}, {"item.removeFromGroup", Phase2Method::ItemRemoveFromGroup},
	{"item.getChildren", Phase2Method::ItemGetChildren},
	{"canvas.list", Phase2Method::CanvasList}, {"canvas.getMain", Phase2Method::CanvasGetMain},
	{"canvas.get", Phase2Method::CanvasGet}, {"canvas.create", Phase2Method::CanvasCreate},
	{"canvas.remove", Phase2Method::CanvasRemove}, {"canvas.rename", Phase2Method::CanvasRename},
	{"canvas.getVideoSettings", Phase2Method::CanvasGetVideoSettings},
	{"canvas.setVideoSettings", Phase2Method::CanvasSetVideoSettings},
	{"canvas.listScenes", Phase2Method::CanvasListScenes}, {"canvas.getChannel", Phase2Method::CanvasGetChannel},
	{"canvas.setChannel", Phase2Method::CanvasSetChannel}, {"canvas.getFlags", Phase2Method::CanvasGetFlags},
	{"program.getScene", Phase2Method::ProgramGetScene}, {"program.setScene", Phase2Method::ProgramSetScene},
	{"preview.getScene", Phase2Method::PreviewGetScene}, {"preview.setScene", Phase2Method::PreviewSetScene},
	{"preview.getInfo", Phase2Method::PreviewGetInfo},
	{"previewOutput.create", Phase2Method::PreviewOutputCreate},
	{"previewOutput.destroy", Phase2Method::PreviewOutputDestroy},
	{"previewOutput.list", Phase2Method::PreviewOutputList},
	{"previewOutput.get", Phase2Method::PreviewOutputGet},
	{"previewOutput.setTarget", Phase2Method::PreviewOutputSetTarget},
	{"previewOutput.getInfo", Phase2Method::PreviewOutputGetInfo},
	{"previewOutput.setEnabled", Phase2Method::PreviewOutputSetEnabled},
	{"previewOutput.resize", Phase2Method::PreviewOutputResize},
	{"previewOutput.getSharedTexture", Phase2Method::PreviewOutputGetSharedTexture},
	{"previewOutput.releaseSharedTexture", Phase2Method::PreviewOutputReleaseSharedTexture},
	{"transition.kindList", Phase2Method::TransitionKindList},
	{"transition.kindDefaults", Phase2Method::TransitionKindDefaults},
	{"transition.kindProperties", Phase2Method::TransitionKindProperties},
	{"transition.list", Phase2Method::TransitionList},
	{"transition.get", Phase2Method::TransitionGet},
	{"transition.create", Phase2Method::TransitionCreate},
	{"transition.remove", Phase2Method::TransitionRemove},
	{"transition.rename", Phase2Method::TransitionRename},
	{"transition.getSettings", Phase2Method::TransitionGetSettings},
	{"transition.patchSettings", Phase2Method::TransitionPatchSettings},
	{"transition.replaceSettings", Phase2Method::TransitionReplaceSettings},
	{"transition.getProperties", Phase2Method::TransitionGetProperties},
	{"transition.getDuration", Phase2Method::TransitionGetDuration},
	{"transition.setDuration", Phase2Method::TransitionSetDuration},
	{"transition.getState", Phase2Method::TransitionGetState},
	{"studio.getEnabled", Phase2Method::StudioGetEnabled},
	{"studio.setEnabled", Phase2Method::StudioSetEnabled},
	{"studio.getTransition", Phase2Method::StudioGetTransition},
	{"studio.setTransition", Phase2Method::StudioSetTransition},
	{"studio.getTransitionDuration", Phase2Method::StudioGetTransitionDuration},
	{"studio.setTransitionDuration", Phase2Method::StudioSetTransitionDuration},
	{"studio.transition", Phase2Method::StudioTransition},
};

Phase2Method classify(std::string_view method)
{
	for (const Phase2MethodName &entry : kMethods) {
		if (entry.name == method)
			return entry.method;
	}
	return Phase2Method::Unknown;
}

bool mutating(Phase2Method method)
{
	constexpr Phase2Method mutating_methods[] = {
		Phase2Method::SceneCreate,
		Phase2Method::SceneRemove,
		Phase2Method::SceneRename,
		Phase2Method::SceneDuplicate,
		Phase2Method::ItemCreate,
		Phase2Method::ItemRemove,
		Phase2Method::ItemDuplicate,
		Phase2Method::ItemSetTransform,
		Phase2Method::ItemSetPosition,
		Phase2Method::ItemSetScale,
		Phase2Method::ItemSetRotation,
		Phase2Method::ItemSetAlignment,
		Phase2Method::ItemSetBounds,
		Phase2Method::ItemSetBoundsAlignment,
		Phase2Method::ItemSetCrop,
		Phase2Method::ItemSetCropToBounds,
		Phase2Method::ItemSetVisible,
		Phase2Method::ItemSetLocked,
		Phase2Method::ItemSetOrder,
		Phase2Method::ItemMoveUp,
		Phase2Method::ItemMoveDown,
		Phase2Method::ItemMoveTop,
		Phase2Method::ItemMoveBottom,
		Phase2Method::ItemSetScaleFilter,
		Phase2Method::ItemSetBlendMode,
		Phase2Method::ItemSetBlendMethod,
		Phase2Method::ItemCreateGroup,
		Phase2Method::ItemUngroup,
		Phase2Method::ItemAddToGroup,
		Phase2Method::ItemRemoveFromGroup,
		Phase2Method::CanvasCreate,
		Phase2Method::CanvasRemove,
		Phase2Method::CanvasRename,
		Phase2Method::CanvasSetVideoSettings,
		Phase2Method::CanvasSetChannel,
		Phase2Method::ProgramSetScene,
		Phase2Method::PreviewSetScene,
		Phase2Method::PreviewOutputCreate,
		Phase2Method::PreviewOutputDestroy,
		Phase2Method::PreviewOutputSetEnabled,
		Phase2Method::PreviewOutputResize,
		Phase2Method::PreviewOutputSetTarget,
		Phase2Method::TransitionCreate,
		Phase2Method::TransitionRemove,
		Phase2Method::TransitionRename,
		Phase2Method::TransitionPatchSettings,
		Phase2Method::TransitionReplaceSettings,
		Phase2Method::TransitionSetDuration,
		Phase2Method::StudioSetEnabled,
		Phase2Method::StudioSetTransition,
		Phase2Method::StudioSetTransitionDuration,
		Phase2Method::StudioTransition,
	};
	for (const Phase2Method candidate : mutating_methods) {
		if (candidate == method)
			return true;
	}
	return false;
}

struct HandlerEntry {
	Phase2Method method;
	Handler handler;
};

constexpr HandlerEntry kHandlers[] = {
	{Phase2Method::SceneList, &Engine::v2_scene_list},
	{Phase2Method::SceneGet, &Engine::v2_scene_get},
	{Phase2Method::SceneCreate, &Engine::v2_scene_create},
	{Phase2Method::SceneRemove, &Engine::v2_scene_remove},
	{Phase2Method::SceneRename, &Engine::v2_scene_rename},
	{Phase2Method::SceneDuplicate, &Engine::v2_scene_duplicate},
	{Phase2Method::SceneGetItems, &Engine::v2_scene_get_items},
	{Phase2Method::SceneGetState, &Engine::v2_scene_get_state},
	{Phase2Method::ItemGet, &Engine::v2_item_get},
	{Phase2Method::ItemCreate, &Engine::v2_item_create},
	{Phase2Method::ItemRemove, &Engine::v2_item_remove},
	{Phase2Method::ItemDuplicate, &Engine::v2_item_duplicate},
	{Phase2Method::ItemGetTransform, &Engine::v2_item_get_transform},
	{Phase2Method::ItemSetTransform, &Engine::v2_item_set_transform},
	{Phase2Method::ItemSetPosition, &Engine::v2_item_set_position},
	{Phase2Method::ItemSetScale, &Engine::v2_item_set_scale},
	{Phase2Method::ItemSetRotation, &Engine::v2_item_set_rotation},
	{Phase2Method::ItemSetAlignment, &Engine::v2_item_set_alignment},
	{Phase2Method::ItemSetBounds, &Engine::v2_item_set_bounds},
	{Phase2Method::ItemSetBoundsAlignment, &Engine::v2_item_set_bounds_alignment},
	{Phase2Method::ItemSetCrop, &Engine::v2_item_set_crop},
	{Phase2Method::ItemSetCropToBounds, &Engine::v2_item_set_crop_to_bounds},
	{Phase2Method::ItemSetVisible, &Engine::v2_item_set_visible},
	{Phase2Method::ItemSetLocked, &Engine::v2_item_set_locked},
	{Phase2Method::ItemSetOrder, &Engine::v2_item_set_order},
	{Phase2Method::ItemMoveUp, &Engine::v2_item_move_up},
	{Phase2Method::ItemMoveDown, &Engine::v2_item_move_down},
	{Phase2Method::ItemMoveTop, &Engine::v2_item_move_top},
	{Phase2Method::ItemMoveBottom, &Engine::v2_item_move_bottom},
	{Phase2Method::ItemSetScaleFilter, &Engine::v2_item_set_scale_filter},
	{Phase2Method::ItemSetBlendMode, &Engine::v2_item_set_blend_mode},
	{Phase2Method::ItemSetBlendMethod, &Engine::v2_item_set_blend_method},
	{Phase2Method::ItemCreateGroup, &Engine::v2_item_create_group},
	{Phase2Method::ItemUngroup, &Engine::v2_item_ungroup},
	{Phase2Method::ItemAddToGroup, &Engine::v2_item_add_to_group},
	{Phase2Method::ItemRemoveFromGroup, &Engine::v2_item_remove_from_group},
	{Phase2Method::ItemGetChildren, &Engine::v2_item_get_children},
	{Phase2Method::CanvasList, &Engine::v2_canvas_list},
	{Phase2Method::CanvasGetMain, &Engine::v2_canvas_get_main},
	{Phase2Method::CanvasGet, &Engine::v2_canvas_get},
	{Phase2Method::CanvasCreate, &Engine::v2_canvas_create},
	{Phase2Method::CanvasRemove, &Engine::v2_canvas_remove},
	{Phase2Method::CanvasRename, &Engine::v2_canvas_rename},
	{Phase2Method::CanvasGetVideoSettings, &Engine::v2_canvas_get_video_settings},
	{Phase2Method::CanvasSetVideoSettings, &Engine::v2_canvas_set_video_settings},
	{Phase2Method::CanvasListScenes, &Engine::v2_canvas_list_scenes},
	{Phase2Method::CanvasGetChannel, &Engine::v2_canvas_get_channel},
	{Phase2Method::CanvasSetChannel, &Engine::v2_canvas_set_channel},
	{Phase2Method::CanvasGetFlags, &Engine::v2_canvas_get_flags},
	{Phase2Method::ProgramGetScene, &Engine::v2_program_get_scene},
	{Phase2Method::ProgramSetScene, &Engine::v2_program_set_scene},
	{Phase2Method::PreviewGetScene, &Engine::v2_preview_get_scene},
	{Phase2Method::PreviewSetScene, &Engine::v2_preview_set_scene},
	{Phase2Method::PreviewGetInfo, &Engine::v2_preview_get_info},
	{Phase2Method::PreviewOutputCreate, &Engine::v2_preview_output_create},
	{Phase2Method::PreviewOutputDestroy, &Engine::v2_preview_output_destroy},
	{Phase2Method::PreviewOutputList, &Engine::v2_preview_output_list},
	{Phase2Method::PreviewOutputGet, &Engine::v2_preview_output_get},
	{Phase2Method::PreviewOutputSetTarget, &Engine::v2_preview_output_set_target},
	{Phase2Method::PreviewOutputGetInfo, &Engine::v2_preview_output_get_info},
	{Phase2Method::PreviewOutputSetEnabled, &Engine::v2_preview_output_set_enabled},
	{Phase2Method::PreviewOutputResize, &Engine::v2_preview_output_resize},
	{Phase2Method::PreviewOutputGetSharedTexture, &Engine::v2_preview_output_get_shared_texture},
	{Phase2Method::PreviewOutputReleaseSharedTexture, &Engine::v2_preview_output_release_shared_texture},
	{Phase2Method::TransitionKindList, &Engine::v2_transition_kind_list},
	{Phase2Method::TransitionKindDefaults, &Engine::v2_transition_kind_defaults},
	{Phase2Method::TransitionKindProperties, &Engine::v2_transition_kind_properties},
	{Phase2Method::TransitionList, &Engine::v2_transition_list},
	{Phase2Method::TransitionGet, &Engine::v2_transition_get},
	{Phase2Method::TransitionCreate, &Engine::v2_transition_create},
	{Phase2Method::TransitionRemove, &Engine::v2_transition_remove},
	{Phase2Method::TransitionRename, &Engine::v2_transition_rename},
	{Phase2Method::TransitionGetSettings, &Engine::v2_transition_get_settings},
	{Phase2Method::TransitionPatchSettings, &Engine::v2_transition_patch_settings},
	{Phase2Method::TransitionReplaceSettings, &Engine::v2_transition_replace_settings},
	{Phase2Method::TransitionGetProperties, &Engine::v2_transition_get_properties},
	{Phase2Method::TransitionGetDuration, &Engine::v2_transition_get_duration},
	{Phase2Method::TransitionSetDuration, &Engine::v2_transition_set_duration},
	{Phase2Method::TransitionGetState, &Engine::v2_transition_get_state},
	{Phase2Method::StudioGetEnabled, &Engine::v2_studio_get_enabled},
	{Phase2Method::StudioSetEnabled, &Engine::v2_studio_set_enabled},
	{Phase2Method::StudioGetTransition, &Engine::v2_studio_get_transition},
	{Phase2Method::StudioSetTransition, &Engine::v2_studio_set_transition},
	{Phase2Method::StudioGetTransitionDuration, &Engine::v2_studio_get_transition_duration},
	{Phase2Method::StudioSetTransitionDuration, &Engine::v2_studio_set_transition_duration},
	{Phase2Method::StudioTransition, &Engine::v2_studio_transition},
};

Handler handler_for(Phase2Method method)
{
	for (const HandlerEntry &entry : kHandlers) {
		if (entry.method == method)
			return entry.handler;
	}
	return nullptr;
}

void publish_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events)
		events.publish(EngineEventKind::State, event.name, revision, event.data.get());
}

class Phase2CaptureScope {
public:
	Phase2CaptureScope(Engine &engine, RuntimeV2Result &result) : engine_(engine)
	{
		engine_.v2_begin_event_capture(result);
		engine_.v2_wait_for_event_capture_callbacks();
	}

	~Phase2CaptureScope()
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

	Phase2CaptureScope(const Phase2CaptureScope &) = delete;
	Phase2CaptureScope &operator=(const Phase2CaptureScope &) = delete;

private:
	Engine &engine_;
	bool active_ = true;
};

bool reject_read_guard(const V2Request &request, uint64_t revision)
{
	if (!request.has_if_revision)
		return false;
	send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
			      revision);
	return true;
}

bool validate_guard(const V2Request &request, RevisionState::MutationGuard &guard)
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

bool prepare(const V2Request &request, Phase2Method method, Engine &engine, RevisionState &revisions,
		     RuntimeV2Result &result, std::optional<Phase2CaptureScope> &capture,
		     std::optional<RevisionState::MutationGuard> &guard)
{
	if (mutating(method)) {
		capture.emplace(engine, result);
		guard.emplace(revisions.lock_mutation());
		engine.v2_drain_deferred_source_events(*guard);
	}
	const uint64_t current = guard ? guard->current() : revisions.current();
	if ((mutating(method) && !validate_guard(request, *guard)) || (!mutating(method) && reject_read_guard(request, current))) {
		if (capture)
			capture->flush(*guard);
		return false;
	}
	if (guard && !guard->can_commit_mutation()) {
		send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr, guard->current());
		capture->flush(*guard);
		return false;
	}
	return true;
}

bool execute_phase2_handler(Engine &engine, Phase2Method method, obs_data_t *params, RuntimeV2Result &result,
				    RuntimeV2Error &error)
{
	const Handler handler = handler_for(method);
	return handler && (engine.*handler)(params, result, error);
}

void normalize_phase2_error(RuntimeV2Error &error)
{
	if (error.code.empty()) {
		error.code = "internal_error";
		error.message = "Phase-2 dispatch failed";
	}
}

void flush_phase2_capture(std::optional<Phase2CaptureScope> &capture,
				 std::optional<RevisionState::MutationGuard> &guard)
{
	if (capture)
		capture->flush(*guard);
}

bool commit_phase2_result(const V2Request &request, RuntimeV2Result &result, RuntimeV2Error &error,
				  RevisionState &revisions, std::optional<RevisionState::MutationGuard> &guard,
				  std::optional<Phase2CaptureScope> &capture, uint64_t &revision)
{
	revision = guard ? guard->current() : revisions.current();
	if (!result.mutated)
		return true;
	if (guard) {
		revision = guard->commit_mutation();
		return true;
	}
	send_v2_error(request.id, "internal_error", "read-only Phase-2 method mutated engine state", nullptr,
		      revisions.current());
	flush_phase2_capture(capture, guard);
	error.code = "internal_error";
	error.message = "read-only Phase-2 method mutated engine state";
	return false;
}

} // namespace

bool is_phase2_method(std::string_view method)
{
	return classify(method) != Phase2Method::Unknown;
}

bool handle_phase2_request(Engine &engine, RevisionState &revisions, EventDispatcher &events,
				   const V2Request &request)
{
	const Phase2Method method = classify(request.method);
	if (method == Phase2Method::Unknown)
		return false;

	RuntimeV2Result result;
	RuntimeV2Error error;
	std::optional<Phase2CaptureScope> capture;
	std::optional<RevisionState::MutationGuard> guard;
	if (!prepare(request, method, engine, revisions, result, capture, guard))
		return true;

	const bool succeeded = execute_phase2_handler(engine, method, request.params.get(), result, error);
	engine.v2_sync_source_observers();
	if (!succeeded) {
		normalize_phase2_error(error);
		send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
			      guard ? guard->current() : revisions.current());
		flush_phase2_capture(capture, guard);
		return true;
	}

	uint64_t revision = 0;
	if (!commit_phase2_result(request, result, error, revisions, guard, capture, revision))
		return true;
	send_v2_ok(request.id, result.data.get(), revision);
	publish_events(events, revision, result);
	flush_phase2_capture(capture, guard);
	return true;
}

} // namespace obs_engine
