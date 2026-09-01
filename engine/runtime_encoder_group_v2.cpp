#include "runtime.hpp"

#include "runtime_phase2_common.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

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
	error.message = message ? message : "encoder group operation failed";
	return false;
}

void set_nullable_group(obs_data_t *data, uint64_t handle)
{
	if (handle)
		phase2_set_handle(data, "group", handle);
	else
		obs_data_set_obj(data, "group", nullptr);
}

ObsDataPtr make_group_summary(uint64_t handle, const EncoderGroupEntry &entry, bool active)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "group", handle);
	obs_data_set_bool(data.get(), "active", active);
	obs_data_set_int(data.get(), "count", static_cast<long long>(entry.encoders.size()));
	ObsArrayPtr members(obs_data_array_create());
	for (const uint64_t encoder : entry.encoders) {
		ObsDataPtr member(obs_data_create());
		phase2_set_handle(member.get(), "encoder", encoder);
		obs_data_array_push_back(members.get(), member.get());
	}
	obs_data_set_array(data.get(), "encoders", members.get());
	return data;
}

ObsDataPtr make_encoder_group_change(uint64_t encoder, uint64_t group)
{
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "encoder", encoder);
	set_nullable_group(data.get(), group);
	return data;
}

void append_group_mutation_events(RuntimeV2Result &result, uint64_t group, uint64_t encoder,
					  const EncoderGroupEntry &entry, bool active)
{
	phase2_append_event(result, "encoderGroup.changed", make_group_summary(group, entry, active));
	phase2_append_event(result, "encoder.groupChanged", make_encoder_group_change(encoder, group));
}

} // namespace

bool Engine::v2_get_encoder_group_entry(obs_data_t *params, uint64_t &handle, EncoderGroupEntry *&entry,
					RuntimeV2Error &error) const
{
	if (!phase2_read_handle(params, "group", handle))
		return fail(error, "bad_request", "params.group must be a canonical decimal encoder group handle string");
	const auto it = encoder_groups_.find(handle);
	if (it == encoder_groups_.end() || !it->second.group)
		return fail(error, "not_found", "encoder group handle was not found");
	entry = const_cast<EncoderGroupEntry *>(&it->second);
	return true;
}

bool Engine::v2_encoder_group_is_active(const EncoderGroupEntry &entry) const
{
	for (const uint64_t handle : entry.encoders) {
		const auto it = encoders_.find(handle);
		if (it != encoders_.end() && it->second.encoder && obs_encoder_active(it->second.encoder))
			return true;
	}
	return false;
}

void Engine::v2_prepare_encoder_group_shutdown() noexcept
{
	for (auto &[group_handle, group] : encoder_groups_) {
		for (const uint64_t encoder_handle : group.encoders) {
			auto encoder = encoders_.find(encoder_handle);
			if (encoder == encoders_.end() || encoder->second.group != group_handle || !encoder->second.encoder)
				continue;
			if (!obs_encoder_active(encoder->second.encoder) &&
			    obs_encoder_set_group(encoder->second.encoder, nullptr))
				encoder->second.group = 0;
			else
				std::fprintf(stderr, "obs-engine: encoder group %llu could not detach encoder %llu during shutdown\n",
					     static_cast<unsigned long long>(group_handle),
					     static_cast<unsigned long long>(encoder_handle));
		}
		if (group.group)
			obs_encoder_group_destroy(group.group);
		group.group = nullptr;
	}
	encoder_groups_.clear();
}

bool Engine::v2_encoder_group_list(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	std::vector<uint64_t> handles;
	handles.reserve(encoder_groups_.size());
	for (const auto &[handle, _] : encoder_groups_)
		handles.push_back(handle);
	std::sort(handles.begin(), handles.end());
	ObsArrayPtr groups(obs_data_array_create());
	for (const uint64_t handle : handles) {
		const auto it = encoder_groups_.find(handle);
		if (it != encoder_groups_.end())
			obs_data_array_push_back(groups.get(),
						make_group_summary(handle, it->second, v2_encoder_group_is_active(it->second)).get());
	}
	ObsDataPtr data(obs_data_create());
	obs_data_set_array(data.get(), "groups", groups.get());
	result.data = std::move(data);
	return true;
}

bool Engine::v2_encoder_group_get(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderGroupEntry *entry = nullptr;
	if (!v2_get_encoder_group_entry(params, handle, entry, error))
		return false;
	result.data = make_group_summary(handle, *entry, v2_encoder_group_is_active(*entry));
	return true;
}

