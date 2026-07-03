#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_SHT31.h>
#include <U8g2lib.h>
#include <Wire.h>

#define LED            2
#define SDA_PIN        21
#define SCL_PIN        22
#define PIN_O2_ANALOG  35
#define PIN_LUZ_ANALOG 39
#define PIN_BUZZER 16
#define PIN_BOTON 4

const bool BUZZER_HABILITADO = true;  // Temporal: probar boton sin sonido

// OLED display
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
  U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN
);

// CASA!!!!!
// WiFi credentials
//const char* ssid     = "Fibertel WiFi696 2.4GHz"; //CAMBIAR!!
//const char* password = "00413579699"; //CAMBIAR!!

// Server for limits
//const char* limits_server_ip   = "192.168.0.141"; //CAMBIAR!!

// CELU!!!!!
const char* ssid     = "Anto's WiFi"; //CAMBIAR!!
const char* password = "pecanegrocolada"; //CAMBIAR!!

// Server for limits
const char* limits_server_ip   = "10.134.93.240"; //CAMBIAR!!
const int   limits_server_port = 5001;
char server_url[80];

// MQTT broker
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ESP identifier and topic
const int  esp_id     = 1;
String     mqtt_topic = "sensor/1/datos";
String     mqtt_topic_silenciar;

// Timing
unsigned long lastSend = 0;
bool firstPublicationPending = true;
const unsigned long NORMAL_SEND_INTERVAL = 60000UL * 5;  // Sin alarma: cada 5 min
const unsigned long ALARM_SEND_INTERVAL = 10000UL;      // Con alarma: cada 10 s
const unsigned long OLED_STARTUP_DURATION = 1000;
const unsigned long SERIAL_DEBUG_INTERVAL = 2000;
unsigned long oledStartupStart = 0;
unsigned long lastSerialDebug = 0;
bool initialReadingShown = false;
bool alarmaActivaAnterior = false;

// Limits
float T_min, T_max, T_tol;
float H_min, H_max, H_tol;
float I_min, I_max, I_tol;
float O_min, O_max, O_tol;


// Alarm flags & legends
bool   alarma_T, alarma_H, alarma_I, alarma_O;
String ley_T, ley_H, ley_I, ley_O;
String fecha;


// Sensor
Adafruit_SHT31 sht31;

const int CANTIDAD_MUESTRAS_ADC = 80;
const unsigned long INTERVALO_MUESTRA_ADC_MS = 3;

struct LecturaAnalogica {
  int raw;
  float voltaje;
};

LecturaAnalogica medirAnalogico(uint8_t pin) {
  uint32_t sumaRaw = 0;
  uint32_t sumaMv = 0;

  analogRead(pin);
  analogReadMilliVolts(pin);
  delay(INTERVALO_MUESTRA_ADC_MS);

  for (int i = 0; i < CANTIDAD_MUESTRAS_ADC; i++) {
    sumaRaw += analogRead(pin);
    sumaMv += analogReadMilliVolts(pin);
    delay(INTERVALO_MUESTRA_ADC_MS);
  }

  LecturaAnalogica lectura;
  lectura.raw = sumaRaw / CANTIDAD_MUESTRAS_ADC;
  lectura.voltaje = (sumaMv / (float)CANTIDAD_MUESTRAS_ADC) / 1000.0;
  return lectura;
}

