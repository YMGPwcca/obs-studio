#pragma once

#include <obs.h>

#include <cstdint>
#include <memory>
#include <string>

namespace obs_engine {

inline constexpr uint32_t kProtocolVersion = 1;
inline constexpr size_t kMaxMessageBytes = 256 * 1024;

struct ObsDataDeleter {
	void operator()(obs_data_t *value) const
	{
		if (value)
			obs_data_release(value);
	}
};

struct ObsArrayDeleter {
	void operator()(obs_data_array_t *value) const
	{
		if (value)
			obs_data_array_release(value);
	}
};

using ObsDataPtr = std::unique_ptr<obs_data_t, ObsDataDeleter>;
using ObsArrayPtr = std::unique_ptr<obs_data_array_t, ObsArrayDeleter>;

enum class ReadLineResult { Ok, Eof, TooLong };

ReadLineResult read_line_limited(std::string &line);
bool start_protocol_writer();
void stop_protocol_writer() noexcept;
void write_json_line(std::string json);
void write_json(obs_data_t *data);
void send_error(long long request_id, const char *code, const char *message);
void send_ok(long long request_id, obs_data_t *result = nullptr);
bool read_integer(obs_data_t *data, const char *name, long long &out, bool &present);
bool request_handle(obs_data_t *request, const char *field, uint64_t &out, bool allow_zero = false);
bool read_finite_double(obs_data_t *data, const char *name, double min_value, double max_value, double &out,
			bool &present);

} // namespace obs_engine
