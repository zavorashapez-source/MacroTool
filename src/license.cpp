#include "license.h"
#include "json.h"
#include <bcrypt.h>
#include <dpapi.h>
#include <sddl.h>
#include <winhttp.h>
#include <algorithm>
#include <ctime>

namespace
{

// empty in the free build. for keyauth fill in app name, owner id, url and public key.
std::string AppName()
{
    return "";
}
std::string Owner()
{
    return "";
}
std::string Version()
{
    return "1.0";
}
std::wstring ApiUrl()
{
    return L"";
}
std::string ServerKey()
{
    return "";
}

bool HexToBytes(const std::string &hex, uint8_t *out, size_t n)
{
    if (hex.size() != n * 2)
        return false;
    auto v = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < n; i++)
    {
        int a = v(hex[2 * i]), b = v(hex[2 * i + 1]);
        if (a < 0 || b < 0)
            return false;
        out[i] = (uint8_t)(a * 16 + b);
    }
    return true;
}

std::string Hex(const uint8_t *d, size_t n)
{
    static const char *x = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++)
    {
        s += x[d[i] >> 4];
        s += x[d[i] & 15];
    }
    return s;
}

std::string HashHex(LPCWSTR alg, const std::string &data)
{
    BCRYPT_ALG_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&h, alg, nullptr, 0) != 0)
        return {};
    DWORD len = 0, got = 0;
    BCryptGetProperty(h, BCRYPT_HASH_LENGTH, (PUCHAR)&len, sizeof len, &got, 0);
    std::vector<uint8_t> out(len);
    NTSTATUS st = BCryptHash(h, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), out.data(), len);
    BCryptCloseAlgorithmProvider(h, 0);
    return st == 0 ? Hex(out.data(), out.size()) : std::string();
}

std::string UrlEncode(const std::string &s)
{
    std::string o;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            o += (char)c;
        else
        {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            o += b;
        }
    }
    return o;
}

struct Http
{
    bool ok = false;
    DWORD status = 0;
    std::string body, signature, timestamp;
    std::wstring error;
};

std::string Header(HINTERNET req, const wchar_t *name)
{
    DWORD size = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
        return {};
    std::wstring v(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, v.data(), &size, WINHTTP_NO_HEADER_INDEX))
        return {};
    v.resize(size / sizeof(wchar_t));
    return Narrow(Trim(v));
}

Http Post(const std::wstring &url, const std::string &form)
{
    Http r;
    URL_COMPONENTS uc{sizeof uc};
    wchar_t host[256], path[1024];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
    {
        r.error = L"The license server address is invalid.";
        return r;
    }
    HINTERNET s = WinHttpOpen(L"MacroTool/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : nullptr;
    HINTERNET q = c ? WinHttpOpenRequest(c, L"POST", path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
    if (q)
        WinHttpSetTimeouts(q, 8000, 8000, 10000, 15000);
    const wchar_t *hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (q && WinHttpSendRequest(q, hdr, (DWORD)-1L, (LPVOID)form.data(), (DWORD)form.size(), (DWORD)form.size(), 0) &&
        WinHttpReceiveResponse(q, nullptr))
    {
        DWORD size = sizeof r.status;
        WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &r.status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        std::wstring hSig = L"x-signature-ed25519", hTs = L"x-signature-timestamp";
        r.signature = Header(q, hSig.c_str());
        r.timestamp = Header(q, hTs.c_str());
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(q, &avail) && avail)
        {
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(q, chunk.data(), avail, &got) || !got)
                break;
            r.body.append(chunk.data(), got);
            if (r.body.size() > (1 << 20))
                break;
        }
        r.ok = true;
    }
    else
    {
        r.error = L"Couldn't reach the license server. Check your internet connection.";
    }
    if (q)
        WinHttpCloseHandle(q);
    if (c)
        WinHttpCloseHandle(c);
    if (s)
        WinHttpCloseHandle(s);
    return r;
}

std::wstring Field(const json::Value &v, const char *k)
{
    auto *x = v.Get(k);
    if (!x)
        return {};
    if (x->t == json::Value::Str)
        return Widen(x->s);
    if (x->t == json::Value::Num)
        return std::to_wstring((long long)x->n);
    return {};
}

std::string ExeMd5()
{
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, p, (DWORD)std::size(p));
    std::string bytes;
    ReadFileBytes(std::wstring(p, n), bytes);
    return HashHex(BCRYPT_MD5_ALGORITHM, bytes);
}

std::wstring KeyPath()
{
    return PathJoin(ExeDir(), L"license.dat");
}

// returns the json only if the signature checks out
bool Call(const std::string &form, json::Value &out, std::wstring &error)
{
    Http h = Post(ApiUrl(), form);
    if (!h.ok)
    {
        error = h.error;
        return false;
    }
    if (h.body == "KeyAuth_Invalid")
    {
        error = L"This app isn't registered on KeyAuth (check the name and owner ID).";
        return false;
    }
    std::wstring why;
    if (!License::VerifySigned(h.timestamp, h.body, h.signature, (long long)time(nullptr), &why))
    {
        error = L"The license server's answer couldn't be verified (" + why + L").";
        return false;
    }
    if (!json::Parse(h.body, out) || out.t != json::Value::Obj)
    {
        error = L"The license server sent something unexpected.";
        return false;
    }
    if (Field(out, "ownerid") != Widen(Owner()))
    {
        error = L"The license server answered for a different app.";
        return false;
    }
    return true;
}

