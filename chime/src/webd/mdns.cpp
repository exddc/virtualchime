#include "chime/webd_mdns.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "vc/logging/logger.h"

namespace chime::webd {
namespace {

constexpr uint16_t kMdnsPort = 5353;
constexpr const char* kMdnsGroup = "224.0.0.251";
constexpr uint32_t kDnsTypeA = 1;
constexpr uint32_t kDnsTypeAny = 255;

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool GetInterfaceIPv4(const std::string& interface_name, struct in_addr* address,
                      std::string* error) {
  if (address == nullptr) {
    if (error != nullptr) {
      *error = "address pointer is null";
    }
    return false;
  }

  struct ifaddrs* addrs = nullptr;
  if (getifaddrs(&addrs) != 0) {
    if (error != nullptr) {
      *error = "getifaddrs failed";
    }
    return false;
  }

  bool found = false;
  struct in_addr fallback{};
  bool fallback_set = false;

  for (struct ifaddrs* it = addrs; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if ((it->ifa_flags & IFF_LOOPBACK) != 0) {
      continue;
    }

    const auto* ipv4 = reinterpret_cast<const struct sockaddr_in*>(it->ifa_addr);
    if (!fallback_set) {
      fallback = ipv4->sin_addr;
      fallback_set = true;
    }

    if (!interface_name.empty() && interface_name != it->ifa_name) {
      continue;
    }

    *address = ipv4->sin_addr;
    found = true;
    break;
  }

  if (!found && fallback_set) {
    *address = fallback;
    found = true;
  }

  freeifaddrs(addrs);

  if (!found && error != nullptr) {
    *error = "no non-loopback IPv4 address available";
  }

  return found;
}

std::vector<uint8_t> EncodeDnsName(const std::string& fqdn) {
  std::vector<uint8_t> output;
  std::size_t start = 0;
  while (start < fqdn.size()) {
    const std::size_t dot = fqdn.find('.', start);
    const std::size_t end = (dot == std::string::npos) ? fqdn.size() : dot;
    const std::size_t length = end - start;
    if (length == 0 || length > 63) {
      return {};
    }
    output.push_back(static_cast<uint8_t>(length));
    for (std::size_t i = start; i < end; ++i) {
      output.push_back(static_cast<uint8_t>(fqdn[i]));
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  output.push_back(0);
  return output;
}

bool DecodeDnsName(const std::vector<uint8_t>& packet, std::size_t start,
                   std::string* output, std::size_t* consumed,
                   int depth = 0) {
  if (output == nullptr || consumed == nullptr) {
    return false;
  }
  if (depth > 8) {
    return false;
  }

  std::size_t offset = start;
  std::size_t local_consumed = 0;
  bool jumped = false;
  std::string name;

  while (offset < packet.size()) {
    const uint8_t length = packet[offset];
    if (length == 0) {
      if (!jumped) {
        ++local_consumed;
      }
      break;
    }

    if ((length & 0xC0) == 0xC0) {
      if (offset + 1 >= packet.size()) {
        return false;
      }
      const uint16_t pointer =
          static_cast<uint16_t>((length & 0x3F) << 8) | packet[offset + 1];
      if (pointer >= packet.size()) {
        return false;
      }
      if (!jumped) {
        local_consumed += 2;
      }
      offset = pointer;
      jumped = true;
      ++depth;
      if (depth > 8) {
        return false;
      }
      continue;
    }

    if (offset + 1 + length > packet.size()) {
      return false;
    }

    if (!name.empty()) {
      name.push_back('.');
    }

    for (std::size_t i = 0; i < length; ++i) {
      name.push_back(static_cast<char>(packet[offset + 1 + i]));
    }

    if (!jumped) {
      local_consumed += 1 + length;
    }
    offset += 1 + length;
  }

  *output = name;
  *consumed = local_consumed;
  return true;
}

bool QueryRequestsHost(const std::vector<uint8_t>& packet,
                       const std::string& host_fqdn) {
  if (packet.size() < 12) {
    return false;
  }

  const uint16_t qdcount =
      static_cast<uint16_t>(packet[4] << 8) | static_cast<uint16_t>(packet[5]);

  std::size_t offset = 12;
  const std::string target = ToLower(host_fqdn);

  for (uint16_t i = 0; i < qdcount; ++i) {
    std::string name;
    std::size_t consumed = 0;
    if (!DecodeDnsName(packet, offset, &name, &consumed)) {
      return false;
    }
    offset += consumed;
    if (offset + 4 > packet.size()) {
      return false;
    }

    const uint16_t qtype =
        static_cast<uint16_t>(packet[offset] << 8) | packet[offset + 1];
    const uint16_t qclass =
        static_cast<uint16_t>(packet[offset + 2] << 8) | packet[offset + 3];
    offset += 4;

    const bool class_ok = ((qclass & 0x7FFF) == 1);
    const bool type_ok = (qtype == kDnsTypeA || qtype == kDnsTypeAny);

    if (class_ok && type_ok && ToLower(name) == target) {
      return true;
    }
  }

  return false;
}

std::vector<uint8_t> BuildAnswerPacket(const std::string& fqdn,
                                       const struct in_addr& ip) {
  const std::vector<uint8_t> name = EncodeDnsName(fqdn);
  if (name.empty()) {
    return {};
  }

  std::vector<uint8_t> packet;
  packet.reserve(12 + name.size() + 16);

  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x84);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x01);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);

