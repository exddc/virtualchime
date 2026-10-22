#!/usr/bin/env python3
"""Generate C++ and TypeScript representations from schema/chime_config.json."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[1]
SCHEMA_PATH = REPO / "schema" / "chime_config.json"
CPP_HEADER = REPO / "chime" / "include" / "chime" / "generated" / "config_types.h"
CPP_JSON = REPO / "chime" / "include" / "chime" / "generated" / "config_json.h"
TS_PATH = REPO / "webui" / "src" / "generated" / "config_schema.ts"
DOC_PATH = REPO / "docs" / "config-schema.md"
CONTRACT_TEST = REPO / "chime" / "tests" / "config_schema_contract_test.cpp"


def load_schema() -> dict[str, Any]:
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


def default_text(field: dict[str, Any]) -> str:
    value = field["default"]
    kind = field["type"]
    if kind == "csv":
        if not isinstance(value, list):
            raise TypeError(f"{field['key']} csv default must be a list")
        return ",".join(value)
    if kind == "bool":
        return "true" if value else "false"
    if kind == "int":
        return str(int(value))
    if value is None:
        return ""
    return str(value)


def cpp_default(field: dict[str, Any]) -> str:
    kind = field["type"]
    value = field["default"]
    name = field["key"]
    if kind == "string":
        if value == "":
            return f"std::string {name}{{}}"
        escaped = str(value).replace("\\", "\\\\").replace('"', '\\"')
        return f'std::string {name} = "{escaped}"'
    if kind == "int":
        return f"int {name} = {int(value)}"
    if kind == "bool":
        return f"bool {name} = {'true' if value else 'false'}"
    if kind == "csv":
        items = field["default"]
        if not items:
            return f"std::vector<std::string> {name}{{}}"
        inner = ", ".join(f'"{item}"' for item in items)
        return f"std::vector<std::string> {name}{{{inner}}}"
    raise ValueError(f"unsupported type {kind}")


def ts_default(field: dict[str, Any]) -> str:
    kind = field["type"]
    value = field["default"]
    if kind == "string":
        escaped = str(value).replace("\\", "\\\\").replace('"', '\\"')
        return f'"{escaped}"'
    if kind == "int":
        return str(int(value))
    if kind == "bool":
        return "true" if value else "false"
    if kind == "csv":
        items = ", ".join(json.dumps(item) for item in value)
        return f"[{items}]"
    raise ValueError(f"unsupported type {kind}")


def ts_type(field: dict[str, Any]) -> str:
    kind = field["type"]
    if kind == "string":
        return "string"
    if kind == "int":
        return "number"
    if kind == "bool":
        return "boolean"
    if kind == "csv":
        return "string[]"
    raise ValueError(f"unsupported type {kind}")


def bool_lit(value: bool) -> str:
    return "true" if value else "false"


def repair_text(field: dict[str, Any]) -> str:
    if field["type"] == "csv" and field.get("file_required") and field.get("shipped"):
        shipped = field["shipped"]
        if isinstance(shipped, list) and shipped:
            return ",".join(str(item) for item in shipped)
    return default_text(field)


def field_forbid_newline(field: dict[str, Any]) -> bool:
    if "forbid_newline" in field:
        return bool(field["forbid_newline"])
    return field["type"] in ("string", "csv")


def spec_row(field: dict[str, Any]) -> str:
    persist = field.get("persist") or "none"
    persist_enum = {
        "file": "ConfigPersist::kFile",
        "wpa": "ConfigPersist::kWpa",
        "none": "ConfigPersist::kNone",
    }[persist]
    min_value = int(field["min"]) if "min" in field else 0
    max_value = int(field["max"]) if "max" in field else 0
    min_len = int(field.get("min_len") or 0)
    max_len = int(field.get("max_len") or 0)
    type_enum = {
        "string": "ConfigValueType::kString",
        "int": "ConfigValueType::kInt",
        "bool": "ConfigValueType::kBool",
        "csv": "ConfigValueType::kCsv",
    }[field["type"]]
    default = default_text(field).replace("\\", "\\\\").replace('"', '\\"')
    repair = repair_text(field).replace("\\", "\\\\").replace('"', '\\"')
    return (
        "    {"
        f'"{field["key"]}", {type_enum}, "{default}", "{repair}", {persist_enum}, '
        f'{bool_lit(bool(field["runtime"]))}, {bool_lit(bool(field["api"]))}, '
        f'{bool_lit(bool(field["ui"]))}, {bool_lit(bool(field["init"]))}, '
        f'{bool_lit(bool(field["secret"]))}, {bool_lit(bool(field.get("file_required")))}, '
        f'{bool_lit(bool(field.get("api_required")))}, {bool_lit(bool(field.get("api_empty_ok")))}, '
        f'{bool_lit(bool(field.get("forbid_whitespace")))}, {bool_lit(field_forbid_newline(field))}, '
        f"{min_value}, {max_value}, {min_len}, {max_len}"
        "}"
    )


def parse_call(struct: str, field: dict[str, Any]) -> str:
    name = field["key"]
    kind = field["type"]
    min_len = int(field.get("min_len") or 0)
    max_len = int(field.get("max_len") or 0)
    forbid_ws = bool_lit(bool(field.get("forbid_whitespace")))
    forbid_nl = bool_lit(field_forbid_newline(field))
    if kind == "string":
        return (
            f"oc::config::parse_string<{struct}, &{struct}::{name}, {min_len}, {max_len}, "
            f"{forbid_ws}, {forbid_nl}>"
        )
    if kind == "bool":
        return f"oc::config::parse_bool<{struct}, &{struct}::{name}>"
    if kind == "csv":
        return f"oc::config::parse_csv<{struct}, &{struct}::{name}, {forbid_ws}, {forbid_nl}>"
    if kind == "int":
        min_value = int(field["min"])
        max_value = int(field["max"])
        return f"oc::config::parse_int<{struct}, &{struct}::{name}, {min_value}, {max_value}>"
    raise ValueError(f"unsupported type {kind}")


def field_table(struct: str, fields: list[dict[str, Any]], required_key: str) -> str:
    lines = [f"inline const oc::config::Field<{struct}> k{struct}Fields[] = {{"]
    for field in fields:
        required = bool(field.get(required_key))
        lines.append(f'    {{"{field["key"]}", {parse_call(struct, field)}, {bool_lit(required)}}},')
    lines.append("};")
    return "\n".join(lines)


def cpp_string_literal(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def invalid_value_examples() -> list[tuple[str, str]]:
    return [
        ("mqtt_host", "bad host"),
        ("mqtt_client_id", "x" * 129),
        ("ring_topic", "ring/+/pressed/#\ninjected"),
        ("mqtt_topics", "bad topic"),
        ("heartbeat_topic", "chime/heartbeat\n"),
        ("notification_success_sound_path", ""),
    ]


def generate_invalid_examples_cpp() -> str:
    rows = []
    for key, value in invalid_value_examples():
        rows.append(f"    {{{cpp_string_literal(key)}, {cpp_string_literal(value)}}}")
    return ",\n".join(rows)


def generate_migration_cpp(schema: dict[str, Any]) -> tuple[str, str, list[str]]:
    migrations = sorted(schema.get("migrations") or [], key=lambda item: int(item["to"]))
    arrays: list[str] = []
    steps: list[str] = []
    removed_keys: list[str] = []
    for mig in migrations:
        to = int(mig["to"])
        remove = list(mig.get("remove") or [])
        renames = list(mig.get("rename") or [])
        replaces = list(mig.get("replace") or [])
        fields = [f".to_version = {to}"]
        if remove:
            name = f"kConfigMigrationRemove{to}"
            inner = ", ".join(cpp_string_literal(key) for key in remove)
            arrays.append(f"inline constexpr const char *{name}[] = {{{inner}}};")
            fields += [f".remove = {name}", f".remove_count = {len(remove)}"]
            removed_keys.extend(remove)
        if renames:
            name = f"kConfigMigrationRename{to}"
            inner = ", ".join(
                f"{{{cpp_string_literal(item['from'])}, {cpp_string_literal(item['to'])}}}" for item in renames
            )
            arrays.append(f"inline constexpr ConfigRenameSpec {name}[] = {{{inner}}};")
            fields += [f".renames = {name}", f".rename_count = {len(renames)}"]
        if replaces:
            name = f"kConfigMigrationReplace{to}"
            inner = ", ".join(
                f"{{{cpp_string_literal(item['key'])}, {cpp_string_literal(item['from'])}, "
                f"{cpp_string_literal(item['to'])}}}"
                for item in replaces
            )
            arrays.append(f"inline constexpr ConfigValueReplaceSpec {name}[] = {{{inner}}};")
            fields += [f".replaces = {name}", f".replace_count = {len(replaces)}"]
        steps.append(f"    {{{', '.join(fields)}}}")
    unique_removed: list[str] = []
    for key in removed_keys:
        if key not in unique_removed:
            unique_removed.append(key)
    return "\n".join(arrays), ",\n".join(steps), unique_removed


def generate_cpp_types(schema: dict[str, Any]) -> str:
    fields = schema["fields"]
    runtime_fields = [field for field in fields if field["runtime"]]
    file_fields = [field for field in fields if field.get("persist") == "file"]
    core_fields = [field for field in fields if field["api"] and field["key"] != "wifi_password"]
    migration_arrays, migration_steps, removed_keys = generate_migration_cpp(schema)

    runtime_members = "\n".join(f"    {cpp_default(field)};" for field in runtime_fields)
    file_members = "\n".join(f"    {cpp_default(field)};" for field in file_fields)
    core_members = "\n".join(f"    {cpp_default(field)};" for field in core_fields)
    spec_rows = ",\n".join(spec_row(field) for field in fields)
    removed_key_list = ", ".join(cpp_string_literal(key) for key in removed_keys)
    invalid_examples = generate_invalid_examples_cpp()
    migration_arrays_block = f"{migration_arrays}\n\n" if migration_arrays else ""

    copies_runtime = []
    for field in runtime_fields:
        name = field["key"]
        copies_runtime.append(f"    out.{name} = file.{name};")
    copies_core = []
    for field in core_fields:
        if field.get("persist") != "file":
            continue
        name = field["key"]
        copies_core.append(f"    out.{name} = file.{name};")
    apply_core = []
    for field in core_fields:
        if field.get("persist") != "file":
            continue
        name = field["key"]
        apply_core.append(f"    file->{name} = core.{name};")

    replacements = ['        {"schema_version", std::to_string(kConfigSchemaVersion)},']
    for field in core_fields:
        if field.get("persist") != "file":
            continue
        name = field["key"]
        kind = field["type"]
        if name == "mqtt_password":
            replacements.append('        {"mqtt_password", mqtt_password},')
            continue
        if kind == "string":
            replacements.append(f'        {{"{name}", config.{name}}},')
        elif kind == "int":
            replacements.append(f'        {{"{name}", std::to_string(config.{name})}},')
        elif kind == "bool":
            replacements.append(f'        {{"{name}", oc::config::bool_to_text(config.{name})}},')
        elif kind == "csv":
            replacements.append(f'        {{"{name}", oc::config::join_csv(config.{name})}},')

    return f"""#ifndef CHIME_GENERATED_CONFIG_TYPES_H
