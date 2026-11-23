#include <LiquidCrystal.h>
#include <DHT.h>
#include <avr/wdt.h>
#include <util/atomic.h>
#include <math.h>

// ==============================================
// PRACTICA 3 - MAQUINA EXPENDEDORA
// Arduino UNO R3
// ==============================================

// ---------- PINES ----------

const byte LED1 = A3;
const byte LED2 = 5;

const byte BOTON = 2;

const byte TRIG = 9;
const byte ECHO = 3;

const byte DHT_PIN = 6;

const byte JOY_X = A0;
const byte JOY_Y = A1;
const byte JOY_SW = A2;

// LCD: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(4, 7, 8, 10, 11, 12);

DHT dht(DHT_PIN, DHT11);

// ---------- ESTADOS ----------

enum Modo {
  ARRANQUE,
  SERVICIO,
  ADMIN
};

enum EstadoServicio {
  ESPERANDO,
  MOSTRAR_AMBIENTE,
  SELECCIONANDO,
  PREPARANDO,
  ENTREGANDO
};

enum EstadoAdmin {
  MENU_ADMIN,
  VER_TEMPERATURA,
  VER_DISTANCIA,
  VER_CONTADOR,
  LISTA_PRECIOS,
  EDITAR_PRECIO
};

Modo modo = ARRANQUE;
EstadoServicio estadoServicio = ESPERANDO;
EstadoAdmin estadoAdmin = MENU_ADMIN;

unsigned long inicioEstado = 0;
unsigned long ultimaPantalla = 0;

// ---------- PRODUCTOS ----------

// Los precios se guardan en centimos.

const char* productos[5] = {
  "Cafe Solo",
  "Cafe Cortado",
  "Cafe Doble",
  "Cafe Premium",
  "Chocolate"
};

int precios[5] = {
  100, 110, 125, 150, 200
};

byte productoActual = 0;
byte opcionAdmin = 0;

int precioTemporal = 0;

// ---------- PREPARACION ----------

unsigned long tiempoPreparacion = 0;
unsigned long ultimaPreparacion = 0;
unsigned long ultimoPWM = 0;

// ---------- SENSORES ----------

float temperatura = NAN;
float humedad = NAN;

unsigned long ultimoDHT = 0;

unsigned long distanciaCm = 0;
bool distanciaValida = false;

// Variables compartidas con la interrupcion ECHO.
volatile unsigned long subidaEcoUs = 0;
volatile unsigned long duracionEcoUs = 0;
volatile bool ecoListo = false;

bool esperandoEco = false;
unsigned long inicioPingUs = 0;
unsigned long ultimoPingMs = 0;

// ---------- BOTON EXTERNO ----------

volatile byte botonBruto = HIGH;
volatile unsigned long ultimoFlancoUs = 0;

byte botonEstable = HIGH;

unsigned long inicioPulsacion = 0;
bool pulsacionLargaAtendida = false;

// ---------- JOYSTICK ----------

bool ejeYArmado = true;
bool ejeXArmado = true;

bool swAnterior = HIGH;
bool swEstable = HIGH;

unsigned long cambioSW = 0;

struct EventosJoy {
  bool arriba;
  bool abajo;
  bool izquierda;
  bool click;
};

// ==============================================
// 1. INTERRUPCIONES HARDWARE
// ==============================================

// Interrupcion del boton externo.
void interrupcionBoton() {
  botonBruto = digitalRead(BOTON);
  ultimoFlancoUs = micros();
}

// Interrupcion del sensor ultrasonico.
void interrupcionEco() {

  if (digitalRead(ECHO) == HIGH) {
    subidaEcoUs = micros();

  } else if (subidaEcoUs != 0) {
    duracionEcoUs = micros() - subidaEcoUs;
    ecoListo = true;
  }
}

// ==============================================
// 2. FUNCIONES LCD
// ==============================================

// Escribe una fila completa del LCD.
void escribirFila(byte fila, const char* texto) {

  lcd.setCursor(0, fila);

  byte i = 0;

  while (i < 16 && texto[i] != '\0') {
    lcd.write(texto[i]);
    i++;
  }

  // Borra caracteres anteriores sobrantes.
  while (i < 16) {
    lcd.write(' ');
    i++;
  }
}

void mostrarEspera() {

  escribirFila(0, "ESPERANDO");
  escribirFila(1, "CLIENTE");
}

