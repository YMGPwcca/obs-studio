#include "runtime.hpp"

#include <obs-interaction.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obs_engine {
namespace {

constexpr size_t kMaxKeyTextBytes = 4;
constexpr size_t kMaxTextBytes = 4096;
constexpr size_t kMaxTextScalars = 1024;
constexpr size_t kMaxTrackedKeysPerSource = 256;
constexpr uint32_t kMaxClickCount = 3;
constexpr uint32_t kMouseButtonMask = INTERACT_MOUSE_LEFT | INTERACT_MOUSE_MIDDLE | INTERACT_MOUSE_RIGHT;

struct TrackedKey {
	uint32_t modifiers = 0;
	std::string text;
	uint32_t native_modifiers = 0;
	uint32_t native_scancode = 0;
	uint32_t native_vkey = 0;
};

struct InteractionSourceState {
	bool focused = false;
	int32_t mouse_x = 0;
	int32_t mouse_y = 0;
	uint32_t mouse_modifiers = 0;
	bool mouse_left = false;
	bool mouse_middle = false;
	bool mouse_right = false;
	std::vector<TrackedKey> pressed_keys;
};

struct ModifierDescriptor {
	const char *name;
	uint32_t flag;
};

constexpr ModifierDescriptor kModifiers[] = {
	{"capsLock", INTERACT_CAPS_KEY},
	{"shift", INTERACT_SHIFT_KEY},
	{"control", INTERACT_CONTROL_KEY},
	{"alt", INTERACT_ALT_KEY},
	{"mouseLeft", INTERACT_MOUSE_LEFT},
	{"mouseMiddle", INTERACT_MOUSE_MIDDLE},
	{"mouseRight", INTERACT_MOUSE_RIGHT},
	{"command", INTERACT_COMMAND_KEY},
	{"numLock", INTERACT_NUMLOCK_KEY},
	{"keypad", INTERACT_IS_KEY_PAD},
	{"left", INTERACT_IS_LEFT},
	{"right", INTERACT_IS_RIGHT},
};

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "interaction operation failed";
	return false;
}

bool read_string_field(obs_data_t *data, const char *name, std::string &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_STRING) {
		obs_data_item_release(&item);
		return false;
	}
	const char *value = obs_data_item_get_string(item);
	out = value ? value : "";
	obs_data_item_release(&item);
	return true;
}

bool read_bool_field(obs_data_t *data, const char *name, bool &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_BOOLEAN) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_bool(item);
	obs_data_item_release(&item);
	return true;
}

bool read_integer_field(obs_data_t *data, const char *name, long long &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_NUMBER || obs_data_item_numtype(item) != OBS_DATA_NUM_INT) {
		obs_data_item_release(&item);
		return false;
	}
	out = obs_data_item_get_int(item);
	obs_data_item_release(&item);
	return true;
}

bool read_object_field(obs_data_t *data, const char *name, ObsDataPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_obj(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
}

bool parse_handle_text(std::string_view text, uint64_t &out)
{
	if (text.empty() || (text.size() > 1 && text.front() == '0'))
		return false;
	uint64_t value = 0;
	const char *begin = text.data();
	const char *end = begin + text.size();
	const auto parsed = std::from_chars(begin, end, value, 10);
	if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0 ||
	    value > static_cast<uint64_t>(std::numeric_limits<long long>::max()))
		return false;
	out = value;
	return true;
}

bool read_handle_field(obs_data_t *data, const char *name, uint64_t &out)
{
	std::string text;
	bool present = false;
	return read_string_field(data, name, text, present) && present && parse_handle_text(text, out);
}

bool read_int32_required(obs_data_t *data, const char *name, int32_t &out)
{
	long long value = 0;
	bool present = false;
	if (!read_integer_field(data, name, value, present) || !present ||
	    value < static_cast<long long>(std::numeric_limits<int32_t>::min()) ||
	    value > static_cast<long long>(std::numeric_limits<int32_t>::max()))
		return false;
	out = static_cast<int32_t>(value);
	return true;
}

bool read_u32_optional(obs_data_t *data, const char *name, uint32_t &out)
{
	long long value = 0;
	bool present = false;
	if (!read_integer_field(data, name, value, present))
		return false;
	if (!present) {
		out = 0;
		return true;
	}
	if (value < 0 || static_cast<unsigned long long>(value) > std::numeric_limits<uint32_t>::max())
		return false;
	out = static_cast<uint32_t>(value);
	return true;
}

