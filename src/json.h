#pragma once
#include <string>
#include <string_view>
#include <vector>

// Small JSON DOM: keeps object member order, strings are UTF-8.
namespace json {

struct Member;

struct Value {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<Member> o;

    static Value Boolean(bool v);
    static Value Number(double v);
    static Value String(std::string v);
    static Value Array();
    static Value Object();

    // Object lookup, ASCII case-insensitive.
    const Value* Get(std::string_view key) const;
    void Set(std::string key, Value v);
    void Push(Value v);
};

struct Member {
    std::string k;
    Value v;
};

bool Parse(std::string_view text, Value& out);
std::string Write(const Value& v, bool indent);

}  // namespace json
