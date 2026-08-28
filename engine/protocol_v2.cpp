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
	FilterKindList,
	FilterKindDefaults,
	FilterKindProperties,
	FilterList,
	FilterGet,
	FilterCreate,
	FilterRemove,
	FilterRename,
	FilterDuplicate,
	FilterGetSettings,
	FilterPatchSettings,
	FilterReplaceSettings,
	FilterSetEnabled,
	FilterGetEnabled,
	FilterSetOrder,
	FilterMoveUp,
	FilterMoveDown,
	FilterMoveTop,
	FilterMoveBottom,
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

V2Method classify_method(std::string_view method)
{
	if (method == "session.hello")
		return V2Method::SessionHello;
	if (method == "session.ping")
		return V2Method::SessionPing;
	if (method == "session.subscribe")
		return V2Method::SessionSubscribe;
	if (method == "session.unsubscribe")
		return V2Method::SessionUnsubscribe;
	if (method == "session.getSubscriptions")
		return V2Method::SessionGetSubscriptions;
	if (method == "session.close")
		return V2Method::SessionClose;
	if (method == "engine.getCapabilities")
		return V2Method::EngineGetCapabilities;
	if (method == "properties.get")
		return V2Method::PropertiesGet;
	if (method == "properties.resolve")
		return V2Method::PropertiesResolve;
	if (method == "properties.getListItems")
		return V2Method::PropertiesGetListItems;
	if (method == "properties.invokeButton")
		return V2Method::PropertiesInvokeButton;
	if (method == "properties.validate")
		return V2Method::PropertiesValidate;
	if (method == "properties.refresh")
		return V2Method::PropertiesRefresh;
	if (method == "source.kindList")
		return V2Method::SourceKindList;
	if (method == "source.kindGet")
		return V2Method::SourceKindGet;
	if (method == "source.kindDefaults")
		return V2Method::SourceKindDefaults;
	if (method == "source.kindProperties")
		return V2Method::SourceKindProperties;
	if (method == "source.list")
		return V2Method::SourceList;
	if (method == "source.get")
		return V2Method::SourceGet;
	if (method == "source.create")
		return V2Method::SourceCreate;
	if (method == "source.duplicate")
		return V2Method::SourceDuplicate;
	if (method == "source.remove")
		return V2Method::SourceRemove;
	if (method == "source.rename")
		return V2Method::SourceRename;
	if (method == "source.getSettings")
		return V2Method::SourceGetSettings;
	if (method == "source.patchSettings")
		return V2Method::SourcePatchSettings;
	if (method == "source.replaceSettings")
		return V2Method::SourceReplaceSettings;
	if (method == "source.resetSettings")
		return V2Method::SourceResetSettings;
	if (method == "source.getProperties")
		return V2Method::SourceGetProperties;
	if (method == "source.getFlags")
		return V2Method::SourceGetFlags;
	if (method == "source.getDimensions")
		return V2Method::SourceGetDimensions;
	if (method == "source.getState")
		return V2Method::SourceGetState;
	if (method == "source.getActive")
		return V2Method::SourceGetActive;
	if (method == "source.getShowing")
		return V2Method::SourceGetShowing;
	if (method == "source.getMissingFiles")
		return V2Method::SourceGetMissingFiles;
	if (method == "source.refresh")
		return V2Method::SourceRefresh;
	if (method == "source.saveState")
		return V2Method::SourceSaveState;
	if (method == "source.loadState")
		return V2Method::SourceLoadState;
	if (method == "interaction.focus")
		return V2Method::InteractionFocus;
	if (method == "interaction.mouseMove")
		return V2Method::InteractionMouseMove;
	if (method == "interaction.mouseButton")
		return V2Method::InteractionMouseButton;
	if (method == "interaction.mouseWheel")
		return V2Method::InteractionMouseWheel;
	if (method == "interaction.key")
		return V2Method::InteractionKey;
	if (method == "interaction.text")
		return V2Method::InteractionText;
	if (method == "interaction.reset")
		return V2Method::InteractionReset;
	if (method == "scene.create")
		return V2Method::SceneCreate;
	if (method == "scene.remove")
		return V2Method::SceneRemove;
	if (method == "item.create")
		return V2Method::ItemCreate;
	if (method == "item.remove")
		return V2Method::ItemRemove;
	if (method == "item.setTransform")
		return V2Method::ItemSetTransform;
	if (method == "filter.kindList")
		return V2Method::FilterKindList;
	if (method == "filter.kindDefaults")
		return V2Method::FilterKindDefaults;
	if (method == "filter.kindProperties")
		return V2Method::FilterKindProperties;
	if (method == "filter.list")
		return V2Method::FilterList;
	if (method == "filter.get")
		return V2Method::FilterGet;
	if (method == "filter.create")
		return V2Method::FilterCreate;
	if (method == "filter.remove")
		return V2Method::FilterRemove;
	if (method == "filter.rename")
		return V2Method::FilterRename;
	if (method == "filter.duplicate")
		return V2Method::FilterDuplicate;
	if (method == "filter.getSettings")
		return V2Method::FilterGetSettings;
	if (method == "filter.patchSettings")
		return V2Method::FilterPatchSettings;
	if (method == "filter.replaceSettings")
		return V2Method::FilterReplaceSettings;
	if (method == "filter.setEnabled")
		return V2Method::FilterSetEnabled;
	if (method == "filter.getEnabled")
		return V2Method::FilterGetEnabled;
	if (method == "filter.setOrder")
		return V2Method::FilterSetOrder;
	if (method == "filter.moveUp")
		return V2Method::FilterMoveUp;
	if (method == "filter.moveDown")
		return V2Method::FilterMoveDown;
	if (method == "filter.moveTop")
		return V2Method::FilterMoveTop;
	if (method == "filter.moveBottom")
		return V2Method::FilterMoveBottom;
	if (method == "media.getState")
		return V2Method::MediaGetState;
	if (method == "media.play")
		return V2Method::MediaPlay;
	if (method == "media.pause")
		return V2Method::MediaPause;
	if (method == "media.togglePause")
		return V2Method::MediaTogglePause;
	if (method == "media.stop")
		return V2Method::MediaStop;
	if (method == "media.restart")
		return V2Method::MediaRestart;
	if (method == "media.next")
		return V2Method::MediaNext;
	if (method == "media.previous")
		return V2Method::MediaPrevious;
	if (method == "media.getDuration")
		return V2Method::MediaGetDuration;
	if (method == "media.getPosition")
		return V2Method::MediaGetPosition;
	if (method == "media.setPosition")
		return V2Method::MediaSetPosition;
	return V2Method::Unknown;
}

