#ifndef CHIME_WEBD_JSON_H
#define CHIME_WEBD_JSON_H

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chime::webd {

class JsonValue {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  JsonValue();

  static JsonValue Null();
  static JsonValue Bool(bool value);
  static JsonValue Number(double value);
  static JsonValue String(std::string value);
  static JsonValue Array(std::vector<JsonValue> value);
  static JsonValue Object(std::map<std::string, JsonValue> value);

  Type type() const;

  bool AsBool(bool* value) const;
  bool AsNumber(double* value) const;
  bool AsString(std::string* value) const;
  bool AsArray(std::vector<JsonValue>* value) const;
  bool AsObject(std::map<std::string, JsonValue>* value) const;

  const std::vector<JsonValue>& array_items() const;
  const std::map<std::string, JsonValue>& object_items() const;

 private:
  Type type_ = Type::kNull;
  bool bool_value_ = false;
  double number_value_ = 0.0;
  std::string string_value_;
  std::vector<JsonValue> array_value_;
  std::map<std::string, JsonValue> object_value_;
};

struct JsonParseResult {
  bool success = false;
  std::string error;
  JsonValue value;
};

JsonParseResult ParseJson(std::string_view input);
std::string JsonEscape(std::string_view input);
std::string JsonString(std::string_view input);
std::string JsonBool(bool value);
std::string JsonNumber(int value);

std::optional<JsonValue> GetObjectField(const JsonValue& value,
                                        const std::string& key);

}  // namespace chime::webd

#endif
