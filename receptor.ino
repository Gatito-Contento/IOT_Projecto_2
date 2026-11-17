// ============================================================
// Estación meteorológica IoT distribuida - NODO RECEPTOR / GATEWAY (ESP32 #2)
// Recibe el payload por ESP-NOW y lo publica en ThingSpeak y Blynk.
//
// >>> Antes de compilar, reemplaza los textos entre comillas marcados
// >>> con "REEMPLAZAR" por tus propios datos (ver README.md).
// ============================================================

// ==========================================
// 1. CREDENCIALES DE BLYNK
//    (deben definirse ANTES de incluir BlynkSimpleEsp32.h)
// ==========================================
#define BLYNK_TEMPLATE_ID   "REEMPLAZAR_TEMPLATE_ID"      // ID de la plantilla (Blynk > Device Info)
#define BLYNK_TEMPLATE_NAME "Estacion Meteorologica"      // Nombre de tu plantilla en Blynk
#define BLYNK_AUTH_TOKEN    "REEMPLAZAR_AUTH_TOKEN"       // Token del dispositivo (Blynk > Device Info)

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <BlynkSimpleEsp32.h>
#include <ThingSpeak.h>

// ==========================================
// 2. CREDENCIALES DE WI-FI Y THINGSPEAK
// ==========================================
const char* ssid = "REEMPLAZAR_NOMBRE_WIFI";              // Nombre de tu red Wi-Fi (2.4 GHz)
const char* password = "REEMPLAZAR_CLAVE_WIFI";           // Contraseña de tu red Wi-Fi

unsigned long myChannelNumber = 0000000;                  // REEMPLAZAR: número (ID) de tu canal de ThingSpeak
const char* myWriteAPIKey = "REEMPLAZAR_WRITE_API_KEY";   // REEMPLAZAR: Write API Key de tu canal

WiFiClient client;
BlynkTimer timer;

// 3. ESTRUCTURA DEL PAYLOAD (igual al emisor)
typedef struct struct_message {
  int ldrValue;
  float humedad;
  float tempDHT;
  float presion;
  float tempBMP;
} struct_message;

struct_message myData;

// Variables de control y supervisión de enlace
volatile bool nuevosDatos = false;
volatile unsigned long ultimoTiempoRecepcion = 0;
const unsigned long TIMEOUT_OFFLINE = 5000;   // ms sin paquetes => fuera de línea
unsigned long contadorActualizaciones = 0;
bool alertaEnviada = false;
bool espNowOK = false;

// CONVERSIÓN ADC (0-4095) A PORCENTAJE
int calcularPorcentajeLuz(int lecturaCruda) {
  int valorLimitado = constrain(lecturaCruda, 0, 4095);
  return map(valorLimitado, 0, 4095, 0, 100);
}

// 4. CALLBACK DE RECEPCIÓN ESP-NOW
void OnDataRecv(const esp_now_recv_info_t * recv_info, const uint8_t * incomingData, int len) {
  if (len != sizeof(myData)) return;  // ignora paquetes de tamaño incorrecto
  memcpy(&myData, incomingData, sizeof(myData));
  ultimoTiempoRecepcion = millis();
  nuevosDatos = true;
}

// 5. ENVÍO A THINGSPEAK (cada 20 segundos)
void enviarAThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (ultimoTiempoRecepcion == 0 || (millis() - ultimoTiempoRecepcion) > TIMEOUT_OFFLINE) {
    Serial.println("[ThingSpeak] Emisor fuera de línea. Omitiendo subida.");
    return;
  }

  int luzPorcentaje = calcularPorcentajeLuz(myData.ldrValue);

  ThingSpeak.setField(1, myData.tempDHT);
  ThingSpeak.setField(2, myData.humedad);
  ThingSpeak.setField(3, myData.presion);
  ThingSpeak.setField(4, luzPorcentaje);
  ThingSpeak.setField(5, myData.tempBMP);

  int httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  if (httpCode == 200) {
    Serial.println("[ThingSpeak] Canal actualizado correctamente.");
  } else {
    Serial.print("[ThingSpeak] Error al actualizar. Código HTTP: ");
    Serial.println(httpCode);
  }
}

// 6. SUPERVISIÓN DE ESTADO Y ALERTA EN BLYNK (cada 1 s)
void verificarEstadoEmisor() {
  if (!Blynk.connected()) return;

  if (ultimoTiempoRecepcion > 0 && (millis() - ultimoTiempoRecepcion) <= TIMEOUT_OFFLINE) {
    Blynk.virtualWrite(V5, "En línea");
    Blynk.setProperty(V5, "color", "#23C48E");
    alertaEnviada = false;
  } else {
    Blynk.virtualWrite(V5, "⚠️ FUERA DE LÍNEA");
    Blynk.setProperty(V5, "color", "#D3435C");

    if (!alertaEnviada) {
      Serial.println("[ALERTA] El nodo emisor de sensores está FUERA DE LÍNEA (>5s).");
      Blynk.logEvent("nodo_offline", "⚠️ Alerta: El ESP32 emisor de sensores está fuera de línea.");
      alertaEnviada = true;
    }
  }
}

// 7. REVISIÓN DE WIFI (cada 10 s)
void revisarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Desconectado. Reintentando...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. WiFi en modo Estación
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // IMPORTANTE: evita perder paquetes ESP-NOW
  WiFi.setAutoReconnect(true);
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado.");
  Serial.print("MAC del Receptor: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Canal WiFi asignado por el router: ");
  Serial.println(WiFi.channel());

  // 2. ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    espNowOK = true;
  }

  // 3. ThingSpeak y Blynk
  ThingSpeak.begin(client);
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  // 4. Temporizadores
  timer.setInterval(20000L, enviarAThingSpeak);
  timer.setInterval(1000L, verificarEstadoEmisor);
  timer.setInterval(10000L, revisarWiFi);

  Serial.println("Gateway listo esperando datos...");
}

void loop() {
  Blynk.run();
  timer.run();

  if (nuevosDatos) {
    nuevosDatos = false;
    contadorActualizaciones++;

    int luzPorcentaje = calcularPorcentajeLuz(myData.ldrValue);

    Serial.println("===== Datos Recibidos =====");
    Serial.print("Canal actual: "); Serial.println(WiFi.channel());
    Serial.print("Luz (LDR): "); Serial.print(luzPorcentaje);
    Serial.print("% (Crudo: "); Serial.print(myData.ldrValue); Serial.println(")");
    Serial.print("Humedad: "); Serial.print(myData.humedad); Serial.println(" %");
    Serial.print("Temp DHT: "); Serial.print(myData.tempDHT); Serial.println(" °C");
    Serial.print("Presión: "); Serial.print(myData.presion); Serial.println(" hPa");
    Serial.print("Temp BMP: "); Serial.print(myData.tempBMP); Serial.println(" °C");
    Serial.println("===========================");

    if (Blynk.connected()) {
      Blynk.virtualWrite(V0, myData.tempDHT);
      Blynk.virtualWrite(V1, myData.humedad);
      Blynk.virtualWrite(V2, myData.presion);
      Blynk.virtualWrite(V3, luzPorcentaje);
      Blynk.virtualWrite(V4, myData.tempBMP);

      String infoActualizacion = "Paquete #" + String(contadorActualizaciones);
      Blynk.virtualWrite(V6, infoActualizacion);
    }
  }
}
