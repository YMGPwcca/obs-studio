#include "runtime.hpp"

#include "runtime_phase2_common.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace obs_engine {
namespace {

void reset_result(RuntimeV2Result &result, RuntimeV2Error &error)
{
	result = RuntimeV2Result{};
	error = RuntimeV2Error{};
}

bool fail(RuntimeV2Error &error, const char *code, const char *message)
{
	error.code = code ? code : "internal_error";
	error.message = message ? message : "streaming operation failed";
	return false;
}

bool protocol_supported(const char *protocols, const char *protocol)
{
	if (!protocols || !*protocols || !protocol || !*protocol)
		return false;
	std::string_view remaining(protocols);
	while (!remaining.empty()) {
		const size_t separator = remaining.find(';');
		if (remaining.substr(0, separator) == protocol)
			return true;
		if (separator == std::string_view::npos)
			break;
		remaining.remove_prefix(separator + 1);
	}
	return false;
}

bool streaming_output_compatible(const OutputEntry &entry, RuntimeV2Error &error)
{
	const uint32_t flags = obs_output_get_flags(entry.output);
	if ((flags & OBS_OUTPUT_SERVICE) == 0 || (flags & OBS_OUTPUT_ENCODED) == 0)
		return fail(error, "unsupported_capability", "streaming requires a service-backed encoded Output");
	return true;
}

bool service_has_stream_credential(obs_service_t *service)
{
	static constexpr uint32_t types[] = {
		OBS_SERVICE_CONNECT_INFO_STREAM_KEY,
		OBS_SERVICE_CONNECT_INFO_STREAM_ID,
		OBS_SERVICE_CONNECT_INFO_PASSWORD,
		OBS_SERVICE_CONNECT_INFO_BEARER_TOKEN,
	};
	for (const uint32_t type : types) {
		const char *value = obs_service_get_connect_info(service, type);
		if (value && *value)
			return true;
	}
	return false;
}

bool streaming_service_compatible(const OutputEntry &entry, RuntimeV2Error &error)
{
	obs_service_t *service = obs_output_get_service(entry.output);
	if (!service || !obs_service_initialized(service))
		return fail(error, "invalid_state", "streaming Output requires an initialized Service");
	const char *protocols = obs_output_get_protocols(entry.output);
	const char *protocol = obs_service_get_protocol(service);
	if (protocols && *protocols && !protocol_supported(protocols, protocol))
		return fail(error, "unsupported_capability", "Service protocol is incompatible with streaming Output");
	if (!service_has_stream_credential(service))
		return fail(error, "invalid_state", "streaming Service has no configured credential");
	return true;
}

ObsDataPtr make_streaming_config(uint64_t output_handle, bool configured)
{
	ObsDataPtr data(obs_data_create());
	obs_data_set_bool(data.get(), "configured", configured);
	if (configured)
		phase2_set_handle(data.get(), "output", output_handle);
	else
		obs_data_set_obj(data.get(), "output", nullptr);
	return data;
}

ObsDataPtr streaming_output_params(uint64_t output_handle)
{
	ObsDataPtr params(obs_data_create());
	phase2_set_handle(params.get(), "output", output_handle);
	return params;
}

bool make_streaming_service_params(uint64_t output_handle, obs_data_t *input, ObsDataPtr &forwarded,
					   RuntimeV2Error &error)
{
	uint64_t service_handle = 0;
	bool is_null = false;
	bool present = false;
	if (!phase2_read_nullable_handle(input, "service", service_handle, is_null, present) || !present)
		return fail(error, "bad_request", "params.service must be a canonical service handle string or null");
	forwarded = streaming_output_params(output_handle);
	if (is_null)
		obs_data_set_obj(forwarded.get(), "service", nullptr);
	else
		phase2_set_handle(forwarded.get(), "service", service_handle);
	return true;
}

} // namespace

bool Engine::v2_get_streaming_output(uint64_t &handle, OutputEntry *&entry, RuntimeV2Error &error) const
{
	if (!streaming_.output)
		return fail(error, "invalid_state", "streaming is not configured");
	handle = streaming_.output;
	const auto output = outputs_.find(handle);
	if (output == outputs_.end() || !output->second.output)
		return fail(error, "not_found", "configured streaming Output was not found");
	entry = const_cast<OutputEntry *>(&output->second);
	return streaming_output_compatible(*entry, error);
}

void Engine::v2_prepare_streaming_shutdown() noexcept
{
	streaming_ = StreamingEntry{};
}

bool Engine::v2_streaming_get_config(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	result.data = make_streaming_config(streaming_.output, streaming_.output != 0);
	return true;
}

bool Engine::v2_streaming_configure(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_output_entry(params, output_handle, entry, error))
		return false;
	if (!streaming_output_compatible(*entry, error))
		return false;
	if (streaming_.output && streaming_.output != output_handle)
		return fail(error, "object_in_use", "another Output is already assigned to streaming");
	if (recording_.output == output_handle || replay_.output == output_handle)
		return fail(error, "object_in_use", "Output is already assigned to another convenience role");
	if (!v2_output_is_inactive(*entry, error))
		return false;
	const bool newly_configured = streaming_.output == 0;
	streaming_.output = output_handle;
	result.data = make_streaming_config(output_handle, true);
	if (newly_configured) {
		phase2_append_event(result, "streaming.configChanged", phase2_clone_data(result.data.get()));
		result.mutated = true;
	}
	return true;
}

bool Engine::v2_streaming_unconfigure(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	if (!v2_output_is_inactive(*entry, error))
		return false;
	streaming_ = StreamingEntry{};
	result.data = make_streaming_config(0, false);
	phase2_append_event(result, "streaming.configChanged", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_streaming_start(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error) || !streaming_service_compatible(*entry, error))
		return false;
	return v2_output_start(streaming_output_params(output_handle).get(), result, error);
}

bool Engine::v2_streaming_stop(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	return v2_output_stop(streaming_output_params(output_handle).get(), result, error);
}

bool Engine::v2_streaming_force_stop(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	return v2_output_force_stop(streaming_output_params(output_handle).get(), result, error);
}

bool Engine::v2_streaming_get_state(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	ObsDataPtr data(v2_output_state(output_handle, *entry));
	obs_data_set_bool(data.get(), "configured", true);
	result.data = std::move(data);
	return true;
}

bool Engine::v2_streaming_get_stats(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	return v2_output_get_stats(streaming_output_params(output_handle).get(), result, error);
}

bool Engine::v2_streaming_get_service(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	return v2_output_get_service(streaming_output_params(output_handle).get(), result, error);
}

bool Engine::v2_streaming_set_service(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	ObsDataPtr forwarded;
	if (!make_streaming_service_params(output_handle, params, forwarded, error))
		return false;
	return v2_output_set_service(forwarded.get(), result, error);
}

bool Engine::v2_streaming_get_reconnect_state(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	return v2_output_get_reconnect(streaming_output_params(output_handle).get(), result, error);
}

bool Engine::v2_streaming_get_last_error(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t output_handle = 0;
	OutputEntry *entry = nullptr;
	if (!v2_get_streaming_output(output_handle, entry, error))
		return false;
	return v2_output_get_last_error(streaming_output_params(output_handle).get(), result, error);
}

} // namespace obs_engine
