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
  - potencia de 19,5 dBm después de obtener IP y retorno inmediato a 8,5 dBm
    ante una desconexión.
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
- La zona POSIX `<-03>3` representa Argentina UTC-3 sin horario de verano.
- Una vez sincronizado, el reloj local continúa avanzando aunque se pierda
  Wi-Fi. La reconexión, mDNS y el clima siguen operando en segundo plano.
- `reloj` se usa como hostname DHCP y mDNS publica `reloj.local`. mDNS se
  detiene cuando desaparece la conexión y se reinicia al volver la red.

## Datos meteorológicos

- Open-Meteo permite obtener sin credenciales la temperatura, humedad, código
  WMO, viento a 10 m y estado día/noche actuales, además de máxima y mínima
  diarias.
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
  `debug_load_mode = manual` y 1 MHz.
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

## Estado reproducible

La compilación debug verificada usa Arduino ESP32 2.0.17, MD_MAX72XX 3.5.1 y
ArduinoJson 7.4.3. Ocupa 41.132 bytes de RAM (12,6 %) y 863.866 bytes de flash
(65,9 %). `include/secrets.h` y `.pio` permanecen ignorados; nunca deben entrar
en un commit.