std::wstring FriendlyError(const json::Value &v)
{
    std::wstring m = Field(v, "message");
    if (m == L"invalidver")
        return L"This version of Macro Tool is outdated. Download the latest one.";
    return m.empty() ? L"The key was rejected." : m;
}

} // namespace

namespace License
{

bool Configured()
{
    return !AppName().empty() && Owner().size() == 10;
}

std::wstring Hwid()
{
    // hwid = user SID + machine guid, hashed
    std::string sid;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        DWORD needed = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
        std::vector<BYTE> buf(needed);
        LPSTR str = nullptr;
        if (needed && GetTokenInformation(token, TokenUser, buf.data(), needed, &needed) &&
            ConvertSidToStringSidA(((TOKEN_USER *)buf.data())->User.Sid, &str))
        {
            sid = str;
            LocalFree(str);
        }
        CloseHandle(token);
    }
    wchar_t guid[64] = L"";
    DWORD sz = sizeof guid;
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                 RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, guid, &sz);
    std::string h = HashHex(BCRYPT_SHA256_ALGORITHM, sid + "|" + Narrow(guid));
    return Widen(h.substr(0, 40));
}

bool VerifySigned(const std::string &timestamp, const std::string &body, const std::string &sigHex, long long nowSec,
                  std::wstring *why)
{
    auto fail = [&](const wchar_t *w) {
        if (why)
            *why = w;
        return false;
    };
    if (sigHex.empty() || timestamp.empty())
        return fail(L"no signature");
    std::string sig = sigHex;
    if (sig.rfind("0x", 0) == 0)
        sig = sig.substr(2);
    uint8_t s[64], pk[32];
    if (!HexToBytes(sig, s, 64))
        return fail(L"bad signature");
    HexToBytes(ServerKey(), pk, 32);
    if (!std::all_of(timestamp.begin(), timestamp.end(), ::isdigit) ||
        (timestamp.size() != 10 && timestamp.size() != 13))
        return fail(L"bad timestamp");
    long long ts = std::stoll(timestamp);
    if (timestamp.size() == 13)
        ts /= 1000;
    if (std::llabs(nowSec - ts) > 120)
        return fail(L"your PC clock is off");
    std::string msg = timestamp + body;
    if (!Ed25519Verify(s, (const uint8_t *)msg.data(), msg.size(), pk))
        return fail(L"signature mismatch");
    return true;
}

LicenseResult Activate(const std::wstring &rawKey)
{
    LicenseResult r;
    if (!Configured())
    {
        r.error = L"This build isn't connected to KeyAuth yet.";
        return r;
    }
    std::string name = AppName(), owner = Owner();
    std::string key = Narrow(Trim(rawKey));
    if (key.empty())
    {
        r.error = L"Enter your license key.";
        return r;
    }
    json::Value init;
    std::string form = "type=init&ver=" + UrlEncode(Version()) + "&hash=" + UrlEncode(ExeMd5()) +
                       "&name=" + UrlEncode(name) + "&ownerid=" + UrlEncode(owner);
    if (!Call(form, init, r.error))
        return r;
    auto *ok = init.Get("success");
    std::wstring session = Field(init, "sessionid");
    if (!ok || ok->t != json::Value::Bool || !ok->b || session.empty())
    {
        r.error = FriendlyError(init);
        return r;
    }
    json::Value lic;
    form = "type=license&key=" + UrlEncode(key) + "&code=&hwid=" + UrlEncode(Narrow(Hwid())) +
           "&sessionid=" + UrlEncode(Narrow(session)) + "&name=" + UrlEncode(name) + "&ownerid=" + UrlEncode(owner);
    if (!Call(form, lic, r.error))
        return r;
    ok = lic.Get("success");
    if (!ok || ok->t != json::Value::Bool || !ok->b)
    {
        r.error = FriendlyError(lic);
        return r;
    }
    if (auto *info = lic.Get("info"); info && info->t == json::Value::Obj)
    {
        if (auto *subs = info->Get("subscriptions"); subs && subs->t == json::Value::Arr)
        {
            for (auto &s : subs->a)
            {
                long long exp = 0;
                std::wstring e = Field(s, "expiry");
                if (!e.empty())
                    exp = _wtoi64(e.c_str());
                if (exp > r.expiry)
                {
                    r.expiry = exp;
                    r.plan = Field(s, "subscription");
                }
            }
        }
    }
    if (r.expiry && r.expiry <= (long long)time(nullptr))
    {
        r.error = L"This key has expired.";
        return r;
    }
    r.ok = true;
    return r;
}

std::wstring LoadKey()
{
    std::string blob;
    if (!ReadFileBytes(KeyPath(), blob) || blob.empty())
        return {};
    DATA_BLOB in{(DWORD)blob.size(), (BYTE *)blob.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return {};
    std::wstring key((wchar_t *)out.pbData, out.cbData / sizeof(wchar_t));
    LocalFree(out.pbData);
    return key;
}

void SaveKey(const std::wstring &key)
{
    DATA_BLOB in{(DWORD)(key.size() * sizeof(wchar_t)), (BYTE *)key.data()}, out{};
    if (!CryptProtectData(&in, L"Macro Tool license", nullptr, nullptr, nullptr, 0, &out))
        return;
    WriteFileAtomic(KeyPath(), std::string((char *)out.pbData, out.cbData));
    LocalFree(out.pbData);
}

void ForgetKey()
{
    DeleteFileW(KeyPath().c_str());
}

} // namespace License
