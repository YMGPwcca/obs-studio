#pragma once

#include <obs.h>

#include <cstdint>

// Engine-only additive hooks. They are declared outside the public obs.h API
// and are used solely to correlate a filter settings request with the
// deferred update callback that actually covers its submission.
extern "C" {
bool obs_source_update_tracked(obs_source_t *source, obs_data_t *settings, uint64_t *serial);
bool obs_source_reset_settings_tracked(obs_source_t *source, obs_data_t *settings, uint64_t *serial);
}
