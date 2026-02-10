#ifndef VC_MQTT_CLIENT_H
#define VC_MQTT_CLIENT_H

#include <string>

namespace vc::logging {
class Logger;
}

struct mosquitto;
struct mosquitto_message;

namespace vc::mqtt {

struct Message {
  std::string topic;
  std::string payload;
  int qos = 0;
  bool retain = false;
};

struct ConnectOptions {
  std::string client_id;
  int keepalive_seconds = 60;
  int reconnect_min_seconds = 2;
  int reconnect_max_seconds = 10;
  bool reconnect_exponential_backoff = true;
};

class EventHandler {
 public:
  virtual ~EventHandler() = default;
  virtual void OnConnect(int rc) = 0;
  virtual void OnDisconnect(int rc) = 0;
  virtual void OnMessage(const Message& message) = 0;
};

class Client {
 public:
  Client(vc::logging::Logger& logger, EventHandler& handler);
  ~Client();

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool Connect(const std::string& host, int port, const ConnectOptions& options);
  int Loop(int timeout_ms, int max_packets);
  bool Reconnect();
  bool Disconnect();
  bool Subscribe(const std::string& topic, int qos);
  bool Publish(const std::string& topic, const std::string& payload, int qos,
               bool retain);

  bool IsConnected() const;
  std::string LastError() const;

 private:
  static void HandleConnect(struct mosquitto* mosq, void* obj, int rc);
  static void HandleDisconnect(struct mosquitto* mosq, void* obj, int rc);
  static void HandleMessage(struct mosquitto* mosq, void* obj,
                            const struct mosquitto_message* msg);

  void SetLastError(const std::string& message);
  void DestroyClient();

  vc::logging::Logger& logger_;
  EventHandler& handler_;
  struct mosquitto* mosq_ = nullptr;
  bool connected_ = false;
  bool lib_ready_ = false;
  std::string last_error_;
};

}  // namespace vc::mqtt

#endif
