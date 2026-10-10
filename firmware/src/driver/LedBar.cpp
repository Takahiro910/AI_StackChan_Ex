#if defined(USE_LED_BAR)

#include <Arduino.h>
#include <math.h>
#include <driver/rmt.h>
#include "LedBar.h"

// M5Stack Core2 for AWS 側面 LED バー（SK6812 x10, DIN=GPIO25）を ESP-IDF の RMT で直接駆動する。
// FastLED の RMT ドライバは割り込みで RMT メモリを逐次補充するため、Wi-Fi/TLS の割り込み負荷下で
// ビットタイミングが崩れ「白く点いて強弱がある」状態になった。ここでは RMT のメモリブロックを 4 個
// （64 item x 4 = 256 item）確保し、1 フレーム分（10 LED x 24 bit + リセット = 241 item）を丸ごと
// 載せてハードウェアに送らせる。送信中に CPU が介在しないので割り込み遅延の影響を受けない。

#ifndef LED_BAR_PIN
#define LED_BAR_PIN 25
#endif
#ifndef LED_BAR_NUM
#define LED_BAR_NUM 10
#endif
#ifndef LED_BAR_BRIGHTNESS
#define LED_BAR_BRIGHTNESS 24   // 0-255。側面 LED は明るいので控えめに
#endif
#ifndef LED_BAR_RMT_CHANNEL
#define LED_BAR_RMT_CHANNEL RMT_CHANNEL_0
#endif
// SK6812 が RGBW（4byte/LED）だった場合は -DLED_BAR_RGBW を定義する
#ifdef LED_BAR_RGBW
#define LED_BAR_BYTES_PER_LED 4
#else
#define LED_BAR_BYTES_PER_LED 3
#endif

namespace {
  struct Rgb { uint8_t r, g, b; };

  Rgb leds[LED_BAR_NUM];
  volatile LedBar::Mode currentMode = LedBar::Off;
  volatile LedBar::Mode modeBeforeWake = LedBar::Off;
  volatile int currentLevel = 0;
  volatile uint32_t wakeUntilMs = 0;
  volatile uint32_t frameCount = 0;
  TaskHandle_t taskHandle = nullptr;

  // RMT: clk_div=2 → 40MHz → 1 tick = 25ns
  // SK6812: T0H 0.30us / T0L 0.90us, T1H 0.60us / T1L 0.60us, RESET >= 80us
  constexpr uint32_t kT0H = 12, kT0L = 36, kT1H = 24, kT1L = 24, kReset = 3200;
  constexpr int kBitsPerFrame = LED_BAR_NUM * LED_BAR_BYTES_PER_LED * 8;
  constexpr int kItems = kBitsPerFrame + 1;              // + リセット
  constexpr int kMemBlocks = (kItems + 63) / 64;          // 10 LED x 3 byte → 241 item → 4 ブロック
  static_assert(kMemBlocks <= 8, "LED frame does not fit in RMT memory");
  rmt_item32_t items[kItems];
  bool rmtReady = false;

  inline rmt_item32_t bitItem(bool one)
  {
    rmt_item32_t it;
    it.level0 = 1; it.duration0 = one ? kT1H : kT0H;
    it.level1 = 0; it.duration1 = one ? kT1L : kT0L;
    return it;
  }

  void show()
  {
    if(!rmtReady) return;
    int n = 0;
    const uint32_t br = LED_BAR_BRIGHTNESS;
    for(int i = 0; i < LED_BAR_NUM; i++){
      // GRB(W) 順で送る
      uint8_t bytes[4] = {
        (uint8_t)(leds[i].g * br / 255),
        (uint8_t)(leds[i].r * br / 255),
        (uint8_t)(leds[i].b * br / 255),
        0 };
      for(int k = 0; k < LED_BAR_BYTES_PER_LED; k++){
        for(int bit = 7; bit >= 0; bit--){
          items[n++] = bitItem((bytes[k] >> bit) & 1);
        }
      }
    }
    rmt_item32_t rst; rst.level0 = 0; rst.duration0 = kReset; rst.level1 = 0; rst.duration1 = 0;
    items[n++] = rst;
    rmt_write_items(LED_BAR_RMT_CHANNEL, items, n, false);
    rmt_wait_tx_done(LED_BAR_RMT_CHANNEL, pdMS_TO_TICKS(50));
  }

  inline void fill(const Rgb& c) { for(int i = 0; i < LED_BAR_NUM; i++) leds[i] = c; }

