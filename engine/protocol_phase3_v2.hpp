#pragma once

#include <string_view>

namespace obs_engine {

class Engine;
class EventDispatcher;
class RevisionState;
struct V2Request;

bool is_phase3_method(std::string_view method);
bool handle_phase3_request(Engine &engine, RevisionState &revisions, EventDispatcher &events,
			   const V2Request &request);

} // namespace obs_engine
