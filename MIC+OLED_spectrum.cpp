#include <Arduino.h>
#include <driver/i2s.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "arduinoFFT.h"

// Определение пинов для дисплея
#define TFT_SCL 19
#define TFT_SDA 9
#define TFT_RS  20
#define TFT_CS  47
#define TFT_RES 48
#define TFT_BLK 35

#define TFT_H 80
#define TFT_W 160

//I2S
#define I2S_WS 16
#define I2S_SD 15
#define I2S_SCK 37
#define I2S_PORT I2S_NUM_0

//INMP441
const uint16_t SAMPLES = 128; // Должно быть степенью 2
const double SAMPLING_FREQUENCY = 44100; // Увеличим частоту дискретизации
const uint8_t FREQ_BANDS = 32;

SPIClass hspi(HSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&hspi, TFT_CS, TFT_RS, TFT_RES);

// Буферы для БПФ
double vReal[SAMPLES];
double vImag[SAMPLES];
arduinoFFT FFT = arduinoFFT(); // Создаем объект FFT

// Буфер для аудиоданных
int16_t audioBuffer[SAMPLES];

// Массив для хранения амплитуд частотных полос
uint8_t spectrum[FREQ_BANDS];
uint8_t previousSpectrum[FREQ_BANDS];

// Переменные для диагностики
unsigned long lastDataTime = 0;
bool dataReceived = false;

// Добавьте глобальные переменные
float micGain = 2.0;
float sensitivityDivider = 50.0; // Делитель в строке average / 100

void i2s_install(){
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = (uint32_t)SAMPLING_FREQUENCY,
    .bits_per_sample = i2s_bits_per_sample_t(16),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8, // Увеличим количество буферов
    .dma_buf_len = SAMPLES,
    .use_apll = true // Включим APLL для лучшего качества звука
  };
  
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %d\n", err);
  } else {
    Serial.println("I2S driver installed successfully");
  }
}

void i2s_setpin(){
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  
  esp_err_t err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S set pin failed: %d\n", err);
  } else {
    Serial.println("I2S pins configured successfully");
  }
}

// Функция для проверки данных с микрофона
void testMicrophone() {
  size_t bytesRead = 0;
  int16_t testBuffer[10];
  
  esp_err_t result = i2s_read(I2S_PORT, testBuffer, sizeof(testBuffer), &bytesRead, 1000 / portTICK_PERIOD_MS);
  
  if(result == ESP_OK && bytesRead > 0) {
    Serial.printf("Microphone test: Read %d bytes, first sample: %d\n", bytesRead, testBuffer[0]);
    dataReceived = true;
    lastDataTime = millis();
  } else {
    Serial.printf("Microphone test failed: result=%d, bytes=%d\n", result, bytesRead);
    dataReceived = false;
  }
}

// Упрощенная версия для отладки
void calculateSimpleSpectrum() {
  // Проверяем, есть ли данные
  if (!dataReceived) {
    // Если данных нет, генерируем тестовый сигнал
    for(int i = 0; i < SAMPLES; i++) {
      // Синусоида 440 Гц для тестирования
      audioBuffer[i] = (int16_t)(1000 * sin(2 * PI * 440 * i / SAMPLING_FREQUENCY));
    }
  }
  
  for(int i = 0; i < SAMPLES; i++) {
    vReal[i] = (double)audioBuffer[i];
    vImag[i] = 0.0;
  }
  
  FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
  
  // Простое линейное распределение полос
  int binsPerBand = (SAMPLES / 2) / FREQ_BANDS;
  
  for(int band = 0; band < FREQ_BANDS; band++) {
    int startIdx = band * binsPerBand + 1;
    int endIdx = (band + 1) * binsPerBand;
    
    if(endIdx >= SAMPLES/2) endIdx = SAMPLES/2 - 1;
    if(startIdx > endIdx) startIdx = endIdx;
    
    double sum = 0;
    int count = 0;
    for(int i = startIdx; i <= endIdx; i++) {
      sum += vReal[i];
      count++;
    }
    
    if(count > 0) {
      double average = sum / count;
      // Более агрессивное масштабирование
      //int value = (int)constrain(average / 100, 0, TFT_H - 5);
      int value = (int)constrain(average / sensitivityDivider, 0, TFT_H - 5);
      spectrum[band] = value;
    } else {
      spectrum[band] = 0;
    }
  }
}

