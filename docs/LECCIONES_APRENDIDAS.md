# Lecciones aprendidas

Este documento conserva las conclusiones obtenidas durante el armado y las
pruebas del reloj. Describe qué funcionó, qué falló y por qué se mantuvieron
determinadas decisiones. `README.md` explica cómo usar el proyecto y
`AGENTS.md` contiene las reglas operativas para modificarlo.

## Hardware y matriz MAX7219

- El montaje probado usa `DIN=GPIO6`, `CLK=GPIO4` y `CS=GPIO7`, con cuatro
  módulos FC-16 encadenados desde `DOUT` hacia el siguiente `DIN`.
- El orden físico de los módulos era correcto; la imagen estaba invertida de
  arriba hacia abajo. La corrección adecuada fue transformar la fila en
  `commitFrame()` mediante `FLIP_VERTICAL`, sin invertir módulos ni deformar
  cada glifo por separado.
- Cuatro matrices necesitan una fuente regulada de 5 V con margen y masa común
  con el ESP32-C3. Los capacitores de desacoplo ayudan frente a ruido y picos,
  pero no resolvieron por sí solos el problema de asociación Wi-Fi.
- El MAX7219 recibe niveles lógicos de 3,3 V en el prototipo, aunque su
  especificación a 5 V no garantiza ese nivel como alto. Para un equipo
  definitivo conviene agregar un adaptador 74AHCT125.

## Renderizado y legibilidad

- Redibujar toda la pantalla periódicamente generaba tráfico SPI innecesario.
  Se adoptaron dos framebuffers de 8 filas por 32 bits: cada cuadro se compara
  con el anterior y sólo se modifican los píxeles diferentes. `matrix.update()`
  se ejecuta únicamente si hubo cambios.
- El MAX7219 transmite registros completos de fila. Aunque la comparación se
  hace por píxel, una fila de 8 bits es la unidad física mínima que termina
  enviándose al controlador afectado.
- Los primeros números finos eran difíciles de leer. La vista horaria usa
  dígitos gruesos 4x7 y los segundos/datos usan 3x5 con segmentos completos.
  Los dos puntos principales se engrosaron a dos píxeles por marca.
- Las condiciones meteorológicas exigieron un alfabeto 3x5 completo. En 32
  columnas caben como máximo ocho caracteres con una columna de separación;
  por eso se usan palabras breves, mayúsculas y sin tildes.
- La animación inicial enciende un píxel apagado al azar cada 80 ms, sin repetir
  hasta completar los 256. No usa `delay()`, por lo que la conexión continúa
  progresando en segundo plano.

## Wi-Fi: síntomas y solución conservada

- Las credenciales eran correctas y otros ESP se asociaban al mismo router,
  pero el ESP32-C3 alternaba principalmente `AUTH_EXPIRE` (motivo 2) y
  `AUTH_FAIL` (motivo 202). Las comillas de los macros no eran el problema.
- El ESP32-C3 sólo opera en 2,4 GHz. El AP probado anunciaba WPA/WPA2 mixto y
  varios equipos o repetidores podían compartir SSID.
- La combinación que finalmente se validó fue:
  - seguridad mínima compatible con WPA (`WIFI_AUTH_WPA_PSK`);
  - escaneo de todos los canales y selección por mejor señal;
  - ahorro de energía Wi-Fi desactivado;
  - potencia de transmisión de 8,5 dBm durante asociación;
  - reconexión automática del controlador, sin llamadas periódicas a
    `WiFi.reconnect()`;
  - potencia de 8,5 dBm también después de obtener IP; 19,5 dBm queda retirado
    hasta validar tráfico sostenido con ese nivel.
- No se aisló experimentalmente cuál de esas medidas fue suficiente por sí
  sola. Se conservan juntas porque el conjunto conectó y se recuperó en el
  hardware real; retirar una requiere repetir una prueba controlada.
- Una fuente adecuada sigue siendo necesaria para la matriz, pero la falla de
  autenticación reapareció aun después de mejorarla. No debe presentarse la
  alimentación como causa única confirmada.

