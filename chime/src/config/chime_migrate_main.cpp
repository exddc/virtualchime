#include <iostream>
#include <string>

#include "chime/config_migrate.h"

int main(int argc, char *argv[]) {
    std::string path = "/etc/chime.conf";
    if (argc > 2) {
        std::cerr << "Usage: chime-migrate [path]\n";
        return 2;
    }
    if (argc == 2) {
        const std::string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: chime-migrate [path]\n";
            return 0;
        }
        path = arg;
    }

    const auto result = chime::MigratePersistedConfig(path);
    if (!result.success) {
        std::cerr << "chime-migrate: " << result.error << "\n";
        return chime::MigrateFailureBlocksStartup(result) ? chime::kConfigFatalExitCode : 1;
    }
    if (result.rewritten) {
        std::cout << "migrated " << path << " from schema " << result.from_version << " to " << result.to_version
                  << "\n";
        for (const auto &value : result.replaced_values) {
            std::cout << "replaced default " << value << "\n";
        }
    }
    return 0;
}