// Функция для отрисовки спектра
void drawSpectrum() {
  int bandWidth = TFT_W / FREQ_BANDS;
  
  for(int band = 0; band < FREQ_BANDS; band++) {
    int x = band * bandWidth;
    int height = spectrum[band];
    
    // Всегда очищаем и перерисовываем для простоты
    tft.fillRect(x, 0, bandWidth - 1, TFT_H, ST77XX_BLACK);
    
    if(height > 0) {
      // Градиент цвета в зависимости от высоты
      uint16_t color;
      if(height < TFT_H/4) {
        color = ST77XX_BLUE;
      } else if(height < TFT_H/2) {
        color = ST77XX_GREEN;
      } else if(height < 3*TFT_H/4) {
        color = ST77XX_YELLOW;
      } else {
        color = ST77XX_RED;
      }
      
      tft.fillRect(x, TFT_H - height, bandWidth - 1, height, color);
    }
  }
}

// Функция для отрисовки сетки
void drawGrid() {
  tft.drawRect(0, 0, TFT_W, TFT_H, ST77XX_WHITE);
  
  // Горизонтальные линии для уровней
  for(int i = 1; i < 4; i++) {
    int y = TFT_H - i * (TFT_H / 4);
    tft.drawFastHLine(0, y, TFT_W, ST77XX_CYAN);
  }
}

// Функция для отображения статуса
void drawStatus() {
  tft.fillRect(0, 0, TFT_W, 10, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(dataReceived ? ST77XX_GREEN : ST77XX_RED);
  tft.setCursor(2, 2);
  tft.print(dataReceived ? "MIC: OK " : "MIC: NO DATA");
  
  // Показываем уровень громкости
  int overallLevel = 0;
  for(int i = 0; i < FREQ_BANDS; i++) {
    overallLevel += spectrum[i];
  }
  overallLevel /= FREQ_BANDS;
  
  tft.setCursor(TFT_W - 20, 2);
  tft.print("L:");
  tft.print(overallLevel);
}

void processAudioData(void *parameter) {
  while(true) {
    size_t bytesRead = 0;
    
    // Читаем аудиоданные
    esp_err_t result = i2s_read(I2S_PORT, audioBuffer, 
                               SAMPLES * sizeof(int16_t), &bytesRead, portMAX_DELAY);
    
    if(result == ESP_OK && bytesRead > 0) {
      dataReceived = true;
      lastDataTime = millis();
      
      // Вычисляем спектр
      calculateSimpleSpectrum();
    } else {
      // Если данных нет долгое время, сбрасываем флаг
      if(millis() - lastDataTime > 1000) {
        dataReceived = false;
      }
    }
    
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Audio Spectrum Analyzer...");
  
  // Инициализация дисплея
  hspi.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);
  
  tft.initR(INITR_MINI160x80_PLUGIN);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  
  // Отображаем начальный экран
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 30);
  tft.println("Initializing...");
  
  delay(100);
  
  // Инициализация I2S
  Serial.println("Initializing I2S...");
  i2s_install();
  i2s_setpin();
  
  // Запускаем I2S
  esp_err_t err = i2s_start(I2S_PORT);
  if (err != ESP_OK) {
    Serial.printf("I2S start failed: %d\n", err);
  } else {
    Serial.println("I2S started successfully");
  }
  
  // Тестируем микрофон
 // Serial.println("Testing microphone...");
 // testMicrophone();
  
  // Инициализируем массивы спектра
  memset(spectrum, 0, sizeof(spectrum));
  memset(previousSpectrum, 0, sizeof(previousSpectrum));
  
  // Рисуем сетку
  drawGrid();
  
  // Создаем задачу для обработки аудио
  xTaskCreatePinnedToCore(processAudioData, "Audio Processing", 8192, NULL, 1, NULL, 1);
  
  tft.fillScreen(ST77XX_BLACK);
  drawGrid();
  Serial.println("Audio Spectrum Analyzer Started");
}

void loop() {
  // Основной цикл - обновляем дисплей
  drawSpectrum();
  drawStatus();
  // Задержка для стабильности отображения
  delay(50);
}
