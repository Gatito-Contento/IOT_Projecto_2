# Estación meteorológica IoT distribuida

Proyecto 2 – Internet de las Cosas · Ingeniería Mecatrónica · Universidad Santo Tomás, Bucaramanga

**Integrantes:** Samuel Joel García Roa · Ebett Stiven Sanchez Corredor

Sistema de dos ESP32: un **nodo emisor** que mide temperatura, humedad, presión e iluminación y las envía por **ESP-NOW** a un **nodo receptor (gateway)**, que las publica en **ThingSpeak** (histórico) y **Blynk** (tiempo real).

```
ESP32 emisor ──ESP-NOW──► ESP32 receptor ──Wi-Fi──► ThingSpeak
 DHT11, BMP180, LDR                       └────────► Blynk (app / web)
```

## Archivos

| Archivo | Descripción |
|---|---|
| `emisor/emisor.ino` | Código del ESP32 que lee los sensores y transmite |
| `receptor/receptor.ino` | Código del ESP32 gateway que recibe y publica en la nube |

## Qué debes hacer para usarlo

Los datos personales (contraseñas, tokens, claves y la MAC) fueron reemplazados por textos genéricos. Debes completarlos con los tuyos **antes de compilar**.

### 1. Receptor (`receptor/receptor.ino`)

| Dónde está | Qué poner |
|---|---|
| `BLYNK_TEMPLATE_ID` | ID de tu plantilla de Blynk (en Blynk: *Developer Zone > Templates*, o *Device Info* del dispositivo) |
| `BLYNK_TEMPLATE_NAME` | Nombre exacto de tu plantilla de Blynk |
| `BLYNK_AUTH_TOKEN` | Token de tu dispositivo (Blynk > *Devices* > tu dispositivo > *Device Info*) |
| `ssid` | Nombre de tu red Wi-Fi (debe ser de **2.4 GHz**) |
| `password` | Contraseña de esa red Wi-Fi |
| `myChannelNumber` | Número (Channel ID) de tu canal de ThingSpeak, sin comillas |
| `myWriteAPIKey` | Write API Key de tu canal (ThingSpeak > tu canal > *API Keys*) |

### 2. Obtener la MAC del receptor y ponerla en el emisor

1. Sube `receptor.ino` al ESP32 receptor y abre el monitor serial a **115200** baudios.
2. Copia la línea `MAC del Receptor: XX:XX:XX:XX:XX:XX`.
3. En `emisor/emisor.ino`, reemplaza el arreglo `receiverAddress` por esa MAC, escribiendo cada par con el prefijo `0x`.
   Ejemplo: `11:22:33:44:55:66` se escribe `{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}`.
4. Sube `emisor.ino` al ESP32 emisor.

### 3. Configurar ThingSpeak

Crea un canal con 5 campos:

| Campo | Variable |
|---|---|
| Field 1 | Temperatura DHT11 (°C) |
| Field 2 | Humedad relativa (%) |
| Field 3 | Presión atmosférica (hPa) |
| Field 4 | Iluminación (%) |
| Field 5 | Temperatura BMP180 (°C) |

### 4. Configurar Blynk

Crea una plantilla con estos Datastreams (pines virtuales) y vincúlalos a widgets:

| Pin | Tipo | Contenido |
|---|---|---|
| V0 | Double | Temperatura DHT11 (°C) |
| V1 | Double | Humedad (%) |
| V2 | Double | Presión (hPa) |
| V3 | Integer | Iluminación (%) |
| V4 | Double | Temperatura BMP180 (°C) |
| V5 | String | Estado: En línea / FUERA DE LÍNEA |
| V6 | String | Número de paquete recibido |

Para la alerta de desconexión, crea en la plantilla un **evento** con el código `nodo_offline`.

## Conexiones del emisor

| Elemento | Pin del ESP32 | Observación |
|---|---|---|
| DHT11 (DATA) | GPIO 4 | Alimentar a 3.3 V; pull-up de 10 kΩ si el módulo no la incluye |
| BMP180 SDA | GPIO 21 | Alimentar a 3.3 V (dirección I2C 0x77) |
| BMP180 SCL | GPIO 22 | |
| LDR + resistencia de 10 kΩ | GPIO 34 (ADC1) | Divisor de tensión entre 3.3 V y GND |

El receptor no lleva sensores, solo alimentación por USB.

## Librerías (Arduino IDE, núcleo ESP32 3.x)

Instálalas desde el Gestor de librerías:

- **DHT sensor library** (Adafruit), junto con *Adafruit Unified Sensor*
- **Adafruit BMP085 Library**
- **Blynk**
- **ThingSpeak** (MathWorks)

`WiFi`, `Wire`, `esp_now` y `esp_wifi` vienen incluidas con el núcleo del ESP32.

Para usar un DHT22 en lugar del DHT11, cambia `#define DHT_TYPE DHT11` por `DHT22` en el emisor.

## Funcionamiento

**Emisor**
- Cada 2 s lee la LDR (valor crudo 0–4095), el DHT11 y el BMP180 (presión en hPa y temperatura).
- Envía un paquete de 20 bytes por ESP-NOW. Si el DHT11 falla, conserva la última lectura válida.
- Recorre los canales Wi-Fi 1 a 13 hasta que el receptor confirma la recepción. Si fallan 3 envíos seguidos, vuelve a buscar el canal.

**Receptor**
- Se conecta al Wi-Fi y recibe los paquetes por ESP-NOW.
- Convierte la LDR a porcentaje y muestra los datos por el monitor serial.
- Actualiza Blynk con cada paquete y sube a ThingSpeak cada 20 s (límite de la cuenta gratuita: 15 s).
- Si pasan más de 5 s sin recibir paquetes, marca al emisor como **FUERA DE LÍNEA** en Blynk, envía la alerta `nodo_offline` y omite la subida a ThingSpeak.
- Revisa el Wi-Fi cada 10 s y reconecta automáticamente si se cae.

## Solución de problemas

| Síntoma | Causa probable |
|---|---|
| El emisor imprime "Receptor NO encontrado" | MAC incorrecta en `receiverAddress` o receptor apagado |
| El emisor imprime "BMP180 no detectado" | Revisar SDA/SCL, alimentación de 3.3 V y dirección I2C |
| El receptor no se conecta al Wi-Fi | Red de 5 GHz, nombre o contraseña incorrectos |
| Blynk no conecta | `BLYNK_TEMPLATE_ID` o `BLYNK_AUTH_TOKEN` incorrectos |
| ThingSpeak devuelve un código distinto de 200 | Write API Key o número de canal incorrectos, o menos de 15 s entre subidas |

> No subas tus credenciales reales a un repositorio público.
