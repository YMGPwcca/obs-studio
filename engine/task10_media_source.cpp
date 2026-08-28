#include <obs-module.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task10-media-source", "en-US")

namespace {

constexpr int64_t kDurationMs = 10000;

struct Task10MediaSource {
	obs_source_t *source = nullptr;
	std::mutex mutex;
	obs_media_state state = OBS_MEDIA_STATE_STOPPED;
	int64_t position_ms = 0;
	int64_t index = 0;
	std::string label;
	std::string scenario;
	std::string peer_label;
	bool peer_triggered = false;
};

std::mutex g_sources_mutex;
std::unordered_map<std::string, Task10MediaSource *> g_sources;

void trigger_peer_started(const std::string &label)
{
	if (label.empty())
		return;

	obs_source_t *peer_source = nullptr;
	Task10MediaSource *peer = nullptr;
	{
		std::lock_guard lock(g_sources_mutex);
		const auto it = g_sources.find(label);
		if (it == g_sources.end() || !it->second || !it->second->source)
			return;
		peer = it->second;
		peer_source = obs_source_get_ref(peer->source);
	}

	{
		std::lock_guard lock(peer->mutex);
		peer->state = OBS_MEDIA_STATE_PLAYING;
	}
	obs_source_media_started(peer_source);
	obs_source_release(peer_source);
}

const char *get_name(void *)
{
	return "Task 10 Media Source";
}

void source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "label", "");
	obs_data_set_default_string(settings, "scenario", "");
	obs_data_set_default_string(settings, "peerLabel", "");
}

void *source_create(obs_data_t *settings, obs_source_t *source)
{
	auto *context = new Task10MediaSource;
	context->source = source;
	const char *label = obs_data_get_string(settings, "label");
	context->label = label && *label ? label : obs_source_get_name(source);
	const char *scenario = obs_data_get_string(settings, "scenario");
	context->scenario = scenario ? scenario : "";
	const char *peer_label = obs_data_get_string(settings, "peerLabel");
	context->peer_label = peer_label ? peer_label : "";

	{
		std::lock_guard lock(g_sources_mutex);
		g_sources[context->label] = context;
	}
	return context;
}

void source_destroy(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	if (!context)
		return;

	{
		std::lock_guard lock(g_sources_mutex);
		const auto it = g_sources.find(context->label);
		if (it != g_sources.end() && it->second == context)
			g_sources.erase(it);
	}
	delete context;
}

void source_update(void *data, obs_data_t *settings)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	if (!context)
		return;

	std::string scenario;
	std::string peer_label;
	const char *scenario_text = obs_data_get_string(settings, "scenario");
	const char *peer_label_text = obs_data_get_string(settings, "peerLabel");
	scenario = scenario_text ? scenario_text : "";
	peer_label = peer_label_text ? peer_label_text : "";

	bool changed = false;
	{
		std::lock_guard lock(context->mutex);
		changed = scenario != context->scenario;
		context->scenario = scenario;
		context->peer_label = peer_label;
	}
	if (!changed)
		return;

	if (scenario == "ended") {
		{
			std::lock_guard lock(context->mutex);
			context->state = OBS_MEDIA_STATE_ENDED;
		}
		obs_source_media_ended(context->source);
	} else if (scenario == "error") {
		{
			std::lock_guard lock(context->mutex);
			context->state = OBS_MEDIA_STATE_ERROR;
		}
		// The current libobs contract has no generic media_error signal. Reuse
		// the real lifecycle signal so the engine can observe the ERROR state.
		obs_source_media_ended(context->source);
	} else if (scenario == "reset") {
		std::lock_guard lock(context->mutex);
		context->state = OBS_MEDIA_STATE_STOPPED;
		context->position_ms = 0;
	} else if (scenario == "overflow") {
		{
			std::lock_guard lock(context->mutex);
			context->state = OBS_MEDIA_STATE_ENDED;
		}
		for (int i = 0; i < 1030; ++i)
			obs_source_media_ended(context->source);
	}
}

uint32_t source_width(void *)
{
	return 640;
}

uint32_t source_height(void *)
{
	return 360;
}

void video_render(void *, gs_effect_t *) {}

void video_tick(void *, float) {}

void media_play_pause(void *data, bool pause)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::string peer_label;
	std::string scenario;
	{
		std::lock_guard lock(context->mutex);
		context->state = pause ? OBS_MEDIA_STATE_PAUSED : OBS_MEDIA_STATE_PLAYING;
		peer_label = context->peer_label;
		scenario = context->scenario;
	}

	bool trigger_peer = false;
	{
		std::lock_guard lock(context->mutex);
		trigger_peer = !pause && scenario == "peer" && !context->peer_triggered;
		if (trigger_peer)
			context->peer_triggered = true;
	}
	if (trigger_peer)
		trigger_peer_started(peer_label);
	if (!pause && scenario == "error") {
		{
			std::lock_guard lock(context->mutex);
			context->state = OBS_MEDIA_STATE_ERROR;
		}
		obs_source_media_ended(context->source);
	}
}

void media_restart(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::lock_guard lock(context->mutex);
	context->position_ms = 0;
	context->state = OBS_MEDIA_STATE_PLAYING;
}

void media_stop(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::lock_guard lock(context->mutex);
	context->position_ms = 0;
	context->state = OBS_MEDIA_STATE_STOPPED;
}

void media_next(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::string scenario;
	{
		std::lock_guard lock(context->mutex);
		++context->index;
		context->position_ms = 0;
		context->state = OBS_MEDIA_STATE_PLAYING;
		scenario = context->scenario;
	}
	if (scenario == "ended") {
		{
			std::lock_guard lock(context->mutex);
			context->state = OBS_MEDIA_STATE_ENDED;
		}
		obs_source_media_ended(context->source);
	}
}

void media_previous(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::lock_guard lock(context->mutex);
	--context->index;
	context->position_ms = 0;
	context->state = OBS_MEDIA_STATE_PLAYING;
}

int64_t media_get_duration(void *)
{
	return kDurationMs;
}

int64_t media_get_time(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::lock_guard lock(context->mutex);
	return context->position_ms;
}

void media_set_time(void *data, int64_t milliseconds)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::lock_guard lock(context->mutex);
	context->position_ms = milliseconds;
}

enum obs_media_state media_get_state(void *data)
{
	auto *context = static_cast<Task10MediaSource *>(data);
	std::lock_guard lock(context->mutex);
	return context->state;
}

obs_source_info info = {
	.id = "task10_media_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CONTROLLABLE_MEDIA,
	.get_name = get_name,
	.create = source_create,
	.destroy = source_destroy,
	.get_width = source_width,
	.get_height = source_height,
	.get_defaults = source_defaults,
	.update = source_update,
	.video_tick = video_tick,
	.video_render = video_render,
	.media_play_pause = media_play_pause,
	.media_restart = media_restart,
	.media_stop = media_stop,
	.media_next = media_next,
	.media_previous = media_previous,
	.media_get_duration = media_get_duration,
	.media_get_time = media_get_time,
	.media_set_time = media_set_time,
	.media_get_state = media_get_state,
};

obs_source_info no_seek_info = {};

} // namespace

bool obs_module_load(void)
{
	obs_register_source(&info);
	no_seek_info = info;
	no_seek_info.id = "task10_media_no_seek";
	no_seek_info.media_set_time = nullptr;
	obs_register_source(&no_seek_info);
	blog(LOG_INFO, "[task10-media] deterministic media source loaded");
	return true;
}
