#pragma once

#include <cstddef>

namespace obs_engine {

inline bool is_safe_identifier(const char *value, size_t max_bytes)
{
	if (!value || !*value)
		return false;

	for (size_t index = 0; value[index] != '\0'; ++index) {
		if (index >= max_bytes)
			return false;
		const unsigned char ch = static_cast<unsigned char>(value[index]);
		const bool alpha_num = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
				       (ch >= '0' && ch <= '9');
		if (!alpha_num && ch != '_' && ch != '-' && ch != '.')
			return false;
	}
	return true;
}

} // namespace obs_engine
