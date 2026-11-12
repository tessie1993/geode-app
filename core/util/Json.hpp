#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace geode::json {

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool boolValue() const { return kind_ == Kind::Bool && number_ != 0.0; }
    double number() const { return number_; }
    // A number written whole (an optional '-' and digits) that fits in 64 bits, which org.json reads as an
    // Integer or a Long rather than a Double; integer() is its exact value.
    bool integral() const { return integral_; }
    long long integer() const { return integer_; }
    const std::string& string() const { return string_; }
    const std::vector<Value>& array() const { return array_; }
    const std::map<std::string, Value>& object() const { return object_; }
    const Value* get(const std::string& key) const;
    std::string optString(const std::string& key, const std::string& fallback = "") const;

    static Value null() { return Value(); }
    static Value boolean(bool b) { Value v; v.kind_ = Kind::Bool; v.number_ = b ? 1.0 : 0.0; return v; }
    static Value number(double d) { Value v; v.kind_ = Kind::Number; v.number_ = d; return v; }
    static Value whole(double d, long long n) { Value v = number(d); v.integral_ = true; v.integer_ = n; return v; }
    static Value string(std::string s) { Value v; v.kind_ = Kind::String; v.string_ = std::move(s); return v; }
    static Value array(std::vector<Value> a) { Value v; v.kind_ = Kind::Array; v.array_ = std::move(a); return v; }
    static Value object(std::map<std::string, Value> o) { Value v; v.kind_ = Kind::Object; v.object_ = std::move(o); return v; }

private:
    Kind kind_ = Kind::Null;
    double number_ = 0.0;
    bool integral_ = false;
    long long integer_ = 0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

// Parses a whole JSON document; nullopt on any syntax error.
std::optional<Value> parse(const std::string& text);

// org.json's reading of a value as another type (Android's JSONObject opt*, through its JSON.to* coercions):
// nullopt wherever org.json falls back to the caller's default.
// asBoolean: a bool, or the string "true" or "false" in any case.
// asDouble: a number, or a string Java's Double.valueOf reads.
// asInt: a whole number as Integer.intValue or Long.intValue (its low 32 bits), any other number or a string
// Double.valueOf reads cast as Java's (int) does (truncated, saturated, NaN 0).
// asString: a string, or a bool, number or null as Java's String.valueOf spells it; not an object or array.
std::optional<bool> asBoolean(const Value& v);
std::optional<double> asDouble(const Value& v);
std::optional<int> asInt(const Value& v);
std::optional<std::string> asString(const Value& v);

// Java's Double.valueOf(String): trimmed, a sign, NaN, Infinity, decimal or hex digits, an f/F/d/D suffix.
std::optional<double> javaParseDouble(std::string_view text);
// Java's Double.toString: the shortest digits that read back to d, plain from 1e-3 up to 1e7, else d.dddE<n>.
std::string javaDoubleText(double d);
// org.json's JSONObject.numberToString for a Double: "-0" for negative zero, a whole value that fits a long
// as that long, anything else as Double.toString. d must be finite.
std::string numberText(double d);
// org.json's JSONStringer string(): quoted; a quote, backslash or slash gets a backslash, tab, backspace,
// newline, return and form feed their two-letter escapes, other controls a four-digit lowercase hex escape,
// and everything else (UTF-8 bytes included) is written as it is.
std::string quoted(std::string_view s);

}  // namespace geode::json
