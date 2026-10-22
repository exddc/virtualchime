#ifndef CHIME_CONFIG_MIGRATE_H
#define CHIME_CONFIG_MIGRATE_H

#include <cstddef>
#include <string>
#include <vector>

#include "chime/generated/config_types.h"
#include "oc/config/kv_document.h"

namespace chime {

constexpr int kConfigFatalExitCode = 78;

struct MigrateResult {
    bool success = false;
    bool rewritten = false;
    bool unsupported_version = false;
    bool malformed_version = false;
    int from_version = 0;
    int to_version = 0;
    std::vector<std::string> replaced_values;
    std::string error;
};

MigrateResult MigratePersistedConfig(const std::string &path);

std::vector<std::string> ApplyConfigMigrationSteps(std::vector<oc::config::KvEntry> *document, int from_version,
                                                   int to_version, const ConfigMigrationStep *steps,
                                                   std::size_t step_count);
void FillAndRepairKnownFields(std::vector<oc::config::KvEntry> *document);

inline bool MigrateFailureBlocksStartup(const MigrateResult &result) {
    return result.malformed_version || result.unsupported_version;
}

} // namespace chime

#endif