#define CHIME_GENERATED_CONFIG_TYPES_H

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "oc/config/kv_config.h"

namespace chime {{

constexpr int kConfigSchemaVersion = {schema["schema_version"]};
constexpr int kLegacyUnversionedSchema = {schema["legacy_unversioned"]};

enum class ConfigValueType {{ kString, kInt, kBool, kCsv }};
enum class ConfigPersist {{ kNone, kFile, kWpa }};

struct ConfigFieldSpec {{
    const char *key;
    ConfigValueType type;
    const char *default_text;
    const char *repair_text;
    ConfigPersist persist;
    bool runtime;
    bool api;
    bool ui;
    bool init_only;
    bool secret;
    bool file_required;
    bool api_required;
    bool api_empty_ok;
    bool forbid_whitespace;
    bool forbid_newline;
    int min_value;
    int max_value;
    int min_len;
    int max_len;
}};

struct ConfigRenameSpec {{
    const char *from;
    const char *to;
}};

struct ConfigValueReplaceSpec {{
    const char *key;
    const char *from;
    const char *to;
}};

struct ConfigMigrationStep {{
    int to_version = 0;
    const char *const *remove = nullptr;
    std::size_t remove_count = 0;
    const ConfigRenameSpec *renames = nullptr;
    std::size_t rename_count = 0;
    const ConfigValueReplaceSpec *replaces = nullptr;
    std::size_t replace_count = 0;
}};

struct ConfigInvalidValueExample {{
    const char *key;
    const char *value;
}};

constexpr ConfigFieldSpec kAllConfigFields[] = {{
{spec_rows}
}};

inline constexpr const char *kRemovedConfigKeys[] = {{{removed_key_list}}};

{migration_arrays_block}inline constexpr ConfigMigrationStep kConfigMigrationSteps[] = {{
{migration_steps}
}};

inline constexpr ConfigInvalidValueExample kConfigInvalidValueExamples[] = {{
{invalid_examples}
}};

inline const ConfigFieldSpec *FindConfigField(std::string_view key) {{
    for (const auto &field : kAllConfigFields) {{
        if (key == field.key) {{
            return &field;
        }}
    }}
    return nullptr;
}}

inline bool ConfigFieldValueValid(const ConfigFieldSpec &spec, std::string_view value) {{
    if (spec.type == ConfigValueType::kInt) {{
        int parsed = 0;
        return oc::config::parse_int_value(value, spec.min_value, spec.max_value, &parsed);
    }}
    if (spec.type == ConfigValueType::kBool) {{
        bool parsed = false;
        return oc::config::parse_bool_value(value, &parsed);
    }}
    if (spec.type == ConfigValueType::kCsv) {{
        const auto items = oc::config::split_csv(value);
        if (spec.file_required && items.empty()) {{
            return false;
        }}
        for (const auto &item : items) {{
            if (!oc::config::string_value_valid(item, spec.min_len, spec.max_len, spec.forbid_whitespace,
                                                spec.forbid_newline)) {{
                return false;
            }}
        }}
        return true;
    }}
    return oc::config::string_value_valid(value, spec.min_len, spec.max_len, spec.forbid_whitespace,
                                         spec.forbid_newline);
}}

struct ChimeConfig {{
{runtime_members}
}};

{field_table("ChimeConfig", runtime_fields, "file_required")}

struct FileConfig {{
{file_members}
}};

{field_table("FileConfig", file_fields, "never")}

namespace webd {{

struct CoreConfig {{
{core_members}
}};

}} // namespace webd

inline ChimeConfig RuntimeConfigFromFile(const FileConfig &file) {{
    ChimeConfig out;
{chr(10).join(copies_runtime)}
    return out;
}}

inline webd::CoreConfig CoreConfigFromFile(const FileConfig &file) {{
    webd::CoreConfig out;
{chr(10).join(copies_core)}
    return out;
}}

inline void ApplyCoreConfigToFile(const webd::CoreConfig &core, FileConfig *file) {{
    if (file == nullptr) {{
        return;
    }}
{chr(10).join(apply_core)}
}}

inline std::map<std::string, std::string> CoreConfigFileReplacements(const webd::CoreConfig &config,
                                                                    const std::string &mqtt_password) {{
    return {{
{chr(10).join(replacements)}
    }};
}}

}} // namespace chime

