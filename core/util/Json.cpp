#include "util/Json.hpp"

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace geode::json {

const Value* Value::get(const std::string& key) const {
    if (kind_ != Kind::Object) return nullptr;
    const auto it = object_.find(key);
    return it == object_.end() ? nullptr : &it->second;
}

std::string Value::optString(const std::string& key, const std::string& fallback) const {
    const Value* v = get(key);
    return (v && v->kind() == Kind::String) ? v->string() : fallback;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    std::optional<Value> document() {
        skipSpace();
        auto v = value();
        skipSpace();
        if (!v || pos_ != text_.size()) return std::nullopt;
        return v;
    }

private:
    void skipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) pos_++;
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            pos_++;
            return true;
        }
        return false;
    }

    bool literal(const char* word) {
        const size_t n = std::char_traits<char>::length(word);
        if (text_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    std::optional<Value> value() {
        if (pos_ >= text_.size()) return std::nullopt;
        const char c = text_[pos_];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') {
            auto s = string();
            if (!s) return std::nullopt;
            return Value::string(std::move(*s));
        }
        if (literal("true")) return Value::boolean(true);
        if (literal("false")) return Value::boolean(false);
        if (literal("null")) return Value::null();
        return number();
    }

    std::optional<Value> number() {
        const char* start = text_.c_str() + pos_;
        char* end = nullptr;
        const double d = std::strtod(start, &end);
        if (end == start) return std::nullopt;
        pos_ += static_cast<size_t>(end - start);
        const char* digits = *start == '-' ? start + 1 : start;
        bool whole = digits < end;
        for (const char* c = digits; c < end && whole; ++c) whole = *c >= '0' && *c <= '9';
        if (whole) {
            errno = 0;
            char* wholeEnd = nullptr;
            const long long n = std::strtoll(start, &wholeEnd, 10);
            if (errno != ERANGE && wholeEnd == end) return Value::whole(d, n);
        }
        return Value::number(d);
    }

    bool hex4(unsigned int& out) {
        if (pos_ + 4 > text_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; i++) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned int>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int cp) {
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

    std::optional<std::string> string() {
        if (!consume('"')) return std::nullopt;
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) return std::nullopt;
            const char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!hex4(cp)) return std::nullopt;
                    if (cp >= 0xD800 && cp <= 0xDBFF && text_.compare(pos_, 2, "\\u") == 0) {
                        pos_ += 2;
                        unsigned int low = 0;
                        if (!hex4(low)) return std::nullopt;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Value> array() {
        if (!consume('[')) return std::nullopt;
        std::vector<Value> items;
        skipSpace();
        if (consume(']')) return Value::array(std::move(items));
        while (true) {
            skipSpace();
            auto v = value();
            if (!v) return std::nullopt;
            items.push_back(std::move(*v));
            skipSpace();
            if (consume(']')) return Value::array(std::move(items));
            if (!consume(',')) return std::nullopt;
        }
    }

    std::optional<Value> object() {
        if (!consume('{')) return std::nullopt;
        std::map<std::string, Value> members;
        skipSpace();
        if (consume('}')) return Value::object(std::move(members));
        while (true) {
            skipSpace();
            auto key = string();
            if (!key) return std::nullopt;
            skipSpace();
            if (!consume(':')) return std::nullopt;
            skipSpace();
            auto v = value();
            if (!v) return std::nullopt;
            members[*key] = std::move(*v);
            skipSpace();
            if (consume('}')) return Value::object(std::move(members));
            if (!consume(',')) return std::nullopt;
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
};

}  // namespace

std::optional<Value> parse(const std::string& text) {
    return Parser(text).document();
}

namespace {

// Java's (int) of a double: truncated toward zero, saturated, NaN as 0.
int javaInt(double d) {
    if (std::isnan(d)) return 0;
    if (d >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (d <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    return static_cast<int>(d);
}

// Java's (long) of a double, the same way.
long long javaLong(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9223372036854775807.0) return std::numeric_limits<long long>::max();
    if (d <= -9223372036854775808.0) return std::numeric_limits<long long>::min();
    return static_cast<long long>(d);
}

bool equalsIgnoringCase(const std::string& s, std::string_view word) {
    if (s.size() != word.size()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != word[i]) return false;
    }
    return true;
}

}  // namespace

std::optional<bool> asBoolean(const Value& v) {
    if (v.kind() == Value::Kind::Bool) return v.boolValue();
    if (v.kind() != Value::Kind::String) return std::nullopt;
    if (equalsIgnoringCase(v.string(), "true")) return true;
    if (equalsIgnoringCase(v.string(), "false")) return false;
    return std::nullopt;
}

std::optional<double> asDouble(const Value& v) {
    // A whole number is a long to org.json, so "-0" reads as 0, not as negative zero.
    if (v.kind() == Value::Kind::Number) return v.integral() ? static_cast<double>(v.integer()) : v.number();
    if (v.kind() == Value::Kind::String) return javaParseDouble(v.string());
    return std::nullopt;
}

std::optional<int> asInt(const Value& v) {
    if (v.kind() == Value::Kind::Number) {
        if (!v.integral()) return javaInt(v.number());
        return static_cast<int>(static_cast<unsigned int>(static_cast<unsigned long long>(v.integer())));
    }
    if (v.kind() == Value::Kind::String) {
        const auto d = javaParseDouble(v.string());
        if (d) return javaInt(*d);
    }
    return std::nullopt;
}

std::optional<std::string> asString(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::String: return v.string();
        case Value::Kind::Bool: return std::string(v.boolValue() ? "true" : "false");
        case Value::Kind::Null: return std::string("null");
        case Value::Kind::Number: return v.integral() ? std::to_string(v.integer()) : javaDoubleText(v.number());
        case Value::Kind::Array:
        case Value::Kind::Object: break;
    }
    return std::nullopt;
}

std::optional<double> javaParseDouble(std::string_view text) {
    while (!text.empty() && static_cast<unsigned char>(text.front()) <= ' ') text.remove_prefix(1);
    while (!text.empty() && static_cast<unsigned char>(text.back()) <= ' ') text.remove_suffix(1);
    std::string_view body = text;
    bool negative = false;
    if (!body.empty() && (body.front() == '+' || body.front() == '-')) {
        negative = body.front() == '-';
        body.remove_prefix(1);
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    if (body == "NaN") return std::numeric_limits<double>::quiet_NaN();
    if (body == "Infinity") return negative ? -kInf : kInf;
    if (!body.empty() && (body.back() == 'f' || body.back() == 'F' || body.back() == 'd' || body.back() == 'D')) {
        body.remove_suffix(1);
    }
    const bool hex = body.size() > 1 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X');
    const auto digit = [hex](char c) {
        const auto u = static_cast<unsigned char>(c);
        return hex ? std::isxdigit(u) != 0 : std::isdigit(u) != 0;
    };
    size_t i = hex ? 2 : 0;
    size_t mantissa = 0;
    for (; i < body.size() && digit(body[i]); ++i) ++mantissa;
    if (i < body.size() && body[i] == '.') {
        for (++i; i < body.size() && digit(body[i]); ++i) ++mantissa;
    }
    if (mantissa == 0) return std::nullopt;
    if (i < body.size() && std::tolower(static_cast<unsigned char>(body[i])) == (hex ? 'p' : 'e')) {
        ++i;
        if (i < body.size() && (body[i] == '+' || body[i] == '-')) ++i;
        size_t exponent = 0;
        for (; i < body.size() && std::isdigit(static_cast<unsigned char>(body[i])); ++i) ++exponent;
        if (exponent == 0) return std::nullopt;
    } else if (hex) {
        return std::nullopt;  // a hex float needs its binary exponent
    }
    if (i != body.size()) return std::nullopt;
    const std::string number = (negative ? "-" : "") + std::string(body);
    return std::strtod(number.c_str(), nullptr);
}

std::string javaDoubleText(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    if (d == 0.0) return std::signbit(d) ? "-0.0" : "0.0";
    // The shortest digits that read back to d, then laid out as Double.toString lays them out.
    char buf[64];
    const auto written = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);
    std::string_view sci(buf, static_cast<size_t>(written.ptr - buf));
    std::string out;
    if (sci.front() == '-') {
        out += '-';
        sci.remove_prefix(1);
    }
    const size_t e = sci.find('e');
    std::string digits;
    for (const char c : sci.substr(0, e)) {
        if (c != '.') digits += c;
    }
    const int exponent = std::atoi(std::string(sci.substr(e + 1)).c_str());
    if (exponent >= -3 && exponent < 7) {
        if (exponent < 0) {
            out += "0.";
            out.append(static_cast<size_t>(-exponent - 1), '0');
            out += digits;
            return out;
        }
        const size_t whole = static_cast<size_t>(exponent) + 1;
        if (digits.size() <= whole) {
            out += digits;
            out.append(whole - digits.size(), '0');
            out += ".0";
        } else {
            out += digits.substr(0, whole);
            out += '.';
            out += digits.substr(whole);
        }
        return out;
    }
    out += digits[0];
    out += '.';
    out += digits.size() > 1 ? digits.substr(1) : std::string("0");
    out += 'E';
    out += std::to_string(exponent);
    return out;
}

std::string numberText(double d) {
    if (d == 0.0 && std::signbit(d)) return "-0";
    const long long whole = javaLong(d);
    if (static_cast<double>(whole) == d) return std::to_string(whole);
    return javaDoubleText(d);
}

std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '/': out += "\\/"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1F) {
                    char escape[8];
                    std::snprintf(escape, sizeof escape, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += escape;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

}  // namespace geode::json
