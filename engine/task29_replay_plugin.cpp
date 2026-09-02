#include <obs-module.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task29-replay", "en-US")

namespace {

struct Task29Replay {
	obs_output_t *output = nullptr;
	std::mutex mutex;
	std::string last_file;
	std::vector<std::thread> workers;
};

const char *replay_name(void *)
{
	return "Task 29 Deterministic Replay Buffer";
}

void replay_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "async_save", true);
	obs_data_set_default_bool(settings, "async_stop", true);
	obs_data_set_default_bool(settings, "fail_save", false);
	obs_data_set_default_int(settings, "save_delay_ms", 60);
	obs_data_set_default_string(settings, "path", "C:/task29-replay.mp4");
}

obs_properties_t *replay_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_bool(properties, "async_save", "Save asynchronously");
	obs_properties_add_bool(properties, "async_stop", "Stop asynchronously");
	obs_properties_add_bool(properties, "fail_save", "Reject save");
	obs_properties_add_int(properties, "save_delay_ms", "Save delay", 0, 5000, 1);
	obs_properties_add_text(properties, "path", "Replay path", OBS_TEXT_DEFAULT);
	return properties;
}

bool setting_bool(obs_output_t *output, const char *name)
{
	obs_data_t *settings = obs_output_get_settings(output);
	if (!settings)
		return false;
	const bool value = obs_data_get_bool(settings, name);
	obs_data_release(settings);
	return value;
}

int setting_int(obs_output_t *output, const char *name)
{
	obs_data_t *settings = obs_output_get_settings(output);
	if (!settings)
		return 0;
	const int value = static_cast<int>(obs_data_get_int(settings, name));
	obs_data_release(settings);
	return value;
}

std::string setting_string(obs_output_t *output, const char *name)
{
	obs_data_t *settings = obs_output_get_settings(output);
	if (!settings)
		return {};
	const char *value = obs_data_get_string(settings, name);
	const std::string result = value ? value : "";
	obs_data_release(settings);
	return result;
}

void add_worker(Task29Replay *context, std::thread worker)
{
	std::lock_guard lock(context->mutex);
	context->workers.emplace_back(std::move(worker));
}

void save_worker(Task29Replay *context, obs_output_t *output, bool write_path)
{
	const int delay = setting_int(output, "save_delay_ms");
	if (delay > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
	{
		std::lock_guard lock(context->mutex);
		context->last_file = write_path ? setting_string(output, "path") : "";
		if (write_path && context->last_file.empty())
			context->last_file = "C:/task29-replay.mp4";
	}
	calldata_t calldata;
	calldata_init(&calldata);
	signal_handler_signal(obs_output_get_signal_handler(output), "saved", &calldata);
	calldata_free(&calldata);
	obs_output_release(output);
}

void stop_worker(obs_output_t *output)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(40));
	obs_output_end_data_capture(output);
	obs_output_release(output);
}

void save_proc(void *data, calldata_t *)
{
	auto *context = static_cast<Task29Replay *>(data);
	if (!context || !obs_output_active(context->output))
		return;
	const bool write_path = !setting_bool(context->output, "fail_save");
	if (!write_path)
		obs_output_set_last_error(context->output, "task29 deterministic save failure");
	obs_output_t *output = obs_output_get_ref(context->output);
	add_worker(context, std::thread(save_worker, context, output, write_path));
}

void get_last_replay_proc(void *data, calldata_t *calldata)
{
	auto *context = static_cast<Task29Replay *>(data);
	if (!context || !calldata)
		return;
	std::lock_guard lock(context->mutex);
	if (!context->last_file.empty())
		calldata_set_string(calldata, "path", context->last_file.c_str());
}

void *replay_create(obs_data_t *, obs_output_t *output)
{
	auto *context = new Task29Replay;
	context->output = output;
	proc_handler_t *procedures = obs_output_get_proc_handler(output);
	proc_handler_add(procedures, "void save()", save_proc, context);
	proc_handler_add(procedures, "void get_last_replay(out string path)", get_last_replay_proc, context);
	signal_handler_add(obs_output_get_signal_handler(output), "void saved()");
	return context;
}

void replay_destroy(void *data)
{
	auto *context = static_cast<Task29Replay *>(data);
	if (!context)
		return;
	std::vector<std::thread> workers;
	{
		std::lock_guard lock(context->mutex);
		workers.swap(context->workers);
	}
	for (std::thread &worker : workers)
		if (worker.joinable())
			worker.join();
	delete context;
}

bool replay_start(void *data)
{
	auto *context = static_cast<Task29Replay *>(data);
	return context && obs_output_can_begin_data_capture(context->output, 0) &&
	       obs_output_initialize_encoders(context->output, 0) &&
	       obs_output_begin_data_capture(context->output, 0);
}

void replay_stop(void *data, uint64_t)
{
	auto *context = static_cast<Task29Replay *>(data);
	if (!context)
		return;
	if (setting_bool(context->output, "async_stop")) {
		obs_output_t *output = obs_output_get_ref(context->output);
		add_worker(context, std::thread(stop_worker, output));
	} else {
		obs_output_end_data_capture(context->output);
	}
}

void replay_packet(void *, struct encoder_packet *) {}

obs_output_info info = {
	.id = "task29_test_replay",
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_MULTI_TRACK_AV | OBS_OUTPUT_CAN_PAUSE,
	.get_name = replay_name,
	.create = replay_create,
	.destroy = replay_destroy,
	.start = replay_start,
	.stop = replay_stop,
	.encoded_packet = replay_packet,
	.get_defaults = replay_defaults,
	.get_properties = replay_properties,
	.encoded_video_codecs = "task23-video",
	.encoded_audio_codecs = "task23-audio",
};

} // namespace

bool obs_module_load(void)
{
	obs_register_output(&info);
	blog(LOG_INFO, "[task29-replay] deterministic replay output loaded");
	return true;
}
