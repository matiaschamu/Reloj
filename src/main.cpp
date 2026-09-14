#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <MD_MAX72xx.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <ping/ping_sock.h>
#include <lwip/dns.h>
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
constexpr uint8_t DEFAULT_INTENSITY = 2;
constexpr uint8_t DISPLAY_MAX_INTENSITY = 15;
constexpr bool FLIP_VERTICAL = true;

constexpr uint32_t DISPLAY_REFRESH_MS = 80;
constexpr uint32_t WIFI_STATUS_LOG_MS = 20000;
constexpr uint32_t MDNS_RETRY_MS = 10000;
constexpr uint32_t NTP_INITIAL_RETRY_MS = 30000;
constexpr uint32_t NTP_SYNC_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t GATEWAY_PING_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_RECOVERY_COOLDOWN_MS = 5UL * 60UL * 1000UL;
constexpr uint8_t GATEWAY_PING_FAILURE_LIMIT = 3;
constexpr uint32_t INFO_CYCLE_MS = 25000;
constexpr uint32_t DATE_PAGE_MS = 3000;
constexpr uint32_t INFO_PAGE_MS = 2000;
constexpr uint32_t WEATHER_UPDATE_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS = 60UL * 1000UL;

constexpr char TIME_ZONE[] = "<-03>3";
constexpr char MDNS_HOSTNAME[] = "reloj";
constexpr char NTP_SERVER_1[] = "ntp2.hidro.gob.ar";
constexpr char NTP_SERVER_2[] = "ntp.inti.gob.ar";
constexpr char NTP_SERVER_3[] = "time.cloudflare.com";
constexpr char WEATHER_HOST[] = "api.open-meteo.com";
const IPAddress PRIMARY_DNS(8, 8, 8, 8);
constexpr char WEATHER_URL[] =
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=-38.114864&longitude=-57.607937"
    "&current=temperature_2m,relative_humidity_2m,weather_code,"
    "wind_speed_10m,is_day"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&timezone=America%2FArgentina%2FBuenos_Aires&forecast_days=1";

MD_MAX72XX matrix(
    MD_MAX72XX::FC16_HW, DATA_PIN, CLK_PIN, CS_PIN, MODULE_COUNT);
WebServer webServer(80);