float calibrarLuzUwCm2(float voltaje) {
  // Curva por tramos medida contra equipo calibrado.
  // Salida en uW/cm2. El ultimo punto se usa como limite saturado.
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

float calibrarOxigenoPorcentaje(float voltaje) {
  // Ajuste lineal usando los puntos medios de las mediciones:
  // 0.907V=21%, 1.713V=36%, 1.9665V=39%, 2.0445V=43%, 2.320V=47%.
  const float pendiente = 18.39647163;
  const float ordenada = 4.26663649;
  float oxigeno = pendiente * voltaje + ordenada;

  if (oxigeno < 21.0) {
    return 21.0;
  }

  if (oxigeno > 100.0) {
    return 100.0;
  }

  return oxigeno;
}

// =========================
// LÓGICA DE SILENCIADO
// =========================
bool silenced = false;
unsigned long silenceStart = 0;
const unsigned long SILENCE_DURATION = 60000;   // 60 s


const unsigned long BUTTON_HOLD_TIME = 3000;    // 3 s


// =========================
// VARIABLES PARA BOTÓN + ISR
// =========================
volatile bool buttonEventPending = false;
volatile bool buttonLevelISR = LOW;
volatile unsigned long lastInterruptTime = 0;


// Variables procesadas en loop()
bool buttonCurrentlyPressed = false;
unsigned long buttonPressStartTime = 0;
bool holdActionExecuted = false;


// Servidores NTP
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";


// Zona horaria Argentina
const long gmtOffset_sec = -3 * 3600;
const int daylightOffset_sec = 0;


// =====================================================
// ISR DEL BOTÓN
// Solo registra estado y marca evento
// =====================================================
void IRAM_ATTR handleButtonInterrupt() {
  unsigned long nowMicros = micros();


  // debounce básico por tiempo
  if (nowMicros - lastInterruptTime < 50000) { // 50 ms
    return;
  }


  lastInterruptTime = nowMicros;
  buttonLevelISR = digitalRead(PIN_BOTON);
  buttonEventPending = true;
}


void connectWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");
  Serial.print("IP: "); Serial.println(WiFi.localIP());
}

void silenciarBuzzer() {
  silenced = true;
  silenceStart = millis();
  digitalWrite(PIN_BUZZER, LOW);
  Serial.println("Orden MQTT -> buzzer silenciado durante 60 s");
}

void manejarMensajeMQTT(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT recibido en: ");
  Serial.println(topic);

  if (String(topic) == mqtt_topic_silenciar) {
    silenciarBuzzer();
  }
}

void connectMQTT() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(manejarMensajeMQTT);
  while (!mqttClient.connected()) {
    String clientId = "ESP32_" + String(esp_id);
    Serial.print("Conectando MQTT...");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" OK");
      if (mqttClient.subscribe(mqtt_topic_silenciar.c_str())) {
        Serial.print("Suscripto a: ");
        Serial.println(mqtt_topic_silenciar);
      } else {
        Serial.println("No se pudo suscribir al tópico para silenciar la alarma");
      }
    } else {
      Serial.print(" Falló rc=");
      Serial.print(mqttClient.state());
      Serial.println(" → reintentando en 1s");
      delay(1000);
    }
  }
}

void cargarLimites() {
  Serial.println("Descargando límites...");
  HTTPClient http;
  snprintf(server_url, sizeof(server_url),
           "http://%s:%d/limites?esp=%d",
           limits_server_ip, limits_server_port, esp_id);
  http.begin(server_url);
  if (http.GET() == HTTP_CODE_OK) {
    String resp = http.getString();
    Serial.print("Respuesta recibida: ");
    Serial.println(resp);
    StaticJsonDocument<512> doc;
    auto err = deserializeJson(doc, resp);
    if (err) {
      Serial.print("JSON error: ");
      Serial.println(err.c_str());
    } else {
      T_min = doc["Tmin"];
      T_max = doc["Tmax"];
      T_tol = doc["Ttol"];
      H_min = doc["Hmin"];
      H_max = doc["Hmax"];
      H_tol = doc["Htol"];
      I_min = doc["Imin"];
      I_max = doc["Imax"];
      I_tol = doc["Itol"];
      O_min = doc["Omin"];
      O_max = doc["Omax"];
      O_tol = doc["Otol"];
      Serial.println("Límites cargados");
    }
  } else {
    /////////////////////////// BORRAR ESTO, ES PARA QUE LE FUNCIONE A LOLA
    T_min = 34;
    T_max = 38;
    T_tol = 1;
    H_min = 22;
    H_max = 60;
    H_tol = 1;
    I_min = 0.01;
    I_max = 0.8;
    I_tol = 0.01;
    O_min = 23;
    O_max = 40;
    O_tol = 2;
    ////////////////////////////
    Serial.print("HTTP error: ");
    Serial.println(http.GET());
  }
  http.end();
}



