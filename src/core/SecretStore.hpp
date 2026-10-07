/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
GPLv2 — see LICENSE for full text.
*/
#pragma once

#include <string>

namespace smulti {

/**
 * Protect a secret for at-rest storage.
 *
 * Windows uses DPAPI scoped to the current Windows user. Other platforms
 * return false so callers can retain the existing plaintext-compatible
 * format until a native keychain backend is implemented.
 */
bool protect_secret(const std::string &plain, std::string &protected_value);

/**
 * Decode a value produced by protect_secret(). Returns false if the value is
 * malformed or cannot be decrypted for the current Windows user.
 */
bool unprotect_secret(const std::string &protected_value, std::string &plain);

} // namespace smulti