bool method_is_mutating(V2Method method)
{
	switch (method) {
	case V2Method::SessionClose:
	case V2Method::PropertiesInvokeButton:
	case V2Method::SourceCreate:
	case V2Method::SourceDuplicate:
	case V2Method::SourceRemove:
	case V2Method::SourceRename:
	case V2Method::SourcePatchSettings:
	case V2Method::SourceReplaceSettings:
	case V2Method::SourceResetSettings:
	case V2Method::SourceLoadState:
	case V2Method::SceneCreate:
	case V2Method::SceneRemove:
	case V2Method::ItemCreate:
	case V2Method::ItemRemove:
	case V2Method::ItemSetTransform:
	case V2Method::FilterCreate:
	case V2Method::FilterRemove:
	case V2Method::FilterRename:
	case V2Method::FilterDuplicate:
	case V2Method::FilterPatchSettings:
	case V2Method::FilterReplaceSettings:
	case V2Method::FilterSetEnabled:
	case V2Method::FilterSetOrder:
	case V2Method::FilterMoveUp:
	case V2Method::FilterMoveDown:
	case V2Method::FilterMoveTop:
	case V2Method::FilterMoveBottom:
	case V2Method::MediaPlay:
	case V2Method::MediaPause:
	case V2Method::MediaTogglePause:
	case V2Method::MediaStop:
	case V2Method::MediaRestart:
	case V2Method::MediaNext:
	case V2Method::MediaPrevious:
	case V2Method::MediaSetPosition:
		return true;
	default:
		return false;
	}
}

