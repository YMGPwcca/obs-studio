#include "events.hpp"

#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

std::mutex g_output_mutex;
std::vector<std::string> g_output_lines;

void reset_output()
{
	std::lock_guard lock(g_output_mutex);
	g_output_lines.clear();
}

std::vector<std::string> output_lines()
{
	std::lock_guard lock(g_output_mutex);
	return g_output_lines;
}

bool require(bool condition, const char *message)
{
	if (condition)
		return true;
	std::fprintf(stderr, "events-test: %s\n", message);
	return false;
}

bool contains(const std::string &line, const char *needle)
{
	return line.find(needle) != std::string::npos;
}

bool check_pattern_cases()
{
	const std::string too_long_pattern(127, 'a');
	return require(obs_engine::is_valid_event_pattern("engine.stopping"), "exact event pattern rejected") &&
	       require(obs_engine::is_valid_event_pattern("engine.*"), "namespace wildcard rejected") &&
	       require(!obs_engine::is_valid_event_pattern("*"), "global wildcard must not be accepted") &&
	       require(!obs_engine::is_valid_event_pattern("engine..stopping"),
		       "empty event namespace segment accepted") &&
	       require(!obs_engine::is_valid_event_pattern("engine.*.bad"), "non-terminal wildcard accepted") &&
	       require(!obs_engine::is_valid_event_pattern(too_long_pattern + ".*"), "oversized wildcard pattern accepted");
}

bool check_overlap_output(const std::vector<std::string> &lines)
{
	return require(lines.size() == 1, "overlapping subscriptions emitted duplicate events") &&
	       require(contains(lines[0], "\"seq\":1"), "first delivered event did not use seq 1") &&
	       require(contains(lines[0], "\"revision\":7"), "event revision was not preserved") &&
	       require(contains(lines[0], "\"event\":\"engine.stopping\""), "wrong event name emitted");
}

bool check_eviction_output(const std::vector<std::string> &lines)
{
	return require(lines.size() == 2, "telemetry eviction changed the expected state-event count") &&
	       require(contains(lines[0], "\"event\":\"engine.one\""), "older state event was lost") &&
	       require(contains(lines[1], "\"event\":\"engine.two\""), "new state event was lost") &&
	       require(!contains(lines[0], "session.resyncRequired") && !contains(lines[1], "session.resyncRequired"),
		       "telemetry eviction unnecessarily forced state resync") &&
	       require(!contains(lines[0], "meter.level") && !contains(lines[1], "meter.level"),
		       "evicted telemetry was still delivered");
}

bool test_patterns_and_overlap()
{
	using namespace obs_engine;
	if (!check_pattern_cases())
		return false;

	reset_output();
	EventDispatcher events(4);
	std::string error;
	if (!require(events.subscribe({{"engine.*", false}, {"engine.stopping", false}, {"engine.*", false}}, error),
		     "valid subscriptions were rejected"))
		return false;
	if (!require(events.subscriptions().size() == 2, "duplicate subscription was not deduplicated"))
		return false;
	if (!require(events.publish(EngineEventKind::State, "engine.stopping", 7) == EventPublishResult::Enqueued,
		     "overlapping subscription did not enqueue event"))
		return false;
	events.start();
	events.stop_and_drain();

	const auto lines = output_lines();
	return check_overlap_output(lines);
}

bool test_state_overflow_requires_resync()
{
	using namespace obs_engine;
	reset_output();
	EventDispatcher events(2);
	std::string error;
	if (!require(events.subscribe({{"engine.*", false}}, error), "state subscription rejected"))
		return false;
	if (!require(events.publish(EngineEventKind::State, "engine.one", 5) == EventPublishResult::Enqueued,
		     "first state event was not enqueued") ||
	    !require(events.publish(EngineEventKind::State, "engine.two", 4) == EventPublishResult::Enqueued,
		     "second state event was not enqueued") ||
	    !require(events.publish(EngineEventKind::State, "engine.three", 3) == EventPublishResult::ResyncRequired,
		     "state overflow did not require resync"))
		return false;

	events.start();
	events.stop_and_drain();
	const auto lines = output_lines();
	return require(lines.size() == 1, "state overflow leaked stale queued events") &&
	       require(contains(lines[0], "\"event\":\"session.resyncRequired\""),
		       "state overflow did not emit mandatory resync marker") &&
	       require(contains(lines[0], "\"revision\":5"),
		       "resync marker did not carry the highest invalidated revision") &&
	       require(contains(lines[0], "event_queue_overflow"), "resync marker did not identify overflow reason");
}

