#pragma once
#include "util.h"
#include <cstdint>

// This is the free, no-key public release: the app always runs, the activation
// gate is removed, and KeyAuth is never contacted. The license.cpp stubs below
// return empty values (no app name, owner ID or server URL ship in the binary).
#ifndef FREE_BUILD
#define FREE_BUILD 1
#endif

bool Ed25519Verify(const uint8_t sig[64], const uint8_t* msg, size_t n, const uint8_t pk[32]);

struct LicenseResult {
    bool ok = false;
    std::wstring error;
    std::wstring plan;     // subscription name, e.g. "default"
    long long expiry = 0;  // unix seconds
};

namespace License {
bool Configured();
// Talks to KeyAuth (blocking, call off the UI thread): init, then license login bound to this PC.
LicenseResult Activate(const std::wstring& key);
std::wstring Hwid();
// The key is kept encrypted for this Windows user (DPAPI) so it doesn't need typing each launch.
std::wstring LoadKey();
void SaveKey(const std::wstring& key);
void ForgetKey();
// Checks a signed KeyAuth response: Ed25519 over timestamp + body with KeyAuth's public key.
bool VerifySigned(const std::string& timestamp, const std::string& body, const std::string& sigHex, long long nowSec,
                  std::wstring* why);
}  // namespace License
