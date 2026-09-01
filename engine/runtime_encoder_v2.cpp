#include "runtime.hpp"

#include "events.hpp"
#include "properties.hpp"
#include "runtime_phase2_common.hpp"
#include "validation.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obs_engine {

struct EncoderMutation {
	std::mutex mutex;
	std::condition_variable cv;
	bool done = false;
	bool success = false;
	uint64_t callback_serial = 0;
};

struct EncoderV2State {
	std::mutex mutex;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	bool accepting = false;
	uint64_t next_serial = 1;
	std::unordered_map<obs_encoder_t *, std::shared_ptr<EncoderMutation>> pending;
};

namespace {

constexpr size_t kMaxEncoderKindBytes = 128;
constexpr size_t kMaxEncoderNameBytes = 256;
constexpr uint32_t kMaxEncoderDimension = 16384;
constexpr std::chrono::milliseconds kEncoderUpdateTimeout{2000};

struct ObsPropertiesDeleter {
	void operator()(obs_properties_t *properties) const
	{
		if (properties)
			obs_properties_destroy(properties);
	}
};

using ObsPropertiesPtr = std::unique_ptr<obs_properties_t, ObsPropertiesDeleter>;

enum class UpdateOutcome { Applied, Rejected, TimedOut };

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "encoder operation failed";
	return false;
}

const char *encoder_type_name(enum obs_encoder_type type)
{
	return type == OBS_ENCODER_VIDEO ? "video" : "audio";
}

const char *module_load_state_name(enum obs_module_load_state state)
{
	switch (state) {
	case OBS_MODULE_ENABLED:
		return "enabled";
	case OBS_MODULE_MISSING:
		return "missing";
	case OBS_MODULE_DISABLED:
		return "disabled";
	case OBS_MODULE_DISABLED_SAFE:
		return "disabledSafe";
	case OBS_MODULE_FAILED_TO_OPEN:
		return "failedToOpen";
	case OBS_MODULE_FAILED_TO_INITIALIZE:
		return "failedToInitialize";
	default:
		return "invalid";
	}
}

bool encoder_kind_exists(std::string_view requested)
{
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &kind); ++index) {
		if (kind && requested == kind)
			return true;
	}
	return false;
}

bool read_encoder_kind(obs_data_t *params, std::string &kind, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "kind", kind, present) || !present ||
	    !phase2_is_bounded_string(kind, kMaxEncoderKindBytes) || !is_safe_identifier(kind.c_str(), kMaxEncoderKindBytes))
		return fail(error, "bad_request", "params.kind must be a valid encoder kind identifier");
	if (!encoder_kind_exists(kind))
		return fail(error, "not_found", "encoder kind is not registered");
	return true;
}

struct EncoderCreateOptions {
	std::string kind;
	std::string type;
	std::string name;
	ObsDataPtr settings;
	uint64_t canvas = 0;
	long long audio_track = 0;
};

bool read_encoder_type(obs_data_t *params, std::string &type, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "type", type, present) || !present || (type != "video" && type != "audio"))
		return fail(error, "bad_request", "params.type must be 'video' or 'audio'");
	return true;
}

bool read_encoder_name_and_settings(obs_data_t *params, uint64_t handle, std::string &name, ObsDataPtr &settings,
					    RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_string(params, "name", name, present))
		return fail(error, "bad_request", "params.name must be a string when present");
	if (!present)
		name = "engine-encoder-" + std::to_string(handle);
	if (!phase2_is_bounded_string(name, kMaxEncoderNameBytes))
		return fail(error, "bad_request", "params.name must be a non-empty encoder name of at most 256 bytes");
	if (!phase2_read_object(params, "settings", settings, present))
		return fail(error, "bad_request", "params.settings must be an object when present");
	return true;
}

bool read_encoder_video_input(const Engine &engine, obs_data_t *params, uint64_t default_canvas,
				      uint64_t &canvas, RuntimeV2Error &error)
{
	canvas = default_canvas;
	ObsDataPtr input;
	bool present = false;
	if (!phase2_read_object(params, "videoInput", input, present))
		return fail(error, "bad_request", "params.videoInput must be an object when present");
	if (present) {
		std::string type;
		bool type_present = false;
		if (!phase2_read_string(input.get(), "type", type, type_present) || !type_present || type != "canvas" ||
		    !phase2_read_handle(input.get(), "canvas", canvas))
			return fail(error, "bad_request", "videoInput must be {type:'canvas',canvas:<handle>}");
	}
	obs_canvas_t *target = engine.v2_canvas_for_handle(canvas);
	if (!target || obs_canvas_removed(target) || !obs_canvas_get_video(target))
		return fail(error, "not_found", "videoInput canvas was not found or has no video");
	return true;
}

bool read_encoder_audio_track(obs_data_t *params, long long &track, RuntimeV2Error &error)
{
	bool present = false;
	if (!phase2_read_integer(params, "audioTrack", track, present) || !present || track < 1 ||
	    track > MAX_AUDIO_MIXES)
		return fail(error, "bad_request", "audioTrack must be an integer from 1 through MAX_AUDIO_MIXES");
	return true;
}

