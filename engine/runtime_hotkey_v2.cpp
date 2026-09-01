#include "runtime.hpp"

#include "events.hpp"
#include "validation.hpp"

#include <obs-hotkey.h>
#include <obs-interaction.h>
#include <util/dstr.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace obs_engine {

struct HotkeyTrigger {
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	bool pressed = false;
};

struct HotkeyOwnedContext {
	Engine *engine = nullptr;
	uint64_t source_handle = 0;
};

struct HotkeyV2State {
	std::mutex mutex;
	Engine *engine = nullptr;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	std::deque<HotkeyTrigger> triggers;
	std::unordered_map<obs_hotkey_id, std::shared_ptr<HotkeyOwnedContext>> owned_audio_hotkeys;
	bool accepting = false;
	bool tick_registered = false;
	bool background_capture = true;
};

namespace {

constexpr size_t kMaxHotkeyNameBytes = 256;
constexpr size_t kMaxHotkeyDescriptionBytes = 1024;
constexpr size_t kMaxBindings = 64;
constexpr size_t kMaxImportedHotkeys = 256;
constexpr size_t kMaxTriggerQueue = 512;

struct ModifierDescriptor {
	const char *name;
	uint32_t flag;
};

constexpr ModifierDescriptor kModifiers[] = {
	{"shift", INTERACT_SHIFT_KEY},
	{"control", INTERACT_CONTROL_KEY},
	{"alt", INTERACT_ALT_KEY},
	{"command", INTERACT_COMMAND_KEY},
};

struct HotkeyRegisterer {
	std::string type;
	std::string handle;
	std::string runtime_id;
	std::string object_name;
	std::string kind;
};

struct HotkeyBinding {
	obs_key_combination_t combination = {};
};

struct HotkeySnapshot {
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id pair_partner = OBS_INVALID_HOTKEY_ID;
	std::string name;
	std::string description;
	HotkeyRegisterer registerer;
	std::vector<HotkeyBinding> bindings;
};

struct HotkeyEnumerationContext {
	Engine *engine = nullptr;
	std::vector<HotkeySnapshot> *snapshots = nullptr;
	bool failed = false;
};

struct BindingUpdate {
	HotkeySnapshot snapshot;
	std::vector<HotkeyBinding> bindings;
};

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "hotkey operation failed";
	return false;
}

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

void append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data)
{
	result.events.push_back(RuntimeV2Event{name, std::move(data)});
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

bool read_array_field(obs_data_t *data, const char *name, ObsArrayPtr &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_ARRAY) {
		obs_data_item_release(&item);
		return false;
	}
	out.reset(obs_data_item_get_array(item));
	obs_data_item_release(&item);
	return static_cast<bool>(out);
}

bool is_canonical_handle(std::string_view value)
{
	if (value.empty() || (value.size() > 1 && value.front() == '0'))
		return false;
	return std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
}

const ModifierDescriptor *find_modifier(std::string_view name)
{
	for (const ModifierDescriptor &modifier : kModifiers) {
		if (modifier.name == name)
			return &modifier;
	}
	return nullptr;
}

bool read_optional_string(obs_data_t *data, const char *field, size_t max_bytes, std::string &value, bool &present,
				  RuntimeV2Error &error)
{
	if (!read_string_field(data, field, value, present))
		return fail(error, "bad_request", "hotkey field must be a string when present");
	if (present && value.size() > max_bytes)
		return fail(error, "bad_request", "hotkey field is too long");
	return true;
}

bool read_registerer_type(obs_data_t *data, HotkeyRegisterer &registerer, bool &present, RuntimeV2Error &error)
{
	if (!read_string_field(data, "type", registerer.type, present) || !present)
		return fail(error, "bad_request", "registerer.type is required");
	if (registerer.type != "frontend" && registerer.type != "source" && registerer.type != "output" &&
	    registerer.type != "encoder" && registerer.type != "service")
		return fail(error, "bad_request", "registerer.type is not supported");
	return true;
}

bool read_registerer_handle(obs_data_t *data, HotkeyRegisterer &registerer, bool &present, RuntimeV2Error &error)
{
	if (!read_optional_string(data, "handle", 32, registerer.handle, present, error))
		return false;
	if (present && (!is_canonical_handle(registerer.handle) || registerer.handle.size() > 32))
		return fail(error, "bad_request", "registerer.handle must be a canonical runtime handle");
	return true;
}

bool read_registerer_metadata(obs_data_t *data, HotkeyRegisterer &registerer, bool &present, RuntimeV2Error &error)
{
	if (!read_optional_string(data, "runtimeId", kMaxHotkeyDescriptionBytes, registerer.runtime_id, present, error) ||
	    !read_optional_string(data, "name", kMaxHotkeyNameBytes, registerer.object_name, present, error) ||
	    !read_optional_string(data, "kind", kMaxHotkeyNameBytes, registerer.kind, present, error))
		return false;
	return true;
}

bool read_registerer(obs_data_t *data, HotkeyRegisterer &registerer, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_registerer_type(data, registerer, present, error))
		return false;
	if (!read_registerer_handle(data, registerer, present, error))
		return false;
	return read_registerer_metadata(data, registerer, present, error);
}

