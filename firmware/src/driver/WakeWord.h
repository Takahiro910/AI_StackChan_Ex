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
#ifndef DIST_THRESHOLD
#define DIST_THRESHOLD  (250)   // DTW 距離の一致閾値。platformio.ini の -DDIST_THRESHOLD=... で上書き可。小さいほど誤検出が減り、取りこぼしが増える
#endif

#endif //_WAKEWORD_H

#endif //ENABLE_WAKEWORD