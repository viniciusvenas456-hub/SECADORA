#include <esp_system.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 4
#define PIN_HEATER   2
#define PIN_FAN      3
#define PIN_LED      8          // LED azul da placa (ativo em LOW)

// OLED 0.42" 72x40
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

Adafruit_BME280 bme;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
WebServer server(80);
Preferences prefs;

struct PerfilSecagem {
  String nome;
  float tempAlvo;
  int tempoRampaMin;
  int tempoPatamarMin;
  int tempoCoolMin;
};

PerfilSecagem perfis[] = {
  {"Parado", 0.0, 0, 0, 0},
  {"PLA",    50.0, 15, 240, 15},
  {"PETG",   65.0, 20, 360, 20},
  {"ASA",    80.0, 25, 360, 25},
  {"ABS",    80.0, 25, 360, 25}
};

int perfilAtual = 0;
unsigned long inicioCicloMs = 0;
float tempInicialCiclo = 25.0;
float setpointAtual = 0.0;
String faseAtual = "Aguardando";

float t1 = 0, t2 = 0, tAmb = 0, umid = 0;
int pwmHeater = 0, pwmFan = 0;
String statusWifiMsg = "Sem rede";
bool oledOk = false;
bool bmeOk = false;
int dsCount = 0;
volatile int ultimoMotivo = 0;
unsigned long ultimaBuscaDs = 0;
bool dsOk[2] = {false, false};
unsigned long ultimoT1Ok = 0, ultimoT2Ok = 0;
bool falhaSensor = false;
unsigned long ultimoIpSerial = 0, ultimaTentativaWifi = 0;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Filament Care C3</title>
<style>
:root{--bg:#0b1220;--p:#132034;--p2:#1b2b43;--t:#edf4fc;--m:#a7b7cc;--a:#59d6a4;--w:#f59e0b;--d:#ef4444;--line:#2b3d55}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--t);font-family:system-ui,sans-serif;padding:20px}
.wrap{max-width:900px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px;margin:16px 0}
.card{background:var(--p);border:1px solid var(--line);border-radius:16px;padding:18px}
.val{font-size:32px;font-weight:800;margin:6px 0;color:var(--a)}.muted{color:var(--m);font-size:13px}
.btns{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px;margin-top:12px}
button{padding:14px;border-radius:12px;border:1px solid var(--line);background:var(--p2);color:var(--t);font-weight:700;cursor:pointer;font-size:15px}
button:hover{border-color:var(--a)}button.stop{background:#3b161c;border-color:var(--d);color:#fca5a5}
.bar{height:10px;background:#0b1525;border-radius:99px;overflow:hidden;margin-top:10px}.fill{height:100%;background:var(--a);width:0%;transition:.4s}
</style></head><body><div class="wrap">
<h2>Filament Care C3</h2>
<div class="card">
  <div style="display:flex;justify-content:space-between;align-items:center">
    <div><span class="muted">STATUS DO CICLO</span><h3 style="margin:4px 0" id="fase">Carregando...</h3></div>
    <div style="text-align:right"><span class="muted">ALVO ATUAL</span><h3 style="margin:4px 0;color:var(--w)" id="sp">-- °C</h3></div>
  </div>
  <div class="bar"><div class="fill" id="prog"></div></div>
  <div class="muted" style="margin-top:8px" id="tempo">Tempo decorrido: 0 min</div>
</div>
<div class="grid">
  <div class="card"><div class="muted">CÂMARA</div><div class="val" id="t1">-- °C</div></div>
  <div class="card"><div class="muted">AQUECEDOR</div><div class="val" id="t2">-- °C</div></div>
  <div class="card"><div class="muted">UMIDADE</div><div class="val" id="hum">-- %</div></div>
  <div class="card"><div class="muted">POTÊNCIA / FAN</div><div class="val" id="pwr">--% / --%</div></div>
</div>
<div class="card">
  <h3 style="margin-top:0">Iniciar Rampa</h3>
  <div class="btns">
    <button onclick="setP(1)">PLA<br><span class="muted">50°C · 4h30</span></button>
    <button onclick="setP(2)">PETG<br><span class="muted">65°C · 6h40</span></button>
    <button onclick="setP(3)">ASA<br><span class="muted">80°C · 6h50</span></button>
    <button onclick="setP(4)">ABS<br><span class="muted">80°C · 6h50</span></button>
    <button class="stop" onclick="setP(0)">PARAR</button>
  </div>
</div></div>
<script>
function setP(id){fetch('/set?p='+id).then(()=>atualizar())}
function atualizar(){
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('t1').innerText=d.t1.toFixed(1)+' °C';
    document.getElementById('t2').innerText=d.t2.toFixed(1)+' °C';
    document.getElementById('hum').innerText=d.hum.toFixed(1)+' %';
    document.getElementById('fase').innerText=d.perfil+' — '+d.fase;
    document.getElementById('sp').innerText=d.sp.toFixed(1)+' °C';
    document.getElementById('pwr').innerText=Math.round(d.heater/2.55)+'% / '+Math.round(d.fan/2.55)+'%';
    document.getElementById('tempo').innerText='Decorrido: '+d.minDecorridos+' min / Total: '+d.minTotal+' min';
    let pct=d.minTotal>0?Math.min(100,(d.minDecorridos/d.minTotal)*100):0;
    document.getElementById('prog').style.width=pct+'%';
  });
}
setInterval(atualizar,2000);atualizar();
</script></body></html>
)rawliteral";