const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Reloj ESP32-C3</title>
  <style>
    :root{color-scheme:dark;--bg:#080b12;--panel:#111827;--line:#263247;--text:#e5edf7;--muted:#91a0b5;--accent:#67e8f9;--good:#4ade80;--warn:#fbbf24}
    *{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 20% 0,#14213b 0,transparent 38%),var(--bg);color:var(--text);font:15px system-ui,sans-serif}
    main{width:min(980px,calc(100% - 28px));margin:30px auto 50px}header{display:flex;align-items:end;justify-content:space-between;gap:20px;margin-bottom:22px}
    h1{font-size:clamp(25px,5vw,40px);margin:0;letter-spacing:-.04em}header p{margin:6px 0 0;color:var(--muted)}
    .live{display:flex;align-items:center;gap:8px;color:var(--muted);white-space:nowrap}.dot{width:9px;height:9px;border-radius:50%;background:var(--warn);box-shadow:0 0 12px currentColor}.dot.ok{background:var(--good)}
    .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:15px}.card{background:color-mix(in srgb,var(--panel) 94%,transparent);border:1px solid var(--line);border-radius:16px;padding:19px;box-shadow:0 14px 40px #0004}
    .wide{grid-column:1/-1}h2{margin:0 0 15px;font-size:13px;text-transform:uppercase;letter-spacing:.12em;color:var(--accent)}
    dl{display:grid;grid-template-columns:1fr auto;gap:10px 18px;margin:0}dt{color:var(--muted)}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
    .slider-row{display:grid;grid-template-columns:1fr 52px;gap:14px;align-items:center}input[type=range]{width:100%;accent-color:var(--accent)}output{font-size:25px;text-align:center;font-weight:700}
    button{border:1px solid #38506c;background:#1b2a3f;color:var(--text);border-radius:10px;padding:10px 14px;font:inherit;cursor:pointer}button:hover{border-color:var(--accent)}button:disabled{opacity:.55;cursor:wait}
    .actions{display:flex;align-items:center;gap:12px;margin-top:16px}.message{color:var(--muted);font-size:13px}.signal-meaning{display:block;max-width:290px;margin-top:4px;color:var(--muted);font-size:12px;line-height:1.4}.endpoint{font-family:ui-monospace,monospace;color:var(--muted);font-size:12px;margin-top:15px}
    @media(max-width:650px){header{align-items:start;flex-direction:column}.grid{grid-template-columns:1fr}.wide{grid-column:auto}}
  </style>
</head>
<body>
<main>
  <header><div><h1>Reloj ESP32-C3</h1><p>Control y diagnóstico local</p></div><div class="live"><span id="dot" class="dot"></span><span id="connection">Conectando…</span></div></header>
  <section class="grid">
    <article class="card wide"><h2>Brillo de la matriz</h2><div class="slider-row"><input id="brightness" type="range" min="0" max="15" step="1"><output id="brightnessValue">–</output></div><div id="brightnessMessage" class="message">El cambio se aplica inmediatamente.</div></article>
    <article class="card"><h2>Dispositivo</h2><dl><dt>Vista</dt><dd id="view">–</dd><dt>Actividad</dt><dd id="uptime">–</dd><dt>Memoria libre</dt><dd id="heap">–</dd><dt>Mínimo libre</dt><dd id="minHeap">–</dd></dl></article>
    <article class="card"><h2>Hora</h2><dl><dt>NTP iniciado</dt><dd id="ntpStarted">–</dd><dt>Sincronizada</dt><dd id="timeSynced">–</dd><dt>Hora local actual</dt><dd id="localTime">–</dd><dt>Intentos iniciales</dt><dd id="ntpAttempts">–</dd><dt>Último intento</dt><dd id="lastNtpAttempt">–</dd><dt>Respuestas aceptadas</dt><dd id="ntpResponses">–</dd><dt>Última respuesta</dt><dd id="lastNtpSync">–</dd><dt>Hora recibida</dt><dd id="ntpResponseTime">–</dd><dt>Epoch recibido</dt><dd id="ntpEpoch">–</dd><dt>Servidores consultados</dt><dd id="ntpServers">–</dd><dt>Servidor que respondió</dt><dd>No disponible en la API</dd></dl></article>
    <article class="card"><h2>Wi-Fi</h2><dl><dt>Estado</dt><dd id="wifiState">–</dd><dt>IP</dt><dd id="ip">–</dd><dt>Señal recibida</dt><dd><span id="rssi">–</span><span id="rssiMeaning" class="signal-meaning">Cuanto más cerca de 0, mejor.</span></dd><dt>Canal / BSSID</dt><dd id="radio">–</dd><dt>Gateway</dt><dd id="gateway">–</dd><dt>Acceso al gateway</dt><dd id="gatewayHealth">–</dd><dt>Última prueba</dt><dd id="gatewayPingAge">–</dd><dt>Recuperaciones</dt><dd id="wifiRecoveries">–</dd><dt>DNS</dt><dd id="dns">–</dd><dt>Última desconexión</dt><dd id="disconnect">–</dd></dl></article>
    <article class="card"><h2>Clima exterior</h2><dl><dt>Estado</dt><dd id="weatherState">–</dd><dt>Condición</dt><dd id="condition">–</dd><dt>Temperatura</dt><dd id="temperature">–</dd><dt>Humedad</dt><dd id="humidity">–</dd><dt>Viento</dt><dd id="wind">–</dd><dt>Máxima / mínima</dt><dd id="extremes">–</dd><dt>Último pedido</dt><dd id="lastWeather">–</dd></dl><div class="actions"><button id="refreshWeather">Actualizar clima</button><span id="weatherMessage" class="message"></span></div></article>
  </section>
  <div class="endpoint">Estado JSON: /api/status · actualización automática cada 2 s</div>
</main>
<script>
const $=id=>document.getElementById(id);let brightnessReady=false;
const yes=value=>value?'Sí':'No';
const duration=ms=>{const s=Math.floor(ms/1000),d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+' d ':'')+(h?h+' h ':'')+m+' min'};
const age=(now,then)=>!then?'Nunca':duration((now-then)>>>0)+' atrás';
async function loadStatus(){
  try{const response=await fetch('/api/status',{cache:'no-store'});if(!response.ok)throw Error(response.status);const data=await response.json();
    $('dot').classList.add('ok');$('connection').textContent='En línea';
    if(!brightnessReady){$('brightness').value=data.device.brightness;$('brightnessValue').value=data.device.brightness;brightnessReady=true}
    $('view').textContent=data.device.view;$('uptime').textContent=duration(data.device.uptime_ms);$('heap').textContent=data.device.free_heap+' bytes';$('minHeap').textContent=data.device.min_free_heap+' bytes';
    $('ntpStarted').textContent=yes(data.time.ntp_started);$('timeSynced').textContent=yes(data.time.synchronized);$('localTime').textContent=data.time.local||'Sin hora válida';$('ntpAttempts').textContent=data.time.attempt_count;$('lastNtpAttempt').textContent=age(data.device.uptime_ms,data.time.last_attempt_ms);$('ntpResponses').textContent=data.time.sync_count;$('lastNtpSync').textContent=age(data.device.uptime_ms,data.time.last_sync_ms);$('ntpResponseTime').textContent=data.time.last_response_local||'Ninguna';$('ntpEpoch').textContent=data.time.last_response_epoch_seconds?data.time.last_response_epoch_seconds+'.'+String(data.time.last_response_microseconds).padStart(6,'0')+' s':'–';$('ntpServers').textContent=data.time.servers.join(' · ');
    $('wifiState').textContent=data.wifi.connected?'Conectado (código '+data.wifi.status+')':'Desconectado (código '+data.wifi.status+')';$('ip').textContent=data.wifi.ip;$('rssi').textContent=data.wifi.rssi+' dBm';$('rssiMeaning').textContent=data.wifi.rssi_quality+' — '+data.wifi.rssi_explanation+' Cuanto más cerca de 0, mejor.';$('radio').textContent=data.wifi.channel+' / '+data.wifi.bssid;$('gateway').textContent=data.wifi.gateway;$('gatewayHealth').textContent=data.wifi.gateway_ping_pending?'Comprobando…':(data.wifi.gateway_ping_ok?'Disponible · '+data.wifi.gateway_ping_rtt_ms+' ms':(data.wifi.gateway_ping_completed_ms?'Sin respuesta · '+data.wifi.gateway_ping_failures+' fallos':'Todavía sin probar'));$('gatewayPingAge').textContent=age(data.device.uptime_ms,data.wifi.gateway_ping_completed_ms);$('wifiRecoveries').textContent=data.wifi.recovery_count;$('dns').textContent=data.wifi.dns;$('disconnect').textContent=data.wifi.last_disconnect_reason||'Ninguna';
    $('weatherState').textContent=data.weather.in_progress?'Consultando…':(data.weather.valid?'Dato válido':'Sin dato válido');$('condition').textContent=data.weather.condition;$('temperature').textContent=data.weather.temperature_c+' °C';$('humidity').textContent=data.weather.humidity_percent+' %';$('wind').textContent=data.weather.wind_kmh+' km/h · '+data.weather.wind_condition;$('extremes').textContent=data.weather.maximum_c+' / '+data.weather.minimum_c+' °C';$('lastWeather').textContent=age(data.device.uptime_ms,data.weather.last_request_ms);
  }catch(error){$('dot').classList.remove('ok');$('connection').textContent='Sin respuesta';}
}
$('brightness').addEventListener('input',event=>$('brightnessValue').value=event.target.value);
$('brightness').addEventListener('change',async event=>{const value=event.target.value;$('brightnessMessage').textContent='Aplicando…';try{const response=await fetch('/api/brightness?value='+value,{method:'POST'});if(!response.ok)throw Error(response.status);$('brightnessMessage').textContent='Brillo '+value+' aplicado.'}catch(error){$('brightnessMessage').textContent='No se pudo cambiar el brillo.'}});
$('refreshWeather').addEventListener('click',async()=>{const button=$('refreshWeather');button.disabled=true;$('weatherMessage').textContent='Solicitando…';try{const response=await fetch('/api/weather/refresh',{method:'POST'});const result=await response.json();if(!response.ok)throw Error(result.error||response.status);$('weatherMessage').textContent='Consulta programada.'}catch(error){$('weatherMessage').textContent='No disponible: '+error.message}finally{button.disabled=false;loadStatus()}});
loadStatus();setInterval(loadStatus,2000);
</script>
</body>
</html>
)HTML";

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

constexpr uint8_t GLYPH_PERCENT[5] = {0b000, 0b101, 0b010, 0b101, 0b000};
constexpr uint8_t GLYPH_COLON[5] = {0b000, 0b010, 0b000, 0b010, 0b000};
constexpr uint8_t GLYPH_SLASH[5] = {0b000, 0b001, 0b010, 0b100, 0b000};
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
bool webServerStarted = false;
bool timeWasSynchronized = false;
volatile bool ntpSyncLogPending = false;
uint16_t ntpAttemptCount = 0;
volatile uint16_t ntpSyncCount = 0;
uint32_t lastNtpAttemptAt = 0;
volatile uint32_t lastNtpSyncAt = 0;
int64_t lastNtpResponseEpochSeconds = 0;
int32_t lastNtpResponseMicroseconds = 0;
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
bool startupPixelsTurningOff = false;
uint8_t displayIntensity = DEFAULT_INTENSITY;
volatile uint8_t lastWifiDisconnectReason = 0;
volatile bool gatewayPingActive = false;
volatile bool gatewayPingCompleted = false;
volatile bool gatewayPingSucceeded = false;
volatile uint32_t gatewayPingRttMs = 0;
uint32_t lastGatewayPingStartedAt = 0;
uint32_t lastGatewayPingCompletedAt = 0;
uint32_t lastWifiRecoveryAt = 0;
uint8_t gatewayPingFailures = 0;
uint16_t wifiRecoveryCount = 0;
wifi_power_t currentWifiTxPower = WIFI_POWER_19_5dBm;
portMUX_TYPE weatherDataMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE ntpDataMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE networkDataMux = portMUX_INITIALIZER_UNLOCKED;
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
bool weatherRefreshRequested = false;

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
  const bool colonOn = localTime.tm_sec % 2 == 0;
  setPixel(1, 11, colonOn);
  setPixel(2, 11, colonOn);
  setPixel(4, 11, colonOn);
  setPixel(5, 11, colonOn);
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

  if (!startupPixelsTurningOff) {
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
    if (startupPixelCount == PIXEL_COUNT) startupPixelsTurningOff = true;
  } else {
    // Elegir uniformemente uno de los pixeles que todavia estan encendidos.
    uint16_t litIndex =
        static_cast<uint16_t>(random(static_cast<long>(startupPixelCount)));
    for (uint16_t pixel = 0; pixel < PIXEL_COUNT; ++pixel) {
      const uint8_t row = pixel / DISPLAY_WIDTH;
      const uint8_t column = pixel % DISPLAY_WIDTH;
      const uint32_t mask = 1UL << column;
      if ((startupPixels[row] & mask) == 0) continue;
      if (litIndex-- == 0) {
        startupPixels[row] &= ~mask;
        --startupPixelCount;
        break;
      }
    }
    if (startupPixelCount == 0) startupPixelsTurningOff = false;
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

void logNetworkDiagnostics(const char* context) {
  Serial.printf(
      "[%lu] Red (%s): estado=%d, IP=%s, mascara=%s, gateway=%s, "
      "DNS1=%s, DNS2=%s, BSSID=%s, canal=%d, RSSI=%d dBm\n",
      millis(), context, WiFi.status(), WiFi.localIP().toString().c_str(),
      WiFi.subnetMask().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP(0).toString().c_str(),
      WiFi.dnsIP(1).toString().c_str(), WiFi.BSSIDstr().c_str(),
      WiFi.channel(), WiFi.RSSI());
}

void applyPrimaryDns() {
  ip_addr_t dnsAddress;
  IP_ADDR4(&dnsAddress, PRIMARY_DNS[0], PRIMARY_DNS[1], PRIMARY_DNS[2],
           PRIMARY_DNS[3]);
  dns_setserver(0, &dnsAddress);
  const bool configured = WiFi.dnsIP(0) == PRIMARY_DNS;
  Serial.printf("DNS primario %s: %s\n", configured ? "configurado" : "fallo",
                WiFi.dnsIP(0).toString().c_str());
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_START) {
    Serial.printf("[%lu] Interfaz Wi-Fi iniciada.\n", millis());
  } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.printf("[%lu] Asociado al punto de acceso.\n", millis());
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    setWifiTxPower(WIFI_POWER_8_5dBm, "8,5 dBm (conexion)");
    const auto reason =
        static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
    lastWifiDisconnectReason = static_cast<uint8_t>(reason);
    Serial.printf("[%lu] Wi-Fi desconectado. Motivo: %u (%s)\n", millis(),
                  reason, WiFi.disconnectReasonName(reason));
  } else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
    Serial.printf("[%lu] Wi-Fi perdio la direccion IP.\n", millis());
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    setWifiTxPower(WIFI_POWER_8_5dBm, "8,5 dBm (conectado)");
    applyPrimaryDns();
    logNetworkDiagnostics("IP obtenida");
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

void onGatewayPingSuccess(esp_ping_handle_t handle, void*) {
  uint32_t rttMs = 0;
  esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &rttMs,
                       sizeof(rttMs));
  portENTER_CRITICAL(&networkDataMux);
  gatewayPingSucceeded = true;
  gatewayPingRttMs = rttMs;
  portEXIT_CRITICAL(&networkDataMux);
}