void mostrarAmbiente() {

  char texto[17];

  if (isnan(temperatura)) {
    escribirFila(0, "Temp: sin dato");

  } else {
    snprintf(
      texto,
      sizeof(texto),
      "Temp: %d C",
      (int)temperatura
    );

    escribirFila(0, texto);
  }

  if (isnan(humedad)) {
    escribirFila(1, "Hum: sin dato");

  } else {
    snprintf(
      texto,
      sizeof(texto),
      "Humedad: %d%%",
      (int)humedad
    );

    escribirFila(1, texto);
  }
}

void mostrarProducto() {

  char texto[17];

  int precio = precios[productoActual];

  escribirFila(0, productos[productoActual]);

  snprintf(
    texto,
    sizeof(texto),
    "%d.%02d EUR SW=OK",
    precio / 100,
    precio % 100
  );

  escribirFila(1, texto);
}

// ==============================================
// 3. CAMBIOS DE MODO
// ==============================================

void iniciarServicio() {

  modo = SERVICIO;
  estadoServicio = ESPERANDO;

  digitalWrite(LED1, LOW);
  analogWrite(LED2, 0);

  inicioEstado = millis();

  mostrarEspera();

  Serial.println(F("Modo SERVICIO"));
}

void mostrarMenuAdmin() {

  const char* opciones[4] = {
    "Temperatura",
    "Distancia",
    "Contador",
    "Modificar precio"
  };

  escribirFila(0, opciones[opcionAdmin]);
  escribirFila(1, "SW=OK  Arr/Abj");
}

void iniciarAdmin() {

  modo = ADMIN;
  estadoAdmin = MENU_ADMIN;

  opcionAdmin = 0;

  // Ambos LEDs encendidos.
  digitalWrite(LED1, HIGH);
  analogWrite(LED2, 255);

  mostrarMenuAdmin();

  Serial.println(F("Modo ADMIN"));
}

// ==============================================
// 4. SENSOR DHT11
// ==============================================

void actualizarDHT() {

  unsigned long ahora = millis();

  // Lectura cada 2.5 segundos.
  if (ahora - ultimoDHT >= 2500) {

    ultimoDHT = ahora;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {

      temperatura = t;
      humedad = h;
    }
  }
}

// ==============================================
// 5. SENSOR ULTRASONICO
// ==============================================

void actualizarUltrasonico() {

  unsigned long ahora = millis();

  // Inicia una medicion cada 80 ms.
  if (!esperandoEco &&
      ahora - ultimoPingMs >= 80) {

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      ecoListo = false;
      subidaEcoUs = 0;
    }

    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG, LOW);

    inicioPingUs = micros();
    ultimoPingMs = ahora;

    esperandoEco = true;
  }

  if (!esperandoEco) {
    return;
  }

  bool recibido = false;
  unsigned long duracion = 0;

  // Copia atomica de los datos de la ISR.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {

    if (ecoListo) {

      duracion = duracionEcoUs;
      ecoListo = false;

      recibido = true;
    }
  }

  if (recibido) {

    if (duracion >= 100 &&
        duracion <= 30000) {

      // Aproximadamente 58 us por centimetro.
      distanciaCm = duracion / 58;

      distanciaValida = true;

    } else {
      distanciaValida = false;
    }

    esperandoEco = false;

  } else if (
    micros() - inicioPingUs >= 30000UL
  ) {

    // Timeout: no ha llegado el eco.
    distanciaValida = false;
    esperandoEco = false;
  }
}

// ==============================================
// 6. JOYSTICK
// ==============================================

EventosJoy leerJoystick() {

  EventosJoy evento = {
    false, false, false, false
  };

  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  // ---------- ARRIBA / ABAJO ----------

  if (y > 420 && y < 600) {
    ejeYArmado = true;
  }

  if (ejeYArmado) {

    if (y < 350) {

      evento.arriba = true;
      ejeYArmado = false;

    } else if (y > 700) {

      evento.abajo = true;
      ejeYArmado = false;
    }
  }

  // ---------- IZQUIERDA ----------

  if (x > 420 && x < 600) {
    ejeXArmado = true;
  }

  if (ejeXArmado && x < 350) {

    evento.izquierda = true;
    ejeXArmado = false;
  }

  // ---------- PULSADOR SW ----------

  bool lectura = digitalRead(JOY_SW);

  if (lectura != swAnterior) {

    swAnterior = lectura;
    cambioSW = millis();
  }

  // Antirrebote de 35 ms.
  if (millis() - cambioSW >= 35 &&
      lectura != swEstable) {

    swEstable = lectura;

    if (swEstable == LOW) {
      evento.click = true;
    }
  }

  return evento;
}

