#include "chime/webd_wifi_scan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "chime/webd_string_utils.h"
#include "vc/config/kv_config.h"
#include "vc/logging/logger.h"

namespace chime::webd {
namespace {

constexpr int kScanTimeoutMs = 8000;
constexpr std::size_t kMaxCommandOutputBytes = 262144;
#if defined(__APPLE__)
constexpr const char *kAirportPath =
    "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport";
#endif

struct CommandResult {
    bool success = false;
    bool timed_out = false;
    int exit_code = -1;
    std::string output;
    std::string error;
};

std::string SecurityFromFlags(const std::string &flags);

CommandResult RunCommand(const std::vector<std::string> &args, int timeout_ms, std::size_t max_output_bytes) {
    CommandResult result;
    if (args.empty()) {
        result.error = "empty command";
        return result;
    }

    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        result.error = "pipe failed: " + std::string(std::strerror(errno));
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        result.error = "fork failed: " + std::string(std::strerror(errno));
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return result;
    }

    if (pid == 0) {
        setpgid(0, 0);

        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        auto exec_with = [&](const std::string &executable) {
            std::vector<std::string> exec_args = args;
            exec_args[0] = executable;
            std::vector<char *> argv;
            argv.reserve(exec_args.size() + 1);
            for (auto &arg : exec_args) {
                argv.push_back(arg.data());
            }
            argv.push_back(nullptr);
            execv(executable.c_str(), argv.data());
        };

        if (args[0].find('/') != std::string::npos) {
            exec_with(args[0]);
        } else {
            constexpr const char *kLookupDirs[] = {"/usr/sbin", "/sbin", "/usr/bin", "/bin"};
            for (const char *directory : kLookupDirs) {
                const std::string candidate = std::string(directory) + "/" + args[0];
                if (access(candidate.c_str(), X_OK) == 0) {
                    exec_with(candidate);
                }
            }

            std::vector<std::string> exec_args = args;
            std::vector<char *> argv;
            argv.reserve(exec_args.size() + 1);
            for (auto &arg : exec_args) {
                argv.push_back(arg.data());
            }
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
        }

        _exit(127);
    }

    close(pipe_fds[1]);

    const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    int elapsed_ms = 0;
    constexpr int kPollStepMs = 100;

    while (true) {
        struct pollfd pfd{pipe_fds[0], POLLIN, 0};

        const int poll_rc = poll(&pfd, 1, kPollStepMs);
        if (poll_rc > 0 && (pfd.revents & POLLIN)) {
            std::array<char, 4096> buffer{};
            const ssize_t bytes = read(pipe_fds[0], buffer.data(), buffer.size());
            if (bytes > 0) {
                const std::size_t remaining =
                    max_output_bytes > result.output.size() ? (max_output_bytes - result.output.size()) : 0;
                const std::size_t to_copy = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytes));
                if (to_copy > 0) {
                    result.output.append(buffer.data(), to_copy);
                }
            }
        }

        int status = 0;
        const pid_t wait_rc = waitpid(pid, &status, WNOHANG);
        if (wait_rc == pid) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
                result.success = (result.exit_code == 0);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = 128 + WTERMSIG(status);
                result.success = false;
            }

            while (true) {
                std::array<char, 4096> buffer{};
                const ssize_t bytes = read(pipe_fds[0], buffer.data(), buffer.size());
                if (bytes <= 0) {
                    break;
                }
                const std::size_t remaining =
                    max_output_bytes > result.output.size() ? (max_output_bytes - result.output.size()) : 0;
                const std::size_t to_copy = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytes));
                if (to_copy > 0) {
                    result.output.append(buffer.data(), to_copy);
                }
            }

            close(pipe_fds[0]);
            return result;
        }

        elapsed_ms += kPollStepMs;
        if (elapsed_ms >= timeout_ms) {
            result.timed_out = true;
            result.error = "command timed out";
            kill(-pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(pipe_fds[0]);
            return result;
        }
    }
}

