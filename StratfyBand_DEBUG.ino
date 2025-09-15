//Autor: Fábio Henrique Cabrini
//Resumo: Esse programa possibilita ligar e desligar o led onboard, além de mandar o status para o Broker MQTT possibilitando o Helix saber
//se o led está ligado ou desligado.
//Revisões:
//Rev1: 26-08-2023 Código portado para o ESP32 e para realizar a leitura de luminosidade e publicar o valor em um tópico aprorpiado do broker
//Autor Rev1: Lucas Demetrius Augusto
//Rev2: 28-08-2023 Ajustes para o funcionamento no FIWARE Descomplicado
//Autor Rev2: Fábio Henrique Cabrini
//Rev3: 1-11-2023 Refinamento do código e ajustes para o funcionamento no FIWARE Descomplicado
//Autor Rev3: Fábio Henrique Cabrini

#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_VERSION MQTT_VERSION_3_1_1

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <MPU6050.h>
#include <math.h>

// =================== Configurações - variáveis editáveis ===================
const int   band_ID                 = 2;
const char* default_SSID            = "Thinkphone";
const char* default_PASSWORD        = "pobrefeio";
const char* default_BROKER_MQTT     = "20.62.13.44";
const int   default_BROKER_PORT     = 1883;
const char* default_TOPICO_SUBSCRIBE= "/TEF/band010/cmd";
const char* default_TOPICO_PUBLISH_1= "/TEF/band010/attrs";
const char* default_TOPICO_PUBLISH_2= "/TEF/band010/attrs/scoreX";
const char* default_TOPICO_PUBLISH_3= "/TEF/band010/attrs/scoreY";
const char* default_TOPICO_PUBLISH_4= "/TEF/band010/attrs/scoreZ";
const char* default_ID_MQTT         = "fiware_band010";
const int   default_D4              = 2;

// Prefixo de mensagem
const char* topicPrefix = "band010";

// =================== Variáveis globais (usadas em todo o código) ===================
int   BAND_ID          = band_ID;
char* SSID             = const_cast<char*>(default_SSID);
char* PASSWORD         = const_cast<char*>(default_PASSWORD);
char* BROKER_MQTT      = const_cast<char*>(default_BROKER_MQTT);
int   BROKER_PORT      = default_BROKER_PORT;
char* TOPICO_SUBSCRIBE = const_cast<char*>(default_TOPICO_SUBSCRIBE);
char* TOPICO_PUBLISH_1 = const_cast<char*>(default_TOPICO_PUBLISH_1);
char* TOPICO_PUBLISH_2 = const_cast<char*>(default_TOPICO_PUBLISH_2);
char* TOPICO_PUBLISH_3 = const_cast<char*>(default_TOPICO_PUBLISH_3);
char* TOPICO_PUBLISH_4 = const_cast<char*>(default_TOPICO_PUBLISH_4);
char* ID_MQTT          = const_cast<char*>(default_ID_MQTT);
int   D4               = default_D4;

MPU6050 accelgyro;
WiFiClient espClient;
PubSubClient MQTT(espClient);

unsigned long lastTime;
float scoreX = 0;
float scoreY = 0;
float scoreZ = 0;

int  message = 0;
char EstadoSaida = '0';
bool emEvento    = false;

// Timers/holdoffs de reconexão (anti-spam)
static unsigned long lastWifiAttempt = 0;
static unsigned long lastMqttAttempt = 0;
const  unsigned long WIFI_RETRY_HOLDOFF_MS = 8000;
const  unsigned long MQTT_RETRY_HOLDOFF_MS = 4000;

// =================== Prototipos ===================
void initSerial();
void initWiFi();
void initMQTT();
void wifiWarmupOnce();
void reconectWiFi();
int  reconnectMQTT();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void VerificaConexoesWiFIEMQTT();
void EnviaEstadoOutputMQTT();
void InitOutput();
void handleAccel();

// =================== Implementações ===================
void initSerial() {
  Serial.begin(115200);
  delay(100);
}

void wifiWarmupOnce() {
  // Evita estados residuais na primeira subida
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // mais estável para MQTT
  WiFi.setAutoReconnect(true);   // o driver tenta, mas controlamos as tentativas
  WiFi.disconnect(true, true);   // limpa estado e NVS
  delay(150);
}

void initWiFi() {
  Serial.println("------ Conexao WI-FI ------");
  Serial.print("Conectando-se na rede: ");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWORD);

  // Espera inicial com timeout (não bloqueia para sempre)
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Wi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(D4, LOW);
  } else {
    Serial.println("❌ Timeout no Wi-Fi. O loop tentará novamente.");
  }
}

void initMQTT() {
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(mqtt_callback);
  MQTT.setKeepAlive(60);
  MQTT.setSocketTimeout(10);
  MQTT.setBufferSize(MQTT_MAX_PACKET_SIZE);
}

