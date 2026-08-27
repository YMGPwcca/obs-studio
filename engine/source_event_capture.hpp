#pragma once

#include <thread>

namespace obs_engine {

enum class SourceEventCaptureRoute { Direct, Capture, Defer };

// Caller synchronization is required. SourceV2State holds its mutex whenever
// this gate is inspected or changed.
class SourceEventCaptureGate {
public:
	void begin() noexcept
	{
		owner_ = std::this_thread::get_id();
		active_ = true;
	}

	void end() noexcept
	{
		active_ = false;
		owner_ = {};
	}

	bool active() const noexcept
	{
		return active_;
	}

	SourceEventCaptureRoute route_for_current_thread() const noexcept
	{
		if (!active_)
			return SourceEventCaptureRoute::Direct;
		return owner_ == std::this_thread::get_id() ? SourceEventCaptureRoute::Capture
							      : SourceEventCaptureRoute::Defer;
	}

private:
	std::thread::id owner_{};
	bool active_ = false;
};

} // namespace obs_engine
