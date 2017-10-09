// SPI-COMPOSITE for PIC32MK1024GPD064 by K.Tanaka, inspired by Gombe
// SPI出力を4本使用したNTSCコンポジット信号出力

// SPI# DMAch# Port# Pin# Resistor
// SDO3 DMACH1 RB12 (62)  2K ohm
// SDO4 DMACH2 RB14 ( 2)  1K ohm
// SDO5 DMACH3 RB15 ( 3)  510 ohm
// SDO6 DMACH4 RB13 (63)  200 ohm

#include <xc.h>
#include <sys/attribs.h>
#include <stdint.h>
#include <math.h>
#include "spi_composite.h"

// パルス幅定数
#define V_NTSC		262					// 262本/画面
#define V_SYNC		10					// 垂直同期本数
#define V_PREEQ		26					// ブランキング区間上側
#define V_LINE		Y_RES				// 画像描画区間
#define H_NTSC		3648				// 水平1ライン　SPI転送bit数、約63.5μsec
#define H_SYNC		272					// 水平同期幅　SPI転送bit数、約4.7μsec
#define H_CBST		304					// カラーバースト開始位置（水平同期立下りから）
#define H_BACK		496					// 左スペース（水平同期立ち上がりから）

#define DMA_TRANSLATION_SIZE 12			// 1回のDMA転送バイト数
#define LINE_BUFFER_SIZE (H_NTSC/8)		// 水平1ライン、1ビット分のバッファ容量
#define NUM_OF_BUFFERS 2				// DMAバッファのライン数
#define BUFFER_SIZE (NUM_OF_BUFFERS * LINE_BUFFER_SIZE) //DMAバッファ1ビット分の容量
#define DA_BITNUM 4						// DAコンバータービット数
#define _CLOCK_DIVIDE 16

volatile int LineCount;	// DMAバッファリング中の行
volatile int drawcount;	// 1画面表示終了ごとに1足す。アプリ側で0にする。
volatile int drawing;	//　映像区間処理中は-1、その他は0
uint16_t *VRAMp; //処理中VRAMアドレス

/*
 * PIC32MKのRAMは2つのバンクに分かれており、CPUとDMAの同時アクセスによるバスの衝突を減らすことができる
 * バンク1 0xA000 0000 - 0xA001 FFFF
 * バンク2 0xA002 0000 - 0xA003 FFFF
 * 通常データをバンク1、DMAバッファをバンク2に割り当てると高速化する場合あり
 */

//uint16_t VRAM[X_RES*Y_RES]  __attribute__((address(0xa0004000), space(data))) __attribute__ ((aligned (4)));
uint16_t VRAM[X_RES*Y_RES] __attribute__ ((aligned (4)));

//uint32_t colordata[2*1024]  __attribute__((address(0xa0002000), space(data))); // パレットデータ
uint32_t colordata[2*1024]; // パレットデータ

//uint8_t chdata[BUFFER_SIZE*DA_BITNUM]  __attribute__((address(0xa003e000), space(data))) __attribute__ ((aligned (4)));
uint8_t chdata[BUFFER_SIZE*DA_BITNUM]  __attribute__ ((aligned (4)));

// 垂直同期期間用バッファデータ作成
void VSYNC(uint8_t *dmabuf) {
	int i;
	uint32_t *p;
	p=(uint32_t *)(dmabuf+32);
	for(i=8;i<105;i++) *p++=0;
	*p++=0xffff0000;

	p=(uint32_t *) (dmabuf+BUFFER_SIZE+32);
	for(i=8;i<105;i++) *p++=0;
	*p++=0xffff0000;

	p=(uint32_t *) (dmabuf+BUFFER_SIZE*2+32);
	for(i=8;i<114-8;i++) *p++=0;

	p=(uint32_t *) (dmabuf+BUFFER_SIZE*3+32);
	for(i=8;i<114-8;i++) *p++=0;
}