bool read_encoder_create_options(const Engine &engine, obs_data_t *params, uint64_t handle, uint64_t main_canvas,
					EncoderCreateOptions &options, RuntimeV2Error &error)
{
	if (!read_encoder_kind(params, options.kind, error) || !read_encoder_type(params, options.type, error) ||
	    !read_encoder_name_and_settings(params, handle, options.name, options.settings, error))
		return false;
	const enum obs_encoder_type expected = options.type == "video" ? OBS_ENCODER_VIDEO : OBS_ENCODER_AUDIO;
	if (obs_get_encoder_type(options.kind.c_str()) != expected)
		return fail(error, "bad_request", "encoder kind type does not match params.type");
	if (!options.settings)
		options.settings.reset(obs_encoder_defaults(options.kind.c_str()));
	if (!options.settings)
		options.settings.reset(obs_data_create());
	if (options.type == "video")
		return read_encoder_video_input(engine, params, main_canvas, options.canvas, error);
	return read_encoder_audio_track(params, options.audio_track, error);
}

obs_encoder_t *create_encoder(const EncoderCreateOptions &options)
{
	if (options.type == "video")
		return obs_video_encoder_create(options.kind.c_str(), options.name.c_str(), options.settings.get(), nullptr);
	return obs_audio_encoder_create(options.kind.c_str(), options.name.c_str(), options.settings.get(),
					static_cast<size_t>(options.audio_track - 1), nullptr);
}

bool attach_encoder_media(const EncoderCreateOptions &options, obs_encoder_t *encoder, const Engine &engine,
				  RuntimeV2Error &error)
{
	if (options.type == "video") {
		obs_canvas_t *canvas = engine.v2_canvas_for_handle(options.canvas);
		obs_encoder_set_video(encoder, canvas ? obs_canvas_get_video(canvas) : nullptr);
		if (!canvas || obs_encoder_parent_video(encoder) != obs_canvas_get_video(canvas))
			return fail(error, "obs_error", "libobs rejected the encoder video input");
		return true;
	}
	obs_encoder_set_audio(encoder, obs_get_audio());
	return obs_encoder_audio(encoder) != nullptr || fail(error, "obs_error", "libobs rejected the encoder audio input");
}

ObsDataPtr make_encoder_capabilities(const char *kind)
{
	const uint32_t caps = obs_get_encoder_caps(kind);
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "deprecated", (caps & OBS_ENCODER_CAP_DEPRECATED) != 0);
	obs_data_set_bool(data.get(), "internal", (caps & OBS_ENCODER_CAP_INTERNAL) != 0);
	obs_data_set_bool(data.get(), "passTexture", (caps & OBS_ENCODER_CAP_PASS_TEXTURE) != 0);
	obs_data_set_bool(data.get(), "dynamicBitrate", (caps & OBS_ENCODER_CAP_DYN_BITRATE) != 0);
	obs_data_set_bool(data.get(), "roi", (caps & OBS_ENCODER_CAP_ROI) != 0);
	obs_data_set_bool(data.get(), "scaling", (caps & OBS_ENCODER_CAP_SCALING) != 0);
	obs_data_set_bool(data.get(), "multitrackDynamicBitrate",
			 (caps & OBS_ENCODER_CAP_MULTITRACK_DYN_BITRATE) != 0);
	return data;
}

ObsDataPtr make_encoder_kind_data(const char *kind)
{
	const enum obs_encoder_type type = obs_get_encoder_type(kind);
	const enum obs_module_load_state load_state = obs_encoder_load_state(kind);
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "id", kind);
	obs_data_set_string(data.get(), "displayName",
			   obs_encoder_get_display_name(kind) ? obs_encoder_get_display_name(kind) : kind);
	obs_data_set_string(data.get(), "type", encoder_type_name(type));
	obs_data_set_string(data.get(), "codec", obs_get_encoder_codec(kind) ? obs_get_encoder_codec(kind) : "");
	obs_data_set_int(data.get(), "moduleLoadState", static_cast<long long>(load_state));
	obs_data_set_string(data.get(), "moduleLoadStateName", module_load_state_name(load_state));
	obs_data_set_bool(data.get(), "registered", true);
	obs_data_set_bool(data.get(), "moduleLoaded", load_state == OBS_MODULE_ENABLED);
	/* Registration and module loading do not prove that a hardware context can
	 * initialize. That fact is only learned from the later Output path. */
	obs_data_set_string(data.get(), "actualRuntimeCompatibility", "unknown");
	obs_module_t *module = obs_encoder_get_module(kind);
	if (module) {
		const char *file = obs_get_module_file_name(module);
		if (file)
			obs_data_set_string(data.get(), "module", file);
	}
	obs_data_set_obj(data.get(), "capabilities", make_encoder_capabilities(kind).get());
	return data;
}

void set_nullable_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	if (handle)
		phase2_set_handle(data, name, handle);
	else
		obs_data_set_obj(data, name, nullptr);
}

