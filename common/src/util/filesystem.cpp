#include "vc/util/filesystem.h"

#include <fstream>

#include "vc/config/kv_config.h"

namespace vc::util {

bool FileExists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

std::string ReadTrimmedFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::string line;
  std::getline(file, line);
  return vc::config::trim(line);
}

}  // namespace vc::util