void configurarHora() {
  // Configura NTP y la zona horaria
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  Serial.println("Sincronizando hora via NTP…");
  struct tm timeinfo;
  // Espera hasta que tengamos hora válida
  for (int i = 0; i < 20; ++i) {
    if (getLocalTime(&timeinfo)) {
      Serial.println("Hora sincronizada correctamente");
      break;
    }
    Serial.print(".");
    delay(500);
  }
}

String obtenerTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(); // String vacío si no hay hora aún
  }
  char buf[25];
  // Formato: 2025-06-20T22:15:30
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buf);
}

bool publicarLectura(float t, float h, float l, float o) {
  // Verificar conexión MQTT
  Serial.print("¿MQTT conectado? ");
  Serial.println(mqttClient.connected() ? "✅ Sí" : "❌ No");
  if (!mqttClient.connected()) {
    Serial.println("🔌 Intentando reconectar MQTT...");
    connectMQTT();
  }
  Serial.print("Estado MQTT: ");
  Serial.println(mqttClient.state());

  // Construir JSON
  StaticJsonDocument<512> payload;
  payload["fecha"]   = fecha;
  payload["temperatura"] = t;
  payload["humedad"]     = h;
  payload["iluminancia"] = l;
  payload["oxigeno"]     = o;
  payload["Tmax"]=T_max;
  payload["Tmin"]=T_min;
  payload["Ttol"]=T_tol;
  payload["Hmax"]=H_max;
  payload["Hmin"]=H_min;
  payload["Htol"]=H_tol;
  payload["Imax"]=I_max;
  payload["Imin"]=I_min;
  payload["Itol"]=I_tol;
  payload["Omax"]=O_max;
  payload["Omin"]=O_min;
  payload["Otol"]=O_tol;
  payload["alarma_T"] = alarma_T;
  payload["alarma_H"] = alarma_H;
  payload["alarma_I"] = alarma_I;
  payload["alarma_O"] = alarma_O;
  payload["ley_T"] = ley_T;
  payload["ley_H"] = ley_H;
  payload["ley_I"] = ley_I;
  payload["ley_O"] = ley_O;
  payload["esp"] = esp_id;
  

  char bufJson[512];
  size_t n = serializeJson(payload, bufJson);
  Serial.print("Tamaño del payload: ");
  Serial.println(n);

  // Debug
  Serial.print("➡️ Topic: "); Serial.println(mqtt_topic);
  Serial.print("➡️ Payload: "); Serial.println(bufJson);

  // Publicar
  bool ok = mqttClient.publish(mqtt_topic.c_str(), bufJson, n);
  if (ok) {
    Serial.println("✅ Publicación exitosa");
  } else {
    Serial.println("❌ Falló la publicación MQTT");
  }
  return ok;
}

//////////////
void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(PIN_BOTON, INPUT);  // Botón como entrada
  pinMode(PIN_BUZZER, OUTPUT);        // Buzzer como salida
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LUZ_ANALOG, ADC_11db);
  analogSetPinAttenuation(PIN_O2_ANALOG, ADC_11db);

   // Buzzer apagado al inicio
  digitalWrite(PIN_BUZZER, LOW);
// Interrupción por cualquier cambio del botón
  attachInterrupt(digitalPinToInterrupt(PIN_BOTON), handleButtonInterrupt, CHANGE);

  connectWiFi();
  configurarHora();

  // Tópico estático para tu ESP
  mqtt_topic = "sensor/" + String(esp_id) + "/datos";
  mqtt_topic_silenciar = "incubadora/" + String(esp_id) + "/silenciar_alarma";

  connectMQTT();
  cargarLimites();

  // Init sensor y OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!sht31.begin(0x44)) {
    Serial.println("No detect SHT31");
    while (1) delay(1);
  }
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 12, "INICIANDO...");
  display.sendBuffer();
  oledStartupStart = millis();
}