// 映像期間外用バッファデータ作成（水平同期、カラーバースト信号）
void HSYNC_colorburst(uint8_t *dmabuf) {
// バースト信号（4bit DA）
//2-3-1-1-1-1-2-2-3-3-5-4-5-5-4-4-
//0000,
//003f,
//c0ec
//c37c

	int i;
	uint32_t *p;
	p=(uint32_t *) (dmabuf+32);
	*p++=0xffff0000;
	*p++=0xc37cffff;
	for(i=0;i<4;i++) *p++=0xc37cc37c;
	for(i=0;i<100-8;i++) *p++=0xffffffff;

	p=(uint32_t *) (dmabuf+BUFFER_SIZE+32);
	*p++=0xffff0000;
	*p++=0xc0ecffff;
	for(i=0;i<4;i++) *p++=0xc0ecc0ec;
	for(i=0;i<100-8;i++) *p++=0xffffffff;

	p=(uint32_t *) (dmabuf+BUFFER_SIZE*2+32);
	*p++=0;
	*p++=0x3f000000;
	for(i=0;i<4;i++) *p++=0x3f003f00;
	for(i=0;i<100-8;i++) *p++=0;

	p=(uint32_t *) (dmabuf+BUFFER_SIZE*3+32);
	for(i=8;i<114-8;i++) *p++=0;
}

void VideoLine(uint8_t *dmabuf){
//	TMR3=0; // For time measurement
	//t7=CHECON;
	asm volatile("la	$t1,%0"::"i"(&CHECON));
	asm volatile("lw	$t7,0($t1)");
	//CHECONCLR=0x30; // Disable predictive prefetch
	asm volatile("addiu	$t0,$zero,0x30");
	asm volatile("sw	$t0,4($t1)");
	// a2=VRAMp;
	asm volatile("la	$t1,%0"::"i"(&VRAMp));
	asm volatile("lw	$a2,0($t1)");
	// a0=dmabuf+(H_SYNC+H_BACK)/8;
	asm volatile("addiu	$a0,$a0,%0"::"n"((H_SYNC+H_BACK)/8));
	// a1=VRAMp+XRES*2;
	asm volatile("addiu	$a1,$a2,%0"::"n"(X_RES*2));
	// a3=colordata;
	asm volatile("la	$a3,%0"::"i"(colordata));
	asm volatile("li	$t6,0x03ff03ff");

asm volatile("VideoLineLoop:");
	asm volatile("lw	$t4,0($a2)");
	asm volatile("lw	$t2,4($a2)");
	asm volatile("and	$t4,$t4,$t6");
	asm volatile("srl	$t5,$t4,13");
	asm volatile("addu	$t3,$t5,$a3");
	asm volatile("lw	$t1,4($t3)");
	asm volatile("ins	$t5,$t4,3,10");
	asm volatile("addu	$t5,$t5,$a3");
	asm volatile("lw	$t0,0($t5)");

	asm volatile("and	$t2,$t2,$t6");
	asm volatile("srl	$t5,$t2,13");
	asm volatile("addu	$t4,$t5,$a3");
	asm volatile("lw	$t3,4($t4)");
	asm volatile("ins	$t5,$t2,3,10");
	asm volatile("addu	$t5,$t5,$a3");
	asm volatile("lw	$t2,0($t5)");

	asm volatile("addu	$t4,$zero,$t0");
	asm volatile("ins	$t4,$t1,8,8");
	asm volatile("ins	$t4,$t2,16,8");
	asm volatile("ins	$t4,$t3,24,8");
	asm volatile("sw	$t4,0($a0)");

	asm volatile("rotr	$t0,$t0,8");
	asm volatile("rotr	$t2,$t2,24");
	asm volatile("rotr	$t3,$t3,16");

	asm volatile("addu	$t4,$zero,$t3");
	asm volatile("ins	$t4,$t2,0,24");
	asm volatile("ins	$t4,$t1,0,16");
	asm volatile("ins	$t4,$t0,0,8");
	asm volatile("sw	$t4,%0($a0)"::"n"(BUFFER_SIZE));

	asm volatile("addu	$t4,$zero,$t2");
	asm volatile("ins	$t4,$t1,0,24");
	asm volatile("ins	$t4,$t0,0,16");
	asm volatile("ins	$t4,$t3,0,8");
	asm volatile("rotr	$t4,$t4,8");
	asm volatile("sw	$t4,%0($a0)"::"n"(BUFFER_SIZE*2));

	asm volatile("ins	$t1,$t0,0,24");
	asm volatile("ins	$t1,$t3,0,16");
	asm volatile("ins	$t1,$t2,0,8");
	asm volatile("rotr	$t1,$t1,16");
	asm volatile("sw	$t1,%0($a0)"::"n"(BUFFER_SIZE*3));

	asm volatile("addiu	$a2,$a2,8");
	asm volatile("addiu	$a0,$a0,4");//遅延スロット
	asm volatile("bne	$a2,$a1,VideoLineLoop");
	//CHECON=t7;
	asm volatile("la	$t1,%0"::"i"(&CHECON));
	asm volatile("sw	$t7,0($t1)");
}