// ==============================================
// 7. BOTON EXTERNO
// ==============================================

void actualizarBotonExterno() {

  byte lectura;
  unsigned long ultimoFlanco;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    lectura = botonBruto;
    ultimoFlanco = ultimoFlancoUs;
  }

  // Antirrebote.
  if (lectura != botonEstable &&
      (unsigned long)(micros() - ultimoFlanco)
      >= 35000UL) {

    botonEstable = lectura;

    if (botonEstable == LOW) {

      inicioPulsacion = millis();
      pulsacionLargaAtendida = false;

    } else {

      unsigned long duracion =
        millis() - inicioPulsacion;

      // Cancelar soltando entre 2 y 3 s.
      if (!pulsacionLargaAtendida &&
          modo == SERVICIO &&
          duracion >= 2000 &&
          duracion <= 3000) {

        iniciarServicio();

        Serial.println(
          F("Operacion cancelada")
        );
      }
    }
  }

  // Mantener durante 5 segundos.
  if (botonEstable == LOW &&
      !pulsacionLargaAtendida &&
      millis() - inicioPulsacion >= 5000) {

    pulsacionLargaAtendida = true;

    if (modo == ADMIN) {
      iniciarServicio();

    } else {
      iniciarAdmin();
    }
  }
}

// ==============================================
// 8. PREPARACION DE BEBIDA
// ==============================================

void iniciarPreparacion() {

  estadoServicio = PREPARANDO;

  inicioEstado = millis();
  ultimoPWM = 0;

  // Tiempo pseudoaleatorio entre 4 y 8 s.
  do {

    tiempoPreparacion = random(4000L, 8001L);

  } while (
    tiempoPreparacion == ultimaPreparacion
  );

  ultimaPreparacion = tiempoPreparacion;

  analogWrite(LED2, 0);

  escribirFila(0, "Preparando Cafe");
  escribirFila(1, "Progreso: 0%");

  Serial.print(F("Preparacion: "));
  Serial.print(tiempoPreparacion);
  Serial.println(F(" ms"));
}

void actualizarPreparacion() {

  unsigned long ahora = millis();

  unsigned long transcurrido =
    ahora - inicioEstado;

  // ---------- BEBIDA TERMINADA ----------

  if (transcurrido >= tiempoPreparacion) {

    analogWrite(LED2, 0);

    estadoServicio = ENTREGANDO;
    inicioEstado = ahora;

    escribirFila(0, "RETIRE BEBIDA");
    escribirFila(1, "");

    return;
  }

  // ---------- ACTUALIZAR PROGRESO ----------

  if (ahora - ultimoPWM >= 50) {

    ultimoPWM = ahora;

    int porcentaje =
      (transcurrido * 100UL)
      / tiempoPreparacion;

    int brillo =
      (transcurrido * 255UL)
      / tiempoPreparacion;

    // PWM del LED2.
    analogWrite(LED2, brillo);

    char texto[17];

    snprintf(
      texto,
      sizeof(texto),
      "Progreso: %d%%",
      porcentaje
    );

    escribirFila(1, texto);
  }
}

// ==============================================
// 9. MODO SERVICIO
// ==============================================

void actualizarServicio(EventosJoy joy) {

  unsigned long ahora = millis();

  switch (estadoServicio) {

    // ---------- ESPERANDO CLIENTE ----------

    case ESPERANDO:

      if (distanciaValida &&
          distanciaCm < 100) {

        estadoServicio = MOSTRAR_AMBIENTE;

        inicioEstado = ahora;
        ultimaPantalla = ahora;

        mostrarAmbiente();
      }

      break;

    // ---------- MOSTRAR TEMPERATURA ----------

    case MOSTRAR_AMBIENTE:

      if (ahora - inicioEstado >= 5000) {

        estadoServicio = SELECCIONANDO;
        productoActual = 0;

        mostrarProducto();

      } else if (
        ahora - ultimaPantalla >= 500
      ) {

        ultimaPantalla = ahora;
        mostrarAmbiente();
      }

      break;

    // ---------- SELECCION PRODUCTOS ----------

    case SELECCIONANDO:

      if (joy.arriba) {

        if (productoActual > 0) {
          productoActual--;
        }

        mostrarProducto();
      }

      if (joy.abajo) {

        if (productoActual < 4) {
          productoActual++;
        }

        mostrarProducto();
      }

      if (joy.click) {
        iniciarPreparacion();
      }

      break;

    // ---------- PREPARANDO ----------

    case PREPARANDO:

      actualizarPreparacion();

      break;

    // ---------- RETIRAR BEBIDA ----------

    case ENTREGANDO:

      if (ahora - inicioEstado >= 3000) {
        iniciarServicio();
      }

      break;
  }
}