void loop() {
  unsigned long now = millis();


  // =====================================================
  // 1) PROCESAR EVENTO DE BOTÓN CAPTURADO POR ISR
  // =====================================================
  if (buttonEventPending) {
    noInterrupts();
    bool level = buttonLevelISR;
    buttonEventPending = false;
    interrupts();


    if (level == HIGH) {
      buttonCurrentlyPressed = true;
      buttonPressStartTime = now;
      holdActionExecuted = false;
      Serial.println("ISR -> boton PRESIONADO");
    } else {
      buttonCurrentlyPressed = false;
      holdActionExecuted = false;
      Serial.println("ISR -> boton SUELTO");
    }
  }


  // Si sigue presionado, medir cuánto tiempo lleva
  if (buttonCurrentlyPressed && !holdActionExecuted) {
    if (now - buttonPressStartTime >= BUTTON_HOLD_TIME) {
      if ((alarma_H || alarma_T) && !silenced) {
        silenced = true;
        silenceStart = now;
        holdActionExecuted = true;


        digitalWrite(PIN_BUZZER, LOW); // OFF
        Serial.println("Boton mantenido 3 s -> alarma silenciada");
      }
    }
  }


  // =====================================================
  // 2) MQTT
  // =====================================================
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();


  // =====================================================
  // 3) LEER SENSORES
  // =====================================================
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  LecturaAnalogica lecturaLuz = medirAnalogico(PIN_LUZ_ANALOG);
  LecturaAnalogica lecturaO2 = medirAnalogico(PIN_O2_ANALOG);
  float lVoltaje = lecturaLuz.voltaje;
  float l = calibrarLuzUwCm2(lVoltaje);
  float o = calibrarOxigenoPorcentaje(lecturaO2.voltaje);


  // =====================================================
  // 4) EVALUAR ALARMAS
  // =====================================================
  alarma_T = ((t < T_min - T_tol) || (t > T_max + T_tol));
  if (alarma_T && (t < T_min - T_tol)) {
    ley_T = "baja";
  } else if (alarma_T && (t > T_max + T_tol)) {
    ley_T = "alta";
  } else {
    ley_T = "-";
  }


  alarma_H = ((h < H_min - H_tol) || (h > H_max + H_tol));
  if (alarma_H && (h < H_min - H_tol)) {
    ley_H = "baja";
  } else if (alarma_H && (h > H_max + H_tol)) {
    ley_H = "alta";
  } else {
    ley_H = "-";
  }


  alarma_I = ((l < I_min - I_tol) || (l > I_max + I_tol));
  if (alarma_I && (l < I_min - I_tol)) {
    ley_I = "baja";
  } else if (alarma_I && (l > I_max + I_tol)) {
    ley_I = "alta";
  } else {
    ley_I = "-";
  }


  alarma_O = ((o < O_min - O_tol) || (o > O_max + O_tol));
  if (alarma_O && (o < O_min - O_tol)) {
    ley_O = "bajo";
  } else if (alarma_O && (o > O_max + O_tol)) {
    ley_O = "alto";
  } else {
    ley_O = "-";
  }


  fecha = obtenerTimestamp();

  if (now - lastSerialDebug >= SERIAL_DEBUG_INTERVAL) {
    Serial.printf(
      "Lectura -> T: %.2f C | H: %.2f %% | Irr: %.3f uW/cm2 | O2: %.2f %% | Alarmas T:%d H:%d I:%d O:%d\n",
      t, h, l, o,
      alarma_T, alarma_H, alarma_I, alarma_O
    );
    lastSerialDebug = now;
  }


  // =====================================================
  // 5) BUZZER
  // =====================================================
  const unsigned long ahoraBuzzer = millis();
  if (alarma_H or alarma_T) {
    if (silenced) {
      if (ahoraBuzzer - silenceStart < SILENCE_DURATION) {
        digitalWrite(PIN_BUZZER, LOW);   // OFF
      } else {
        silenced = false;
        digitalWrite(PIN_BUZZER, BUZZER_HABILITADO ? HIGH : LOW);  // ON
        Serial.println("Fin del silencio de 60 s -> buzzer ON");
      }
    } else {
      digitalWrite(PIN_BUZZER, BUZZER_HABILITADO ? HIGH : LOW);    // ON
    }
  } else {
    silenced = false;
    digitalWrite(PIN_BUZZER, LOW);       // OFF
  }


  // =====================================================
  // 6) PUBLICACIÓN MQTT + OLED
  // =====================================================
  // Cualquier variable fuera de rango cambia la frecuencia de actualización.
  // El cambio de estado se comunica inmediatamente, sin esperar al intervalo.
  bool hayAlarma = alarma_T || alarma_H || alarma_I || alarma_O;
  bool cambioEstadoAlarma = hayAlarma != alarmaActivaAnterior;
  unsigned long intervaloActual = hayAlarma
      ? ALARM_SEND_INTERVAL
      : NORMAL_SEND_INTERVAL;
  bool shouldPublish = firstPublicationPending ||
                       cambioEstadoAlarma ||
                       (now - lastSend >= intervaloActual);

  if (shouldPublish) {
    // Tomar una muestra nueva justo en el instante de publicacion/OLED.
    t = sht31.readTemperature();
    h = sht31.readHumidity();
    lecturaLuz = medirAnalogico(PIN_LUZ_ANALOG);
    lecturaO2 = medirAnalogico(PIN_O2_ANALOG);
    lVoltaje = lecturaLuz.voltaje;
    l = calibrarLuzUwCm2(lVoltaje);
    o = calibrarOxigenoPorcentaje(lecturaO2.voltaje);

    alarma_T = ((t < T_min - T_tol) || (t > T_max + T_tol));
    if (alarma_T && (t < T_min - T_tol)) {
      ley_T = "baja";
    } else if (alarma_T && (t > T_max + T_tol)) {
      ley_T = "alta";
    } else {
      ley_T = "-";
    }

    alarma_H = ((h < H_min - H_tol) || (h > H_max + H_tol));
    if (alarma_H && (h < H_min - H_tol)) {
      ley_H = "baja";
    } else if (alarma_H && (h > H_max + H_tol)) {
      ley_H = "alta";
    } else {
      ley_H = "-";
    }

    alarma_I = ((l < I_min - I_tol) || (l > I_max + I_tol));
    if (alarma_I && (l < I_min - I_tol)) {
      ley_I = "baja";
    } else if (alarma_I && (l > I_max + I_tol)) {
      ley_I = "alta";
    } else {
      ley_I = "-";
    }

    alarma_O = ((o < O_min - O_tol) || (o > O_max + O_tol));
    if (alarma_O && (o < O_min - O_tol)) {
      ley_O = "bajo";
    } else if (alarma_O && (o > O_max + O_tol)) {
      ley_O = "alto";
    } else {
      ley_O = "-";
    }

    hayAlarma = alarma_T || alarma_H || alarma_I || alarma_O;

    if (publicarLectura(t, h, l, o)) {
      lastSend = now;
      firstPublicationPending = false;
    }
  }

  bool shouldUpdateDisplay = shouldPublish ||
                             (!initialReadingShown &&
                              now - oledStartupStart >= OLED_STARTUP_DURATION);

  if (shouldUpdateDisplay) {
    char buf[32];
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB08_tr);


    snprintf(buf, sizeof(buf), "Temp: %.1f C", t);
    display.drawStr(0, 14, buf);


    snprintf(buf, sizeof(buf), "Hum: %.0f %%", h);
    display.drawStr(0, 28, buf);


    snprintf(buf, sizeof(buf), "Irr: %.3f uW/cm2", l);
    display.drawStr(0, 42, buf);


    snprintf(buf, sizeof(buf), "O2:  %.1f %%", o);
    display.drawStr(0, 56, buf);


    display.sendBuffer();
    initialReadingShown = true;
  }

  alarmaActivaAnterior = hayAlarma;


  // =====================================================
  // 7) ACTUALIZACIÓN DE LÍMITES
  // =====================================================
  static unsigned long lastLimitsCheck = 0;
  const unsigned long LIMITS_UPDATE_INTERVAL = 10000; // reintenta cada 10 s


  if (now - lastLimitsCheck >= LIMITS_UPDATE_INTERVAL) {
    cargarLimites();
    lastLimitsCheck = now;
  }


  delay(5);
}
