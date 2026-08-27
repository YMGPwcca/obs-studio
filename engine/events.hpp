#pragma once

#include <obs.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace obs_engine {

inline constexpr size_t kDefaultEventQueueCapacity = 1024;
inline constexpr size_t kMaxEventSubscriptions = 256;
inline constexpr size_t kMaxEventPatternBytes = 128;

enum class EngineEventKind { State, Telemetry };

enum class EventPublishResult {
	InvalidEvent,
	NotSubscribed,
	Enqueued,
	Coalesced,
	DroppedTelemetry,
	ResyncRequired,
	Stopped,
};

struct EventSubscription {
	std::string pattern;
	bool telemetry = false;
};

bool is_valid_event_pattern(std::string_view pattern);

class EventDispatcher {
public:
	explicit EventDispatcher(size_t capacity = kDefaultEventQueueCapacity);
	~EventDispatcher();

	EventDispatcher(const EventDispatcher &) = delete;
	EventDispatcher &operator=(const EventDispatcher &) = delete;

	void start();
	void stop_and_drain() noexcept;

	bool subscribe(const std::vector<EventSubscription> &requested, std::string &error);
	bool unsubscribe(const std::vector<std::string> &patterns, std::string &error);
	std::vector<EventSubscription> subscriptions() const;

	EventPublishResult publish(EngineEventKind kind, std::string_view event_name, uint64_t revision,
				   obs_data_t *data = nullptr);

private:
	struct PendingEvent {
		EngineEventKind kind = EngineEventKind::State;
		std::string name;
		uint64_t revision = 0;
		std::string data_json;
	};

	bool matches_locked(EngineEventKind kind, std::string_view event_name) const;
	void run() noexcept;
	void emit(PendingEvent event) noexcept;
	void emit_resync_required(uint64_t revision) noexcept;

	const size_t capacity_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<PendingEvent> queue_;
	std::vector<EventSubscription> subscriptions_;
	std::thread worker_;
	bool started_ = false;
	bool stopping_ = false;
	bool resync_pending_ = false;
	uint64_t resync_revision_ = 0;
	uint64_t next_seq_ = 1;
	uint64_t dropped_telemetry_ = 0;
};

} // namespace obs_engine
