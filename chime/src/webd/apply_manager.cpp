#include "chime/webd_apply_manager.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

#include <sys/wait.h>

#include "vc/logging/logger.h"

namespace chime::webd {
namespace {

std::string NowIso8601Utc() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

  std::tm utc_tm{};
#if defined(_WIN32)
  gmtime_s(&utc_tm, &now_time);
#else
  gmtime_r(&now_time, &utc_tm);
#endif

  std::ostringstream out;
  out << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace

ApplyManager::ApplyManager(vc::logging::Logger& logger,
                           std::string network_restart_command,
                           std::string chime_restart_command)
    : logger_(logger),
      network_restart_command_(std::move(network_restart_command)),
      chime_restart_command_(std::move(chime_restart_command)) {
  status_.state = "idle";
}

ApplyStatus ApplyManager::StartApply() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.state == "pending" || status_.state == "running") {
    return status_;
  }

  status_.job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);
  status_.state = "pending";
  status_.error.clear();
  status_.started_at_utc = NowIso8601Utc();
  status_.finished_at_utc.clear();

  const unsigned long long job_id = status_.job_id;
  std::thread([this, job_id]() { RunApplyJob(job_id); }).detach();

  return status_;
}

ApplyStatus ApplyManager::CurrentStatus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

void ApplyManager::RunApplyJob(unsigned long long job_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.job_id != job_id) {
      return;
    }
    status_.state = "running";
    status_.started_at_utc = NowIso8601Utc();
  }

  logger_.Info("webd", "apply job started id=" + std::to_string(job_id));

  std::string error;
  if (!RunCommand(network_restart_command_, &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = "failed";
    status_.finished_at_utc = NowIso8601Utc();
    status_.error = "network restart failed: " + error;
    logger_.Error("webd", "apply job failed id=" + std::to_string(job_id) +
                              " error='" + status_.error + "'");
    return;
  }

  if (!RunCommand(chime_restart_command_, &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = "failed";
    status_.finished_at_utc = NowIso8601Utc();
    status_.error = "chime restart failed: " + error;
    logger_.Error("webd", "apply job failed id=" + std::to_string(job_id) +
                              " error='" + status_.error + "'");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = "succeeded";
    status_.finished_at_utc = NowIso8601Utc();
    status_.error.clear();
  }

  logger_.Info("webd", "apply job succeeded id=" + std::to_string(job_id));
}

bool ApplyManager::RunCommand(const std::string& command, std::string* error) const {
  const int rc = std::system(command.c_str());
  if (rc == 0) {
    return true;
  }

  if (error == nullptr) {
    return false;
  }

  if (rc < 0) {
    *error = "system() failed";
    return false;
  }

  if (WIFEXITED(rc)) {
    *error = "exit code " + std::to_string(WEXITSTATUS(rc));
    return false;
  }

  if (WIFSIGNALED(rc)) {
    *error = "signal " + std::to_string(WTERMSIG(rc));
    return false;
  }

  *error = "unknown failure";
  return false;
}

}  // namespace chime::webd
