#pragma once
/* Audio module: ES8311 codec + ESP32-audioI2S streaming engine.
 * (Class name kept as StreamPlayer — it wraps the lib's "Audio" class.)
 *
 * THREADING MODEL
 * --------------
 * ESP32-audioI2S does the heavy lifting (HTTP + decode) inside
 * Audio::loop(), and runs its own low-priority task for I2S output.
 * Audio::loop() MUST be called regularly and MUST NOT be called from
 * two tasks at once, so this wrapper owns a dedicated FreeRTOS task
 * ("audio_task", core 0, prio 5) that pumps loop() + reconnect logic.
 * The UI task (Arduino loop, core 1) only sets command flags
 * (startStream/stopStream) and reads state/metadata — it never calls
 * into the Audio library directly, so there are no data races.
 *
 * RECONNECT
 * --------
 * If the stream drops (network error, server hiccup, timeout) the
 * wrapper retries automatically with exponential backoff:
 * base -> x2 -> x4 -> ... capped at RECONNECT_MAX_DELAY_MS. The backoff
 * resets after a successful connection. The UI can show the state via
 * getState()/stateName().
 */

#include <Arduino.h>
#include "Audio.h"

enum class StreamState : uint8_t {
  IDLE,         // no stream requested
  CONNECTING,   // trying to establish / (re)connect
  PLAYING,      // audio is decoding and playing
  RECONNECTING, // stream dropped, retrying with backoff
};

class StreamPlayer {
public:
  StreamPlayer(int bclk, int lrc, int dout);

  /* Init ES8311 codec + PA + I2S, start the audio task. */
  void begin();

  /* --- Prompt 2 API ------------------------------------------------- */
  void startStream(const char* url);            // begin playback (async)
  void stopStream();                            // stop + release resources
  void setVolume(uint8_t vol);                  // 0..100 (software volume)
  uint8_t getVolume() { return _volume; }       // 0..100
  void getMetadata(char* buffer, size_t len);   // "artist - title" or ""
  bool isPlaying() { return _state == StreamState::PLAYING; }

  /* Convenience / extras */
  void increaseVolume();                        // +5
  void decreaseVolume();                        // -5
  void setMute(bool m);
  void toggleMute();
  bool isMuted() { return _muted; }
  StreamState getState() { return _state; }
  const char* stateName();
  bool hasStream() { return _streamUrl.length() > 0; }
  uint32_t getBufferLevel() { return audio.inBufferFilled(); } // UI indicator

  /* Called by the audio task every iteration (not by the UI loop). */
  void loop();

private:
  static void audioTaskEntry(void* param);      // FreeRTOS task body
  static void audioEventCallback(Audio::msg_t m); // lib info callback
  void handleEvent(Audio::msg_t m);             // event -> state/metadata

  void doStart(const String& url);              // task context: real work
  void doStop();                                // task context: real work

  Audio audio;
  int _bclk, _lrc, _dout;

  // ---- owned by the audio task only ----
  String _streamUrl;          // current target URL
  StreamState _state = StreamState::IDLE;
  uint32_t _lastAttemptMs = 0;
  uint32_t _reconnectDelayMs = 0;
  bool _manualStop = false;

  // ---- written by UI task, consumed by audio task ----
  SemaphoreHandle_t _cmdMutex = nullptr;
  String _pendingUrl;
  bool _pendingStart = false;
  bool _pendingStop = false;

  // ---- metadata (written in task, read from UI) ----
  SemaphoreHandle_t _metaMutex = nullptr;
  String _metadata;

  uint8_t _volume = 85;       // 0..100
  bool _muted = false;
};
