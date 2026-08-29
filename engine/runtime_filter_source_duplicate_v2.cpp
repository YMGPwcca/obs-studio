#include "runtime.hpp"

namespace obs_engine {

void Engine::v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source, RuntimeV2Result *result,
					       uint64_t duplicate_of)
{
	// Task 8 owns source.duplicate semantics. The fourth argument records that
	// this registration came from nested source duplication, but Task 11 does
	// not synthesize filter.created events into the already-accepted source
	// command. Attached copies become discoverable through filter.list.
	(void)duplicate_of;
	v2_filter_register_source_filters(source_id, source, result);
}

} // namespace obs_engine