void onGatewayPingEnd(esp_ping_handle_t handle, void*) {
  esp_ping_delete_session(handle);
  portENTER_CRITICAL(&networkDataMux);
  gatewayPingActive = false;
  gatewayPingCompleted = true;
  portEXIT_CRITICAL(&networkDataMux);
}

void startGatewayPing(uint32_t now) {
  const IPAddress gateway = WiFi.gatewayIP();
  if (gateway == IPAddress(0, 0, 0, 0)) return;

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = 1;
  config.timeout_ms = 1000;
  config.interval_ms = 1000;
  IP_ADDR4(&config.target_addr, gateway[0], gateway[1], gateway[2], gateway[3]);

  esp_ping_callbacks_t callbacks{};
  callbacks.on_ping_success = onGatewayPingSuccess;
  callbacks.on_ping_end = onGatewayPingEnd;

  esp_ping_handle_t handle = nullptr;
  portENTER_CRITICAL(&networkDataMux);
  gatewayPingSucceeded = false;
  gatewayPingRttMs = 0;
  gatewayPingActive = true;
  portEXIT_CRITICAL(&networkDataMux);

  if (esp_ping_new_session(&config, &callbacks, &handle) != ESP_OK ||
      esp_ping_start(handle) != ESP_OK) {
    if (handle != nullptr) esp_ping_delete_session(handle);
    portENTER_CRITICAL(&networkDataMux);
    gatewayPingActive = false;
    portEXIT_CRITICAL(&networkDataMux);
    Serial.println("No se pudo iniciar la prueba ICMP al gateway.");
    return;
  }
  lastGatewayPingStartedAt = now;
}

