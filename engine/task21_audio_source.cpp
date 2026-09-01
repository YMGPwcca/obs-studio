#include <obs-module.h>

#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <util/platform.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("task21-audio-source", "en-US")

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFrequencyHz = 440.0f;
constexpr float kAmplitude = 0.35f;

struct Task21AudioSource {
	obs_source_t *source = nullptr;
	uint64_t sample_index = 0;
	std::mutex mutex;
	std::condition_variable cv;
	bool stop = false;
	std::thread thread;
};

const char *source_name(void *)
{
	return "Task 21 Deterministic Audio";
}

void emit_audio(Task21AudioSource *context)
{
	constexpr size_t frames = AUDIO_OUTPUT_FRAMES;
	constexpr auto frame_period = std::chrono::nanoseconds((1000000000LL * frames) / 48000);
	std::vector<float> left(frames);
	std::vector<float> right(frames);
	for (;;) {
		{
			std::unique_lock lock(context->mutex);
			if (context->cv.wait_for(lock, frame_period, [&] { return context->stop; }))
				return;
		}
		for (size_t frame = 0; frame < frames; ++frame) {
			const double sample = static_cast<double>(context->sample_index + frame) * kFrequencyHz / 48000.0;
			left[frame] = kAmplitude * std::sin(static_cast<float>(2.0 * kPi * sample));
			right[frame] = left[frame];
		}
		uint8_t *planes[MAX_AV_PLANES] = {};
		planes[0] = reinterpret_cast<uint8_t *>(left.data());
		planes[1] = reinterpret_cast<uint8_t *>(right.data());
		obs_source_audio audio = {};
		audio.data[0] = planes[0];
		audio.data[1] = planes[1];
		audio.frames = static_cast<uint32_t>(frames);
		audio.timestamp = os_gettime_ns();
		audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
		audio.speakers = SPEAKERS_STEREO;
		audio.samples_per_sec = 48000;
		obs_source_output_audio(context->source, &audio);
		context->sample_index += frames;
	}
}

void *source_create(obs_data_t *, obs_source_t *source)
{
	auto *context = new Task21AudioSource;
	context->source = source;
	return context;
}

void source_activate(void *data)
{
	auto *context = static_cast<Task21AudioSource *>(data);
	if (context && !context->thread.joinable())
		context->thread = std::thread(emit_audio, context);
}

void source_deactivate(void *data)
{
	auto *context = static_cast<Task21AudioSource *>(data);
	if (!context || !context->thread.joinable())
		return;
	{
		std::lock_guard lock(context->mutex);
		context->stop = true;
	}
	context->cv.notify_all();
	context->thread.join();
	context->stop = false;
}

void source_destroy(void *data)
{
	auto *context = static_cast<Task21AudioSource *>(data);
	if (!context)
		return;
	{
		std::lock_guard lock(context->mutex);
		context->stop = true;
	}
	context->cv.notify_all();
	if (context->thread.joinable())
		context->thread.join();
	delete context;
}

obs_source_info info = {
	.id = "task21_audio_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = source_name,
	.create = source_create,
	.destroy = source_destroy,
	.activate = source_activate,
	.deactivate = source_deactivate,
};

obs_source_info restricted_info = {
	.id = "task21_restricted_audio_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_SELF_MONITOR,
	.get_name = source_name,
	.create = source_create,
	.destroy = source_destroy,
	.activate = source_activate,
	.deactivate = source_deactivate,
};

} // namespace

bool obs_module_load(void)
{
	obs_register_source(&info);
	obs_register_source(&restricted_info);
	blog(LOG_INFO, "[task21-audio] deterministic audio source loaded");
	return true;
}