## Hora, red local y continuidad

- La hora se obtiene con SNTP desde tres servidores y el intervalo solicitado
  de resincronización es una hora. Se valida el año antes de abandonar la
  animación de inicio para no mostrar la época inicial del sistema.
- No existe un servidor público con disponibilidad garantizada al 100 %. Se
  eligieron `ntp2.hidro.gob.ar`, `ntp.inti.gob.ar` y `time.cloudflare.com`: dos
  referencias argentinas oficiales independientes y un respaldo anycast global.
  Los tres resolvieron DNS y contestaron 3 de 3 solicitudes NTP desde la red de
  instalación. En la misma prueba, `ntp.ign.gob.ar` y
  `ntp.aggo-conicet.gob.ar` resolvieron pero no contestaron 3 de 3 solicitudes.
- Se retiró `time.google.com` porque Google aplica leap-smear y recomienda no
  combinarlo con servidores NTP convencionales. Cloudflare, el pool NTP y las
  fuentes oficiales argentinas utilizan el tratamiento NTP convencional.
- `ntpStarted` no confirma una sincronización: sólo confirma que se llamó a
  `configTzTime()`. La implementación inicial no tenía un camino propio de
  reintento si la primera respuesta no llegaba. Ahora, mientras el reloj siga
  sin una hora válida y haya Wi-Fi, `esp_sntp_restart()` fuerza un nuevo intento
  cada 30 segundos sin bloquear `loop()`. Una callback registra también cada
  respuesta, incluidas las resincronizaciones horarias posteriores.
- La callback SNTP recibe el `timeval` aceptado. Se conserva la última respuesta
  como epoch con microsegundos y se deriva de ella la representación local y
  UTC para `/api/status`. Este dato confirma una respuesta real; el wrapper SNTP
  disponible no expone el servidor de origen, stratum, retardo ni el paquete
  UDP crudo, así que no se deben presentar esos valores como si fueran conocidos.
- La zona POSIX `<-03>3` representa Argentina UTC-3 sin horario de verano.
- Una vez sincronizado, el reloj local continúa avanzando aunque se pierda
  Wi-Fi. La reconexión, mDNS y el clima siguen operando en segundo plano.
- `reloj` se usa como hostname DHCP y mDNS publica `reloj.local`. mDNS se
  detiene cuando desaparece la conexión y se reinicia al volver la red.

## Web local de diagnóstico

- El servidor HTTP integrado comparte el bucle principal y atiende una petición
  por iteración mediante `handleClient()`, sin introducir `delay()` ni modificar
  el modelo dirty-framebuffer del display.
- La página y sus recursos están embebidos en la flash; no se necesita LittleFS
  ni una conexión a Internet para cargarla.
- El control de brillo acepta exclusivamente valores enteros entre 0 y 15 y los
  aplica con `matrix.control(MD_MAX72XX::INTENSITY, ...)`. El ajuste es volátil:
  tras reiniciar vuelve a 2, evitando escrituras reiteradas en flash mientras se
  prueba el deslizador.
- `/api/status` permite observar Wi-Fi, hora, memoria, vista activa y el último
  conjunto meteorológico sin exponer las credenciales. La actualización manual
  del clima reutiliza la tarea `weather-fetch`, por lo que tampoco bloquea el
  refresco del reloj.
- El servicio no tiene autenticación y está pensado solamente para la LAN. No
  se debe publicar su puerto 80 hacia Internet.
- En una prueba real, el ESP continuó ejecutando `loop()` y llegó al reintento
  NTP 21, pero `192.168.1.217` no aparecía por ARP ni respondía ICMP o HTTP desde
  `192.168.1.220`. Por lo tanto, que la aplicación siga viva y
  `WiFi.status() == WL_CONNECTED` no demuestra que haya tráfico útil.