#endif
"""


def json_set_expr(field: dict[str, Any]) -> str:
    name = field["key"]
    kind = field["type"]
    if kind == "string":
        return f'        {{"{name}", JsonValue::String(config.{name})}},'
    if kind == "int":
        return f'        {{"{name}", JsonValue::Number(static_cast<double>(config.{name}))}},'
    if kind == "bool":
        return f'        {{"{name}", JsonValue::Bool(config.{name})}},'
    if kind == "csv":
        return f'        {{"{name}", SerializeStringArray(config.{name})}},'
    raise ValueError(kind)


def json_read_expr(field: dict[str, Any]) -> str:
    name = field["key"]
    kind = field["type"]
    post = field.get("json_post", "required" if field.get("api_required") else "optional")
    if name == "wifi_password":
        return """    const auto wifi_password = ReadOptionalString(object, "wifi_password", errors);
    if (wifi_password.has_value()) {
        request->wifi_password = wifi_password;
    }"""
    if name == "mqtt_password":
        return """    const auto mqtt_password = ReadOptionalString(object, "mqtt_password", errors);
    if (mqtt_password.has_value()) {
        request->mqtt_password = mqtt_password;
    }"""
    if post == "optional":
        reader = {
            "string": "ReadOptionalString",
        }[kind]
        return f"""    const auto {name} = {reader}(object, "{name}", errors);
    if ({name}.has_value()) {{
        request->config.{name} = *{name};
    }}"""
    reader = {
        "string": "ReadRequiredString",
        "int": "ReadRequiredInt",
        "bool": "ReadRequiredBool",
        "csv": "ReadRequiredStringArray",
    }[kind]
    return f"""    const auto {name} = {reader}(object, "{name}", errors);
    if ({name}.has_value()) {{
        request->config.{name} = *{name};
    }}"""


def validate_call(field: dict[str, Any]) -> str:
    name = field["key"]
    kind = field["type"]
    if name == "wifi_password":
        return """    if (request.wifi_password.has_value() && !request.wifi_password->empty()) {
        ValidateApiString(::chime::FindConfigField("wifi_password"), *request.wifi_password, errors);
    }"""
    if name == "mqtt_password":
        return """    if (request.mqtt_password.has_value()) {
        ValidateApiString(::chime::FindConfigField("mqtt_password"), *request.mqtt_password, errors);
    }"""
    if kind == "string":
        return f'    ValidateApiString(::chime::FindConfigField("{name}"), request.config.{name}, errors);'
    if kind == "int":
        return f'    ValidateApiInt(::chime::FindConfigField("{name}"), request.config.{name}, errors);'
    if kind == "csv":
        return f'    ValidateApiCsv(::chime::FindConfigField("{name}"), request.config.{name}, errors);'
    return ""


def generate_cpp_json(schema: dict[str, Any]) -> str:
    fields = schema["fields"]
    get_fields = [
        field
        for field in fields
        if field["api"] and field.get("json_get", not field["secret"]) and field["key"] != "wifi_password"
    ]
    post_fields = [field for field in fields if field["api"] and field.get("json_post") not in (False, None, "omit")]

    get_entries = "\n".join(json_set_expr(field) for field in get_fields)
    read_entries = "\n\n".join(json_read_expr(field) for field in post_fields)
    validate_calls = "\n".join(line for line in (validate_call(field) for field in post_fields) if line)

    return f"""#ifndef CHIME_GENERATED_CONFIG_JSON_H
