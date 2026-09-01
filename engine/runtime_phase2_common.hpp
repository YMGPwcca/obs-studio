#pragma once

#include "runtime.hpp"

#include <string>
#include <string_view>

namespace obs_engine {

void phase2_reset_result(RuntimeV2Result &result, RuntimeV2Error &error);
bool phase2_fail(RuntimeV2Error &error, const char *code, const char *message);

bool phase2_is_bounded_string(std::string_view value, size_t max_bytes, bool allow_empty = false);
bool phase2_read_string(obs_data_t *data, const char *name, std::string &out, bool &present);
bool phase2_read_bool(obs_data_t *data, const char *name, bool &out, bool &present);
bool phase2_read_integer(obs_data_t *data, const char *name, long long &out, bool &present);
bool phase2_read_double(obs_data_t *data, const char *name, double &out, bool &present);
bool phase2_read_object(obs_data_t *data, const char *name, ObsDataPtr &out, bool &present);
bool phase2_read_array(obs_data_t *data, const char *name, ObsArrayPtr &out, bool &present);

bool phase2_parse_handle(std::string_view value, uint64_t &out);
bool phase2_read_handle(obs_data_t *data, const char *name, uint64_t &out);
bool phase2_read_nullable_handle(obs_data_t *data, const char *name, uint64_t &out, bool &is_null, bool &present);
void phase2_set_handle(obs_data_t *data, const char *name, uint64_t handle);

ObsDataPtr phase2_clone_data(obs_data_t *data);
void phase2_append_event(RuntimeV2Result &result, const char *name, ObsDataPtr data);

const char *phase2_scale_filter_name(enum obs_scale_type value);
const char *phase2_blend_method_name(enum obs_blending_method value);
const char *phase2_blend_mode_name(enum obs_blending_type value);
const char *phase2_bounds_type_name(enum obs_bounds_type value);
bool phase2_parse_scale_filter(std::string_view value, enum obs_scale_type &out);
bool phase2_parse_blend_method(std::string_view value, enum obs_blending_method &out);
bool phase2_parse_blend_mode(std::string_view value, enum obs_blending_type &out);
bool phase2_parse_bounds_type(std::string_view value, enum obs_bounds_type &out);

} // namespace obs_engine
