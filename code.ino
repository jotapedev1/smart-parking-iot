#include <WiFi.h>
#include <PubSubClient.h>

// ---------- WiFi (rede simulada do Wokwi) ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// ---------- MQTT ----------
const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;

// Topico de presenca do dispositivo (Last Will publica "offline" aqui se o ESP32 cair)
const char* TOPICO_DISPOSITIVO = "vaga-inteligente/dispositivo/status";

const float LIMITE_DISTANCIA_CM = 40.0;
const unsigned long INTERVALO_ENVIO_MS = 2000;
const unsigned long INTERVALO_PISCA_MS = 150;

struct Vaga {
  const char* topico;
  int trig, echo, ledVermelho, ledVerde;
  // estado
  bool ocupada;
  bool primeiroEnvio;
  unsigned long ultimoEnvio;
  unsigned long ultimoPisca;
  bool ledEstado;
};

// Vaga 1: TRIG 5,  ECHO 18, LED vermelho 2,  LED verde 4
// Vaga 2: TRIG 19, ECHO 21, LED vermelho 22, LED verde 23
Vaga vagas[] = {
  {"vaga-inteligente/vaga/01/status", 5, 18, 2, 4,   false, true, 0, 0, false},
  {"vaga-inteligente/vaga/02/status", 19, 21, 22, 23, false, true, 0, 0, false},
};
const int NUM_VAGAS = sizeof(vagas) / sizeof(vagas[0]);

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

void conectaWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  Serial.print("Conectando ao WiFi");
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - inicio > 20000) {
      Serial.println(" timeout, reiniciando...");
      ESP.restart();
    }
  }
  Serial.print(" OK, IP: ");
  Serial.println(WiFi.localIP());
}

void conectaMQTT() {
  while (!mqtt.connected()) {
    String clientId = "esp32-vagas-" + String(random(0xffff), HEX);
    Serial.print("Conectando ao MQTT...");
    // connect(id, user, pass, willTopic, willQos, willRetain, willMessage)
    if (mqtt.connect(clientId.c_str(), NULL, NULL,
                     TOPICO_DISPOSITIVO, 0, true, "offline")) {
      Serial.println(" OK");
      mqtt.publish(TOPICO_DISPOSITIVO, "online", true);
    } else {
      Serial.print(" falhou, rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// Retorna a duracao do eco em us (0 = fora de alcance)
long mede(const Vaga& v) {
  digitalWrite(v.trig, LOW);
  delayMicroseconds(2);
  digitalWrite(v.trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(v.trig, LOW);
  return pulseIn(v.echo, HIGH, 30000);
}

void publica(const Vaga& v, float distancia, bool foraDeAlcance) {
  char payload[128];
  if (foraDeAlcance) {
    snprintf(payload, sizeof(payload),
             "{\"distancia\":null,\"status\":\"Disponivel\",\"ocupada\":false}");
  } else {
    snprintf(payload, sizeof(payload),
             "{\"distancia\":%.1f,\"status\":\"%s\",\"ocupada\":%s}",
             distancia, v.ocupada ? "Ocupada" : "Disponivel",
             v.ocupada ? "true" : "false");
  }
  mqtt.publish(v.topico, payload, true);  // retain = true
  Serial.print(v.topico);
  Serial.print(" -> ");
  Serial.println(payload);
}

void atualizaLeds(Vaga& v, unsigned long agora) {
  if (v.ocupada) {
    // Ocupada: vermelho fixo, verde apagado
    digitalWrite(v.ledVerde, LOW);
    digitalWrite(v.ledVermelho, HIGH);
  } else {
    // Livre: verde fixo, vermelho apagado
    digitalWrite(v.ledVermelho, LOW);
    digitalWrite(v.ledVerde, HIGH);
  }
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_VAGAS; i++) {
    pinMode(vagas[i].trig, OUTPUT);
    pinMode(vagas[i].echo, INPUT);
    pinMode(vagas[i].ledVermelho, OUTPUT);
    pinMode(vagas[i].ledVerde, OUTPUT);
    digitalWrite(vagas[i].trig, LOW);
  }

  conectaWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(5);  // broker detecta a queda em ~7 s
  conectaMQTT();
}

void loop() {
  if (!mqtt.connected()) conectaMQTT();
  mqtt.loop();

  for (int i = 0; i < NUM_VAGAS; i++) {
    Vaga& v = vagas[i];
    unsigned long agora = millis();

    long duracao = mede(v);
    float distancia = (duracao * 0.0343) / 2.0;
    bool foraDeAlcance = (duracao == 0);
    bool ocupadaAgora = !foraDeAlcance && (distancia < LIMITE_DISTANCIA_CM);

    bool mudou = (ocupadaAgora != v.ocupada);
    v.ocupada = ocupadaAgora;

    if (v.primeiroEnvio || mudou || agora - v.ultimoEnvio >= INTERVALO_ENVIO_MS) {
      publica(v, distancia, foraDeAlcance);
      v.ultimoEnvio = agora;
      v.primeiroEnvio = false;
    }

    atualizaLeds(v, agora);
    delay(30);  // evita um sensor captar o pulso do outro
  }
}
