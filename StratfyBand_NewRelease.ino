//Autor: Fábio Henrique Cabrini
//Rev: MQTT robusto + filtros anti-estouro (baseline, dt cap) + publicação cadenciada

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_VERSION MQTT_VERSION_3_1_1

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <MPU6050.h>
#include <math.h>

// =================== Config ===================
const int   band_ID                 = 2;
const char* default_SSID            = "Thinkphone";
const char* default_PASSWORD        = "pobrefeio";
const char* default_BROKER_MQTT     = "20.62.13.44";
const int   default_BROKER_PORT     = 1883;

// Tópicos IoT Agent (UL 2.0 / MQTT)
const char* default_TOPICO_SUBSCRIBE= "/TEF/band010/cmd";
const char* default_TOPICO_PUBLISH_1= "/TEF/band010/attrs";          // UL: k1|v1|k2|v2...
const char* default_TOPICO_PUBLISH_2= "/TEF/band010/attrs/scoreX";   // alternativo: 1 por atributo
const char* default_TOPICO_PUBLISH_3= "/TEF/band010/attrs/scoreY";
const char* default_TOPICO_PUBLISH_4= "/TEF/band010/attrs/scoreZ";

const char* default_ID_MQTT         = "fiware_band010";  // vai ser encurtado p/ 23 chars
const int   default_D4              = 2;

const char* topicPrefix = "band010"; // payload de comando: "band010@on|" / "band010@off|"

// =================== Globais ===================
int   BAND_ID          = band_ID;
char* SSID             = (char*)default_SSID;
char* PASSWORD         = (char*)default_PASSWORD;
char* BROKER_MQTT      = (char*)default_BROKER_MQTT;
int   BROKER_PORT      = default_BROKER_PORT;
char* TOPICO_SUBSCRIBE = (char*)default_TOPICO_SUBSCRIBE;
char* TOPICO_PUBLISH_1 = (char*)default_TOPICO_PUBLISH_1;
char* TOPICO_PUBLISH_2 = (char*)default_TOPICO_PUBLISH_2;
char* TOPICO_PUBLISH_3 = (char*)default_TOPICO_PUBLISH_3;
char* TOPICO_PUBLISH_4 = (char*)default_TOPICO_PUBLISH_4;
char* ID_MQTT          = (char*)default_ID_MQTT;
int   D4               = default_D4;

MPU6050 accelgyro;
WiFiClient espClient;
PubSubClient MQTT(espClient);

// ---- Estado scores ----
float scoreX = 0, scoreY = 0, scoreZ = 0;
char  EstadoSaida = '0';
bool  emEvento    = false;

// ---- Tempo / heartbeat / publicação ----
unsigned long lastMs        = 0;         // p/ dt
unsigned long lastWifiTry   = 0;
unsigned long lastMqttTry   = 0;
unsigned long lastStatusMs  = 0;
unsigned long lastPubMs     = 0;

const  unsigned long WIFI_RETRY_MS   = 6000;
const  unsigned long MQTT_RETRY_MS   = 3000;
const  unsigned long STATUS_EVERY_MS = 1000;
const  unsigned long PUB_EVERY_MS    = 200;   // envia dados a cada 200 ms

// ---- Anti-estouro / filtros ----
const float DT_CAP        = 0.20f;  // dt máx 200 ms
const float A_CLAMP       = 4.0f;   // clamp de aceleração em g
const float DEADBAND_G    = 0.02f;  // 20 mg

// ---- Baseline (zeragem no ON) ----
float ax0=0, ay0=0, az0=0;
int   warmupLeft = 0;                 // amostras p/ baseline
const int WARMUP_SAMPLES = 20;

// =================== Helpers ===================
static String shortClientId(const char* base) {
  // PubSubClient aceita até 23 caracteres no clientId
  String mac = String((uint32_t)ESP.getEfuseMac(), HEX); // 8 hex
  String b   = String(base);
  // garante <= 23: ex: "fiwareb010-" (11) + mac(8) = 19
  if (b.length() > 12) b = b.substring(0, 12);
  return b + "-" + mac;
}

// =================== Prototipos ===================
void sendStatusHeartbeat();
void handleAccel();

// =================== Setup ===================
void setup() {
  Serial.begin(115200);
  pinMode(D4, OUTPUT);
  digitalWrite(D4, LOW);

  // Wi-Fi básico (sem resets agressivos)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SSID, PASSWORD);

  // MQTT
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setKeepAlive(60);
  MQTT.setSocketTimeout(15);
  MQTT.setBufferSize(MQTT_MAX_PACKET_SIZE);
  MQTT.setCallback([](char* topic, byte* payload, unsigned int length){
    String msg; msg.reserve(length);
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    Serial.printf("[MQTT] %s => %s\n", topic, msg.c_str());

    const String onTopic  = String(topicPrefix) + "@on|";
    const String offTopic = String(topicPrefix) + "@off|";

    if (msg == onTopic) {
      digitalWrite(D4, HIGH);
      EstadoSaida = '1';
      emEvento = true;

      // reseta contadores e baseline
      scoreX = scoreY = scoreZ = 0;
      warmupLeft = WARMUP_SAMPLES;
      ax0 = ay0 = az0 = 0;
      lastMs = millis();
      lastPubMs = 0; // libera a 1ª publicação

    } else if (msg == offTopic) {
      EstadoSaida = '0';
      emEvento = false;
      scoreX = scoreY = scoreZ = 0; // zera para próximo evento
      warmupLeft = 0;
      digitalWrite(D4, LOW);
    }
  });

  // I2C + MPU
  Wire.begin(21,22);
  accelgyro.initialize();

  lastMs = millis();
}

