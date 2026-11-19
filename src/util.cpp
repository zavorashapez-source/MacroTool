#include "util.h"
#include <shlobj.h>
#include <cstdarg>
#include <cwctype>
#include <algorithm>

std::wstring Widen(std::string_view s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(std::wstring_view s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), a.data(), n, nullptr, nullptr);
    return a;
}

std::wstring Trim(std::wstring_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b]))
        b++;
    while (e > b && iswspace(s[e - 1]))
        e--;
    return std::wstring(s.substr(b, e - b));
}

std::wstring Lower(std::wstring_view s)
{
    std::wstring r(s);
    for (auto &c : r)
        c = (wchar_t)towlower(c);
    return r;
}

bool IEquals(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (towlower(a[i]) != towlower(b[i]))
            return false;
    return true;
}

bool IEndsWith(std::wstring_view s, std::wstring_view suffix)
{
    return s.size() >= suffix.size() && IEquals(s.substr(s.size() - suffix.size()), suffix);
}

bool IContains(std::wstring_view s, std::wstring_view needle)
{
    return Lower(s).find(Lower(needle)) != std::wstring::npos;
}

bool Contains(std::wstring_view s, std::wstring_view needle)
{
    return s.find(needle) != std::wstring_view::npos;
}

std::vector<std::wstring> Split(std::wstring_view s, std::wstring_view seps, bool trim, bool removeEmpty)
{
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++)
    {
        if (i == s.size() || seps.find(s[i]) != std::wstring_view::npos)
        {
            std::wstring part(s.substr(start, i - start));
            if (trim)
                part = Trim(part);
            if (!(removeEmpty && part.empty()))
                out.push_back(std::move(part));
            start = i + 1;
        }
    }
    return out;
}

std::wstring Join(const std::vector<std::wstring> &parts, std::wstring_view sep)
{
    std::wstring r;
    for (size_t i = 0; i < parts.size(); i++)
    {
        if (i)
            r += sep;
        r += parts[i];
    }
    return r;
}

bool ParseInt(std::wstring_view s, int &v)
{
    std::wstring t = Trim(s);
    if (t.empty() || t.size() > 10)
        return false;
    size_t i = 0;
    bool neg = false;
    if (t[0] == L'+' || t[0] == L'-')
    {
        neg = t[0] == L'-';
        i = 1;
    }
    if (i >= t.size())
        return false;
    long long r = 0;
    for (; i < t.size(); i++)
    {
        if (t[i] < L'0' || t[i] > L'9')
            return false;
        r = r * 10 + (t[i] - L'0');
    }
    if (neg)
        r = -r;
    if (r < INT_MIN || r > INT_MAX)
        return false;
    v = (int)r;
    return true;
}

std::wstring Fmt(const wchar_t *fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}

std::wstring ExeDir()
{
    wchar_t p[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, p, (DWORD)std::size(p));
    std::wstring s(p, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

std::wstring PathJoin(const std::wstring &a, const std::wstring &b)
{
    if (a.empty())
        return b;
    if (a.back() == L'\\' || a.back() == L'/')
        return a + b;
    return a + L"\\" + b;
}

std::wstring FileStem(const std::wstring &path)
{
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

bool FileExists(const std::wstring &path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring &path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileBytes(const std::wstring &path, std::string &out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 64ll * 1024 * 1024)
    {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD got = 0;
    bool ok = size.QuadPart == 0 || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    out.resize(got);
    return ok;
}

bool WriteFileAtomic(const std::wstring &path, const std::string &data)
{
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok)
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::wstring KnownFolder(const GUID &id)
{
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)))
        r = p;
    CoTaskMemFree(p);
    return r;
}

long long QpcNow()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double QpcToMs(long long ticks)
{
    static long long freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return ticks * 1000.0 / (double)freq;
}
