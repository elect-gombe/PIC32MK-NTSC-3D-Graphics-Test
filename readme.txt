SPI 4chを用いたカラーコンポジット信号生成実験 for PIC32MK1024GPD064 by K.Tanaka
http://www.ze.em-net.ne.jp/~kenken/spicomposite/index.html

●HEXファイル
　spi_composite.X.production.hex　ビルド済み実行ファイル。PICkit3等を用いて書き込めば動作可能

●ソースファイル
下記のC言語ソースおよびヘッダーファイル全てをプロジェクトに追加し、ビルドする。
　demo1.c　　　　　　デモアプリケーション
　raytrace.c　　　　 デモアプリケーション（レイトレーシング）
　graph_library.c　　グラフィック描画関連ライブラリ
　spi_composite.c　　SPIコンポジット生成システム本体

　graph_libray.h　　 グラフィック描画関連ライブラリを使用するためのヘッダ
　spi_composite.h　　SPIコンポジット生成システムを使用するためのヘッダ

●ビルド環境
・MPLAB X IDE v4.00
・XC32 Compiler v1.42
・文字コード　UTF-8
・最適化オプション　O1を指定
