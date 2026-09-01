#include <obs-module.h>
#include <obs-hotkey.h>

#include <atomic>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task22-hotkey-source", "en-US")

namespace {

std::atomic<uint64_t> frontend_triggers{0};
std::atomic<uint64_t> source_triggers{0};
std::atomic<uint64_t> pair_triggers{0};
obs_source_t *public_source = nullptr;
obs_hotkey_id frontend_hotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id source_hotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_pair_id pair_hotkey = OBS_INVALID_HOTKEY_PAIR_ID;

const char *source_name(void *)
{
	return "Task 22 Hotkey Source";
}

void *source_create(obs_data_t *, obs_source_t *)
{
	return new int(0);
}

void source_destroy(void *data)
{
	delete static_cast<int *>(data);
}

void frontend_callback(void *, obs_hotkey_id, obs_hotkey_t *, bool)
{
	frontend_triggers.fetch_add(1, std::memory_order_relaxed);
}

void source_callback(void *, obs_hotkey_id, obs_hotkey_t *, bool)
{
	source_triggers.fetch_add(1, std::memory_order_relaxed);
}

bool pair_callback(void *, obs_hotkey_pair_id, obs_hotkey_t *, bool)
{
	pair_triggers.fetch_add(1, std::memory_order_relaxed);
	return true;
}

obs_source_info source_info = {
	.id = "task22_hotkey_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = 0,
	.get_name = source_name,
	.create = source_create,
	.destroy = source_destroy,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_source(&source_info);
	public_source = obs_source_create(source_info.id, "task22-public-source", nullptr, nullptr);
	if (!public_source)
		return false;
	frontend_hotkey = obs_hotkey_register_frontend("task22.frontend", "Task 22 Frontend", frontend_callback, nullptr);
	source_hotkey = obs_hotkey_register_source(public_source, "task22.source", "Task 22 Source", source_callback, nullptr);
	pair_hotkey = obs_hotkey_pair_register_frontend("task22.pair.start", "Task 22 Pair Start",
										   "task22.pair.stop", "Task 22 Pair Stop", pair_callback,
										   pair_callback, nullptr, nullptr);
	blog(LOG_INFO, "[task22-hotkey] deterministic hotkey registerers loaded");
	return frontend_hotkey != OBS_INVALID_HOTKEY_ID && source_hotkey != OBS_INVALID_HOTKEY_ID &&
	       pair_hotkey != OBS_INVALID_HOTKEY_PAIR_ID;
}

void obs_module_unload(void)
{
	if (pair_hotkey != OBS_INVALID_HOTKEY_PAIR_ID)
		obs_hotkey_pair_unregister(pair_hotkey);
	if (frontend_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(frontend_hotkey);
	if (source_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(source_hotkey);
	if (public_source) {
		obs_source_release(public_source);
		public_source = nullptr;
	}
}
