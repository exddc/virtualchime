#ifndef CHIME_WEBD_MDNS_H
#define CHIME_WEBD_MDNS_H

#include <atomic>
#include <string>
#include <thread>

namespace vc::logging {
class Logger;
}

namespace chime::webd {

class MdnsResponder {
 public:
  MdnsResponder(vc::logging::Logger& logger, std::string host_label,
                std::string interface_name);
  ~MdnsResponder();

  MdnsResponder(const MdnsResponder&) = delete;
  MdnsResponder& operator=(const MdnsResponder&) = delete;

  bool Start();
  void Stop();

 private:
  void Run();

  vc::logging::Logger& logger_;
  std::string host_label_;
  std::string interface_name_;

  std::atomic<bool> running_{false};
  int socket_fd_ = -1;
  std::thread thread_;
};

}  // namespace chime::webd

#endif