bool method_is_runtime(V2Method method)
{
	switch (method) {
	case V2Method::PropertiesGet:
	case V2Method::PropertiesResolve:
	case V2Method::PropertiesGetListItems:
	case V2Method::PropertiesInvokeButton:
	case V2Method::PropertiesValidate:
	case V2Method::PropertiesRefresh:
	case V2Method::SourceKindList:
	case V2Method::SourceKindGet:
	case V2Method::SourceKindDefaults:
	case V2Method::SourceKindProperties:
	case V2Method::SourceList:
	case V2Method::SourceGet:
	case V2Method::SourceCreate:
	case V2Method::SourceDuplicate:
	case V2Method::SourceRemove:
	case V2Method::SourceRename:
	case V2Method::SourceGetSettings:
	case V2Method::SourcePatchSettings:
	case V2Method::SourceReplaceSettings:
	case V2Method::SourceResetSettings:
	case V2Method::SourceGetProperties:
	case V2Method::SourceGetFlags:
	case V2Method::SourceGetDimensions:
	case V2Method::SourceGetState:
	case V2Method::SourceGetActive:
	case V2Method::SourceGetShowing:
	case V2Method::SourceGetMissingFiles:
	case V2Method::SourceRefresh:
	case V2Method::SourceSaveState:
	case V2Method::SourceLoadState:
	case V2Method::InteractionFocus:
	case V2Method::InteractionMouseMove:
	case V2Method::InteractionMouseButton:
	case V2Method::InteractionMouseWheel:
	case V2Method::InteractionKey:
	case V2Method::InteractionText:
	case V2Method::InteractionReset:
	case V2Method::SceneCreate:
	case V2Method::SceneRemove:
	case V2Method::ItemCreate:
	case V2Method::ItemRemove:
	case V2Method::ItemSetTransform:
	case V2Method::FilterKindList:
	case V2Method::FilterKindDefaults:
	case V2Method::FilterKindProperties:
	case V2Method::FilterList:
	case V2Method::FilterGet:
	case V2Method::FilterCreate:
	case V2Method::FilterRemove:
	case V2Method::FilterRename:
	case V2Method::FilterDuplicate:
	case V2Method::FilterGetSettings:
	case V2Method::FilterPatchSettings:
	case V2Method::FilterReplaceSettings:
	case V2Method::FilterSetEnabled:
	case V2Method::FilterGetEnabled:
	case V2Method::FilterSetOrder:
	case V2Method::FilterMoveUp:
	case V2Method::FilterMoveDown:
	case V2Method::FilterMoveTop:
	case V2Method::FilterMoveBottom:
	case V2Method::MediaGetState:
	case V2Method::MediaPlay:
	case V2Method::MediaPause:
	case V2Method::MediaTogglePause:
	case V2Method::MediaStop:
	case V2Method::MediaRestart:
	case V2Method::MediaNext:
	case V2Method::MediaPrevious:
	case V2Method::MediaGetDuration:
	case V2Method::MediaGetPosition:
	case V2Method::MediaSetPosition:
		return true;
	default:
		return false;
	}
}

bool method_needs_source_settle(V2Method method)
{
	switch (method) {
	case V2Method::SourcePatchSettings:
	case V2Method::SourceReplaceSettings:
	case V2Method::SourceResetSettings:
	case V2Method::SourceLoadState:
		return true;
	default:
		return false;
	}
}

bool method_needs_filter_settle(V2Method method)
{
	switch (method) {
	case V2Method::FilterPatchSettings:
	case V2Method::FilterReplaceSettings:
		return true;
	default:
		return false;
	}
}