ObsDataPtr make_video_input(uint64_t canvas)
{
	if (!canvas)
		return ObsDataPtr(obs_data_create());
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "type", "canvas");
	phase2_set_handle(data.get(), "canvas", canvas);
	return data;
}

ObsDataPtr encoder_settings(obs_encoder_t *encoder)
{
	ObsDataPtr current(obs_encoder_get_settings(encoder));
	return current ? clone_property_settings(current.get()) : ObsDataPtr{};
}

void set_bound_outputs(obs_data_t *data, const EncoderEntry &entry)
{
	ObsArrayPtr outputs(obs_data_array_create());
	std::vector<uint64_t> sorted(entry.bound_outputs.begin(), entry.bound_outputs.end());
	std::sort(sorted.begin(), sorted.end());
	for (const uint64_t handle : sorted) {
		ObsDataPtr output(obs_data_create());
		phase2_set_handle(output.get(), "output", handle);
		obs_data_array_push_back(outputs.get(), output.get());
	}
	obs_data_set_array(data, "boundOutputs", outputs.get());
}

ObsDataPtr make_encoder_state(uint64_t handle, const EncoderEntry &entry)
{
	obs_encoder_t *encoder = entry.encoder;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_bool(data.get(), "created", true);
	obs_data_set_bool(data.get(), "initialized", obs_encoder_initialized(encoder));
	obs_data_set_bool(data.get(), "active", obs_encoder_active(encoder));
	obs_data_set_string(data.get(), "type", encoder_type_name(obs_encoder_get_type(encoder)));
	obs_data_set_string(data.get(), "codec", obs_encoder_get_codec(encoder) ? obs_encoder_get_codec(encoder) : "");
	if (entry.audio_track)
		obs_data_set_int(data.get(), "audioTrack", static_cast<long long>(entry.audio_track));
	else
		obs_data_set_obj(data.get(), "videoInput", make_video_input(entry.video_canvas).get());
	set_nullable_handle(data.get(), "group", entry.group);
	set_bound_outputs(data.get(), entry);
	obs_data_set_string(data.get(), "actualRuntimeCompatibility", "unknown");
	return data;
}

ObsDataPtr make_encoder_summary(uint64_t handle, const EncoderEntry &entry)
{
	obs_encoder_t *encoder = entry.encoder;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_string(data.get(), "name", obs_encoder_get_name(encoder) ? obs_encoder_get_name(encoder) : "");
	obs_data_set_string(data.get(), "kind", obs_encoder_get_id(encoder) ? obs_encoder_get_id(encoder) : "");
	obs_data_set_string(data.get(), "type", encoder_type_name(obs_encoder_get_type(encoder)));
	obs_data_set_string(data.get(), "codec", obs_encoder_get_codec(encoder) ? obs_encoder_get_codec(encoder) : "");
	ObsDataPtr state = make_encoder_state(handle, entry);
	obs_data_set_obj(data.get(), "state", state.get());
	obs_data_set_bool(data.get(), "created", true);
	obs_data_set_bool(data.get(), "initialized", obs_encoder_initialized(encoder));
	obs_data_set_bool(data.get(), "active", obs_encoder_active(encoder));
	if (entry.audio_track)
		obs_data_set_int(data.get(), "audioTrack", static_cast<long long>(entry.audio_track));
	else
		obs_data_set_obj(data.get(), "videoInput", make_video_input(entry.video_canvas).get());
	set_nullable_handle(data.get(), "group", entry.group);
	set_bound_outputs(data.get(), entry);
	return data;
}

ObsDataPtr make_encoder_settings_result(uint64_t handle, obs_encoder_t *encoder)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	ObsDataPtr settings = encoder_settings(encoder);
	if (settings)
		obs_data_set_obj(data.get(), "settings", settings.get());
	return data;
}

void tracked_encoder_update(void *data, uint64_t serial, bool success)
{
	EncoderMutation *mutation = static_cast<EncoderMutation *>(data);
	if (!mutation)
		return;
	{
		std::lock_guard lock(mutation->mutex);
		mutation->done = true;
		mutation->success = success;
		mutation->callback_serial = serial;
	}
	mutation->cv.notify_all();
}

void remove_pending_update(EncoderV2State &state, obs_encoder_t *encoder,
				   const std::shared_ptr<EncoderMutation> &mutation)
{
	std::lock_guard lock(state.mutex);
	const auto it = state.pending.find(encoder);
	if (it != state.pending.end() && it->second == mutation)
		state.pending.erase(it);
}

void discard_completed_updates(EncoderV2State &state)
{
	std::lock_guard lock(state.mutex);
	for (auto it = state.pending.begin(); it != state.pending.end();) {
		std::lock_guard mutation_lock(it->second->mutex);
		if (it->second->done)
			it = state.pending.erase(it);
		else
			++it;
	}
}

