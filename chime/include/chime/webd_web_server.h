#ifndef CHIME_WEBD_WEB_SERVER_H
#define CHIME_WEBD_WEB_SERVER_H

#include <atomic>
#include <optional>
#include <string>
#include <thread>

#include "chime/webd_types.h"

namespace vc::logging {
class Logger;
}

namespace chime::webd {

class ApplyManager;
class ConfigStore;
class WifiScanner;

class WebServer {
 public:
  WebServer(vc::logging::Logger& logger, ConfigStore& config_store,
            WifiScanner& wifi_scanner, ApplyManager& apply_manager,
            std::string bind_address, int port, std::string cert_path,
            std::string key_path, std::string ui_dist_dir,
            std::string observed_topics_path, std::string ring_sounds_dir,
            std::string active_ring_sound_path);
  ~WebServer();

  WebServer(const WebServer&) = delete;
  WebServer& operator=(const WebServer&) = delete;

  bool Start();
  void Stop();

 private:
  struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::string content_type;
    bool has_content_type = false;
  };

  struct HttpResponse {
    int status = 500;
    std::string content_type = "application/json; charset=utf-8";
    std::string cache_control = "no-store";
    std::string body = "{\"error\":\"internal\"}";
  };

  void AcceptLoop();
  void HandleConnection(int client_fd);
  bool ReadHttpRequest(void* ssl, HttpRequest* request, std::string* error) const;
  HttpResponse Route(const HttpRequest& request);

  HttpResponse HandleGetCoreConfig();
  HttpResponse HandlePostCoreConfig(const HttpRequest& request);
  HttpResponse HandleWifiScan();
  HttpResponse HandleGetObservedTopics();
  HttpResponse HandleGetRingSounds();
  HttpResponse HandleUploadRingSound(const HttpRequest& request);
  HttpResponse HandleSelectRingSound(const HttpRequest& request);
  HttpResponse ReservedNotImplemented(const std::string& path) const;
  std::optional<HttpResponse> TryServeExternalUi(
      const HttpRequest& request) const;

  bool EnsureTlsMaterial(std::string* error) const;

  vc::logging::Logger& logger_;
  ConfigStore& config_store_;
  WifiScanner& wifi_scanner_;
  ApplyManager& apply_manager_;

  std::string bind_address_;
  int port_ = 8443;
  std::string cert_path_;
  std::string key_path_;
  std::string ui_dist_dir_;
  std::string observed_topics_path_;
  std::string ring_sounds_dir_;
  std::string active_ring_sound_path_;

  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  void* ssl_ctx_ = nullptr;
  std::thread accept_thread_;
};

}  // namespace chime::webd

#endif
