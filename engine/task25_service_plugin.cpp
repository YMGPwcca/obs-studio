#include <obs-module.h>

#include <util/bmem.h>

#include <cstring>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task25-service", "en-US")

namespace {

struct Task25Service {
	obs_data_t *settings = nullptr;
};

const char *service_name(void *)
{
	return "Task 25 Deterministic Service";
}

void service_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "server", "rtmp://127.0.0.1/task25");
	obs_data_set_default_string(settings, "label", "deterministic");
}

obs_properties_t *service_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_text(properties, "server", "Server", OBS_TEXT_DEFAULT);
	obs_properties_add_text(properties, "key", "Stream key", OBS_TEXT_PASSWORD);
	obs_properties_add_text(properties, "username", "Username", OBS_TEXT_DEFAULT);
	obs_properties_add_text(properties, "password", "Password", OBS_TEXT_PASSWORD);
	obs_properties_add_text(properties, "passphrase", "Encryption passphrase", OBS_TEXT_PASSWORD);
	obs_properties_add_text(properties, "bearer_token", "Bearer token", OBS_TEXT_PASSWORD);
	obs_properties_add_text(properties, "label", "Label", OBS_TEXT_DEFAULT);
	return properties;
}

void *service_create(obs_data_t *settings, obs_service_t *)
{
	auto *service = new Task25Service;
	service->settings = settings;
	obs_data_addref(service->settings);
	return service;
}

void service_destroy(void *data)
{
	auto *service = static_cast<Task25Service *>(data);
	if (!service)
		return;
	obs_data_release(service->settings);
	delete service;
}

void service_update(void *, obs_data_t *) {}

const char *service_protocol(void *)
{
	return "task25";
}

const char *service_output_type(void *)
{
	return "null_output";
}

const char *service_connect_info(void *data, uint32_t type)
{
	auto *service = static_cast<Task25Service *>(data);
	if (!service || !service->settings)
		return nullptr;
	switch (type) {
	case OBS_SERVICE_CONNECT_INFO_SERVER_URL:
		return obs_data_get_string(service->settings, "server");
	case OBS_SERVICE_CONNECT_INFO_STREAM_KEY:
		return obs_data_get_string(service->settings, "key");
	case OBS_SERVICE_CONNECT_INFO_USERNAME:
		return obs_data_get_string(service->settings, "username");
	case OBS_SERVICE_CONNECT_INFO_PASSWORD:
		return obs_data_get_string(service->settings, "password");
	case OBS_SERVICE_CONNECT_INFO_ENCRYPT_PASSPHRASE:
		return obs_data_get_string(service->settings, "passphrase");
	case OBS_SERVICE_CONNECT_INFO_BEARER_TOKEN:
		return obs_data_get_string(service->settings, "bearer_token");
	default:
		return nullptr;
	}
}

void service_resolutions(void *, obs_service_resolution **resolutions, size_t *count)
{
	if (!resolutions || !count)
		return;
	*count = 2;
	*resolutions = static_cast<obs_service_resolution *>(bmalloc(*count * sizeof(**resolutions)));
	(*resolutions)[0] = {1920, 1080};
	(*resolutions)[1] = {1280, 720};
}

void service_max_fps(void *, int *fps)
{
	if (fps)
		*fps = 60;
}

void service_max_bitrate(void *, int *video, int *audio)
{
	if (video)
		*video = 6000;
	if (audio)
		*audio = 320;
}

const char **service_video_codecs(void *)
{
	static const char *codecs[] = {"h264", "av1", nullptr};
	return codecs;
}

const char **service_audio_codecs(void *)
{
	static const char *codecs[] = {"aac", nullptr};
	return codecs;
}

void service_apply_encoder_settings(void *, obs_data_t *video, obs_data_t *audio)
{
	if (video)
		obs_data_set_int(video, "recommended_bitrate", 2400);
	if (audio)
		obs_data_set_int(audio, "recommended_bitrate", 128);
}

obs_service_info info = {
	.id = "task25_test_service",
	.get_name = service_name,
	.create = service_create,
	.destroy = service_destroy,
	.update = service_update,
	.get_defaults = service_defaults,
	.get_properties = service_properties,
	.apply_encoder_settings = service_apply_encoder_settings,
	.get_output_type = service_output_type,
	.get_supported_resolutions = service_resolutions,
	.get_max_fps = service_max_fps,
	.get_max_bitrate = service_max_bitrate,
	.get_supported_video_codecs = service_video_codecs,
	.get_protocol = service_protocol,
	.get_supported_audio_codecs = service_audio_codecs,
	.get_connect_info = service_connect_info,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_service(&info);
	blog(LOG_INFO, "[task25-service] deterministic secret-aware service loaded");
	return true;
}