void recoverWifiFromStaleLink(uint32_t now) {
  if (lastWifiRecoveryAt != 0 &&
      now - lastWifiRecoveryAt < WIFI_RECOVERY_COOLDOWN_MS) {
    return;
  }
  lastWifiRecoveryAt = now;
  ++wifiRecoveryCount;
  gatewayPingFailures = 0;
  Serial.println(
      "Gateway inaccesible con Wi-Fi conectado; reiniciando el enlace.");
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  if (webServerStarted) {
    webServer.stop();
    webServerStarted = false;
  }
  setWifiTxPower(WIFI_POWER_8_5dBm, "8,5 dBm (recuperacion)");
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiStatusLog = now;
}

void updateGatewayHealth(bool wifiConnected, uint32_t now) {
  bool active;
  bool completed;
  bool succeeded;
  portENTER_CRITICAL(&networkDataMux);
  active = gatewayPingActive;
  completed = gatewayPingCompleted;
  succeeded = gatewayPingSucceeded;
  if (completed) gatewayPingCompleted = false;
  portEXIT_CRITICAL(&networkDataMux);

  if (completed) {
    lastGatewayPingCompletedAt = now;
    if (succeeded) {
      gatewayPingFailures = 0;
    } else if (gatewayPingFailures < UINT8_MAX) {
      ++gatewayPingFailures;
      Serial.printf("Gateway sin respuesta ICMP (%u/%u).\n",
                    gatewayPingFailures, GATEWAY_PING_FAILURE_LIMIT);
    }
  }

  if (!wifiConnected) {
    gatewayPingFailures = 0;
    return;
  }
  if (gatewayPingFailures >= GATEWAY_PING_FAILURE_LIMIT) {
    recoverWifiFromStaleLink(now);
    return;
  }
  if (!active &&
      (lastGatewayPingStartedAt == 0 ||
       now - lastGatewayPingStartedAt >= GATEWAY_PING_INTERVAL_MS)) {
    startGatewayPing(now);
  }
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
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS disponible como reloj.local");
  } else {
    Serial.println("No se pudo iniciar mDNS; se reintentara.");
  }
}