bool read_hotkey_selector(obs_data_t *params, HotkeyRegisterer &registerer, std::string &name,
				  RuntimeV2Error &error)
{
	ObsDataPtr hotkey;
	bool present = false;
	obs_data_t *root = params;
	if (!read_object_field(params, "hotkey", hotkey, present))
		return fail(error, "bad_request", "params.hotkey must be an object when present");
	if (present)
		root = hotkey.get();
	ObsDataPtr registerer_object;
	if (!read_object_field(root, "registerer", registerer_object, present) || !present)
		return fail(error, "bad_request", "hotkey.registerer is required");
	if (!read_registerer(registerer_object.get(), registerer, error))
		return false;
	if (!read_string_field(root, "name", name, present) || !present || name.empty() ||
	    name.size() > kMaxHotkeyNameBytes)
		return fail(error, "bad_request", "hotkey.name must be a non-empty bounded string");
	return true;
}

bool parse_key_name(std::string_view name, obs_key_t &key)
{
	if (name.empty() || name.size() > kMaxHotkeyNameBytes)
		return false;
	key = obs_key_from_name(std::string(name).c_str());
	return key != OBS_KEY_NONE && key < OBS_KEY_LAST_VALUE;
}

bool read_modifier_entry(const ObsDataPtr &entry, uint32_t &modifiers, RuntimeV2Error &error)
{
	std::string name;
	bool present = false;
	if (!entry || !read_string_field(entry.get(), "name", name, present))
		return fail(error, "bad_request", "each modifier must contain a name string");
	if (!present && !read_string_field(entry.get(), "value", name, present))
		return fail(error, "bad_request", "each modifier must contain a name string");
	const ModifierDescriptor *modifier = find_modifier(name);
	if (!present || !modifier)
		return fail(error, "bad_request", "modifier is not supported");
	modifiers |= modifier->flag;
	return true;
}

bool read_binding_modifiers(obs_data_t *binding, uint32_t &modifiers, RuntimeV2Error &error)
{
	ObsArrayPtr array;
	bool present = false;
	if (!read_array_field(binding, "modifiers", array, present))
		return fail(error, "bad_request", "binding.modifiers must be an array when present");
	modifiers = 0;
	if (!present)
		return true;
	if (obs_data_array_count(array.get()) > std::size(kModifiers))
		return fail(error, "bad_request", "binding.modifiers contains too many entries");
	for (size_t index = 0; index < obs_data_array_count(array.get()); ++index) {
		ObsDataPtr entry(obs_data_array_item(array.get(), index));
		if (!read_modifier_entry(entry, modifiers, error))
			return false;
	}
	return true;
}

bool read_binding_object(obs_data_t *data, HotkeyBinding &binding, RuntimeV2Error &error)
{
	std::string key_name;
	bool present = false;
	if (!read_string_field(data, "key", key_name, present) || !present || !parse_key_name(key_name, binding.combination.key))
		return fail(error, "bad_request", "binding.key must be a valid OBS semantic key name");
	if (!read_binding_modifiers(data, binding.combination.modifiers, error))
		return false;
	return true;
}

bool binding_equal(const HotkeyBinding &left, const HotkeyBinding &right)
{
	return left.combination.key == right.combination.key &&
	       left.combination.modifiers == right.combination.modifiers;
}

bool binding_less(const HotkeyBinding &left, const HotkeyBinding &right)
{
	if (left.combination.key != right.combination.key)
		return left.combination.key < right.combination.key;
	return left.combination.modifiers < right.combination.modifiers;
}

void normalize_bindings(std::vector<HotkeyBinding> &bindings)
{
	std::sort(bindings.begin(), bindings.end(), binding_less);
	bindings.erase(std::unique(bindings.begin(), bindings.end(), binding_equal), bindings.end());
}

bool read_bindings(obs_data_t *data, std::vector<HotkeyBinding> &bindings, RuntimeV2Error &error)
{
	ObsArrayPtr array;
	bool present = false;
	if (!read_array_field(data, "bindings", array, present) || !present)
		return fail(error, "bad_request", "hotkey.bindings must be an array");
	if (obs_data_array_count(array.get()) > kMaxBindings)
		return fail(error, "bad_request", "hotkey.bindings contains too many entries");
	for (size_t index = 0; index < obs_data_array_count(array.get()); ++index) {
		ObsDataPtr entry(obs_data_array_item(array.get(), index));
		if (!entry)
			return fail(error, "bad_request", "each hotkey binding must be an object");
		HotkeyBinding binding;
		if (!read_binding_object(entry.get(), binding, error))
			return false;
		bindings.push_back(binding);
	}
	normalize_bindings(bindings);
	return true;
}

ObsDataPtr make_binding_data(const HotkeyBinding &binding)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "key", obs_key_to_name(binding.combination.key));
	ObsArrayPtr modifiers(obs_data_array_create());
	for (const ModifierDescriptor &modifier : kModifiers) {
		if ((binding.combination.modifiers & modifier.flag) == 0)
			continue;
		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "name", modifier.name);
		obs_data_array_push_back(modifiers.get(), entry.get());
	}
	obs_data_set_array(data.get(), "modifiers", modifiers.get());
	return data;
}

ObsArrayPtr make_binding_array(const std::vector<HotkeyBinding> &bindings)
{
	ObsArrayPtr array(obs_data_array_create());
	for (const HotkeyBinding &binding : bindings) {
		ObsDataPtr entry = make_binding_data(binding);
		obs_data_array_push_back(array.get(), entry.get());
	}
	return array;
}