#define CHIME_GENERATED_CONFIG_JSON_H

#include <map>
#include <string>
#include <vector>

#include "chime/generated/config_types.h"
#include "oc/json/json.h"
#include "oc/json/validate.h"
#include "chime/webd_types.h"

namespace chime::webd {{
namespace generated_config_json {{

using oc::json::JsonValue;
using oc::json::ReadOptionalString;
using oc::json::ReadRequiredBool;
using oc::json::ReadRequiredInt;
using oc::json::ReadRequiredString;
using oc::json::ReadRequiredStringArray;

inline JsonValue SerializeStringArray(const std::vector<std::string> &items) {{
    std::vector<JsonValue> output;
    output.reserve(items.size());
    for (const auto &item : items) {{
        output.push_back(JsonValue::String(item));
    }}
    return JsonValue::Array(std::move(output));
}}

inline std::map<std::string, JsonValue> CoreConfigFieldsToJson(const CoreConfig &config) {{
    return {{
{get_entries}
    }};
}}

inline void ReadSaveRequestFromJson(const JsonValue &object, SaveRequest *request, std::vector<ValidationError> *errors) {{
    if (request == nullptr) {{
        return;
    }}

{read_entries}
}}

inline bool ContainsWhitespace(const std::string &value) {{
    return oc::config::contains_whitespace(value);
}}

inline bool ContainsNewline(const std::string &value) {{
    return oc::config::contains_newline(value);
}}

inline void ValidateApiString(const ::chime::ConfigFieldSpec *spec, const std::string &value,
                              std::vector<ValidationError> *errors) {{
    if (spec == nullptr || errors == nullptr) {{
        return;
    }}
    if (spec->api_required && !spec->api_empty_ok && value.empty()) {{
        errors->push_back({{spec->key, std::string(spec->key) + " is required"}});
        return;
    }}
    if (!oc::config::string_value_valid(value, spec->min_len, spec->max_len, spec->forbid_whitespace,
                                        spec->forbid_newline)) {{
        if (ContainsNewline(value)) {{
            errors->push_back({{spec->key, std::string(spec->key) + " must not contain newline characters"}});
            return;
        }}
        if (spec->min_len > 0 && value.size() < static_cast<std::size_t>(spec->min_len)) {{
            errors->push_back({{spec->key, std::string(spec->key) + " must be >= " + std::to_string(spec->min_len) + " chars"}});
            return;
        }}
        if (spec->max_len > 0 && value.size() > static_cast<std::size_t>(spec->max_len)) {{
            errors->push_back({{spec->key, std::string(spec->key) + " must be <= " + std::to_string(spec->max_len) + " chars"}});
            return;
        }}
        if (spec->forbid_whitespace && ContainsWhitespace(value)) {{
            errors->push_back({{spec->key, std::string(spec->key) + " must not contain spaces"}});
        }}
    }}
}}

inline void ValidateApiInt(const ::chime::ConfigFieldSpec *spec, int value, std::vector<ValidationError> *errors) {{
    if (spec == nullptr || errors == nullptr) {{
        return;
    }}
    if (value < spec->min_value || value > spec->max_value) {{
        errors->push_back({{spec->key, std::string(spec->key) + " must be " + std::to_string(spec->min_value) + "-" +
                                           std::to_string(spec->max_value)}});
    }}
}}

inline void ValidateApiCsv(const ::chime::ConfigFieldSpec *spec, const std::vector<std::string> &items,
                           std::vector<ValidationError> *errors) {{
    if (spec == nullptr || errors == nullptr) {{
        return;
    }}
    if (spec->api_required && items.empty()) {{
        errors->push_back({{spec->key, std::string(spec->key) + " must contain at least one topic"}});
        return;
    }}
    for (std::size_t i = 0; i < items.size(); ++i) {{
        if (items[i].empty() || !oc::config::string_value_valid(items[i], spec->min_len, spec->max_len,
                                                                spec->forbid_whitespace, spec->forbid_newline)) {{
            errors->push_back({{spec->key, std::string(spec->key) + "[" + std::to_string(i) + "] is invalid"}});
        }}
    }}
}}

inline void ValidateSaveRequest(const SaveRequest &request, std::vector<ValidationError> *errors) {{
    if (errors == nullptr) {{
        return;
    }}
{validate_calls}
}}

}} // namespace generated_config_json
}} // namespace chime::webd

