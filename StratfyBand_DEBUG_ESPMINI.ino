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

// Configurações - variáveis editáveis
const int band_ID = 2;
const char* default_SSID = "Google Mesh"; // Nome da rede Wi-Fi
const char* default_PASSWORD = "K@6r1n1trovao"; // Senha da rede Wi-Fi
const char* default_BROKER_MQTT = "20.62.13.44"; // IP do Broker MQTT
const int default_BROKER_PORT = 1883; // Porta do Broker MQTT
const char* default_TOPICO_SUBSCRIBE = "/TEF/band002/cmd"; // Tópico MQTT de escuta
const char* default_TOPICO_PUBLISH_1 = "/TEF/band002/attrs"; // Tópico MQTT de envio de informações para Broker
const char* default_TOPICO_PUBLISH_2 = "/TEF/band002/attrs/scoreX"; // Tópico MQTT de envio de informações para Broker
const char* default_TOPICO_PUBLISH_3 = "/TEF/band002/attrs/scoreY"; // Tópico MQTT de envio de informações para Broker
const char* default_TOPICO_PUBLISH_4 = "/TEF/band002/attrs/scoreZ";
const char* default_ID_MQTT = "fiware_band002"; // ID MQTT
const int default_D4 = 2; // Pino do LED onboard
// Configurações do DHT22

// Declaração da variável para o prefixo do tópico
const char* topicPrefix = "band002";

// Variáveis para configurações editáveis
int BAND_ID = band_ID;
char* SSID = const_cast<char*>(default_SSID);
char* PASSWORD = const_cast<char*>(default_PASSWORD);
char* BROKER_MQTT = const_cast<char*>(default_BROKER_MQTT);
int BROKER_PORT = default_BROKER_PORT;
char* TOPICO_SUBSCRIBE = const_cast<char*>(default_TOPICO_SUBSCRIBE);
char* TOPICO_PUBLISH_1 = const_cast<char*>(default_TOPICO_PUBLISH_1);
char* TOPICO_PUBLISH_2 = const_cast<char*>(default_TOPICO_PUBLISH_2);
char* TOPICO_PUBLISH_3 = const_cast<char*>(default_TOPICO_PUBLISH_3);
char* TOPICO_PUBLISH_4 = const_cast<char*>(default_TOPICO_PUBLISH_4);
char* ID_MQTT = const_cast<char*>(default_ID_MQTT);
int D4 = default_D4;



MPU6050 accelgyro;
WiFiClient espClient;
PubSubClient MQTT(espClient);

unsigned long lastTime;
float scoreX = 0;
float scoreY = 0;
float scoreZ = 0;

int message = 0;

char EstadoSaida = '0';
bool emEvento = false;

void initSerial() {
    Serial.begin(115200);
}

void initWiFi() {
    delay(10);
    Serial.println("------Conexao WI-FI------");
    Serial.print("Conectando-se na rede: ");
    Serial.println(SSID);
    Serial.println("Aguarde");
    reconectWiFi();
}

void initMQTT() {
   
    String clientId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.printf("\nConectando ao broker %s:%d com ID %s...\n",MQTT_HOST, MQTT_PORT, clientId.c_str()); 
    MQTT.setServer(BROKER_MQTT, BROKER_PORT);
    MQTT.setCallback(mqtt_callback);
    MQTT.setKeepAlive(60);        // <— aumenta keepalive
    MQTT.setSocketTimeout(10);
}

void setup() {
    InitOutput();
    initSerial();
    initWiFi();
    initMQTT();
    lastTime = millis();

    Wire.begin(21,22);
    accelgyro.initialize();
    delay(5000);
    MQTT.publish(TOPICO_PUBLISH_1, "s|off");
}

void loop() {
    if (Serial.available() > 0) {
        String entrada = Serial.readString();
        entrada.trim();

        if (entrada == "1") {
            Serial.println("🔧 Entrando no modo de configuração...");
            configurations();
        }
    }
    VerificaConexoesWiFIEMQTT();
    EnviaEstadoOutputMQTT();
    if(emEvento){
        handleAccel();
    }  
    MQTT.loop();
}

void reconectWiFi() {
    if (WiFi.status() == WL_CONNECTED)
        return;

    Serial.print("🔌 Conectando à rede: ");
    Serial.println(SSID);

    WiFi.begin(SSID, PASSWORD);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 10000; // 10 segundos

    // tenta até conectar ou atingir o timeout
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
        Serial.print(".");
    }

    // se conectou, mostra info
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("✅ Conectado com sucesso na rede!");
        Serial.print("SSID: ");
        Serial.println(SSID);
        Serial.print("IP obtido: ");
        Serial.println(WiFi.localIP());

        digitalWrite(D4, LOW);
    } else {
        Serial.println();
        Serial.println("❌ Falha ao conectar no WiFi dentro do tempo limite.");
        Serial.println("➡️ Tentará novamente em background...");

        // ⚠️ Não chamar configurations() aqui
        // Apenas deixar sem conexão e tentar de novo no loop
    }
}



