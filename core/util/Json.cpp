#include "util/Json.hpp"

#include <cctype>
#include <cstdlib>

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

}  // namespace geode::json