std::vector<HotkeyBinding> read_saved_bindings(obs_hotkey_id id)
{
	std::vector<HotkeyBinding> bindings;
	ObsArrayPtr array(obs_hotkey_save(id));
	if (!array)
		return bindings;
	for (size_t index = 0; index < obs_data_array_count(array.get()); ++index) {
		ObsDataPtr entry(obs_data_array_item(array.get(), index));
		if (!entry)
			continue;
		HotkeyBinding binding;
		binding.combination.key = obs_key_from_name(obs_data_get_string(entry.get(), "key"));
		if (binding.combination.key == OBS_KEY_NONE || binding.combination.key >= OBS_KEY_LAST_VALUE)
			continue;
		if (obs_data_get_bool(entry.get(), "shift"))
			binding.combination.modifiers |= INTERACT_SHIFT_KEY;
		if (obs_data_get_bool(entry.get(), "control"))
			binding.combination.modifiers |= INTERACT_CONTROL_KEY;
		if (obs_data_get_bool(entry.get(), "alt"))
			binding.combination.modifiers |= INTERACT_ALT_KEY;
		if (obs_data_get_bool(entry.get(), "command"))
			binding.combination.modifiers |= INTERACT_COMMAND_KEY;
		bindings.push_back(binding);
	}
	normalize_bindings(bindings);
	return bindings;
}

ObsArrayPtr make_libobs_binding_array(const std::vector<HotkeyBinding> &bindings)
{
	ObsArrayPtr array(obs_data_array_create());
	for (const HotkeyBinding &binding : bindings) {
		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "key", obs_key_to_name(binding.combination.key));
		for (const ModifierDescriptor &modifier : kModifiers)
			if ((binding.combination.modifiers & modifier.flag) != 0)
				obs_data_set_bool(entry.get(), modifier.name, true);
		obs_data_array_push_back(array.get(), entry.get());
	}
	return array;
}

const char *registerer_type_name(obs_hotkey_registerer_t type)
{
	switch (type) {
	case OBS_HOTKEY_REGISTERER_FRONTEND:
		return "frontend";
	case OBS_HOTKEY_REGISTERER_SOURCE:
		return "source";
	case OBS_HOTKEY_REGISTERER_OUTPUT:
		return "output";
	case OBS_HOTKEY_REGISTERER_ENCODER:
		return "encoder";
	case OBS_HOTKEY_REGISTERER_SERVICE:
		return "service";
	}
	return "unknown";
}

void fill_source_registerer(Engine &engine, obs_weak_source_t *weak, HotkeyRegisterer &registerer)
{
	obs_source_t *source = weak ? obs_weak_source_get_source(weak) : nullptr;
	if (!source)
		return;
	const uint64_t handle = engine.v2_source_handle_for_pointer(source);
	if (handle != 0)
		registerer.handle = std::to_string(handle);
	const char *uuid = obs_source_get_uuid(source);
	if (uuid && registerer.handle.empty())
		registerer.runtime_id = uuid;
	registerer.object_name = obs_source_get_name(source) ? obs_source_get_name(source) : "";
	registerer.kind = obs_source_get_id(source) ? obs_source_get_id(source) : "";
	obs_source_release(source);
}

void fill_output_registerer(obs_weak_output_t *weak, HotkeyRegisterer &registerer)
{
	obs_output_t *output = weak ? obs_weak_output_get_output(weak) : nullptr;
	if (!output)
		return;
	registerer.object_name = obs_output_get_name(output) ? obs_output_get_name(output) : "";
	registerer.kind = obs_output_get_id(output) ? obs_output_get_id(output) : "";
	obs_output_release(output);
}

void fill_encoder_registerer(obs_weak_encoder_t *weak, HotkeyRegisterer &registerer)
{
	obs_encoder_t *encoder = weak ? obs_weak_encoder_get_encoder(weak) : nullptr;
	if (!encoder)
		return;
	registerer.object_name = obs_encoder_get_name(encoder) ? obs_encoder_get_name(encoder) : "";
	registerer.kind = obs_encoder_get_id(encoder) ? obs_encoder_get_id(encoder) : "";
	obs_encoder_release(encoder);
}

void fill_service_registerer(obs_weak_service_t *weak, HotkeyRegisterer &registerer)
{
	obs_service_t *service = weak ? obs_weak_service_get_service(weak) : nullptr;
	if (!service)
		return;
	registerer.object_name = obs_service_get_name(service) ? obs_service_get_name(service) : "";
	registerer.kind = obs_service_get_id(service) ? obs_service_get_id(service) : "";
	obs_service_release(service);
}

HotkeyRegisterer get_registerer(Engine &engine, const obs_hotkey_t *hotkey)
{
	HotkeyRegisterer registerer;
	const obs_hotkey_registerer_t type = obs_hotkey_get_registerer_type(hotkey);
	registerer.type = registerer_type_name(type);
	void *value = obs_hotkey_get_registerer(hotkey);
	switch (type) {
	case OBS_HOTKEY_REGISTERER_SOURCE:
		fill_source_registerer(engine, static_cast<obs_weak_source_t *>(value), registerer);
		break;
	case OBS_HOTKEY_REGISTERER_OUTPUT:
		fill_output_registerer(static_cast<obs_weak_output_t *>(value), registerer);
		break;
	case OBS_HOTKEY_REGISTERER_ENCODER:
		fill_encoder_registerer(static_cast<obs_weak_encoder_t *>(value), registerer);
		break;
	case OBS_HOTKEY_REGISTERER_SERVICE:
		fill_service_registerer(static_cast<obs_weak_service_t *>(value), registerer);
		break;
	case OBS_HOTKEY_REGISTERER_FRONTEND:
		break;
	}
	return registerer;
}