#endif
"""


def generate_ts(schema: dict[str, Any]) -> str:
    fields = [
        field
        for field in schema["fields"]
        if field["ui"] and not field["secret"] and field["key"] != "wifi_password"
    ]
    type_lines = []
    default_lines = []
    bounds_lines = []
    for field in fields:
        optional = field.get("json_post") == "optional"
        suffix = "?" if optional else ""
        type_lines.append(f"  {field['key']}{suffix}: {ts_type(field)};")
        default_lines.append(f"  {field['key']}: {ts_default(field)},")
        if field["type"] == "int":
            bounds_lines.append(
                f'  {field["key"]}: {{ min: {int(field["min"])}, max: {int(field["max"])} }},'
            )
    return f"""export const CONFIG_SCHEMA_VERSION = {schema["schema_version"]} as const;

export type CoreConfigFields = {{
{chr(10).join(type_lines)}
}};

export const CORE_CONFIG_DEFAULTS: CoreConfigFields = {{
{chr(10).join(default_lines)}
}};

export const CORE_CONFIG_INT_BOUNDS = {{
{chr(10).join(bounds_lines)}
}} as const;
"""


def inventory_row(field: dict[str, Any]) -> str:
    persist = field.get("persist") or "none"
    required = "required" if field.get("file_required") or field.get("api_required") else "optional"
    if field["secret"]:
        secret = "redact on read; preserve if omitted"
    else:
        secret = "no"
    kind = field["type"]
    if kind == "int":
        valid = f"{field['min']}-{field['max']}"
    elif kind == "csv":
        valid = "comma-separated non-empty tokens" if field.get("file_required") else "comma-separated tokens"
    else:
        bits = []
        if field.get("max_len"):
            bits.append(f"max {field['max_len']} chars")
        if field.get("forbid_whitespace"):
            bits.append("no whitespace")
        valid = ", ".join(bits) if bits else "any string"
    default = default_text(field)
    if "shipped" in field:
        default_cell = f"{default} (shipped {shipped_text(field)})"
    else:
        default_cell = default
    owners = ",".join(field.get("owners") or [])
    consumers = []
    if field["runtime"]:
        consumers.append("runtime")
    if field["api"]:
        consumers.append("webd")
    if field["ui"]:
        consumers.append("ui")
    if field["init"]:
        consumers.append("init-only")
    if "schema" in (field.get("owners") or []):
        consumers.append("schema")
    role = "/".join(consumers) if consumers else "documented"
    notes = (field.get("notes") or "").replace("|", "\\|")
    return (
        f"| `{field['key']}` | {owners} | {kind} | `{default_cell}` | {required} | {valid} | {secret} | "
        f"{persist} | {role} | {notes} |"
    )


def current_version_notes(schema: dict[str, Any]) -> str:
    version = int(schema["schema_version"])
    bits: list[str] = []
    for item in (schema.get("removed") or {}).get(str(version), []):
        bits.append(
            f"`{item['key']}` is removed in this version. Existing files lose that key during migration."
        )
    for mig in schema.get("migrations") or []:
        if int(mig["to"]) != version:
            continue
        for item in mig.get("rename") or []:
            bits.append(f"`{item['from']}` is renamed to `{item['to']}` in this version.")
        replaces = mig.get("replace") or []
        if replaces:
            moves = "; ".join(f"`{item['key']}` from `{item['from']}` to `{item['to']}`" for item in replaces)
            bits.append(f"Files that still hold every old default are migrated: {moves}. Custom values are kept.")
    extra = schema.get("version_notes")
    if extra:
        bits.append(str(extra))
    return " ".join(bits)


def removed_inventory_rows(schema: dict[str, Any]) -> str:
    rows: list[str] = []
    removed = schema.get("removed") or {}
    for version in sorted(removed, key=lambda value: int(value)):
        for item in removed[version]:
            rows.append(f"| `{item['key']}` | dropped in v{version} | {item['reason']} |")
    return "\n".join(rows)


def renamed_inventory_section(schema: dict[str, Any]) -> str:
    rows: list[str] = []
    for mig in schema.get("migrations") or []:
        for item in mig.get("rename") or []:
            rows.append(f"| `{item['from']}` | renamed in v{mig['to']} | `{item['to']}` |")
    if not rows:
        return ""
    return (
        "## Renamed keys\n\n"
        "| Key | When | New name |\n"
        "| --- | --- | --- |\n" + "\n".join(rows) + "\n\n"
    )


def generate_doc(schema: dict[str, Any]) -> str:
    rows = "\n".join(inventory_row(field) for field in schema["fields"])
    removed_rows = removed_inventory_rows(schema)
    renamed_section = renamed_inventory_section(schema)
    return f"""# Chime config schema

Product schema version **{schema["schema_version"]}**. `buildroot/version.env` `CHIME_CONFIG_VERSION` is the release-level gate and must equal this integer. The persisted file key is `schema_version`.