bool Engine::v2_encoder_group_create(obs_data_t *, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	obs_encoder_group_t *group = obs_encoder_group_create();
	if (!group)
		return fail(error, "obs_error", "libobs encoder group creation failed");
	const uint64_t handle = allocate_handle();
	EncoderGroupEntry entry;
	entry.group = group;
	try {
		if (!encoder_groups_.emplace(handle, std::move(entry)).second)
			throw std::runtime_error("encoder group handle collision");
	} catch (...) {
		obs_encoder_group_destroy(group);
		throw;
	}
	result.data = make_group_summary(handle, encoder_groups_.at(handle), false);
	phase2_append_event(result, "encoderGroup.created", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_group_remove(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderGroupEntry *entry = nullptr;
	if (!v2_get_encoder_group_entry(params, handle, entry, error))
		return false;
	if (!entry->encoders.empty() || v2_encoder_group_is_active(*entry))
		return fail(error, "object_in_use", "encoder group must be empty and inactive before removal");
	obs_encoder_group_destroy(entry->group);
	encoder_groups_.erase(handle);
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "group", handle);
	result.data = std::move(data);
	phase2_append_event(result, "encoderGroup.removed", phase2_clone_data(result.data.get()));
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_group_add(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t group_handle = 0;
	EncoderGroupEntry *group = nullptr;
	if (!v2_get_encoder_group_entry(params, group_handle, group, error))
		return false;
	uint64_t encoder_handle = 0;
	EncoderEntry *encoder = nullptr;
	if (!v2_get_encoder_entry(params, encoder_handle, encoder, error))
		return false;
	if (encoder->group == group_handle)
		return fail(error, "already_exists", "encoder is already a member of this group");
	if (encoder->group)
		return fail(error, "object_in_use", "encoder is already a member of another group");
	if (v2_encoder_group_is_active(*group) || obs_encoder_active(encoder->encoder) ||
	    obs_encoder_initialized(encoder->encoder))
		return fail(error, "busy", "group membership can only change before encoders are active or initialized");
	if (!obs_encoder_set_group(encoder->encoder, group->group))
		return fail(error, "invalid_state", "libobs rejected encoder group membership");
	encoder->group = group_handle;
	group->encoders.push_back(encoder_handle);
	result.data = make_group_summary(group_handle, *group, false);
	append_group_mutation_events(result, group_handle, encoder_handle, *group, false);
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_group_remove_encoder(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t group_handle = 0;
	EncoderGroupEntry *group = nullptr;
	if (!v2_get_encoder_group_entry(params, group_handle, group, error))
		return false;
	uint64_t encoder_handle = 0;
	EncoderEntry *encoder = nullptr;
	if (!v2_get_encoder_entry(params, encoder_handle, encoder, error))
		return false;
	if (encoder->group != group_handle)
		return fail(error, "not_found", "encoder is not a member of this group");
	if (v2_encoder_group_is_active(*group) || obs_encoder_active(encoder->encoder) ||
	    obs_encoder_initialized(encoder->encoder))
		return fail(error, "busy", "group membership can only change before encoders are active or initialized");
	if (!obs_encoder_set_group(encoder->encoder, nullptr))
		return fail(error, "invalid_state", "libobs rejected encoder group removal");
	encoder->group = 0;
	const auto member = std::find(group->encoders.begin(), group->encoders.end(), encoder_handle);
	if (member != group->encoders.end())
		group->encoders.erase(member);
	result.data = make_group_summary(group_handle, *group, false);
	append_group_mutation_events(result, group_handle, encoder_handle, *group, false);
	/* The group changed event above describes the post-removal group. Replace
	 * the relationship event with the canonical null membership value. */
	result.events.back().data = make_encoder_group_change(encoder_handle, 0);
	result.mutated = true;
	return true;
}

bool Engine::v2_encoder_group_get_encoders(obs_data_t *params, RuntimeV2Result &result, RuntimeV2Error &error)
{
	reset_result(result, error);
	uint64_t handle = 0;
	EncoderGroupEntry *entry = nullptr;
	if (!v2_get_encoder_group_entry(params, handle, entry, error))
		return false;
	ObsArrayPtr encoders(obs_data_array_create());
	for (const uint64_t encoder : entry->encoders) {
		ObsDataPtr data(obs_data_create());
		phase2_set_handle(data.get(), "encoder", encoder);
		obs_data_array_push_back(encoders.get(), data.get());
	}
	ObsDataPtr data(obs_data_create());
	phase2_set_handle(data.get(), "group", handle);
	obs_data_set_array(data.get(), "encoders", encoders.get());
	result.data = std::move(data);
	return true;
}

} // namespace obs_engine
