#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace geode::json {

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool boolValue() const { return kind_ == Kind::Bool && number_ != 0.0; }
    double number() const { return number_; }
    const std::string& string() const { return string_; }
    const std::vector<Value>& array() const { return array_; }
    const std::map<std::string, Value>& object() const { return object_; }
    const Value* get(const std::string& key) const;
    std::string optString(const std::string& key, const std::string& fallback = "") const;

    static Value null() { return Value(); }
    static Value boolean(bool b) { Value v; v.kind_ = Kind::Bool; v.number_ = b ? 1.0 : 0.0; return v; }
    static Value number(double d) { Value v; v.kind_ = Kind::Number; v.number_ = d; return v; }
    static Value string(std::string s) { Value v; v.kind_ = Kind::String; v.string_ = std::move(s); return v; }
    static Value array(std::vector<Value> a) { Value v; v.kind_ = Kind::Array; v.array_ = std::move(a); return v; }
    static Value object(std::map<std::string, Value> o) { Value v; v.kind_ = Kind::Object; v.object_ = std::move(o); return v; }

private:
    Kind kind_ = Kind::Null;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

// Parses a whole JSON document; nullopt on any syntax error.
std::optional<Value> parse(const std::string& text);

}  // namespace geode::json