bool test_state_prefers_telemetry_eviction()
{
	using namespace obs_engine;
	reset_output();
	EventDispatcher events(2);
	std::string error;
	if (!require(events.subscribe({{"engine.*", false}, {"meter.*", true}}, error),
		     "mixed state/telemetry subscriptions were rejected"))
		return false;
	if (!require(events.publish(EngineEventKind::State, "engine.one", 1) == EventPublishResult::Enqueued,
		     "state event was not enqueued") ||
	    !require(events.publish(EngineEventKind::Telemetry, "meter.level", 2) == EventPublishResult::Enqueued,
		     "telemetry event was not enqueued") ||
	    !require(events.publish(EngineEventKind::State, "engine.two", 3) == EventPublishResult::Enqueued,
		     "state event did not evict disposable telemetry from a full queue"))
		return false;

	events.start();
	events.stop_and_drain();
	const auto lines = output_lines();
	return check_eviction_output(lines);
}

bool test_ordered_resync_preserves_queued_event()
{
	using namespace obs_engine;
	reset_output();
	EventDispatcher events(2);
	std::string error;
	if (!require(events.subscribe({{"engine.*", false}, {"session.*", false}}, error),
		     "ordered-resync subscriptions rejected"))
		return false;
	if (!require(events.publish(EngineEventKind::State, "engine.command", 7) == EventPublishResult::Enqueued,
		     "command event was not enqueued before ordered resync"))
		return false;
	events.require_resync_after_queued_events(8);
	events.start();
	events.stop_and_drain();
	const auto lines = output_lines();
	return require(lines.size() == 2, "ordered resync did not preserve both queued messages") &&
	       require(contains(lines[0], "\"event\":\"engine.command\""),
		       "ordered resync overtook the queued command event") &&
	       require(contains(lines[1], "\"event\":\"session.resyncRequired\""),
		       "ordered resync marker was not delivered") &&
	       require(contains(lines[1], "\"revision\":8"),
		       "ordered resync revision was not preserved");
}

bool setup_telemetry_policy(obs_engine::EventDispatcher &events, std::string &error)
{
	using namespace obs_engine;
	if (!require(events.subscribe({{"meter.*", false}, {"engine.*", false}}, error),
		     "telemetry-disabled subscription rejected"))
		return false;
	if (!require(events.publish(EngineEventKind::Telemetry, "meter.level", 1) == EventPublishResult::NotSubscribed,
		     "telemetry was delivered without explicit opt-in"))
		return false;
	if (!require(events.subscribe({{"meter.*", true}}, error), "telemetry opt-in upgrade rejected"))
		return false;
	const auto effective = events.subscriptions();
	return require(effective.size() == 2 && effective[1].pattern == "meter.*" && effective[1].telemetry,
			       "telemetry opt-in did not upgrade the existing pattern");
}

bool check_telemetry_output(const std::vector<std::string> &lines)
{
	return require(lines.size() == 2, "telemetry coalescing/drop produced an unexpected event count") &&
	       require(contains(lines[0], "\"event\":\"engine.changed\""),
		       "newer coalesced telemetry was emitted ahead of older state event") &&
	       require(contains(lines[0], "\"revision\":2"), "state event revision changed unexpectedly") &&
	       require(contains(lines[1], "\"event\":\"meter.level\""), "coalesced telemetry event missing") &&
	       require(contains(lines[1], "\"revision\":3"), "coalesced telemetry did not keep newest revision") &&
	       require(contains(lines[1], "\"telemetry\":true"), "telemetry event was not identified") &&
	       require(!contains(lines[0], "session.resyncRequired") && !contains(lines[1], "session.resyncRequired"),
		       "telemetry loss incorrectly forced state resync");
}

bool test_telemetry_policy()
{
	using namespace obs_engine;
	reset_output();
	EventDispatcher events(2);
	std::string error;
	if (!setup_telemetry_policy(events, error))
		return false;

	if (!require(events.publish(EngineEventKind::Telemetry, "meter.level", 1) == EventPublishResult::Enqueued,
		     "first telemetry event was not enqueued") ||
	    !require(events.publish(EngineEventKind::State, "engine.changed", 2) == EventPublishResult::Enqueued,
		     "interleaved state event was not enqueued") ||
	    !require(events.publish(EngineEventKind::Telemetry, "meter.level", 3) == EventPublishResult::Coalesced,
		     "same-name telemetry was not coalesced") ||
	    !require(events.publish(EngineEventKind::Telemetry, "meter.other", 4) == EventPublishResult::DroppedTelemetry,
		     "full telemetry queue did not drop independently"))
		return false;

	events.start();
	events.stop_and_drain();
	const auto lines = output_lines();
	return check_telemetry_output(lines);
}

} // namespace

namespace obs_engine {

void write_json_line(std::string json)
{
	std::lock_guard lock(g_output_mutex);
	g_output_lines.push_back(std::move(json));
}

} // namespace obs_engine

int main()
{
	if (!test_patterns_and_overlap() || !test_state_overflow_requires_resync() ||
	    !test_state_prefers_telemetry_eviction() || !test_ordered_resync_preserves_queued_event() ||
	    !test_telemetry_policy())
		return 1;
	std::puts("events-test: passed");
	return 0;
}
