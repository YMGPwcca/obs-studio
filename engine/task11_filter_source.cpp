#include <obs-module.h>
#include <callback/signal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task11-filter-source", "en-US")

namespace {

struct Task11Parent {
	obs_source_t *source = nullptr;
};

struct Task11Filter {
	obs_source_t *source = nullptr;
	std::mutex mutex;
	std::condition_variable cv;
	int64_t value = 0;
	uint64_t updates = 0;
	bool trigger_used = false;
};

std::mutex g_filters_mutex;
std::vector<Task11Filter *> g_filters;

Task11Filter *find_peer(Task11Filter *self)
{
	std::lock_guard lock(g_filters_mutex);
	for (Task11Filter *candidate : g_filters) {
		if (candidate != self)
			return candidate;
	}
	return nullptr;
}

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
	// Defaults must remain defaults so caller-provided creation settings win
	// when libobs applies this callback after initializing the context object.
	// The create callback reads the resulting settings directly; it must not
	// recursively call obs_source_update before context.data is installed.
	obs_data_set_default_int(settings, "value", 0);
	obs_data_set_default_int(settings, "blockMs", 0);
	obs_data_set_default_bool(settings, "triggerOther", false);
	obs_data_set_default_bool(settings, "burst", false);
}

obs_properties_t *filter_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_int(properties, "value", "Value", 0, 10000, 1);
	obs_properties_add_int(properties, "blockMs", "Block update (ms)", 0, 20000, 1);
	obs_properties_add_bool(properties, "triggerOther", "Trigger peer update");
	obs_properties_add_bool(properties, "burst", "Trigger deferred queue overflow");
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
	{
		std::lock_guard lock(g_filters_mutex);
		g_filters.push_back(filter);
	}
	return filter;
}

void filter_destroy(void *data)
{
	Task11Filter *filter = static_cast<Task11Filter *>(data);
	{
		std::lock_guard lock(g_filters_mutex);
		g_filters.erase(std::remove(g_filters.begin(), g_filters.end(), filter), g_filters.end());
	}
	delete filter;
}

void filter_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<Task11Filter *>(data);
	if (!filter)
		return;
	const bool trigger_other = obs_data_get_bool(settings, "triggerOther");
	const bool burst = obs_data_get_bool(settings, "burst");
	const uint64_t block_ms = static_cast<uint64_t>(std::max<long long>(0, obs_data_get_int(settings, "blockMs")));
	{
		std::lock_guard lock(filter->mutex);
		filter->value = obs_data_get_int(settings, "value");
		++filter->updates;
		blog(LOG_INFO, "[task11-filter] update value=%lld count=%llu", static_cast<long long>(filter->value),
		     static_cast<unsigned long long>(filter->updates));
	}

	if (trigger_other) {
		bool use_trigger = false;
		{
			std::lock_guard lock(filter->mutex);
			use_trigger = !filter->trigger_used;
			filter->trigger_used = true;
		}
		if (use_trigger) {
			if (Task11Filter *peer = find_peer(filter)) {
				obs_data_t *peer_settings = obs_data_create();
				obs_data_set_int(peer_settings, "value", 777);
				obs_source_update(peer->source, peer_settings);
				obs_data_release(peer_settings);
			}
		}
	}

	if (burst) {
		if (Task11Filter *peer = find_peer(filter)) {
			for (int64_t value = 1; value <= 1100; ++value) {
				obs_data_t *peer_settings = obs_source_get_settings(peer->source);
				if (peer_settings) {
					obs_data_set_int(peer_settings, "value", value);
					obs_data_release(peer_settings);
				}
				signal_handler_signal(obs_source_get_signal_handler(peer->source), "update", nullptr);
			}
		}
	}

	if (block_ms != 0) {
		std::unique_lock lock(filter->mutex);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(block_ms);
		filter->cv.wait_until(lock, deadline, [] { return false; });
	}
}

void filter_add(void *, obs_source_t *) {}
void filter_remove(void *, obs_source_t *) {}

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
	.filter_remove = filter_remove,
	.filter_add = filter_add,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_source(&parent_info);
	obs_register_source(&filter_info);
	blog(LOG_INFO, "[task11-filter] deterministic parent/filter module loaded");
	return true;
}