void set_handle(obs_data_t *data, const char *name, uint64_t handle)
{
	const std::string text = std::to_string(handle);
	obs_data_set_string(data, name, text.c_str());
}

ObsDataPtr make_source_result(uint64_t handle)
{
	ObsDataPtr data(obs_data_create());
	set_handle(data.get(), "source", handle);
	return data;
}

bool parse_modifiers(obs_data_t *params, uint32_t &modifiers, RuntimeV2Error &error)
{
	modifiers = 0;
	ObsDataPtr object;
	bool present = false;
	if (!read_object_field(params, "modifiers", object, present))
		return fail(error, "bad_request", "params.modifiers must be an object when present");
	if (!present)
		return true;

	for (const ModifierDescriptor &descriptor : kModifiers) {
		bool enabled = false;
		bool field_present = false;
		if (!read_bool_field(object.get(), descriptor.name, enabled, field_present))
			return fail(error, "bad_request", "modifier fields must be booleans");
		if (field_present && enabled)
			modifiers |= descriptor.flag;
	}
	return true;
}

bool decode_utf8_lead(unsigned char first, size_t &length, uint32_t &value)
{
	if (first <= 0x7F) {
		length = 1;
		value = first;
	} else if (first >= 0xC2 && first <= 0xDF) {
		length = 2;
		value = first & 0x1F;
	} else if (first >= 0xE0 && first <= 0xEF) {
		length = 3;
		value = first & 0x0F;
	} else if (first >= 0xF0 && first <= 0xF4) {
		length = 4;
		value = first & 0x07;
	} else {
		return false;
	}
	return true;
}

bool is_valid_utf8_scalar(uint32_t value, size_t length)
{
	if (value == 0)
		return false;
	if (length == 3 && value < 0x800)
		return false;
	if (length == 4 && value < 0x10000)
		return false;
	if (value >= 0xD800 && value <= 0xDFFF)
		return false;
	return value <= 0x10FFFF;
}

bool decode_utf8_scalars(std::string_view text, std::vector<std::string> &scalars, size_t max_scalars)
{
	scalars.clear();
	for (size_t index = 0; index < text.size();) {
		const auto first = static_cast<unsigned char>(text[index]);
		size_t length = 0;
		uint32_t value = 0;
		if (!decode_utf8_lead(first, length, value))
			return false;
		if (index + length > text.size())
			return false;
		for (size_t offset = 1; offset < length; ++offset) {
			const auto byte = static_cast<unsigned char>(text[index + offset]);
			if ((byte & 0xC0) != 0x80)
				return false;
			value = (value << 6) | (byte & 0x3F);
		}
		if (!is_valid_utf8_scalar(value, length))
			return false;
		scalars.emplace_back(text.substr(index, length));
		if (scalars.size() > max_scalars)
			return false;
		index += length;
	}
	return true;
}

bool key_matches(const TrackedKey &key, uint32_t native_scancode, uint32_t native_vkey, std::string_view text)
{
	if (native_scancode != 0 || native_vkey != 0)
		return key.native_scancode == native_scancode && key.native_vkey == native_vkey;
	return key.native_scancode == 0 && key.native_vkey == 0 && key.text == text;
}

uint32_t mouse_button_flag(int32_t button)
{
	switch (button) {
	case MOUSE_LEFT:
		return INTERACT_MOUSE_LEFT;
	case MOUSE_MIDDLE:
		return INTERACT_MOUSE_MIDDLE;
	case MOUSE_RIGHT:
		return INTERACT_MOUSE_RIGHT;
	default:
		return 0;
	}
}

bool point_inside_source(obs_source_t *source, int32_t x, int32_t y)
{
	if (x < 0 || y < 0)
		return false;
	const uint32_t width = obs_source_get_width(source);
	const uint32_t height = obs_source_get_height(source);
	return width != 0 && height != 0 && static_cast<uint32_t>(x) < width && static_cast<uint32_t>(y) < height;
}

} // namespace

struct InteractionV2State {
	std::mutex mutex;
	std::unordered_map<uint64_t, InteractionSourceState> sources;
};

