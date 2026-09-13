#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <MD_MAX72xx.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#if __has_include(<secrets.h>)
#include <secrets.h>
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

constexpr uint8_t DATA_PIN = 6;
constexpr uint8_t CLK_PIN = 4;
constexpr uint8_t CS_PIN = 7;
constexpr uint8_t MODULE_COUNT = 4;
constexpr uint8_t DISPLAY_WIDTH = MODULE_COUNT * 8;
constexpr uint8_t INTENSITY = 2;
constexpr bool FLIP_VERTICAL = true;

constexpr uint32_t DISPLAY_REFRESH_MS = 80;
constexpr uint32_t WIFI_STATUS_LOG_MS = 20000;
constexpr uint32_t MDNS_RETRY_MS = 10000;
constexpr uint32_t NTP_SYNC_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t INFO_CYCLE_MS = 25000;
constexpr uint32_t DATE_PAGE_MS = 3000;
constexpr uint32_t INFO_PAGE_MS = 2000;
constexpr uint32_t WEATHER_UPDATE_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS = 60UL * 1000UL;

constexpr char TIME_ZONE[] = "<-03>3";
constexpr char MDNS_HOSTNAME[] = "reloj";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.google.com";
constexpr char NTP_SERVER_3[] = "time.cloudflare.com";
constexpr char WEATHER_HOST[] = "api.open-meteo.com";
constexpr char WEATHER_URL[] =
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=-38.114864&longitude=-57.607937"
    "&current=temperature_2m,relative_humidity_2m,weather_code,"
    "wind_speed_10m,is_day"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&timezone=America%2FArgentina%2FBuenos_Aires&forecast_days=1";

MD_MAX72XX matrix(
    MD_MAX72XX::FC16_HW, DATA_PIN, CLK_PIN, CS_PIN, MODULE_COUNT);

constexpr uint8_t BIG_DIGITS[10][7] = {
    {0b1111, 0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b1111},
    {0b0110, 0b1110, 0b0110, 0b0110, 0b0110, 0b0110, 0b1111},
    {0b1110, 0b0011, 0b0001, 0b0010, 0b0100, 0b1100, 0b1111},
    {0b1110, 0b0011, 0b0001, 0b0110, 0b0001, 0b0011, 0b1110},
    {0b0011, 0b0111, 0b1101, 0b1001, 0b1111, 0b0001, 0b0001},
    {0b1111, 0b1100, 0b1000, 0b1110, 0b0001, 0b0011, 0b1110},
    {0b0111, 0b1100, 0b1000, 0b1110, 0b1001, 0b1001, 0b1110},
    {0b1111, 0b0011, 0b0010, 0b0110, 0b0100, 0b0100, 0b0100},
    {0b1111, 0b1001, 0b1001, 0b1111, 0b1001, 0b1001, 0b1111},
    {0b1110, 0b1001, 0b1001, 0b1111, 0b0001, 0b0011, 0b1110},
};

constexpr uint8_t SMALL_DIGITS[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},
    {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111},
    {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001},
    {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111},
    {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111},
    {0b111, 0b101, 0b111, 0b001, 0b111},
};

constexpr uint8_t SMALL_LETTERS[26][5] = {
    {0b010, 0b101, 0b111, 0b101, 0b101},  // A
    {0b110, 0b101, 0b110, 0b101, 0b110},  // B
    {0b011, 0b100, 0b100, 0b100, 0b011},  // C
    {0b110, 0b101, 0b101, 0b101, 0b110},  // D
    {0b111, 0b100, 0b110, 0b100, 0b111},  // E
    {0b111, 0b100, 0b110, 0b100, 0b100},  // F
    {0b011, 0b100, 0b101, 0b101, 0b011},  // G
    {0b101, 0b101, 0b111, 0b101, 0b101},  // H
    {0b111, 0b010, 0b010, 0b010, 0b111},  // I
    {0b001, 0b001, 0b001, 0b101, 0b010},  // J
    {0b101, 0b101, 0b110, 0b101, 0b101},  // K
    {0b100, 0b100, 0b100, 0b100, 0b111},  // L
    {0b101, 0b111, 0b111, 0b101, 0b101},  // M
    {0b101, 0b111, 0b111, 0b111, 0b101},  // N
    {0b010, 0b101, 0b101, 0b101, 0b010},  // O
    {0b110, 0b101, 0b110, 0b100, 0b100},  // P
    {0b010, 0b101, 0b101, 0b111, 0b011},  // Q
    {0b110, 0b101, 0b110, 0b101, 0b101},  // R
    {0b011, 0b100, 0b010, 0b001, 0b110},  // S
    {0b111, 0b010, 0b010, 0b010, 0b010},  // T
    {0b101, 0b101, 0b101, 0b101, 0b111},  // U
    {0b101, 0b101, 0b101, 0b101, 0b010},  // V
    {0b101, 0b101, 0b111, 0b111, 0b101},  // W
    {0b101, 0b101, 0b010, 0b101, 0b101},  // X
    {0b101, 0b101, 0b010, 0b010, 0b010},  // Y
    {0b111, 0b001, 0b010, 0b100, 0b111},  // Z
};

