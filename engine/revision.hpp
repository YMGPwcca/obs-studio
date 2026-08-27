#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace obs_engine {

class RevisionState {
public:
	uint64_t current() const noexcept
	{
		return revision_.load(std::memory_order_acquire);
	}

	bool matches(uint64_t expected) const noexcept
	{
		return current() == expected;
	}

	bool can_commit_mutation() const noexcept
	{
		return current() < max_revision();
	}

	uint64_t commit_mutation()
	{
		uint64_t observed = revision_.load(std::memory_order_acquire);
		for (;;) {
			if (observed >= max_revision())
				throw std::overflow_error("revision space exhausted");
			const uint64_t next = observed + 1;
			if (revision_.compare_exchange_weak(observed, next, std::memory_order_acq_rel,
						    std::memory_order_acquire))
				return next;
		}
	}

private:
	static constexpr uint64_t max_revision() noexcept
	{
		return static_cast<uint64_t>(std::numeric_limits<long long>::max());
	}

	std::atomic<uint64_t> revision_{0};
};

} // namespace obs_engine