namespace {

using LiveSourceMap = std::unordered_map<uint64_t, obs_source_t *>;

void prune_stale_interaction_sources_locked(InteractionV2State &state, const LiveSourceMap &live_sources)
{
	// If tracked state does not outnumber live sources, retained state is already
	// bounded by the live engine object set. Only pay for a scan once that bound
	// is exceeded; normal high-frequency pointer delivery stays O(1).
	if (state.sources.size() <= live_sources.size())
		return;
	for (auto it = state.sources.begin(); it != state.sources.end();) {
		if (!live_sources.contains(it->first))
			it = state.sources.erase(it);
		else
			++it;
	}
}

std::shared_ptr<InteractionV2State> ensure_interaction_state(std::shared_ptr<InteractionV2State> &slot,
							     const LiveSourceMap &live_sources, uint64_t handle)
{
	if (!slot)
		slot = std::make_shared<InteractionV2State>();
	std::shared_ptr<InteractionV2State> state = slot;
	{
		std::lock_guard lock(state->mutex);
		state->sources.try_emplace(handle);
		prune_stale_interaction_sources_locked(*state, live_sources);
	}
	return state;
}

void prune_existing_interaction_state(const std::shared_ptr<InteractionV2State> &state,
					      const LiveSourceMap &live_sources)
{
	if (!state)
		return;
	std::lock_guard lock(state->mutex);
	prune_stale_interaction_sources_locked(*state, live_sources);
}

InteractionSourceState snapshot_interaction_state(const std::shared_ptr<InteractionV2State> &state,
						  const LiveSourceMap &live_sources, uint64_t handle)
{
	InteractionSourceState snapshot;
	if (!state)
		return snapshot;
	std::lock_guard lock(state->mutex);
	prune_stale_interaction_sources_locked(*state, live_sources);
	const auto it = state->sources.find(handle);
	if (it != state->sources.end())
		snapshot = it->second;
	return snapshot;
}

int count_pressed_mouse_buttons(const InteractionSourceState &state)
{
	return static_cast<int>(state.mouse_left) + static_cast<int>(state.mouse_middle) +
	       static_cast<int>(state.mouse_right);
}

void release_interaction_mouse_button(obs_source_t *source, obs_mouse_event &mouse, uint32_t &modifiers,
					      int32_t button, bool pressed)
{
	if (!pressed)
		return;
	modifiers &= ~mouse_button_flag(button);
	mouse.modifiers = modifiers;
	obs_source_send_mouse_click(source, &mouse, button, true, 1);
}

void release_interaction_keys(obs_source_t *source, InteractionSourceState &state)
{
	for (TrackedKey &key : state.pressed_keys) {
		obs_key_event event{};
		event.modifiers = key.modifiers;
		event.text = key.text.data();
		event.native_modifiers = key.native_modifiers;
		event.native_scancode = key.native_scancode;
		event.native_vkey = key.native_vkey;
		obs_source_send_key_click(source, &event, true);
	}
}

void erase_interaction_state(const std::shared_ptr<InteractionV2State> &state, uint64_t handle)
{
	if (!state)
		return;
	std::lock_guard lock(state->mutex);
	state->sources.erase(handle);
}

} // namespace

bool Engine::v2_get_interaction_source(obs_data_t *params, uint64_t &handle, obs_source_t *&source,
				       RuntimeV2Error &error)
{
	if (!read_handle_field(params, "source", handle))
		return fail(error, "bad_request", "params.source must be a canonical decimal handle string");
	const auto it = sources_.find(handle);
	if (it == sources_.end()) {
		if (interaction_v2_state_) {
			std::lock_guard lock(interaction_v2_state_->mutex);
			interaction_v2_state_->sources.erase(handle);
		}
		return fail(error, "not_found", "source handle was not found");
	}
	source = it->second;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_INTERACTION) == 0)
		return fail(error, "unsupported_capability", "source does not advertise interaction support");
	return true;
}

bool Engine::v2_interaction_focus(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;
	bool focused = false;
	bool present = false;
	if (!read_bool_field(params, "focused", focused, present) || !present)
		return fail(error, "bad_request", "params.focused must be a boolean");

	auto state = ensure_interaction_state(interaction_v2_state_, sources_, handle);
	result.data = make_source_result(handle);
	{
		std::lock_guard lock(state->mutex);
		state->sources.at(handle).focused = focused;
	}
	obs_source_send_focus(source, focused);
	return true;
}

