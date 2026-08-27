#include "protocol.hpp"

#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace obs_engine {
namespace {

constexpr size_t kProtocolWriterQueueCapacity = 1024;

class ProtocolWriter {
public:
	bool start()
	{
		std::lock_guard lock(mutex_);
		if (state_ == State::Running)
			return true;
		if (state_ != State::NeverStarted)
			return false;

		try {
			worker_ = std::thread(&ProtocolWriter::run, this);
		} catch (...) {
			return false;
		}
		state_ = State::Running;
		return true;
	}

	void stop() noexcept
	{
		{
			std::lock_guard lock(mutex_);
			if (state_ != State::Running)
				return;
			state_ = State::Stopping;
		}
		cv_.notify_all();
		if (worker_.joinable())
			worker_.join();

		std::lock_guard lock(mutex_);
		state_ = State::Stopped;
	}

	void enqueue(std::string line)
	{
		std::unique_lock lock(mutex_);
		if (state_ == State::NeverStarted) {
			lock.unlock();
			write_direct(line);
			return;
		}
		if (state_ != State::Running)
			return;

		cv_.wait(lock, [&] { return state_ != State::Running || queue_.size() < kProtocolWriterQueueCapacity; });
		if (state_ != State::Running)
			return;

		queue_.push_back(std::move(line));
		lock.unlock();
		cv_.notify_all();
	}

private:
	enum class State { NeverStarted, Running, Stopping, Stopped };

	static void write_direct(const std::string &line) noexcept
	{
		std::fputs(line.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}

	void run() noexcept
	{
		for (;;) {
			std::string line;
			{
				std::unique_lock lock(mutex_);
				cv_.wait(lock, [&] { return state_ == State::Stopping || !queue_.empty(); });
				if (queue_.empty()) {
					if (state_ == State::Stopping)
						break;
					continue;
				}
				line = std::move(queue_.front());
				queue_.pop_front();
			}
			cv_.notify_all();
			write_direct(line);
		}
	}

	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<std::string> queue_;
	std::thread worker_;
	State state_ = State::NeverStarted;
};

ProtocolWriter &protocol_writer()
{
	static ProtocolWriter writer;
	return writer;
}

} // namespace

ReadLineResult read_line_limited(std::string &line)
{
	line.clear();
	bool too_long = false;
	bool saw_data = false;

	for (;;) {
		const int ch = std::fgetc(stdin);
		if (ch == EOF) {
			if (!saw_data)
				return ReadLineResult::Eof;
			break;
		}

		saw_data = true;
		if (ch == '\n')
			break;

		if (!too_long) {
			if (line.size() >= kMaxMessageBytes)
				too_long = true;
			else
				line.push_back(static_cast<char>(ch));
		}
	}

	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	return too_long ? ReadLineResult::TooLong : ReadLineResult::Ok;
}

bool start_protocol_writer()
{
	return protocol_writer().start();
}

void stop_protocol_writer() noexcept
{
	protocol_writer().stop();
}

void write_json_line(std::string json)
{
	protocol_writer().enqueue(std::move(json));
}

void write_json(obs_data_t *data)
{
	const char *json = obs_data_get_json(data);
	write_json_line(json ? json : "{}");
}

void send_error(long long request_id, const char *code, const char *message)
{
	ObsDataPtr response(obs_data_create());
	obs_data_set_int(response.get(), "id", request_id);
	obs_data_set_bool(response.get(), "ok", false);
	obs_data_set_string(response.get(), "error", code);
	obs_data_set_string(response.get(), "message", message);
	write_json(response.get());
}

void send_ok(long long request_id, obs_data_t *result)
{
	ObsDataPtr response(obs_data_create());
	obs_data_set_int(response.get(), "id", request_id);
	obs_data_set_bool(response.get(), "ok", true);
	if (result)
		obs_data_set_obj(response.get(), "result", result);
	write_json(response.get());
}

bool read_integer(obs_data_t *data, const char *name, long long &out, bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	const bool valid = obs_data_item_gettype(item) == OBS_DATA_NUMBER &&
			   obs_data_item_numtype(item) == OBS_DATA_NUM_INT;
	if (valid)
		out = obs_data_item_get_int(item);
	obs_data_item_release(&item);
	return valid;
}

bool request_handle(obs_data_t *request, const char *field, uint64_t &out, bool allow_zero)
{
	long long raw = 0;
	bool present = false;
	if (!read_integer(request, field, raw, present) || !present)
		return false;
	if (raw < 0 || (!allow_zero && raw == 0))
		return false;
	out = static_cast<uint64_t>(raw);
	return true;
}

bool read_finite_double(obs_data_t *data, const char *name, double min_value, double max_value, double &out,
			bool &present)
{
	obs_data_item_t *item = obs_data_item_byname(data, name);
	if (!item) {
		present = false;
		return true;
	}
	present = true;
	if (obs_data_item_gettype(item) != OBS_DATA_NUMBER) {
		obs_data_item_release(&item);
		return false;
	}
	const double value = obs_data_item_get_double(item);
	obs_data_item_release(&item);
	if (!std::isfinite(value) || value < min_value || value > max_value)
		return false;
	out = value;
	return true;
}

} // namespace obs_engine
