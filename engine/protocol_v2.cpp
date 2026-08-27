#include "protocol_v2.hpp"

#include "events.hpp"
#include "revision.hpp"
#include "runtime.hpp"
#include "validation.hpp"

#include <windows.h>

#include <obs.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace obs_engine {
namespace {

struct CapabilityDescriptor {
	const char *name;
	bool experimental;
};

constexpr CapabilityDescriptor kCapabilities[] = {
	{"engine.capabilities.v1", false},
	{"event.delivery.v1", false},
	{"item.create.v1", false},
	{"item.remove.v1", false},
	{"item.setTransform.v1", false},
	{"scene.create.v1", false},
	{"scene.remove.v1", false},
	{"session.close.v1", false},
	{"session.getSubscriptions.v1", false},
	{"session.hello.v1", false},
	{"session.ping.v1", false},
	{"session.subscribe.v1", false},
	{"session.unsubscribe.v1", false},
	{"source.create.v1", false},
	{"source.getSettings.v1", false},
	{"source.kindDefaults.v1", false},
	{"source.kindList.v1", false},
	{"source.patchSettings.v1", false},
	{"source.remove.v1", false},
};

enum class V2Method {
	SessionHello,
	SessionPing,
	SessionSubscribe,
	SessionUnsubscribe,
	SessionGetSubscriptions,
	SessionClose,
	EngineGetCapabilities,
	SourceKindList,
	SourceKindDefaults,
	SourceCreate,
	SourceGetSettings,
	SourcePatchSettings,
	SourceRemove,
	SceneCreate,
	SceneRemove,
	ItemCreate,
	ItemRemove,
	ItemSetTransform,
	Unknown,
};

V2Method classify_method(std::string_view method)
{
	if (method == "session.hello")
		return V2Method::SessionHello;
	if (method == "session.ping")
		return V2Method::SessionPing;
	if (method == "session.subscribe")
		return V2Method::SessionSubscribe;
	if (method == "session.unsubscribe")
		return V2Method::SessionUnsubscribe;
	if (method == "session.getSubscriptions")
		return V2Method::SessionGetSubscriptions;
	if (method == "session.close")
		return V2Method::SessionClose;
	if (method == "engine.getCapabilities")
		return V2Method::EngineGetCapabilities;
	if (method == "source.kindList")
		return V2Method::SourceKindList;
	if (method == "source.kindDefaults")
		return V2Method::SourceKindDefaults;
	if (method == "source.create")
		return V2Method::SourceCreate;
	if (method == "source.getSettings")
		return V2Method::SourceGetSettings;
	if (method == "source.patchSettings")
		return V2Method::SourcePatchSettings;
	if (method == "source.remove")
		return V2Method::SourceRemove;
	if (method == "scene.create")
		return V2Method::SceneCreate;
	if (method == "scene.remove")
		return V2Method::SceneRemove;
	if (method == "item.create")
		return V2Method::ItemCreate;
	if (method == "item.remove")
		return V2Method::ItemRemove;
	if (method == "item.setTransform")
		return V2Method::ItemSetTransform;
	return V2Method::Unknown;
}

bool method_is_mutating(V2Method method)
{
	switch (method) {
	case V2Method::SessionClose:
	case V2Method::SourceCreate:
	case V2Method::SourcePatchSettings:
	case V2Method::SourceRemove:
	case V2Method::SceneCreate:
	case V2Method::SceneRemove:
	case V2Method::ItemCreate:
	case V2Method::ItemRemove:
	case V2Method::ItemSetTransform:
		return true;
	default:
		return false;
	}
}

bool method_is_runtime(V2Method method)
{
	switch (method) {
	case V2Method::SourceKindList:
	case V2Method::SourceKindDefaults:
	case V2Method::SourceCreate:
	case V2Method::SourceGetSettings:
	case V2Method::SourcePatchSettings:
	case V2Method::SourceRemove:
	case V2Method::SceneCreate:
	case V2Method::SceneRemove:
	case V2Method::ItemCreate:
	case V2Method::ItemRemove:
	case V2Method::ItemSetTransform:
		return true;
	default:
		return false;
	}
}

bool execute_runtime_method(Engine &engine, V2Method method, obs_data_t *params, RuntimeV2Result &result,
			    RuntimeV2Error &error)
{
	switch (method) {
	case V2Method::SourceKindList:
		return engine.v2_source_kind_list(params, result, error);
	case V2Method::SourceKindDefaults:
		return engine.v2_source_kind_defaults(params, result, error);
	case V2Method::SourceCreate:
		return engine.v2_source_create(params, result, error);
	case V2Method::SourceGetSettings:
		return engine.v2_source_get_settings(params, result, error);
	case V2Method::SourcePatchSettings:
		return engine.v2_source_patch_settings(params, result, error);
	case V2Method::SourceRemove:
		return engine.v2_source_remove(params, result, error);
	case V2Method::SceneCreate:
		return engine.v2_scene_create(params, result, error);
	case V2Method::SceneRemove:
		return engine.v2_scene_remove(params, result, error);
	case V2Method::ItemCreate:
		return engine.v2_item_create(params, result, error);
	case V2Method::ItemRemove:
		return engine.v2_item_remove(params, result, error);
	case V2Method::ItemSetTransform:
		return engine.v2_item_set_transform(params, result, error);
	default:
		error.code = "internal_error";
		error.message = "runtime method dispatch failed";
		return false;
	}
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

void set_subscriptions(obs_data_t *data, const std::vector<EventSubscription> &subscriptions)
{
	ObsArrayPtr array(obs_data_array_create());
	for (const EventSubscription &subscription : subscriptions) {
		ObsDataPtr entry(obs_data_create());
		obs_data_set_string(entry.get(), "pattern", subscription.pattern.c_str());
		obs_data_set_bool(entry.get(), "telemetry", subscription.telemetry);
		obs_data_array_push_back(array.get(), entry.get());
	}
	obs_data_set_array(data, "subscriptions", array.get());
}

bool parse_subscription_list(obs_data_t *params, std::vector<EventSubscription> &subscriptions, std::string &error)
{
	subscriptions.clear();
	error.clear();
	ObsArrayPtr array;
	bool present = false;
	if (!read_array_field(params, "subscriptions", array, present) || !present) {
		error = "params.subscriptions must be an array";
		return false;
	}
	const size_t count = obs_data_array_count(array.get());
	if (count == 0 || count > kMaxEventSubscriptions) {
		error = "params.subscriptions must contain between 1 and 256 entries";
		return false;
	}
	subscriptions.reserve(count);
	for (size_t index = 0; index < count; ++index) {
		ObsDataPtr entry(obs_data_array_item(array.get(), index));
		if (!entry) {
			error = "each subscription must be an object";
			return false;
		}
		EventSubscription subscription;
		if (!read_string_field(entry.get(), "pattern", subscription.pattern, present) || !present ||
		    !is_valid_event_pattern(subscription.pattern)) {
			error = "subscription pattern must be an exact event name or a namespace wildcard such as 'source.*'";
			return false;
		}
		bool telemetry = false;
		if (!read_bool_field(entry.get(), "telemetry", telemetry, present)) {
			error = "subscription telemetry must be a boolean when present";
			return false;
		}
		subscription.telemetry = present && telemetry;
		subscriptions.push_back(std::move(subscription));
	}
	return true;
}

void send_subscription_state(const V2Request &request, RevisionState &revisions, EventDispatcher &events)
{
	ObsDataPtr data(obs_data_create());
	set_subscriptions(data.get(), events.subscriptions());
	send_v2_ok(request.id, data.get(), revisions.current());
}

bool validate_revision_guard(RevisionState &revisions, const V2Request &request, V2Method method)
{
	const uint64_t current_revision = revisions.current();
	if (!request.has_if_revision)
		return true;
	if (!method_is_mutating(method)) {
		send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
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

void publish_runtime_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events) {
		const EventPublishResult publish_result =
			events.publish(EngineEventKind::State, event.name, revision, event.data.get());
		if (publish_result == EventPublishResult::InvalidEvent) {
			std::fprintf(stderr, "obs-engine: invalid internal event name '%s'\n", event.name.c_str());
			std::fflush(stderr);
		}
	}
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
		set_parse_error(error, out.id, "bad_request", "method must be a 1-128 byte protocol identifier");
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

bool handle_v2_request(Engine &engine, const Config &, RevisionState &revisions, EventDispatcher &events,
		       const V2Request &request)
{
	const V2Method method = classify_method(request.method);
	if (method == V2Method::Unknown) {
		send_v2_error(request.id, "unsupported_method", "method is not implemented by protocol v2 yet", nullptr,
			      revisions.current());
		return true;
	}
	if (!validate_revision_guard(revisions, request, method))
		return true;
	if (method_is_mutating(method) && !revisions.can_commit_mutation()) {
		send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr,
			      revisions.current());
		return true;
	}
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
	if (method == V2Method::SessionSubscribe) {
		std::vector<EventSubscription> requested;
		std::string error;
		if (!parse_subscription_list(request.params.get(), requested, error) || !events.subscribe(requested, error)) {
			send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
			return true;
		}
		send_subscription_state(request, revisions, events);
		return true;
	}
	if (method == V2Method::SessionUnsubscribe) {
		std::vector<EventSubscription> requested;
		std::string error;
		if (!parse_subscription_list(request.params.get(), requested, error)) {
			send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
			return true;
		}
		std::vector<std::string> patterns;
		patterns.reserve(requested.size());
		for (const EventSubscription &subscription : requested)
			patterns.push_back(subscription.pattern);
		if (!events.unsubscribe(patterns, error)) {
			send_v2_error(request.id, "bad_request", error.c_str(), nullptr, revisions.current());
			return true;
		}
		send_subscription_state(request, revisions, events);
		return true;
	}
	if (method == V2Method::SessionGetSubscriptions) {
		send_subscription_state(request, revisions, events);
		return true;
	}
	if (method == V2Method::SessionClose) {
		const uint64_t revision = revisions.commit_mutation();
		send_v2_ok(request.id, nullptr, revision);
		ObsDataPtr event_data(obs_data_create());
		obs_data_set_string(event_data.get(), "reason", "session.close");
		events.publish(EngineEventKind::State, "engine.stopping", revision, event_data.get());
		return false;
	}
	if (method == V2Method::EngineGetCapabilities) {
		ObsDataPtr data(obs_data_create());
		set_capabilities(data.get());
		send_v2_ok(request.id, data.get(), revisions.current());
		return true;
	}
	if (method_is_runtime(method)) {
		RuntimeV2Result result;
		RuntimeV2Error error;
		if (!execute_runtime_method(engine, method, request.params.get(), result, error)) {
			send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr, revisions.current());
			return true;
		}
		uint64_t revision = revisions.current();
		if (method_is_mutating(method))
			revision = revisions.commit_mutation();
		send_v2_ok(request.id, result.data.get(), revision);
		publish_runtime_events(events, revision, result);
		return true;
	}
	send_v2_error(request.id, "internal_error", "method dispatch failed internally", nullptr, revisions.current());
	return true;
}

} // namespace obs_engine