bool Engine::v2_interaction_mouse_move(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;
	int32_t x = 0;
	int32_t y = 0;
	if (!read_int32_required(params, "x", x) || !read_int32_required(params, "y", y))
		return fail(error, "bad_request", "params.x and params.y must be signed 32-bit integers");
	bool leave = false;
	bool present = false;
	if (!read_bool_field(params, "leave", leave, present))
		return fail(error, "bad_request", "params.leave must be a boolean when present");
	if (!present)
		leave = false;
	if (!leave && !point_inside_source(source, x, y))
		return fail(error, "bad_request", "mouse coordinates must be inside the source unless leave=true");
	uint32_t modifiers = 0;
	if (!parse_modifiers(params, modifiers, error))
		return false;

	auto state = ensure_interaction_state(interaction_v2_state_, sources_, handle);
	result.data = make_source_result(handle);
	{
		std::lock_guard lock(state->mutex);
		auto &source_state = state->sources.at(handle);
		source_state.mouse_x = x;
		source_state.mouse_y = y;
		source_state.mouse_modifiers = modifiers;
	}
	obs_mouse_event event{modifiers, x, y};
	obs_source_send_mouse_move(source, &event, leave);
	return true;
}

struct MouseButtonDescriptor {
	std::string_view name;
	int32_t value;
};

constexpr MouseButtonDescriptor kMouseButtons[] = {
	{"left", MOUSE_LEFT},
	{"middle", MOUSE_MIDDLE},
	{"right", MOUSE_RIGHT},
};

bool parse_mouse_button(std::string_view text, int32_t &button)
{
	for (const MouseButtonDescriptor &descriptor : kMouseButtons) {
		if (descriptor.name == text) {
			button = descriptor.value;
			return true;
		}
	}
	return false;
}

bool read_mouse_button_state(obs_data_t *params, bool &mouse_up, RuntimeV2Error &error)
{
	std::string state;
	bool present = false;
	if (!read_string_field(params, "state", state, present) || !present || (state != "down" && state != "up"))
		return fail(error, "bad_request", "params.state must be 'down' or 'up'");
	mouse_up = state == "up";
	return true;
}

bool read_click_count(obs_data_t *params, uint32_t &click_count, RuntimeV2Error &error)
{
	click_count = 1;
	long long raw = 0;
	bool present = false;
	if (!read_integer_field(params, "clickCount", raw, present))
		return fail(error, "bad_request", "params.clickCount must be an integer when present");
	if (!present)
		return true;
	if (raw < 1 || raw > kMaxClickCount)
		return fail(error, "bad_request", "params.clickCount must be between 1 and 3");
	click_count = static_cast<uint32_t>(raw);
	return true;
}

struct MouseButtonInput {
	int32_t x = 0;
	int32_t y = 0;
	int32_t button = -1;
	bool mouse_up = false;
	uint32_t click_count = 1;
	uint32_t modifiers = 0;
};

bool read_mouse_button_input(obs_data_t *params, obs_source_t *source, MouseButtonInput &input,
					 RuntimeV2Error &error)
{
	if (!read_int32_required(params, "x", input.x) || !read_int32_required(params, "y", input.y))
		return fail(error, "bad_request", "params.x and params.y must be signed 32-bit integers");

	std::string button_text;
	bool present = false;
	if (!read_string_field(params, "button", button_text, present) || !present ||
	    !parse_mouse_button(button_text, input.button))
		return fail(error, "bad_request", "params.button must be 'left', 'middle' or 'right'");
	if (!read_mouse_button_state(params, input.mouse_up, error))
		return false;
	if (!input.mouse_up && !point_inside_source(source, input.x, input.y))
		return fail(error, "bad_request", "mouse-down coordinates must be inside the source");
	if (!read_click_count(params, input.click_count, error))
		return false;
	return parse_modifiers(params, input.modifiers, error);
}

bool Engine::v2_interaction_mouse_button(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;
	MouseButtonInput input;
	if (!read_mouse_button_input(params, source, input, error))
		return false;

	auto state = ensure_interaction_state(interaction_v2_state_, sources_, handle);
	result.data = make_source_result(handle);
	{
		std::lock_guard lock(state->mutex);
		auto &source_state = state->sources.at(handle);
		source_state.mouse_x = input.x;
		source_state.mouse_y = input.y;
		source_state.mouse_modifiers = input.modifiers;
		bool *pressed = input.button == MOUSE_LEFT ? &source_state.mouse_left
					 : input.button == MOUSE_MIDDLE ? &source_state.mouse_middle
									     : &source_state.mouse_right;
		*pressed = !input.mouse_up;
	}
	obs_mouse_event event{input.modifiers, input.x, input.y};
	obs_source_send_mouse_click(source, &event, input.button, input.mouse_up, input.click_count);
	return true;
}

