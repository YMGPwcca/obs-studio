#pragma once

#include "obs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal engine/libobs bridge. This header is intentionally not installed
 * as part of libobs's public headers. The returned serial is not a protocol
 * value and must never cross the engine process boundary.
 */
enum obs_source_media_action_type {
	OBS_SOURCE_MEDIA_ACTION_PLAY_PAUSE,
	OBS_SOURCE_MEDIA_ACTION_RESTART,
	OBS_SOURCE_MEDIA_ACTION_STOP,
	OBS_SOURCE_MEDIA_ACTION_NEXT,
	OBS_SOURCE_MEDIA_ACTION_PREVIOUS,
	OBS_SOURCE_MEDIA_ACTION_SET_TIME,
};

enum obs_source_media_action_enqueue_status {
	OBS_SOURCE_MEDIA_ACTION_ENQUEUED,
	OBS_SOURCE_MEDIA_ACTION_UNSUPPORTED,
	OBS_SOURCE_MEDIA_ACTION_SERIAL_EXHAUSTED,
};

EXPORT enum obs_source_media_action_enqueue_status obs_source_media_action_enqueue(
	obs_source_t *source, enum obs_source_media_action_type type, int64_t value, uint64_t *serial);

#ifdef __cplusplus
}
#endif
