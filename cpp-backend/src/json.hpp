/*
 * json.hpp - Minimal JSON value, parser and serializer (STL based).
 * Supports objects (std::map), arrays (std::vector), string, double/integer,
 * boolean and null. Used for the booking API request/response bodies.
 */
#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null) {}
    Value(std::nullptr_t) : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(size_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(double v) : type_(Type::Number), num_(v) {}
    Value(const char* s) : type_(Type::String), str_(s ? s : "") {}
    Value(const std::string& s) : type_(Type::String), str_(s) {}

    /* ---- Factory helpers ---- */
    static Value object() { Value v; v.type_ = Type::Object; return v; }
    static Value array()  { Value v; v.type_ = Type::Array;  return v; }

    /* ---- Type inspection ---- */
    Type type() const { return type_; }
    bool isNull()   const { return type_ == Type::Null; }
    bool isBool()   const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray()  const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    /* ---- Accessors ---- */
    bool asBool(bool def = false) const {
        return isBool() ? bool_ : def;
    }
    double asDouble(double def = 0) const {
        return isNumber() ? num_ : def;
    }
    long long asInt(long long def = 0) const {
        return isNumber() ? static_cast<long long>(num_) : def;
    }
    const std::string& asString() const { return str_; }

    /* ---- Object interface ---- */
    void set(const std::string& key, const Value& v) {
        ensureObject();
        members_[key] = v;
    }
    bool has(const std::string& key) const {
        return type_ == Type::Object && members_.count(key) > 0;
    }
    /* Missing key yields a Null Value; safe to chain .asString() etc. */
    const Value& at(const std::string& key) const {
        static const Value nullValue;
        if (type_ != Type::Object) return nullValue;
        auto it = members_.find(key);
        return it == members_.end() ? nullValue : it->second;
    }
    const std::map<std::string, Value>& objectItems() const { return members_; }

    /* ---- Array interface ---- */
    void push(const Value& v) {
        ensureArray();
        items_.push_back(v);
    }
    size_t size() const { return items_.size(); }
    const Value& at(size_t i) const { return items_.at(i); }
    const std::vector<Value>& arrayItems() const { return items_; }

    /* ---- Serialization ---- */
    std::string dump() const {
        std::string out;
        write(out);
        return out;
    }

    /* Returns false + error message on malformed input. */
    static bool parse(const std::string& text, Value& out, std::string& err);

private:
    void ensureObject() {
        if (type_ != Type::Object) {
            type_ = Type::Object;
            items_.clear();
        }
    }
    void ensureArray() {
        if (type_ != Type::Array) {
            type_ = Type::Array;
            members_.clear();
        }
    }
    void write(std::string& out) const;

    Type type_;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Value> items_;
    std::map<std::string, Value> members_;
};

/* Conveniences for building/reading objects. */
std::string getString(const Value& v, const std::string& key);
long long getInt(const Value& v, const std::string& key, long long def = 0);

} // namespace json
