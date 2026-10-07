/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
GPLv2 — see LICENSE for full text.
*/

#include "SecretStore.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>

#include <cctype>
#include <vector>
#endif

namespace smulti {

#ifdef _WIN32
namespace {

char hex_digit(unsigned int v)
{
	return static_cast<char>(v < 10 ? ('0' + v) : ('a' + (v - 10)));
}

int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	return -1;
}

std::string hex_encode(const BYTE *data, DWORD size)
{
	std::string out;
	out.resize(static_cast<size_t>(size) * 2);
	for (DWORD i = 0; i < size; ++i) {
		out[static_cast<size_t>(i) * 2] = hex_digit((data[i] >> 4) & 0xF);
		out[static_cast<size_t>(i) * 2 + 1] = hex_digit(data[i] & 0xF);
	}
	return out;
}

bool hex_decode(const std::string &hex, std::vector<BYTE> &out)
{
	if (hex.size() % 2 != 0)
		return false;

	out.resize(hex.size() / 2);
	for (size_t i = 0; i < out.size(); ++i) {
		const int hi = hex_value(hex[i * 2]);
		const int lo = hex_value(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = static_cast<BYTE>((hi << 4) | lo);
	}
	return true;
}

} // namespace
#endif

bool protect_secret(const std::string &plain, std::string &protected_value)
{
#ifdef _WIN32
	if (plain.empty()) {
		protected_value = "dpapi:";
		return true;
	}

	DATA_BLOB input{};
	input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()));
	input.cbData = static_cast<DWORD>(plain.size());

	DATA_BLOB output{};
	if (!CryptProtectData(&input, L"StreamMulticast stream key", nullptr, nullptr, nullptr,
	                      CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		return false;
	}

	protected_value = "dpapi:" + hex_encode(output.pbData, output.cbData);
	if (output.pbData && output.cbData)
		SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);
	return true;
#else
	(void)plain;
	(void)protected_value;
	return false;
#endif
}

bool unprotect_secret(const std::string &protected_value, std::string &plain)
{
#ifdef _WIN32
	static const std::string prefix = "dpapi:";
	if (protected_value.rfind(prefix, 0) != 0)
		return false;

	const std::string hex = protected_value.substr(prefix.size());
	if (hex.empty()) {
		plain.clear();
		return true;
	}

	std::vector<BYTE> encrypted;
	if (!hex_decode(hex, encrypted))
		return false;

	DATA_BLOB input{};
	input.pbData = encrypted.data();
	input.cbData = static_cast<DWORD>(encrypted.size());

	DATA_BLOB output{};
	if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
	                        CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		return false;
	}

	plain.assign(reinterpret_cast<const char *>(output.pbData),
	             static_cast<size_t>(output.cbData));
	if (output.pbData && output.cbData)
		SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);
	if (!encrypted.empty())
		SecureZeroMemory(encrypted.data(), encrypted.size());
	return true;
#else
	(void)protected_value;
	(void)plain;
	return false;
#endif
}

} // namespace smulti
