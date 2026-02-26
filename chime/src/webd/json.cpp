#include "chime/webd_json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace chime::webd {

JsonValue::JsonValue() = default;

JsonValue JsonValue::Null() { return JsonValue(); }

JsonValue JsonValue::Bool(bool value) {
  JsonValue result;
  result.type_ = Type::kBool;
  result.bool_value_ = value;
  return result;
}

JsonValue JsonValue::Number(double value) {
  JsonValue result;
  result.type_ = Type::kNumber;
  result.number_value_ = value;
  return result;
}

JsonValue JsonValue::String(std::string value) {
  JsonValue result;
  result.type_ = Type::kString;
  result.string_value_ = std::move(value);
  return result;
}

JsonValue JsonValue::Array(std::vector<JsonValue> value) {
  JsonValue result;
  result.type_ = Type::kArray;
  result.array_value_ = std::move(value);
  return result;
}

JsonValue JsonValue::Object(std::map<std::string, JsonValue> value) {
  JsonValue result;
  result.type_ = Type::kObject;
  result.object_value_ = std::move(value);
  return result;
}

JsonValue::Type JsonValue::type() const { return type_; }

bool JsonValue::AsBool(bool* value) const {
  if (type_ != Type::kBool || value == nullptr) {
    return false;
  }
  *value = bool_value_;
  return true;
}

bool JsonValue::AsNumber(double* value) const {
  if (type_ != Type::kNumber || value == nullptr) {
    return false;
  }
  *value = number_value_;
  return true;
}

bool JsonValue::AsString(std::string* value) const {
  if (type_ != Type::kString || value == nullptr) {
    return false;
  }
  *value = string_value_;
  return true;
}

bool JsonValue::AsArray(std::vector<JsonValue>* value) const {
  if (type_ != Type::kArray || value == nullptr) {
    return false;
  }
  *value = array_value_;
  return true;
}

bool JsonValue::AsObject(std::map<std::string, JsonValue>* value) const {
  if (type_ != Type::kObject || value == nullptr) {
    return false;
  }
  *value = object_value_;
  return true;
}

const std::vector<JsonValue>& JsonValue::array_items() const { return array_value_; }

const std::map<std::string, JsonValue>& JsonValue::object_items() const {
  return object_value_;
}

namespace {

class Parser {
 public:
  explicit Parser(std::string_view input) : input_(input) {}

  JsonParseResult Parse() {
    JsonParseResult result;
    SkipWhitespace();
    if (!ParseValue(&result.value, &result.error)) {
      result.success = false;
      if (result.error.empty()) {
        result.error = "invalid json";
      }
      return result;
    }

    SkipWhitespace();
    if (!AtEnd()) {
      result.success = false;
      result.error = "unexpected trailing characters";
      return result;
    }

    result.success = true;
    return result;
  }

 private:
  bool ParseValue(JsonValue* value, std::string* error) {
    if (value == nullptr || error == nullptr) {
      return false;
    }

    if (AtEnd()) {
      *error = "unexpected end of json";
      return false;
    }

    const char c = Peek();
    if (c == '{') {
      return ParseObject(value, error);
    }
    if (c == '[') {
      return ParseArray(value, error);
    }
    if (c == '"') {
      std::string parsed;
      if (!ParseString(&parsed, error)) {
        return false;
      }
      *value = JsonValue::String(std::move(parsed));
      return true;
    }
    if (c == 't') {
      return ParseLiteral("true", JsonValue::Bool(true), value, error);
    }
    if (c == 'f') {
      return ParseLiteral("false", JsonValue::Bool(false), value, error);
    }
    if (c == 'n') {
      return ParseLiteral("null", JsonValue::Null(), value, error);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      double parsed = 0.0;
      if (!ParseNumber(&parsed, error)) {
        return false;
      }
      *value = JsonValue::Number(parsed);
      return true;
    }

    *error = "unexpected token";
    return false;
  }

  bool ParseObject(JsonValue* value, std::string* error) {
    Consume();
    SkipWhitespace();

    std::map<std::string, JsonValue> object;
    if (!AtEnd() && Peek() == '}') {
      Consume();
      *value = JsonValue::Object(std::move(object));
      return true;
    }

    while (!AtEnd()) {
      std::string key;
      if (!ParseString(&key, error)) {
        return false;
      }

      SkipWhitespace();
      if (AtEnd() || Peek() != ':') {
        *error = "expected ':' in object";
        return false;
      }
      Consume();
      SkipWhitespace();

      JsonValue parsed;
      if (!ParseValue(&parsed, error)) {
        return false;
      }
      object[key] = std::move(parsed);

      SkipWhitespace();
      if (AtEnd()) {
        *error = "unterminated object";
        return false;
      }
      if (Peek() == '}') {
        Consume();
        *value = JsonValue::Object(std::move(object));
        return true;
      }
      if (Peek() != ',') {
        *error = "expected ',' in object";
        return false;
      }
      Consume();
      SkipWhitespace();
    }

    *error = "unterminated object";
    return false;
  }