bool enumerate_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey)
{
	auto *context = static_cast<HotkeyEnumerationContext *>(data);
	try {
		HotkeySnapshot snapshot;
		snapshot.id = id;
		snapshot.pair_partner = obs_hotkey_get_pair_partner_id(hotkey);
		snapshot.name = obs_hotkey_get_name(hotkey) ? obs_hotkey_get_name(hotkey) : "";
		snapshot.description = obs_hotkey_get_description(hotkey) ? obs_hotkey_get_description(hotkey) : "";
		snapshot.registerer = get_registerer(*context->engine, hotkey);
		snapshot.bindings = read_saved_bindings(id);
		context->snapshots->push_back(std::move(snapshot));
		return true;
	} catch (...) {
		context->failed = true;
		return false;
	}
}

bool hotkey_snapshot_less(const HotkeySnapshot &left, const HotkeySnapshot &right)
{
	if (left.registerer.type != right.registerer.type)
		return left.registerer.type < right.registerer.type;
	if (left.registerer.handle != right.registerer.handle)
		return left.registerer.handle < right.registerer.handle;
	if (left.registerer.runtime_id != right.registerer.runtime_id)
		return left.registerer.runtime_id < right.registerer.runtime_id;
	if (left.registerer.object_name != right.registerer.object_name)
		return left.registerer.object_name < right.registerer.object_name;
	if (left.name != right.name)
		return left.name < right.name;
	return left.id < right.id;
}

bool get_hotkey_snapshots(Engine &engine, std::vector<HotkeySnapshot> &snapshots, RuntimeV2Error &error)
{
	HotkeyEnumerationContext context{&engine, &snapshots, false};
	obs_enum_hotkeys(enumerate_hotkey, &context);
	if (context.failed)
		return fail(error, "internal_error", "could not enumerate libobs hotkeys");
	for (HotkeySnapshot &snapshot : snapshots) {
		const uint64_t source_handle = engine.v2_hotkey_owned_source_handle(snapshot.id);
		if (source_handle == 0)
			continue;
		snapshot.registerer = HotkeyRegisterer{"source", std::to_string(source_handle), {}, {}, {}};
	}
	std::sort(snapshots.begin(), snapshots.end(), hotkey_snapshot_less);
	return true;
}

bool registerer_matches(const HotkeyRegisterer &actual, const HotkeyRegisterer &selector)
{
	if (actual.type != selector.type)
		return false;
	if (!selector.handle.empty())
		return actual.handle == selector.handle;
	if (!selector.runtime_id.empty())
		return actual.runtime_id == selector.runtime_id;
	if (!selector.object_name.empty())
		return actual.object_name == selector.object_name &&
		       (selector.kind.empty() || actual.kind == selector.kind);
	return actual.type == "frontend";
}

std::vector<const HotkeySnapshot *> find_hotkeys(const std::vector<HotkeySnapshot> &snapshots,
						 const HotkeyRegisterer &registerer, std::string_view name)
{
	std::vector<const HotkeySnapshot *> matches;
	for (const HotkeySnapshot &snapshot : snapshots)
		if (snapshot.name == name && registerer_matches(snapshot.registerer, registerer))
			matches.push_back(&snapshot);
	return matches;
}

const HotkeySnapshot *find_hotkey_by_id(const std::vector<HotkeySnapshot> &snapshots, obs_hotkey_id id)
{
	for (const HotkeySnapshot &snapshot : snapshots)
		if (snapshot.id == id)
			return &snapshot;
	return nullptr;
}

void set_registerer_data(obs_data_t *data, const HotkeyRegisterer &registerer)
{
	ObsDataPtr value(obs_data_create());
	obs_data_set_string(value.get(), "type", registerer.type.c_str());
	if (!registerer.handle.empty())
		obs_data_set_string(value.get(), "handle", registerer.handle.c_str());
	if (!registerer.runtime_id.empty())
		obs_data_set_string(value.get(), "runtimeId", registerer.runtime_id.c_str());
	if (!registerer.object_name.empty())
		obs_data_set_string(value.get(), "name", registerer.object_name.c_str());
	if (!registerer.kind.empty())
		obs_data_set_string(value.get(), "kind", registerer.kind.c_str());
	obs_data_set_obj(data, "registerer", value.get());
}

ObsDataPtr make_hotkey_data(const HotkeySnapshot &snapshot, const std::vector<HotkeySnapshot> &snapshots,
				    bool include_bindings)
{
	ObsDataPtr data(obs_data_create());
	set_registerer_data(data.get(), snapshot.registerer);
	obs_data_set_string(data.get(), "name", snapshot.name.c_str());
	obs_data_set_string(data.get(), "description", snapshot.description.c_str());
	if (const HotkeySnapshot *partner = find_hotkey_by_id(snapshots, snapshot.pair_partner)) {
		ObsDataPtr pair(obs_data_create());
		set_registerer_data(pair.get(), partner->registerer);
		obs_data_set_string(pair.get(), "name", partner->name.c_str());
		obs_data_set_obj(data.get(), "pairPartner", pair.get());
	}
	if (include_bindings) {
		ObsArrayPtr bindings = make_binding_array(snapshot.bindings);
		obs_data_set_array(data.get(), "bindings", bindings.get());
	}
	return data;
}

