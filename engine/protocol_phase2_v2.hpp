#pragma once

#include "protocol_v2.hpp"

#include <string_view>

namespace obs_engine {

bool is_phase2_method(std::string_view method);
bool handle_phase2_request(Engine &engine, RevisionState &revisions, EventDispatcher &events,
				   const V2Request &request);

} // namespace obs_engine
