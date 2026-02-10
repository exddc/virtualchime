#include "vc/logging/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace vc::logging {
namespace {
const char* LevelName(Level level) {
  switch (level) {
    case Level::kInfo:
      return "INFO";
    case Level::kWarn:
      return "WARN";
    case Level::kError:
      return "ERROR";
  }
  return "INFO";
}

std::string NowString() {
  const auto now = std::chrono::system_clock::now();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
      1000;
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
#if defined(_WIN32)
  localtime_s(&local_tm, &now_time);
#else
  localtime_r(&now_time, &local_tm);
#endif

  std::ostringstream out;
  out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3)
      << std::setfill('0') << millis.count();
  return out.str();
}
}  // namespace

void StderrLogger::Log(Level level, std::string_view component,
                       std::string_view message) {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::cerr << NowString() << " [" << LevelName(level) << "] [" << component
            << "] " << message << "\n";
}

}  // namespace vc::logging
