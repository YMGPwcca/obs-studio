#include "protocol_phase2_v2.hpp"

#include "events.hpp"
#include "runtime.hpp"

#include <obs.h>

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
	Unknown,
};

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
	switch (method) {
	case Phase2Method::SceneCreate:
	case Phase2Method::SceneRemove:
	case Phase2Method::SceneRename:
	case Phase2Method::SceneDuplicate:
	case Phase2Method::ItemCreate:
	case Phase2Method::ItemRemove:
	case Phase2Method::ItemDuplicate:
	case Phase2Method::ItemSetTransform:
	case Phase2Method::ItemSetPosition:
	case Phase2Method::ItemSetScale:
	case Phase2Method::ItemSetRotation:
	case Phase2Method::ItemSetAlignment:
	case Phase2Method::ItemSetBounds:
	case Phase2Method::ItemSetBoundsAlignment:
	case Phase2Method::ItemSetCrop:
	case Phase2Method::ItemSetCropToBounds:
	case Phase2Method::ItemSetVisible:
	case Phase2Method::ItemSetLocked:
	case Phase2Method::ItemSetOrder:
	case Phase2Method::ItemMoveUp:
	case Phase2Method::ItemMoveDown:
	case Phase2Method::ItemMoveTop:
	case Phase2Method::ItemMoveBottom:
	case Phase2Method::ItemSetScaleFilter:
	case Phase2Method::ItemSetBlendMode:
	case Phase2Method::ItemSetBlendMethod:
	case Phase2Method::ItemCreateGroup:
	case Phase2Method::ItemUngroup:
	case Phase2Method::ItemAddToGroup:
	case Phase2Method::ItemRemoveFromGroup:
		return true;
	case Phase2Method::CanvasCreate:
	case Phase2Method::CanvasRemove:
	case Phase2Method::CanvasRename:
	case Phase2Method::CanvasSetVideoSettings:
	case Phase2Method::CanvasSetChannel:
		return true;
	case Phase2Method::ProgramSetScene:
		return true;
	default:
		return false;
	}
}

using Handler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

