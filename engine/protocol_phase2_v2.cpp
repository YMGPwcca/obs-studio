#include "protocol_phase2_v2.hpp"

#include "events.hpp"
#include "runtime.hpp"

#include <obs.h>

#include <optional>
#include <string_view>

namespace obs_engine {
namespace {

enum class Phase2Method {
	SceneList,
	SceneGet,
	SceneCreate,
	SceneRemove,
	SceneRename,
	SceneDuplicate,
	SceneGetItems,
	SceneGetState,
	Unknown,
};

struct Phase2MethodName {
	std::string_view name;
	Phase2Method method;
};

constexpr Phase2MethodName kMethods[] = {
	{"scene.list", Phase2Method::SceneList},       {"scene.get", Phase2Method::SceneGet},
	{"scene.create", Phase2Method::SceneCreate},   {"scene.remove", Phase2Method::SceneRemove},
	{"scene.rename", Phase2Method::SceneRename},   {"scene.duplicate", Phase2Method::SceneDuplicate},
	{"scene.getItems", Phase2Method::SceneGetItems}, {"scene.getState", Phase2Method::SceneGetState},
};

Phase2Method classify(std::string_view method)
{
	for (const Phase2MethodName &entry : kMethods) {
		if (entry.name == method)
			return entry.method;
	}
	return Phase2Method::Unknown;
}

bool mutating(Phase2Method method)
{
	return method == Phase2Method::SceneCreate || method == Phase2Method::SceneRemove ||
	       method == Phase2Method::SceneRename || method == Phase2Method::SceneDuplicate;
}

using Handler = bool (Engine::*)(obs_data_t *, RuntimeV2Result &, RuntimeV2Error &);

Handler handler_for(Phase2Method method)
{
	switch (method) {
	case Phase2Method::SceneList:
		return &Engine::v2_scene_list;
	case Phase2Method::SceneGet:
		return &Engine::v2_scene_get;
	case Phase2Method::SceneCreate:
		return &Engine::v2_scene_create;
	case Phase2Method::SceneRemove:
		return &Engine::v2_scene_remove;
	case Phase2Method::SceneRename:
		return &Engine::v2_scene_rename;
	case Phase2Method::SceneDuplicate:
		return &Engine::v2_scene_duplicate;
	case Phase2Method::SceneGetItems:
		return &Engine::v2_scene_get_items;
	case Phase2Method::SceneGetState:
		return &Engine::v2_scene_get_state;
	case Phase2Method::Unknown:
		return nullptr;
	}
	return nullptr;
}

void publish_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events)
		events.publish(EngineEventKind::State, event.name, revision, event.data.get());
}

class Phase2CaptureScope {
public:
	Phase2CaptureScope(Engine &engine, RuntimeV2Result &result) : engine_(engine)
	{
		engine_.v2_begin_event_capture(result);
		engine_.v2_wait_for_event_capture_callbacks();
	}

	~Phase2CaptureScope()
	{
		if (active_)
			engine_.v2_end_event_capture();
	}

	void flush(RevisionState::MutationGuard &guard)
	{
		if (!active_)
			return;
		engine_.v2_flush_deferred_source_events(guard);
		active_ = false;
	}

	Phase2CaptureScope(const Phase2CaptureScope &) = delete;
	Phase2CaptureScope &operator=(const Phase2CaptureScope &) = delete;

private:
	Engine &engine_;
	bool active_ = true;
};

bool reject_read_guard(const V2Request &request, uint64_t revision)
{
	if (!request.has_if_revision)
		return false;
	send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
			      revision);
	return true;
}

bool validate_guard(const V2Request &request, RevisionState::MutationGuard &guard)
{
	if (!request.has_if_revision || guard.matches(static_cast<uint64_t>(request.if_revision)))
		return true;
	ObsDataPtr details(obs_data_create());
	obs_data_set_int(details.get(), "expectedRevision", request.if_revision);
	obs_data_set_int(details.get(), "actualRevision", static_cast<long long>(guard.current()));
	send_v2_error(request.id, "revision_conflict", "ifRevision does not match the current engine revision",
		      details.get(), guard.current());
	return false;
}

bool prepare(const V2Request &request, Phase2Method method, Engine &engine, RevisionState &revisions,
		     RuntimeV2Result &result, std::optional<Phase2CaptureScope> &capture,
		     std::optional<RevisionState::MutationGuard> &guard)
{
	if (mutating(method)) {
		capture.emplace(engine, result);
		guard.emplace(revisions.lock_mutation());
		engine.v2_drain_deferred_source_events(*guard);
	}
	const uint64_t current = guard ? guard->current() : revisions.current();
	if ((mutating(method) && !validate_guard(request, *guard)) || (!mutating(method) && reject_read_guard(request, current))) {
		if (capture)
			capture->flush(*guard);
		return false;
	}
	if (guard && !guard->can_commit_mutation()) {
		send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr, guard->current());
		capture->flush(*guard);
		return false;
	}
	return true;
}

} // namespace

bool is_phase2_method(std::string_view method)
{
	return classify(method) != Phase2Method::Unknown;
}

bool handle_phase2_request(Engine &engine, RevisionState &revisions, EventDispatcher &events,
				   const V2Request &request)
{
	const Phase2Method method = classify(request.method);
	if (method == Phase2Method::Unknown)
		return false;

	RuntimeV2Result result;
	RuntimeV2Error error;
	std::optional<Phase2CaptureScope> capture;
	std::optional<RevisionState::MutationGuard> guard;
	if (!prepare(request, method, engine, revisions, result, capture, guard))
		return true;

	const Handler handler = handler_for(method);
	const bool succeeded = handler && (engine.*handler)(request.params.get(), result, error);
	engine.v2_sync_source_observers();
	if (!succeeded) {
		if (error.code.empty()) {
			error.code = "internal_error";
			error.message = "Phase-2 scene dispatch failed";
		}
		send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
			      guard ? guard->current() : revisions.current());
		if (capture)
			capture->flush(*guard);
		return true;
	}

	uint64_t revision = guard ? guard->current() : revisions.current();
	if (result.mutated) {
		if (!guard) {
			send_v2_error(request.id, "internal_error", "read-only scene method mutated engine state", nullptr,
				      revisions.current());
			if (capture)
				capture->flush(*guard);
			return true;
		}
		revision = guard->commit_mutation();
	}
	send_v2_ok(request.id, result.data.get(), revision);
	publish_events(events, revision, result);
	if (capture)
		capture->flush(*guard);
	return true;
}

} // namespace obs_engine
