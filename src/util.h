#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

// parse error, msg is shown to the user
struct FormatError {
    std::wstring msg;
};

std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);
std::wstring Trim(std::wstring_view s);
std::wstring Lower(std::wstring_view s);
bool IEquals(std::wstring_view a, std::wstring_view b);
bool IEndsWith(std::wstring_view s, std::wstring_view suffix);
bool IContains(std::wstring_view s, std::wstring_view needle);
bool Contains(std::wstring_view s, std::wstring_view needle);
// split on any char in seps
std::vector<std::wstring> Split(std::wstring_view s, std::wstring_view seps, bool trim, bool removeEmpty);
std::wstring Join(const std::vector<std::wstring>& parts, std::wstring_view sep);
bool ParseInt(std::wstring_view s, int& v);
std::wstring Fmt(const wchar_t* fmt, ...);

std::wstring ExeDir();
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring FileStem(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool DirExists(const std::wstring& path);
bool ReadFileBytes(const std::wstring& path, std::string& out);
// temp file + rename so a crash can't leave half a file
bool WriteFileAtomic(const std::wstring& path, const std::string& data);
std::wstring KnownFolder(const GUID& id);

long long QpcNow();
double QpcToMs(long long ticks);