bool parseIP(const String &str, IPAddress &ipOut) {
  String s = str;
  s.trim();
  if (s.length() == 0) return false;
  return ipOut.fromString(s);
}

void reportarStatusSerial() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI_OK_IP:" + WiFi.localIP().toString());
  } else {
    Serial.println("WIFI_DESCONECTADO");
  }
}

void conectarWifiSalvo() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  String ipStr = prefs.getString("ip", "");
  String gwStr = prefs.getString("gw", "");
  String snStr = prefs.getString("sn", "255.255.255.0");
  String dnsStr = prefs.getString("dns", "8.8.8.8");
  prefs.end();

  if (ssid.length() == 0) {
    statusWifiMsg = "Sem SSID";
    Serial.println("WIFI_SEM_CONFIG");
    WiFi.mode(WIFI_STA);   // liga a pilha de rede; sem isso server.begin() derruba a placa (PANIC)
    return;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  IPAddress localIP, gateway, subnet, dns;
  if (parseIP(ipStr, localIP)) {
    if (!parseIP(gwStr, gateway)) gateway = IPAddress(localIP[0], localIP[1], localIP[2], 1);
    if (!parseIP(snStr, subnet))  subnet  = IPAddress(255, 255, 255, 0);
    if (!parseIP(dnsStr, dns))    dns     = IPAddress(8, 8, 8, 8);
    WiFi.config(localIP, gateway, subnet, dns);
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // placas ESP32-C3 mini costumam falhar em potência máxima
  ultimoMotivo = 0;
  Serial.printf("Conectando em '%s' (%d caracteres de senha, IP %s)\n", ssid.c_str(), (int)pass.length(), ipStr.length() ? ipStr.c_str() : "automatico");
  WiFi.begin(ssid.c_str(), pass.c_str());
  statusWifiMsg = "Conectando...";
  Serial.println("Tentando conectar WiFi...");

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    // Pisca o LED enquanto procura Wi-Fi
    digitalWrite(PIN_LED, LOW);    // acende
    delay(200);
    digitalWrite(PIN_LED, HIGH);   // apaga
    delay(300);
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    statusWifiMsg = WiFi.localIP().toString();
    Serial.println("WIFI_OK_IP:" + statusWifiMsg);
  } else {
    statusWifiMsg = "Falha Wi-Fi";
    Serial.println("WIFI_MOTIVO:" + String(ultimoMotivo) + " status:" + String((int)WiFi.status()));
    Serial.println("WIFI_FALHA");
  }
}

