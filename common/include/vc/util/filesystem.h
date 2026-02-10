#ifndef VC_UTIL_FILESYSTEM_H
#define VC_UTIL_FILESYSTEM_H

#include <string>

namespace vc::util {

bool FileExists(const std::string& path);
std::string ReadTrimmedFile(const std::string& path);

}  // namespace vc::util

#endif
