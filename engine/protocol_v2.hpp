#pragma once

#include "config.hpp"
#include "protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace obs_engine {

class RevisionState;

inline constexpr uint32_t kProtocolV2Major = 2;
inline constexpr uint32_t kProtocolV2Minor = 0;
inline constexpr size_t kMaxV2RequestIdBytes = 128;
inline constexpr size_t kMaxV2MethodBytes = 128;

struct V2Request {
	std::string id;
	std::string method;
	ObsDataPtr params;
	bool has_if_revision = false;
	long long if_revision = 0;
	bool has_timeout_ms = false;
	long long timeout_ms = 0;
};

struct V2ParseError {
	std::string id;
	std::string code;
	std::string message;
};

bool parse_v2_request(obs_data_t *request, V2Request &out, V2ParseError &error);
void send_v2_error(const std::string &request_id, const char *code, const char *message, obs_data_t *details = nullptr,
		   uint64_t revision = 0);
void send_v2_ok(const std::string &request_id, obs_data_t *data = nullptr, uint64_t revision = 0);
bool handle_v2_request(const Config &config, RevisionState &revisions, const V2Request &request);

} // namespace obs_engine