constexpr uint8_t GLYPH_PERCENT[5] = {0b101, 0b001, 0b010, 0b100, 0b101};
constexpr uint8_t GLYPH_COLON[5] = {0b000, 0b010, 0b000, 0b010, 0b000};
constexpr uint8_t GLYPH_SLASH[5] = {0b001, 0b001, 0b010, 0b100, 0b100};
constexpr uint8_t GLYPH_MINUS[5] = {0b000, 0b000, 0b111, 0b000, 0b000};
constexpr uint8_t GLYPH_SPACE[5] = {0, 0, 0, 0, 0};

enum class ClockView : uint8_t {
  TIME,
  DATE,
  TEMPERATURE,
  HUMIDITY,
  WEATHER,
  MAXIMUM,
  MINIMUM,
  WIND
};

bool ntpStarted = false;
bool mdnsStarted = false;
bool timeWasSynchronized = false;
bool infoSequenceActive = false;
uint32_t infoSequenceStartedAt = 0;
uint32_t lastDisplayRefresh = 0;
uint32_t lastWifiStatusLog = 0;
uint32_t lastMdnsAttempt = 0;
uint32_t lastInfoSequenceAt = 0;
uint32_t currentFrame[8] = {};
uint32_t nextFrame[8] = {};
uint32_t startupPixels[8] = {};
uint16_t startupPixelCount = 0;
wifi_power_t currentWifiTxPower = WIFI_POWER_19_5dBm;
portMUX_TYPE weatherDataMux = portMUX_INITIALIZER_UNLOCKED;
int16_t weatherTemperatureC = 0;
uint8_t weatherHumidityPercent = 0;
uint8_t weatherCode = 0;
uint16_t weatherWindKmh = 0;
bool weatherIsDay = true;
int16_t weatherMaximumC = 0;
int16_t weatherMinimumC = 0;
volatile bool weatherHasValidData = false;
volatile bool weatherRequestInProgress = false;
uint32_t lastWeatherRequestAt = 0;

void beginFrame() {
  memset(nextFrame, 0, sizeof(nextFrame));
}

void setPixel(uint8_t row, uint8_t column, bool on = true) {
  if (row >= 8 || column >= DISPLAY_WIDTH) return;
  const uint32_t mask = 1UL << column;
  if (on) {
    nextFrame[row] |= mask;
  } else {
    nextFrame[row] &= ~mask;
  }
}

void commitFrame() {
  bool changed = false;
  for (uint8_t row = 0; row < 8; ++row) {
    uint32_t differences = currentFrame[row] ^ nextFrame[row];
    while (differences != 0) {
      const uint8_t column = static_cast<uint8_t>(__builtin_ctzl(differences));
      const uint8_t displayRow = FLIP_VERTICAL ? 7 - row : row;
      const bool on = (nextFrame[row] & (1UL << column)) != 0;
      matrix.setPoint(displayRow, column, on);
      differences &= differences - 1;
      changed = true;
    }
    currentFrame[row] = nextFrame[row];
  }
  if (changed) matrix.update();
}

void drawBigDigit(uint8_t digit, uint8_t x) {
  for (uint8_t row = 0; row < 7; ++row) {
    for (uint8_t column = 0; column < 4; ++column) {
      setPixel(row, x + column, BIG_DIGITS[digit][row] & (1U << (3 - column)));
    }
  }
}