bool register_pending_update(EncoderV2State &state, obs_encoder_t *encoder,
				     const std::shared_ptr<EncoderMutation> &mutation, uint64_t &serial, RuntimeV2Error &error)
{
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting) {
			fail(error, "invalid_state", "encoder runtime is shutting down");
			return false;
		}
		if (state.pending.contains(encoder)) {
			fail(error, "busy", "encoder already has an unsettled settings update");
			return false;
		}
		serial = state.next_serial++;
		state.pending.emplace(encoder, mutation);
	}
	return true;
}

UpdateOutcome settle_pending_update(EncoderV2State &state, obs_encoder_t *encoder,
					 const std::shared_ptr<EncoderMutation> &mutation, uint64_t serial, bool active,
					 uint64_t revision, RuntimeV2Error &error)
{
	std::unique_lock lock(mutation->mutex);
	if (!mutation->cv.wait_for(lock, kEncoderUpdateTimeout, [&] { return mutation->done; })) {
		if (state.events)
			state.events->require_resync_after_queued_events(revision);
		std::fprintf(stderr, "obs-engine: encoder update serial %llu timed out%s; controller resync required\n",
			     static_cast<unsigned long long>(serial), active ? " while active" : "");
		return UpdateOutcome::TimedOut;
	}
	const bool success = mutation->success;
	const uint64_t callback_serial = mutation->callback_serial;
	lock.unlock();
	remove_pending_update(state, encoder, mutation);
	if (!success) {
		if (active && state.events)
			state.events->require_resync_after_queued_events(revision);
		fail(error, "obs_error", "encoder plugin rejected the settings update");
		return UpdateOutcome::Rejected;
	}
	if (callback_serial != serial) {
		fail(error, "internal_error", "encoder update completion serial did not match the request");
		return UpdateOutcome::Rejected;
	}
	return UpdateOutcome::Applied;
}

UpdateOutcome submit_encoder_update(EncoderV2State &state, obs_encoder_t *encoder, obs_data_t *settings,
					     bool replace_settings, bool active, uint64_t revision, RuntimeV2Error &error)
{
	discard_completed_updates(state);
	std::shared_ptr<EncoderMutation> mutation = std::make_shared<EncoderMutation>();
	uint64_t serial = 0;
	if (!register_pending_update(state, encoder, mutation, serial, error))
		return UpdateOutcome::Rejected;
	if (!obs_encoder_update_tracked(encoder, settings, replace_settings, serial, tracked_encoder_update,
				       mutation.get())) {
		remove_pending_update(state, encoder, mutation);
		fail(error, "busy", "encoder rejected a concurrent settings update");
		return UpdateOutcome::Rejected;
	}
	return settle_pending_update(state, encoder, mutation, serial, active, revision, error);
}

bool validate_encoder_candidate(obs_encoder_t *encoder, obs_data_t *candidate, RuntimeV2Error &error)
{
	ObsPropertiesPtr properties(obs_encoder_properties(encoder));
	if (!properties)
		return true;
	ObsArrayPtr issues = validate_property_patch(properties.get(), candidate);
	if (obs_data_array_count(issues.get()) != 0)
		return fail(error, "bad_request", "encoder settings failed property validation");
	return true;
}

struct RoiList {
	std::vector<obs_encoder_roi> values;
};

void collect_roi(void *data, struct obs_encoder_roi *roi)
{
	if (data && roi)
		static_cast<RoiList *>(data)->values.push_back(*roi);
}

ObsDataPtr make_roi_list(uint64_t handle, obs_encoder_t *encoder)
{
	RoiList list;
	obs_encoder_enum_roi(encoder, collect_roi, &list);
	ObsArrayPtr values(obs_data_array_create());
	const size_t count = list.values.size();
	for (size_t reverse_index = 0; reverse_index < count; ++reverse_index) {
		const obs_encoder_roi &roi = list.values[reverse_index];
		ObsDataPtr item(obs_data_create());
		obs_data_set_int(item.get(), "index", static_cast<long long>(count - reverse_index - 1));
		obs_data_set_int(item.get(), "top", roi.top);
		obs_data_set_int(item.get(), "bottom", roi.bottom);
		obs_data_set_int(item.get(), "left", roi.left);
		obs_data_set_int(item.get(), "right", roi.right);
		obs_data_set_double(item.get(), "priority", roi.priority);
		obs_data_array_push_back(values.get(), item.get());
	}
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_array(data.get(), "rois", values.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(count));
	return data;
}

bool encoder_input_dimensions(const EncoderEntry &entry, uint32_t &width, uint32_t &height)
{
	video_t *video = obs_encoder_parent_video(entry.encoder);
	if (!video)
		return false;
	width = video_output_get_width(video);
	height = video_output_get_height(video);
	return width != 0 && height != 0;
}

bool read_roi_coordinate(obs_data_t *input, const char *name, uint32_t &target, RuntimeV2Error &error)
{
	long long value = 0;
	bool present = false;
	if (!phase2_read_integer(input, name, value, present) || !present || value < 0 ||
	    static_cast<uint64_t>(value) > std::numeric_limits<uint32_t>::max())
		return fail(error, "bad_request", "ROI coordinates must be non-negative 32-bit integers");
	target = static_cast<uint32_t>(value);
	return true;
}

