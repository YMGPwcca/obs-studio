#include <obs-module.h>

#include <cstddef>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task23-encoder", "en-US")

namespace {

struct Task23Encoder {
	obs_encoder_t *encoder = nullptr;
};

const char *video_name(void *)
{
	return "Task 23 Deterministic Video Encoder";
}

const char *audio_name(void *)
{
	return "Task 23 Deterministic Audio Encoder";
}

void set_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "bitrate", 1200);
	obs_data_set_default_bool(settings, "reject_update", false);
}

obs_properties_t *properties(void *)
{
	obs_properties_t *result = obs_properties_create();
	obs_properties_add_int(result, "bitrate", "Bitrate", 1, 100000, 1);
	obs_properties_add_bool(result, "reject_update", "Reject update");
	return result;
}

void *create(obs_data_t *, obs_encoder_t *encoder)
{
	auto *state = new Task23Encoder;
	state->encoder = encoder;
	return state;
}

void destroy(void *data)
{
	delete static_cast<Task23Encoder *>(data);
}

bool update(void *data, obs_data_t *settings)
{
	if (obs_data_get_bool(settings, "reject_update"))
		return false;
	return data != nullptr;
}

bool encode(void *, struct encoder_frame *, struct encoder_packet *, bool *received_packet)
{
	if (received_packet)
		*received_packet = false;
	return true;
}

size_t audio_frame_size(void *)
{
	return 1024;
}

obs_encoder_info video_info = {
	.id = "task23_test_video",
	.type = OBS_ENCODER_VIDEO,
	.codec = "task23-video",
	.get_name = video_name,
	.create = create,
	.destroy = destroy,
	.encode = encode,
	.get_defaults = set_defaults,
	.get_properties = properties,
	.update = update,
	.caps = OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI | OBS_ENCODER_CAP_SCALING,
};

obs_encoder_info audio_info = {
	.id = "task23_test_audio",
	.type = OBS_ENCODER_AUDIO,
	.codec = "task23-audio",
	.get_name = audio_name,
	.create = create,
	.destroy = destroy,
	.encode = encode,
	.get_frame_size = audio_frame_size,
	.get_defaults = set_defaults,
	.get_properties = properties,
	.update = update,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_encoder(&video_info);
	obs_register_encoder(&audio_info);
	blog(LOG_INFO, "[task23-encoder] deterministic video/audio encoders loaded");
	return true;
}