const uint8_t* smallGlyph(char character) {
  if (character >= '0' && character <= '9') {
    return SMALL_DIGITS[character - '0'];
  }
  if (character >= 'A' && character <= 'Z') {
    return SMALL_LETTERS[character - 'A'];
  }
  switch (character) {
    case '%': return GLYPH_PERCENT;
    case ':': return GLYPH_COLON;
    case '/': return GLYPH_SLASH;
    case '-': return GLYPH_MINUS;
    default: return GLYPH_SPACE;
  }
}

void drawSmallCharacter(char character, uint8_t x, uint8_t y = 1) {
  const uint8_t* glyph = smallGlyph(character);
  for (uint8_t row = 0; row < 5; ++row) {
    for (uint8_t column = 0; column < 3; ++column) {
      setPixel(y + row, x + column, glyph[row] & (1U << (2 - column)));
    }
  }
}

void drawCenteredSmallText(const char* text) {
  const size_t length = strlen(text);
  if (length == 0) return;
  const uint8_t textWidth = static_cast<uint8_t>(length * 3 + length - 1);
  uint8_t x = textWidth < DISPLAY_WIDTH ? (DISPLAY_WIDTH - textWidth) / 2 : 0;
  for (size_t index = 0; index < length && x + 2 < DISPLAY_WIDTH; ++index) {
    drawSmallCharacter(text[index], x);
    x += 4;
  }
}

void drawTime(const tm& localTime) {
  drawBigDigit(localTime.tm_hour / 10, 1);
  drawBigDigit(localTime.tm_hour % 10, 6);
  setPixel(1, 11);
  setPixel(2, 11);
  setPixel(4, 11);
  setPixel(5, 11);
  drawBigDigit(localTime.tm_min / 10, 13);
  drawBigDigit(localTime.tm_min % 10, 18);
  drawSmallCharacter('0' + localTime.tm_sec / 10, 23);
  drawSmallCharacter('0' + localTime.tm_sec % 10, 27);
}

void drawDate(const tm& localTime) {
  char date[9];
  snprintf(date, sizeof(date), "%02d/%02d/%02d", localTime.tm_mday,
           localTime.tm_mon + 1, (localTime.tm_year + 1900) % 100);
  drawCenteredSmallText(date);
}

void drawTemperature() {
  int16_t temperature;
  portENTER_CRITICAL(&weatherDataMux);
  temperature = weatherTemperatureC;
  portEXIT_CRITICAL(&weatherDataMux);

  char text[9];
  snprintf(text, sizeof(text), "T:%dC", temperature);
  drawCenteredSmallText(text);
}

void drawHumidity() {
  uint8_t humidity;
  portENTER_CRITICAL(&weatherDataMux);
  humidity = weatherHumidityPercent;
  portEXIT_CRITICAL(&weatherDataMux);

  char text[9];
  snprintf(text, sizeof(text), "H:%u%%", humidity);
  drawCenteredSmallText(text);
}

const char* weatherDescription(uint8_t code, bool isDay) {
  if (code == 0) return isDay ? "SOLEADO" : "CLARO";
  if (code <= 2) return "PARCIAL";
  if (code == 3) return "NUBLADO";
  if (code == 45 || code == 48) return "NIEBLA";
  if ((code >= 51 && code <= 57)) return "LLOVIZNA";
  if (code >= 61 && code <= 67) return "LLUVIA";
  if (code >= 71 && code <= 77) return "NIEVE";
  if (code >= 80 && code <= 82) return "CHUBASCO";
  if (code == 85 || code == 86) return "NEVANDO";
  if (code == 95) return "TORMENTA";
  if (code == 96 || code == 99) return "GRANIZO";
  return "SIN DATO";
}

const char* windDescription(uint16_t speedKmh) {
  if (speedKmh < 4) return "CALMO";
  if (speedKmh < 16) return "BRISA";
  if (speedKmh < 30) return "VIENTO";
  if (speedKmh < 50) return "VENTOSO";
  return "FUERTE";
}

void drawWeatherCondition() {
  uint8_t code;
  bool isDay;
  bool valid;
  portENTER_CRITICAL(&weatherDataMux);
  code = weatherCode;
  isDay = weatherIsDay;
  valid = weatherHasValidData;
  portEXIT_CRITICAL(&weatherDataMux);
  drawCenteredSmallText(valid ? weatherDescription(code, isDay) : "SIN DATO");
}