// ==============================================
// 10. PANTALLAS ADMIN
// ==============================================

void mostrarDistanciaAdmin() {

  char texto[17];

  escribirFila(0, "Distancia:");

  if (distanciaValida) {

    snprintf(
      texto,
      sizeof(texto),
      "%lu cm",
      distanciaCm
    );

    escribirFila(1, texto);

  } else {

    escribirFila(1, "Sin lectura");
  }
}

void mostrarContadorAdmin() {

  char texto[17];

  escribirFila(0, "Tiempo encendido");

  snprintf(
    texto,
    sizeof(texto),
    "%lu segundos",
    millis() / 1000UL
  );

  escribirFila(1, texto);
}

void mostrarListaPrecios() {

  char texto[17];

  int precio = precios[productoActual];

  escribirFila(0, productos[productoActual]);

  snprintf(
    texto,
    sizeof(texto),
    "%d.%02d EUR SW=EDIT",
    precio / 100,
    precio % 100
  );

  escribirFila(1, texto);
}

void mostrarEditarPrecio() {

  char texto[17];

  escribirFila(0, productos[productoActual]);

  snprintf(
    texto,
    sizeof(texto),
    "%d.%02d EUR ^v/OK",
    precioTemporal / 100,
    precioTemporal % 100
  );

  escribirFila(1, texto);
}

// ==============================================
// 11. MODO ADMINISTRACION
// ==============================================

void actualizarAdmin(EventosJoy joy) {

  unsigned long ahora = millis();

  // ---------- VOLVER CON IZQUIERDA ----------

  if (joy.izquierda) {

    if (estadoAdmin == EDITAR_PRECIO) {

      // Descartar los cambios.
      estadoAdmin = LISTA_PRECIOS;

      mostrarListaPrecios();

    } else if (
      estadoAdmin == LISTA_PRECIOS ||
      estadoAdmin == VER_TEMPERATURA ||
      estadoAdmin == VER_DISTANCIA ||
      estadoAdmin == VER_CONTADOR
    ) {

      estadoAdmin = MENU_ADMIN;

      mostrarMenuAdmin();
    }

    return;
  }

  switch (estadoAdmin) {

    // ---------- MENU PRINCIPAL ----------

    case MENU_ADMIN:

      if (joy.arriba && opcionAdmin > 0) {

        opcionAdmin--;

        mostrarMenuAdmin();
      }

      if (joy.abajo && opcionAdmin < 3) {

        opcionAdmin++;

        mostrarMenuAdmin();
      }

      if (joy.click) {

        ultimaPantalla = 0;

        switch (opcionAdmin) {

          case 0:
            estadoAdmin = VER_TEMPERATURA;
            mostrarAmbiente();
            break;

          case 1:
            estadoAdmin = VER_DISTANCIA;
            mostrarDistanciaAdmin();
            break;

          case 2:
            estadoAdmin = VER_CONTADOR;
            mostrarContadorAdmin();
            break;

          case 3:
            estadoAdmin = LISTA_PRECIOS;
            productoActual = 0;
            mostrarListaPrecios();
            break;
        }
      }

      break;

    // ---------- TEMPERATURA ----------

    case VER_TEMPERATURA:

      if (ahora - ultimaPantalla >= 500) {

        ultimaPantalla = ahora;

        mostrarAmbiente();
      }

      break;

    // ---------- DISTANCIA ----------

    case VER_DISTANCIA:

      if (ahora - ultimaPantalla >= 500) {

        ultimaPantalla = ahora;

        mostrarDistanciaAdmin();
      }

      break;

    // ---------- CONTADOR ----------

    case VER_CONTADOR:

      if (ahora - ultimaPantalla >= 500) {

        ultimaPantalla = ahora;

        mostrarContadorAdmin();
      }

      break;

    // ---------- LISTADO DE PRECIOS ----------

    case LISTA_PRECIOS:

      if (joy.arriba && productoActual > 0) {

        productoActual--;

        mostrarListaPrecios();
      }

      if (joy.abajo && productoActual < 4) {

        productoActual++;

        mostrarListaPrecios();
      }

      if (joy.click) {

        precioTemporal = precios[productoActual];

        estadoAdmin = EDITAR_PRECIO;

        mostrarEditarPrecio();
      }

      break;

    // ---------- MODIFICAR PRECIO ----------

    case EDITAR_PRECIO:

      // Subir precio 5 centimos.
      if (joy.arriba && precioTemporal <= 9990) {

        precioTemporal += 5;

        mostrarEditarPrecio();
      }

      // Bajar precio 5 centimos.
      if (joy.abajo && precioTemporal >= 5) {

        precioTemporal -= 5;

        mostrarEditarPrecio();
      }

      // Confirmar nuevo precio.
      if (joy.click) {

        precios[productoActual] = precioTemporal;

        estadoAdmin = LISTA_PRECIOS;

        mostrarListaPrecios();
      }

      break;
  }
}

