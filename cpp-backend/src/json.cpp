/*
 * json.cpp - Recursive-descent JSON parser and serializer.
 */
#include "json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util.hpp"

namespace json {

namespace {

class Parser {
public:
    Parser(const std::string& s, std::string& err) : s_(s), err_(err) {}

    bool run(Value& out) {
        skipWs();
        size_t start = i_;
        if (!parseValue(out, 0)) return false;
        skipWs();
        if (i_ != s_.size()) {
            fail("trailing characters after JSON value");
            return false;
        }
        (void)start;
        return true;
    }

private:
    const std::string& s_;
    std::string& err_;
    size_t i_ = 0;
    static constexpr size_t kMaxDepth = 128;

    void fail(const char* msg) {
        if (err_.empty()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s at offset %zu", msg, i_);
            err_ = buf;
        }
    }

    bool eof() const { return i_ >= s_.size(); }
    char peek() const { return s_[i_]; }

    void skipWs() {
        while (!eof()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    bool expect(char c) {
        if (eof() || s_[i_] != c) {
            fail("unexpected character");
            return false;
        }
        ++i_;
        return true;
    }

    bool parseValue(Value& out, size_t depth) {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
            return false;
        }
        if (eof()) {
            fail("unexpected end of input");
            return false;
        }
        switch (peek()) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string str;
                if (!parseString(str)) return false;
                out = Value(str);
                return true;
            }
            case 't': return parseLiteral("true", Value(true), out);
            case 'f': return parseLiteral("false", Value(false), out);
            case 'n': return parseLiteral("null", Value(), out);
            default:  return parseNumber(out);
        }
    }

    bool parseLiteral(const char* lit, Value v, Value& out) {
        size_t len = std::strlen(lit);
        if (s_.compare(i_, len, lit) != 0) {
            fail("invalid literal");
            return false;
        }
        i_ += len;
        out = v;
        return true;
    }

    bool parseNumber(Value& out) {
        size_t start = i_;
        if (!eof() && (peek() == '-' || peek() == '+')) ++i_;
        bool digits = false;
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) { ++i_; digits = true; }
        if (!eof() && peek() == '.') {
            ++i_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) { ++i_; digits = true; }
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++i_;
            if (!eof() && (peek() == '-' || peek() == '+')) ++i_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        if (!digits) {
            fail("invalid number");
            return false;
        }
        out = Value(std::strtod(s_.c_str() + start, nullptr));
        return true;
    }

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool parseHex4(unsigned& cp) {
        if (i_ + 4 > s_.size()) {
            fail("bad \\u escape");
            return false;
        }
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            int v = hexVal(s_[i_ + static_cast<size_t>(k)]);
            if (v < 0) {
                fail("bad \\u escape");
                return false;
            }
            cp = (cp << 4) | static_cast<unsigned>(v);
        }
        i_ += 4;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseString(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (true) {
            if (eof()) {
                fail("unterminated string");
                return false;
            }
            char c = s_[i_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (eof()) {
                    fail("bad escape");
                    return false;
                }
                char e = s_[i_++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        unsigned cp;
                        if (!parseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF &&
                            i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                            i_ += 2;
                            unsigned low;
                            if (!parseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                appendUtf8(out, cp);
                                cp = low; /* unpaired fallback */
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default:
                        fail("bad escape");
                        return false;
                }
            } else {
                out += c;
            }
        }
    }

    bool parseObject(Value& out, size_t depth) {
        if (!expect('{')) return false;
        out = Value::object();
        skipWs();
        if (!eof() && peek() == '}') {
            ++i_;
            return true;
        }
        while (true) {
            skipWs();
            std::string key;
            if (eof() || peek() != '"') {
                fail("expected object key");
                return false;
            }
            if (!parseString(key)) return false;
            skipWs();
            if (!expect(':')) return false;
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.set(key, v);
            skipWs();
            if (eof()) {
                fail("unterminated object");
                return false;
            }
            if (peek() == ',') {
                ++i_;
                continue;
            }
            if (peek() == '}') {
                ++i_;
                return true;
            }
            fail("expected ',' or '}'");
            return false;
        }
    }

    bool parseArray(Value& out, size_t depth) {
        if (!expect('[')) return false;
        out = Value::array();
        skipWs();
        if (!eof() && peek() == ']') {
            ++i_;
            return true;
        }
        while (true) {
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.push(v);
            skipWs();
            if (eof()) {
                fail("unterminated array");
                return false;
            }
            if (peek() == ',') {
                ++i_;
                continue;
            }
            if (peek() == ']') {
                ++i_;
                return true;
            }
            fail("expected ',' or ']'");
            return false;
        }
    }
};

void writeNumber(std::string& out, double d) {
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        out += buf;
    } else {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", d);
        out += buf;
    }
}

void writeValue(const Value& v, std::string& out) {
    switch (v.type()) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += v.asBool() ? "true" : "false";
            break;
        case Type::Number:
            writeNumber(out, v.asDouble());
            break;
        case Type::String:
            out += '"';
            out += util::jsonEscape(v.asString());
            out += '"';
            break;
        case Type::Array: {
            out += '[';
            const auto& items = v.arrayItems();
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) out += ',';
                writeValue(items[i], out);
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : v.objectItems()) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out += util::jsonEscape(kv.first);
                out += "\":";
                writeValue(kv.second, out);
            }
            out += '}';
            break;
        }
    }
}

} // namespace

bool Value::parse(const std::string& text, Value& out, std::string& err) {
    err.clear();
    Parser p(text, err);
    return p.run(out);
}

void Value::write(std::string& out) const { writeValue(*this, out); }

std::string getString(const Value& v, const std::string& key) {
    const Value& f = v.at(key);
    return f.isString() ? f.asString() : "";
}

long long getInt(const Value& v, const std::string& key, long long def) {
    const Value& f = v.at(key);
    if (f.isNumber()) return f.asInt(def);
    if (f.isString()) {
        const std::string& s = f.asString();
        if (!s.empty()) {
            char* end = nullptr;
            long long r = std::strtoll(s.c_str(), &end, 10);
            if (end && *end == '\0') return r;
        }
    }
    return def;
}

} // namespace json
