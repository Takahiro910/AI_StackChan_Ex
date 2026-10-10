#if defined(USE_LED_BAR)

#ifndef _LED_BAR_H
#define _LED_BAR_H

#include <Arduino.h>

// M5Stack Core2 for AWS の側面 LED バー（SK6812 x10, GPIO25）を会話状態に連動させる。
//  - Off      : 消灯
//  - Listening: 青のゆっくりした明滅（聞き取り中）
//  - Speaking : 音量に応じて中央から外側へ伸びる VU メーター（口パク連動）
//  - Wake     : ウェイクワード検出時の一瞬のフラッシュ（自動で直前のモードへ戻る）
// ピンと個数は -DLED_BAR_PIN / -DLED_BAR_NUM で変更できる。
namespace LedBar {
  enum Mode { Off = 0, Listening, Speaking, Wake };

  void begin();                 // FastLED 初期化と描画タスク起動（USE_LED_BAR 定義時のみ有効）
  void setMode(Mode mode);
  void setLevel(int level);     // Speaking 時の音量 (0..15000, RealtimeLLMBase::getAudioLevel() と同スケール)
  void flashWake();             // ウェイクワード検出の合図
  uint32_t frames();            // 描画フレーム数（タスク生存確認用）
}

#endif // _LED_BAR_H

#endif // USE_LED_BAR