void lerSensores() {
  if (dsCount == 0) {
    // sem sensor: só tenta achar de novo a cada 5 s, sem tocar no barramento a cada ciclo
    if (millis() - ultimaBuscaDs > 5000UL) {
      ultimaBuscaDs = millis();
      ds18b20.begin();
      dsCount = ds18b20.getDeviceCount();
    }
    falhaSensor = true;
    return;
  }
  ds18b20.requestTemperatures();
  float r1 = ds18b20.getTempCByIndex(0);
  float r2 = ds18b20.getTempCByIndex(1);
  unsigned long agora = millis();
  // -127 = sensor ausente; 85.0 exato = valor de power-on do DS18B20
  if (r1 > -50 && r1 < 125 && r1 != 85.0) { t1 = r1; ultimoT1Ok = agora; }
  if (r2 > -50 && r2 < 125 && r2 != 85.0) { t2 = r2; ultimoT2Ok = agora; }
  if (bmeOk) {
    float h = bme.readHumidity();
    float t = bme.readTemperature();
    if (!isnan(h)) umid = h;
    if (!isnan(t)) tAmb = t;
  }
  // sem leitura válida por 10 s = falha (o aquecedor é cortado)
  falhaSensor = (agora - ultimoT1Ok > 10000UL) || (agora - ultimoT2Ok > 10000UL);
}

void processarRampaEControle() {
  if (perfilAtual == 0) {
    setpointAtual = 0;
    faseAtual = "Parado";
    pwmHeater = 0;
    pwmFan = (t1 > 38.0) ? 160 : 0;
    analogWrite(PIN_HEATER, pwmHeater);
    analogWrite(PIN_FAN, pwmFan);
    return;
  }

  PerfilSecagem p = perfis[perfilAtual];
  float decorridoMin = (millis() - inicioCicloMs) / 60000.0f;
  int totalMin = p.tempoRampaMin + p.tempoPatamarMin + p.tempoCoolMin;

  if (falhaSensor) {
    faseAtual = "FALHA SENSOR";
    setpointAtual = 0;
    pwmHeater = 0;
    pwmFan = 255;
    analogWrite(PIN_HEATER, 0);
    analogWrite(PIN_FAN, pwmFan);
    return;
  }

  if (decorridoMin < p.tempoRampaMin) {
    faseAtual = "1. Rampa";
    float progresso = decorridoMin / (float)p.tempoRampaMin;
    setpointAtual = tempInicialCiclo + (p.tempAlvo - tempInicialCiclo) * progresso;
    pwmFan = 200;
  } else if (decorridoMin < (p.tempoRampaMin + p.tempoPatamarMin)) {
    faseAtual = "2. Patamar";
    setpointAtual = p.tempAlvo;
    pwmFan = 255;
  } else if (decorridoMin < totalMin) {
    faseAtual = "3. Resfriando";
    setpointAtual = 0;
    pwmHeater = 0;
    pwmFan = 255;
    analogWrite(PIN_HEATER, 0);
    analogWrite(PIN_FAN, pwmFan);
    return;
  } else {
    perfilAtual = 0;
    return;
  }

  if (t2 > 105.0 || t1 > 92.0) {
    pwmHeater = 0;
    pwmFan = 255;
  } else {
    float erro = setpointAtual - t1;
    pwmHeater = (erro <= 0) ? 0 : constrain((int)(erro * 45.0), 40, 255);
  }

  analogWrite(PIN_HEATER, pwmHeater);
  analogWrite(PIN_FAN, pwmFan);
}

void verificarComandosSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') { if (c != '\r' && buf.length() < 250) buf += c; continue; }
    String cmd = buf; buf = "";
    cmd.trim();

    if (cmd == "GET_IP") { reportarStatusSerial(); continue; }
    if (cmd == "SCAN") {
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n; i++) {
        String nome = WiFi.SSID(i);
        if (nome.length() == 0 || nome.indexOf('|') >= 0) continue;
        Serial.println("NET:" + nome + "|" + String(WiFi.RSSI(i)));
      }
      WiFi.scanDelete();
      Serial.println("SCAN_END");
      continue;
    }
    if (!cmd.startsWith("WIFI:")) continue;

    String dados = cmd.substring(5);
    String partes[6];
    int idx = 0, start = 0;
    while (idx < 6) {
      int pos = dados.indexOf('|', start);
      if (pos == -1) { partes[idx++] = dados.substring(start); break; }
      partes[idx++] = dados.substring(start, pos);
      start = pos + 1;
    }
    if (partes[0].length() == 0) { Serial.println("WIFI_FALHA"); continue; }

    prefs.begin("wifi", false);
    prefs.putString("ssid", partes[0]);
    prefs.putString("pass", partes[1]);
    prefs.putString("ip",   partes[2]);
    prefs.putString("gw",   partes[3]);
    prefs.putString("sn",   partes[4].length() ? partes[4] : "255.255.255.0");
    prefs.putString("dns",  partes[5].length() ? partes[5] : "8.8.8.8");
    prefs.end();

    conectarWifiSalvo();
  }
}

// LED sem travar o programa: pisca rápido = conectando, lento = sem Wi-Fi/sem rede, aceso = conectado
void atualizarLed() {
  unsigned long m = millis();
  bool aceso;
  if (WiFi.status() == WL_CONNECTED) aceso = true;
  else if (statusWifiMsg == "Sem SSID" || statusWifiMsg == "Sem rede") aceso = (m % 2000) < 1000;
  else aceso = (m % 500) < 250;
  digitalWrite(PIN_LED, aceso ? LOW : HIGH);   // ativo em LOW
}

// Reconecta sozinho se a rede cair e repete o IP pela USB (o navegador pode abrir a porta depois do boot)
void manutencaoWifi() {
  unsigned long m = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (m - ultimoIpSerial > 2000) {
      ultimoIpSerial = m;
      Serial.println("WIFI_OK_IP:" + WiFi.localIP().toString());
    }
  } else if (statusWifiMsg != "Sem SSID" && m - ultimaTentativaWifi > 30000UL) {
    ultimaTentativaWifi = m;
    WiFi.reconnect();
  }
}

void atualizarOLED() {
  if (!oledOk) return;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_tr);

  // Linha 1 - IP ou status
  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(0, 8, WiFi.localIP().toString().c_str());
  } else {
    u8g2.drawStr(0, 8, statusWifiMsg.c_str());
  }

  // Linha 2
  char buf[32];
  snprintf(buf, sizeof(buf), "Cam:%.1f Alvo:%.0f", t1, setpointAtual);
  u8g2.drawStr(0, 18, buf);

  // Linha 3
  snprintf(buf, sizeof(buf), "PTC:%.1f U:%.0f%%", t2, umid);
  u8g2.drawStr(0, 28, buf);

  // Linha 4
  String linha = perfis[perfilAtual].nome + " " + faseAtual;
  u8g2.drawStr(0, 38, linha.c_str());

  u8g2.sendBuffer();
}

