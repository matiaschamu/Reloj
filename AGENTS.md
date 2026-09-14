# AGENTS.md — Reloj ESP32-C3

## Objetivo

Construir un reloj con un ESP32-C3 y una matriz LED 8x32 formada por cuatro
módulos MAX7219 en serie. El reloj se conecta a Wi-Fi, obtiene la hora por NTP y
la mantiene sincronizada. La temperatura, humedad, condición del tiempo y
viento exterior se obtienen de Open-Meteo usando coordenadas fijas.

## Entorno

- PlatformIO con framework Arduino.
- Entorno normal: `esp32-c3-devkitm-1`.
- Entorno de depuración: `esp32-c3-debug`.
- Bibliotecas: `majicdesigns/MD_MAX72XX @ ^3.5.1` y
  `bblanchon/ArduinoJson @ ^7.0.0`.
- Monitor serie: 115200 baudios.
- Fuente principal: `src/main.cpp`.
- Plantilla Wi-Fi: `include/secrets.example.h`.
- Credenciales locales: `include/secrets.h`, ignorado por Git.
- Registro técnico: `docs/LECCIONES_APRENDIDAS.md`. Actualizarlo cuando una
  prueba de hardware cambie una conclusión o revele una limitación nueva.

Si cambia el modelo físico de placa, actualizar únicamente `board` en
`platformio.ini` después de confirmar su identificador de PlatformIO.

## Cableado confirmado

| Señal MAX7219 | ESP32-C3 |
| --- | --- |
| DIN | GPIO6 |
| CLK | GPIO4 |
| CS / LOAD | GPIO7 |
| GND | GND |
| VCC | 5 V |

El primer conector debe ser el de entrada. Encadenar `DOUT` hacia `DIN` hasta el
cuarto módulo. Usar preferentemente una fuente externa regulada de 5 V y unir su
GND con el GND del ESP32-C3. No alimentar matrices desde GPIO. Para un montaje
definitivo, considerar un 74AHCT125 entre las señales de 3,3 V y el MAX7219.

## Geometría y orientación

- Display: 8 filas por 32 columnas.
- Hardware MD_MAX72XX: `FC16_HW`.
- Cantidad de controladores: 4.
- Orden físico confirmado: izquierda a derecha, sin invertir dispositivos.
- La matriz está invertida verticalmente respecto de las coordenadas lógicas.
- `FLIP_VERTICAL` debe permanecer en `true` con el montaje actual.
- Todas las primitivas deben dibujar mediante `setPixel()`, que aplica la
  transformación vertical. No corregir la orientación dentro de cada fuente.
- Brillo inicial: 2 sobre 15.

## Web local

- Servir el panel de control en `http://reloj.local/` mientras haya Wi-Fi.
- Permitir brillo de 0 a 15 sin persistirlo; cada reinicio vuelve al valor 2.
- Exponer diagnóstico sin secretos mediante `GET /api/status` y aceptar cambios
  de brillo solamente por `POST /api/brightness?value=0..15`.
- Mostrar el RSSI en dBm con su signo y una interpretación orientativa: desde
  -55 excelente, desde -67 muy buena, desde -74 buena, desde -85 regular, desde
  -92 débil y por debajo muy débil. Aclarar que más cerca de cero es mejor.
- `POST /api/weather/refresh` debe reutilizar la tarea meteorológica existente;
  nunca ejecutar HTTP de Open-Meteo dentro del manejador web.
- Atender el servidor desde `loop()` sin `delay()` y detenerlo al perder Wi-Fi.
- Probar el gateway por ICMP cada 30 segundos mediante la tarea interna de ping.
  Si falla tres veces mientras `WiFi.status()` todavía indica conexión, reiniciar
  deliberadamente el enlace, con al menos cinco minutos entre recuperaciones.
- El servidor no tiene autenticación: mantenerlo limitado a la red local y no
  documentar ni recomendar la publicación del puerto 80 en Internet.

## Fuentes y vistas

- `BIG_DIGITS`: fuente 4x7 gruesa, estilo segmentos, usada para horas y
  minutos. Conservar los cuatro píxeles de ancho para mantener separaciones.
