#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// Main alternativo para calibrar UN sensor analogico por vez.

#define SDA_PIN 21
#define SCL_PIN 22

#define PIN_OXIGENO_ANALOG 35
#define PIN_LUZ_ANALOG     39
#define PIN_BOTON           4

// Tiempo que queda visible cada medicion despues de pulsar el boton.
const unsigned long TIEMPO_MUESTRA_MS = 10000;  // 10 segundos: cambiar si hace falta.

// Boton:
// En tu main actual el boton se toma como PRESIONADO cuando lee HIGH.
// Si tu boton esta cableado contra GND y queres usar pullup interno:
// 1) cambia MODO_BOTON a INPUT_PULLUP
// 2) cambia NIVEL_BOTON_PRESIONADO a LOW
const uint8_t MODO_BOTON = INPUT;
const uint8_t NIVEL_BOTON_PRESIONADO = HIGH;
const int CANTIDAD_MUESTRAS_ADC = 80;
const unsigned long INTERVALO_MUESTRA_ADC_MS = 3;

// Sensor a calibrar:
// Dejar activa una sola linea. La otra queda comentada.
#define SENSOR_OXIGENO
//#define SENSOR_LUZ

#if defined(SENSOR_OXIGENO) && defined(SENSOR_LUZ)
#error "Deja activo solo un sensor: SENSOR_OXIGENO o SENSOR_LUZ."
#endif

#if !defined(SENSOR_OXIGENO) && !defined(SENSOR_LUZ)
#error "Activa un sensor: SENSOR_OXIGENO o SENSOR_LUZ."
#endif

#if defined(SENSOR_OXIGENO)
const char* NOMBRE_SENSOR = "Oxigeno";
const uint8_t PIN_SENSOR = PIN_OXIGENO_ANALOG;
#else
const char* NOMBRE_SENSOR = "Luz";
const uint8_t PIN_SENSOR = PIN_LUZ_ANALOG;
#endif

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
  U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN
);

struct LecturaAnalogica {
  int raw;
  float voltaje;
  float luzUwCm2;
};

bool botonPresionadoAnterior = false;
unsigned long mostrarHasta = 0;
LecturaAnalogica ultimaLectura = {0, 0.0, 0.0};
bool hayLectura = false;

#if defined(SENSOR_LUZ)
float calibrarLuzUwCm2(float voltaje) {
  // Curva por tramos medida contra equipo calibrado.
  // El ultimo punto se usa como limite saturado.
  const float voltajes[] = {0.142, 0.351, 2.780, 2.800, 3.050, 3.160, 3.600};
  const float luzUwCm2[] = {0.000, 0.040, 0.097, 0.117, 0.170, 0.410, 0.520};
  const size_t cantidadPuntos = sizeof(voltajes) / sizeof(voltajes[0]);

  if (voltaje <= voltajes[0]) {
    return luzUwCm2[0];
  }

  if (voltaje >= voltajes[cantidadPuntos - 1]) {
    return luzUwCm2[cantidadPuntos - 1];
  }

  for (size_t i = 1; i < cantidadPuntos; i++) {
    if (voltaje <= voltajes[i]) {
      float proporcion = (voltaje - voltajes[i - 1]) / (voltajes[i] - voltajes[i - 1]);
      return luzUwCm2[i - 1] + proporcion * (luzUwCm2[i] - luzUwCm2[i - 1]);
    }
  }

  return luzUwCm2[cantidadPuntos - 1];
}
#else
float calibrarOxigenoPorcentaje(float voltaje) {
  // Ajuste lineal usando los puntos medios de las mediciones:
  // 0.907V=21%, 1.713V=36%, 1.9665V=39%, 2.0445V=43%, 2.320V=47%.
  const float pendiente = 18.39647163;
  const float ordenada = 4.26663649;
  float oxigeno = pendiente * voltaje + ordenada;

  if (oxigeno < 0.0) {
    return 0.0;
  }

  if (oxigeno > 100.0) {
    return 100.0;
  }

  return oxigeno;
}
#endif

