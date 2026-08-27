#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace obs_engine {

class RevisionState {
public:
	class MutationGuard {
	public:
		MutationGuard(MutationGuard &&) noexcept = default;
		MutationGuard &operator=(MutationGuard &&) = delete;

		MutationGuard(const MutationGuard &) = delete;
		MutationGuard &operator=(const MutationGuard &) = delete;

		uint64_t current() const noexcept
		{
			return owner_.current();
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
			return owner_.commit_mutation_unlocked();
		}

	private:
		friend class RevisionState;

		explicit MutationGuard(RevisionState &owner) : owner_(owner), lock_(owner.mutation_mutex_) {}

		RevisionState &owner_;
		std::unique_lock<std::mutex> lock_;
	};

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

	MutationGuard lock_mutation()
	{
		return MutationGuard(*this);
	}

	uint64_t commit_mutation()
	{
		std::lock_guard lock(mutation_mutex_);
		return commit_mutation_unlocked();
	}

private:
	static constexpr uint64_t max_revision() noexcept
	{
		return static_cast<uint64_t>(std::numeric_limits<long long>::max());
	}

	uint64_t commit_mutation_unlocked()
	{
		const uint64_t observed = revision_.load(std::memory_order_acquire);
		if (observed >= max_revision())
			throw std::overflow_error("revision space exhausted");
		const uint64_t next = observed + 1;
		revision_.store(next, std::memory_order_release);
		return next;
	}

	std::mutex mutation_mutex_;
	std::atomic<uint64_t> revision_{0};
};

} // namespace obs_engine