- `SMALL_DIGITS`: fuente 3x5 de segmentos llenos usada para segundos, fecha y
  mediciones; mantener barras completas en 2, 3, 5, 6, 8 y 9.
- `SMALL_LETTERS`: alfabeto latino 3x5 completo en mayúsculas. Los textos de
  estado deben escribirse sin tildes y limitarse a 8 caracteres para caber.
- Los dos puntos del reloj tienen dos píxeles verticales por marca
  (filas 1-2 y 4-5) para resultar visibles a distancia.
- Vista principal: `HH:MM SS`.
- Cada 25.000 ms desde el inicio de la secuencia anterior se muestran siete
  páginas consecutivas:
  1. `DD/MM/AA` durante 3000 ms.
  2. `T:0C` durante 2000 ms.
  3. `H:0%` durante 2000 ms.
  4. Condición meteorológica durante 2000 ms.
  5. Máxima diaria `MAX:nC` durante 2000 ms.
  6. Mínima diaria `MIN:nC` durante 2000 ms.
  7. Intensidad del viento durante 2000 ms.
- Después vuelve a la hora.
- Hasta obtener el primer dato meteorológico válido se muestran valores cero.
  Después se conserva el último valor correcto aunque una consulta falle.

## Clima exterior

- Coordenadas: latitud `-38.114864`, longitud `-57.607937`.
- Endpoint: Open-Meteo Forecast API con los campos actuales
  `temperature_2m`, `relative_humidity_2m`, `weather_code`, `wind_speed_10m`
  e `is_day`; campos diarios `temperature_2m_max` y `temperature_2m_min`.
- Pedir un solo día con `forecast_days=1` y la zona
  `America/Argentina/Buenos_Aires` para que máxima y mínima correspondan al día
  local, no al día UTC.
- Actualizar cada 15 minutos. Antes del primer éxito, reintentar cada minuto.
- Iniciar consultas solamente con Wi-Fi y hora NTP válidos.
- Ejecutar HTTP y análisis JSON en la tarea `weather-fetch`; nunca bloquear el
  refresco del reloj esperando la red.
- Redondear la temperatura al entero más cercano y aceptar humedad de 0 a 100.
- Validar máxima y mínima entre -99 y 99 °C y exigir que la mínima no supere la
  máxima antes de publicar el conjunto meteorológico.
- Traducir el código WMO a uno de estos textos: `SOLEADO`/`CLARO`, `PARCIAL`,
  `NUBLADO`, `NIEBLA`, `LLOVIZNA`, `LLUVIA`, `NIEVE`, `CHUBASCO`, `NEVANDO`,
  `TORMENTA` o `GRANIZO`. Los códigos desconocidos muestran `SIN DATO`.
- Clasificar el viento en km/h: menos de 4 `CALMO`, menos de 16 `BRISA`, menos
  de 30 `VIENTO`, menos de 50 `VENTOSO`; desde 50 `FUERTE`.
- Se usa HTTP simple porque Open-Meteo lo admite y sólo se reciben datos públicos
  sin credenciales ni acciones sensibles. Si el endpoint transporta información
  privada en el futuro, migrar a HTTPS con una CA raíz validada.
- Estos valores representan el exterior estimado por un modelo meteorológico,
  no la temperatura ni humedad dentro de la habitación.

La lógica de vistas usa `millis()` y no debe introducir `delay()` ni bloquear el
bucle. Durante las animaciones se prepara un cuadro cada 80 ms; esta comprobación
periódica no implica una escritura física al display.

El dibujo se realiza en `nextFrame`, se compara contra `currentFrame` y
`commitFrame()` llama al MAX7219 solamente para píxeles diferentes. Las
actualizaciones automáticas de MD_MAX72XX permanecen deshabilitadas; una llamada
manual a `matrix.update()` transmite solo las filas marcadas. Conservar este
modelo dirty-framebuffer para evitar tráfico SPI cuando la imagen no cambia. El
MAX7219 escribe registros completos de 8 bits, por lo que una fila afectada es la
granularidad física mínima aunque haya cambiado un solo píxel.

## Wi-Fi y NTP