Source of truth: `schema/chime_config.json`. Generated artifacts:

- `chime/include/chime/generated/config_types.h`
- `chime/include/chime/generated/config_json.h`
- `webui/src/generated/config_schema.ts`

Regenerate with `python3 scripts/gen_chime_config_schema.py`. `scripts/check_config_schema.sh` fails when generated files, `chime.conf`, init-script defaults, or `CHIME_CONFIG_VERSION` drift.

## Ownership

| Role | Meaning |
| --- | --- |
| runtime | `chime` daemon (`ChimeConfig`) |
| webd | `chime-webd` HTTP API (`CoreConfig`) |
| ui | `webui` settings form |
| init-only | `S41timesync` or `S99chime`. Not daemon fields. |
| schema | migration / versioning |
| webd-process | read by `chime-webd` at start, not `/api/v1/config/core` |

Unknown assignment keys: **{schema["unknown_key_policy"]}**. {schema["unknown_key_policy_notes"]}

{current_version_notes(schema)}

## Key inventory

| Key | Owner | Type | Default | Required | Valid range | Secret | Persist | Role | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
{rows}

## Removed keys

| Key | When | Reason |
| --- | --- | --- |
{removed_rows}

{renamed_section}## Before / after samples

Before (shipped schema 4, no persisted version key): [`docs/config-samples/chime.conf.v4`](config-samples/chime.conf.v4).

After: [`buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf`](../buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf).

Migration from unversioned files treats them as schema {schema["legacy_unversioned"]}. `chime-migrate` runs from `S32config-migrate` after `S31persistent` bind-mounts `/data/etc` onto `/etc/persistent` and before `S41timesync`, `S45webd`, and `S99chime`. The daemons also migrate on start so `scripts/local_chime.sh` stays consistent. Ordered per-version steps in `schema/chime_config.json` `migrations` run from the file version to the current schema, then missing keys are filled and invalid values repaired. On rewrite, migration writes `<resolved-path>.bak` (following the `/etc/chime.conf` symlink) and replaces the live file with `rename(2)`. A write failure leaves the original inode unchanged. Malformed or future `schema_version` values are a permanent failure: `chime-migrate` and the daemons exit 78 (`EX_CONFIG`), `S32config-migrate` records `/var/lib/chime/config.fatal`, and `S45webd` / `S99chime` do not restart.

Secrets: GET `/api/v1/config/core` returns `wifi_password_set` and `mqtt_password_set`, never the password values. POST may omit `wifi_password` / `mqtt_password` to keep the stored secret.
"""


def generate_contract_test(schema: dict[str, Any]) -> str:
    checks = []
    for field in schema["fields"]:
        if not (field["runtime"] and field["api"] and field["key"] != "mqtt_password"):
            continue
        name = field["key"]
        checks.append(f"        CHECK(runtime.{name} == core.{name});")
        if field.get("persist") == "file":
            checks.append(f"        CHECK(runtime.{name} == file.{name});")

    constraint_cases = []
    for field in schema["fields"]:
        if not field["api"]:
            continue
        name = field["key"]
        if name in ("wifi_password", "mqtt_password"):
            continue
        if field.get("forbid_whitespace") and field["type"] == "string":
            constraint_cases.append(
                f"""        request = valid;
        request.config.{name} = "bad value";
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "{name}"));"""
            )
        if field["type"] == "string" and field.get("api_required") and not field.get("api_empty_ok"):
            constraint_cases.append(
                f"""        request = valid;
        request.config.{name} = "";
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "{name}"));"""
            )
        if field["type"] == "string" and field.get("max_len"):
            constraint_cases.append(
                f"""        request = valid;
        request.config.{name} = std::string({int(field["max_len"]) + 1}, 'x');
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "{name}"));"""
            )
        if field["type"] == "int" and "max" in field:
            constraint_cases.append(
                f"""        request = valid;
        request.config.{name} = {int(field["max"]) + 1};
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "{name}"));"""
            )
        if field["type"] == "csv" and field.get("forbid_whitespace"):
            constraint_cases.append(
                f"""        request = valid;
        request.config.{name} = {{"bad topic"}};
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "{name}"));"""
            )

    return f"""#include <iterator>
#include <string>
#include <vector>

#include "chime/chime_config.h"
#include "chime/config_migrate.h"
#include "chime/generated/config_json.h"
#include "chime/generated/config_types.h"
#include "chime/webd_types.h"
#include "doctest.h"
#include "oc/config/kv_document.h"

namespace {{

bool HasFieldError(const std::vector<chime::webd::ValidationError> &errors, const std::string &field) {{
    for (const auto &error : errors) {{
        if (error.field == field) {{
            return true;
        }}
    }}
    return false;
}}

chime::webd::SaveRequest ValidApiSaveRequest() {{
    chime::webd::SaveRequest request;
    request.config.wifi_ssid = "net";
    request.config.mqtt_host = "broker";
    request.config.mqtt_topics = {{"ring/pressed"}};
    return request;
}}

}} // namespace

