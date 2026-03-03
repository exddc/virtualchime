#ifndef CHIME_AUDIO_PLAYER_H
#define CHIME_AUDIO_PLAYER_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace vc::logging {
class Logger;
}

namespace chime {

class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;
  virtual void Play(const std::string& path, int volume_percent = 100) = 0;
  virtual bool IsPlaying() const = 0;
};

class AplayAudioPlayer final : public AudioPlayer {
 public:
  explicit AplayAudioPlayer(vc::logging::Logger& logger);
  ~AplayAudioPlayer() override;

  void Play(const std::string& path, int volume_percent = 100) override;
  bool IsPlaying() const override;

 private:
  vc::logging::Logger& logger_;
  std::atomic<bool> playing_{false};
  std::mutex playback_thread_mutex_;
  std::thread playback_thread_;
};

}  // namespace chime

#endif