LecturaAnalogica medirVoltaje() {
  uint32_t sumaRaw = 0;
  uint32_t sumaMv = 0;

  analogRead(PIN_SENSOR);
  analogReadMilliVolts(PIN_SENSOR);
  delay(INTERVALO_MUESTRA_ADC_MS);

  for (int i = 0; i < CANTIDAD_MUESTRAS_ADC; i++) {
    sumaRaw += analogRead(PIN_SENSOR);
    sumaMv += analogReadMilliVolts(PIN_SENSOR);
    delay(INTERVALO_MUESTRA_ADC_MS);
  }

  LecturaAnalogica lectura;
  lectura.raw = sumaRaw / CANTIDAD_MUESTRAS_ADC;
  lectura.voltaje = (sumaMv / (float)CANTIDAD_MUESTRAS_ADC) / 1000.0;
#if defined(SENSOR_LUZ)
  lectura.luzUwCm2 = calibrarLuzUwCm2(lectura.voltaje);
#else
  lectura.luzUwCm2 = 0.0;
#endif
  return lectura;
}

void escribirDisplay(const char* linea1, const char* linea2, const char* linea3, const char* linea4) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 12, linea1);
  display.drawStr(0, 28, linea2);
  display.drawStr(0, 44, linea3);
  display.drawStr(0, 60, linea4);
  display.sendBuffer();
}

void mostrarEspera() {
  char linea1[24];
  snprintf(linea1, sizeof(linea1), "Calibrar %s", NOMBRE_SENSOR);

  escribirDisplay(
    linea1,
    "Pulse boton",
    "para medir",
    "Monitor: 115200"
  );
}

void mostrarLectura(const LecturaAnalogica& lectura) {
  char linea1[24];
  char linea2[24];
  char linea3[24];
  char linea4[24];

  snprintf(linea1, sizeof(linea1), "%s", NOMBRE_SENSOR);
  snprintf(linea2, sizeof(linea2), "Voltaje: %.3f V", lectura.voltaje);
#if defined(SENSOR_LUZ)
  snprintf(linea3, sizeof(linea3), "Luz: %.3f uW/cm2", lectura.luzUwCm2);
  snprintf(linea4, sizeof(linea4), "ADC raw: %d", lectura.raw);
#else
  snprintf(linea3, sizeof(linea3), "O2: %.1f %%", calibrarOxigenoPorcentaje(lectura.voltaje));
  snprintf(linea4, sizeof(linea4), "ADC raw: %d", lectura.raw);
#endif

  escribirDisplay(linea1, linea2, linea3, linea4);
}

void imprimirLecturaSerial(const LecturaAnalogica& lectura) {
  Serial.println();
  Serial.println("===== MEDICION DE CALIBRACION =====");
  Serial.print("Sensor: ");
  Serial.println(NOMBRE_SENSOR);
  Serial.print("Pin ADC: GPIO ");
  Serial.println(PIN_SENSOR);
  Serial.print("Voltaje: ");
  Serial.print(lectura.voltaje, 4);
  Serial.println(" V");
#if defined(SENSOR_LUZ)
  Serial.print("Luz calibrada: ");
  Serial.print(lectura.luzUwCm2, 4);
  Serial.println(" uW/cm2");
#else
  Serial.print("Oxigeno calibrado: ");
  Serial.print(calibrarOxigenoPorcentaje(lectura.voltaje), 2);
  Serial.println(" %");
#endif
  Serial.print("ADC raw: ");
  Serial.println(lectura.raw);
  Serial.println("===================================");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_BOTON, MODO_BOTON);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SENSOR, ADC_11db);

  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin();

  Serial.println();

  Serial.print("Sensor activo: ");
  Serial.println(NOMBRE_SENSOR);
  Serial.print("Pin ADC: GPIO ");
  Serial.println(PIN_SENSOR);
#if defined(SENSOR_LUZ)
  Serial.println("Calibracion luz: interpolacion lineal por tramos.");
  Serial.println("Saturacion: valores >= 3.600 V se limitan a 0.520 uW/cm2.");
#endif
  Serial.println("Pulse el boton para tomar una medicion.");

  mostrarEspera();
}

void loop() {
  bool botonPresionado = (digitalRead(PIN_BOTON) == NIVEL_BOTON_PRESIONADO);
  bool flancoPresionado = botonPresionado && !botonPresionadoAnterior;
  botonPresionadoAnterior = botonPresionado;

  if (flancoPresionado) {
    delay(40);  // debounce simple
    if (digitalRead(PIN_BOTON) == NIVEL_BOTON_PRESIONADO) {
      ultimaLectura = medirVoltaje();
      hayLectura = true;
      mostrarHasta = millis() + TIEMPO_MUESTRA_MS;

      imprimirLecturaSerial(ultimaLectura);
      mostrarLectura(ultimaLectura);
    }
  }

  if (hayLectura && millis() > mostrarHasta) {
    hayLectura = false;
    mostrarEspera();
  }

  delay(10);
}
