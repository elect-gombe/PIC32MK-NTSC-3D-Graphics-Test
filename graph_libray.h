// SPI-Composite出力グラフィックライブラリ for PIC32MK1024GPD064用ヘッダーファイル　by K.Tanaka

void g_clearscreen(void); //Graphic画面クリア

// グラフィック画面関連
void g_pset(int x,int y,unsigned int c);
	// (x,y)の位置にカラーcで点を描画
void g_putbmpmn(int x,int y,char m,char n,const unsigned short bmp[]);
	// 横m*縦nドットのキャラクターを座標x,yに表示
	// bmp[m*n]配列に、単純にカラー番号を並べる
	// カラー番号が0の部分は透明色として扱う
void g_clrbmpmn(int x,int y,char m,char n);
	// 縦m*横nドットのキャラクター消去
	// カラー0で塗りつぶし
void g_gline(int x1,int y1,int x2,int y2,unsigned int c);
	// (x1,y1)-(x2,y2)にカラーcで線分を描画
void g_hline(int x1,int x2,int y,unsigned int c);
	// (x1,y)-(x2,y)の水平ラインをカラーcで高速描画
void g_boxfill(int x1,int y1,int x2,int y2,unsigned int c);
	//座標(x1,y1),(x2,y2)を対角線とするカラーcで塗られた長方形を描画
	// (x1,y1),(x2,y2)を対角線とするカラーcで塗られた長方形を描画
void g_circle(int x0,int y0,unsigned int r,unsigned int c);
	// (x0,y0)を中心に、半径r、カラーcの円を描画
void g_circlefill(int x0,int y0,unsigned int r,unsigned int c);
	// (x0,y0)を中心に、半径r、カラーcで塗られた円を描画
void g_putfont(int x,int y,unsigned int c,int bc,unsigned char n);
	//8*8ドットのアルファベットフォント表示
	//座標（x,y)、カラー番号c
	//bc:バックグランドカラー、負数の場合無視
	//n:文字番号
void g_printstr(int x,int y,unsigned int c,int bc,unsigned char *s);
	//座標(x,y)からカラー番号cで文字列sを表示、bc:バックグランドカラー
void g_printnum(int x,int y,unsigned char c,int bc,unsigned int n);
	//座標(x,y)にカラー番号cで数値nを表示、bc:バックグランドカラー
void g_printnum2(int x,int y,unsigned char c,int bc,unsigned int n,unsigned char e);
	//座標(x,y)にカラー番号cで数値nを表示、bc:バックグランドカラー、e桁で表示
unsigned int g_color(int x,int y);
//座標(x,y)のVRAM上の現在のパレット番号を返す、画面外は0を返す