  // 左右 5 個ずつの前提。index 0..4 を左、5..9 を右とみなし、各側 i=0 を中央寄りとして点灯する。
  // 実機の並びが逆に見える場合は -DLED_BAR_REVERSE で反転。
  inline int physIndex(int idx)
  {
#ifdef LED_BAR_REVERSE
    return LED_BAR_NUM - 1 - idx;
#else
    return idx;
#endif
  }

  void render()
  {
    const uint32_t now = millis();
    LedBar::Mode mode = currentMode;

    if(mode == LedBar::Wake){
      if((int32_t)(wakeUntilMs - now) <= 0){
        currentMode = modeBeforeWake;
        mode = currentMode;
      }else{
        fill(Rgb{180, 180, 255});
        show();
        return;
      }
    }

    switch(mode){
    case LedBar::Listening: {
      // 2 秒周期の呼吸（青）
      float phase = (now % 2000) / 2000.0f;
      float b = 0.35f + 0.65f * (0.5f - 0.5f * cosf(phase * 2.0f * PI));
      fill(Rgb{0, (uint8_t)(90 * b), (uint8_t)(255 * b)});
      break;
    }
    case LedBar::Speaking: {
      // 音量 0..15000 → 片側 0..5 個。立ち上がりは即、減衰はなめらかに
      static float smoothed = 0;
      int lv = currentLevel;
      if(lv < 0) lv = 0;
      if(lv > 15000) lv = 15000;
      float target = lv / 15000.0f;
      smoothed = (target > smoothed) ? target : smoothed * 0.80f + target * 0.20f;
      const int half = LED_BAR_NUM / 2;
      int count = (int)roundf(smoothed * half);
      for(int i = 0; i < half; i++){
        Rgb color = (i < count) ? Rgb{255, (uint8_t)(90 + 30 * i), (uint8_t)(40 + 40 * i)} : Rgb{0, 0, 0};
        leds[physIndex(half - 1 - i)] = color;   // 左側: 中央寄りから外へ
        leds[physIndex(half + i)] = color;       // 右側
      }
      break;
    }
    case LedBar::Off:
    default:
      fill(Rgb{0, 0, 0});
      break;
    }
    show();
  }

  void ledTask(void*)
  {
    for(;;){
      render();
      frameCount = frameCount + 1;
      vTaskDelay(pdMS_TO_TICKS(40));
    }
  }

  const char* modeName(LedBar::Mode m)
  {
    switch(m){ case LedBar::Off: return "Off"; case LedBar::Listening: return "Listening"; case LedBar::Speaking: return "Speaking"; case LedBar::Wake: return "Wake"; }
    return "?";
  }
}

namespace LedBar {

void begin()
{
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)LED_BAR_PIN, LED_BAR_RMT_CHANNEL);
  cfg.clk_div = 2;                 // 80MHz / 2 = 40MHz
  cfg.mem_block_num = kMemBlocks;  // 1 フレーム丸ごと載せる（割り込みによる補充なし）
  cfg.tx_config.loop_en = false;
  cfg.tx_config.carrier_en = false;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  esp_err_t err = rmt_config(&cfg);
  if(err == ESP_OK) err = rmt_driver_install(LED_BAR_RMT_CHANNEL, 0, 0);
  if(err != ESP_OK){
    Serial.printf("LedBar: RMT init failed (%d)\n", (int)err);
    return;
  }
  rmtReady = true;
  fill(Rgb{0, 0, 0});
  show();
  // スピーカー(APP_CPU, 優先度5)の邪魔をしないよう PRO_CPU・低優先度で回す
  xTaskCreatePinnedToCore(ledTask, "ledBar", 3 * 1024, nullptr, 1, &taskHandle, PRO_CPU_NUM);
  Serial.printf("LedBar: %d LEDs on GPIO%d via RMT ch%d (%d mem blocks, %d bytes/LED)\n",
                LED_BAR_NUM, LED_BAR_PIN, (int)LED_BAR_RMT_CHANNEL, kMemBlocks, LED_BAR_BYTES_PER_LED);
}

void setMode(Mode mode)
{
  if(currentMode == Wake){ modeBeforeWake = mode; return; }
  if(currentMode != mode){
    Serial.printf("[LedBar] %s -> %s\n", modeName(currentMode), modeName(mode));   // 状態遷移の切り分け用
    currentMode = mode;
  }
}

void setLevel(int level) { currentLevel = level; }

uint32_t frames() { return frameCount; }

void flashWake()
{
  if(currentMode != Wake) modeBeforeWake = currentMode;
  wakeUntilMs = millis() + 250;
  currentMode = Wake;
}

} // namespace LedBar

#endif // USE_LED_BAR