void drawWindCondition() {
  uint16_t speedKmh;
  bool valid;
  portENTER_CRITICAL(&weatherDataMux);
  speedKmh = weatherWindKmh;
  valid = weatherHasValidData;
  portEXIT_CRITICAL(&weatherDataMux);
  drawCenteredSmallText(valid ? windDescription(speedKmh) : "SIN DATO");
}

void drawDailyExtreme(bool maximum) {
  int16_t temperature;
  bool valid;
  portENTER_CRITICAL(&weatherDataMux);
  temperature = maximum ? weatherMaximumC : weatherMinimumC;
  valid = weatherHasValidData;
  portEXIT_CRITICAL(&weatherDataMux);

  char text[9];
  snprintf(text, sizeof(text), maximum ? "MAX:%dC" : "MIN:%dC",
           valid ? temperature : 0);
  drawCenteredSmallText(text);
}

void drawConnectionAnimation(bool waitingForNtp) {
  (void)waitingForNtp;
  constexpr uint16_t PIXEL_COUNT = DISPLAY_WIDTH * 8;

  if (startupPixelCount >= PIXEL_COUNT) {
    memset(startupPixels, 0, sizeof(startupPixels));
    startupPixelCount = 0;
  }

  // Elegir uniformemente uno de los pixeles que todavia estan apagados.
  uint16_t unlitIndex = static_cast<uint16_t>(
      random(static_cast<long>(PIXEL_COUNT - startupPixelCount)));
  for (uint16_t pixel = 0; pixel < PIXEL_COUNT; ++pixel) {
    const uint8_t row = pixel / DISPLAY_WIDTH;
    const uint8_t column = pixel % DISPLAY_WIDTH;
    const uint32_t mask = 1UL << column;
    if ((startupPixels[row] & mask) != 0) continue;
    if (unlitIndex-- == 0) {
      startupPixels[row] |= mask;
      ++startupPixelCount;
      break;
    }
  }

  for (uint8_t row = 0; row < 8; ++row) {
    for (uint8_t column = 0; column < DISPLAY_WIDTH; ++column) {
      if ((startupPixels[row] & (1UL << column)) != 0) {
        setPixel(row, column);
      }
    }
  }
}

bool readLocalTime(tm& localTime) {
  return getLocalTime(&localTime, 0) && localTime.tm_year >= (2024 - 1900);
}

void setWifiTxPower(wifi_power_t power, const char* description) {
  if (currentWifiTxPower == power) return;
  if (WiFi.setTxPower(power)) {
    currentWifiTxPower = power;
    Serial.printf("Potencia Wi-Fi: %s\n", description);
  } else {
    Serial.printf("No se pudo ajustar la potencia Wi-Fi a %s\n", description);
  }
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    setWifiTxPower(WIFI_POWER_8_5dBm, "8,5 dBm (conexion)");
    const auto reason =
        static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
    Serial.printf("Wi-Fi desconectado. Motivo: %u (%s)\n", reason,
                  WiFi.disconnectReasonName(reason));
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    setWifiTxPower(WIFI_POWER_19_5dBm, "19,5 dBm (conectado)");
    Serial.printf("Wi-Fi conectado. IP: %s, BSSID: %s, canal: %d, RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str(),
                  WiFi.channel(), WiFi.RSSI());
  }
}

void beginWifi() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("Falta configurar WIFI_SSID en include/secrets.h");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  // El AP MATIAS anuncia modo mixto WPA/WPA2 (auth=4). Permitir WPA como
  // umbral hace compatible la negociacion; conviene configurar WPA2-CCMP puro
  // en el router cuando sea posible.
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
  // El escaneo rapido puede detenerse en el primer AP/repetidor que comparta
  // el SSID. Recorrer todos los canales permite elegir el BSSID con mejor RSSI.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  // Espressif recomienda desactivar modem-sleep al diagnosticar expiraciones
  // de autenticacion, en las que el AP no responde al pedido del ESP32-C3.
  WiFi.setSleep(false);
  setWifiTxPower(WIFI_POWER_8_5dBm, "8,5 dBm (conexion)");
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.onEvent(onWifiEvent);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiStatusLog = millis();
  Serial.printf("Conectando a Wi-Fi: %s\n", WIFI_SSID);
}

