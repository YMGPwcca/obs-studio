#include "protocol_v2.hpp"

#include "events.hpp"
#include "revision.hpp"
#include "runtime.hpp"

#include <windows.h>

#include <obs.h>

#include <cstdio>
#include <optional>
#include <string_view>

namespace obs_engine {

// protocol_v2.cpp is compiled with a source-local preprocessor rename so its
// accepted Task-10 implementation remains byte-for-byte unchanged while this
// file adds the Task-11 routing layer.
bool handle_v2_request_core(Engine &engine, const Config &config, RevisionState &revisions, EventDispatcher &events,
			    const V2Request &request);

namespace {

struct CapabilityDescriptor {
	const char *name;
	bool experimental;
};

// Keep this list equal to the accepted Protocol-v2 capability set plus the
// Task-11 filter namespace. Controllers feature-detect these names rather than
// infer behavior from the libobs version.
constexpr CapabilityDescriptor kCapabilities[] = {
	{"engine.capabilities.v1", false},
	{"event.delivery.v1", false},
	{"interaction.v1", false},
	{"interaction.focus.v1", false},
	{"interaction.key.v1", false},
	{"interaction.mouseButton.v1", false},
	{"interaction.mouseMove.v1", false},
	{"interaction.mouseWheel.v1", false},
	{"interaction.reset.v1", false},
	{"interaction.text.v1", false},
	{"item.create.v1", false},
	{"item.remove.v1", false},
	{"item.setTransform.v1", false},
	{"filter.create.v1", false},
	{"filter.duplicate.v1", false},
	{"filter.get.v1", false},
	{"filter.getEnabled.v1", false},
	{"filter.getSettings.v1", false},
	{"filter.kindDefaults.v1", false},
	{"filter.kindList.v1", false},
	{"filter.kindProperties.v1", false},
	{"filter.list.v1", false},
	{"filter.moveBottom.v1", false},
	{"filter.moveDown.v1", false},
	{"filter.moveTop.v1", false},
	{"filter.moveUp.v1", false},
	{"filter.patchSettings.v1", false},
	{"filter.remove.v1", false},
	{"filter.rename.v1", false},
	{"filter.replaceSettings.v1", false},
	{"filter.setEnabled.v1", false},
	{"filter.setOrder.v1", false},
	{"filter.v1", false},
	{"media.getDuration.v1", false},
	{"media.getPosition.v1", false},
	{"media.getState.v1", false},
	{"media.next.v1", false},
	{"media.pause.v1", false},
	{"media.play.v1", false},
	{"media.previous.v1", false},
	{"media.restart.v1", false},
	{"media.setPosition.v1", false},
	{"media.stop.v1", false},
	{"media.togglePause.v1", false},
	{"media.v1", false},
	{"properties.v1", false},
	{"properties.get.v1", false},
	{"properties.getListItems.v1", false},
	{"properties.invokeButton.v1", false},
	{"properties.refresh.v1", false},
	{"properties.resolve.v1", false},
	{"properties.validate.v1", false},
	{"scene.create.v1", false},
	{"scene.remove.v1", false},
	{"session.close.v1", false},
	{"session.getSubscriptions.v1", false},
	{"session.hello.v1", false},
	{"session.ping.v1", false},
	{"session.subscribe.v1", false},
	{"session.unsubscribe.v1", false},
	{"source.v1", false},
	{"source.create.v1", false},
	{"source.duplicate.v1", false},
	{"source.get.v1", false},
	{"source.getActive.v1", false},
	{"source.getDimensions.v1", false},
	{"source.getFlags.v1", false},
	{"source.getMissingFiles.v1", false},
	{"source.getProperties.v1", false},
	{"source.getSettings.v1", false},
	{"source.getShowing.v1", false},
	{"source.getState.v1", false},
	{"source.kindDefaults.v1", false},
	{"source.kindGet.v1", false},
	{"source.kindList.v1", false},
	{"source.kindProperties.v1", false},
	{"source.list.v1", false},
	{"source.loadState.v1", false},
	{"source.patchSettings.v1", false},
	{"source.refresh.v1", false},
	{"source.remove.v1", false},
	{"source.rename.v1", false},
	{"source.replaceSettings.v1", false},
	{"source.resetSettings.v1", false},
	{"source.saveState.v1", false},
};

enum class FilterMethod {
	KindList,
	KindDefaults,
	KindProperties,
	List,
	Get,
	Create,
	Remove,
	Rename,
	Duplicate,
	GetSettings,
	PatchSettings,
	ReplaceSettings,
	SetEnabled,
	GetEnabled,
	SetOrder,
	MoveUp,
	MoveDown,
	MoveTop,
	MoveBottom,
	Unknown,
};

FilterMethod classify_filter_method(std::string_view method)
{
	if (method == "filter.kindList")
		return FilterMethod::KindList;
	if (method == "filter.kindDefaults")
		return FilterMethod::KindDefaults;
	if (method == "filter.kindProperties")
		return FilterMethod::KindProperties;
	if (method == "filter.list")
		return FilterMethod::List;
	if (method == "filter.get")
		return FilterMethod::Get;
	if (method == "filter.create")
		return FilterMethod::Create;
	if (method == "filter.remove")
		return FilterMethod::Remove;
	if (method == "filter.rename")
		return FilterMethod::Rename;
	if (method == "filter.duplicate")
		return FilterMethod::Duplicate;
	if (method == "filter.getSettings")
		return FilterMethod::GetSettings;
	if (method == "filter.patchSettings")
		return FilterMethod::PatchSettings;
	if (method == "filter.replaceSettings")
		return FilterMethod::ReplaceSettings;
	if (method == "filter.setEnabled")
		return FilterMethod::SetEnabled;
	if (method == "filter.getEnabled")
		return FilterMethod::GetEnabled;
	if (method == "filter.setOrder")
		return FilterMethod::SetOrder;
	if (method == "filter.moveUp")
		return FilterMethod::MoveUp;
	if (method == "filter.moveDown")
		return FilterMethod::MoveDown;
	if (method == "filter.moveTop")
		return FilterMethod::MoveTop;
	if (method == "filter.moveBottom")
		return FilterMethod::MoveBottom;
	return FilterMethod::Unknown;
}

bool is_mutating(FilterMethod method)
{
	switch (method) {
	case FilterMethod::Create:
	case FilterMethod::Remove:
	case FilterMethod::Rename:
	case FilterMethod::Duplicate:
	case FilterMethod::PatchSettings:
	case FilterMethod::ReplaceSettings:
	case FilterMethod::SetEnabled:
	case FilterMethod::SetOrder:
	case FilterMethod::MoveUp:
	case FilterMethod::MoveDown:
	case FilterMethod::MoveTop:
	case FilterMethod::MoveBottom:
		return true;
	default:
		return false;
	}
}

bool needs_settings_settle(FilterMethod method)
{
	return method == FilterMethod::PatchSettings || method == FilterMethod::ReplaceSettings;
}

bool execute_filter_method(Engine &engine, FilterMethod method, obs_data_t *params, RuntimeV2Result &result,
			   RuntimeV2Error &error)
{
	switch (method) {
	case FilterMethod::KindList:
		return engine.v2_filter_kind_list(params, result, error);
	case FilterMethod::KindDefaults:
		return engine.v2_filter_kind_defaults(params, result, error);
	case FilterMethod::KindProperties:
		return engine.v2_filter_kind_properties(params, result, error);
	case FilterMethod::List:
		return engine.v2_filter_list(params, result, error);
	case FilterMethod::Get:
		return engine.v2_filter_get(params, result, error);
	case FilterMethod::Create:
		return engine.v2_filter_create(params, result, error);
	case FilterMethod::Remove:
		return engine.v2_filter_remove(params, result, error);
	case FilterMethod::Rename:
		return engine.v2_filter_rename(params, result, error);
	case FilterMethod::Duplicate:
		return engine.v2_filter_duplicate(params, result, error);
	case FilterMethod::GetSettings:
		return engine.v2_filter_get_settings(params, result, error);
	case FilterMethod::PatchSettings:
		return engine.v2_filter_patch_settings(params, result, error);
	case FilterMethod::ReplaceSettings:
		return engine.v2_filter_replace_settings(params, result, error);
	case FilterMethod::SetEnabled:
		return engine.v2_filter_set_enabled(params, result, error);
	case FilterMethod::GetEnabled:
		return engine.v2_filter_get_enabled(params, result, error);
	case FilterMethod::SetOrder:
		return engine.v2_filter_set_order(params, result, error);
	case FilterMethod::MoveUp:
		return engine.v2_filter_move_up(params, result, error);
	case FilterMethod::MoveDown:
		return engine.v2_filter_move_down(params, result, error);
	case FilterMethod::MoveTop:
		return engine.v2_filter_move_top(params, result, error);
	case FilterMethod::MoveBottom:
		return engine.v2_filter_move_bottom(params, result, error);
	default:
		error.code = "internal_error";
		error.message = "filter method dispatch failed";
		return false;
	}
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

bool reject_guard_on_read(const V2Request &request, uint64_t revision)
{
	if (!request.has_if_revision)
		return false;
	send_v2_error(request.id, "bad_request", "ifRevision is only valid for engine-state mutating methods", nullptr,
		      revision);
	return true;
}

bool validate_mutation_guard(const V2Request &request, RevisionState::MutationGuard &guard)
{
	if (!request.has_if_revision)
		return true;
	const uint64_t expected = static_cast<uint64_t>(request.if_revision);
	if (guard.matches(expected))
		return true;
	ObsDataPtr details(obs_data_create());
	obs_data_set_int(details.get(), "expectedRevision", request.if_revision);
	obs_data_set_int(details.get(), "actualRevision", static_cast<long long>(guard.current()));
	send_v2_error(request.id, "revision_conflict", "ifRevision does not match the current engine revision",
		      details.get(), guard.current());
	return false;
}

void publish_runtime_events(EventDispatcher &events, uint64_t revision, RuntimeV2Result &result)
{
	for (RuntimeV2Event &event : result.events) {
		const EventPublishResult publish_result =
			events.publish(EngineEventKind::State, event.name, revision, event.data.get());
		if (publish_result == EventPublishResult::InvalidEvent) {
			std::fprintf(stderr, "obs-engine: invalid internal filter event name '%s'\n", event.name.c_str());
			std::fflush(stderr);
		}
	}
}

class FilterCaptureScope {
public:
	FilterCaptureScope(Engine &engine, RuntimeV2Result &result) : engine_(engine)
	{
		engine_.v2_begin_event_capture(result);
		// This is deliberately before the caller acquires the mutation lock.
		// It preserves the accepted Task-10 signal-mutex/revision-mutex ordering.
		engine_.v2_wait_for_event_capture_callbacks();
	}

	~FilterCaptureScope()
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

	FilterCaptureScope(const FilterCaptureScope &) = delete;
	FilterCaptureScope &operator=(const FilterCaptureScope &) = delete;

private:
	Engine &engine_;
	bool active_ = true;
};

bool handle_capability_request(RevisionState &revisions, const V2Request &request)
{
	if (reject_guard_on_read(request, revisions.current()))
		return true;

	if (request.method == "session.hello") {
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

	ObsDataPtr data(obs_data_create());
	set_capabilities(data.get());
	send_v2_ok(request.id, data.get(), revisions.current());
	return true;
}

bool handle_filter_request(Engine &engine, RevisionState &revisions, EventDispatcher &events, const V2Request &request,
			   FilterMethod method)
{
	const bool mutating = is_mutating(method);
	if (!mutating && reject_guard_on_read(request, revisions.current()))
		return true;

	RuntimeV2Result result;
	RuntimeV2Error error;
	std::optional<FilterCaptureScope> capture;
	std::optional<RevisionState::MutationGuard> guard;

	if (mutating) {
		capture.emplace(engine, result);
		guard.emplace(revisions.lock_mutation());
		engine.v2_drain_deferred_source_events(*guard);

		if (!validate_mutation_guard(request, *guard)) {
			capture->flush(*guard);
			return true;
		}
		if (!guard->can_commit_mutation()) {
			send_v2_error(request.id, "internal_error", "engine revision space is exhausted", nullptr,
				      guard->current());
			capture->flush(*guard);
			return true;
		}
	}

	bool succeeded = execute_filter_method(engine, method, request.params.get(), result, error);
	if (succeeded && capture && needs_settings_settle(method) && result.mutated)
		succeeded = engine.v2_settle_filter_mutation(request.params.get(), result, error);

	// This cascades source -> media -> filter observer synchronization while the
	// capture gate is still active.
	engine.v2_sync_source_observers();

	if (!succeeded) {
		send_v2_error(request.id, error.code.c_str(), error.message.c_str(), nullptr,
		      guard ? guard->current() : revisions.current());
		if (capture)
			capture->flush(*guard);
		return true;
	}

	uint64_t revision = guard ? guard->current() : revisions.current();
	if (result.mutated) {
		if (!guard) {
			send_v2_error(request.id, "internal_error", "read-only filter method unexpectedly mutated engine state",
			      nullptr, revisions.current());
			return true;
		}
		revision = guard->commit_mutation();
	}

	send_v2_ok(request.id, result.data.get(), revision);
	publish_runtime_events(events, revision, result);
	if (capture)
		capture->flush(*guard);
	return true;
}

} // namespace

bool handle_v2_request(Engine &engine, const Config &config, RevisionState &revisions, EventDispatcher &events,
		       const V2Request &request)
{
	if (request.method == "session.hello" || request.method == "engine.getCapabilities")
		return handle_capability_request(revisions, request);

	const FilterMethod filter_method = classify_filter_method(request.method);
	if (filter_method != FilterMethod::Unknown)
		return handle_filter_request(engine, revisions, events, request, filter_method);

	return handle_v2_request_core(engine, config, revisions, events, request);
}

} // namespace obs_engine
