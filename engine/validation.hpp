#pragma once

#include <cstddef>
#include <string_view>

namespace obs_engine {

inline bool is_safe_identifier_character(unsigned char ch)
{
	constexpr std::string_view allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.";
	return allowed.find(static_cast<char>(ch)) != std::string_view::npos;
}

inline bool is_safe_identifier(const char *value, size_t max_bytes)
{
	if (!value || !*value)
		return false;

	for (size_t index = 0; value[index] != '\0'; ++index) {
		if (index >= max_bytes)
			return false;
		const unsigned char ch = static_cast<unsigned char>(value[index]);
		if (!is_safe_identifier_character(ch))
			return false;
	}
	return true;
}

} // namespace obs_engine
