#include "vc/util/time.h"

namespace vc::util {

bool ClockIsSane(std::time_t minimum_epoch) {
  return std::time(nullptr) >= minimum_epoch;
}

}  // namespace vc::util