- Definir `WIFI_SSID` y `WIFI_PASSWORD` solamente en `include/secrets.h`.
- Nunca imprimir ni versionar la contraseña.
- Sin credenciales, el firmware debe seguir compilando y mostrar la animación de
  espera Wi-Fi.
- El Wi-Fi funciona en modo estación con reconexión automática. No llamar
  periódicamente a `WiFi.reconnect()`: reinicia una asociación en curso y
  genera `ASSOC_FAIL`/motivo 205. Cada 20 segundos se informa el estado sin
  alterar la máquina de conexión.
- Registrar por `Serial` la IP al conectar y el código de motivo al desconectar;
  `202` es `WIFI_REASON_AUTH_FAIL` y `2` es `WIFI_REASON_AUTH_EXPIRE`.
- El ESP32-C3 solo admite 2,4 GHz. Para el SSID `Domotics` se validó señal fuerte,
  WPA2-Personal y canal 9; una secuencia que termina en motivo `202` indica que
  deben revisarse primero la contraseña o la configuración de autenticación.
- El AP `MATIAS` fue detectado con RSSI -49 dBm, canal 1 y modo
  `WIFI_AUTH_WPA_WPA2_PSK` (valor 4), es decir, WPA/WPA2 mixto. Se usa
  `WIFI_AUTH_WPA_PSK` como umbral de compatibilidad. La solución preferida en
  el router es WPA2-Personal con AES/CCMP puro.
- Usar `WIFI_ALL_CHANNEL_SCAN` y ordenar por señal para no elegir el primer AP
  o repetidor que anuncie el mismo SSID.
- Mantener `WiFi.setSleep(false)` mientras se diagnostique `AUTH_EXPIRE`; es
  una prueba recomendada por Espressif cuando el AP no responde al pedido de
  autenticación.
- Mantener `WIFI_POWER_8_5dBm` durante asociación, conexión y reconexión. Con
  RSSI de -37 dBm hay margen de sobra y se observó que, tras subir a 19,5 dBm,
  el ESP conservaba asociación e IP pero dejaba de cursar tráfico IP. La versión
  fija en 8,5 dBm restauró ping, HTTP y conectividad general en hardware. No
  volver a 19,5 dBm sin confirmar ping, HTTP y NTP sostenidos.
- La combinación de escaneo completo, umbral WPA compatible, modem-sleep
  desactivado y potencia de 8,5 dBm quedó verificada en hardware: conectó al
  BSSID `A0:F4:79:B8:31:1C`, canal 1, con RSSI -49 dBm; obtuvo IP
  `192.168.100.77`, inició `reloj.local` y sincronizó NTP. No se aisló todavía
  cuál de los cambios resolvió por sí solo la autenticación, por lo que no
  retirar el escaneo completo, el umbral WPA ni la potencia reducida durante la
  conexión sin repetir la prueba física.
- El escaneo diagnóstico bloqueante se retiró después de identificar el AP; no
  reintroducirlo en `setup()`, porque impediría avanzar la animación de inicio.
  El controlador sigue usando internamente el escaneo completo no bloqueante.
- La fuente externa de 5 V y el GND común son necesarios para las matrices, pero
  no se consideran ya la causa confirmada del fallo Wi-Fi: el problema reapareció
  con la alimentación mejorada y capacitores de desacoplo.
- Zona POSIX: `<-03>3`, equivalente a Argentina UTC-3 sin horario de verano.
- Servidores NTP: `ntp2.hidro.gob.ar` (Observatorio Naval),
  `ntp.inti.gob.ar` (INTI) y `time.cloudflare.com` (respaldo global). Los tres
  respondieron 3 de 3 consultas desde la red de instalación y usan tiempo UTC
  convencional, sin mezclarlo con el leap-smear de Google.
- Intervalo SNTP: 3.600.000 ms, una hora.
- `ntpStarted` indica solamente que SNTP fue configurado, no que llegó una
  respuesta. Antes de la primera hora válida, reiniciar SNTP cada 30 segundos de
  forma no bloqueante y registrar cada intento. Después del primer éxito, el
  intervalo normal de resincronización permanece en una hora.
