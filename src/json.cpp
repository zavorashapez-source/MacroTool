#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json
{

Value Value::Boolean(bool v)
{
    Value x;
    x.t = Bool;
    x.b = v;
    return x;
}
Value Value::Number(double v)
{
    Value x;
    x.t = Num;
    x.n = v;
    return x;
}
Value Value::String(std::string v)
{
    Value x;
    x.t = Str;
    x.s = std::move(v);
    return x;
}
Value Value::Array()
{
    Value x;
    x.t = Arr;
    return x;
}
Value Value::Object()
{
    Value x;
    x.t = Obj;
    return x;
}

static bool KeyEq(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
    }
    return true;
}

const Value *Value::Get(std::string_view key) const
{
    if (t != Obj)
        return nullptr;
    for (auto &m : o)
        if (m.k == key)
            return &m.v;
    for (auto &m : o)
        if (KeyEq(m.k, key))
            return &m.v;
    return nullptr;
}

void Value::Set(std::string key, Value v)
{
    t = Obj;
    o.push_back({std::move(key), std::move(v)});
}

void Value::Push(Value v)
{
    t = Arr;
    a.push_back(std::move(v));
}

namespace
{

struct Parser
{
    const char *p;
    const char *e;
    int depth = 0;

    void Ws()
    {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
    }

    static void Utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80)
        {
            out += (char)cp;
        }
        else if (cp < 0x800)
        {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool Hex4(unsigned &v)
    {
        if (e - p < 4)
            return false;
        v = 0;
        for (int i = 0; i < 4; i++)
        {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= c - '0';
            else if (c >= 'a' && c <= 'f')
                v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                v |= c - 'A' + 10;
            else
                return false;
        }
        return true;
    }

    bool Str(std::string &out)
    {
        if (p >= e || *p != '"')
            return false;
        p++;
        while (p < e)
        {
            char c = *p++;
            if (c == '"')
                return true;
            if ((unsigned char)c < 0x20)
                return false;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (p >= e)
                return false;
            char x = *p++;
            switch (x)
            {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                unsigned cp;
                if (!Hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00)
                {
                    unsigned lo = 0;
                    const char *save = p;
                    if (e - p >= 6 && p[0] == '\\' && p[1] == 'u')
                    {
                        p += 2;
                        if (Hex4(lo) && lo >= 0xDC00 && lo < 0xE000)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        else
                        {
                            p = save;
                            cp = 0xFFFD;
                        }
                    }
                    else
                    {
                        cp = 0xFFFD;
                    }
                }
                else if (cp >= 0xDC00 && cp < 0xE000)
                {
                    cp = 0xFFFD;
                }
                Utf8(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool Val(Value &v)
    {
        Ws();
        if (p >= e)
            return false;
        char c = *p;
        if (c == '{')
        {
            if (++depth > 64)
                return false;
            p++;
            v.t = Value::Obj;
            Ws();
            if (p < e && *p == '}')
            {
                p++;
                depth--;
                return true;
            }
            for (;;)
            {
                Ws();
                std::string k;
                if (!Str(k))
                    return false;
                Ws();
                if (p >= e || *p != ':')
                    return false;
                p++;
                Value x;
                if (!Val(x))
                    return false;
                v.o.push_back({std::move(k), std::move(x)});
                Ws();
                if (p < e && *p == ',')
                {
                    p++;
                    continue;
                }
                if (p < e && *p == '}')
                {
                    p++;
                    depth--;
                    return true;
                }
                return false;
            }
        }
        if (c == '[')
        {
            if (++depth > 64)
                return false;
            p++;
            v.t = Value::Arr;
            Ws();
            if (p < e && *p == ']')
            {
                p++;
                depth--;
                return true;
            }
            for (;;)
            {
                Value x;
                if (!Val(x))
                    return false;
                v.a.push_back(std::move(x));
                Ws();
                if (p < e && *p == ',')
                {
                    p++;
                    continue;
                }
                if (p < e && *p == ']')
                {
                    p++;
                    depth--;
                    return true;
                }
                return false;
            }
        }
        if (c == '"')
        {
            v.t = Value::Str;
            return Str(v.s);
        }
        if (e - p >= 4 && !memcmp(p, "true", 4))
        {
            p += 4;
            v = Value::Boolean(true);
            return true;
        }
        if (e - p >= 5 && !memcmp(p, "false", 5))
        {
            p += 5;
            v = Value::Boolean(false);
            return true;
        }
        if (e - p >= 4 && !memcmp(p, "null", 4))
        {
            p += 4;
            v = Value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            const char *s = p;
            if (*p == '-')
                p++;
            while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
                p++;
            std::string num(s, p);
            char *end = nullptr;
            double d = strtod(num.c_str(), &end);
            if (!end || *end)
                return false;
            v = Value::Number(d);
            return true;
        }
        return false;
    }
};

void Esc(const std::string &s, std::string &o)
{
    o += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        case '\b':
            o += "\\b";
            break;
        case '\f':
            o += "\\f";
            break;
        default:
            if (c < 0x20)
            {
                char b[8];
                snprintf(b, sizeof b, "\\u%04X", c);
                o += b;
            }
            else
            {
                o += (char)c;
            }
        }
    }
    o += '"';
}

void W(const Value &v, std::string &o, bool ind, int lvl)
{
    auto nl = [&](int l) {
        if (!ind)
            return;
        o += "\r\n";
        o.append((size_t)l * 2, ' ');
    };
    switch (v.t)
    {
    case Value::Null:
        o += "null";
        break;
    case Value::Bool:
        o += v.b ? "true" : "false";
        break;
    case Value::Num: {
        char b[64];
        if (std::floor(v.n) == v.n && std::fabs(v.n) < 1e15)
            snprintf(b, sizeof b, "%lld", (long long)v.n);
        else
            snprintf(b, sizeof b, "%.17g", v.n);
        o += b;
        break;
    }
    case Value::Str:
        Esc(v.s, o);
        break;
    case Value::Arr:
        if (v.a.empty())
        {
            o += "[]";
            break;
        }
        o += '[';
        for (size_t i = 0; i < v.a.size(); i++)
        {
            if (i)
                o += ',';
            nl(lvl + 1);
            W(v.a[i], o, ind, lvl + 1);
        }
        nl(lvl);
        o += ']';
        break;
    case Value::Obj:
        if (v.o.empty())
        {
            o += "{}";
            break;
        }
        o += '{';
        for (size_t i = 0; i < v.o.size(); i++)
        {
            if (i)
                o += ',';
            nl(lvl + 1);
            Esc(v.o[i].k, o);
            o += ind ? ": " : ":";
            W(v.o[i].v, o, ind, lvl + 1);
        }
        nl(lvl);
        o += '}';
        break;
    }
}

} // namespace

bool Parse(std::string_view text, Value &out)
{
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.remove_prefix(3);
    Parser ps{text.data(), text.data() + text.size()};
    Value v;
    if (!ps.Val(v))
        return false;
    ps.Ws();
    if (ps.p != ps.e)
        return false;
    out = std::move(v);
    return true;
}

std::string Write(const Value &v, bool indent)
{
    std::string o;
    W(v, o, indent, 0);
    return o;
}

} // namespace json
