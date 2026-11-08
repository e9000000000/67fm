#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

class Json {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(double value);
    Json(int value);
    Json(long long value);
    Json(const char* value);
    Json(std::string value);

    static Json MakeArray();
    static Json MakeObject();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool defaultValue = false) const;
    double asNumber(double defaultValue = 0.0) const;
    long long asInt(long long defaultValue = 0) const;
    std::string asString(const std::string& defaultValue = {}) const;

    bool has(std::string_view key) const;
    const Json& operator[](std::string_view key) const;
    Json& operator[](std::string_view key);

    size_t size() const;
    const Json& at(size_t index) const;
    Json& at(size_t index);
    void push(Json value);

    std::string dump(int indent = 2) const;

    static bool parse(std::string_view text, Json& out, std::string* error = nullptr);

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};