  bool ParseArray(JsonValue* value, std::string* error) {
    Consume();
    SkipWhitespace();

    std::vector<JsonValue> array;
    if (!AtEnd() && Peek() == ']') {
      Consume();
      *value = JsonValue::Array(std::move(array));
      return true;
    }

    while (!AtEnd()) {
      JsonValue parsed;
      if (!ParseValue(&parsed, error)) {
        return false;
      }
      array.push_back(std::move(parsed));

      SkipWhitespace();
      if (AtEnd()) {
        *error = "unterminated array";
        return false;
      }
      if (Peek() == ']') {
        Consume();
        *value = JsonValue::Array(std::move(array));
        return true;
      }
      if (Peek() != ',') {
        *error = "expected ',' in array";
        return false;
      }
      Consume();
      SkipWhitespace();
    }

    *error = "unterminated array";
    return false;
  }

  bool ParseString(std::string* value, std::string* error) {
    if (value == nullptr || error == nullptr) {
      return false;
    }
    if (AtEnd() || Peek() != '"') {
      *error = "expected string";
      return false;
    }
    Consume();

    std::string out;
    while (!AtEnd()) {
      const char c = Consume();
      if (c == '"') {
        *value = std::move(out);
        return true;
      }
      if (c == '\\') {
        if (AtEnd()) {
          *error = "invalid escape";
          return false;
        }
        const char esc = Consume();
        switch (esc) {
          case '"':
          case '\\':
          case '/':
            out.push_back(esc);
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u': {
            if (position_ + 4 > input_.size()) {
              *error = "invalid unicode escape";
              return false;
            }
            const std::string hex(input_.substr(position_, 4));
            char* end = nullptr;
            const long code = std::strtol(hex.c_str(), &end, 16);
            if (end == nullptr || *end != '\0' || code < 0) {
              *error = "invalid unicode escape";
              return false;
            }
            position_ += 4;
            if (code < 0x80) {
              out.push_back(static_cast<char>(code));
            } else {
              out.push_back('?');
            }
            break;
          }
          default:
            *error = "unsupported escape sequence";
            return false;
        }
        continue;
      }

      if (static_cast<unsigned char>(c) < 0x20) {
        *error = "control character in string";
        return false;
      }
      out.push_back(c);
    }

    *error = "unterminated string";
    return false;
  }

  bool ParseNumber(double* value, std::string* error) {
    if (value == nullptr || error == nullptr) {
      return false;
    }

    const std::size_t start = position_;

    if (!AtEnd() && Peek() == '-') {
      Consume();
    }

    if (AtEnd()) {
      *error = "invalid number";
      return false;
    }

    if (Peek() == '0') {
      Consume();
    } else {
      if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
        *error = "invalid number";
        return false;
      }
      while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
        Consume();
      }
    }

    if (!AtEnd() && Peek() == '.') {
      Consume();
      if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
        *error = "invalid number";
        return false;
      }
      while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
        Consume();
      }
    }

    if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
      Consume();
      if (!AtEnd() && (Peek() == '+' || Peek() == '-')) {
        Consume();
      }
      if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
        *error = "invalid number";
        return false;
      }
      while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
        Consume();
      }
    }

    const std::string num(input_.substr(start, position_ - start));
    char* end = nullptr;
    const double parsed = std::strtod(num.c_str(), &end);
    if (end == nullptr || *end != '\0' || std::isnan(parsed) ||
        std::isinf(parsed)) {
      *error = "invalid number";
      return false;
    }

    *value = parsed;
    return true;
  }

  bool ParseLiteral(std::string_view literal, const JsonValue& value,
                    JsonValue* out, std::string* error) {
    if (position_ + literal.size() > input_.size()) {
      *error = "unexpected end of json";
      return false;
    }
    if (input_.substr(position_, literal.size()) != literal) {
      *error = "invalid literal";
      return false;
    }
    position_ += literal.size();
    *out = value;
    return true;
  }

  void SkipWhitespace() {
    while (!AtEnd()) {
      const char c = Peek();
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        Consume();
        continue;
      }
      break;
    }
  }

  bool AtEnd() const { return position_ >= input_.size(); }

  char Peek() const { return input_[position_]; }

  char Consume() { return input_[position_++]; }

  std::string_view input_;
  std::size_t position_ = 0;
};

}  // namespace

JsonParseResult ParseJson(std::string_view input) {
  Parser parser(input);
  return parser.Parse();
}

std::string JsonEscape(std::string_view input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (const char c : input) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += '?';
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

std::string JsonString(std::string_view input) {
  return std::string("\"") + JsonEscape(input) + "\"";
}

std::string JsonBool(bool value) { return value ? "true" : "false"; }

std::string JsonNumber(int value) { return std::to_string(value); }

std::optional<JsonValue> GetObjectField(const JsonValue& value,
                                        const std::string& key) {
  if (value.type() != JsonValue::Type::kObject) {
    return std::nullopt;
  }
  const auto& object = value.object_items();
  const auto it = object.find(key);
  if (it == object.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace chime::webd
