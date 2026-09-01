#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

bool argument(std::string_view value, std::string_view name, std::string &out)
{
	const std::string prefix = "--" + std::string(name) + "=";
	if (!value.starts_with(prefix))
		return false;
	out.assign(value.substr(prefix.size()));
	return true;
}

bool parse_unsigned(std::string_view value, uint64_t &out, int base)
{
	if (value.empty())
		return false;
	uint64_t parsed = 0;
	const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, base);
	if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
		return false;
	out = parsed;
	return true;
}

bool parse_luid(std::string_view value, LUID &out)
{
	const size_t separator = value.find('-');
	if (separator == std::string_view::npos)
		return false;
	uint64_t high = 0;
	uint64_t low = 0;
	if (!parse_unsigned(value.substr(0, separator), high, 16) ||
	    !parse_unsigned(value.substr(separator + 1), low, 16) || high > std::numeric_limits<uint32_t>::max() ||
	    low > std::numeric_limits<uint32_t>::max())
		return false;
	out.HighPart = static_cast<LONG>(static_cast<uint32_t>(high));
	out.LowPart = static_cast<DWORD>(low);
	return true;
}

uint64_t fnv1a_rows(const D3D11_MAPPED_SUBRESOURCE &mapped, uint32_t width, uint32_t height,
			    uint8_t center[4])
{
	uint64_t hash = 1469598103934665603ULL;
	for (uint32_t row = 0; row < height; ++row) {
		const auto *pixels = static_cast<const uint8_t *>(mapped.pData) + row * mapped.RowPitch;
		for (uint32_t byte = 0; byte < width * 4; ++byte) {
			hash ^= pixels[byte];
			hash *= 1099511628211ULL;
		}
	}
	const auto *middle = static_cast<const uint8_t *>(mapped.pData) + (height / 2) * mapped.RowPitch + (width / 2) * 4;
	for (size_t index = 0; index < 4; ++index)
		center[index] = middle[index];
	return hash;
}

int fail(const char *message, HRESULT hr = S_OK)
{
	if (hr == S_OK)
		std::printf("{\"ok\":false,\"error\":\"%s\"}\n", message);
	else
		std::printf("{\"ok\":false,\"error\":\"%s\",\"hr\":%lu}\n", message,
			    static_cast<unsigned long>(hr));
	return 1;
}

} // namespace