ObsDataPtr make_hotkey_identity_data(const HotkeySnapshot &snapshot)
{
	ObsDataPtr data(obs_data_create());
	set_registerer_data(data.get(), snapshot.registerer);
	obs_data_set_string(data.get(), "name", snapshot.name.c_str());
	return data;
}

ObsDataPtr make_binding_changed_data(const HotkeySnapshot &snapshot, const std::vector<HotkeyBinding> &bindings)
{
	ObsDataPtr data = make_hotkey_identity_data(snapshot);
	ObsArrayPtr values = make_binding_array(bindings);
	obs_data_set_array(data.get(), "bindings", values.get());
	return data;
}

bool resolve_single_hotkey(Engine &engine, obs_data_t *params, HotkeySnapshot &snapshot,
				  std::vector<HotkeySnapshot> *all, RuntimeV2Error &error)
{
	HotkeyRegisterer registerer;
	std::string name;
	if (!read_hotkey_selector(params, registerer, name, error))
		return false;
	std::vector<HotkeySnapshot> snapshots;
	if (!get_hotkey_snapshots(engine, snapshots, error))
		return false;
	const auto matches = find_hotkeys(snapshots, registerer, name);
	if (matches.empty())
		return fail(error, "not_found", "hotkey was not found");
	if (matches.size() != 1)
		return fail(error, "already_exists", "hotkey selector is ambiguous");
	snapshot = *matches.front();
	if (all)
		*all = std::move(snapshots);
	return true;
}

bool has_binding_conflict(const std::vector<HotkeySnapshot> &snapshots, obs_hotkey_id except,
				 const std::vector<HotkeyBinding> &bindings)
{
	for (const HotkeySnapshot &snapshot : snapshots) {
		if (snapshot.id == except)
			continue;
		for (const HotkeyBinding &candidate : snapshot.bindings)
			if (std::any_of(bindings.begin(), bindings.end(), [&](const HotkeyBinding &binding) {
				    return binding_equal(binding, candidate);
			    }))
				return true;
	}
	return false;
}

bool has_import_conflict(const std::vector<BindingUpdate> &updates, obs_hotkey_id except,
				const std::vector<HotkeyBinding> &bindings)
{
	for (const BindingUpdate &update : updates) {
		if (update.snapshot.id == except)
			continue;
		if (std::any_of(update.bindings.begin(), update.bindings.end(), [&](const HotkeyBinding &candidate) {
				return std::any_of(bindings.begin(), bindings.end(),
						  [&](const HotkeyBinding &binding) { return binding_equal(binding, candidate); });
			}))
			return true;
	}
	return false;
}

bool apply_binding_update(const HotkeySnapshot &snapshot, const std::vector<HotkeyBinding> &bindings,
				  RuntimeV2Result &result)
{
	if (std::equal(snapshot.bindings.begin(), snapshot.bindings.end(), bindings.begin(), bindings.end(), binding_equal))
		return true;
	ObsArrayPtr array = make_libobs_binding_array(bindings);
	obs_hotkey_load(snapshot.id, array.get());
	append_event(result, "hotkey.bindingsChanged", make_binding_changed_data(snapshot, bindings));
	result.mutated = true;
	return true;
}

void queue_hotkey_trigger(HotkeyV2State &state, obs_hotkey_id id, bool pressed) noexcept
{
	if (!state.mutex.try_lock())
		return;
	if (state.accepting) {
		if (state.triggers.size() < kMaxTriggerQueue)
			state.triggers.push_back(HotkeyTrigger{id, pressed});
	}
	state.mutex.unlock();
}

void engine_audio_mute_hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *context = static_cast<HotkeyOwnedContext *>(data);
	if (context && context->engine)
		context->engine->v2_hotkey_toggle_audio_mute(context->source_handle);
}

void hotkey_router_callback(void *data, obs_hotkey_id id, bool pressed)
{
	auto *state = static_cast<HotkeyV2State *>(data);
	if (state)
		queue_hotkey_trigger(*state, id, pressed);
	obs_hotkey_trigger_routed_callback(id, pressed);
}

bool take_hotkey_triggers(HotkeyV2State &state, std::vector<HotkeyTrigger> &triggers,
				  RevisionState *&revisions, EventDispatcher *&events, Engine *&engine)
{
	{
		std::lock_guard lock(state.mutex);
		if (!state.accepting)
			return false;
		while (!state.triggers.empty()) {
			triggers.push_back(state.triggers.front());
			state.triggers.pop_front();
		}
		revisions = state.revisions;
		events = state.events;
		engine = state.engine;
	}
	return !triggers.empty() && revisions && events && engine;
}

void publish_hotkey_trigger(const HotkeyTrigger &trigger, const std::vector<HotkeySnapshot> &snapshots,
				    RevisionState &revisions, EventDispatcher &events)
{
	const HotkeySnapshot *snapshot = find_hotkey_by_id(snapshots, trigger.id);
	if (!snapshot)
		return;
	ObsDataPtr payload = make_hotkey_identity_data(*snapshot);
	obs_data_set_bool(payload.get(), "pressed", trigger.pressed);
	events.try_publish_telemetry("hotkey.triggered", revisions.current(), payload.get());
}