void onNtpTimeSync(timeval* receivedTime) {
  const uint32_t syncAt = millis();
  portENTER_CRITICAL(&ntpDataMux);
  lastNtpSyncAt = syncAt;
  lastNtpResponseEpochSeconds = receivedTime->tv_sec;
  lastNtpResponseMicroseconds = receivedTime->tv_usec;
  ++ntpSyncCount;
  ntpSyncLogPending = true;
  portEXIT_CRITICAL(&ntpDataMux);
}

void requestNtpSync(uint32_t now) {
  const bool firstAttempt = !ntpStarted;
  lastNtpAttemptAt = now;
  ++ntpAttemptCount;

  if (firstAttempt) {
    configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    esp_sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
    ntpStarted = true;
    Serial.println("Wi-Fi conectado. Esperando la hora NTP...");
    return;
  }

  if (esp_sntp_restart()) {
    Serial.printf("Sin respuesta NTP; reintento no bloqueante #%u.\n",
                  ntpAttemptCount);
  } else {
    configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    esp_sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
    Serial.printf("SNTP no estaba activo; reinicializacion #%u.\n",
                  ntpAttemptCount);
  }
}

void updateNtp(bool wifiConnected, uint32_t now) {
  bool syncLogPending;
  uint16_t syncCount;
  portENTER_CRITICAL(&ntpDataMux);
  syncLogPending = ntpSyncLogPending;
  ntpSyncLogPending = false;
  syncCount = ntpSyncCount;
  portEXIT_CRITICAL(&ntpDataMux);

  if (syncLogPending) {
    Serial.printf("Respuesta NTP recibida; sincronizacion #%u.\n", syncCount);
  }
  if (!wifiConnected) return;

  tm localTime{};
  if (readLocalTime(localTime)) return;
  if (!ntpStarted || now - lastNtpAttemptAt >= NTP_INITIAL_RETRY_MS) {
    requestNtpSync(now);
  }
}

