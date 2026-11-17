// ============================================================
// Estación meteorológica IoT distribuida - NODO EMISOR (ESP32 #1)
// Sensores: DHT11 (GPIO 4), BMP180 (I2C 21/22), LDR (GPIO 34)
// Comunicación: ESP-NOW hacia el ESP32 receptor/gateway
// ============================================================
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- Definición de Pines ---
#define LDR_PIN 34
#define DHT_PIN 4
#define DHT_TYPE DHT11   // Cambiar a DHT22 si se usa ese sensor

// --- Objetos de Sensores ---
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP085 bmp;
bool bmpOK = false;

// --- Dirección MAC del Receptor ---
// >>> REEMPLAZAR por la MAC de TU ESP32 receptor (se ve en su monitor serial).
// Ejemplo de formato: 11:22:33:44:55:66  ->  {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
uint8_t receiverAddress[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// --- Estructura de Datos (debe ser IGUAL en el Receptor) ---
typedef struct struct_message {
  int ldrValue;
  float humedad;
  float tempDHT;
  float presion;
  float tempBMP;
} struct_message;

struct_message myData;

// --- Control de canal / envío ---
const uint8_t CANAL_MIN = 1;
const uint8_t CANAL_MAX = 13;
uint8_t canalActual = 1;
bool canalFijado = false;
uint8_t fallosConsecutivos = 0;
const uint8_t MAX_FALLOS = 3;
bool espNowListo = false;

volatile bool callbackListo = false;
volatile bool ultimoEnvioOk = false;

// Último valor válido del DHT (evita enviar ceros falsos)
float ultimaHumedad = 0;
float ultimaTempDHT = 0;

// Callback de envío (núcleo ESP32 3.x)
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  ultimoEnvioOk = (status == ESP_NOW_SEND_SUCCESS);
  callbackListo = true;
}

// Cambia de canal
void fijarCanal(uint8_t canal) {
  esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
  canalActual = canal;
}

// Envía y espera el resultado del callback (true = el receptor dio ACK)
bool enviarYEsperar() {
  callbackListo = false;
  ultimoEnvioOk = false;

  esp_err_t r = esp_now_send(receiverAddress, (uint8_t *)&myData, sizeof(myData));
  if (r != ESP_OK) return false;

  unsigned long t0 = millis();
  while (!callbackListo && (millis() - t0) < 300) {
    delay(1);
  }
  return callbackListo && ultimoEnvioOk;
}

// Recorre los canales hasta que el receptor responda
bool buscarCanal() {
  Serial.println("Buscando canal del receptor...");
  for (uint8_t ch = CANAL_MIN; ch <= CANAL_MAX; ch++) {
    fijarCanal(ch);
    delay(20);
    for (int intento = 0; intento < 2; intento++) {
      if (enviarYEsperar()) {
        Serial.print("Receptor encontrado en canal ");
        Serial.println(ch);
        return true;
      }
    }
  }
  Serial.println("Receptor NO encontrado en ningún canal.");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Iniciando Emisor ESP-NOW...");

  // Sensores
  dht.begin();
  bmpOK = bmp.begin();
  Serial.println(bmpOK ? "BMP180 OK" : "Error: BMP180 no detectado");
  pinMode(LDR_PIN, INPUT);

  // WiFi en modo Estación (necesario para ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.print("MAC del Emisor: ");
  Serial.println(WiFi.macAddress());

  // ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  // Registrar receptor como peer (canal 0 = usa el canal actual)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Fallo al añadir Peer");
    return;
  }

  espNowListo = true;
  fijarCanal(1);
  Serial.println("Emisor listo.");
  Serial.println("-------------------------");
}

void loop() {
  if (!espNowListo) {
    Serial.println("ESP-NOW no inicializado. Reinicia el emisor.");
    delay(2000);
    return;
  }

  // Leer LDR (valor crudo 0-4095)
  myData.ldrValue = analogRead(LDR_PIN);

  // Leer DHT11 (conserva el último valor válido si falla)
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println("Error leyendo DHT11, usando último valor válido");
  } else {
    ultimaHumedad = h;
    ultimaTempDHT = t;
  }
  myData.humedad = ultimaHumedad;
  myData.tempDHT = ultimaTempDHT;

  // Leer BMP180
  if (bmpOK) {
    myData.presion = bmp.readPressure() / 100.0; // Pa -> hPa
    myData.tempBMP = bmp.readTemperature();
  } else {
    myData.presion = 0;
    myData.tempBMP = 0;
  }

  // Enviar
  bool ok;
  if (!canalFijado) {
    ok = buscarCanal();
    canalFijado = ok;
    fallosConsecutivos = 0;
  } else {
    ok = enviarYEsperar();
    if (ok) {
      fallosConsecutivos = 0;
    } else {
      fallosConsecutivos++;
      Serial.print("Fallo de envío (");
      Serial.print(fallosConsecutivos);
      Serial.println(")");
      if (fallosConsecutivos >= MAX_FALLOS) {
        Serial.println("Demasiados fallos: se reiniciará la búsqueda de canal.");
        canalFijado = false;
      }
    }
  }

  // Debug local
  Serial.print("[Canal "); Serial.print(canalActual);
  Serial.print(ok ? " OK] " : " FALLO] ");
  Serial.print("LDR:"); Serial.print(myData.ldrValue);
  Serial.print(" | Hum:"); Serial.print(myData.humedad);
  Serial.print("% | T.DHT:"); Serial.print(myData.tempDHT);
  Serial.print("C | Pres:"); Serial.print(myData.presion);
  Serial.print("hPa | T.BMP:"); Serial.print(myData.tempBMP);
  Serial.println("C");

  delay(2000);
}
