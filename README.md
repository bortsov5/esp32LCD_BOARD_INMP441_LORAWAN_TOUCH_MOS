# Универсальная доска для esp32 или esp32s3 
 
## Питание 5-19в
 ------- Опционально -------
- 5 силовых ключей коммутации
- INMP441 - микрофон
   * пины для esp32-s3
     * #define I2S_WS 16
     * #define I2S_SD 15
     * #define I2S_SCK 37
- Экранчик  80x160
   * пины для esp32-s3
     * #define TFT_SCL 19
     * #define TFT_SDA 9
     * #define TFT_RS  20
     * #define TFT_CS  47
     * #define TFT_RES 48
     * #define TFT_BLK 35
- Динамик пищалка
- LORAWAN     

<img src="src\esp32LCD.png" alt="" width="600">