// =================== Setup/Loop ===================
void setup() {
  initSerial();
  InitOutput();

  wifiWarmupOnce();
  initWiFi();
  initMQTT();

  lastTime = millis();

  // I2C (ESP32-C3 Super Mini: 6/7 costumam ser utilizáveis; ajuste se necessário)
  Wire.begin(21, 22);
  accelgyro.initialize();

  delay(100);
  // Publica estado inicial apenas se já houver MQTT
  if (MQTT.connected()) {
    MQTT.publish(TOPICO_PUBLISH_1, "s|off");
  }
}

void loop() {
  VerificaConexoesWiFIEMQTT();
  EnviaEstadoOutputMQTT();

  if (emEvento) {
    handleAccel();
  }

  if (MQTT.connected()) {
    MQTT.loop();
  }

  // Pequeno yield para manter o loop responsivo
  delay(10);
}

// =================== Wi-Fi e MQTT ===================
void reconectWiFi() {
  // Evita chamar begin() em excesso e enquanto ainda está tentando
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWifiAttempt < WIFI_RETRY_HOLDOFF_MS) return;
  lastWifiAttempt = millis();

  Serial.print("🔌 WiFi.begin(\""); Serial.print(SSID); Serial.println("\")");
  // Opcional: garantir reset do estado antes de nova tentativa
  WiFi.disconnect(true, false);
  delay(50);
  WiFi.begin(SSID, PASSWORD);
}

int reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    // Sem rede, aborta tentativa de MQTT
    return 1;
  }
  if (millis() - lastMqttAttempt < MQTT_RETRY_HOLDOFF_MS) return 1;
  lastMqttAttempt = millis();

  Serial.print("* Tentando se conectar ao Broker MQTT: ");
  Serial.print(BROKER_MQTT); Serial.print(":"); Serial.println(BROKER_PORT);

  // Evita colisão de sessão com ClientID único
  String cid = String(ID_MQTT) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = MQTT.connect(cid.c_str());
  if (ok) {
    Serial.println("✅ Conectado ao broker MQTT!");
    MQTT.subscribe(TOPICO_SUBSCRIBE);
    return 0;
  } else {
    Serial.print("❌ Falha MQTT. state="); Serial.println(MQTT.state());
    return 1;
  }
}

void VerificaConexoesWiFIEMQTT() {
  reconectWiFi();  // 1º garante Wi-Fi
  if (WiFi.status() == WL_CONNECTED && !MQTT.connected()) {
    reconnectMQTT(); // 2º tenta MQTT
  }
}

// =================== MQTT payloads ===================
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.print("- Mensagem recebida: ");
  Serial.println(msg);

  String onTopic  = String(topicPrefix) + "@on|";
  String offTopic = String(topicPrefix) + "@off|";

  if (msg.equals(onTopic)) {
    digitalWrite(D4, HIGH);
    EstadoSaida = '1';
    emEvento = true;
  } else if (msg.equals(offTopic)) {
    EstadoSaida = '0';
    emEvento = false;

    // Publica scores somente se houver MQTT
    if (MQTT.connected()) {
      String mX = String(scoreX);
      String mY = String(scoreY);
      String mZ = String(scoreZ);
      MQTT.publish(TOPICO_PUBLISH_2, mX.c_str());
      MQTT.publish(TOPICO_PUBLISH_3, mY.c_str());
      MQTT.publish(TOPICO_PUBLISH_4, mZ.c_str());
    }
    scoreX = scoreY = scoreZ = 0;
  }
}

void EnviaEstadoOutputMQTT() {
  if (!MQTT.connected()) return;  // evita publish sem conexão

  if (EstadoSaida == '1') {
    MQTT.publish(TOPICO_PUBLISH_1, "s|on");
    Serial.println("- Durante evento");
  } else {
    MQTT.publish(TOPICO_PUBLISH_1, "s|off");
    Serial.println("- Fora de evento");
  }
  Serial.println("- Estado do evento enviado ao broker!");
  delay(1000);
}

void InitOutput() {
  pinMode(D4, OUTPUT);
  digitalWrite(D4, HIGH);
  bool toggle = false;
  for (int i = 0; i <= 10; i++) {
    toggle = !toggle;
    digitalWrite(D4, toggle);
    delay(200);
  }
}

// =================== Sensores/score ===================
void handleAccel() {
  int16_t ax, ay, az, gx, gy, gz;
  accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  // Converte para g / dps
  float ax_g = ax / 16384.0;
  float ay_g = ay / 16384.0;
  float az_g = az / 16384.0;

  float gx_dps = gx / 131.0;
  float gy_dps = gy / 131.0;
  float gz_dps = gz / 131.0;

  // Threshold para ruído
  if (fabs(ax_g) < 1) ax_g = 0;
  if (fabs(ay_g) < 1) ay_g = 0;
  if (fabs(az_g) < 1) az_g = 0;
  if (fabs(gx_dps) < 10 && fabs(gy_dps) < 10 && fabs(gz_dps) < 10) {
    return;
  }

  // Tempo para integração
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  // Score
  scoreX += fabs(ax_g) * dt;
  scoreY += fabs(ay_g) * dt;
  scoreZ += fabs(az_g) * dt;

  Serial.print("Score X: "); Serial.print(scoreX);
  Serial.print(" | Score Y: "); Serial.println(scoreY);
  Serial.print(" | Score Z: "); Serial.println(scoreZ);
}