// =================== Reconexões ===================
void tryReconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiTry < WIFI_RETRY_MS) return;
  lastWifiTry = millis();
  Serial.println("[WiFi] tentando reconectar...");
  WiFi.reconnect();
}

void tryReconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (MQTT.connected()) return;
  if (millis() - lastMqttTry < MQTT_RETRY_MS) return;
  lastMqttTry = millis();

  String cid = shortClientId(ID_MQTT);
  Serial.printf("[MQTT] Conectando (%s:%d) cid=%s\n", BROKER_MQTT, BROKER_PORT, cid.c_str());
  if (MQTT.connect(cid.c_str())) {
    Serial.println("[MQTT] conectado!");
    MQTT.subscribe(TOPICO_SUBSCRIBE);
  } else {
    Serial.printf("[MQTT] falha state=%d\n", MQTT.state());
  }
}

// =================== Loop ===================
void loop() {
  // Conexões
  tryReconnectWiFi();
  tryReconnectMQTT();

  // Heartbeat (s|on / s|off) sem bloquear
  sendStatusHeartbeat();

  // Sensoriamento
  if (emEvento) handleAccel();

  // Mantém sessão MQTT viva
  MQTT.loop();

  delay(1);
}

// =================== Heartbeat ===================
void sendStatusHeartbeat() {
  if (!MQTT.connected()) return;
  unsigned long now = millis();
  if (now - lastStatusMs < STATUS_EVERY_MS) return;
  lastStatusMs = now;

  const char* payload = (EstadoSaida == '1') ? "s|on" : "s|off";
  if (!MQTT.publish(TOPICO_PUBLISH_1, payload)) {
    Serial.println("[MQTT] publish estado falhou");
  }
}

// =================== Sensores / Score ===================
void handleAccel() {
  unsigned long now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt <= 0) return;
  if (dt > DT_CAP) dt = DT_CAP;

  int16_t ax, ay, az, gx, gy, gz;
  accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  // g
  float ax_g = ax / 16384.0f;
  float ay_g = ay / 16384.0f;
  float az_g = az / 16384.0f;

  // ---- baseline (warm-up) ----
  if (warmupLeft > 0) {
    ax0 += ax_g; ay0 += ay_g; az0 += az_g;
    warmupLeft--;
    if (warmupLeft == 0) {
      ax0 /= (float)WARMUP_SAMPLES;
      ay0 /= (float)WARMUP_SAMPLES;
      az0 /= (float)WARMUP_SAMPLES;
      Serial.printf("[BASE] ax0=%.3f ay0=%.3f az0=%.3f\n", ax0, ay0, az0);
    }
    return; // não integra durante warm-up
  }

  // remove baseline
  ax_g -= ax0; ay_g -= ay0; az_g -= az0;

  // deadband + clamp
  if (fabs(ax_g) < DEADBAND_G) ax_g = 0;
  if (fabs(ay_g) < DEADBAND_G) ay_g = 0;
  if (fabs(az_g) < DEADBAND_G) az_g = 0;
  ax_g = constrain(ax_g, -A_CLAMP, A_CLAMP);
  ay_g = constrain(ay_g, -A_CLAMP, A_CLAMP);
  az_g = constrain(az_g, -A_CLAMP, A_CLAMP);

  // integra (energia de movimento)
  scoreX += fabs(ax_g) * dt;
  scoreY += fabs(ay_g) * dt;
  scoreZ += fabs(az_g) * dt;

  // Publica cadenciado
  if (!MQTT.connected()) return;
  if (now - lastPubMs < PUB_EVERY_MS) return;
  lastPubMs = now;

  // ① UL 2.0 — /attrs
  String ul = "scoreX|" + String(scoreX, 3) +
              "|scoreY|" + String(scoreY, 3) +
              "|scoreZ|" + String(scoreZ, 3);
  bool ok1 = MQTT.publish(TOPICO_PUBLISH_1, ul.c_str());

  // ② Por-atributo — /attrs/scoreX (mantém compatibilidade com seu fluxo antigo)
  bool ok2 = MQTT.publish(TOPICO_PUBLISH_2, String(scoreX, 3).c_str());
  bool ok3 = MQTT.publish(TOPICO_PUBLISH_3, String(scoreY, 3).c_str());
  bool ok4 = MQTT.publish(TOPICO_PUBLISH_4, String(scoreZ, 3).c_str());

  if (!(ok1 && ok2 && ok3 && ok4)) {
    Serial.printf("[MQTT] publish falhou (ok1=%d ok2=%d ok3=%d ok4=%d, state=%d)\n",
                  ok1, ok2, ok3, ok4, MQTT.state());
  }
}