void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (int i = 0; i < length; i++) {
        char c = (char)payload[i];
        msg += c;
    }
    Serial.print("- Mensagem recebida: ");
    Serial.println(msg);

    // Forma o padrão de tópico para comparação
    String onTopic = String(topicPrefix) + "@on|";
    String offTopic = String(topicPrefix) + "@off|";

    // Compara com o tópico recebido
    if (msg.equals(onTopic)) {
        digitalWrite(D4, HIGH);
        EstadoSaida = '1';
        emEvento = true;
    }
    

    if (msg.equals(offTopic)) {
        EstadoSaida = '0';
        emEvento = false;
        String mX = String(scoreX);
        String mY = String(scoreY);
        String mZ = String(scoreZ);
        MQTT.publish(TOPICO_PUBLISH_2, mX.c_str());
        MQTT.publish(TOPICO_PUBLISH_3, mY.c_str());
        MQTT.publish(TOPICO_PUBLISH_4, mZ.c_str());
        scoreX = 0, scoreY = 0, scoreZ = 0;
        
    }
}

void VerificaConexoesWiFIEMQTT() {
    if (!MQTT.connected())
        reconnectMQTT();
    reconectWiFi();
}

void EnviaEstadoOutputMQTT() {
    if (EstadoSaida == '1') {
        MQTT.publish(TOPICO_PUBLISH_1, "s|on");
        Serial.println("- Durante evento");
    }

    if (EstadoSaida == '0') {
        MQTT.publish(TOPICO_PUBLISH_1, "s|off");
        Serial.println("- Fora de evento");
    }
    Serial.println("- Estado do evento enviado ao broker!");
    delay(1000);
}

void InitOutput() {
    pinMode(D4, OUTPUT);
    digitalWrite(D4, HIGH);
    boolean toggle = false;

    for (int i = 0; i <= 10; i++) {
        toggle = !toggle;
        digitalWrite(D4, toggle);
        delay(200);
    }
}

void reconnectMQTT() {
    while (!MQTT.connected()) {
        Serial.print("* Tentando se conectar ao Broker MQTT: ");
        Serial.println(BROKER_MQTT);
        if (MQTT.connect(ID_MQTT)) {
            Serial.println("Conectado com sucesso ao broker MQTT!");
            MQTT.subscribe(TOPICO_SUBSCRIBE);
        } else {
            Serial.println("Falha ao reconectar no broker.");
            Serial.println("Haverá nova tentativa de conexão em 2s");
            Serial.println(MQTT.state());
            delay(2000);
        }
    }
}

//MUDAR PARA GIROSCÓPIO 
void handleAccel() {
    int16_t ax, ay, az, gx, gy, gz;
    accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Converte para g
    float ax_g = ax / 16384.0;
    float ay_g = ay / 16384.0;
    float az_g = az / 16384.0;

    // Converte giroscópio para °/s (LSB = 131 para ±250°/s)
    float gx_dps = gx / 131.0;
    float gy_dps = gy / 131.0;
    float gz_dps = gz / 131.0;

    // Threshold para descartar ruído
    if (fabs(ax_g) < 1) ax_g = 0;
    if (fabs(ay_g) < 1) ay_g = 0;
    if (fabs(az_g) < 1) az_g = 0;
    if (fabs(gx_dps) < 10 && fabs(gy_dps) < 10 && fabs(gz_dps) < 10) {
        // braço praticamente parado → não soma pontos
        return;
    }

    // Tempo para integração
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0;
    lastTime = now;

    // Score só quando há aceleração + movimento real
    scoreX += fabs(ax_g) * dt;
    scoreY += fabs(ay_g) * dt;
    scoreZ += fabs(az_g) * dt;

    Serial.print("Score X: "); Serial.print(scoreX);
    Serial.print(" | Score Y: "); Serial.println(scoreY);
    Serial.print(" | Score Z: "); Serial.println(scoreZ);

}

void configurations() {
    Serial.println("Configurações na serial\n10. SSID\n11. BROKER MQTT\n12. ID DA BAND");

    while (Serial.available() == 0) {}  // espera entrada
    String command = Serial.readString();
    command.trim();  // remove espaços e quebras de linha

    if (command == "10") {
        Serial.println("Digite o novo SSID: ");
        while (Serial.available() == 0) {}
        String newMessage = Serial.readString();
        newMessage.trim();
        SSID = strdup(newMessage.c_str());  // duplica string para memória estática

        Serial.println("Digite a nova senha: ");
        while (Serial.available() == 0) {}
        newMessage = Serial.readString();
        newMessage.trim();
        PASSWORD = strdup(newMessage.c_str());

    } else if (command == "11") {
        Serial.println("Digite o novo IP: ");
        while (Serial.available() == 0) {}
        String newMessage = Serial.readString();
        newMessage.trim();
        BROKER_MQTT = strdup(newMessage.c_str());

    } else if (command == "12") {
        Serial.println("Digite o novo ID da BAND: ");
        while (Serial.available() == 0) {}
        String newMessage = Serial.readString();
        newMessage.trim();
        BAND_ID = newMessage.toInt();

    } else {
        Serial.println("Nada selecionado.");
    }

    initWiFi();
    initMQTT();
}

