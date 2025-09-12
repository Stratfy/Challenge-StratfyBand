#include <WiFi.h>
#include <PubSubClient.h>

// --- CONFIGURAÇÃO ---
const char* SSID       = "SEU_WIFI";       // ajuste aqui
const char* PASSWORD   = "SUA_SENHA";      // ajuste aqui
const char* MQTT_HOST  = "52.180.152.18";  // broker
const int   MQTT_PORT  = 1883;             // porta (1883 = sem TLS, 8883 = TLS)
const char* MQTT_TOPIC = "test/hello";

// --- OBJETOS ---
WiFiClient espClient;
PubSubClient mqtt(espClient);

void setupWiFi() {
  Serial.print("Conectando no WiFi");
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  Serial.print("IP obtido: ");
  Serial.println(WiFi.localIP());
}

bool connectMQTT() {
  if (mqtt.connected()) return true;

  // Gera um ClientID único baseado no MAC do chip
  String clientId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("\nConectando ao broker %s:%d com ID %s...\n",
                MQTT_HOST, MQTT_PORT, clientId.c_str());

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("MQTT conectado!");
    mqtt.publish(MQTT_TOPIC, "oi do esp32");
    return true;
  } else {
    Serial.printf("Falha MQTT. state=%d\n", mqtt.state());
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  setupWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);     // evita problemas com payload maior
  mqtt.setSocketTimeout(5);     // segundos
}

void loop() {
  if (!connectMQTT()) {
    delay(2000);
    return;
  }
  mqtt.loop();
}