  packet.insert(packet.end(), name.begin(), name.end());

  packet.push_back(0x00);
  packet.push_back(0x01);
  packet.push_back(0x00);
  packet.push_back(0x01);

  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x00);
  packet.push_back(0x78);

  packet.push_back(0x00);
  packet.push_back(0x04);

  const uint8_t* ip_bytes = reinterpret_cast<const uint8_t*>(&ip.s_addr);
  packet.push_back(ip_bytes[0]);
  packet.push_back(ip_bytes[1]);
  packet.push_back(ip_bytes[2]);
  packet.push_back(ip_bytes[3]);

  return packet;
}

}  // namespace

MdnsResponder::MdnsResponder(vc::logging::Logger& logger, std::string host_label,
                             std::string interface_name)
    : logger_(logger),
      host_label_(std::move(host_label)),
      interface_name_(std::move(interface_name)) {}

MdnsResponder::~MdnsResponder() { Stop(); }

bool MdnsResponder::Start() {
  if (running_.load()) {
    return true;
  }

  socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd_ < 0) {
    logger_.Error("webd", "mDNS socket() failed");
    return false;
  }

  const int reuse = 1;
  setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in bind_addr {};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(kMdnsPort);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&bind_addr),
           sizeof(bind_addr)) != 0) {
    logger_.Error("webd", "mDNS bind failed");
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

  struct ip_mreq mreq {};
  mreq.imr_multiaddr.s_addr = inet_addr(kMdnsGroup);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                 sizeof(mreq)) != 0) {
    logger_.Warn("webd", "mDNS multicast join failed");
  }

  const unsigned char ttl = 255;
  setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

  running_.store(true);
  thread_ = std::thread([this]() { Run(); });
  return true;
}

void MdnsResponder::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }

  if (thread_.joinable()) {
    thread_.join();
  }
}

void MdnsResponder::Run() {
  const std::string fqdn = host_label_ + ".local";

  logger_.Info("webd", "mDNS responder started for " + fqdn);

  auto last_announce =
      std::chrono::steady_clock::now() - std::chrono::seconds(120);

  while (running_.load()) {
    struct in_addr ip_addr {};
    std::string ip_error;
    const bool has_ip = GetInterfaceIPv4(interface_name_, &ip_addr, &ip_error);

    if (!has_ip) {
      logger_.Warn("webd", "mDNS skipped: " + ip_error);
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }

    const std::vector<uint8_t> answer = BuildAnswerPacket(fqdn, ip_addr);
    if (answer.empty()) {
      logger_.Warn("webd", "mDNS failed to build answer packet");
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_announce >= std::chrono::seconds(120)) {
      struct sockaddr_in multicast_addr {};
      multicast_addr.sin_family = AF_INET;
      multicast_addr.sin_port = htons(kMdnsPort);
      multicast_addr.sin_addr.s_addr = inet_addr(kMdnsGroup);

      sendto(socket_fd_, answer.data(), answer.size(), 0,
             reinterpret_cast<struct sockaddr*>(&multicast_addr),
             sizeof(multicast_addr));
      last_announce = now;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_fd_, &read_fds);

    struct timeval timeout {};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    const int select_rc = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
    if (select_rc <= 0) {
      continue;
    }

    std::array<uint8_t, 1500> request_buffer{};
    struct sockaddr_in source_addr {};
    socklen_t source_len = sizeof(source_addr);
    const ssize_t bytes = recvfrom(socket_fd_, request_buffer.data(),
                                   request_buffer.size(), 0,
                                   reinterpret_cast<struct sockaddr*>(&source_addr),
                                   &source_len);
    if (bytes <= 0) {
      continue;
    }

    std::vector<uint8_t> request(request_buffer.begin(),
                                 request_buffer.begin() + bytes);
    if (!QueryRequestsHost(request, fqdn)) {
      continue;
    }

    sendto(socket_fd_, answer.data(), answer.size(), 0,
           reinterpret_cast<struct sockaddr*>(&source_addr), source_len);
  }

  logger_.Info("webd", "mDNS responder stopped");
}

}  // namespace chime::webd