void hotkey_tick(void *data, float)
{
	auto *state = static_cast<HotkeyV2State *>(data);
	if (!state)
		return;
	std::vector<HotkeyTrigger> triggers;
	RevisionState *revisions = nullptr;
	EventDispatcher *events = nullptr;
	Engine *engine = nullptr;
	if (!take_hotkey_triggers(*state, triggers, revisions, events, engine))
		return;
	std::vector<HotkeySnapshot> snapshots;
	RuntimeV2Error error;
	if (!get_hotkey_snapshots(*engine, snapshots, error))
		return;
	for (const HotkeyTrigger &trigger : triggers)
		publish_hotkey_trigger(trigger, snapshots, *revisions, *events);
}

std::string key_combination_name(obs_key_combination_t combination)
{
	struct dstr text = {};
	obs_key_combination_to_str(combination, &text);
	std::string result = text.array ? text.array : "";
	dstr_free(&text);
	return result;
}

bool read_combination_param(obs_data_t *params, HotkeyBinding &binding, RuntimeV2Error &error)
{
	ObsDataPtr object;
	bool present = false;
	obs_data_t *root = params;
	if (!read_object_field(params, "binding", object, present))
		return fail(error, "bad_request", "params.binding must be an object when present");
	if (present)
		root = object.get();
	return read_binding_object(root, binding, error);
}

bool read_action(obs_data_t *params, std::string &action, RuntimeV2Error &error)
{
	bool present = false;
	if (!read_string_field(params, "action", action, present) || !present)
		return fail(error, "bad_request", "params.action must be press, release, or click");
	if (action != "press" && action != "release" && action != "click")
		return fail(error, "bad_request", "params.action must be press, release, or click");
	return true;
}

bool read_import_update(const ObsDataPtr &entry, const std::vector<HotkeySnapshot> &snapshots,
				std::unordered_set<obs_hotkey_id> &seen, std::vector<BindingUpdate> &updates,
				RuntimeV2Error &error)
{
	if (!entry)
		return fail(error, "bad_request", "each imported hotkey must be an object");
	HotkeyRegisterer registerer;
	std::string name;
	if (!read_hotkey_selector(entry.get(), registerer, name, error))
		return false;
	const auto matches = find_hotkeys(snapshots, registerer, name);
	if (matches.empty())
		return fail(error, "not_found", "an imported hotkey was not found");
	if (matches.size() != 1 || !seen.insert(matches.front()->id).second)
		return fail(error, "already_exists", "import contains an ambiguous or duplicate hotkey");
	std::vector<HotkeyBinding> bindings;
	if (!read_bindings(entry.get(), bindings, error))
		return false;
	if (has_binding_conflict(snapshots, matches.front()->id, bindings) ||
	    has_import_conflict(updates, matches.front()->id, bindings))
		return fail(error, "already_exists", "imported hotkey bindings conflict with another hotkey");
	updates.push_back(BindingUpdate{*matches.front(), std::move(bindings)});
	return true;
}

bool can_register_audio_hotkey(HotkeyV2State &state, uint64_t source_handle)
{
	std::lock_guard lock(state.mutex);
	if (!state.accepting || !state.engine)
		return false;
	for (const auto &[_, context] : state.owned_audio_hotkeys)
		if (context && context->source_handle == source_handle)
			return false;
	return true;
}

bool claim_audio_hotkey(HotkeyV2State &state, const std::unordered_map<uint64_t, obs_source_t *> &sources,
			obs_hotkey_id id, std::shared_ptr<HotkeyOwnedContext> &context, uint64_t source_handle)
{
	std::lock_guard lock(state.mutex);
	if (!state.accepting || !sources.contains(source_handle) || state.owned_audio_hotkeys.contains(id))
		return false;
	state.owned_audio_hotkeys.emplace(id, std::move(context));
	return true;
}

} // namespace

void Engine::v2_bind_hotkey_events(RevisionState *revisions, EventDispatcher *events)
{
	if (!hotkey_v2_state_)
		hotkey_v2_state_ = std::make_shared<HotkeyV2State>();
	{
		std::lock_guard lock(hotkey_v2_state_->mutex);
		hotkey_v2_state_->engine = this;
		hotkey_v2_state_->revisions = revisions;
		hotkey_v2_state_->events = events;
		hotkey_v2_state_->accepting = revisions && events;
		hotkey_v2_state_->background_capture = true;
	}
	if (!hotkey_v2_state_->accepting)
		return;
	obs_hotkey_set_callback_routing_func(hotkey_router_callback, hotkey_v2_state_.get());
	obs_hotkey_enable_callback_rerouting(true);
	obs_hotkey_enable_background_press(true);
	if (!hotkey_v2_state_->tick_registered) {
		obs_add_tick_callback(hotkey_tick, hotkey_v2_state_.get());
		hotkey_v2_state_->tick_registered = true;
	}
}

void Engine::v2_register_audio_hotkey(uint64_t source_handle)
{
	if (!hotkey_v2_state_ || !can_register_audio_hotkey(*hotkey_v2_state_, source_handle))
		return;
	auto context = std::make_shared<HotkeyOwnedContext>();
	context->engine = this;
	context->source_handle = source_handle;
	const std::string name = "engine.source." + std::to_string(source_handle) + ".toggleMute";
	const obs_hotkey_id id = obs_hotkey_register_frontend(name.c_str(), "Toggle source mute", engine_audio_mute_hotkey,
									 context.get());
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	if (!claim_audio_hotkey(*hotkey_v2_state_, sources_, id, context, source_handle))
		obs_hotkey_unregister(id);
}

