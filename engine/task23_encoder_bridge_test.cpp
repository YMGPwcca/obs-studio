#include <obs.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace {

struct DataDeleter {
	void operator()(obs_data_t *data) const
	{
		if (data)
			obs_data_release(data);
	}
};

struct EncoderDeleter {
	void operator()(obs_encoder_t *encoder) const
	{
		if (encoder)
			obs_encoder_release(encoder);
	}
};

struct OutputDeleter {
	void operator()(obs_output_t *output) const
	{
		if (output)
			obs_output_release(output);
	}
};

using DataPtr = std::unique_ptr<obs_data_t, DataDeleter>;
using EncoderPtr = std::unique_ptr<obs_encoder_t, EncoderDeleter>;
using OutputPtr = std::unique_ptr<obs_output_t, OutputDeleter>;

struct ObsRuntimeGuard {
	bool active = true;
	~ObsRuntimeGuard()
	{
		if (active)
			obs_shutdown();
	}
};

struct UpdateResult {
	std::mutex mutex;
	std::condition_variable cv;
	bool done = false;
	bool success = false;
	uint64_t serial = 0;
};

void update_callback(void *data, uint64_t serial, bool success)
{
	auto *result = static_cast<UpdateResult *>(data);
	{
		std::lock_guard lock(result->mutex);
		result->done = true;
		result->success = success;
		result->serial = serial;
	}
	result->cv.notify_all();
}

bool wait_for(const std::function<bool()> &predicate, std::chrono::milliseconds timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!predicate()) {
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		os_sleep_ms(10);
	}
	return true;
}

bool wait_for_update(UpdateResult &result)
{
	std::unique_lock lock(result.mutex);
	return result.cv.wait_for(lock, std::chrono::seconds(5), [&] { return result.done; });
}

bool reset_video()
{
	obs_video_info video = {};
	video.graphics_module = DL_D3D11;
	video.fps_num = 60;
	video.fps_den = 1;
	video.base_width = 320;
	video.base_height = 180;
	video.output_width = 320;
	video.output_height = 180;
	video.output_format = VIDEO_FORMAT_BGRA;
	video.colorspace = VIDEO_CS_SRGB;
	video.range = VIDEO_RANGE_FULL;
	video.scale_type = OBS_SCALE_BILINEAR;
	return obs_reset_video(&video) == OBS_VIDEO_SUCCESS;
}

bool reset_audio()
{
	obs_audio_info audio = {};
	audio.samples_per_sec = 48000;
	audio.speakers = SPEAKERS_STEREO;
	return obs_reset_audio(&audio);
}

bool start_runtime()
{
	char *config_path = os_get_config_path_ptr("obs-engine/task23-bridge-test");
	if (!config_path || os_mkdirs(config_path) == MKDIR_ERROR) {
		bfree(config_path);
		return false;
	}
	const bool started = obs_startup("en-US", config_path, nullptr);
	bfree(config_path);
	if (!started || !reset_video() || !reset_audio()) {
		if (started)
			obs_shutdown();
		return false;
	}
	return true;
}

bool load_test_modules()
{
	obs_add_safe_module("obs-outputs");
	obs_add_safe_module("task23-encoder");
	obs_module_failure_info failures = {};
	obs_load_all_modules2(&failures);
	obs_module_failure_info_free(&failures);
	obs_post_load_modules();
	return obs_get_module("obs-outputs") && obs_get_module("task23-encoder");
}

bool create_test_pipeline(EncoderPtr &encoder, EncoderPtr &audio, OutputPtr &output)
{
	DataPtr settings(obs_data_create());
	encoder.reset(obs_video_encoder_create("task23_test_video", "task23-bridge", settings.get(), nullptr));
	if (!encoder)
		return false;
	obs_encoder_set_video(encoder.get(), obs_get_video());
	DataPtr audio_settings(obs_data_create());
	audio.reset(obs_audio_encoder_create("task23_test_audio", "task23-bridge-audio", audio_settings.get(), 0,
							 nullptr));
	if (!audio)
		return false;
	obs_encoder_set_audio(audio.get(), obs_get_audio());
	output.reset(obs_output_create("null_output", "task23-null", nullptr, nullptr));
	if (!output)
		return false;
	obs_output_set_video_encoder(output.get(), encoder.get());
	obs_output_set_audio_encoder(output.get(), audio.get(), 0);
	return true;
}

bool run_active_update_checks(obs_encoder_t *encoder)
{
	DataPtr applied(obs_data_create());
	obs_data_set_int(applied.get(), "bitrate", 2200);
	UpdateResult success;
	if (!obs_encoder_update_tracked(encoder, applied.get(), false, 1, update_callback, &success) ||
	    !wait_for_update(success) || !success.success || success.serial != 1)
		return false;
	DataPtr rejected(obs_data_create());
	obs_data_set_bool(rejected.get(), "reject_update", true);
	UpdateResult failure;
	if (!obs_encoder_update_tracked(encoder, rejected.get(), false, 2, update_callback, &failure) ||
	    !wait_for_update(failure) || failure.success || failure.serial != 2)
		return false;
	return true;
}

bool stop_test_output(obs_output_t *output)
{
	obs_output_stop(output);
	return wait_for([&] { return !obs_output_active(output); }, std::chrono::seconds(5));
}

} // namespace

int main()
{
	if (!start_runtime()) {
		std::fprintf(stderr, "Task 23 bridge: libobs startup failed\n");
		return 1;
	}
	ObsRuntimeGuard runtime;
	if (!load_test_modules()) {
		std::fprintf(stderr, "Task 23 bridge: required test modules did not load\n");
		return 1;
	}
	EncoderPtr encoder;
	EncoderPtr audio;
	OutputPtr output;
	if (!create_test_pipeline(encoder, audio, output)) {
		std::fprintf(stderr, "Task 23 bridge: test pipeline creation failed\n");
		return 1;
	}
	const bool output_started = obs_output_start(output.get());
	if (!output_started ||
	    !wait_for([&] { return obs_encoder_initialized(encoder.get()); }, std::chrono::seconds(5))) {
		std::fprintf(stderr, "Task 23 bridge: output did not initialize the encoder (started=%d, error=%s, flags=%u)\n",
			     output_started, obs_output_get_last_error(output.get()) ? obs_output_get_last_error(output.get()) : "",
			     obs_output_get_flags(output.get()));
		obs_output_stop(output.get());
		return 1;
	}
	if (!run_active_update_checks(encoder.get())) {
		std::fprintf(stderr, "Task 23 bridge: active update did not settle successfully\n");
		obs_output_stop(output.get());
		return 1;
	}
	if (!stop_test_output(output.get())) {
		std::fprintf(stderr, "Task 23 bridge: null output did not stop\n");
		return 1;
	}
	output.reset();
	audio.reset();
	encoder.reset();
	runtime.active = false;
	obs_shutdown();
	std::printf("Task 23 encoder bridge: PASS\n");
	return 0;
}
