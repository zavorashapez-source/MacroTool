#pragma once
#include "util.h"
#include <cstdint>

// free build: no key check, keyauth is never contacted. the stubs in license.cpp
// return empty values so no app name / owner id / url ends up in the exe.
#ifndef FREE_BUILD
#define FREE_BUILD 1
#endif

bool Ed25519Verify(const uint8_t sig[64], const uint8_t *msg, size_t n, const uint8_t pk[32]);

struct LicenseResult
{
    bool ok = false;
    std::wstring error;
    std::wstring plan;    // subscription name, e.g. "default"
    long long expiry = 0; // unix seconds
};

namespace License
{
bool Configured();
// blocking! init + license login bound to this pc
LicenseResult Activate(const std::wstring &key);
std::wstring Hwid();
// key is stored with DPAPI (per windows user)
std::wstring LoadKey();
void SaveKey(const std::wstring &key);
void ForgetKey();
// ed25519 check of timestamp+body against keyauth's public key
bool VerifySigned(const std::string &timestamp, const std::string &body, const std::string &sigHex, long long nowSec,
                  std::wstring *why);
} // namespace License