- La callback de sincronización debe conservar para la web la última marca de
  tiempo aceptada, en hora local, UTC y epoch con microsegundos, además del total
  de respuestas. La API SNTP disponible no identifica cuál servidor contestó
  ni expone el paquete recibido; indicarlo explícitamente en el panel.
- Antes de una hora válida, mostrar la animación NTP. No mostrar una hora basada
  en el valor inicial del sistema.
- Si Wi-Fi cae después de sincronizar, el reloj local debe continuar y Wi-Fi
  debe intentar reconectarse en segundo plano.
- No asumir que `WL_CONNECTED` garantiza tráfico: se observó el bucle activo y
  reintentando NTP mientras el equipo no figuraba por ARP ni respondía ping/HTTP.
  La comprobación ICMP del gateway distingue este enlace fantasma de un fallo
  exclusivo de NTP o del servidor web.
- Una vez conectado, anunciar `reloj.local` con ESPmDNS y usar también `reloj`
  como hostname DHCP. Detener mDNS al perder Wi-Fi y reiniciarlo al reconectar.

## Animaciones de estado

- Durante la conexión Wi-Fi y la espera NTP, encender exactamente un píxel
  apagado elegido al azar por cada cuadro de 80 ms.
- Los píxeles se acumulan sin repetirse. Al llegar a 256, limpiar el estado de
  la animación e iniciar una nueva secuencia.
- Inicializar la secuencia aleatoria una vez en `setup()` con `esp_random()`.
- No reemplazar estas animaciones con esperas bloqueantes.

## Comandos de trabajo

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

Si `pio` no figura en el PATH de Windows:

```powershell
& 'C:\Users\Matias\.platformio\penv\Scripts\platformio.exe' run
```

Antes de entregar cambios, ejecutar al menos `pio run`. No subir firmware sin
pedido del usuario y sin confirmar que la placa correcta está conectada.

## Depuración en VS Code

- `esp32-c3-debug` hereda toda la configuración del entorno normal.
- Compila con `build_type = debug`, `-Og`, símbolos GDB nivel 3 y `ggdb3`.
- Usa `debug_tool = esp-builtin`, pero carga mediante `upload_protocol =
  esptool` por `COM4`. JTAG se reserva para la sesión de depuración.
- Usa JTAG a 40 MHz (`debug_speed = 40000`), la frecuencia base máxima anunciada
  por el USB/JTAG integrado. Con la alimentación corregida, OpenOCD completó
  pruebas escalonadas a 10, 20 y 40 MHz, con tres ciclos de pausa/continuación
  en cada velocidad, sin `LIBUSB_ERROR_PIPE`. GDB también leyó registros y
  ejecutó un paso de instrucción a 40 MHz. No se validó todavía una sesión larga.
- El `LIBUSB_ERROR_PIPE` anterior apareció durante una carga JTAG larga. Se
  conserva la carga por `esptool` y se usa JTAG solamente para depurar.
- Una nueva prueba escribió por JTAG a 40 MHz una imagen de aplicación de
  950.272 bytes, a unos 78 KB/s, y finalizó con `Verify OK`. Es un primer éxito
  de carga con la alimentación corregida; conservar por ahora el flujo habitual
  por `esptool` hasta repetir cargas y sesiones largas sin fallos.
- Usa `debug_load_mode = manual`; la tarea de VS Code carga primero el firmware
  debug mediante el bootloader serie y OpenOCD no vuelve a escribir la flash.
- Los dos entornos definen `ARDUINO_USB_MODE=1` y
  `ARDUINO_USB_CDC_ON_BOOT=1`; `Serial` debe ser `HWCDC` sobre el periférico USB
  Serial/JTAG y no `HardwareSerial` UART0.
- Usa la copia local de `tool-openocd-esp32` 2.1200.20260703 mediante
  `platform_packages`. Después de clonar o borrar `.pio`, ejecutar
  `powershell -ExecutionPolicy Bypass -File tools\install-debug-openocd.ps1`
  antes de compilar el entorno debug.
- No volver a habilitar la carga automática de OpenOCD sin revisar las rutas:
  PlatformIO 6.1 genera `"{C:/ruta con espacios/...}"`; OpenOCD 20260703 toma
  las llaves literalmente y devuelve `Protocol error with Rcmd`.