struct MouseWheelInput {
	int32_t x = 0;
	int32_t y = 0;
	int32_t delta_x = 0;
	int32_t delta_y = 0;
	uint32_t modifiers = 0;
};

bool read_mouse_wheel_input(obs_data_t *params, obs_source_t *source, MouseWheelInput &input,
					RuntimeV2Error &error)
{
	if (!read_int32_required(params, "x", input.x) || !read_int32_required(params, "y", input.y) ||
	    !read_int32_required(params, "deltaX", input.delta_x) ||
	    !read_int32_required(params, "deltaY", input.delta_y))
		return fail(error, "bad_request", "mouse wheel coordinates and deltas must be signed 32-bit integers");
	if (!point_inside_source(source, input.x, input.y))
		return fail(error, "bad_request", "mouse wheel coordinates must be inside the source");
	if (input.delta_x == 0 && input.delta_y == 0)
		return fail(error, "bad_request", "at least one mouse wheel delta must be non-zero");
	return parse_modifiers(params, input.modifiers, error);
}

bool Engine::v2_interaction_mouse_wheel(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;
	MouseWheelInput input;
	if (!read_mouse_wheel_input(params, source, input, error))
		return false;

	auto state = ensure_interaction_state(interaction_v2_state_, sources_, handle);
	result.data = make_source_result(handle);
	{
		std::lock_guard lock(state->mutex);
		auto &source_state = state->sources.at(handle);
		source_state.mouse_x = input.x;
		source_state.mouse_y = input.y;
		source_state.mouse_modifiers = input.modifiers;
	}
	obs_mouse_event event{input.modifiers, input.x, input.y};
	obs_source_send_mouse_wheel(source, &event, input.delta_x, input.delta_y);
	return true;
}

struct KeyInput {
	bool key_up = false;
	std::string text;
	uint32_t modifiers = 0;
	uint32_t native_modifiers = 0;
	uint32_t native_scancode = 0;
	uint32_t native_vkey = 0;
};

bool read_key_state(obs_data_t *params, bool &key_up, RuntimeV2Error &error)
{
	std::string state;
	bool present = false;
	if (!read_string_field(params, "state", state, present) || !present || (state != "down" && state != "up"))
		return fail(error, "bad_request", "params.state must be 'down' or 'up'");
	key_up = state == "up";
	return true;
}

bool read_key_text(obs_data_t *params, std::string &text, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "text", text, present))
		return fail(error, "bad_request", "params.text must be a string when present");
	if (!present)
		text.clear();
	if (text.size() > kMaxKeyTextBytes)
		return fail(error, "bad_request", "params.text is too long for one key event");
	std::vector<std::string> scalars;
	if (!decode_utf8_scalars(text, scalars, 1) || scalars.size() > 1)
		return fail(error, "bad_request", "params.text must contain at most one non-NUL UTF-8 scalar");
	return true;
}

bool read_key_native_fields(obs_data_t *params, KeyInput &input, RuntimeV2Error &error)
{
	if (!read_u32_optional(params, "nativeModifiers", input.native_modifiers))
		return fail(error, "bad_request", "params.nativeModifiers must be an unsigned 32-bit integer");
	if (!read_u32_optional(params, "nativeScanCode", input.native_scancode))
		return fail(error, "bad_request", "params.nativeScanCode must be an unsigned 32-bit integer");
	if (!read_u32_optional(params, "nativeVirtualKey", input.native_vkey))
		return fail(error, "bad_request", "params.nativeVirtualKey must be an unsigned 32-bit integer");
	return true;
}

bool read_key_input(obs_data_t *params, KeyInput &input, RuntimeV2Error &error)
{
	if (!read_key_state(params, input.key_up, error) || !read_key_text(params, input.text, error) ||
	    !read_key_native_fields(params, input, error))
		return false;
	if (input.text.empty() && input.native_scancode == 0 && input.native_vkey == 0)
		return fail(error, "bad_request", "key event requires text, nativeScanCode or nativeVirtualKey");
	return parse_modifiers(params, input.modifiers, error);
}

