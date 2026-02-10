#ifndef CHIME_AUDIO_PLAYER_H
#define CHIME_AUDIO_PLAYER_H

#include <atomic>
#include <string>

namespace vc::logging {
class Logger;
}

namespace chime {

class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;
  virtual void Play(const std::string& path) = 0;
  virtual bool IsPlaying() const = 0;
};

class AplayAudioPlayer final : public AudioPlayer {
 public:
  explicit AplayAudioPlayer(vc::logging::Logger& logger);

  void Play(const std::string& path) override;
  bool IsPlaying() const override;

 private:
  vc::logging::Logger& logger_;
  std::atomic<bool> playing_{false};
};

}  // namespace chime

#endif
