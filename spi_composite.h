#include <stdint.h>

#define X_RES	320 // Graphic横方向解像度
#define Y_RES	216 // Graphic縦方向解像度

extern uint16_t VRAM[]; // Graphicビデオメモリ
extern volatile int drawcount;	//　1画面表示終了ごとに1足す。アプリ側で0にする。
					// 最低1回は画面表示したことのチェックと、アプリの処理が何画面期間必要かの確認に利用。
extern volatile int drawing;		//　映像区間処理中は-1、その他は0

// カラーパレット設定
// c:パレット番号（0-1023）
// ib,ir,ig:BRG各色の明るさ（0-255）
void set_palette(uint16_t c, uint8_t ib, uint8_t ir, uint8_t ig);

// コンポジット出力開始または再開
void start_composite(void);

// コンポジット出力停止
void stop_composite(void);

// コンポジット初期化
void init_composite(void);
