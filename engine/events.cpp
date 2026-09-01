#include "events.hpp"

#include "protocol.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <utility>

namespace obs_engine {
namespace {

bool is_event_name_character(unsigned char ch)
{
	constexpr std::string_view allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
	return allowed.find(static_cast<char>(ch)) != std::string_view::npos;
}

bool is_event_name(std::string_view value)
{
	if (value.empty() || value.size() > kMaxEventPatternBytes)
		return false;

	bool segment_has_character = false;
	for (const unsigned char ch : value) {
		if (ch == '.') {
			if (!segment_has_character)
				return false;
			segment_has_character = false;
		} else if (!is_event_name_character(ch)) {
			return false;
		} else {
			segment_has_character = true;
		}
	}
	return segment_has_character;
}

bool pattern_matches(std::string_view pattern, std::string_view event_name)
{
	if (pattern.size() >= 2 && pattern.ends_with(".*")) {
		const std::string_view prefix = pattern.substr(0, pattern.size() - 1);
		return event_name.size() > prefix.size() && event_name.starts_with(prefix);
	}
	return pattern == event_name;
}

std::string data_to_json(obs_data_t *data)
{
	if (!data)
		return "{}";
	const char *json = obs_data_get_json(data);
	return json ? json : "{}";
}

} // namespace

bool is_valid_event_pattern(std::string_view pattern)
{
	if (pattern.empty() || pattern.size() > kMaxEventPatternBytes)
		return false;
	if (pattern.size() >= 2 && pattern.ends_with(".*"))
		return is_event_name(pattern.substr(0, pattern.size() - 2));
	return is_event_name(pattern);
}

EventDispatcher::EventDispatcher(size_t capacity) : capacity_(std::max<size_t>(capacity, 1)) {}

EventDispatcher::~EventDispatcher()
{
	stop_and_drain();
}

void EventDispatcher::start()
{
	std::lock_guard lock(mutex_);
	if (started_)
		return;
	stopping_ = false;
	worker_ = std::thread(&EventDispatcher::run, this);
	started_ = true;
}

void EventDispatcher::stop_and_drain() noexcept
{
	{
		std::lock_guard lock(mutex_);
		if (!started_)
			return;
		stopping_ = true;
	}
	cv_.notify_all();
	if (worker_.joinable())
		worker_.join();

	std::lock_guard lock(mutex_);
	started_ = false;
	stopping_ = false;
}

bool EventDispatcher::subscribe(const std::vector<EventSubscription> &requested, std::string &error)
{
	error.clear();
	for (const EventSubscription &subscription : requested) {
		if (!is_valid_event_pattern(subscription.pattern)) {
			error = "subscription pattern must be an exact event name or a namespace wildcard such as 'source.*'";
			return false;
		}
	}

	std::lock_guard lock(mutex_);
	auto next = subscriptions_;
	for (const EventSubscription &subscription : requested) {
		auto existing = std::find_if(next.begin(), next.end(), [&](const EventSubscription &candidate) {
			return candidate.pattern == subscription.pattern;
		});
		if (existing == next.end()) {
			if (next.size() >= kMaxEventSubscriptions) {
				error = "session subscription limit exceeded";
				return false;
			}
			next.push_back(subscription);
		} else {
			existing->telemetry = existing->telemetry || subscription.telemetry;
		}
	}

	std::sort(next.begin(), next.end(), [](const EventSubscription &a, const EventSubscription &b) {
		return a.pattern < b.pattern;
	});
	subscriptions_.swap(next);
	return true;
}

bool EventDispatcher::unsubscribe(const std::vector<std::string> &patterns, std::string &error)
{
	error.clear();
	for (const std::string &pattern : patterns) {
		if (!is_valid_event_pattern(pattern)) {
			error = "subscription pattern must be an exact event name or a namespace wildcard such as 'source.*'";
			return false;
		}
	}

	std::lock_guard lock(mutex_);
	for (const std::string &pattern : patterns) {
		subscriptions_.erase(std::remove_if(subscriptions_.begin(), subscriptions_.end(),
					    [&](const EventSubscription &subscription) {
						    return subscription.pattern == pattern;
					    }),
			     subscriptions_.end());
	}
	return true;
}

std::vector<EventSubscription> EventDispatcher::subscriptions() const
{
	std::lock_guard lock(mutex_);
	return subscriptions_;
}

bool EventDispatcher::matches_locked(EngineEventKind kind, std::string_view event_name) const
{
	for (const EventSubscription &subscription : subscriptions_) {
		if (kind == EngineEventKind::Telemetry && !subscription.telemetry)
			continue;
		if (pattern_matches(subscription.pattern, event_name))
			return true;
	}
	return false;
}

void EventDispatcher::invalidate_queued_events_locked(uint64_t revision)
{
	uint64_t highest_lost_revision = revision;
	for (const PendingEvent &queued : queue_)
		highest_lost_revision = std::max(highest_lost_revision, queued.revision);
	queue_.clear();
	resync_pending_ = true;
	resync_revision_ = std::max(resync_revision_, highest_lost_revision);
}

EventPublishResult EventDispatcher::enqueue_telemetry_locked(PendingEvent pending)
{
	for (auto it = queue_.begin(); it != queue_.end(); ++it) {
		if (it->kind == EngineEventKind::Telemetry && it->name == pending.name) {
			queue_.erase(it);
			queue_.push_back(std::move(pending));
			return EventPublishResult::Coalesced;
		}
	}
	if (queue_.size() >= capacity_) {
		++dropped_telemetry_;
		return EventPublishResult::DroppedTelemetry;
	}
	queue_.push_back(std::move(pending));
	cv_.notify_one();
	return EventPublishResult::Enqueued;
}

EventPublishResult EventDispatcher::enqueue_state_locked(PendingEvent pending)
{
	if (queue_.size() < capacity_) {
		queue_.push_back(std::move(pending));
		cv_.notify_one();
		return EventPublishResult::Enqueued;
	}

	auto telemetry = std::find_if(queue_.begin(), queue_.end(), [](const PendingEvent &queued) {
		return queued.kind == EngineEventKind::Telemetry;
	});
	if (telemetry != queue_.end()) {
		queue_.erase(telemetry);
		++dropped_telemetry_;
		queue_.push_back(std::move(pending));
		cv_.notify_one();
		return EventPublishResult::Enqueued;
	}

	invalidate_queued_events_locked(pending.revision);
	cv_.notify_one();
	return EventPublishResult::ResyncRequired;
}

EventPublishResult EventDispatcher::enqueue_event_locked(PendingEvent pending)
{
	if (pending.kind == EngineEventKind::Telemetry)
		return enqueue_telemetry_locked(std::move(pending));
	return enqueue_state_locked(std::move(pending));
}

EventPublishResult EventDispatcher::publish(EngineEventKind kind, std::string_view event_name, uint64_t revision,
					    obs_data_t *data)
{
	if (!is_event_name(event_name))
		return EventPublishResult::InvalidEvent;

	PendingEvent pending;
	pending.kind = kind;
	pending.name.assign(event_name);
	pending.revision = revision;
	pending.data_json = data_to_json(data);

	std::lock_guard lock(mutex_);
	if (stopping_)
		return EventPublishResult::Stopped;
	if (!matches_locked(kind, event_name))
		return EventPublishResult::NotSubscribed;

	return enqueue_event_locked(std::move(pending));
}

EventPublishResult EventDispatcher::try_publish_telemetry(std::string_view event_name, uint64_t revision,
								  obs_data_t *data) noexcept
{
	try {
		if (!is_event_name(event_name))
			return EventPublishResult::InvalidEvent;
		PendingEvent pending;
		pending.kind = EngineEventKind::Telemetry;
		pending.name.assign(event_name);
		pending.revision = revision;
		pending.data_json = data_to_json(data);
		std::unique_lock lock(mutex_, std::try_to_lock);
		if (!lock.owns_lock()) {
			++dropped_telemetry_;
			return EventPublishResult::DroppedTelemetry;
		}
		if (stopping_)
			return EventPublishResult::Stopped;
		if (!matches_locked(EngineEventKind::Telemetry, event_name))
			return EventPublishResult::NotSubscribed;
		return enqueue_telemetry_locked(std::move(pending));
	} catch (...) {
		return EventPublishResult::DroppedTelemetry;
	}
}

void EventDispatcher::require_resync_due_to_overflow(uint64_t revision) noexcept
{
	try {
		std::lock_guard lock(mutex_);
		if (stopping_)
			return;

		invalidate_queued_events_locked(revision);
		cv_.notify_one();
	} catch (...) {
		std::fprintf(stderr, "obs-engine: failed to schedule mandatory resync after event overflow\n");
		std::fflush(stderr);
	}
}

void EventDispatcher::require_resync_after_queued_events(uint64_t revision) noexcept
{
	try {
		PendingEvent event;
		event.kind = EngineEventKind::State;
		event.name = "session.resyncRequired";
		event.revision = revision;
		event.data_json = "{\"reason\":\"event_queue_overflow\"}";

		std::lock_guard lock(mutex_);
		if (stopping_)
			return;
		if (queue_.size() >= capacity_) {
			invalidate_queued_events_locked(revision);
		} else {
			queue_.push_back(std::move(event));
		}
		cv_.notify_one();
	} catch (...) {
		std::fprintf(stderr, "obs-engine: failed to schedule ordered controller resync\n");
		std::fflush(stderr);
	}
}

EventDispatcher::NextEventResult EventDispatcher::wait_for_next_event(PendingEvent &event, uint64_t &resync_revision)
{
	std::unique_lock lock(mutex_);
	cv_.wait(lock, [&] { return stopping_ || resync_pending_ || !queue_.empty(); });
	if (resync_pending_) {
		resync_revision = resync_revision_;
		resync_pending_ = false;
		resync_revision_ = 0;
		return NextEventResult::Resync;
	}
	if (!queue_.empty()) {
		event = std::move(queue_.front());
		queue_.pop_front();
		return NextEventResult::Event;
	}
	return NextEventResult::Stop;
}

void EventDispatcher::run() noexcept
{
	try {
		for (;;) {
			PendingEvent event;
			uint64_t resync_revision = 0;
			switch (wait_for_next_event(event, resync_revision)) {
			case NextEventResult::Stop:
				return;
			case NextEventResult::Resync:
				emit_resync_required(resync_revision);
				break;
			case NextEventResult::Event:
				emit(std::move(event));
				break;
			}
		}
	} catch (const std::exception &error) {
		std::fprintf(stderr, "obs-engine: event dispatcher failed internally: %s\n", error.what());
	} catch (...) {
		std::fprintf(stderr, "obs-engine: event dispatcher failed with an unknown exception\n");
	}
}

void EventDispatcher::emit(PendingEvent event) noexcept
{
	try {
		if (next_seq_ == 0) {
			std::fprintf(stderr, "obs-engine: event sequence space exhausted; event delivery stopped\n");
			return;
		}

		const uint64_t seq = next_seq_;
		if (next_seq_ == std::numeric_limits<uint64_t>::max())
			next_seq_ = 0;
		else
			++next_seq_;

		std::string line;
		line.reserve(96 + event.name.size() + event.data_json.size());
		line += "{\"op\":\"event\",\"seq\":";
		line += std::to_string(seq);
		line += ",\"revision\":";
		line += std::to_string(event.revision);
		line += ",\"event\":\"";
		line += event.name;
		line += "\"";
		if (event.kind == EngineEventKind::Telemetry)
			line += ",\"telemetry\":true";
		line += ",\"data\":";
		line += event.data_json.empty() ? "{}" : event.data_json;
		line += '}';
		write_json_line(std::move(line));
	} catch (...) {
		std::fprintf(stderr, "obs-engine: failed to serialize queued event\n");
	}
}

void EventDispatcher::emit_resync_required(uint64_t revision) noexcept
{
	PendingEvent event;
	event.kind = EngineEventKind::State;
	event.name = "session.resyncRequired";
	event.revision = revision;
	event.data_json = "{\"reason\":\"event_queue_overflow\"}";
	emit(std::move(event));
}

} // namespace obs_engine
