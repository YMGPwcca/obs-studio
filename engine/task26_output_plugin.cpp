#include <obs-module.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task26-output", "en-US")

namespace {

struct Task26Output {
	obs_output_t *output = nullptr;
	std::mutex workers_mutex;
	std::vector<std::thread> workers;
	std::atomic_bool disconnect_fired = false;
};

const char *output_name(void *)
{
	return "Task 26 Deterministic Output";
}

void output_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "fail_start", false);
	obs_data_set_default_bool(settings, "fail_create", false);
	obs_data_set_default_bool(settings, "async_start", false);
	obs_data_set_default_bool(settings, "async_stop", true);
	obs_data_set_default_bool(settings, "disconnect_once", false);
}

obs_properties_t *output_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_bool(properties, "fail_start", "Reject start");
	obs_properties_add_bool(properties, "fail_create", "Reject create");
	obs_properties_add_bool(properties, "async_start", "Start asynchronously");
	obs_properties_add_bool(properties, "async_stop", "Stop asynchronously");
	obs_properties_add_bool(properties, "disconnect_once", "Disconnect once");
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

std::string failure_message(obs_output_t *output)
{
	std::string message = "task26 deterministic start failure";
	obs_service_t *service = obs_output_get_service(output);
	if (service) {
		const char *secret = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY);
		if (secret && *secret) {
			message += ": ";
			message += secret;
		}
	}
	return message;
}

void add_worker(Task26Output *context, std::function<void()> work)
{
	std::lock_guard lock(context->workers_mutex);
	context->workers.emplace_back(std::move(work));
}

void run_async_start(Task26Output *context)
{
	obs_output_t *output = obs_output_get_ref(context->output);
	add_worker(context, [output] {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		if (obs_output_can_begin_data_capture(output, 0) && obs_output_initialize_encoders(output, 0) &&
		    obs_output_begin_data_capture(output, 0)) {
			obs_output_release(output);
			return;
		}
		obs_output_set_last_error(output, "task26 asynchronous start failed");
		obs_output_signal_stop(output, OBS_OUTPUT_ERROR);
		obs_output_release(output);
	});
}

void run_async_stop(Task26Output *context)
{
	obs_output_t *output = obs_output_get_ref(context->output);
	add_worker(context, [output] {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		obs_output_end_data_capture(output);
		obs_output_release(output);
	});
}

void run_async_disconnect(Task26Output *context)
{
	obs_output_t *output = obs_output_get_ref(context->output);
	add_worker(context, [output] {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		obs_output_signal_stop(output, OBS_OUTPUT_DISCONNECTED);
		obs_output_release(output);
	});
}

void *output_create(obs_data_t *settings, obs_output_t *output)
{
	if (settings && obs_data_get_bool(settings, "fail_create"))
		return nullptr;
	auto *context = new Task26Output;
	context->output = output;
	return context;
}

void output_destroy(void *data)
{
	auto *context = static_cast<Task26Output *>(data);
	if (!context)
		return;
	std::vector<std::thread> workers;
	{
		std::lock_guard lock(context->workers_mutex);
		workers.swap(context->workers);
	}
	for (std::thread &worker : workers)
		if (worker.joinable())
			worker.join();
	delete context;
}

bool output_start(void *data)
{
	auto *context = static_cast<Task26Output *>(data);
	if (!context)
		return false;
	if (setting_bool(context->output, "fail_start")) {
		const std::string message = failure_message(context->output);
		obs_output_set_last_error(context->output, message.c_str());
		return false;
	}
	if (setting_bool(context->output, "async_start")) {
		run_async_start(context);
		return true;
	}
	if (!obs_output_can_begin_data_capture(context->output, 0) ||
	    !obs_output_initialize_encoders(context->output, 0) ||
	    !obs_output_begin_data_capture(context->output, 0))
		return false;
	if (!context->disconnect_fired.load() && setting_bool(context->output, "disconnect_once")) {
		context->disconnect_fired.store(true);
		run_async_disconnect(context);
	}
	return true;
}

void output_stop(void *data, uint64_t)
{
	auto *context = static_cast<Task26Output *>(data);
	if (!context)
		return;
	if (setting_bool(context->output, "async_stop"))
		run_async_stop(context);
	else
		obs_output_end_data_capture(context->output);
}

void output_packet(void *, struct encoder_packet *) {}

obs_output_info info = {
	.id = "task26_test_output",
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE | OBS_OUTPUT_MULTI_TRACK_AV |
		 OBS_OUTPUT_CAN_PAUSE,
	.get_name = output_name,
	.create = output_create,
	.destroy = output_destroy,
	.start = output_start,
	.stop = output_stop,
	.encoded_packet = output_packet,
	.get_defaults = output_defaults,
	.get_properties = output_properties,
	.encoded_video_codecs = "task23-video",
	.encoded_audio_codecs = "task23-audio",
	.protocols = "task25",
};

} // namespace

bool obs_module_load(void)
{
	obs_register_output(&info);
	blog(LOG_INFO, "[task26-output] deterministic output loaded");
	return true;
}