// ==============================================
// 12. MODO ARRANQUE
// ==============================================

void actualizarArranque() {

  unsigned long transcurrido =
    millis() - inicioEstado;

  // Finalizar despues de 3 segundos.
  if (transcurrido >= 3000) {

    digitalWrite(LED1, LOW);

    iniciarServicio();

    return;
  }

  // 500 ms encendido, 500 ms apagado.
  bool encendido =
    ((transcurrido / 500UL) % 2 == 0);

  digitalWrite(
    LED1,
    encendido ? HIGH : LOW
  );
}

// ==============================================
// 13. SETUP
// ==============================================

void setup() {

  // Limpiar flag de reset por watchdog y desactivarlo
  // durante la inicializacion.
  MCUSR = 0;
  wdt_disable();

  Serial.begin(9600);

  // ---------- LEDS ----------

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  digitalWrite(LED1, LOW);
  analogWrite(LED2, 0);

  // ---------- BOTONES ----------

  pinMode(BOTON, INPUT_PULLUP);
  pinMode(JOY_SW, INPUT_PULLUP);

  // ---------- ULTRASONIDO ----------

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  digitalWrite(TRIG, LOW);

  // ---------- LCD ----------

  lcd.begin(16, 2);

  // ---------- DHT11 ----------

  dht.begin();

  // ---------- INTERRUPCIONES ----------

  botonBruto = digitalRead(BOTON);
  ultimoFlancoUs = micros();

  attachInterrupt(
    digitalPinToInterrupt(BOTON),
    interrupcionBoton,
    CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(ECHO),
    interrupcionEco,
    CHANGE
  );

  // ---------- ALEATORIEDAD ----------

  // A4 debe estar desconectado.
  randomSeed(analogRead(A4));

  // ---------- TEMPORIZADORES ----------

  inicioEstado = millis();
  ultimoDHT = millis();

  // ---------- PANTALLA INICIAL ----------

  escribirFila(0, "CARGANDO...");
  escribirFila(1, "");

  // ---------- WATCHDOG ----------

  // Periodo de 1 s: el loop tarda como mucho ~35 ms (lectura DHT).
  wdt_enable(WDTO_1S);

  Serial.println(F("Sistema iniciado"));
}

// ==============================================
// 14. LOOP PRINCIPAL
// ==============================================

void loop() {

  // Alimentar el watchdog.
  wdt_reset();

  // Gestionar boton externo.
  actualizarBotonExterno();

  // Actualizar distancia.
  actualizarUltrasonico();

  // Leer DHT11 cuando no hay eco pendiente.
  if (!esperandoEco) {
    actualizarDHT();
  }

  // Leer joystick.
  EventosJoy joy = leerJoystick();

  // Ejecutar modo correspondiente.
  switch (modo) {

    case ARRANQUE:
      actualizarArranque();
      break;

    case SERVICIO:
      actualizarServicio(joy);
      break;

    case ADMIN:
      actualizarAdmin(joy);
      break;
  }
}
