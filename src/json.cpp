#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace {

const Json kNull;

void AppendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class Parser {
public:
    Parser(std::string_view text, std::string* error) : text_(text), error_(error) {}

    bool parse(Json& out) {
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing data");
            return false;
        }
        return true;
    }

private:
    void fail(const std::string& message) {
        if (error_ && error_->empty()) {
            *error_ = message + " at byte " + std::to_string(pos_);
        }
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseValue(Json& out) {
        if (pos_ >= text_.size()) {
            fail("unexpected end of input");
            return false;
        }
        const char c = text_[pos_];
        switch (c) {
        case '{': return parseObject(out);
        case '[': return parseArray(out);
        case '"': {
            std::string value;
            if (!parseString(value)) {
                return false;
            }
            out = Json(std::move(value));
            return true;
        }
        case 't':
            if (text_.substr(pos_, 4) == "true") {
                pos_ += 4;
                out = Json(true);
                return true;
            }
            break;
        case 'f':
            if (text_.substr(pos_, 5) == "false") {
                pos_ += 5;
                out = Json(false);
                return true;
            }
            break;
        case 'n':
            if (text_.substr(pos_, 4) == "null") {
                pos_ += 4;
                out = Json(nullptr);
                return true;
            }
            break;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return parseNumber(out);
            }
            break;
        }
        fail("invalid value");
        return false;
    }

    bool parseObject(Json& out) {
        out = Json::MakeObject();
        ++pos_;  // {
        skipWhitespace();
        if (consume('}')) {
            return true;
        }
        for (;;) {
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                fail("expected object key");
                return false;
            }
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (!consume(':')) {
                fail("expected ':'");
                return false;
            }
            skipWhitespace();
            Json value;
            if (!parseValue(value)) {
                return false;
            }
            out[key] = std::move(value);
            skipWhitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                fail("expected ',' or '}'");
                return false;
            }
        }
    }

    bool parseArray(Json& out) {
        out = Json::MakeArray();
        ++pos_;  // [
        skipWhitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            skipWhitespace();
            Json value;
            if (!parseValue(value)) {
                return false;
            }
            out.push(std::move(value));
            skipWhitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                fail("expected ',' or ']'");
                return false;
            }
        }
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            fail("expected string");
            return false;
        }
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    fail("unterminated escape");
                    return false;
                }
                const char esc = text_[pos_++];
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned int code = 0;
                    if (!parseHex4(code)) {
                        return false;
                    }
                    if (code >= 0xD800 && code <= 0xDBFF && pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned int low = 0;
                        if (!parseHex4(low)) {
                            return false;
                        }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    fail("invalid escape");
                    return false;
                }
            } else if (c < 0x20) {
                fail("control character in string");
                return false;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        fail("unterminated string");
        return false;
    }

    bool parseHex4(unsigned int& value) {
        if (pos_ + 4 > text_.size()) {
            fail("truncated \\u escape");
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else {
                fail("invalid hex digit");
                return false;
            }
        }
        return true;
    }

    bool parseNumber(Json& out) {
        const size_t start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (pos_ == start) {
            fail("invalid number");
            return false;
        }
        const std::string number(text_.substr(start, pos_ - start));
        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || *end != '\0') {
            fail("invalid number");
            return false;
        }
        out = Json(value);
        return true;
    }

    std::string_view text_;
    std::string* error_ = nullptr;
    size_t pos_ = 0;
};

void EscapeString(const std::string& input, std::string& out) {
    out.push_back('"');
    for (const unsigned char c : input) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

}  // namespace

Json::Json() = default;

Json::Json(std::nullptr_t) : type_(Type::Null) {}

Json::Json(bool value) : type_(Type::Bool), bool_(value) {}

Json::Json(double value) : type_(Type::Number), number_(value) {}

Json::Json(int value) : type_(Type::Number), number_(static_cast<double>(value)) {}

Json::Json(long long value) : type_(Type::Number), number_(static_cast<double>(value)) {}

Json::Json(const char* value) : type_(Type::String), string_(value ? value : "") {}

Json::Json(std::string value) : type_(Type::String), string_(std::move(value)) {}

Json Json::MakeArray() {
    Json value;
    value.type_ = Type::Array;
    return value;
}

Json Json::MakeObject() {
    Json value;
    value.type_ = Type::Object;
    return value;
}

bool Json::asBool(bool defaultValue) const {
    return type_ == Type::Bool ? bool_ : defaultValue;
}

double Json::asNumber(double defaultValue) const {
    return type_ == Type::Number ? number_ : defaultValue;
}

long long Json::asInt(long long defaultValue) const {
    if (type_ != Type::Number) {
        return defaultValue;
    }
    if (number_ < static_cast<double>(std::numeric_limits<long long>::min()) ||
        number_ > static_cast<double>(std::numeric_limits<long long>::max())) {
        return defaultValue;
    }
    return static_cast<long long>(number_);
}

std::string Json::asString(const std::string& defaultValue) const {
    return type_ == Type::String ? string_ : defaultValue;
}

bool Json::has(std::string_view key) const {
    return type_ == Type::Object && object_.find(std::string(key)) != object_.end();
}

const Json& Json::operator[](std::string_view key) const {
    if (type_ != Type::Object) {
        return kNull;
    }
    const auto it = object_.find(std::string(key));
    return it == object_.end() ? kNull : it->second;
}

Json& Json::operator[](std::string_view key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    return object_[std::string(key)];
}

size_t Json::size() const {
    if (type_ == Type::Array) {
        return array_.size();
    }
    if (type_ == Type::Object) {
        return object_.size();
    }
    return 0;
}

const Json& Json::at(size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) {
        return kNull;
    }
    return array_[index];
}

Json& Json::at(size_t index) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    if (index >= array_.size()) {
        array_.resize(index + 1);
    }
    return array_[index];
}

void Json::push(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    const auto newline = [&](int d) {
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(indent * d), ' ');
        }
    };
    switch (type_) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Type::Number: {
        if (std::isfinite(number_) && std::floor(number_) == number_ && std::fabs(number_) < 1e15) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.0f", number_);
            out += buffer;
        } else {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.17g", number_);
            out += buffer;
        }
        break;
    }
    case Type::String:
        EscapeString(string_, out);
        break;
    case Type::Array: {
        out.push_back('[');
        for (size_t i = 0; i < array_.size(); ++i) {
            if (i) {
                out.push_back(',');
            }
            newline(depth + 1);
            array_[i].dumpTo(out, indent, depth + 1);
        }
        if (!array_.empty()) {
            newline(depth);
        }
        out.push_back(']');
        break;
    }
    case Type::Object: {
        out.push_back('{');
        bool first = true;
        for (const auto& [key, value] : object_) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            newline(depth + 1);
            EscapeString(key, out);
            out += indent > 0 ? ": " : ":";
            value.dumpTo(out, indent, depth + 1);
        }
        if (!object_.empty()) {
            newline(depth);
        }
        out.push_back('}');
        break;
    }
    }
}

bool Json::parse(std::string_view text, Json& out, std::string* error) {
    if (error) {
        error->clear();
    }
    Parser parser(text, error);
    return parser.parse(out);
}