void updateMdns(bool wifiConnected, uint32_t now) {
  if (!wifiConnected) {
    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
      Serial.println("mDNS detenido hasta recuperar Wi-Fi.");
    }
    return;
  }
  if (mdnsStarted ||
      (lastMdnsAttempt != 0 && now - lastMdnsAttempt < MDNS_RETRY_MS)) {
    return;
  }
  lastMdnsAttempt = now;
  mdnsStarted = MDNS.begin(MDNS_HOSTNAME);
  if (mdnsStarted) {
    Serial.println("mDNS disponible como reloj.local");
  } else {
    Serial.println("No se pudo iniciar mDNS; se reintentara.");
  }
}

void startNtp() {
  configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  esp_sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
  ntpStarted = true;
  Serial.println("Wi-Fi conectado. Esperando la hora NTP...");
}

void fetchWeatherTask(void*) {
  WiFiClient networkClient;
  networkClient.setTimeout(8);

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  bool success = false;
  IPAddress weatherAddress;
  const int dnsResult = WiFi.hostByName(WEATHER_HOST, weatherAddress);
  if (dnsResult == 1) {
    Serial.printf("DNS clima: %s -> %s\n", WEATHER_HOST,
                  weatherAddress.toString().c_str());
  } else {
    Serial.printf("No se pudo resolver el servidor meteorologico. codigo=%d\n",
                  dnsResult);
  }

  if (WiFi.status() == WL_CONNECTED && dnsResult == 1 &&
      http.begin(networkClient, WEATHER_URL)) {
    const int statusCode = http.GET();
    if (statusCode == HTTP_CODE_OK) {
      const String payload = http.getString();
      JsonDocument document;
      const DeserializationError error =
          deserializeJson(document, payload);
      const float temperature =
          document["current"]["temperature_2m"] | NAN;
      const int humidity =
          document["current"]["relative_humidity_2m"] | -1;
      const int code = document["current"]["weather_code"] | -1;
      const float windSpeed = document["current"]["wind_speed_10m"] | NAN;
      const int isDay = document["current"]["is_day"] | -1;
      const float maximum =
          document["daily"]["temperature_2m_max"][0] | NAN;
      const float minimum =
          document["daily"]["temperature_2m_min"][0] | NAN;

      if (!error && isfinite(temperature) && temperature >= -99.0f &&
          temperature <= 99.0f && humidity >= 0 && humidity <= 100 &&
          code >= 0 && code <= 99 && isfinite(windSpeed) &&
          windSpeed >= 0.0f && windSpeed <= 500.0f &&
          (isDay == 0 || isDay == 1) && isfinite(maximum) &&
          isfinite(minimum) && maximum >= -99.0f && maximum <= 99.0f &&
          minimum >= -99.0f && minimum <= 99.0f && minimum <= maximum) {
        const int16_t roundedTemperature =
            static_cast<int16_t>(lroundf(temperature));
        const uint16_t roundedWind =
            static_cast<uint16_t>(lroundf(windSpeed));
        const int16_t roundedMaximum =
            static_cast<int16_t>(lroundf(maximum));
        const int16_t roundedMinimum =
            static_cast<int16_t>(lroundf(minimum));
        portENTER_CRITICAL(&weatherDataMux);
        weatherTemperatureC = roundedTemperature;
        weatherHumidityPercent = static_cast<uint8_t>(humidity);
        weatherCode = static_cast<uint8_t>(code);
        weatherWindKmh = roundedWind;
        weatherIsDay = isDay == 1;
        weatherMaximumC = roundedMaximum;
        weatherMinimumC = roundedMinimum;
        weatherHasValidData = true;
        portEXIT_CRITICAL(&weatherDataMux);
        success = true;
        Serial.printf(
            "Clima actualizado: %d C, %d %%, codigo=%d, viento=%u km/h, "
            "dia=%d, max=%d C, min=%d C\n",
            roundedTemperature, humidity, code, roundedWind, isDay,
            roundedMaximum, roundedMinimum);
      } else {
        Serial.printf("Respuesta meteorologica invalida: %s\n",
                      error.c_str());
      }
    } else {
      Serial.printf("Error HTTP al consultar clima: %d (%s)\n", statusCode,
                    HTTPClient::errorToString(statusCode).c_str());
    }
    http.end();
  } else {
    Serial.println("No se pudo iniciar la consulta meteorologica.");
  }

  if (!success) {
    Serial.println("Se conserva el ultimo dato meteorologico valido.");
  }
  networkClient.stop();
  weatherRequestInProgress = false;
  vTaskDelete(nullptr);
}

