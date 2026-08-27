#include "protocol_v2.hpp"

#include "revision.hpp"
#include "validation.hpp"

#include <windows.h>

#include <obs.h>

#include <string_view>

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

enum class V2Method {
	SessionHello,
	SessionPing,
	SessionClose,
	EngineGetCapabilities,
	Unknown,
};

V2Method classify_method(std::string_view method)
{
	if (method == "session.hello")
		return V2Method::SessionHello;
	if (method == "session.ping")
		return V2Method::SessionPing;
	if (method == "session.close")
		return V2Method::SessionClose;
	if (method == "engine.getCapabilities")
		return V2Method::EngineGetCapabilities;
	return V2Method::Unknown;
}

bool method_is_mutating(V2Method method)
{
	return method == V2Method::SessionClose;
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

bool validate_revision_guard(RevisionState &revisions, const V2Request &request, V2Method method)
{
	const uint64_t current_revision = revisions.current();
	if (!request.has_if_revision)
		return true;

	if (!method_is_mutating(method)) {
		send_v2_error(request.id, "bad_request", "ifRevision is only valid for mutating methods", nullptr,
			      current_revision);
		return false;
	}

	const uint64_t expected_revision = static_cast<uint64_t>(request.if_revision);
	if (expected_revision == current_revision)
		return true;

	ObsDataPtr details(obs_data_create());
	obs_data_set_int(details.get(), "expectedRevision", request.if_revision);
	obs_data_set_int(details.get(), "actualRevision", static_cast<long long>(current_revision));
	send_v2_error(request.id, "revision_conflict", "ifRevision does not match the current engine revision",
		      details.get(), current_revision);
	return false;
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

bool handle_v2_request(const Config &, RevisionState &revisions, const V2Request &request)
{
	const V2Method method = classify_method(request.method);
	if (method == V2Method::Unknown) {
		send_v2_error(request.id, "unsupported_method", "method is not implemented by protocol v2 yet", nullptr,
			      revisions.current());
		return true;
	}

	if (!validate_revision_guard(revisions, request, method))
		return true;

	if (method == V2Method::SessionHello) {
		const uint64_t revision = revisions.current();
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
		obs_data_set_int(data.get(), "revision", static_cast<long long>(revision));
		send_v2_ok(request.id, data.get(), revision);
		return true;
	}

	if (method == V2Method::SessionPing) {
		ObsDataPtr data(obs_data_create());
		obs_data_set_bool(data.get(), "pong", true);
		send_v2_ok(request.id, data.get(), revisions.current());
		return true;
	}

	if (method == V2Method::SessionClose) {
		const uint64_t revision = revisions.commit_mutation();
		send_v2_ok(request.id, nullptr, revision);
		return false;
	}

	if (method == V2Method::EngineGetCapabilities) {
		ObsDataPtr data(obs_data_create());
		set_capabilities(data.get());
		send_v2_ok(request.id, data.get(), revisions.current());
		return true;
	}

	send_v2_error(request.id, "internal_error", "method dispatch failed internally", nullptr, revisions.current());
	return true;
}

} // namespace obs_engine
