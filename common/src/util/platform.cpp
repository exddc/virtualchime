#include "vc/util/platform.h"

namespace vc::util {

bool IsLinux() {
#ifdef __linux__
  return true;
#else
  return false;
#endif
}

}  // namespace vc::util
