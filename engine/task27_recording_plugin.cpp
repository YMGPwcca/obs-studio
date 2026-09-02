#include <obs-module.h>

#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task27-recording", "en-US")

namespace {

struct Task27Recording {
	obs_output_t *output = nullptr;
};

const char *recording_name(void *)
{
	return "Task 27 Deterministic Recording";
}

void recording_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "path", "");
	obs_data_set_default_bool(settings, "allow_overwrite", false);
	obs_data_set_default_bool(settings, "split_file", true);
}

obs_properties_t *recording_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_text(properties, "path", "Recording path", OBS_TEXT_DEFAULT);
	obs_properties_add_bool(properties, "allow_overwrite", "Allow overwrite");
	obs_properties_add_bool(properties, "split_file", "Enable file splitting");
	return properties;
}

void split_file_proc(void *data, calldata_t *calldata)
{
	auto *context = static_cast<Task27Recording *>(data);
	obs_data_t *settings = obs_output_get_settings(context->output);
	const bool enabled = settings && obs_data_get_bool(settings, "split_file");
	if (settings)
		obs_data_release(settings);
	calldata_set_bool(calldata, "split_file_enabled", enabled);
	if (!enabled || !obs_output_active(context->output))
		return;
	settings = obs_output_get_settings(context->output);
	const char *path = settings ? obs_data_get_string(settings, "path") : nullptr;
	std::string next = path && *path ? path : "task27-recording.mkv";
	if (settings)
		obs_data_release(settings);
	next += ".part2";
	calldata_t file_changed;
	calldata_init(&file_changed);
	calldata_set_string(&file_changed, "next_file", next.c_str());
	signal_handler_signal(obs_output_get_signal_handler(context->output), "file_changed", &file_changed);
	calldata_free(&file_changed);
}

void add_chapter_proc(void *, calldata_t *) {}

void *recording_create(obs_data_t *, obs_output_t *output)
{
	auto *context = new Task27Recording;
	context->output = output;
	signal_handler_add(obs_output_get_signal_handler(output), "void file_changed(string next_file)");
	proc_handler_t *procedures = obs_output_get_proc_handler(output);
	proc_handler_add(procedures, "void split_file(out bool split_file_enabled)", split_file_proc, context);
	proc_handler_add(procedures, "void add_chapter(string chapter_name)", add_chapter_proc, context);
	return context;
}

void recording_destroy(void *data)
{
	delete static_cast<Task27Recording *>(data);
}

bool recording_start(void *data)
{
	auto *context = static_cast<Task27Recording *>(data);
	return context && obs_output_can_begin_data_capture(context->output, 0) &&
	       obs_output_initialize_encoders(context->output, 0) && obs_output_begin_data_capture(context->output, 0);
}

void recording_stop(void *data, uint64_t)
{
	auto *context = static_cast<Task27Recording *>(data);
	if (context)
		obs_output_end_data_capture(context->output);
}

void recording_packet(void *, struct encoder_packet *) {}

obs_output_info info = {
	.id = "task27_test_recording",
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_MULTI_TRACK_AV | OBS_OUTPUT_CAN_PAUSE,
	.get_name = recording_name,
	.create = recording_create,
	.destroy = recording_destroy,
	.start = recording_start,
	.stop = recording_stop,
	.encoded_packet = recording_packet,
	.get_defaults = recording_defaults,
	.get_properties = recording_properties,
	.encoded_video_codecs = "task23-video",
	.encoded_audio_codecs = "task23-audio",
};

} // namespace

bool obs_module_load(void)
{
	obs_register_output(&info);
	blog(LOG_INFO, "[task27-recording] deterministic recording output loaded");
	return true;
}