void updateWeather(bool wifiConnected, uint32_t now) {
  if (!wifiConnected || !timeWasSynchronized || weatherRequestInProgress) {
    return;
  }

  const uint32_t interval =
      weatherHasValidData ? WEATHER_UPDATE_MS : WEATHER_RETRY_MS;
  if (lastWeatherRequestAt != 0 &&
      now - lastWeatherRequestAt < interval) {
    return;
  }

  lastWeatherRequestAt = now;
  weatherRequestInProgress = true;
  if (xTaskCreate(fetchWeatherTask, "weather-fetch", 8192, nullptr, 1,
                  nullptr) != pdPASS) {
    weatherRequestInProgress = false;
    Serial.println("No se pudo crear la tarea meteorologica.");
  }
}

ClockView currentView(uint32_t now) {
  if (!infoSequenceActive) return ClockView::TIME;
  const uint32_t elapsed = now - infoSequenceStartedAt;
  if (elapsed < DATE_PAGE_MS) return ClockView::DATE;
  if (elapsed < DATE_PAGE_MS + INFO_PAGE_MS) {
    return ClockView::TEMPERATURE;
  }
  if (elapsed < DATE_PAGE_MS + INFO_PAGE_MS * 2) {
    return ClockView::HUMIDITY;
  }
  if (elapsed < DATE_PAGE_MS + INFO_PAGE_MS * 3) return ClockView::WEATHER;
  if (elapsed < DATE_PAGE_MS + INFO_PAGE_MS * 4) return ClockView::MAXIMUM;
  if (elapsed < DATE_PAGE_MS + INFO_PAGE_MS * 5) return ClockView::MINIMUM;
  if (elapsed < DATE_PAGE_MS + INFO_PAGE_MS * 6) return ClockView::WIND;
  infoSequenceActive = false;
  return ClockView::TIME;
}

void updateClockDisplay(uint32_t now) {
  tm localTime{};
  if (!readLocalTime(localTime)) {
    drawConnectionAnimation(true);
    return;
  }
  if (!timeWasSynchronized) {
    timeWasSynchronized = true;
    lastInfoSequenceAt = now;
    Serial.println("Hora NTP recibida correctamente.");
  }
  if (!infoSequenceActive &&
      now - lastInfoSequenceAt >= INFO_CYCLE_MS) {
    infoSequenceActive = true;
    infoSequenceStartedAt = now;
    lastInfoSequenceAt = now;
  }
  switch (currentView(now)) {
    case ClockView::DATE: drawDate(localTime); break;
    case ClockView::TEMPERATURE: drawTemperature(); break;
    case ClockView::HUMIDITY: drawHumidity(); break;
    case ClockView::WEATHER: drawWeatherCondition(); break;
    case ClockView::MAXIMUM: drawDailyExtreme(true); break;
    case ClockView::MINIMUM: drawDailyExtreme(false); break;
    case ClockView::WIND: drawWindCondition(); break;
    case ClockView::TIME: drawTime(localTime); break;
  }
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  matrix.begin();
  matrix.control(MD_MAX72XX::INTENSITY, INTENSITY);
  matrix.clear();
  matrix.update(MD_MAX72XX::OFF);
  beginWifi();
}

void loop() {
  const uint32_t now = millis();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  updateMdns(wifiConnected, now);
  if (wifiConnected && !ntpStarted) {
    startNtp();
  } else if (strlen(WIFI_SSID) > 0 && !wifiConnected &&
             now - lastWifiStatusLog >= WIFI_STATUS_LOG_MS) {
    lastWifiStatusLog = now;
    Serial.printf("Wi-Fi aun sin conectar; autorreconexion activa. estado=%d\n",
                  WiFi.status());
  }
  updateWeather(wifiConnected, now);
  if (now - lastDisplayRefresh < DISPLAY_REFRESH_MS) return;
  lastDisplayRefresh = now;
  beginFrame();
  if (!ntpStarted) {
    drawConnectionAnimation(false);
  } else {
    updateClockDisplay(now);
  }
  commitFrame();
}