- El inicio ejecuta `tbreak setup`, por lo que la primera pausa ocurre al entrar
  en `setup()`.
- `.vscode/launch.json` apunta expresamente a
  `.pio/build/esp32-c3-debug/firmware.elf`. Mantener esa ruta: algunas versiones
  de PlatformIO lo regeneran apuntando por error al ELF del entorno normal.
- La extensión recomendada es `platformio.platformio-ide`.
- Para compilar sin iniciar una sesión: `pio run --environment esp32-c3-debug`.
- En VS Code se inicia mediante Run and Debug, configuración `PIO Debug`, o F5.
- `debug_extra_cmds` traduce la ruta de compilación de Arduino en CI hacia los
  encabezados locales de PlatformIO. Conservar el mapeo mientras se use esta
  versión del framework.
- El depurador integrado necesita el USB Serial/JTAG nativo del ESP32-C3 en
  GPIO18 (D-) y GPIO19 (D+). Confirmar que la placa y su conector lo exponen.
- No configurar un adaptador JTAG externo sin revisar el cableado: sus señales
  predeterminadas usan GPIO4, GPIO5, GPIO6 y GPIO7, tres de los cuales están
  ocupados actualmente por el MAX7219.
- Para fallos sin JTAG, el entorno normal habilita el filtro
  `esp32_exception_decoder` en el monitor serie.
- El controlador Windows correcto quedó instalado como `USB JTAG debug unit`,
  proveedor `libwdi`, INF `oem176.inf`. El anterior `winusb.inf` genérico hacía
  que OpenOCD devolviera `LIBUSB_ERROR_NOT_SUPPORTED` y
  `LIBUSB_ERROR_NOT_FOUND`.
- La conexión fue validada con OpenOCD: detectó el serial
  USB, el TAP `esp32c3.cpu` y un núcleo RISC-V XLEN=32.
- El entorno debug fija el paquete oficial de Espressif
  `openocd-esp32-win64-0.12.0-esp32-20260703.zip`. No volver a la versión de
  PlatformIO de 2023: las versiones posteriores a octubre de 2024 corrigen
  panics espontáneos del watchdog durante step/continue en RISC-V.
- La advertencia de GDB sobre el directorio del index cache es inofensiva y no
  debe confundirse con un fallo JTAG.
- Un `SIGINT` al usar el botón de pausa es normal. Si FreeRTOS estaba inactivo,
  puede detenerse en `semihosting_call_noerrno()` en lugar de una línea de la
  aplicación.
- Después de una carga JTAG interrumpida, `Abstractct.busy appears stuck` puede
  persistir incluso tras un reset. Pedir un ciclo físico de alimentación USB
  antes de volver a diagnosticar JTAG.

## Estado validado

El firmware Wi-Fi/NTP, clima y todas las vistas compila correctamente con
PlatformIO, Arduino ESP32 2.0.17, MD_MAX72XX 3.5.1 y ArduinoJson 7.4.3. La
compilación debug validada usa 41.132 bytes de RAM (12,6 %) y 863.866 bytes de
flash (65,9 %). En hardware obtuvo IP, sincronizó NTP, resolvió
`api.open-meteo.com` como `188.40.99.226` y actualizó correctamente todos los
campos meteorológicos; en la última prueba fueron 6 °C, 63 %, código WMO 3,
viento de 17 km/h, noche, máxima de 10 °C y mínima de 5 °C. Esos datos producen
las vistas `NUBLADO`, `MAX:10C`, `MIN:5C` y `VIENTO`.

## Próximos pasos

1. Probar durante varias horas la reconexión Wi-Fi y actualización NTP.
2. Validar en hardware la legibilidad de todas las palabras meteorológicas.
3. Opcional: integrar un sensor si se necesitan condiciones interiores reales.

## Reglas para cambios futuros

- Preservar pines, orden y orientación salvo cambio físico indicado.
- Mantener secretos fuera del repositorio.
- Usar lógica no bloqueante basada en `millis()`.
- Documentar cambios de placa, pines, servidor, zona horaria, formatos o sensor.
- Preservar modificaciones del usuario que no estén relacionadas con la tarea.
