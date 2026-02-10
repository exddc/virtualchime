#include "vc/runtime/signal_handler.h"

namespace vc::runtime {
namespace {
volatile std::sig_atomic_t g_should_stop = 0;
volatile std::sig_atomic_t g_last_signal = 0;
}  // namespace

void SignalHandler::Install() {
  std::signal(SIGINT, SignalHandler::Handle);
  std::signal(SIGTERM, SignalHandler::Handle);
}

bool SignalHandler::ShouldStop() const { return g_should_stop != 0; }

int SignalHandler::LastSignal() const { return g_last_signal; }

std::string SignalHandler::SignalName(int signal) {
  switch (signal) {
    case SIGINT:
      return "SIGINT";
    case SIGTERM:
      return "SIGTERM";
    default:
      return std::to_string(signal);
  }
}

void SignalHandler::Handle(int signal) {
  g_last_signal = signal;
  g_should_stop = 1;
}

}  // namespace vc::runtime