bool execute_runtime_method(Engine &engine, V2Method method, obs_data_t *params, RuntimeV2Result &result,
			    RuntimeV2Error &error)
{
	switch (method) {
	case V2Method::PropertiesGet:
		return engine.v2_properties_get(params, result, error);
	case V2Method::PropertiesResolve:
		return engine.v2_properties_resolve(params, result, error);
	case V2Method::PropertiesGetListItems:
		return engine.v2_properties_get_list_items(params, result, error);
	case V2Method::PropertiesInvokeButton:
		return engine.v2_properties_invoke_button(params, result, error);
	case V2Method::PropertiesValidate:
		return engine.v2_properties_validate(params, result, error);
	case V2Method::PropertiesRefresh:
		return engine.v2_properties_refresh(params, result, error);
	case V2Method::SourceKindList:
		return engine.v2_source_kind_list(params, result, error);
	case V2Method::SourceKindGet:
		return engine.v2_source_kind_get(params, result, error);
	case V2Method::SourceKindDefaults:
		return engine.v2_source_kind_defaults(params, result, error);
	case V2Method::SourceKindProperties:
		return engine.v2_source_kind_properties(params, result, error);
	case V2Method::SourceList:
		return engine.v2_source_list(params, result, error);
	case V2Method::SourceGet:
		return engine.v2_source_get(params, result, error);
	case V2Method::SourceCreate:
		return engine.v2_source_create(params, result, error);
	case V2Method::SourceDuplicate:
		return engine.v2_source_duplicate(params, result, error);
	case V2Method::SourceRemove:
		return engine.v2_source_remove(params, result, error);
	case V2Method::SourceRename:
		return engine.v2_source_rename(params, result, error);
	case V2Method::SourceGetSettings:
		return engine.v2_source_get_settings(params, result, error);
	case V2Method::SourcePatchSettings:
		return engine.v2_source_patch_settings(params, result, error);
	case V2Method::SourceReplaceSettings:
		return engine.v2_source_replace_settings(params, result, error);
	case V2Method::SourceResetSettings:
		return engine.v2_source_reset_settings(params, result, error);
	case V2Method::SourceGetProperties:
		return engine.v2_source_get_properties(params, result, error);
	case V2Method::SourceGetFlags:
		return engine.v2_source_get_flags(params, result, error);
	case V2Method::SourceGetDimensions:
		return engine.v2_source_get_dimensions(params, result, error);
	case V2Method::SourceGetState:
		return engine.v2_source_get_state(params, result, error);
	case V2Method::SourceGetActive:
		return engine.v2_source_get_active(params, result, error);
	case V2Method::SourceGetShowing:
		return engine.v2_source_get_showing(params, result, error);
	case V2Method::SourceGetMissingFiles:
		return engine.v2_source_get_missing_files(params, result, error);
	case V2Method::SourceRefresh:
		return engine.v2_source_refresh(params, result, error);
	case V2Method::SourceSaveState:
		return engine.v2_source_save_state(params, result, error);
	case V2Method::SourceLoadState:
		return engine.v2_source_load_state(params, result, error);
	case V2Method::InteractionFocus:
		return engine.v2_interaction_focus(params, result, error);
	case V2Method::InteractionMouseMove:
		return engine.v2_interaction_mouse_move(params, result, error);
	case V2Method::InteractionMouseButton:
		return engine.v2_interaction_mouse_button(params, result, error);
	case V2Method::InteractionMouseWheel:
		return engine.v2_interaction_mouse_wheel(params, result, error);
	case V2Method::InteractionKey:
		return engine.v2_interaction_key(params, result, error);
	case V2Method::InteractionText:
		return engine.v2_interaction_text(params, result, error);
	case V2Method::InteractionReset:
		return engine.v2_interaction_reset(params, result, error);
	case V2Method::SceneCreate:
		return engine.v2_scene_create(params, result, error);
	case V2Method::SceneRemove:
		return engine.v2_scene_remove(params, result, error);
	case V2Method::ItemCreate:
		return engine.v2_item_create(params, result, error);
	case V2Method::ItemRemove:
		return engine.v2_item_remove(params, result, error);
	case V2Method::ItemSetTransform:
		return engine.v2_item_set_transform(params, result, error);
	case V2Method::FilterKindList:
		return engine.v2_filter_kind_list(params, result, error);
	case V2Method::FilterKindDefaults:
		return engine.v2_filter_kind_defaults(params, result, error);
	case V2Method::FilterKindProperties:
		return engine.v2_filter_kind_properties(params, result, error);
	case V2Method::FilterList:
		return engine.v2_filter_list(params, result, error);
	case V2Method::FilterGet:
		return engine.v2_filter_get(params, result, error);
	case V2Method::FilterCreate:
		return engine.v2_filter_create(params, result, error);
	case V2Method::FilterRemove:
		return engine.v2_filter_remove(params, result, error);
	case V2Method::FilterRename:
		return engine.v2_filter_rename(params, result, error);
	case V2Method::FilterDuplicate:
		return engine.v2_filter_duplicate(params, result, error);
	case V2Method::FilterGetSettings:
		return engine.v2_filter_get_settings(params, result, error);
	case V2Method::FilterPatchSettings:
		return engine.v2_filter_patch_settings(params, result, error);
	case V2Method::FilterReplaceSettings:
		return engine.v2_filter_replace_settings(params, result, error);
	case V2Method::FilterSetEnabled:
		return engine.v2_filter_set_enabled(params, result, error);
	case V2Method::FilterGetEnabled:
		return engine.v2_filter_get_enabled(params, result, error);
	case V2Method::FilterSetOrder:
		return engine.v2_filter_set_order(params, result, error);
	case V2Method::FilterMoveUp:
		return engine.v2_filter_move_up(params, result, error);
	case V2Method::FilterMoveDown:
		return engine.v2_filter_move_down(params, result, error);
	case V2Method::FilterMoveTop:
		return engine.v2_filter_move_top(params, result, error);
	case V2Method::FilterMoveBottom:
		return engine.v2_filter_move_bottom(params, result, error);
	case V2Method::MediaGetState:
		return engine.v2_media_get_state(params, result, error);
	case V2Method::MediaPlay:
		return engine.v2_media_play(params, result, error);
	case V2Method::MediaPause:
		return engine.v2_media_pause(params, result, error);
	case V2Method::MediaTogglePause:
		return engine.v2_media_toggle_pause(params, result, error);
	case V2Method::MediaStop:
		return engine.v2_media_stop(params, result, error);
	case V2Method::MediaRestart:
		return engine.v2_media_restart(params, result, error);
	case V2Method::MediaNext:
		return engine.v2_media_next(params, result, error);
	case V2Method::MediaPrevious:
		return engine.v2_media_previous(params, result, error);
	case V2Method::MediaGetDuration:
		return engine.v2_media_get_duration(params, result, error);
	case V2Method::MediaGetPosition:
		return engine.v2_media_get_position(params, result, error);
	case V2Method::MediaSetPosition:
		return engine.v2_media_set_position(params, result, error);
	default:
		error.code = "internal_error";
		error.message = "runtime method dispatch failed";
		return false;
	}
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
		if (!read_string_field(entry.get(), "pattern", subscription.pattern, present) || !present ||
		    !is_valid_event_pattern(subscription.pattern)) {
			error = "subscription pattern must be an exact event name or a namespace wildcard such as 'source.*'";
			return false;
		}
		bool telemetry = false;
		if (!read_bool_field(entry.get(), "telemetry", telemetry, present)) {
			error = "subscription telemetry must be a boolean when present";
			return false;
		}
		subscription.telemetry = present && telemetry;
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

} // namespace

