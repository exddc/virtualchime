#include "chime/config_migrate.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <utility>

#include "oc/util/filesystem.h"

namespace chime {
namespace {

constexpr mode_t kChimeConfigMode = 0600;

bool ReadFile(const std::string &path, std::string *content, std::string *error) {
    if (content == nullptr || error == nullptr) {
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        *error = "failed to open config '" + path + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        *error = "failed to read config '" + path + "'";
        return false;
    }
    *content = std::move(text);
    return true;
}

bool ParseSchemaVersion(const std::vector<oc::config::KvEntry> &document, int *version, std::string *error) {
    if (version == nullptr || error == nullptr) {
        return false;
    }
    if (!oc::config::KvDocumentHasKey(document, "schema_version")) {
        *version = kLegacyUnversionedSchema;
        return true;
    }
    const std::string raw = oc::config::KvDocumentValue(document, "schema_version");
    if (!oc::config::parse_int_value(raw, 1, 1000000, version)) {
        *error = "malformed schema_version";
        return false;
    }
    return true;
}

void DropRemovedKeys(std::vector<oc::config::KvEntry> *document) {
    for (const char *key : kRemovedConfigKeys) {
        oc::config::KvDocumentRemoveKey(*document, key);
    }
}

bool HoldsReplacedDefaults(const std::vector<oc::config::KvEntry> &document, const ConfigMigrationStep &step) {
    for (std::size_t i = 0; i < step.replace_count; ++i) {
        const ConfigValueReplaceSpec &replace = step.replaces[i];
        if (oc::config::KvDocumentHasKey(document, replace.key) &&
            oc::config::KvDocumentValue(document, replace.key) != replace.from) {
            return false;
        }
    }
    return true;
}

std::string PersistentBackupPath(const std::string &path) {
    std::error_code ec;
    std::filesystem::path resolved(path);
    if (std::filesystem::is_symlink(resolved, ec) && !ec) {
        std::filesystem::path target = std::filesystem::read_symlink(resolved, ec);
        if (!ec) {
            if (target.is_relative()) {
                target = resolved.parent_path() / target;
            }
            resolved = target.lexically_normal();
        }
    }
    return resolved.string() + ".bak";
}

} // namespace

std::vector<std::string> ApplyConfigMigrationSteps(std::vector<oc::config::KvEntry> *document, int from_version,
                                                   int to_version, const ConfigMigrationStep *steps,
                                                   std::size_t step_count) {
    std::vector<std::string> replaced;
    if (document == nullptr || steps == nullptr) {
        return replaced;
    }
    for (std::size_t i = 0; i < step_count; ++i) {
        const ConfigMigrationStep &step = steps[i];
        if (step.to_version <= from_version || step.to_version > to_version) {
            continue;
        }
        for (std::size_t r = 0; r < step.remove_count; ++r) {
            oc::config::KvDocumentRemoveKey(*document, step.remove[r]);
        }
        for (std::size_t n = 0; n < step.rename_count; ++n) {
            oc::config::KvDocumentRenameKey(*document, step.renames[n].from, step.renames[n].to);
        }
        if (!HoldsReplacedDefaults(*document, step)) {
            continue;
        }
        for (std::size_t v = 0; v < step.replace_count; ++v) {
            const ConfigValueReplaceSpec &replace = step.replaces[v];
            oc::config::KvDocumentSetValue(*document, replace.key, replace.to);
            replaced.push_back(std::string(replace.key) + ": " + replace.from + " -> " + replace.to);
        }
    }
    return replaced;
}

void FillAndRepairKnownFields(std::vector<oc::config::KvEntry> *document) {
    if (document == nullptr) {
        return;
    }
    DropRemovedKeys(document);
    for (const auto &spec : kAllConfigFields) {
        if (spec.persist != ConfigPersist::kFile) {
            continue;
        }
        if (!oc::config::KvDocumentHasKey(*document, spec.key)) {
            oc::config::KvDocumentSetValue(*document, spec.key, spec.repair_text);
            continue;
        }
        const std::string current = oc::config::KvDocumentValue(*document, spec.key);
        if (!ConfigFieldValueValid(spec, current)) {
            oc::config::KvDocumentSetValue(*document, spec.key, spec.repair_text);
        }
    }
    oc::config::KvDocumentSetValue(*document, "schema_version", std::to_string(kConfigSchemaVersion));
}

MigrateResult MigratePersistedConfig(const std::string &path) {
    MigrateResult result;
    result.to_version = kConfigSchemaVersion;

    std::string original;
    if (!ReadFile(path, &original, &result.error)) {
        return result;
    }

    auto document = oc::config::ParseKvDocument(original);
    if (!ParseSchemaVersion(document, &result.from_version, &result.error)) {
        result.malformed_version = true;
        return result;
    }
    if (result.from_version > kConfigSchemaVersion) {
        result.unsupported_version = true;
        result.error = "config schema_version is newer than this software";
        return result;
    }

    result.replaced_values = ApplyConfigMigrationSteps(&document, result.from_version, kConfigSchemaVersion,
                                                       kConfigMigrationSteps, std::size(kConfigMigrationSteps));
    FillAndRepairKnownFields(&document);
    const std::string migrated = oc::config::RenderKvDocument(document);
    if (migrated == original) {
        result.success = true;
        result.rewritten = false;
        result.from_version = kConfigSchemaVersion;
        return result;
    }

    const std::string backup_path = PersistentBackupPath(path);
    if (!oc::util::AtomicWriteFile(backup_path, original, kChimeConfigMode, &result.error)) {
        return result;
    }
    if (!oc::util::AtomicWriteFile(path, migrated, kChimeConfigMode, &result.error)) {
        return result;
    }

    result.success = true;
    result.rewritten = true;
    result.to_version = kConfigSchemaVersion;
    return result;
}

} // namespace chime