void Engine::v2_forget_audio_hotkey(uint64_t source_handle) noexcept
{
	if (!hotkey_v2_state_)
		return;
	obs_hotkey_id found = OBS_INVALID_HOTKEY_ID;
	{
		std::lock_guard lock(hotkey_v2_state_->mutex);
		for (auto it = hotkey_v2_state_->owned_audio_hotkeys.begin();
		     it != hotkey_v2_state_->owned_audio_hotkeys.end(); ++it) {
			if (!it->second || it->second->source_handle != source_handle)
				continue;
			found = it->first;
			hotkey_v2_state_->owned_audio_hotkeys.erase(it);
			break;
		}
	}
	if (found != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(found);
}

uint64_t Engine::v2_hotkey_owned_source_handle(size_t hotkey_id) const
{
	if (!hotkey_v2_state_)
		return 0;
	std::lock_guard lock(hotkey_v2_state_->mutex);
	auto it = hotkey_v2_state_->owned_audio_hotkeys.find(hotkey_id);
	return it == hotkey_v2_state_->owned_audio_hotkeys.end() || !it->second ? 0 : it->second->source_handle;
}

void Engine::v2_hotkey_toggle_audio_mute(uint64_t source_handle) noexcept
{
	auto source_it = sources_.find(source_handle);
	if (source_it == sources_.end() || !source_it->second)
		return;
	obs_source_set_muted(source_it->second, !obs_source_muted(source_it->second));
}

void Engine::v2_prepare_hotkey_shutdown() noexcept
{
	if (!hotkey_v2_state_)
		return;
	if (hotkey_v2_state_->tick_registered) {
		obs_remove_tick_callback(hotkey_tick, hotkey_v2_state_.get());
		hotkey_v2_state_->tick_registered = false;
	}
	std::vector<obs_hotkey_id> owned_ids;
	{
		std::lock_guard lock(hotkey_v2_state_->mutex);
		hotkey_v2_state_->accepting = false;
		hotkey_v2_state_->triggers.clear();
		owned_ids.reserve(hotkey_v2_state_->owned_audio_hotkeys.size());
		for (const auto &[id, _] : hotkey_v2_state_->owned_audio_hotkeys)
			owned_ids.push_back(id);
		hotkey_v2_state_->owned_audio_hotkeys.clear();
		hotkey_v2_state_->revisions = nullptr;
		hotkey_v2_state_->events = nullptr;
	}
	for (obs_hotkey_id id : owned_ids)
		obs_hotkey_unregister(id);
	obs_hotkey_enable_callback_rerouting(false);
	obs_hotkey_set_callback_routing_func(nullptr, nullptr);
}

bool Engine::v2_hotkey_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<HotkeySnapshot> snapshots;
	if (!get_hotkey_snapshots(*this, snapshots, error))
		return false;
	ObsArrayPtr array(obs_data_array_create());
	for (const HotkeySnapshot &snapshot : snapshots) {
		ObsDataPtr entry = make_hotkey_data(snapshot, snapshots, false);
		obs_data_array_push_back(array.get(), entry.get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "hotkeys", array.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(snapshots.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	HotkeySnapshot snapshot;
	std::vector<HotkeySnapshot> snapshots;
	if (!resolve_single_hotkey(*this, params, snapshot, &snapshots, error))
		return false;
	result.data = make_hotkey_data(snapshot, snapshots, true);
	return true;
}

bool Engine::v2_hotkey_get_bindings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	HotkeySnapshot snapshot;
	std::vector<HotkeySnapshot> snapshots;
	if (!resolve_single_hotkey(*this, params, snapshot, &snapshots, error))
		return false;
	ObsDataPtr data = make_hotkey_data(snapshot, snapshots, false);
	ObsArrayPtr bindings = make_binding_array(snapshot.bindings);
	obs_data_set_array(data.get(), "bindings", bindings.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_set_bindings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	HotkeySnapshot snapshot;
	std::vector<HotkeySnapshot> snapshots;
	if (!resolve_single_hotkey(*this, params, snapshot, &snapshots, error))
		return false;
	std::vector<HotkeyBinding> bindings;
	if (!read_bindings(params, bindings, error))
		return false;
	if (has_binding_conflict(snapshots, snapshot.id, bindings))
		return fail(error, "already_exists", "one or more hotkey bindings conflict with another hotkey");
	if (!apply_binding_update(snapshot, bindings, result))
		return false;
	snapshot.bindings = read_saved_bindings(snapshot.id);
	result.data = make_hotkey_data(snapshot, snapshots, true);
	return true;
}

bool Engine::v2_hotkey_clear_bindings(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	HotkeySnapshot snapshot;
	std::vector<HotkeySnapshot> snapshots;
	if (!resolve_single_hotkey(*this, params, snapshot, &snapshots, error))
		return false;
	const std::vector<HotkeyBinding> bindings;
	if (!apply_binding_update(snapshot, bindings, result))
		return false;
	snapshot.bindings.clear();
	result.data = make_hotkey_data(snapshot, snapshots, true);
	return true;
}

bool Engine::v2_hotkey_trigger(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!hotkey_v2_state_)
		return fail(error, "not_available", "hotkey callback routing is not available");
	HotkeySnapshot snapshot;
	std::vector<HotkeySnapshot> snapshots;
	if (!resolve_single_hotkey(*this, params, snapshot, &snapshots, error))
		return false;
	std::string action;
	if (!read_action(params, action, error))
		return false;
	const auto trigger = [&](bool pressed) {
		queue_hotkey_trigger(*hotkey_v2_state_, snapshot.id, pressed);
		obs_hotkey_trigger_routed_callback(snapshot.id, pressed);
	};
	if (action == "press" || action == "click")
		trigger(true);
	if (action == "release" || action == "click")
		trigger(false);
	ObsDataPtr data = make_hotkey_data(snapshot, snapshots, false);
	obs_data_set_string(data.get(), "action", action.c_str());
	result.data = std::move(data);
	result.mutated = !result.events.empty();
	return true;
}

bool Engine::v2_hotkey_get_key_name(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::string key_name;
	bool present = false;
	if (!read_string_field(params, "key", key_name, present) || !present)
		return fail(error, "bad_request", "params.key must be a semantic key name");
	obs_key_t key = OBS_KEY_NONE;
	if (!parse_key_name(key_name, key))
		return fail(error, "bad_request", "params.key is not a valid OBS semantic key name");
	ObsDataPtr data(obs_data_create());
	obs_data_set_string(data.get(), "key", obs_key_to_name(key));
	obs_data_set_string(data.get(), "name", key_combination_name(obs_key_combination_t{0, key}).c_str());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_get_key_combination_name(obs_data_t *params, RuntimeV2Result &result,
						RuntimeV2Error &error)
{
	reset_result(result, error);
	HotkeyBinding binding;
	if (!read_combination_param(params, binding, error))
		return false;
	ObsDataPtr data = make_binding_data(binding);
	obs_data_set_string(data.get(), "name", key_combination_name(binding.combination).c_str());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_get_conflicts(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	HotkeyBinding requested;
	if (!read_combination_param(params, requested, error))
		return false;
	std::vector<HotkeySnapshot> snapshots;
	if (!get_hotkey_snapshots(*this, snapshots, error))
		return false;
	ObsArrayPtr conflicts(obs_data_array_create());
	for (const HotkeySnapshot &snapshot : snapshots) {
		if (std::any_of(snapshot.bindings.begin(), snapshot.bindings.end(),
				[&](const HotkeyBinding &binding) { return binding_equal(binding, requested); })) {
			ObsDataPtr entry = make_hotkey_data(snapshot, snapshots, false);
			obs_data_array_push_back(conflicts.get(), entry.get());
		}
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "conflicts", conflicts.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(obs_data_array_count(conflicts.get())));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_get_background_capture(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!hotkey_v2_state_)
		return fail(error, "not_available", "hotkey callback routing is not available");
	bool enabled = false;
	{
		std::lock_guard lock(hotkey_v2_state_->mutex);
		enabled = hotkey_v2_state_->background_capture;
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "enabled", enabled);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_set_background_capture(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	if (!hotkey_v2_state_)
		return fail(error, "not_available", "hotkey callback routing is not available");
	bool enabled = false;
	bool present = false;
	if (!read_bool_field(params, "enabled", enabled, present) || !present)
		return fail(error, "bad_request", "params.enabled must be a boolean");
	bool before = false;
	{
		std::lock_guard lock(hotkey_v2_state_->mutex);
		before = hotkey_v2_state_->background_capture;
		hotkey_v2_state_->background_capture = enabled;
	}
	obs_hotkey_enable_background_press(enabled);
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "enabled", enabled);
	result.data = std::move(data);
	result.mutated = before != enabled;
	if (result.mutated)
		append_event(result, "hotkey.backgroundCaptureChanged", [&] {
			ObsDataPtr event(obs_data_create());
			obs_data_set_bool(event.get(), "enabled", enabled);
			return event;
		}());
	return true;
}

bool Engine::v2_hotkey_export(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<HotkeySnapshot> snapshots;
	if (!get_hotkey_snapshots(*this, snapshots, error))
		return false;
	ObsArrayPtr array(obs_data_array_create());
	for (const HotkeySnapshot &snapshot : snapshots) {
		ObsDataPtr entry = make_hotkey_data(snapshot, snapshots, true);
		obs_data_array_push_back(array.get(), entry.get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "hotkeys", array.get());
	obs_data_set_int(data.get(), "count", static_cast<long long>(snapshots.size()));
	result.data = std::move(data);
	return true;
}

bool Engine::v2_hotkey_import(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	ObsArrayPtr array;
	bool present = false;
	if (!read_array_field(params, "hotkeys", array, present) || !present)
		return fail(error, "bad_request", "params.hotkeys must be an array");
	if (obs_data_array_count(array.get()) > kMaxImportedHotkeys)
		return fail(error, "bad_request", "params.hotkeys contains too many entries");
	std::vector<HotkeySnapshot> snapshots;
	if (!get_hotkey_snapshots(*this, snapshots, error))
		return false;
	std::vector<BindingUpdate> updates;
	std::unordered_set<obs_hotkey_id> seen;
	for (size_t index = 0; index < obs_data_array_count(array.get()); ++index) {
		ObsDataPtr entry(obs_data_array_item(array.get(), index));
		if (!read_import_update(entry, snapshots, seen, updates, error))
			return false;
	}
	for (const BindingUpdate &update : updates)
		if (!apply_binding_update(update.snapshot, update.bindings, result))
			return false;
	ObsDataPtr data(obs_data_create());
	obs_data_set_int(data.get(), "count", static_cast<long long>(updates.size()));
	result.data = std::move(data);
	return true;
}

} // namespace obs_engine
