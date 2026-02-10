#ifndef VC_LOGGING_LOGGER_H
#define VC_LOGGING_LOGGER_H

#include <mutex>
#include <string_view>

namespace vc::logging {

enum class Level { kInfo, kWarn, kError };

class Logger {
 public:
  virtual ~Logger() = default;
  virtual void Log(Level level, std::string_view component,
                   std::string_view message) = 0;

  void Info(std::string_view component, std::string_view message) {
    Log(Level::kInfo, component, message);
  }

  void Warn(std::string_view component, std::string_view message) {
    Log(Level::kWarn, component, message);
  }

  void Error(std::string_view component, std::string_view message) {
    Log(Level::kError, component, message);
  }
};

class StderrLogger final : public Logger {
 public:
  void Log(Level level, std::string_view component,
           std::string_view message) override;

 private:
  std::mutex mutex_;
};

}  // namespace vc::logging

#endif