int main(int argc, char **argv)
{
	uint64_t shared_handle_value = 0;
	uint64_t expected_width = 0;
	uint64_t expected_height = 0;
	uint64_t expected_frames = 60;
	uint64_t timeout_ms = 250;
	LUID expected_luid = {};
	bool have_luid = false;
	for (int index = 1; index < argc; ++index) {
		const std::string_view arg(argv[index]);
		std::string value;
		if (argument(arg, "shared-handle", value)) {
			if (!parse_unsigned(value, shared_handle_value, 10))
				return fail("invalid shared handle");
		} else if (argument(arg, "width", value)) {
			if (!parse_unsigned(value, expected_width, 10))
				return fail("invalid width");
		} else if (argument(arg, "height", value)) {
			if (!parse_unsigned(value, expected_height, 10))
				return fail("invalid height");
		} else if (argument(arg, "frames", value)) {
			if (!parse_unsigned(value, expected_frames, 10) || expected_frames == 0)
				return fail("invalid frame count");
		} else if (argument(arg, "adapter-luid", value)) {
			if (!parse_luid(value, expected_luid))
				return fail("invalid adapter LUID");
			have_luid = true;
		} else if (argument(arg, "timeout-ms", value)) {
			if (!parse_unsigned(value, timeout_ms, 10) || timeout_ms > std::numeric_limits<DWORD>::max())
				return fail("invalid timeout");
		}
	}
	if (shared_handle_value == 0 || expected_width == 0 || expected_height == 0 || !have_luid)
		return fail("shared handle, dimensions, and adapter LUID are required");

	ComPtr<IDXGIFactory1> factory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
	if (FAILED(hr))
		return fail("CreateDXGIFactory1 failed", hr);
	ComPtr<IDXGIAdapter1> selected_adapter;
	for (UINT index = 0;; ++index) {
		ComPtr<IDXGIAdapter1> candidate;
		hr = factory->EnumAdapters1(index, candidate.GetAddressOf());
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;
		if (FAILED(hr))
			return fail("EnumAdapters1 failed", hr);
		DXGI_ADAPTER_DESC1 description = {};
		if (FAILED(candidate->GetDesc1(&description)))
			continue;
		if (description.AdapterLuid.HighPart == expected_luid.HighPart &&
		    description.AdapterLuid.LowPart == expected_luid.LowPart) {
			selected_adapter = candidate;
			break;
		}
	}
	if (!selected_adapter)
		return fail("requested adapter LUID was not found");

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
	const D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	hr = D3D11CreateDevice(selected_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			       feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION, device.GetAddressOf(), &feature_level,
			       context.GetAddressOf());
	if (FAILED(hr))
		return fail("D3D11CreateDevice failed", hr);

	ComPtr<ID3D11Texture2D> shared_texture;
	const HANDLE shared_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(shared_handle_value));
	hr = device->OpenSharedResource(shared_handle, IID_PPV_ARGS(shared_texture.GetAddressOf()));
	if (FAILED(hr))
		return fail("OpenSharedResource failed", hr);
	D3D11_TEXTURE2D_DESC description = {};
	shared_texture->GetDesc(&description);
	if (description.Width != expected_width || description.Height != expected_height ||
	    description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
		return fail("shared texture descriptor mismatch");

	ComPtr<IDXGIKeyedMutex> keyed_mutex;
	hr = shared_texture.As(&keyed_mutex);
	if (FAILED(hr))
		return fail("shared texture did not expose keyed mutex", hr);
	D3D11_TEXTURE2D_DESC staging_description = description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	staging_description.MiscFlags = 0;
	ComPtr<ID3D11Texture2D> staging;
	hr = device->CreateTexture2D(&staging_description, nullptr, staging.GetAddressOf());
	if (FAILED(hr))
		return fail("staging texture creation failed", hr);

	std::vector<uint64_t> checksums;
	checksums.reserve(static_cast<size_t>(expected_frames));
	uint8_t first_center[4] = {};
	uint8_t last_center[4] = {};
	uint64_t timeouts = 0;
	for (uint64_t frame = 0; frame < expected_frames; ++frame) {
		hr = keyed_mutex->AcquireSync(1, static_cast<DWORD>(timeout_ms));
		if (hr == WAIT_TIMEOUT) {
			timeouts++;
			continue;
		}
		if (FAILED(hr))
			return fail("keyed mutex acquire failed", hr);
		context->CopyResource(staging.Get(), shared_texture.Get());
		context->Flush();
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr)) {
			keyed_mutex->ReleaseSync(0);
			return fail("staging map failed", hr);
		}
		uint8_t center[4] = {};
		const uint64_t checksum = fnv1a_rows(mapped, description.Width, description.Height, center);
		context->Unmap(staging.Get(), 0);
		if (checksums.empty())
			std::copy(std::begin(center), std::end(center), std::begin(first_center));
		std::copy(std::begin(center), std::end(center), std::begin(last_center));
		checksums.push_back(checksum);
		hr = keyed_mutex->ReleaseSync(0);
		if (FAILED(hr))
			return fail("keyed mutex release failed", hr);
	}
	if (checksums.empty())
		return fail("no frames acquired");
	uint64_t unique = 0;
	for (size_t index = 0; index < checksums.size(); ++index) {
		bool seen = false;
		for (size_t previous = 0; previous < index; ++previous)
			seen = seen || checksums[previous] == checksums[index];
		if (!seen)
			unique++;
	}
	std::printf(
		"{\"ok\":true,\"frames\":%zu,\"timeouts\":%llu,\"uniqueChecksums\":%llu,"
		"\"firstChecksum\":\"%llu\",\"lastChecksum\":\"%llu\","
		"\"firstCenterB\":%u,\"firstCenterG\":%u,\"firstCenterR\":%u,\"firstCenterA\":%u,"
		"\"lastCenterB\":%u,\"lastCenterG\":%u,\"lastCenterR\":%u,\"lastCenterA\":%u}\n",
		checksums.size(), static_cast<unsigned long long>(timeouts), static_cast<unsigned long long>(unique),
		static_cast<unsigned long long>(checksums.front()), static_cast<unsigned long long>(checksums.back()),
		first_center[0], first_center[1], first_center[2], first_center[3], last_center[0], last_center[1],
		last_center[2], last_center[3]);
	return 0;
}