bool parse_v2_request(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	out = V2Request{};
	error = V2ParseError{};
	bool present = false;
	if (!read_string_field(request, "id", out.id, present) || !present ||
	    !is_safe_identifier(out.id.c_str(), kMaxV2RequestIdBytes)) {
		set_parse_error(error, "", "bad_request",
				"id must be a 1-128 byte token using only ASCII letters, digits, '_', '-' or '.'");
		return false;
	}
	std::string op;
	if (!read_string_field(request, "op", op, present) || !present || op != "request") {
		set_parse_error(error, out.id, "bad_request", "op must be the string 'request'");
		return false;
	}
	if (!read_string_field(request, "method", out.method, present) || !present ||
	    !is_safe_identifier(out.method.c_str(), kMaxV2MethodBytes)) {
		set_parse_error(error, out.id, "bad_request", "method must be a 1-128 byte protocol identifier");
		return false;
	}
	if (!read_object_field(request, "params", out.params, present)) {
		set_parse_error(error, out.id, "bad_request", "params must be an object when present");
		return false;
	}
	if (!present)
		out.params.reset(obs_data_create());
	if (!read_integer(request, "ifRevision", out.if_revision, out.has_if_revision) ||
	    (out.has_if_revision && out.if_revision < 0)) {
		set_parse_error(error, out.id, "bad_request", "ifRevision must be a non-negative integer");
		return false;
	}
	if (!read_integer(request, "timeoutMs", out.timeout_ms, out.has_timeout_ms) ||
	    (out.has_timeout_ms && out.timeout_ms <= 0)) {
		set_parse_error(error, out.id, "bad_request", "timeoutMs must be a positive integer");
		return false;
	}
	return true;
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

	// Source callbacks run while libobs owns its signal mutex. Establish the
	// request-thread capture gate before taking the revision mutex so a callback
	// can never hold a libobs signal mutex while waiting behind this request.
	if (method_is_runtime(method) && method_is_mutating(method))
		capture.emplace(engine, runtime_result);
	if (method_is_mutating(method))
		mutation_guard.emplace(revisions.lock_mutation());

	// Callbacks that raced with capture establishment are older than this
	// request's revision guard. Give their normalized batches independent
	// revisions before validating ifRevision; callbacks arriving after this
	// snapshot remain deferred until the request finishes.
	if (capture)
		engine.v2_drain_deferred_source_events(*mutation_guard);

	const uint64_t guarded_revision = mutation_guard ? mutation_guard->current() : revisions.current();
	if (!validate_revision_guard(request, method, guarded_revision)) {
		if (capture)
			capture->flush(*mutation_guard);
		return true;
	}
	if (mutation_guard && !mutation_guard->can_commit_mutation()) {
		send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr,
			      mutation_guard->current());
		if (capture)
			capture->flush(*mutation_guard);
		return true;
	}

	if (method == V2Method::SessionHello) {
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
		return true;
	}
	if (method == V2Method::SessionPing) {
		ObsDataPtr data(obs_data_create());
		obs_data_set_bool(data.get(), "pong", true);
		send_v2_ok(request.id, data.get(), revisions.current());
		return true;
	}
	if (method == V2Method::SessionSubscribe) {
		std::vector<EventSubscription> requested;
		std::string error;
		if (!parse_subscription_list(request.params.get(), requested, error) || !events.subscribe(requested, error)) {
			send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
			return true;
		}
		send_subscription_state(request, revisions, events);
		return true;
	}
	if (method == V2Method::SessionUnsubscribe) {
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
	if (method == V2Method::SessionGetSubscriptions) {
		send_subscription_state(request, revisions, events);
		return true;
	}
	if (method == V2Method::SessionClose) {
		const uint64_t revision = mutation_guard->commit_mutation();
		send_v2_ok(request.id, nullptr, revision);
		ObsDataPtr event_data(obs_data_create());
		obs_data_set_string(event_data.get(), "reason", "session.close");
		events.publish(EngineEventKind::State, "engine.stopping", revision, event_data.get());
		return false;
	}
	if (method == V2Method::EngineGetCapabilities) {
		ObsDataPtr data(obs_data_create());
		set_capabilities(data.get());
		send_v2_ok(request.id, data.get(), revisions.current());
		return true;
	}
	if (method_is_runtime(method)) {
		RuntimeV2Result &result = runtime_result;
		RuntimeV2Error error;
		const bool succeeded = execute_runtime_method(engine, method, request.params.get(), result, error);
		if (succeeded && (method == V2Method::SourceKindList || method == V2Method::SourceKindGet))
			engine.v2_normalize_source_kind_metadata(result);
		if (succeeded && capture && method_needs_source_settle(method))
			engine.v2_settle_source_mutation(request.params.get(), result);
		if (succeeded && capture && method_needs_filter_settle(method))
			engine.v2_settle_filter_mutation(request.params.get(), result);

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
		uint64_t revision = mutation_guard ? mutation_guard->current() : revisions.current();
		if (result.mutated) {
			if (!mutation_guard) {
				send_v2_error(request.id, "internal_error",
					      "read-only runtime method unexpectedly mutated engine state", nullptr,
					      revisions.current());
				return true;
			}
			revision = mutation_guard->commit_mutation();
		}
		send_v2_ok(request.id, result.data.get(), revision);
		publish_runtime_events(events, revision, result);
		if (capture)
			capture->flush(*mutation_guard);
		return true;
	}
	send_v2_error(request.id, "internal_error", "method dispatch failed internally", nullptr, revisions.current());
	return true;
}

} // namespace obs_engine
