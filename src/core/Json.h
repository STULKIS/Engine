// Derecho Engine - a small, dependency-free JSON value used for scene files.
//
// Scene files are plain JSON so they can be read, diffed and edited by hand and
// by other tools. This is intentionally a minimal implementation: objects keep
// insertion order (so saving is stable), numbers are doubles, and there are no
// comments or trailing commas (standard JSON).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace derecho::core {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;
    explicit JsonValue(bool value) : m_type(Type::Bool), m_bool(value) {}
    explicit JsonValue(double value) : m_type(Type::Number), m_number(value) {}
    explicit JsonValue(int value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    explicit JsonValue(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
    explicit JsonValue(const char* value) : m_type(Type::String), m_string(value ? value : "") {}

    static JsonValue MakeArray() { JsonValue v; v.m_type = Type::Array; return v; }
    static JsonValue MakeObject() { JsonValue v; v.m_type = Type::Object; return v; }

    Type GetType() const { return m_type; }
    bool IsNull() const { return m_type == Type::Null; }
    bool IsBool() const { return m_type == Type::Bool; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    bool BoolValue(bool fallback = false) const { return m_type == Type::Bool ? m_bool : fallback; }
    double NumberValue(double fallback = 0.0) const { return m_type == Type::Number ? m_number : fallback; }
    int IntValue(int fallback = 0) const {
        return m_type == Type::Number ? static_cast<int>(m_number) : fallback;
    }
    const std::string& StringValue() const { return m_string; }

    const std::vector<JsonValue>& Items() const { return m_array; }
    void Push(JsonValue value) { m_array.push_back(std::move(value)); }

    // Object access. operator[] creates the member when it is missing.
    JsonValue& operator[](const std::string& key);
    const JsonValue* Find(const std::string& key) const;
    const std::vector<std::pair<std::string, JsonValue>>& Members() const { return m_object; }

    // Convenience readers with defaults, for scene parsing.
    double Get(const std::string& key, double fallback) const;
    int GetInt(const std::string& key, int fallback) const;
    bool GetBool(const std::string& key, bool fallback) const;
    std::string GetString(const std::string& key, const std::string& fallback) const;

    // Parses `text`. On failure returns false and fills `error` with a message
    // that includes the byte offset.
    bool Parse(const std::string& text, std::string& error);

    // Serialises. `indent` of 0 produces a single line.
    std::string Serialize(int indent = 2) const;

    bool SaveToFile(const std::string& path, std::string& error) const;
    bool LoadFromFile(const std::string& path, std::string& error);

private:
    void SerializeInto(std::string& out, int indent, int depth) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<JsonValue> m_array;
    std::vector<std::pair<std::string, JsonValue>> m_object;
};

} // namespace derecho::core
