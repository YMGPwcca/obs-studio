#include <obs-module.h>

#include <cstdint>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task9-interaction-source", "en-US")

namespace {

struct Task9InteractionSource {
	obs_source_t *source = nullptr;
};

const char *get_name(void *)
{
	return "Task 9 Interaction Source";
}

void *create(obs_data_t *, obs_source_t *source)
{
	auto *state = new Task9InteractionSource;
	state->source = source;
	return state;
}

void destroy(void *data)
{
	delete static_cast<Task9InteractionSource *>(data);
}

uint32_t get_width(void *)
{
	return 640;
}

uint32_t get_height(void *)
{
	return 360;
}

void video_render(void *, gs_effect_t *) {}

void mouse_click(void *, const obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count)
{
	blog(LOG_INFO,
	     "[task9-interaction] mouseButton x=%d y=%d modifiers=%u button=%d up=%d clickCount=%u",
	     event ? event->x : 0, event ? event->y : 0, event ? event->modifiers : 0, type, mouse_up ? 1 : 0,
	     click_count);
}

void mouse_move(void *, const obs_mouse_event *event, bool mouse_leave)
{
	blog(LOG_INFO, "[task9-interaction] mouseMove x=%d y=%d modifiers=%u leave=%d", event ? event->x : 0,
	     event ? event->y : 0, event ? event->modifiers : 0, mouse_leave ? 1 : 0);
}

void mouse_wheel(void *, const obs_mouse_event *event, int x_delta, int y_delta)
{
	blog(LOG_INFO, "[task9-interaction] mouseWheel x=%d y=%d modifiers=%u deltaX=%d deltaY=%d",
	     event ? event->x : 0, event ? event->y : 0, event ? event->modifiers : 0, x_delta, y_delta);
}

void focus(void *, bool focused)
{
	blog(LOG_INFO, "[task9-interaction] focus focused=%d", focused ? 1 : 0);
}

void key_click(void *, const obs_key_event *event, bool key_up)
{
	blog(LOG_INFO,
	     "[task9-interaction] key up=%d modifiers=%u text=%s nativeModifiers=%u nativeScanCode=%u nativeVirtualKey=%u",
	     key_up ? 1 : 0, event ? event->modifiers : 0, event && event->text ? event->text : "",
	     event ? event->native_modifiers : 0, event ? event->native_scancode : 0, event ? event->native_vkey : 0);
}

obs_source_info info = {
	.id = "task9_interaction_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.get_width = get_width,
	.get_height = get_height,
	.video_render = video_render,
	.mouse_click = mouse_click,
	.mouse_move = mouse_move,
	.mouse_wheel = mouse_wheel,
	.focus = focus,
	.key_click = key_click,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_source(&info);
	return true;
}
