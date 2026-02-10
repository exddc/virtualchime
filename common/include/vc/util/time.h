#ifndef VC_UTIL_TIME_H
#define VC_UTIL_TIME_H

#include <ctime>

namespace vc::util {

bool ClockIsSane(std::time_t minimum_epoch);

}  // namespace vc::util

#endif