- Después de cargar el monitor ICMP se reprodujo el fallo con más evidencia: el
  ESP respondió ARP con su MAC `e8:f6:0a:4e:05:6c`, pero no respondió ping ni
  HTTP; simultáneamente, el ping interno al gateway falló 3 de 3 y SNTP tampoco
  recibió respuestas. La recuperación reconectó, obtuvo otra vez
  `192.168.1.217` con RSSI -37 dBm y volvió a elevar la radio a 19,5 dBm. Al
  mantener 8,5 dBm permanentemente, ping, HTTP y el resto de la red volvieron a
  funcionar; la reducción queda validada en este hardware.
- Espressif especifica picos de 276 a 335 mA al transmitir cerca de 18,5–21 dBm
  y advierte que el aumento súbito de corriente durante TX puede colapsar el
  riel de 3,3 V. Recomienda una fuente capaz de al menos 500 mA, un capacitor de
  10 µF en la entrada y otro desacoplo de 10 µF/0,1 µF cerca de VDD3P3. Que el
  microcontrolador no se reinicie no descarta una perturbación breve suficiente
  para romper el tráfico del bloque RF.
- Pasar de 8,5 a 19,5 dBm agrega 11 dB, equivalente a unas 12,6 veces más
  potencia radiada. Con RSSI -37 dBm no aporta una mejora útil: 8,5 dBm queda
  como valor aceptable y confirmado. Si el montaje cambia y el RSSI cae por
  debajo de aproximadamente -70 dBm, probar primero 11 o 13 dBm y validar ping,
  HTTP y NTP sostenidos antes de aumentar más.
- Se agregó un ping asíncrono al gateway cada 30 segundos. Tres fallos seguidos
  provocan una reconexión deliberada, limitada a una cada cinco minutos. La
  prueba se ejecuta en la tarea interna de ping para no bloquear el display y se
  expone en `/api/status`; aún falta validar esta recuperación en hardware.
- Espressif indica que no existe una clasificación oficial de RSSI y propone
  como referencia una escala similar a Android, con `-100 dBm` como extremo
  débil y `-55 dBm` o mejor como nivel máximo. El datasheet del ESP32-C3-MINI-1
  especifica sensibilidades desde `-98 dBm` a 1 Mbps hasta `-71,2 dBm` para
  802.11n HT40 MCS7. La web usa por eso una escala práctica, no una garantía:
  excelente desde -55, muy buena desde -67, buena desde -74, regular desde -85,
  débil desde -92 y muy débil por debajo. Siempre se conserva el valor numérico
  para diagnosticar sin depender sólo de la etiqueta.

## Datos meteorológicos

- Open-Meteo permite obtener sin credenciales la temperatura, humedad, código
  WMO, viento a 10 m y estado día/noche actuales, además de máxima y mínima
  diarias.
- Se fuerza `8.8.8.8` como DNS primario después de obtener la dirección por
  DHCP, porque el DNS entregado por el router presentó fallos intermitentes al
  resolver `api.open-meteo.com`. El puerto serie registra IP, máscara, gateway,
  DNS, BSSID, canal y RSSI al conectar y antes de consultar el clima.
- La consulta usa las coordenadas `-38.114864, -57.607937`, un solo día de
  pronóstico y `America/Argentina/Buenos_Aires`. La zona explícita evita que la
  máxima y mínima cambien según el corte de día UTC.
- Los datos se consultan cada 15 minutos; antes del primer éxito se reintenta
  cada minuto. Una falla conserva el último conjunto completo válido.
- HTTP y JSON se procesan en una tarea FreeRTOS separada para no congelar el
  reloj. Los datos compartidos se publican juntos dentro de una sección crítica,
  evitando que la pantalla mezcle valores de dos respuestas distintas.
- Se eligió HTTP simple porque la prueba TLS devolvía error `-1` y la API sólo
  entrega información pública, sin secretos ni acciones. Si en el futuro se
  transportan datos privados, debe usarse HTTPS con una CA raíz validada.
- Los códigos WMO se resumen en palabras de hasta ocho caracteres. El viento
  se agrupa por legibilidad: `CALMO` (<4 km/h), `BRISA` (<16), `VIENTO` (<30),
  `VENTOSO` (<50) y `FUERTE` (desde 50).
