# Reloj ESP32-C3 con matriz MAX7219 8x32

Reloj conectado por Wi-Fi que obtiene la hora mediante NTP. La pantalla
principal muestra `HH:MM SS`, con horas y minutos más grandes que los segundos.
Cuando está conectado también anuncia el nombre local `reloj.local` mediante
mDNS.

El mismo nombre abre una web de control en `http://reloj.local/`. Desde allí se
puede cambiar inmediatamente el brillo de la matriz entre 0 y 15, consultar el
estado de Wi-Fi, NTP, memoria y clima, y solicitar una actualización
meteorológica fuera del intervalo automático. El brillo vuelve al valor inicial
2 después de reiniciar el dispositivo.

Los números usan una fuente de segmentos compacta y gruesa: 4x7 para horas y
minutos y 3x5 para segundos y datos. Los dos puntos ocupan dos píxeles de alto
en cada marca para mejorar su lectura a distancia.

Cada 25 segundos se ejecuta esta secuencia:

1. Fecha `DD/MM/AA` durante 3 segundos.
2. Temperatura `T:0C` durante 2 segundos.
3. Humedad `H:0%` durante 2 segundos.
4. Estado del tiempo, por ejemplo `SOLEADO`, `NUBLADO`, `LLUVIA`, `NIEVE` o
   `TORMENTA`, durante 2 segundos.
5. Máxima pronosticada del día, por ejemplo `MAX:18C`, durante 2 segundos.
6. Mínima pronosticada del día, por ejemplo `MIN:9C`, durante 2 segundos.
7. Intensidad del viento (`CALMO`, `BRISA`, `VIENTO`, `VENTOSO` o `FUERTE`)
   durante 2 segundos.
8. Regreso a la hora.

La temperatura y la humedad corresponden al clima exterior estimado para las
coordenadas `-38.114864, -57.607937`. La consulta también obtiene el código
meteorológico, la velocidad del viento a 10 metros y si es de día o de noche.
También obtiene las temperaturas máxima y mínima pronosticadas para el día
local de Argentina. Se realiza mediante Open-Meteo cada 15 minutos y se conserva
el último valor válido si falla.

La condición cubre cielo despejado de día o de noche, nubosidad parcial,
nublado, niebla, llovizna, lluvia, nieve, chubascos, tormenta y granizo. La
velocidad se resume por legibilidad: menos de 4 km/h es `CALMO`, menos de 16
`BRISA`, menos de 30 `VIENTO`, menos de 50 `VENTOSO` y desde 50 `FUERTE`.