std::vector<std::string> SplitLines(const std::string &input) {
    std::vector<std::string> lines;
    std::istringstream stream(input);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> SplitTabs(const std::string &input) {
    std::vector<std::string> fields;
    std::string current;
    for (const char c : input) {
        if (c == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string OneLineOutput(std::string output, std::size_t max_chars) {
    for (char &ch : output) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    output = vc::config::trim(output);
    if (output.size() <= max_chars) {
        return output;
    }
    return output.substr(0, max_chars) + "...";
}

std::string DescribeCommandFailure(const std::string &command, const CommandResult &command_result) {
    std::string details = command + " failed";
    if (command_result.timed_out) {
        details += " (timed out)";
    } else if (command_result.exit_code >= 0) {
        details += " (exit=" + std::to_string(command_result.exit_code) + ")";
    }
    const std::string output = OneLineOutput(command_result.output, 180);
    if (!output.empty()) {
        details += ": " + output;
    } else if (!command_result.error.empty()) {
        details += ": " + command_result.error;
    }
    return details;
}

bool ContainsBusySignal(const std::string &output) {
    const std::string lowered = ToLower(output);
    return lowered.find("busy") != std::string::npos;
}

std::vector<WifiNetwork> ParseWpaCliScanResults(const std::string &output) {
    std::vector<WifiNetwork> networks;
    for (const auto &line : SplitLines(output)) {
        if (line.empty() || line.rfind("bssid", 0) == 0) {
            continue;
        }

        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 5) {
            continue;
        }

        int signal = -1000;
        if (!fields[2].empty()) {
            char *end = nullptr;
            const long parsed = std::strtol(fields[2].c_str(), &end, 10);
            if (end != nullptr && *end == '\0') {
                signal = static_cast<int>(parsed);
            }
        }

        WifiNetwork network;
        network.ssid = fields[4];
        network.signal_dbm = signal;
        network.security = SecurityFromFlags(fields[3]);
        if (!network.ssid.empty()) {
            networks.push_back(std::move(network));
        }
    }
    return networks;
}

std::string SecurityFromFlags(const std::string &flags) {
    if (flags.find("WPA3") != std::string::npos) {
        return "WPA3";
    }
    if (flags.find("WPA2") != std::string::npos || flags.find("RSN") != std::string::npos) {
        return "WPA2";
    }
    if (flags.find("WPA") != std::string::npos) {
        return "WPA";
    }
    if (flags.find("WEP") != std::string::npos) {
        return "WEP";
    }
    return "OPEN";
}

std::vector<WifiNetwork> DeduplicateStrongest(const std::vector<WifiNetwork> &input) {
    std::map<std::string, WifiNetwork> deduped;
    for (const auto &network : input) {
        if (network.ssid.empty()) {
            continue;
        }
        const auto it = deduped.find(network.ssid);
        if (it == deduped.end() || network.signal_dbm > it->second.signal_dbm) {
            deduped[network.ssid] = network;
        }
    }

    std::vector<WifiNetwork> output;
    output.reserve(deduped.size());
    for (const auto &[_, network] : deduped) {
        output.push_back(network);
    }

    std::sort(output.begin(), output.end(),
              [](const WifiNetwork &a, const WifiNetwork &b) { return a.signal_dbm > b.signal_dbm; });
    return output;
}

#if defined(__APPLE__)
std::string JoinFieldsFrom(const std::vector<std::string> &fields, std::size_t start_index) {
    if (start_index >= fields.size()) {
        return "";
    }

    std::string output;
    for (std::size_t i = start_index; i < fields.size(); ++i) {
        if (i > start_index) {
            output.push_back(' ');
        }
        output += fields[i];
    }
    return output;
}

WifiScanResult ScanWithAirport() {
    WifiScanResult result;

    const CommandResult scan_output = RunCommand({kAirportPath, "-s"}, kScanTimeoutMs, kMaxCommandOutputBytes);
    if (!scan_output.success) {
        result.error = "airport scan command failed";
        return result;
    }

    static const std::regex bssid_regex("([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}");

    std::vector<WifiNetwork> networks;
    for (const auto &raw_line : SplitLines(scan_output.output)) {
        const std::string line = vc::config::trim(raw_line);
        if (line.empty() || line.rfind("SSID", 0) == 0) {
            continue;
        }

        std::smatch match;
        if (!std::regex_search(line, match, bssid_regex)) {
            continue;
        }

        const std::string ssid = vc::config::trim(line.substr(0, match.position()));
        if (ssid.empty()) {
            continue;
        }

        const std::string remainder = vc::config::trim(line.substr(match.position() + match.length()));
        std::istringstream remainder_stream(remainder);
        std::vector<std::string> fields;
        std::string token;
        while (remainder_stream >> token) {
            fields.push_back(token);
        }
        if (fields.empty()) {
            continue;
        }

        int signal_dbm = -1000;
        char *end = nullptr;
        const long parsed = std::strtol(fields[0].c_str(), &end, 10);
        if (end != nullptr && *end == '\0') {
            signal_dbm = static_cast<int>(parsed);
        }

        const std::string security_field = JoinFieldsFrom(fields, 4);

        WifiNetwork network;
        network.ssid = ssid;
        network.signal_dbm = signal_dbm;
        network.security = SecurityFromFlags(security_field);
        networks.push_back(std::move(network));
    }

    result.networks = DeduplicateStrongest(networks);
    result.success = true;
    return result;
}
#endif

} // namespace

WifiScanner::WifiScanner(vc::logging::Logger &logger, std::string interface_name)
    : logger_(logger), interface_name_(std::move(interface_name)) {}

WifiScanResult WifiScanner::Scan() const {
#if defined(__APPLE__)
    WifiScanResult airport_result = ScanWithAirport();
    if (airport_result.success) {
        return airport_result;
    }
    logger_.Warn("webd", "airport scan failed, falling back to Linux scanners");
#endif

    WifiScanResult primary = ScanWithWpaCli();
    if (primary.success) {
        return primary;
    }

    logger_.Warn("webd", "wpa_cli scan failed, falling back to iw: " + primary.error);
    WifiScanResult fallback = ScanWithIw();
    if (fallback.success) {
        return fallback;
    }

    logger_.Warn("webd", "iw scan failed: " + fallback.error);
    WifiScanResult result;
    result.success = false;
    result.error = "wifi scan failed: " + primary.error + " | " + fallback.error;
    return result;
}

WifiScanResult WifiScanner::ScanWithWpaCli() const {
    WifiScanResult result;

    const CommandResult trigger =
        RunCommand({"wpa_cli", "-i", interface_name_, "scan"}, kScanTimeoutMs, kMaxCommandOutputBytes);
    const bool trigger_busy = !trigger.success && ContainsBusySignal(trigger.output);
    if (!trigger.success && !trigger_busy) {
        result.error = DescribeCommandFailure("wpa_cli scan", trigger);
        return result;
    }

    constexpr int kScanResultsAttempts = 6;
    constexpr useconds_t kScanResultsPollDelayUs = 750 * 1000;
    std::vector<WifiNetwork> networks;
    std::string last_scan_results_error;

    for (int attempt = 0; attempt < kScanResultsAttempts; ++attempt) {
        usleep(kScanResultsPollDelayUs);

        const CommandResult scan_results =
            RunCommand({"wpa_cli", "-i", interface_name_, "scan_results"}, kScanTimeoutMs, kMaxCommandOutputBytes);
        if (!scan_results.success) {
            last_scan_results_error = DescribeCommandFailure("wpa_cli scan_results", scan_results);
            continue;
        }

        networks = ParseWpaCliScanResults(scan_results.output);
        if (!networks.empty()) {
            break;
        }
    }

    if (networks.empty()) {
        if (!last_scan_results_error.empty()) {
            result.error = last_scan_results_error;
        } else if (trigger_busy) {
            result.error = "wpa_cli scan busy and no cached scan results available";
        } else {
            result.error = "wpa_cli returned no scan results";
        }
        return result;
    }

    result.networks = DeduplicateStrongest(networks);
    result.success = true;
    return result;
}

WifiScanResult WifiScanner::ScanWithIw() const {
    WifiScanResult result;
    const CommandResult iw_output =
        RunCommand({"iw", "dev", interface_name_, "scan"}, kScanTimeoutMs, kMaxCommandOutputBytes);
    if (!iw_output.success) {
        result.error = DescribeCommandFailure("iw scan", iw_output);
        return result;
    }

    std::vector<WifiNetwork> networks;
    WifiNetwork current;
    bool in_block = false;

    auto push_current = [&]() {
        if (in_block && !current.ssid.empty()) {
            if (current.security.empty()) {
                current.security = "OPEN";
            }
            networks.push_back(current);
        }
        current = WifiNetwork{};
        in_block = false;
    };

    for (const auto &line : SplitLines(iw_output.output)) {
        const std::string trimmed = vc::config::trim(line);
        if (trimmed.rfind("BSS ", 0) == 0) {
            push_current();
            in_block = true;
            continue;
        }

        if (!in_block) {
            continue;
        }

        if (trimmed.rfind("SSID:", 0) == 0) {
            current.ssid = vc::config::trim(trimmed.substr(5));
            continue;
        }

        if (trimmed.rfind("signal:", 0) == 0) {
            std::string value = vc::config::trim(trimmed.substr(7));
            const auto space = value.find(' ');
            if (space != std::string::npos) {
                value = value.substr(0, space);
            }
            char *end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (end != nullptr && *end == '\0') {
                current.signal_dbm = static_cast<int>(parsed);
            }
            continue;
        }

        if (trimmed.rfind("RSN:", 0) == 0) {
            current.security = "WPA2";
            continue;
        }

        if (trimmed.rfind("WPA:", 0) == 0) {
            current.security = "WPA";
            continue;
        }

        if (trimmed.find("WEP") != std::string::npos) {
            current.security = "WEP";
        }
    }

    push_current();

    result.networks = DeduplicateStrongest(networks);
    result.success = true;
    return result;
}

} // namespace chime::webd