- Prueba real más reciente: 6 °C, 63 %, WMO 3, 17 km/h, noche, máxima de 10 °C
  y mínima de 5 °C. El firmware tradujo ese conjunto a `NUBLADO`, `MAX:10C`,
  `MIN:5C` y `VIENTO`.

## Depuración del ESP32-C3 en Windows

- El USB/JTAG integrado requiere que la placa exponga GPIO18/19 y que Windows
  use el controlador `USB JTAG debug unit`. Con WinUSB genérico, OpenOCD no pudo
  abrir el dispositivo (`LIBUSB_ERROR_NOT_SUPPORTED`/`NOT_FOUND`).
- OpenOCD antiguo provocó panics al avanzar con GDB. Se fijó la versión oficial
  de Espressif 0.12.0-esp32-20260703 y se agregó un instalador que verifica el
  SHA-256 antes de colocarla en `.pio`.
- Cargar imágenes grandes por JTAG resultó inestable. El flujo confiable carga
  por el bootloader USB serie en COM4 y reserva JTAG para breakpoints, usando
  `debug_load_mode = manual`.
- Después de corregir la alimentación se probó el transporte JTAG de forma
  escalonada a 10, 20 y 40 MHz. OpenOCD detectó el ESP32-C3 y completó tres
  ciclos `halt`/`resume` en cada velocidad sin `LIBUSB_ERROR_PIPE`. GDB también
  pudo leer el PC y ejecutar un paso de instrucción a 40 MHz, la frecuencia base
  máxima anunciada por el adaptador; por eso `debug_speed = 40000` queda como
  configuración actual. Esto valida pruebas breves, no todavía la estabilidad
  de una sesión larga; la flash continúa cargándose por COM4.
- La prueba GDB aislada terminó con un error interno al interpretar FreeRTOS
  porque el firmware que estaba en flash no correspondía al ELF debug recién
  compilado. El flujo normal `PIO Debug` evita ese desajuste cargando primero el
  mismo binario por COM4.
- La siguiente prueba escribió por JTAG a 40 MHz la imagen debug de aplicación
  completa: 950.272 bytes transferidos a aproximadamente 78 KB/s. OpenOCD borró
  232 sectores, programó la imagen y terminó la verificación con `Verify OK`, sin
  `LIBUSB_ERROR_PIPE`. El firmware arrancó, se asoció a Wi-Fi, obtuvo
  `192.168.1.217` y anunció la nueva web. Este éxito cambia la conclusión sobre
  la imposibilidad de escribir por JTAG, pero una sola carga no basta para
  reemplazar aún el flujo conservador por `esptool`.
- `Serial` debe usar USB CDC nativo. Las definiciones
  `ARDUINO_USB_MODE=1` y `ARDUINO_USB_CDC_ON_BOOT=1` evitan el camino UART que
  produjo panics al entrar en `Serial.begin()` durante la depuración.
- Una pausa manual puede detenerse legítimamente dentro de
  `semihosting_call_noerrno()`. Eso es un `SIGINT` de GDB sobre código interno,
  no necesariamente el origen de una falla.
- Si aparece `Abstractct.busy appears stuck` tras interrumpir una carga, el
  módulo JTAG puede requerir desconectar físicamente el USB unos segundos.
- `launch.json` debe apuntar al ELF de `esp32-c3-debug`, no al del entorno
  normal. Se usan `${workspaceFolder}` y `${env:USERPROFILE}` para que la
  configuración no dependa de una ruta absoluta de una computadora concreta.
- PlatformIO regeneró `launch.json` durante una compilación y volvió a apuntarlo
  al ELF normal. Después de compilar se verificó y restauró el archivo; si los
  breakpoints vuelven a caer en líneas incoherentes, ésta es la primera ruta que
  se debe revisar.

## Estado reproducible

La compilación debug verificada usa Arduino ESP32 2.0.17, MD_MAX72XX 3.5.1 y
ArduinoJson 7.4.3. Ocupa 41.132 bytes de RAM (12,6 %) y 863.866 bytes de flash
(65,9 %). `include/secrets.h` y `.pio` permanecen ignorados; nunca deben entrar
en un commit.