Datos meteorológicos: [Open-Meteo](https://open-meteo.com/), bajo licencia
CC BY 4.0.

Las decisiones técnicas, problemas encontrados y pruebas realizadas están
registrados en [Lecciones aprendidas](docs/LECCIONES_APRENDIDAS.md). Las reglas
operativas para continuar desarrollando el firmware están en [AGENTS.md](AGENTS.md).

El renderizado usa dos framebuffers de 8x32 bits. Cada cuadro nuevo se compara
con el anterior y solo se marcan los píxeles modificados; si no cambió ninguno,
no se transmite nada al MAX7219. Cuando hay cambios, la biblioteca envía
únicamente los registros de fila afectados, que es la granularidad mínima del
controlador. Durante las animaciones el firmware calcula un cuadro cada 80 ms,
pero esa comprobación no genera tráfico SPI cuando la imagen resulta idéntica.

## Credenciales Wi-Fi

Copiar `include/secrets.example.h` como `include/secrets.h` y completar:

```cpp
#define WIFI_SSID "nombre-de-tu-wifi"
#define WIFI_PASSWORD "contrasena-de-tu-wifi"
```

`include/secrets.h` está ignorado por Git para no publicar la contraseña. Si no
existe, el firmware igualmente compila y mantiene la animación de conexión.

## Cableado

Conectar el ESP32-C3 al conector de entrada del primer módulo:

| MAX7219 | ESP32-C3 |
| --- | --- |
| DIN | GPIO6 |
| CLK | GPIO4 |
| CS / LOAD | GPIO7 |
| GND | GND |
| VCC | 5 V |

Conectar `DOUT` de cada módulo al `DIN` del siguiente. Para cuatro matrices se
recomienda una fuente regulada externa de 5 V con su GND unido al del ESP32-C3.
No alimentar las matrices desde un GPIO.

## Comportamiento de inicio

- Durante la conexión Wi-Fi y la espera NTP se encienden píxeles al azar, uno
  por cuadro y sin repetir. Al completar la matriz, se limpia y vuelve a empezar.
- Cuando llega una hora válida comienza la vista normal del reloj.
- SNTP consulta `ntp2.hidro.gob.ar`, `ntp.inti.gob.ar` y
  `time.cloudflare.com`, y se resincroniza cada hora.
- Si todavía no llegó la primera hora válida, SNTP se reinicia cada 30 segundos
  sin bloquear el reloj. La web muestra intentos, cantidad de respuestas, la
  última hora aceptada y su epoch con microsegundos.
- La zona horaria configurada es Argentina, UTC-3.
- El monitor serie informa el código de desconexión Wi-Fi y la IP obtenida.
- Al obtener red, el dispositivo inicia mDNS como `reloj.local`; si pierde
  Wi-Fi, lo detiene y vuelve a iniciarlo automáticamente al reconectar.
- El servidor web se inicia y detiene junto con la conexión. El estado completo
  también está disponible como JSON en `http://reloj.local/api/status`.
- La radio permanece en 8,5 dBm durante asociación y conexión. Con la señal
  fuerte observada no hace falta elevarla a 19,5 dBm, nivel que coincidió con
  una asociación aparentemente válida pero sin tráfico IP. Mantener 8,5 dBm
  restauró la conectividad y es el valor validado para este montaje.

### Web de control y diagnóstico

La página funciona únicamente dentro de la red local y se actualiza cada dos
segundos. No publica el SSID ni la contraseña. Sus rutas son:

- `GET /`: panel web adaptable a computadora o teléfono.
- `GET /api/status`: diagnóstico en JSON.
- `POST /api/brightness?value=0..15`: cambia el brillo del MAX7219.
- `POST /api/weather/refresh`: programa una consulta meteorológica inmediata.

No hay autenticación: cualquier equipo que pueda acceder a `reloj.local` en la
misma red puede cambiar el brillo o pedir una actualización del clima. No se
debe exponer el puerto 80 del reloj a Internet.

El panel prueba además el gateway local por ICMP cada 30 segundos y muestra la
latencia o la cantidad de fallos. Si el gateway falla tres veces seguidas aunque
Arduino todavía informe Wi-Fi conectado, el firmware reinicia deliberadamente
el enlace. Hay un enfriamiento de cinco minutos para evitar reconexiones en
bucle si una red bloquea ICMP.

La señal Wi-Fi se muestra como RSSI con valores negativos: `-38 dBm`, por
ejemplo, es una señal excelente. Cuanto más cerca de cero, más fuerte es la
señal. La etiqueta cualitativa de la página es orientativa y tiene en cuenta el
rango recomendado como referencia por Espressif y la sensibilidad del módulo
ESP32-C3-MINI-1.

El diagnóstico NTP distingue entre haber iniciado el cliente y haber recibido
una respuesta válida. Presenta la última marca de tiempo aceptada en hora local,
UTC y epoch. La interfaz SNTP usada por el framework no informa cuál de los tres
servidores configurados respondió ni entrega el paquete NTP crudo, por lo que el
panel enumera los servidores consultados y señala esa limitación.

### Diagnóstico Wi-Fi

La red debe operar en 2,4 GHz; el ESP32-C3 no puede asociarse a una red de 5
GHz. El código `202` en el monitor serie significa fallo de autenticación:
comprobar primero que la contraseña de `include/secrets.h` coincida exactamente
con la configurada para el SSID de 2,4 GHz.

Si aparecen repetidamente los motivos `2` (`AUTH_EXPIRE`) y `202` aun con
credenciales WPA2 verificadas, probar el ESP32-C3 con el VCC de las matrices
desconectado. Las cuatro matrices deben alimentarse desde una fuente externa de
5 V con margen suficiente y GND común; una caída o ruido durante la transmisión
Wi-Fi puede impedir que el punto de acceso reciba correctamente la asociación.

## Compilar y cargar

El proyecto usa PlatformIO, Arduino y MD_MAX72XX:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

El entorno actual es `esp32-c3-devkitm-1`. Si la placa es otro modelo de
ESP32-C3, cambiar `board` en `platformio.ini`.

La constante `FLIP_VERTICAL = true` corrige la orientación física actual del
display sin modificar el orden de los cuatro módulos.

## Depurar desde VS Code

El proyecto incluye el entorno `esp32-c3-debug`, preparado para el controlador
USB/JTAG integrado del ESP32-C3:

1. Instalar la extensión recomendada **PlatformIO IDE** si VS Code la solicita.
2. Conectar el puerto USB nativo de la placa, que debe estar cableado a GPIO18
   (USB D-) y GPIO19 (USB D+). No todos los DevKit exponen ese puerto.
3. Abrir **Run and Debug** con `Ctrl+Shift+D`.
4. Elegir **PIO Debug** y presionar `F5`.
5. La tarea previa compila y carga el firmware debug por `COM4`; luego JTAG se
   conecta sin volver a escribir la flash y se detiene al comenzar `setup()`.
6. Colocar breakpoints desde el margen izquierdo y continuar con `F5`; usar
   `F10` para avanzar por línea y `F11` para entrar en una función.

También se puede validar el firmware debug sin conectar la placa:

```powershell
pio run --environment esp32-c3-debug
```

El entorno normal continúa siendo `esp32-c3-devkitm-1`. Si la placa solo tiene
un conversor USB-serie y no expone el USB/JTAG nativo, se podrá cargar y usar el
monitor serie, pero para breakpoints hará falta otro puerto nativo o un adaptador
JTAG. El JTAG externo comparte GPIO4, GPIO6 y GPIO7 con el display actual, por lo
que requeriría cambiar temporalmente ese cableado.

### Controlador USB/JTAG en Windows

La interfaz JTAG debe aparecer con el controlador oficial **USB JTAG debug
unit**, no como el dispositivo WinUSB genérico. En esta computadora quedó
instalado desde el INF firmado de Espressif y OpenOCD confirmó el núcleo RISC-V.

`Serial` está configurado como USB CDC nativo mediante `ARDUINO_USB_MODE=1` y
`ARDUINO_USB_CDC_ON_BOOT=1`. Esto permite usar el monitor en COM4 junto con JTAG
y evita inicializar el UART0 mediante `HardwareSerial`.

El entorno debug usa OpenOCD oficial de Espressif 0.12.0-esp32-20260703. Las
versiones antiguas provocaban panics espontáneos al avanzar paso a paso en el
ESP32-C3. Si GDB muestra
`Couldn't determine a path for the index cache directory`, es una advertencia
inofensiva del caché y no una falla de conexión.

Como esa versión todavía no está publicada en el registro de PlatformIO, el
proyecto la instala en su caché local. Después de clonar el repositorio o borrar
`.pio`, ejecutar una vez:

```powershell
powershell -ExecutionPolicy Bypass -File tools\install-debug-openocd.ps1
pio run --environment esp32-c3-debug
```

El script descarga el paquete oficial para Windows x64 y comprueba su SHA-256
antes de instalarlo. `platformio.ini` usa después esa copia local.

La carga se realiza por el bootloader USB serie y OpenOCD se usa solamente para
breakpoints y ejecución paso a paso. Esto evita tanto la incompatibilidad de
PlatformIO 6.1 con rutas entre llaves (`Protocol error with Rcmd`) como los
cortes observados al escribir imágenes grandes mediante JTAG. Si Windows cambia
el puerto, actualizar `upload_port` en `platformio.ini` y el argumento de carga
en `.vscode/tasks.json`.

Si una sesión JTAG interrumpida deja `Abstractct.busy appears stuck`, desconectar
físicamente el USB de la placa durante unos segundos y volver a conectarlo. Ese
estado pertenece al módulo de depuración del chip y necesita un ciclo completo
de alimentación; no indica un error del firmware.

Al pausar manualmente, GDB puede mostrar `SIGINT` dentro de
`semihosting_call_noerrno()`. Es normal: el procesador fue interrumpido mientras
FreeRTOS ejecutaba código interno. El entorno debug traduce la ruta del servidor
de compilación de Arduino hacia la copia local de esos encabezados.