void fetchWeatherTask(void*) {
  WiFiClient networkClient;
  networkClient.setTimeout(8);

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  bool success = false;
  logNetworkDiagnostics("consulta meteorologica");
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
    if (weatherHasValidData) {
      Serial.println("Se conserva el ultimo dato meteorologico valido.");
    } else {
      Serial.println("Todavia no hay un dato meteorologico valido.");
    }
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
  if (!weatherRefreshRequested && lastWeatherRequestAt != 0 &&
      now - lastWeatherRequestAt < interval) {
    return;
  }

  weatherRefreshRequested = false;
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

const char* clockViewName(ClockView view) {
  switch (view) {
    case ClockView::DATE: return "FECHA";
    case ClockView::TEMPERATURE: return "TEMPERATURA";
    case ClockView::HUMIDITY: return "HUMEDAD";
    case ClockView::WEATHER: return "CONDICION";
    case ClockView::MAXIMUM: return "MAXIMA";
    case ClockView::MINIMUM: return "MINIMA";
    case ClockView::WIND: return "VIENTO";
    case ClockView::TIME: return "HORA";
  }
  return "DESCONOCIDA";
}

const char* wifiSignalQuality(int32_t rssi) {
  if (rssi >= -55) return "Excelente";
  if (rssi >= -67) return "Muy buena";
  if (rssi >= -74) return "Buena";
  if (rssi >= -85) return "Regular";
  if (rssi >= -92) return "Débil";
  return "Muy débil";
}

const char* wifiSignalExplanation(int32_t rssi) {
  if (rssi >= -55) return "Señal muy fuerte y con amplio margen.";
  if (rssi >= -67) return "Enlace estable con buen margen.";
  if (rssi >= -74) return "Conexión útil, todavía cerca del rango de alta velocidad.";
  if (rssi >= -85) return "Puede reducir la velocidad o volverse sensible al ruido.";
  if (rssi >= -92) return "Sólo quedan los modos más robustos y lentos.";
  return "Está cerca del límite de recepción del módulo.";
}

void sendStatusJson() {
  const uint32_t now = millis();
  tm localTime{};
  const bool validTime = readLocalTime(localTime);
  char localTimeText[20] = {};
  if (validTime) {
    strftime(localTimeText, sizeof(localTimeText), "%Y-%m-%d %H:%M:%S",
             &localTime);
  }

  int16_t temperature;
  uint8_t humidity;
  uint8_t code;
  uint16_t wind;
  bool isDay;
  int16_t maximum;
  int16_t minimum;
  bool validWeather;
  portENTER_CRITICAL(&weatherDataMux);
  temperature = weatherTemperatureC;
  humidity = weatherHumidityPercent;
  code = weatherCode;
  wind = weatherWindKmh;
  isDay = weatherIsDay;
  maximum = weatherMaximumC;
  minimum = weatherMinimumC;
  validWeather = weatherHasValidData;
  portEXIT_CRITICAL(&weatherDataMux);

  JsonDocument document;
  JsonObject device = document["device"].to<JsonObject>();
  device["uptime_ms"] = now;
  device["free_heap"] = ESP.getFreeHeap();
  device["min_free_heap"] = ESP.getMinFreeHeap();
  device["brightness"] = displayIntensity;
  if (WiFi.status() != WL_CONNECTED) {
    device["view"] = "ESPERA WIFI";
  } else if (!validTime) {
    device["view"] = "ESPERA NTP";
  } else {
    device["view"] = clockViewName(currentView(now));
  }

  JsonObject wifi = document["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["status"] = static_cast<int>(WiFi.status());
  wifi["ip"] = WiFi.localIP().toString();
  wifi["gateway"] = WiFi.gatewayIP().toString();
  wifi["dns"] = WiFi.dnsIP(0).toString();
  wifi["bssid"] = WiFi.BSSIDstr();
  wifi["channel"] = WiFi.channel();
  const int32_t rssi = WiFi.RSSI();
  wifi["rssi"] = rssi;
  wifi["rssi_quality"] = wifiSignalQuality(rssi);
  wifi["rssi_explanation"] = wifiSignalExplanation(rssi);
  const uint8_t disconnectReason = lastWifiDisconnectReason;
  wifi["last_disconnect_reason"] =
      disconnectReason == 0
          ? ""
          : WiFi.disconnectReasonName(
                static_cast<wifi_err_reason_t>(disconnectReason));
  bool pingActive;
  bool pingSucceeded;
  uint32_t pingRttMs;
  portENTER_CRITICAL(&networkDataMux);
  pingActive = gatewayPingActive;
  pingSucceeded = gatewayPingSucceeded;
  pingRttMs = gatewayPingRttMs;
  portEXIT_CRITICAL(&networkDataMux);
  wifi["gateway_ping_pending"] = pingActive;
  wifi["gateway_ping_ok"] = pingSucceeded && gatewayPingFailures == 0;
  wifi["gateway_ping_rtt_ms"] = pingRttMs;
  wifi["gateway_ping_completed_ms"] = lastGatewayPingCompletedAt;
  wifi["gateway_ping_failures"] = gatewayPingFailures;
  wifi["recovery_count"] = wifiRecoveryCount;

  JsonObject time = document["time"].to<JsonObject>();
  time["ntp_started"] = ntpStarted;
  time["synchronized"] = validTime;
  time["local"] = localTimeText;
  time["attempt_count"] = ntpAttemptCount;
  time["last_attempt_ms"] = lastNtpAttemptAt;
  uint16_t syncCount;
  uint32_t syncAt;
  int64_t responseEpochSeconds;
  int32_t responseMicroseconds;
  portENTER_CRITICAL(&ntpDataMux);
  syncCount = ntpSyncCount;
  syncAt = lastNtpSyncAt;
  responseEpochSeconds = lastNtpResponseEpochSeconds;
  responseMicroseconds = lastNtpResponseMicroseconds;
  portEXIT_CRITICAL(&ntpDataMux);
  time["sync_count"] = syncCount;
  time["last_sync_ms"] = syncAt;
  time["last_response_epoch_seconds"] = responseEpochSeconds;
  time["last_response_microseconds"] = responseMicroseconds;
  char responseUtc[24] = {};
  char responseLocal[24] = {};
  if (responseEpochSeconds > 0) {
    const time_t responseTime = static_cast<time_t>(responseEpochSeconds);
    tm responseUtcTime{};
    tm responseLocalTime{};
    gmtime_r(&responseTime, &responseUtcTime);
    localtime_r(&responseTime, &responseLocalTime);
    strftime(responseUtc, sizeof(responseUtc), "%Y-%m-%d %H:%M:%S UTC",
             &responseUtcTime);
    strftime(responseLocal, sizeof(responseLocal), "%Y-%m-%d %H:%M:%S",
             &responseLocalTime);
  }
  time["last_response_utc"] = responseUtc;
  time["last_response_local"] = responseLocal;
  JsonArray ntpServers = time["servers"].to<JsonArray>();
  ntpServers.add(NTP_SERVER_1);
  ntpServers.add(NTP_SERVER_2);
  ntpServers.add(NTP_SERVER_3);
  time["initial_retry_ms"] = NTP_INITIAL_RETRY_MS;
  time["sync_interval_ms"] = NTP_SYNC_INTERVAL_MS;

  JsonObject weather = document["weather"].to<JsonObject>();
  weather["valid"] = validWeather;
  weather["in_progress"] = weatherRequestInProgress;
  weather["last_request_ms"] = lastWeatherRequestAt;
  weather["temperature_c"] = temperature;
  weather["humidity_percent"] = humidity;
  weather["code"] = code;
  weather["condition"] =
      validWeather ? weatherDescription(code, isDay) : "SIN DATO";
  weather["wind_kmh"] = wind;
  weather["wind_condition"] =
      validWeather ? windDescription(wind) : "SIN DATO";
  weather["maximum_c"] = maximum;
  weather["minimum_c"] = minimum;
  weather["is_day"] = isDay;

  String response;
  response.reserve(900);
  serializeJson(document, response);
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", response);
}

void handleBrightnessChange() {
  if (!webServer.hasArg("value")) {
    webServer.send(400, "application/json", "{\"error\":\"falta value\"}");
    return;
  }

  const String valueText = webServer.arg("value");
  if (valueText.length() == 0 || valueText.length() > 2) {
    webServer.send(400, "application/json", "{\"error\":\"brillo invalido\"}");
    return;
  }
  for (size_t index = 0; index < valueText.length(); ++index) {
    if (!isDigit(valueText[index])) {
      webServer.send(400, "application/json", "{\"error\":\"brillo invalido\"}");
      return;
    }
  }

  const int value = valueText.toInt();
  if (value < 0 || value > DISPLAY_MAX_INTENSITY) {
    webServer.send(400, "application/json", "{\"error\":\"use un valor de 0 a 15\"}");
    return;
  }

  displayIntensity = static_cast<uint8_t>(value);
  matrix.control(MD_MAX72XX::INTENSITY, displayIntensity);
  Serial.printf("Brillo cambiado desde la web: %u/15\n", displayIntensity);
  String response = "{\"brightness\":";
  response += displayIntensity;
  response += '}';
  webServer.send(200, "application/json", response);
}

void handleWeatherRefresh() {
  if (!timeWasSynchronized) {
    webServer.send(409, "application/json",
                   "{\"error\":\"la hora NTP aun no es valida\"}");
    return;
  }
  if (weatherRequestInProgress) {
    webServer.send(409, "application/json",
                   "{\"error\":\"ya hay una consulta en curso\"}");
    return;
  }
  weatherRefreshRequested = true;
  webServer.send(202, "application/json", "{\"queued\":true}");
}

void configureWebServer() {
  webServer.on("/", HTTP_GET, []() {
    webServer.sendHeader("Cache-Control", "no-store");
    webServer.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
  });
  webServer.on("/api/status", HTTP_GET, sendStatusJson);
  webServer.on("/api/brightness", HTTP_POST, handleBrightnessChange);
  webServer.on("/api/weather/refresh", HTTP_POST, handleWeatherRefresh);
  webServer.onNotFound([]() {
    webServer.send(404, "application/json", "{\"error\":\"ruta inexistente\"}");
  });
}

void updateWebServer(bool wifiConnected) {
  if (!wifiConnected) {
    if (webServerStarted) {
      webServer.stop();
      webServerStarted = false;
      Serial.println("Web detenida hasta recuperar Wi-Fi.");
    }
    return;
  }
  if (!webServerStarted) {
    webServer.begin();
    webServerStarted = true;
    Serial.println("Web de control disponible en http://reloj.local/");
  }
  webServer.handleClient();
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
  matrix.control(MD_MAX72XX::INTENSITY, displayIntensity);
  matrix.clear();
  matrix.update(MD_MAX72XX::OFF);
  esp_sntp_set_time_sync_notification_cb(onNtpTimeSync);
  configureWebServer();
  beginWifi();
}

void loop() {
  const uint32_t now = millis();
  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  updateGatewayHealth(wifiConnected, now);
  wifiConnected = WiFi.status() == WL_CONNECTED;
  updateMdns(wifiConnected, now);
  updateWebServer(wifiConnected);
  updateNtp(wifiConnected, now);
  if (strlen(WIFI_SSID) > 0 && !wifiConnected &&
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