TEST_SUITE("config_schema_contract") {{
    TEST_CASE("schema version matches generated constant") {{
        CHECK(chime::kConfigSchemaVersion == {schema["schema_version"]});
        CHECK(chime::kLegacyUnversionedSchema == {schema["legacy_unversioned"]});
        CHECK(chime::FileConfig{{}}.schema_version == chime::kConfigSchemaVersion);
    }}

    TEST_CASE("runtime, file, and API structs share defaults for overlapping fields") {{
        const chime::ChimeConfig runtime;
        const chime::FileConfig file;
        const chime::webd::CoreConfig core;
{chr(10).join(checks)}
    }}

    TEST_CASE("removed keys are listed for migration") {{
        bool found = false;
        for (const char *key : chime::kRemovedConfigKeys) {{
            if (std::string(key) == "volume_other") {{
                found = true;
            }}
        }}
        CHECK(found);
        CHECK(chime::FindConfigField("volume_other") == nullptr);
        CHECK(chime::FindConfigField("volume_bell") == nullptr);
        CHECK(chime::FindConfigField("volume_ring") != nullptr);
        CHECK(chime::FindConfigField("mqtt_host") != nullptr);
        CHECK(chime::FindConfigField("ntp_servers")->init_only);
        CHECK_FALSE(chime::FindConfigField("ntp_servers")->runtime);
        REQUIRE(std::size(chime::kConfigMigrationSteps) >= 1);
        CHECK(chime::kConfigMigrationSteps[0].to_version == 5);
        CHECK(chime::kConfigMigrationSteps[std::size(chime::kConfigMigrationSteps) - 1].to_version ==
              chime::kConfigSchemaVersion);
    }}

    TEST_CASE("value replacements move old defaults to the schema defaults") {{
        for (const auto &step : chime::kConfigMigrationSteps) {{
            if (step.replace_count == 0) {{
                continue;
            }}
            std::string text;
            for (std::size_t i = 0; i < step.replace_count; ++i) {{
                text += std::string(step.replaces[i].key) + "=" + step.replaces[i].from + "\\n";
            }}
            auto document = oc::config::ParseKvDocument(text);
            chime::ApplyConfigMigrationSteps(&document, step.to_version - 1, step.to_version, &step, 1);
            for (std::size_t i = 0; i < step.replace_count; ++i) {{
                const auto *field = chime::FindConfigField(step.replaces[i].key);
                REQUIRE(field != nullptr);
                CHECK(oc::config::KvDocumentValue(document, step.replaces[i].key) == step.replaces[i].to);
                if (step.to_version == chime::kConfigSchemaVersion) {{
                    CHECK(std::string(step.replaces[i].to) == field->repair_text);
                }}
            }}
        }}
    }}

    TEST_CASE("schema field specs expose API validation metadata") {{
        const auto *host = chime::FindConfigField("mqtt_host");
        REQUIRE(host != nullptr);
        CHECK(host->api_required);
        CHECK_FALSE(host->api_empty_ok);
        CHECK(host->forbid_whitespace);
        CHECK(host->forbid_newline);
        CHECK(host->max_len == 256);

        const auto *volume = chime::FindConfigField("volume_ring");
        REQUIRE(volume != nullptr);
        CHECK(volume->min_value == 0);
        CHECK(volume->max_value == 100);
        CHECK(std::string(chime::FindConfigField("mqtt_topics")->repair_text) == "ring/+/pressed/#,ring/+/status");
        CHECK(std::string(chime::FindConfigField("mqtt_topics")->default_text).empty());
    }}

    TEST_CASE("schema constraints affect API validation") {{
        const auto valid = ValidApiSaveRequest();
        auto request = valid;
        std::vector<chime::webd::ValidationError> errors;
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(errors.empty());

{chr(10).join(constraint_cases)}

        request = valid;
        request.config.mqtt_client_id = "x\\naudio_enabled=false";
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "mqtt_client_id"));

        request = valid;
        request.config.mqtt_topics = {{"ring/pressed\\naudio_enabled=false"}};
        errors.clear();
        chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
        CHECK(HasFieldError(errors, "mqtt_topics"));
    }}

    TEST_CASE("shared invalid fixtures fail generated field validation") {{
        for (const auto &example : chime::kConfigInvalidValueExamples) {{
            const auto *spec = chime::FindConfigField(example.key);
            REQUIRE(spec != nullptr);
            CHECK_FALSE(chime::ConfigFieldValueValid(*spec, example.value));
        }}
    }}
}}
"""


def render_all(schema: dict[str, Any]) -> dict[Path, str]:
    return {
        CPP_HEADER: generate_cpp_types(schema),
        CPP_JSON: generate_cpp_json(schema),
        TS_PATH: generate_ts(schema),
        DOC_PATH: generate_doc(schema),
        CONTRACT_TEST: generate_contract_test(schema),
    }


def write_outputs(outputs: dict[Path, str]) -> None:
    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        if not content.endswith("\n"):
            content += "\n"
        path.write_text(content, encoding="utf-8")


def check_outputs(outputs: dict[Path, str]) -> int:
    failed = False
    for path, content in outputs.items():
        if not content.endswith("\n"):
            content += "\n"
        if not path.exists():
            print(f"missing generated file: {path}", file=sys.stderr)
            failed = True
            continue
        actual = path.read_text(encoding="utf-8")
        if actual != content:
            print(f"generated file is stale: {path}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def conf_keys(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value
    return values


def read_shell_default(path: Path, name: str) -> str:
    prefix = f"{name}="
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith(prefix):
            value = line[len(prefix) :]
            if value.startswith('"') and value.endswith('"'):
                return value[1:-1]
            return value
    raise KeyError(name)


def shipped_text(field: dict[str, Any]) -> str:
    value = field["shipped"] if "shipped" in field else field["default"]
    if isinstance(value, list):
        return ",".join(str(item) for item in value)
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def check_product(schema: dict[str, Any]) -> int:
    failed = False
    version_text = (REPO / "buildroot" / "version.env").read_text(encoding="utf-8")
    config_version = None
    for line in version_text.splitlines():
        if line.startswith("CHIME_CONFIG_VERSION="):
            config_version = int(line.split("=", 1)[1].strip())
    if config_version != schema["schema_version"]:
        print(
            f"CHIME_CONFIG_VERSION={config_version} does not match schema_version={schema['schema_version']}",
            file=sys.stderr,
        )
        failed = True

    migrations = schema.get("migrations")
    if not isinstance(migrations, list) or not migrations:
        print("schema migrations must be a non-empty list", file=sys.stderr)
        failed = True
    else:
        tos = [int(item["to"]) for item in migrations]
        if tos != sorted(tos) or len(set(tos)) != len(tos):
            print("schema migrations must have unique increasing to versions", file=sys.stderr)
            failed = True
        elif tos[-1] != schema["schema_version"]:
            print("last migration to= must equal schema_version", file=sys.stderr)
            failed = True
        removed = schema.get("removed") or {}
        for item in migrations:
            to = str(int(item["to"]))
            expected = [entry["key"] for entry in removed.get(to, [])]
            actual = list(item.get("remove") or [])
            if expected != actual:
                print(
                    f"migrations to {to} remove {actual!r} != removed.{to} keys {expected!r}",
                    file=sys.stderr,
                )
                failed = True
        file_fields = {field["key"]: field for field in schema["fields"] if field.get("persist") == "file"}
        for item in migrations:
            current = int(item["to"]) == schema["schema_version"]
            for replace in item.get("replace") or []:
                field = file_fields.get(replace["key"])
                if not replace["from"] or replace["from"] == replace["to"]:
                    print(
                        f"migrations to {item['to']} replace {replace['key']} needs a non-empty from != to",
                        file=sys.stderr,
                    )
                    failed = True
                if field is None:
                    print(f"migrations to {item['to']} replace unknown key {replace['key']!r}", file=sys.stderr)
                    failed = True
                elif current and replace["to"] != shipped_text(field):
                    print(
                        f"migrations to {item['to']} replace {replace['key']} to {replace['to']!r} "
                        f"!= schema shipped {shipped_text(field)!r}",
                        file=sys.stderr,
                    )
                    failed = True

    conf_path = REPO / "buildroot" / "board" / "raspberrypi0w" / "rootfs_overlay" / "etc" / "chime.conf"
    shipped = conf_keys(conf_path)
    expected_file_keys = {field["key"] for field in schema["fields"] if field.get("persist") == "file"}
    extra = set(shipped) - expected_file_keys
    missing = expected_file_keys - set(shipped)
    if extra:
        print(f"chime.conf has keys not in schema: {sorted(extra)}", file=sys.stderr)
        failed = True
    if missing:
        print(f"schema file keys missing from chime.conf: {sorted(missing)}", file=sys.stderr)
        failed = True

    for field in schema["fields"]:
        if field.get("persist") != "file":
            continue
        key = field["key"]
        if key not in shipped:
            continue
        expected = shipped_text(field)
        if shipped[key] != expected:
            print(f"chime.conf {key}={shipped[key]!r} != schema shipped {expected!r}", file=sys.stderr)
            failed = True

    app = (REPO / "webui" / "src" / "App.svelte").read_text(encoding="utf-8")
    if "volume_other" in app or "volumeOther" in app:
        print("volume_other still present in webui/src/App.svelte", file=sys.stderr)
        failed = True
    if "volume_other" in shipped:
        print("volume_other still present in chime.conf", file=sys.stderr)
        failed = True
    if "volume_bell" in shipped:
        print("volume_bell still present in chime.conf", file=sys.stderr)
        failed = True

    for path in (REPO / "webui" / "src").rglob("*"):
        if path.suffix not in {".ts", ".svelte"}:
            continue
        text = path.read_text(encoding="utf-8")
        if "volume_bell" in text or "volumeBell" in text:
            print(f"volume_bell still present in {path.relative_to(REPO)}", file=sys.stderr)
            failed = True

    timesync = (
        REPO
        / "buildroot"
        / "board"
        / "common"
        / "rootfs_overlay"
        / "etc"
        / "init.d"
        / "S41timesync"
    )
    chime_init = (
        REPO / "buildroot" / "board" / "raspberrypi0w" / "rootfs_overlay" / "etc" / "init.d" / "S99chime"
    )
    init_defaults = {
        "ntp_servers": read_shell_default(timesync, "NTP_SERVERS_DEFAULT"),
        "time_http_urls": read_shell_default(timesync, "TIME_HTTP_URLS_DEFAULT"),
        "time_sync_retries": read_shell_default(timesync, "TIME_SYNC_RETRIES_DEFAULT"),
        "time_sync_retry_delay": read_shell_default(timesync, "TIME_SYNC_RETRY_DELAY_DEFAULT"),
        "time_sync_interval": read_shell_default(timesync, "TIME_SYNC_INTERVAL_DEFAULT"),
        "log_max_bytes": read_shell_default(chime_init, "LOG_MAX_BYTES_DEFAULT"),
        "log_rotate_keep": read_shell_default(chime_init, "LOG_ROTATE_KEEP_DEFAULT"),
        "log_rotate_check_interval": read_shell_default(chime_init, "LOG_ROTATE_CHECK_INTERVAL_DEFAULT"),
    }
    by_key = {field["key"]: field for field in schema["fields"]}
    for key, shell_default in init_defaults.items():
        expected = default_text(by_key[key])
        if shell_default != expected:
            print(f"init default {key}={shell_default!r} != schema {expected!r}", file=sys.stderr)
            failed = True

    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify committed generated files")
    args = parser.parse_args()
    schema = load_schema()
    outputs = render_all(schema)
    if args.check:
        generated = check_outputs(outputs)
        product = check_product(schema)
        return generated or product
    write_outputs(outputs)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
