# Smart Band (ESP32 + Acelerômetro) — Stratfy

Projeto de **Smart Band** com **ESP32** e acelerômetro integrado ao **FIWARE Descomplicado**.  
O dispositivo envia **dados do sensor** **ou** uma **pontuação (pseudo-score) calculada no edge**; esses dados serão consumidos para montar um **leaderboard de pontos**.

---

## ✨ Objetivos
- Ler aceleração (X/Y/Z) no ESP32.
- Conectar ao FIWARE Descomplicado (Orion).
- Publicar **atributos NGSI-v2** com dados crus **ou** `score`.
- Consumir os dados para exibir um **ranking (leaderboard)**.

---

## 🔌 Hardware (mínimo)
- ESP32 (Wi-Fi habilitado).
- Acelerômetro (ex.: **MPU6050**, **ADXL345** via **I²C**).
- Ligações I²C padrão ESP32: **SDA = GPIO21**, **SCL = GPIO22**.
- Alimentação 3V3, GND comum.

> Você pode trocar os pinos no código (`Wire.begin(SDA, SCL)`).

---

## 🧩 Estrutura de dados (NGSI-v2)

**Entidade (exemplo):**
- `id`: `urn:ngsi-ld:smartband:STRATFY:001`
- `type`: `smartband`

**Atributos (opções):**
- Dados crus:  
  - `accX`, `accY`, `accZ` — `Number` (m/s² ou g)
- OU pontuação no edge:
  - `score` — `Number` (métrica definida a partir de aceleração/atividade)

**Exemplo de payload (NGSI-v2):**
```json
{
  "accX": {"type": "Number", "value": -0.12},
  "accY": {"type": "Number", "value": 0.98},
  "accZ": {"type": "Number", "value": 9.71},
  "score": {"type": "Number", "value": 42}
}
```

**HTTP (update attrs):**
```
POST http://<ORION_HOST>:1026/v2/entities/urn:ngsi-ld:smartband:STRATFY:001/attrs
Headers:
  Content-Type: application/json
  Fiware-Service: <seu_service>
  Fiware-ServicePath: /
Body: (JSON acima)
```

---

## ⚙️ Variáveis de ambiente (sugestão `.env`)
```
WIFI_SSID=<sua_rede>
WIFI_PASS=<sua_senha>

ORION_URL=http://<ORION_HOST>:1026
FIWARE_SERVICE=<seu_service>
FIWARE_SERVICEPATH=/

ENTITY_ID=urn:ngsi-ld:smartband:STRATFY:001
ENTITY_TYPE=smartband
```

---

## 🚀 Passos iniciais
1. **Configurar ESP32 no Arduino IDE** (ou PlatformIO).
2. Instalar bibliotecas (exemplos):  
   - `Wire` (I²C padrão)  
   - Sensor (ex.: `MPU6050` ou `Adafruit_ADXL345_U`)
   - `WiFi.h` e `HTTPClient.h`
3. Ajustar `.env`/constantes no código (Wi-Fi, Orion, entidade).
4. Escolher modo de envio: **(A)** dados crus `accX/accY/accZ` **ou** **(B)** `score` calculado.
5. Fazer um **POST de teste** no Orion e verificar no histórico (se STH/QuantumLeap estiverem configurados).

---

## 🧪 Exemplo mínimo de envio (trecho C++/Arduino)
```cpp
#include <WiFi.h>
#include <HTTPClient.h>
// + libs do sensor (ex.: MPU6050/ADXL345)

const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASS";
String ORION = "http://<ORION_HOST>:1026";
String ENTITY = "urn:ngsi-ld:smartband:STRATFY:001";

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  // init I2C + sensor...
}

void loop() {
  // ler sensor -> ax, ay, az (ou calcular score)
  float ax = -0.12, ay = 0.98, az = 9.71;
  int score = 42; // TODO: definir regra de cálculo no edge

  String url = ORION + "/v2/entities/" + ENTITY + "/attrs";
  String payload = "{"accX":{"type":"Number","value":" + String(ax, 2) +
                   "},"accY":{"type":"Number","value":" + String(ay, 2) +
                   "},"accZ":{"type":"Number","value":" + String(az, 2) +
                   "},"score":{"type":"Number","value":" + String(score) + "}}";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Fiware-Service", "<seu_service>");
  http.addHeader("Fiware-ServicePath", "/");
  int code = http.POST(payload);
  http.end();

  delay(2000);
}
```

> **Importante:** manter consistência de unidades (g vs m/s²) e faixa do sensor (±2g, ±4g…).

---

## 📊 Leaderboard (consumo)
- Backend consulta Orion/BD histórico e agrega pontuações por `ENTITY_ID`.
- UI (web/app) exibe ranking atualizado (ex.: top N do dia/semana).
- Métricas sugeridas: soma de `score`, média ponderada, badges por marcos.

---

## 🗺️ Roadmap
- [ ] Definir regra de **pseudo-score** no edge.  
- [ ] Normalização/filtragem de ruído do sensor.  
- [ ] Publicação com QoS (buffer/retry).  
- [ ] Serviço de agregação e **API do leaderboard**.  
- [ ] Dashboard (web) com ranking e histórico.

---

## 👥 Time
**Stratfy**

---

## 📄 Licença (MIT)

Copyright (c) 2025 Stratfy

Permission is hereby granted, free of charge, to any person obtaining a copy  
of this software and associated documentation files (the “Software”), to deal  
in the Software without restriction, including without limitation the rights  
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell  
copies of the Software, and to permit persons to whom the Software is  
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in  
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE  
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER  
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN  
THE SOFTWARE.
