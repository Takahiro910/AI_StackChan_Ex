#if defined(ENABLE_WAKEWORD)

#ifndef _WAKEWORD_H
#define _WAKEWORD_H

void clear_buff();
void wakeword_setup();
void wakeword_init();
int wakeword_regist();
int wakeword_compare();
void delete_mfcc(int idx);
int wakeword_count();   // 登録済みウェイクワード数
extern int mode;   // 0: none, <0: REGIST, >0: COMPARE

#define base_path "/spiffs"
#define filename_base "/wakeword"
#define REGISTER_MAX    (10)
// 誤検出対策のガード（platformio.ini の -D で上書き可）
#ifndef WAKEWORD_MIN_RMS
#define WAKEWORD_MIN_RMS        (300)   // 発話区間の RMS がこれ未満（遠い音・小さい音）は不一致扱い
#endif
#ifndef WAKEWORD_LEN_MIN_PCT
#define WAKEWORD_LEN_MIN_PCT    (60)    // 候補の長さがテンプレートの 60% 未満なら不一致（短いノイズ・キーボード音）
#endif
#ifndef WAKEWORD_LEN_MAX_PCT
#define WAKEWORD_LEN_MAX_PCT    (160)   // 候補の長さがテンプレートの 160% 超なら不一致（長い会話）
#endif
#ifndef DIST_THRESHOLD
#define DIST_THRESHOLD  (250)   // DTW 距離の一致閾値。platformio.ini の -DDIST_THRESHOLD=... で上書き可。小さいほど誤検出が減り、取りこぼしが増える
#endif

#endif //_WAKEWORD_H

#endif //ENABLE_WAKEWORD