void setup() {
  // Saídas seguras primeiro
  pinMode(PIN_HEATER, OUTPUT);
  pinMode(PIN_FAN, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_HEATER, LOW);
  digitalWrite(PIN_FAN, LOW);
  digitalWrite(PIN_LED, LOW);    // LED aceso no boot: prova que o firmware está rodando

  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Filament Care C3 ===");

  // OLED
  Wire.begin(5, 6);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);
  oledOk = true;

  // Motivo do último reinício (ajuda a achar loop de boot)
  esp_reset_reason_t rr = esp_reset_reason();
  const char* rn = "OUTRO";
  switch (rr) {
    case ESP_RST_POWERON:  rn = "POWERON"; break;
    case ESP_RST_SW:       rn = "SOFTWARE"; break;
    case ESP_RST_PANIC:    rn = "PANIC/CRASH"; break;
    case ESP_RST_INT_WDT:  rn = "INT_WDT"; break;
    case ESP_RST_TASK_WDT: rn = "TASK_WDT"; break;
    case ESP_RST_WDT:      rn = "WDT"; break;
    case ESP_RST_BROWNOUT: rn = "BROWNOUT"; break;
    case ESP_RST_USB:      rn = "USB"; break;
    default: break;
  }
  Serial.printf("RESET: %s (%d)\n", rn, (int)rr);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2, 10, "Filament Care");
  u8g2.drawStr(2, 21, "C3 v1.0.5");
  u8g2.drawStr(2, 34, rn);
  u8g2.sendBuffer();
  delay(2500);

  bmeOk = bme.begin(0x76) || bme.begin(0x77);
  Serial.println(bmeOk ? "BME280 OK" : "BME280 NAO ENCONTRADO");
  ds18b20.begin();
  ds18b20.setResolution(10);
  dsCount = ds18b20.getDeviceCount();
  Serial.printf("DS18B20 encontrados: %d\n", dsCount);
  ultimoT1Ok = ultimoT2Ok = millis();

  WiFi.onEvent([](WiFiEvent_t ev, WiFiEventInfo_t info) {
    if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) ultimoMotivo = info.wifi_sta_disconnected.reason;
  });
  conectarWifiSalvo();
  Serial.println("STEP 1: wifi ok");
  atualizarOLED();
  Serial.println("STEP 2: oled ok");

  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  
  server.on("/status", []() {
    PerfilSecagem p = perfis[perfilAtual];
    unsigned long dec = (perfilAtual == 0) ? 0 : (millis() - inicioCicloMs) / 60000UL;
    int tot = p.tempoRampaMin + p.tempoPatamarMin + p.tempoCoolMin;
    String json = "{";
    json += "\"t1\":" + String(t1, 1) + ",";
    json += "\"t2\":" + String(t2, 1) + ",";
    json += "\"hum\":" + String(umid, 1) + ",";
    json += "\"perfil\":\"" + p.nome + "\",";
    json += "\"fase\":\"" + faseAtual + "\",";
    json += "\"sp\":" + String(setpointAtual, 1) + ",";
    json += "\"heater\":" + String(pwmHeater) + ",";
    json += "\"fan\":" + String(pwmFan) + ",";
    json += "\"minDecorridos\":" + String(dec) + ",";
    json += "\"minTotal\":" + String(tot);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/set", []() {
    if (server.hasArg("p")) {
      int p = server.arg("p").toInt();
      if (p >= 0 && p <= 4) {
        perfilAtual = p;
        inicioCicloMs = millis();
        tempInicialCiclo = (t1 > 10.0) ? t1 : 25.0;
      }
    }
    server.send(200, "text/plain", "OK");
  });

  Serial.println("STEP 3: rotas ok");
  server.begin();
  Serial.println("STEP 4: servidor ok");
}

unsigned long ultimoCiclo = 0;
void loop() {
  server.handleClient();
  verificarComandosSerial();
  atualizarLed();
  manutencaoWifi();

  if (millis() - ultimoCiclo >= 1500) {
    ultimoCiclo = millis();
    static bool primeiro = true;
    if (primeiro) Serial.println("LOOP 1: sensores");
    lerSensores();
    if (primeiro) Serial.println("LOOP 2: controle");
    processarRampaEControle();
    if (primeiro) Serial.println("LOOP 3: oled");
    atualizarOLED();
    if (primeiro) { Serial.println("LOOP 4: ok"); primeiro = false; }
  }
}
