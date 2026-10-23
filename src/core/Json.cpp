#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace derecho::core {
namespace {

class Parser {
public:
    Parser(const std::string& text, std::string& error) : m_text(text), m_error(error) {}

    bool ParseDocument(JsonValue& out) {
        SkipWhitespace();
        if (!ParseValue(out, 0)) {
            return false;
        }
        SkipWhitespace();
        if (m_pos != m_text.size()) {
            return Fail("unexpected trailing characters");
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool Fail(const std::string& message) {
        std::ostringstream stream;
        stream << "JSON error at byte " << m_pos << ": " << message;
        m_error = stream.str();
        return false;
    }

    void SkipWhitespace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    bool ParseValue(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            return Fail("nesting too deep");
        }
        SkipWhitespace();
        if (m_pos >= m_text.size()) {
            return Fail("unexpected end of input");
        }
        const char c = m_text[m_pos];
        switch (c) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"': {
                std::string text;
                if (!ParseString(text)) {
                    return false;
                }
                out = JsonValue(std::move(text));
                return true;
            }
            case 't':
                if (m_text.compare(m_pos, 4, "true") == 0) {
                    m_pos += 4;
                    out = JsonValue(true);
                    return true;
                }
                return Fail("invalid literal");
            case 'f':
                if (m_text.compare(m_pos, 5, "false") == 0) {
                    m_pos += 5;
                    out = JsonValue(false);
                    return true;
                }
                return Fail("invalid literal");
            case 'n':
                if (m_text.compare(m_pos, 4, "null") == 0) {
                    m_pos += 4;
                    out = JsonValue();
                    return true;
                }
                return Fail("invalid literal");
            default: return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue& out, int depth) {
        ++m_pos; // '{'
        out = JsonValue::MakeObject();
        SkipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        while (true) {
            SkipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
                return Fail("expected a string key");
            }
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
                return Fail("expected ':' after a key");
            }
            ++m_pos;
            JsonValue value;
            if (!ParseValue(value, depth + 1)) {
                return false;
            }
            out[key] = std::move(value);
            SkipWhitespace();
            if (m_pos >= m_text.size()) {
                return Fail("unterminated object");
            }
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            return Fail("expected ',' or '}'");
        }
    }

    bool ParseArray(JsonValue& out, int depth) {
        ++m_pos; // '['
        out = JsonValue::MakeArray();
        SkipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        while (true) {
            JsonValue value;
            if (!ParseValue(value, depth + 1)) {
                return false;
            }
            out.Push(std::move(value));
            SkipWhitespace();
            if (m_pos >= m_text.size()) {
                return Fail("unterminated array");
            }
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            return Fail("expected ',' or ']'");
        }
    }

    bool ParseString(std::string& out) {
        ++m_pos; // opening quote
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_pos >= m_text.size()) {
                return Fail("unterminated escape");
            }
            const char escape = m_text[m_pos++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    // Scene files only need ASCII; non-ASCII escapes are
                    // accepted and replaced with '?' rather than failing.
                    if (m_pos + 4 > m_text.size()) {
                        return Fail("truncated \\u escape");
                    }
                    m_pos += 4;
                    out.push_back('?');
                    break;
                }
                default: return Fail("unknown escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseNumber(JsonValue& out) {
        const size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) {
            ++m_pos;
        }
        bool digits = false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if ((c >= '0' && c <= '9')) {
                digits = true;
                ++m_pos;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                ++m_pos;
            } else {
                break;
            }
        }
        if (!digits) {
            return Fail("expected a value");
        }
        const std::string token = m_text.substr(start, m_pos - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0') {
            return Fail("invalid number");
        }
        out = JsonValue(value);
        return true;
    }

    const std::string& m_text;
    std::string& m_error;
    size_t m_pos = 0;
};

void EscapeInto(const std::string& text, std::string& out) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
}

void NumberInto(double value, std::string& out) {
    char buffer[64];
    if (value == static_cast<double>(static_cast<long long>(value)) && std::fabs(value) < 1e15) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    }
    out += buffer;
}

} // namespace

JsonValue& JsonValue::operator[](const std::string& key) {
    if (m_type != Type::Object) {
        m_type = Type::Object;
    }
    for (auto& member : m_object) {
        if (member.first == key) {
            return member.second;
        }
    }
    m_object.emplace_back(key, JsonValue());
    return m_object.back().second;
}

const JsonValue* JsonValue::Find(const std::string& key) const {
    if (m_type != Type::Object) {
        return nullptr;
    }
    for (const auto& member : m_object) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

double JsonValue::Get(const std::string& key, double fallback) const {
    const JsonValue* value = Find(key);
    return value ? value->NumberValue(fallback) : fallback;
}

int JsonValue::GetInt(const std::string& key, int fallback) const {
    const JsonValue* value = Find(key);
    return value ? value->IntValue(fallback) : fallback;
}

bool JsonValue::GetBool(const std::string& key, bool fallback) const {
    const JsonValue* value = Find(key);
    return value ? value->BoolValue(fallback) : fallback;
}

std::string JsonValue::GetString(const std::string& key, const std::string& fallback) const {
    const JsonValue* value = Find(key);
    return value && value->IsString() ? value->StringValue() : fallback;
}

bool JsonValue::Parse(const std::string& text, std::string& error) {
    Parser parser(text, error);
    return parser.ParseDocument(*this);
}

void JsonValue::SerializeInto(std::string& out, int indent, int depth) const {
    const std::string pad(indent > 0 ? static_cast<size_t>(indent) * (depth + 1) : 0, ' ');
    const std::string closingPad(indent > 0 ? static_cast<size_t>(indent) * depth : 0, ' ');
    const char* newline = indent > 0 ? "\n" : "";

    switch (m_type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Number: NumberInto(m_number, out); break;
        case Type::String: EscapeInto(m_string, out); break;
        case Type::Array: {
            if (m_array.empty()) {
                out += "[]";
                break;
            }
            out += "[";
            out += newline;
            for (size_t i = 0; i < m_array.size(); ++i) {
                out += pad;
                m_array[i].SerializeInto(out, indent, depth + 1);
                if (i + 1 < m_array.size()) {
                    out += ",";
                }
                out += newline;
            }
            out += closingPad;
            out += "]";
            break;
        }
        case Type::Object: {
            if (m_object.empty()) {
                out += "{}";
                break;
            }
            out += "{";
            out += newline;
            for (size_t i = 0; i < m_object.size(); ++i) {
                out += pad;
                EscapeInto(m_object[i].first, out);
                out += ": ";
                m_object[i].second.SerializeInto(out, indent, depth + 1);
                if (i + 1 < m_object.size()) {
                    out += ",";
                }
                out += newline;
            }
            out += closingPad;
            out += "}";
            break;
        }
    }
}

std::string JsonValue::Serialize(int indent) const {
    std::string out;
    SerializeInto(out, indent, 0);
    return out;
}

bool JsonValue::SaveToFile(const std::string& path, std::string& error) const {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "could not open '" + path + "' for writing";
        return false;
    }
    file << Serialize(2) << '\n';
    if (!file) {
        error = "failed while writing '" + path + "'";
        return false;
    }
    return true;
}

bool JsonValue::LoadFromFile(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open '" + path + "'";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return Parse(buffer.str(), error);
}

} // namespace derecho::core
