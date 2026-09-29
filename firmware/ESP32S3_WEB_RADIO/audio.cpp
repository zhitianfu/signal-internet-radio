#include "audio.h"
#include "config.h"
#include "debug.h"
#include <Wire.h>
#include "es8311.h"
#include "touch.h"

// Singleton hook so the static lib callback can reach the instance.
static StreamPlayer* _g_self = nullptr;

// Codec handle kept at file scope so stream-rate events can re-clock it:
// I2S runs at the SOURCE sample rate (MCLK = 256*fs, I2S_MCLK_MULTIPLE_256),
// but es8311_codec_init() configures the ES8311 for 44.1 kHz only. A 48 kHz
// stream then plays with wrong internal dividers -> audible hiss/noise.
static es8311_handle_t g_es = nullptr;
static int g_esRate = 0;

// ------------------------------------------------------------------
// ES8311 codec init (Waveshare 1.75C: I2C SDA=15/SCL=14, I2S MCLK=16
// BCLK=9 WS=45 DOUT=8, PA=46). Codec volume fixed; the 0..100 software
// volume lives in ESP32-audioI2S.
// ------------------------------------------------------------------
static void es8311_codec_init(void) {
  /* shared bus: use the pins the touch probe resolved (may be swapped) */
  Wire.begin(gI2cSda >= 0 ? gI2cSda : TOUCH_SDA,
             gI2cScl >= 0 ? gI2cScl : TOUCH_SCL);
  pinMode(PA, OUTPUT);
  digitalWrite(PA, HIGH);   /* power amplifier enable */

  g_es = es8311_create(0, ES8311_ADDRRES_0);
  if (!g_es) {
    LOG_E("audio", "es8311 create failed");
    return;
  }
  const uint32_t sample_rate = 44100;
  const es8311_clock_config_t es_clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = sample_rate * 256,
    .sample_frequency = sample_rate
  };
  es8311_init(g_es, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  es8311_sample_frequency_config(g_es, es_clk.mclk_frequency, es_clk.sample_frequency);
  es8311_microphone_config(g_es, false);
  es8311_voice_volume_set(g_es, 85, NULL);   /* 0 - 100 */
  g_esRate = sample_rate;
  LOG_I("audio", "ES8311 codec ready");
}

// ------------------------------------------------------------------
// StreamPlayer
// ------------------------------------------------------------------
StreamPlayer::StreamPlayer(int bclk, int lrc, int dout)
  : _bclk(bclk), _lrc(lrc), _dout(dout) {}

void StreamPlayer::begin() {
  es8311_codec_init();
  audio.setPinout(_bclk, _lrc, _dout, PIN_ES8311_MCLK);
  /* I2S follows each stream's source rate; per-stream ES8311 re-clock happens
   * on the "SampleRate (Hz):" event (see handleEvent). Do NOT pin output to a
   * fixed rate here — the library's resampler made the noise floor worse. */
  audio.setVolumeSteps(100);                       // 0..100 software volume
  audio.setConnectionTimeout(STREAM_CONNECT_TIMEOUT_MS,
                             STREAM_CONNECT_TIMEOUT_SSL_MS);
  audio.setAudioTaskCore(0);                       // I2S pump on core 0
  audio.setVolume(_volume);

  _cmdMutex = xSemaphoreCreateMutex();
  _metaMutex = xSemaphoreCreateMutex();

  // Route the library's info callback into this instance. The callback
  // fires inside audio.loop() = our audio task context.
  _g_self = this;
  Audio::audio_info_callback = StreamPlayer::audioEventCallback;

  xTaskCreatePinnedToCore(StreamPlayer::audioTaskEntry, "audio_task",
                          8192, this, 5, nullptr, 0);
  LOG_I("audio", "streaming engine ready (task on core 0, prio 5)");
}

// ------------------------------------------------------------------
// Public API (safe from any task)
// ------------------------------------------------------------------
void StreamPlayer::startStream(const char* url) {
  if (!url || !*url) return;
  LOG_I("audio", "startStream: %s", url);
  xSemaphoreTake(_cmdMutex, portMAX_DELAY);
  _pendingUrl = url;
  _pendingStart = true;
  _pendingStop = false;
  _manualStop = false;
  xSemaphoreGive(_cmdMutex);
}

void StreamPlayer::stopStream() {
  LOG_I("audio", "stopStream");
  xSemaphoreTake(_cmdMutex, portMAX_DELAY);
  _pendingStop = true;
  _pendingStart = false;
  _manualStop = true;
  xSemaphoreGive(_cmdMutex);
}

void StreamPlayer::setVolume(uint8_t vol) {
  uint8_t v = constrain(vol, 0, 100);
  if (v != _volume) {            // log only on change (slider drags are chatty)
    _volume = v;
    audio.setVolume(_volume);    // safe: just stores the value
    LOG_I("audio", "volume %u%%", _volume);
  }
}

void StreamPlayer::increaseVolume() { setVolume(_volume + 5); }
void StreamPlayer::decreaseVolume() { setVolume(_volume - 5); }

void StreamPlayer::setMute(bool m) {
  _muted = m;
  audio.setMute(m);
  LOG_I("audio", "mute %s", m ? "ON" : "OFF");
}

void StreamPlayer::toggleMute() { setMute(!_muted); }

void StreamPlayer::getMetadata(char* buffer, size_t len) {
  if (!buffer || len == 0) return;
  xSemaphoreTake(_metaMutex, portMAX_DELAY);
  strncpy(buffer, _metadata.c_str(), len - 1);
  buffer[len - 1] = '\0';
  xSemaphoreGive(_metaMutex);
}