bool read_roi_request(obs_data_t *params, obs_encoder_roi &roi, RuntimeV2Error &error)
{
	ObsDataPtr object;
	bool object_present = false;
	if (!phase2_read_object(params, "roi", object, object_present))
		return fail(error, "bad_request", "params.roi must be an object when present");
	obs_data_t *input = object_present ? object.get() : params;
	if (!read_roi_coordinate(input, "left", roi.left, error) || !read_roi_coordinate(input, "top", roi.top, error) ||
	    !read_roi_coordinate(input, "right", roi.right, error) ||
	    !read_roi_coordinate(input, "bottom", roi.bottom, error))
		return false;
	bool present = false;
	double priority = 0.0;
	if (!read_finite_double(input, "priority", -1.0, 1.0, priority, present))
		return fail(error, "bad_request", "ROI priority must be a finite number in [-1,1]");
	roi.priority = present ? static_cast<float>(priority) : 0.0f;
	if (!present)
		return fail(error, "bad_request", "ROI priority is required");
	return true;
}

bool validate_roi(const EncoderEntry &entry, const obs_encoder_roi &roi, RuntimeV2Error &error)
{
	if (!(obs_encoder_get_caps(entry.encoder) & OBS_ENCODER_CAP_ROI))
		return fail(error, "unsupported_capability", "encoder does not support ROI");
	uint32_t width = 0;
	uint32_t height = 0;
	if (!encoder_input_dimensions(entry, width, height))
		return fail(error, "not_available", "encoder video input dimensions are unavailable");
	if (roi.left >= roi.right || roi.top >= roi.bottom || roi.right > width || roi.bottom > height)
		return fail(error, "bad_request", "ROI rectangle must be inside the encoder input dimensions");
	if (roi.right - roi.left < 16 || roi.bottom - roi.top < 16)
		return fail(error, "bad_request", "ROI rectangle must be at least 16 by 16 pixels");
	return true;
}

bool require_inactive_video_encoder(const EncoderEntry &entry, RuntimeV2Error &error)
{
	if (!entry.video_canvas)
		return fail(error, "unsupported_capability", "operation is only valid for video encoders");
	if (obs_encoder_active(entry.encoder) || obs_encoder_initialized(entry.encoder))
		return fail(error, "busy", "video encoder configuration can only change before initialization");
	return true;
}

bool read_video_input_descriptor(const Engine &engine, obs_data_t *params, uint64_t &canvas_handle,
					RuntimeV2Error &error)
{
	ObsDataPtr input;
	bool present = false;
	if (!phase2_read_object(params, "videoInput", input, present) || !present)
		return fail(error, "bad_request", "params.videoInput object is required");
	std::string type;
	if (!phase2_read_string(input.get(), "type", type, present) || !present || type != "canvas")
		return fail(error, "bad_request", "videoInput.type must be 'canvas'");
	if (!phase2_read_handle(input.get(), "canvas", canvas_handle))
		return fail(error, "bad_request", "videoInput.canvas must be a canonical canvas handle string");
	obs_canvas_t *canvas = engine.v2_canvas_for_handle(canvas_handle);
	if (!canvas || obs_canvas_removed(canvas) || !obs_canvas_get_video(canvas))
		return fail(error, "not_found", "videoInput canvas was not found or has no video");
	return true;
}

bool read_scaled_dimensions(obs_data_t *params, uint32_t &width, uint32_t &height, RuntimeV2Error &error)
{
	long long requested_width = 0;
	long long requested_height = 0;
	bool width_present = false;
	bool height_present = false;
	if (!phase2_read_integer(params, "width", requested_width, width_present) || !width_present ||
	    !phase2_read_integer(params, "height", requested_height, height_present) || !height_present ||
	    requested_width < 0 || requested_height < 0 || requested_width > kMaxEncoderDimension ||
	    requested_height > kMaxEncoderDimension || ((requested_width == 0) != (requested_height == 0)))
		return fail(error, "bad_request", "scaled width and height must both be 0 or bounded positive integers");
	width = static_cast<uint32_t>(requested_width);
	height = static_cast<uint32_t>(requested_height);
	return true;
}

} // namespace

void Engine::v2_bind_encoder_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!encoder_v2_state_)
		encoder_v2_state_ = std::make_shared<EncoderV2State>();
	std::lock_guard lock(encoder_v2_state_->mutex);
	encoder_v2_state_->revisions = revisions;
	encoder_v2_state_->events = events;
	encoder_v2_state_->accepting = true;
}

void Engine::v2_prepare_encoder_shutdown() noexcept
{
	if (!encoder_v2_state_)
		return;
	{
		std::lock_guard lock(encoder_v2_state_->mutex);
		encoder_v2_state_->accepting = false;
	}
	for (auto &[_, entry] : encoders_) {
		if (entry.encoder && obs_encoder_active(entry.encoder))
			std::fprintf(stderr, "obs-engine: active encoder retained until its Output owner stops it\n");
		if (entry.encoder)
			obs_encoder_release(entry.encoder);
		entry.encoder = nullptr;
	}
	encoders_.clear();
	{
		std::lock_guard lock(encoder_v2_state_->mutex);
		encoder_v2_state_->pending.clear();
		encoder_v2_state_->revisions = nullptr;
		encoder_v2_state_->events = nullptr;
	}
}

