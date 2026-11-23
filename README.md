# Práctica 3 · Máquina expendedora de café (Arduino UNO R3)

## Montaje

Vista general del circuito (Arduino UNO R3, protoboard, LCD 16x2, joystick, sensor ultrasónico HC-SR04 y DHT11):

![Vista general del montaje](fotos/montaje_general.jpg)

Montaje en funcionamiento, en modo administración (los dos LEDs encendidos y el menú en pantalla):

![Montaje funcionando en modo admin](fotos/montaje_funcionando.jpg)

Detalle de la protoboard con el DHT11 y el potenciómetro de contraste del LCD:

![Detalle de la protoboard](fotos/montaje_detalle_dht11.jpg)

## Esquemático

<!-- ESQUEMA -->

### Conexiones

| Componente | Pin Arduino | Nota |
|---|---|---|
| Botón externo | D2 (INT0) | INPUT_PULLUP, interrupción CHANGE |
| Ultrasonidos ECHO | D3 (INT1) | interrupción CHANGE |
| Ultrasonidos TRIG | D9 | pulso de 10 µs |
| LED2 | D5 (PWM) | brillo = progreso de la bebida |
| LED1 | A3 | arranque y modo admin |
| DHT11 | D6 | temperatura y humedad |
| Joystick X / Y | A0 / A1 | analógicas |
| Joystick SW | A2 | INPUT_PULLUP |
| LCD 16x2 | RS=4, E=7, D4..D7 = 8, 10, 11, 12 | LiquidCrystal |

## Descripción de la solución

El programa es una **máquina de estados no bloqueante**: no usa `delay()` (salvo los microsegundos del pulso TRIG) y todo el tiempo se controla comparando `millis()`. Tiene tres modos: `ARRANQUE` (3 s, LED1 parpadeando a 500 ms), `SERVICIO` (cliente) y `ADMIN`. Dentro de cada modo hay un enum con sus subestados.

### Recursos utilizados y por qué

| Recurso | ¿Usado? | Por qué |
|---|---|---|
| **Interrupciones hardware** | Sí | El botón (D2) y el ECHO (D3) son eventos asíncronos: la ISR solo anota el instante (`micros()`) y el estado, y el `loop` procesa. Así no se pierde ninguna pulsación ni se mide mal el eco. |
| **Watchdog** (`wdt_enable(WDTO_1S)`) | Sí | Si el programa se cuelga, la placa se reinicia sola en 1 s. El `loop` tarda como mucho ~35 ms (lectura del DHT11), así que `wdt_reset()` al inicio del `loop` es seguro. |
| **Threads** (ArduinoThread) | No | No hacen falta: el `loop` cooperativo con `millis()` ya reparte el tiempo entre sensores, pantalla y botones, y evita el coste de memoria en un UNO. |
| **PWM** | Sí | `analogWrite` sobre LED2 simula el progreso de preparación (0–255 en 4–8 s). |
| **ATOMIC_BLOCK** | Sí | Copia segura de las variables `volatile` compartidas con las ISR (el AVR es de 8 bits y una lectura de 32 bits no es atómica). |

### Detalles de diseño
- **Antirrebote**: botón externo 35 ms (con el instante del último flanco que da la ISR) y SW del joystick 35 ms.
- **Botón externo**: 5 s mantenido cambia entre SERVICIO y ADMIN; soltarlo entre 2 y 3 s en SERVICIO cancela la operación.
- **Ultrasonidos**: medida cada 80 ms, `distancia = duración / 58`; con `timeout` de 30 ms si no llega eco. Cliente detectado a < 100 cm.
- **DHT11**: lectura cada 2,5 s y solo cuando no hay un eco pendiente, para que su lectura (que bloquea unos ms) no estropee la medida de distancia.
- **Joystick**: un eje solo genera un evento al salir de la zona muerta (420–600), "rearmándose" al volver al centro, para que un solo empujón sea un solo movimiento.
- **Tiempo de preparación**: `random(4000, 8001)` con `randomSeed(analogRead(A4))` (A4 sin conectar) y nunca repite el valor anterior.
- **Precios** en céntimos (`int`) para evitar decimales; en admin se editan de 5 en 5 y se descartan con izquierda.
- **Admin**: Temperatura, Distancia, Contador (segundos encendido) y Modificar precio. LED1 y LED2 encendidos mientras se está en admin.

## Código
Ver `maquina_expendedora.ino` (incluido en el zip).