const char* StreamPlayer::stateName() {
  switch (_state) {
    case StreamState::IDLE:         return "IDLE";
    case StreamState::CONNECTING:   return "CONNECTING";
    case StreamState::PLAYING:      return "PLAYING";
    case StreamState::RECONNECTING: return "RECONNECTING";
  }
  return "?";
}

// ------------------------------------------------------------------
// Audio task
// ------------------------------------------------------------------
void StreamPlayer::audioTaskEntry(void* param) {
  StreamPlayer* self = static_cast<StreamPlayer*>(param);
  for (;;) {
    self->loop();
    delay(1);
  }
}

void StreamPlayer::loop() {
  // 1. Consume commands from the UI task
  if (_pendingStart) {
    xSemaphoreTake(_cmdMutex, portMAX_DELAY);
    String url = _pendingUrl;
    _pendingStart = false;
    xSemaphoreGive(_cmdMutex);
    doStart(url);
  }
  if (_pendingStop) {
    xSemaphoreTake(_cmdMutex, portMAX_DELAY);
    _pendingStop = false;
    xSemaphoreGive(_cmdMutex);
    doStop();
  }

  // 2. Pump the library (network read + decode)
  audio.loop();

  /* every 2 s while playing: input-buffer fill telemetry. Buffer emptying
   * (free ≈ max) around audible stalls = network starvation; a steady
   * mid/high fill with chop points at the decoder/clock side instead. */
  static uint32_t bt = 0;
  if (_state == StreamState::PLAYING && millis() - bt > 2000) {
    bt = millis();
    Serial.printf("[buff] free=%u\n", audio.inBufferFree());
  }

  // 3. State tracking + auto-reconnect with backoff
  if (!_manualStop && _streamUrl.length() > 0) {
    if (audio.isRunning()) {
      if (_state != StreamState::PLAYING) {
        LOG_I("audio", "stream is playing");
        _state = StreamState::PLAYING;
      }
      _reconnectDelayMs = RECONNECT_BASE_DELAY_MS;   // reset backoff
    } else if (millis() - _lastAttemptMs >= _reconnectDelayMs) {
      _lastAttemptMs = millis();
      _state = StreamState::CONNECTING;
      LOG_W("audio", "connecting/retrying: %s (delay %u ms)",
            _streamUrl.c_str(), _reconnectDelayMs);
      if (!audio.connecttohost(_streamUrl.c_str())) {
        _state = StreamState::RECONNECTING;
        _reconnectDelayMs = _reconnectDelayMs * 2;
        if (_reconnectDelayMs > RECONNECT_MAX_DELAY_MS)
          _reconnectDelayMs = RECONNECT_MAX_DELAY_MS;
      }
    }
  }
}

// ------------------------------------------------------------------
// Task-context operations (safe to touch the Audio library here)
// ------------------------------------------------------------------
void StreamPlayer::doStart(const String& url) {
  audio.stopSong();                    // release any current resources
  _streamUrl = url;
  _lastAttemptMs = 0;                  // attempt immediately
  _reconnectDelayMs = RECONNECT_BASE_DELAY_MS;
  _state = StreamState::CONNECTING;
  xSemaphoreTake(_metaMutex, portMAX_DELAY);
  _metadata = "";
  xSemaphoreGive(_metaMutex);
  LOG_I("audio", "starting stream: %s", url.c_str());
}

void StreamPlayer::doStop() {
  audio.stopSong();
  _streamUrl = "";
  _state = StreamState::IDLE;
  LOG_I("audio", "stream stopped");
}

// ------------------------------------------------------------------
// Library info callback (audio task context)
// ------------------------------------------------------------------
void StreamPlayer::audioEventCallback(Audio::msg_t m) {
  if (_g_self) _g_self->handleEvent(m);
}

void StreamPlayer::handleEvent(Audio::msg_t m) {
  switch (m.e) {
    case Audio::evt_streamtitle:
      if (m.msg) {
        String title = String(m.msg);
        // Filter the library's noise strings
        if (title.startsWith("HTTP") || title.length() == 0) {
          xSemaphoreTake(_metaMutex, portMAX_DELAY);
          _metadata = "";
          xSemaphoreGive(_metaMutex);
        } else {
          xSemaphoreTake(_metaMutex, portMAX_DELAY);
          _metadata = title;
          xSemaphoreGive(_metaMutex);
        }
        LOG_I("audio", "metadata: %s", m.msg);
      }
      break;

    case Audio::evt_info:
      if (m.msg) {
        const char* s = m.msg;
        if (strstr(s, "established") != nullptr) {
          _state = StreamState::PLAYING;
          LOG_I("audio", "connection established");
        } else if (strstr(s, "disconnect") != nullptr) {
          if (_state == StreamState::PLAYING) {
            _state = StreamState::RECONNECTING;
            LOG_W("audio", "stream disconnected");
          }
        } else if (strstr(s, "SampleRate (Hz):") != nullptr) {
          /* I2S follows the SOURCE rate (no resampler — the library's
           * resampleI2Soutput() made the floor worse), so re-clock the
           * ES8311 per stream: MCLK = 256*fs matches I2S_MCLK_MULTIPLE_256.
           * String: "SampleRate (Hz): <n>" (16 chars before the value). */
          int r = atoi(s + 16);
          if (g_es && r >= 8000 && r <= 192000 && r != g_esRate) {
            g_esRate = r;
            esp_err_t e = es8311_sample_frequency_config(g_es, r * 256, r);
            Serial.printf("[audio] codec fs=%d Hz MCLK=%d %s\\n", r, r * 256,
                          e == ESP_OK ? "OK" : "FAIL");
          }
        }
        Serial.printf("[info] %.70s\n", s);
      }
      break;

    case Audio::evt_eof:
      LOG_W("audio", "stream ended (eof)");
      break;

    default:
      break;
  }
}