Handler handler_for(Phase2Method method)
{
	switch (method) {
	case Phase2Method::SceneList:
		return &Engine::v2_scene_list;
	case Phase2Method::SceneGet:
		return &Engine::v2_scene_get;
	case Phase2Method::SceneCreate:
		return &Engine::v2_scene_create;
	case Phase2Method::SceneRemove:
		return &Engine::v2_scene_remove;
	case Phase2Method::SceneRename:
		return &Engine::v2_scene_rename;
	case Phase2Method::SceneDuplicate:
		return &Engine::v2_scene_duplicate;
	case Phase2Method::SceneGetItems:
		return &Engine::v2_scene_get_items;
	case Phase2Method::SceneGetState:
		return &Engine::v2_scene_get_state;
	case Phase2Method::ItemGet:
		return &Engine::v2_item_get;
	case Phase2Method::ItemCreate:
		return &Engine::v2_item_create;
	case Phase2Method::ItemRemove:
		return &Engine::v2_item_remove;
	case Phase2Method::ItemDuplicate:
		return &Engine::v2_item_duplicate;
	case Phase2Method::ItemGetTransform:
		return &Engine::v2_item_get_transform;
	case Phase2Method::ItemSetTransform:
		return &Engine::v2_item_set_transform;
	case Phase2Method::ItemSetPosition:
		return &Engine::v2_item_set_position;
	case Phase2Method::ItemSetScale:
		return &Engine::v2_item_set_scale;
	case Phase2Method::ItemSetRotation:
		return &Engine::v2_item_set_rotation;
	case Phase2Method::ItemSetAlignment:
		return &Engine::v2_item_set_alignment;
	case Phase2Method::ItemSetBounds:
		return &Engine::v2_item_set_bounds;
	case Phase2Method::ItemSetBoundsAlignment:
		return &Engine::v2_item_set_bounds_alignment;
	case Phase2Method::ItemSetCrop:
		return &Engine::v2_item_set_crop;
	case Phase2Method::ItemSetCropToBounds:
		return &Engine::v2_item_set_crop_to_bounds;
	case Phase2Method::ItemSetVisible:
		return &Engine::v2_item_set_visible;
	case Phase2Method::ItemSetLocked:
		return &Engine::v2_item_set_locked;
	case Phase2Method::ItemSetOrder:
		return &Engine::v2_item_set_order;
	case Phase2Method::ItemMoveUp:
		return &Engine::v2_item_move_up;
	case Phase2Method::ItemMoveDown:
		return &Engine::v2_item_move_down;
	case Phase2Method::ItemMoveTop:
		return &Engine::v2_item_move_top;
	case Phase2Method::ItemMoveBottom:
		return &Engine::v2_item_move_bottom;
	case Phase2Method::ItemSetScaleFilter:
		return &Engine::v2_item_set_scale_filter;
	case Phase2Method::ItemSetBlendMode:
		return &Engine::v2_item_set_blend_mode;
	case Phase2Method::ItemSetBlendMethod:
		return &Engine::v2_item_set_blend_method;
	case Phase2Method::ItemCreateGroup:
		return &Engine::v2_item_create_group;
	case Phase2Method::ItemUngroup:
		return &Engine::v2_item_ungroup;
	case Phase2Method::ItemAddToGroup:
		return &Engine::v2_item_add_to_group;
	case Phase2Method::ItemRemoveFromGroup:
		return &Engine::v2_item_remove_from_group;
	case Phase2Method::ItemGetChildren:
		return &Engine::v2_item_get_children;
	case Phase2Method::CanvasList:
		return &Engine::v2_canvas_list;
	case Phase2Method::CanvasGetMain:
		return &Engine::v2_canvas_get_main;
	case Phase2Method::CanvasGet:
		return &Engine::v2_canvas_get;
	case Phase2Method::CanvasCreate:
		return &Engine::v2_canvas_create;
	case Phase2Method::CanvasRemove:
		return &Engine::v2_canvas_remove;
	case Phase2Method::CanvasRename:
		return &Engine::v2_canvas_rename;
	case Phase2Method::CanvasGetVideoSettings:
		return &Engine::v2_canvas_get_video_settings;
	case Phase2Method::CanvasSetVideoSettings:
		return &Engine::v2_canvas_set_video_settings;
	case Phase2Method::CanvasListScenes:
		return &Engine::v2_canvas_list_scenes;
	case Phase2Method::CanvasGetChannel:
		return &Engine::v2_canvas_get_channel;
	case Phase2Method::CanvasSetChannel:
		return &Engine::v2_canvas_set_channel;
	case Phase2Method::CanvasGetFlags:
		return &Engine::v2_canvas_get_flags;
	case Phase2Method::ProgramGetScene:
		return &Engine::v2_program_get_scene;
	case Phase2Method::ProgramSetScene:
		return &Engine::v2_program_set_scene;
	case Phase2Method::Unknown:
		return nullptr;
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

	const Handler handler = handler_for(method);
	const bool succeeded = handler && (engine.*handler)(request.params.get(), result, error);
	engine.v2_sync_source_observers();
	if (!succeeded) {
		if (error.code.empty()) {
			error.code = "internal_error";
			error.message = "Phase-2 scene dispatch failed";
		}
		send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
			      guard ? guard->current() : revisions.current());
		if (capture)
			capture->flush(*guard);
		return true;
	}

	uint64_t revision = guard ? guard->current() : revisions.current();
	if (result.mutated) {
		if (!guard) {
			send_v2_error(request.id, "internal_error", "read-only scene method mutated engine state", nullptr,
				      revisions.current());
			if (capture)
				capture->flush(*guard);
			return true;
		}
		revision = guard->commit_mutation();
	}
	send_v2_ok(request.id, result.data.get(), revision);
	publish_events(events, revision, result);
	if (capture)
		capture->flush(*guard);
	return true;
}

} // namespace obs_engine
