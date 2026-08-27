#include "protocol_v2.hpp"

#include "validation.hpp"

#include <windows.h>

#include <obs.h>

namespace obs_engine {
namespace {

struct CapabilityDescriptor {
	const char *name;
	bool experimental;
};

constexpr CapabilityDescriptor kCapabilities[] = {
	{"engine.capabilities.v1", false},
	{"session.close.v1", false},
	{"session.hello.v1", false},
	{"session.ping.v1", false},
};

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

void set_parse_error(V2ParseError &error, const std::string &id, const char *code, const char *message)
{
	error.id = id;
	error.code = code;
	error.message = message;
}

ObsArrayPtr make_capabilities_array()
{
	ObsArrayPtr capabilities(obs_data_array_create());
	for (const CapabilityDescriptor &descriptor : kCapabilities) {
		ObsDataPtr capability(obs_data_create());
		obs_data_set_string(capability.get(), "name", descriptor.name);
		obs_data_set_bool(capability.get(), "experimental", descriptor.experimental);
		obs_data_array_push_back(capabilities.get(), capability.get());
	}
	return capabilities;
}

void set_capabilities(obs_data_t *data)
{
	ObsArrayPtr capabilities = make_capabilities_array();
	obs_data_set_array(data, "capabilities", capabilities.get());
}

} // namespace

bool parse_v2_request(obs_data_t *request, V2Request &out, V2ParseError &error)
{
	out = V2Request{};
	error = V2ParseError{};

	bool present = false;
	if (!read_string_field(request, "id", out.id, present) || !present ||
	    !is_safe_identifier(out.id.c_str(), kMaxV2RequestIdBytes)) {
		set_parse_error(error, "", "bad_request",
				"id must be a 1-128 byte token using only ASCII letters, digits, '_', '-' or '.'");
		return false;
	}

	std::string op;
	if (!read_string_field(request, "op", op, present) || !present || op != "request") {
		set_parse_error(error, out.id, "bad_request", "op must be the string 'request'");
		return false;
	}

	if (!read_string_field(request, "method", out.method, present) || !present ||
	    !is_safe_identifier(out.method.c_str(), kMaxV2MethodBytes)) {
		set_parse_error(error, out.id, "bad_request",
				"method must be a 1-128 byte protocol identifier");
		return false;
	}

	if (!read_object_field(request, "params", out.params, present)) {
		set_parse_error(error, out.id, "bad_request", "params must be an object when present");
		return false;
	}
	if (!present)
		out.params.reset(obs_data_create());

	if (!read_integer(request, "ifRevision", out.if_revision, out.has_if_revision) ||
	    (out.has_if_revision && out.if_revision < 0)) {
		set_parse_error(error, out.id, "bad_request", "ifRevision must be a non-negative integer");
		return false;
	}

	if (!read_integer(request, "timeoutMs", out.timeout_ms, out.has_timeout_ms) ||
	    (out.has_timeout_ms && out.timeout_ms <= 0)) {
		set_parse_error(error, out.id, "bad_request", "timeoutMs must be a positive integer");
		return false;
	}

	return true;
}

void send_v2_error(const std::string &request_id, const char *code, const char *message, obs_data_t *details,
		   uint64_t revision)
{
	ObsDataPtr response(obs_data_create());
	ObsDataPtr status(obs_data_create());
	ObsDataPtr empty_details;

	obs_data_set_string(response.get(), "op", "response");
	obs_data_set_string(response.get(), "id", request_id.c_str());
	obs_data_set_bool(status.get(), "ok", false);
	obs_data_set_string(status.get(), "code", code ? code : "internal_error");
	obs_data_set_string(status.get(), "message", message ? message : "request failed");
	if (!details) {
		empty_details.reset(obs_data_create());
		details = empty_details.get();
	}
	obs_data_set_obj(status.get(), "details", details);
	obs_data_set_obj(response.get(), "status", status.get());
	obs_data_set_int(response.get(), "revision", static_cast<long long>(revision));
	write_json(response.get());
}

void send_v2_ok(const std::string &request_id, obs_data_t *data, uint64_t revision)
{
	ObsDataPtr response(obs_data_create());
	ObsDataPtr status(obs_data_create());
	ObsDataPtr empty_data;

	obs_data_set_string(response.get(), "op", "response");
	obs_data_set_string(response.get(), "id", request_id.c_str());
	obs_data_set_bool(status.get(), "ok", true);
	obs_data_set_obj(response.get(), "status", status.get());
	obs_data_set_int(response.get(), "revision", static_cast<long long>(revision));
	if (!data) {
		empty_data.reset(obs_data_create());
		data = empty_data.get();
	}
	obs_data_set_obj(response.get(), "data", data);
	write_json(response.get());
}

bool handle_v2_request(const Config &, const V2Request &request)
{
	if (request.method == "session.hello") {
		ObsDataPtr data(obs_data_create());
		ObsDataPtr protocol(obs_data_create());

		obs_data_set_int(protocol.get(), "major", kProtocolV2Major);
		obs_data_set_int(protocol.get(), "minor", kProtocolV2Minor);
		obs_data_set_obj(data.get(), "protocol", protocol.get());
		obs_data_set_string(data.get(), "engineVersion", obs_get_version_string());
		obs_data_set_string(data.get(), "libobsVersion", obs_get_version_string());
		obs_data_set_string(data.get(), "platform", "windows");
		obs_data_set_int(data.get(), "pid", static_cast<long long>(GetCurrentProcessId()));
		obs_data_set_string(data.get(), "encoding", "utf-8");
		obs_data_set_int(data.get(), "maxMessageBytes", static_cast<long long>(kMaxMessageBytes));
		set_capabilities(data.get());
		obs_data_set_int(data.get(), "revision", 0);
		send_v2_ok(request.id, data.get());
		return true;
	}

	if (request.method == "session.ping") {
		ObsDataPtr data(obs_data_create());
		obs_data_set_bool(data.get(), "pong", true);
		send_v2_ok(request.id, data.get());
		return true;
	}

	if (request.method == "session.close") {
		send_v2_ok(request.id);
		return false;
	}

	if (request.method == "engine.getCapabilities") {
		ObsDataPtr data(obs_data_create());
		set_capabilities(data.get());
		send_v2_ok(request.id, data.get());
		return true;
	}

	send_v2_error(request.id, "unsupported_method", "method is not implemented by protocol v2 yet");
	return true;
}

} // namespace obs_engine