void VTask(uint8_t *dmabuf) {
	if(LineCount==V_SYNC+V_PREEQ+1) drawing=-1;
	else if(LineCount==V_SYNC+V_PREEQ+V_LINE+1){
		drawcount++;
		drawing=0;
	}
	if(LineCount>=V_SYNC+V_PREEQ && LineCount<V_SYNC+V_PREEQ+V_LINE){
		//画像描画期間
		VideoLine(dmabuf);//画像信号をバッファリング
		VRAMp+=X_RES;
	} else if (LineCount < NUM_OF_BUFFERS) {
		//垂直同期期間の最初
		VSYNC(dmabuf);//垂直同期信号をバッファリング
	} else if (LineCount >= V_SYNC && LineCount<V_SYNC+NUM_OF_BUFFERS ||
			   LineCount >= V_SYNC+V_PREEQ+V_LINE && LineCount<V_SYNC+V_PREEQ+V_LINE+NUM_OF_BUFFERS) {
		//垂直同期期間終了後の最初
		HSYNC_colorburst(dmabuf);//水平同期信号とカラーバースト信号をバッファリング
	}
	LineCount++;
	if (LineCount == V_NTSC){
		LineCount = 0;
		VRAMp=VRAM;
	}
}

// DMA1割り込み
// 転送元サイズの半分および全部転送完了時に割り込み発生
void __ISR(_DMA1_VECTOR, IPL5AUTO) _DMA1Interrupt ()
{
	if(DCH1INT & 0x0080){
		// DMA source half empty
		VTask(chdata);
		DCH1INTCLR=0x0080;
	}
	else if(DCH1INT & 0x0040){
		// DMA source done
		VTask(chdata + LINE_BUFFER_SIZE);
		DCH1INTCLR=0x0040;
	}
	IFS2CLR=0x200; // Clear DMA ch1 IF
}
void start_composite(void){
// コンポジット出力開始および再開
	uint32_t *p;
	int i;
	drawing=0;
	VRAMp=VRAM;
	LineCount=0;
	//ラインバッファの水平同期部分および行末部分を初期化（永久に変化しない部分）
	p=(uint32_t *) chdata;
	for(i=0;i<8;i++){
		*p=0;
		*(p+LINE_BUFFER_SIZE/4)=0;
		*(p+LINE_BUFFER_SIZE/4*2)=0;
		*(p+LINE_BUFFER_SIZE/4*3)=0;
		*(p+LINE_BUFFER_SIZE/4*4)=0;
		*(p+LINE_BUFFER_SIZE/4*5)=0;
		*(p+LINE_BUFFER_SIZE/4*6)=0;
		*(p+LINE_BUFFER_SIZE/4*7)=0;
		p++;
	}
	p=(uint32_t *) (chdata+LINE_BUFFER_SIZE-32);
	for(i=0;i<8;i++){
		*p=0xffffffff;
		*(p+LINE_BUFFER_SIZE/4)=0xffffffff;
		*(p+LINE_BUFFER_SIZE/4*2)=0xffffffff;
		*(p+LINE_BUFFER_SIZE/4*3)=0xffffffff;
		*(p+LINE_BUFFER_SIZE/4*4)=0;
		*(p+LINE_BUFFER_SIZE/4*5)=0;
		*(p+LINE_BUFFER_SIZE/4*6)=0;
		*(p+LINE_BUFFER_SIZE/4*7)=0;
		p++;
	}
	// ラインバッファを画面先頭2行分作成
	VTask(chdata);
	VTask(chdata + LINE_BUFFER_SIZE);

//	タイミング合わせ確認用テストデータ
//	chdata[0]=1;chdata[BUFFER_SIZE]=1;chdata[BUFFER_SIZE*2]=1;chdata[BUFFER_SIZE*3]=1;

	REFO1CON=0x0000; // Configure Reference Clock 1, souce=sysclk/divide 1:1/output stop

	// Conifgure Timer2
	T2CON = 0x0000;
	asm("nop");
	PR2 = DMA_TRANSLATION_SIZE*16-1;
	asm("nop");
	TMR2 = 0;

	SPI3CON = 0x810020; //Master clock=REFCLKO1/Enhanced Buffer Enable/8bit/Master mode/
	SPI4CON = 0x810020;
	SPI5CON = 0x810020;
	SPI6CON = 0x810020;
	SPI3CONSET = 0x8000; // SPI ON
	SPI4CONSET = 0x8000;
	SPI5CONSET = 0x8000;
	SPI6CONSET = 0x8000;

	DCH1CONSET = 0x80; // DMA channel Enable
	DCH2CONSET = 0x80;
	DCH3CONSET = 0x80;
	DCH4CONSET = 0x80;

	// Fill SPI buffers
	SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;
	SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;
	SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;SPI6BUF=0;

	SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;
	SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;
	SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;SPI5BUF=0;

	SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;
	SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;
	SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;SPI4BUF=0;

	SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;
	SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;
	SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;SPI3BUF=0;

	REFO1CON=0x8000;// Enable Reference Clock 1
	T2CONSET=0x8000;// TIMER2 ON
}
void stop_composite(void){
// コンポジット出力停止
	T2CONCLR = 0x8000; // stop timer 2
	DCH1ECONSET=0x40; // DMA Transfer Abort
	DCH2ECONSET=0x40;
	DCH3ECONSET=0x40;
	DCH4ECONSET=0x40;
	SPI3CONCLR = 0x8000; // SPI OFF
	SPI4CONCLR = 0x8000;
	SPI5CONCLR = 0x8000;
	SPI6CONCLR = 0x8000;
	REFO1CONCLR = 0x8000; // Disable Reference Clock 1
}
void init_composite(void){
	uint32_t *vp;
	int i;

	//remap pins
	RPB12R=14; // SDO3
	RPB14R=15; // SDO4
	RPB15R=16; // SDO5
	RPB13R=17; // SDO6

	SYSKEY = 0x0; // ensure OSCCON is locked
	SYSKEY = 0xAA996655; // Write Key1 to SYSKEY
	SYSKEY = 0x556699AA; // Write Key2 to SYSKEY
	PB2DIV = 0x8000;
	PB3DIV = 0x8000;
	PB6DIVSET = 0x7F;
	// Relock the SYSKEY
	SYSKEY = 0x0;

	//VRAM消去
	vp=(unsigned int *)VRAM;
	for(i=0;i<X_RES*Y_RES/2;i++) *vp++=0;

	//カラーパレット初期化
	for(i=0;i<8;i++){
		set_palette(i,255*(i&1),255*((i>>1)&1),255*(i>>2));
	}
	for(i=0;i<8;i++){
		set_palette(i+8,128*(i&1),128*((i>>1)&1),128*(i>>2));
	}
	for(i=16;i<0x3ff;i++){
		set_palette(i,255,255,255);
	}

	// SPI初期設定
	SPI3BRG = 0; // Transfer speed = System clock/2
	SPI4BRG = 0;
	SPI5BRG = 0;
	SPI6BRG = 0;

	IFS2CLR=0x200; // Clear DMA ch1 IF
	IEC2SET=0x200; // Enable DMA ch1 interrupt
	IPC18bits.DMA1IP = 5; // Set DMA ch1 interrupt priority

	DMACON = 0x8000;

	//channel 1
	DCH1CON = 0x12;
	DCH1ECON = (_TIMER_2_VECTOR << 8) | 0x10;
	DCH1SSA = ((uint32_t) chdata)&0x1FFFFFFF;
	DCH1DSA = ((uint32_t) (&SPI3BUF))&0x1FFFFFFF;
	DCH1SSIZ = BUFFER_SIZE;
	DCH1DSIZ = 1;
	DCH1CSIZ = DMA_TRANSLATION_SIZE;
	DCH1INT = 0x00c00000; // clear existing events, enable source done, source half empty interrupts

	//channel 2
	DCH2CON = 0x12;
	DCH2ECON = (_TIMER_2_VECTOR << 8) | 0x10;
	DCH2SSA = ((uint32_t) (chdata+BUFFER_SIZE))&0x1FFFFFFF;
	DCH2DSA = ((uint32_t) (&SPI4BUF))&0x1FFFFFFF;
	DCH2SSIZ = BUFFER_SIZE;
	DCH2DSIZ = 1;
	DCH2CSIZ = DMA_TRANSLATION_SIZE;
	DCH2INTCLR = 0x00ff00ff;

	//channel 3
	DCH3CON = 0x12;
	DCH3ECON = (_TIMER_2_VECTOR << 8) | 0x10;
	DCH3SSA = ((uint32_t) (chdata+BUFFER_SIZE*2))&0x1FFFFFFF;
	DCH3DSA = ((uint32_t) (&SPI5BUF))&0x1FFFFFFF;
	DCH3SSIZ = BUFFER_SIZE;
	DCH3DSIZ = 1;
	DCH3CSIZ = DMA_TRANSLATION_SIZE;
	DCH3INTCLR = 0x00ff00ff;

	//channel 4
	DCH4CON = 0x12;
	DCH4ECON = (_TIMER_2_VECTOR << 8) | 0x10;
	DCH4SSA = ((uint32_t) (chdata+BUFFER_SIZE*3))&0x1FFFFFFF;
	DCH4DSA = ((uint32_t) (&SPI6BUF))&0x1FFFFFFF;
	DCH4SSIZ = BUFFER_SIZE;
	DCH4DSIZ = 1;
	DCH4CSIZ = DMA_TRANSLATION_SIZE;
	DCH4INTCLR = 0x00ff00ff;

	//割り込みをマルチベクターモードに設定し、有効化
	INTCONSET = _INTCON_MVEC_MASK; // Configure Interrupt Controller for multi-vector mode
	__builtin_enable_interrupts(); // Set the CP0 Status register IE bit high to globally enable interrupts

	start_composite();
}
void set_palette(uint16_t c, uint8_t ib, uint8_t ir, uint8_t ig) {
//カラーパレット設定
//c:パレット番号、ib:青の強度（0-255）、ir:赤の強度（0-255）、ig:緑の強度（0-255）
	int output[16];
	int i;
	double theta,b,r,g;

	b=(double)ib/256;
	r=(double)ir/256;
	g=(double)ig/256;
	double y = g * 0.587 + b * 0.114 + r * 0.299;
	double b_y = -g * 0.587 + 0.886 * b - 0.299 * r;
	double r_y = -g * 0.587 - 0.114 * b + 0.701 * r;

	double n;
	double err = 0;

	for (i = 0; i < _CLOCK_DIVIDE; i++) {
		theta = 2 * 3.14159265358979323 * i / _CLOCK_DIVIDE;
		//    n = (y+0.4921*b_y*sin(theta)+0.8773*r_y*cos(theta))*3.9+1.54;
//		n = (y + 0.4921 * b_y * sin(theta) + 0.8773 * r_y * cos(theta))*5.0 + 1.0; //3bit DA
//		if(n+err>=8.0) n=7.99-err;
		n = (y + 0.4921 * b_y * sin(theta) + 0.8773 * r_y * cos(theta))*10.0 + 3.0; //4bit DA
		if(n+err>=16.0) n=15.99-err;
		output[i] = n + err; //prev err correction.
		err += n - output[i]; //calculate err of output.
	}

	colordata[c*2]=0;
	colordata[c*2+1]=0;
	for (i=0;i<8;i++) {
			colordata[c*2]<<=1;
			colordata[c*2]+=(output[i]&1)+((output[i]&2)<<7)+((output[i]&4)<<14)+((output[i]&8)<<21);
			colordata[c*2+1]<<=1;
			colordata[c*2+1]+=(output[i+8]&1)+((output[i+8]&2)<<7)+((output[i+8]&4)<<14)+((output[i+8]&8)<<21);
	}
}

