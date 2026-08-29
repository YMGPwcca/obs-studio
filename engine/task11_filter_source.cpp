#include <obs-module.h>

#include <cstdint>
#include <mutex>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task11-filter-source", "en-US")

namespace {

struct Task11Parent {
	obs_source_t *source = nullptr;
};

struct Task11Filter {
	obs_source_t *source = nullptr;
	std::mutex mutex;
	int64_t value = 0;
	uint64_t updates = 0;
};

const char *parent_name(void *)
{
	return "Task 11 Filter Parent";
}

void *parent_create(obs_data_t *, obs_source_t *source)
{
	auto *parent = new Task11Parent;
	parent->source = source;
	return parent;
}

void parent_destroy(void *data)
{
	delete static_cast<Task11Parent *>(data);
}

uint32_t parent_width(void *)
{
	return 640;
}

uint32_t parent_height(void *)
{
	return 360;
}

void parent_render(void *, gs_effect_t *) {}

const char *filter_name(void *)
{
	return "Task 11 Deterministic Filter";
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "value", 0);
}

obs_properties_t *filter_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_int(properties, "value", "Value", 0, 100, 1);
	return properties;
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new Task11Filter;
	filter->source = source;
	// Creation receives the initial settings directly. Only later protocol
	// updates go through obs_source_update(), which is the deferred video-filter
	// path Task 11 needs to settle. Calling obs_source_update() recursively from
	// create would run before libobs has installed context.data for this source.
	filter->value = obs_data_get_int(settings, "value");
	return filter;
}

void filter_destroy(void *data)
{
	delete static_cast<Task11Filter *>(data);
}

void filter_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<Task11Filter *>(data);
	if (!filter)
		return;
	std::lock_guard lock(filter->mutex);
	filter->value = obs_data_get_int(settings, "value");
	++filter->updates;
	blog(LOG_INFO, "[task11-filter] update value=%lld count=%llu", static_cast<long long>(filter->value),
	     static_cast<unsigned long long>(filter->updates));
}

struct obs_source_info parent_info = {
	.id = "task11_filter_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = parent_name,
	.create = parent_create,
	.destroy = parent_destroy,
	.get_width = parent_width,
	.get_height = parent_height,
	.video_render = parent_render,
};

struct obs_source_info filter_info = {
	.id = "task11_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.get_defaults = filter_defaults,
	.get_properties = filter_properties,
	.update = filter_update,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_source(&parent_info);
	obs_register_source(&filter_info);
	blog(LOG_INFO, "[task11-filter] deterministic parent/filter module loaded");
	return true;
}