bool Engine::v2_get_encoder_entry(obs_data_t *params, uint64_t &handle, EncoderEntry *&entry,
				  RuntimeV2Error &error) const
{
	if (!phase2_read_handle(params, "encoder", handle))
		return fail(error, "bad_request", "params.encoder must be a canonical decimal encoder handle string");
	const auto it = encoders_.find(handle);
	if (it == encoders_.end() || !it->second.encoder)
		return fail(error, "not_found", "encoder handle was not found");
	entry = const_cast<EncoderEntry *>(&it->second);
	return true;
}

uint64_t Engine::v2_encoder_handle_for_pointer(const obs_encoder_t *encoder) const
{
	for (const auto &[handle, entry] : encoders_)
		if (entry.encoder == encoder)
			return handle;
	return 0;
}

bool Engine::v2_encoder_kind_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsArrayPtr kinds(obs_data_array_create());
	const char *kind = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &kind); ++index) {
		if (kind)
			obs_data_array_push_back(kinds.get(), make_encoder_kind_data(kind).get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "kinds", kinds.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_kind_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_encoder_kind(params, kind, error))
		return false;
	result.data = make_encoder_kind_data(kind.c_str());
	return true;
}

bool Engine::v2_encoder_kind_defaults(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_encoder_kind(params, kind, error))
		return false;
	ObsDataPtr defaults(obs_encoder_defaults(kind.c_str()));
	if (!defaults)
		return fail(error, "obs_error", "encoder kind did not provide defaults");
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "settings", defaults.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_kind_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_encoder_kind(params, kind, error))
		return false;
	ObsPropertiesPtr properties(obs_get_encoder_properties(kind.c_str()));
	ObsDataPtr defaults(obs_encoder_defaults(kind.c_str()));
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	if (defaults)
		obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), defaults.get()).get());
	obs_data_set_array(data.get(), "properties", serialize_properties(properties.get(), defaults.get()).get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  properties && (obs_properties_get_flags(properties.get()) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_kind_capabilities(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string kind;
	if (!read_encoder_kind(params, kind, error))
		return false;
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "kind", kind.c_str());
	obs_data_set_obj(data.get(), "capabilities", make_encoder_capabilities(kind.c_str()).get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<uint64_t> handles;
	handles.reserve(encoders_.size());
	for (const auto &[handle, _] : encoders_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr values(obs_data_array_create());
	for (const uint64_t handle : handles)
		obs_data_array_push_back(values.get(), make_encoder_summary(handle, encoders_.at(handle)).get());
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "encoders", values.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	result.data = make_encoder_summary(handle, *entry);
	return true;
}

bool Engine::v2_encoder_create(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	const uint64_t handle = allocate_handle();
	EncoderCreateOptions options;
	if (!read_encoder_create_options(*this, params, handle, main_canvas_, options, error))
		return false;
	obs_encoder_t *encoder = create_encoder(options);
	if (!encoder)
		return fail(error, "obs_error", "libobs encoder creation failed");
	if (!attach_encoder_media(options, encoder, *this, error)) {
		obs_encoder_release(encoder);
		return false;
	}
	EncoderEntry entry;
	entry.encoder = encoder;
	entry.video_canvas = options.type == "video" ? options.canvas : 0;
	entry.audio_track = options.type == "audio" ? static_cast<uint64_t>(options.audio_track) : 0;
	try {
		if (!encoders_.emplace(handle, std::move(entry)).second)
			throw std::runtime_error("encoder handle collision");
	} catch (...) {
		obs_encoder_release(encoder);
		throw;
	}
	result.data = make_encoder_summary(handle, encoders_.at(handle));
	phase2_append_event(result, "encoder.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (entry->group || !entry->bound_outputs.empty() || obs_encoder_active(entry->encoder))
		return fail(error, "object_in_use", "encoder is still grouped, output-bound, or active");
	if (encoder_v2_state_) {
		std::lock_guard lock(encoder_v2_state_->mutex);
		if (encoder_v2_state_->pending.contains(entry->encoder))
			return fail(error, "busy", "encoder has an unsettled settings update");
	}
	obs_encoder_release(entry->encoder);
	encoders_.erase(handle);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	result.data = std::move(data);
	phase2_append_event(result, "encoder.removed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_rename(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	std::string name;
	bool present = false;
	if (!phase2_read_string(params, "name", name, present) || !present ||
	    !phase2_is_bounded_string(name, kMaxEncoderNameBytes))
		return fail(error, "bad_request", "params.name must be a non-empty encoder name of at most 256 bytes");
	const std::string old_name = obs_encoder_get_name(entry->encoder) ? obs_encoder_get_name(entry->encoder) : "";
	if (old_name == name) {
		result.data = make_encoder_summary(handle, *entry);
		return true;
	}
	obs_encoder_set_name(entry->encoder, name.c_str());
	const char *actual = obs_encoder_get_name(entry->encoder);
	if (!actual || name != actual)
		return fail(error, "obs_error", "libobs did not accept the encoder name");
	result.data = make_encoder_summary(handle, *entry);
	ObsDataPtr event_data(obs_data_create());
	phase2_set_handle(event_data.get(), "encoder", handle);
	obs_data_set_string(event_data.get(), "name", actual);
	phase2_append_event(result, "encoder.renamed", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_get_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	result.data = make_encoder_settings_result(handle, entry->encoder);
	return true;
}

ObsDataPtr make_encoder_candidate(obs_data_t *current, obs_data_t *requested, bool replace)
{
	if (replace)
		return phase2_clone_data(requested);
	ObsDataPtr candidate = current ? clone_property_settings(current) : ObsDataPtr(obs_data_create());
	if (candidate)
		obs_data_apply(candidate.get(), requested);
	return candidate;
}

bool encoder_settings_equal(obs_data_t *left, obs_data_t *right)
{
	const char *left_json = left ? obs_data_get_json(left) : nullptr;
	const char *right_json = right ? obs_data_get_json(right) : nullptr;
	return left_json && right_json && std::strcmp(left_json, right_json) == 0;
}

bool Engine::v2_update_encoder_settings(EncoderEntry &entry, uint64_t handle, obs_data_t *requested, bool replace,
					RuntimeV2Result &result, RuntimeV2Error &error)
{
	ObsDataPtr current = encoder_settings(entry.encoder);
	ObsDataPtr candidate = make_encoder_candidate(current.get(), requested, replace);
	if (!candidate)
		return fail(error, "internal_error", "could not clone encoder settings");
	if (!validate_encoder_candidate(entry.encoder, candidate.get(), error))
		return false;
	if (encoder_settings_equal(current.get(), candidate.get())) {
		result.data = make_encoder_settings_result(handle, entry.encoder);
		return true;
	}
	if (!encoder_v2_state_)
		return fail(error, "invalid_state", "encoder update bridge is not bound");
	const UpdateOutcome outcome = submit_encoder_update(*encoder_v2_state_, entry.encoder, requested, replace,
							   obs_encoder_active(entry.encoder),
							   encoder_v2_state_->revisions
								   ? encoder_v2_state_->revisions->current()
								   : 0,
							   error);
	if (outcome != UpdateOutcome::Applied)
		return false;
	result.data = make_encoder_settings_result(handle, entry.encoder);
	phase2_append_event(result, "encoder.settingsChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_patch_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	ObsDataPtr settings;
	bool present = false;
	if (!phase2_read_object(params, "settings", settings, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	return v2_update_encoder_settings(*entry, handle, settings.get(), false, result, error);
}

bool Engine::v2_encoder_replace_settings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	ObsDataPtr settings;
	bool present = false;
	if (!phase2_read_object(params, "settings", settings, present) || !present)
		return fail(error, "bad_request", "params.settings object is required");
	return v2_update_encoder_settings(*entry, handle, settings.get(), true, result, error);
}

bool Engine::v2_encoder_get_properties(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	ObsPropertiesPtr properties(obs_encoder_properties(entry->encoder));
	ObsDataPtr settings = encoder_settings(entry->encoder);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_obj(data.get(), "settings", sanitize_property_settings(properties.get(), settings.get()).get());
	obs_data_set_array(data.get(), "properties", serialize_properties(properties.get(), settings.get()).get());
	obs_data_set_bool(data.get(), "deferUpdate",
			  properties && (obs_properties_get_flags(properties.get()) & OBS_PROPERTIES_DEFER_UPDATE) != 0);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_get_video_input(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!entry->video_canvas)
		return fail(error, "unsupported_capability", "audio encoders do not have a video input");
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_obj(data.get(), "videoInput", make_video_input(entry->video_canvas).get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_set_video_input(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	uint64_t canvas_handle = 0;
	if (!require_inactive_video_encoder(*entry, error) ||
	    !read_video_input_descriptor(*this, params, canvas_handle, error))
		return false;
	obs_canvas_t *canvas = v2_canvas_for_handle(canvas_handle);
	obs_encoder_set_video(entry->encoder, obs_canvas_get_video(canvas));
	if (obs_encoder_parent_video(entry->encoder) != obs_canvas_get_video(canvas))
		return fail(error, "obs_error", "libobs did not accept the video input");
	entry->video_canvas = canvas_handle;
	result.data = make_encoder_summary(handle, *entry);
	phase2_append_event(result, "encoder.inputChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_get_codec(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_string(data.get(), "codec", obs_encoder_get_codec(entry->encoder));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_get_type(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_string(data.get(), "type", encoder_type_name(obs_encoder_get_type(entry->encoder)));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_get_dimensions(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!entry->video_canvas)
		return fail(error, "unsupported_capability", "audio encoders do not have video dimensions");
	uint32_t input_width = 0;
	uint32_t input_height = 0;
	if (!encoder_input_dimensions(*entry, input_width, input_height))
		return fail(error, "not_available", "encoder video input dimensions are unavailable");
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", handle);
	obs_data_set_int(data.get(), "inputWidth", input_width);
	obs_data_set_int(data.get(), "inputHeight", input_height);
	obs_data_set_int(data.get(), "width", obs_encoder_get_width(entry->encoder));
	obs_data_set_int(data.get(), "height", obs_encoder_get_height(entry->encoder));
	obs_data_set_bool(data.get(), "scaled", obs_encoder_scaling_enabled(entry->encoder));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_get_state(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	result.data = make_encoder_state(handle, *entry);
	return true;
}

bool Engine::v2_encoder_set_scaled_size(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!require_inactive_video_encoder(*entry, error))
		return false;
	uint32_t width = 0;
	uint32_t height = 0;
	if (!read_scaled_dimensions(params, width, height, error))
		return false;
	obs_encoder_set_scaled_size(entry->encoder, width, height);
	const bool scaled = obs_encoder_scaling_enabled(entry->encoder);
	if (scaled != (width != 0))
		return fail(error, "obs_error", "libobs did not accept the scaled size");
	result.data = make_encoder_summary(handle, *entry);
	phase2_append_event(result, "encoder.scalingChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_set_scale_filter(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!require_inactive_video_encoder(*entry, error))
		return false;
	std::string filter;
	bool present = false;
	if (!phase2_read_string(params, "filter", filter, present) || !present)
		return fail(error, "bad_request", "params.filter must be a scale filter string");
	enum obs_scale_type requested = OBS_SCALE_DISABLE;
	if (!phase2_parse_scale_filter(filter, requested))
		return fail(error, "bad_request", "params.filter is unsupported");
	obs_encoder_set_gpu_scale_type(entry->encoder, requested);
	if (obs_encoder_get_scale_type(entry->encoder) != requested)
		return fail(error, "obs_error", "libobs did not accept the scale filter");
	result.data = make_encoder_summary(handle, *entry);
	phase2_append_event(result, "encoder.scalingChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_roi_list(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!(obs_encoder_get_caps(entry->encoder) & OBS_ENCODER_CAP_ROI))
		return fail(error, "unsupported_capability", "encoder does not support ROI");
	result.data = make_roi_list(handle, entry->encoder);
	return true;
}

bool Engine::v2_encoder_roi_add(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	obs_encoder_roi roi = {};
	if (!read_roi_request(params, roi, error) || !validate_roi(*entry, roi, error))
		return false;
	RoiList before;
	obs_encoder_enum_roi(entry->encoder, collect_roi, &before);
	if (!obs_encoder_add_roi(entry->encoder, &roi))
		return fail(error, "obs_error", "libobs rejected the ROI");
	result.data = make_roi_list(handle, entry->encoder);
	ObsDataPtr event_data = phase2_clone_data(result.data.get());
	obs_data_set_string(event_data.get(), "action", "add");
	phase2_append_event(result, "encoder.roiChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_roi_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!(obs_encoder_get_caps(entry->encoder) & OBS_ENCODER_CAP_ROI))
		return fail(error, "unsupported_capability", "encoder does not support ROI");
	long long index = 0;
	bool present = false;
	if (!phase2_read_integer(params, "index", index, present) || !present || index < 0)
		return fail(error, "bad_request", "params.index must be a non-negative ROI index");
	RoiList current;
	obs_encoder_enum_roi(entry->encoder, collect_roi, &current);
	if (static_cast<uint64_t>(index) >= current.values.size())
		return fail(error, "not_found", "ROI index was not found");
	const size_t insertion_index = current.values.size() - static_cast<size_t>(index) - 1;
	if (!obs_encoder_remove_roi(entry->encoder, insertion_index))
		return fail(error, "obs_error", "libobs did not remove the ROI");
	result.data = make_roi_list(handle, entry->encoder);
	ObsDataPtr event_data = phase2_clone_data(result.data.get());
	obs_data_set_string(event_data.get(), "action", "remove");
	phase2_append_event(result, "encoder.roiChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_roi_clear(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderEntry *entry = nullptr;
	if (!v2_get_encoder_entry(params, handle, entry, error))
		return false;
	if (!(obs_encoder_get_caps(entry->encoder) & OBS_ENCODER_CAP_ROI))
		return fail(error, "unsupported_capability", "encoder does not support ROI");
	RoiList current;
	obs_encoder_enum_roi(entry->encoder, collect_roi, &current);
	if (current.values.empty()) {
		result.data = make_roi_list(handle, entry->encoder);
		return true;
	}
	obs_encoder_clear_roi(entry->encoder);
	result.data = make_roi_list(handle, entry->encoder);
	ObsDataPtr event_data = phase2_clone_data(result.data.get());
	obs_data_set_string(event_data.get(), "action", "clear");
	phase2_append_event(result, "encoder.roiChanged", std::move(event_data));
	result.mutated = true;
	return true;
}

} // namespace obs_engine
