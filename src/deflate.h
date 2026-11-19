#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Raw DEFLATE (RFC 1951), compatible with .NET DeflateStream share codes.
bool Inflate(const uint8_t *src, size_t n, std::vector<uint8_t> &out, size_t maxOut);
std::vector<uint8_t> Deflate(const uint8_t *src, size_t n);

std::string Base64UrlEncode(const std::vector<uint8_t> &data);
// url-safe or normal alphabet, padding optional, whitespace ignored
bool Base64Decode(const std::string &text, std::vector<uint8_t> &out);