bool read_text_input(obs_data_t *params, std::string &text, std::vector<std::string> &scalars, uint32_t &modifiers,
				     RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "text", text, present) || !present || text.empty())
		return fail(error, "bad_request", "params.text must be a non-empty UTF-8 string");
	if (text.size() > kMaxTextBytes)
		return fail(error, "bad_request", "params.text exceeds the 4096-byte interaction limit");
	if (!decode_utf8_scalars(text, scalars, kMaxTextScalars) || scalars.empty())
		return fail(error, "bad_request", "params.text must contain valid non-NUL UTF-8 scalar values");
	return parse_modifiers(params, modifiers, error);
}

bool update_key_tracking(InteractionSourceState &source_state, const KeyInput &input, RuntimeV2Error &error)
{
	auto &keys = source_state.pressed_keys;
	const auto it = std::find_if(keys.begin(), keys.end(), [&](const TrackedKey &key) {
		return key_matches(key, input.native_scancode, input.native_vkey, input.text);
	});
	if (input.key_up) {
		if (it != keys.end())
			keys.erase(it);
		return true;
	}
	if (it != keys.end())
		return true;
	if (keys.size() >= kMaxTrackedKeysPerSource)
		return fail(error, "busy", "source has too many distinct held keys; release keys or reset input");
	keys.push_back(TrackedKey{input.modifiers, input.text, input.native_modifiers, input.native_scancode,
					  input.native_vkey});
	return true;
}

bool Engine::v2_interaction_key(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;

	KeyInput input;
	if (!read_key_input(params, input, error))
		return false;

	auto state = ensure_interaction_state(interaction_v2_state_, sources_, handle);
	ObsDataPtr data = make_source_result(handle);
	{
		std::lock_guard lock(state->mutex);
		if (!update_key_tracking(state->sources.at(handle), input, error))
			return false;
	}
	result.data = std::move(data);

	obs_key_event event{};
	event.modifiers = input.modifiers;
	event.text = input.text.data();
	event.native_modifiers = input.native_modifiers;
	event.native_scancode = input.native_scancode;
	event.native_vkey = input.native_vkey;
	obs_source_send_key_click(source, &event, input.key_up);
	return true;
}

bool Engine::v2_interaction_text(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;
	std::string text;
	std::vector<std::string> scalars;
	uint32_t modifiers = 0;
	if (!read_text_input(params, text, scalars, modifiers, error))
		return false;

	prune_existing_interaction_state(interaction_v2_state_, sources_);
	result.data = make_source_result(handle);
	for (std::string &scalar : scalars) {
		obs_key_event event{};
		event.modifiers = modifiers;
		event.text = scalar.data();
		obs_source_send_key_click(source, &event, false);
		obs_source_send_key_click(source, &event, true);
	}
	return true;
}

bool Engine::v2_interaction_reset(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	obs_source_t *source = nullptr;
	if (!v2_get_interaction_source(params, handle, source, error))
		return false;

	std::shared_ptr<InteractionV2State> interaction_state = interaction_v2_state_;
	InteractionSourceState state = snapshot_interaction_state(interaction_state, sources_, handle);

	ObsDataPtr data = make_source_result(handle);
	obs_data_set_int(data.get(), "releasedKeys", static_cast<long long>(state.pressed_keys.size()));
	obs_data_set_int(data.get(), "releasedButtons", count_pressed_mouse_buttons(state));
	result.data = std::move(data);

	erase_interaction_state(interaction_state, handle);

	obs_mouse_event mouse{};
	mouse.x = state.mouse_x;
	mouse.y = state.mouse_y;
	uint32_t mouse_modifiers = state.mouse_modifiers & ~kMouseButtonMask;
	if (state.mouse_left)
		mouse_modifiers |= INTERACT_MOUSE_LEFT;
	if (state.mouse_middle)
		mouse_modifiers |= INTERACT_MOUSE_MIDDLE;
	if (state.mouse_right)
		mouse_modifiers |= INTERACT_MOUSE_RIGHT;
	release_interaction_mouse_button(source, mouse, mouse_modifiers, MOUSE_LEFT, state.mouse_left);
	release_interaction_mouse_button(source, mouse, mouse_modifiers, MOUSE_MIDDLE, state.mouse_middle);
	release_interaction_mouse_button(source, mouse, mouse_modifiers, MOUSE_RIGHT, state.mouse_right);
	release_interaction_keys(source, state);

	obs_mouse_event leave_event{};
	leave_event.x = state.mouse_x;
	leave_event.y = state.mouse_y;
	obs_source_send_mouse_move(source, &leave_event, true);
	obs_source_send_focus(source, false);
	return true;
}

} // namespace obs_engine
