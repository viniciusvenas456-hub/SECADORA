#include <esp_system.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_BME280.h>
#include <OneWireNg_CurrentPlatform.h>
#include <OneWireNg_BitBangGpio.h>

#define ONE_WIRE_BUS 3
#define PIN_HEATER   2
#define PIN_FAN      7
#define PIN_LED      8          // LED azul da placa (ativo em LOW)

// OLED 0.42" 72x40
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

Adafruit_BME280 bme;
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
:root{--bg:#090e17;--p:#111a2b;--p2:#18243a;--t:#f1f5f9;--m:#94a3b8;--a:#10b981;--c:#38bdf8;--w:#f59e0b;--d:#ef4444;--line:#22314a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--t);font-family:system-ui,-apple-system,sans-serif;padding:16px}
.wrap{max-width:960px;margin:auto}
.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;padding:0 4px}
.brand{display:flex;align-items:center;gap:10px}.brand h2{margin:0;font-size:20px;letter-spacing:-.3px}
.badge{display:inline-flex;align-items:center;gap:6px;background:var(--p);border:1px solid var(--line);padding:6px 12px;border-radius:99px;font-size:12px;color:var(--m)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--d);box-shadow:0 0 8px var(--d);transition:.3s}
.dot.on{background:var(--a);box-shadow:0 0 8px var(--a)}
.card{background:var(--p);border:1px solid var(--line);border-radius:16px;padding:18px;margin-bottom:14px;box-shadow:0 4px 20px rgba(0,0,0,.25)}
.status-hdr{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;flex-wrap:wrap}
.lbl{color:var(--m);font-size:11px;font-weight:700;letter-spacing:.6px;text-transform:uppercase}
.fase-txt{margin:4px 0 0;font-size:22px;font-weight:800}
.sp-txt{margin:4px 0 0;font-size:22px;font-weight:800;color:var(--w)}
.bar{height:10px;background:#070b12;border-radius:99px;overflow:hidden;margin:14px 0 8px;border:1px solid #1a2639}
.fill{height:100%;background:linear-gradient(90deg,var(--a),var(--c));width:0%;transition:width .5s ease}
.time-row{display:flex;justify-content:space-between;font-size:12px;color:var(--m)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-bottom:14px}
.kpi{background:var(--p);border:1px solid var(--line);border-radius:16px;padding:16px;position:relative;overflow:hidden}
.kpi::after{content:"";position:absolute;left:0;top:0;bottom:0;width:4px;background:var(--kc,var(--a))}
.val{font-size:30px;font-weight:800;margin:6px 0 2px;letter-spacing:-.5px}
.sub{font-size:12px;color:var(--m)}
.chart-hdr{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;margin-bottom:10px}
.legend{display:flex;gap:14px;font-size:12px;color:var(--m)}
.leg-item{display:flex;align-items:center;gap:5px}
.leg-sw{width:12px;height:3px;border-radius:2px}
canvas{width:100%;height:210px;display:block;background:#080d16;border-radius:10px;border:1px solid #1b273b}
.btns{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:10px;margin-top:12px}
button{padding:14px 12px;border-radius:12px;border:1px solid var(--line);background:var(--p2);color:var(--t);font-weight:700;cursor:pointer;font-size:15px;transition:.2s;text-align:center}
button:hover{border-color:var(--a);transform:translateY(-1px)}
button.active{border-color:var(--a);background:rgba(16,185,129,.14);box-shadow:0 0 0 1px var(--a)}
button .bsub{display:block;font-size:12px;font-weight:500;color:var(--m);margin-top:3px}
button.stop{background:#2d1216;border-color:#7f1d1d;color:#fca5a5}
button.stop:hover{border-color:var(--d);background:#3f161c}
.alert{display:none;background:#3b1216;border:1px solid var(--d);color:#fecaca;padding:12px 16px;border-radius:12px;margin-bottom:14px;font-size:13px;font-weight:600}
</style></head><body><div class="wrap">
<div class="top">
  <div class="brand"><h2>Filament Care C3</h2></div>
  <div class="badge"><span class="dot" id="dot"></span><span id="conn">Conectando...</span></div>
</div>

<div class="alert" id="alerta"></div>

<div class="card">
  <div class="status-hdr">
    <div><div class="lbl">Status do Ciclo</div><div class="fase-txt" id="fase">Carregando...</div></div>
    <div style="text-align:right"><div class="lbl">Alvo da Rampa</div><div class="sp-txt" id="sp">-- °C</div></div>
  </div>
  <div class="bar"><div class="fill" id="prog"></div></div>
  <div class="time-row">
    <span id="tempo">Decorrido: 0 min</span>
    <span id="restante">Restante: -- min</span>
  </div>
</div>

<div class="grid">
  <div class="kpi" style="--kc:#10b981"><div class="lbl">Câmara (T1)</div><div class="val" style="color:#10b981" id="t1">-- °C</div><div class="sub" id="amb">Ambiente: -- °C</div></div>
  <div class="kpi" style="--kc:#f97316"><div class="lbl">Aquecedor PTC (T2)</div><div class="val" style="color:#f97316" id="t2">-- °C</div><div class="sub">Limite seg.: 105 °C</div></div>
  <div class="kpi" style="--kc:#38bdf8"><div class="lbl">Umidade Relativa</div><div class="val" style="color:#38bdf8" id="hum">-- %</div><div class="sub">Sensor BME280</div></div>
  <div class="kpi" style="--kc:#a855f7"><div class="lbl">Potência / Ventoinha</div><div class="val" style="color:#c084fc" id="pwr">--% / --%</div><div class="sub">PWM Heater / Fan</div></div>
</div>

<div class="card">
  <div class="chart-hdr">
    <div class="lbl">Curva Térmica em Tempo Real</div>
    <div class="legend">
      <div class="leg-item"><span class="leg-sw" style="background:#10b981"></span>Câmara</div>
      <div class="leg-item"><span class="leg-sw" style="background:#f97316"></span>Aquecedor</div>
      <div class="leg-item"><span class="leg-sw" style="background:#f59e0b"></span>Alvo</div>
    </div>
  </div>
  <canvas id="grafico"></canvas>
</div>

<div class="card">
  <div class="lbl">Perfis de Secagem</div>
  <div class="btns">
    <button id="btn1" onclick="setP(1)">PLA<span class="bsub">50 °C · 4h30</span></button>
    <button id="btn2" onclick="setP(2)">PETG<span class="bsub">65 °C · 6h40</span></button>
    <button id="btn3" onclick="setP(3)">ASA<span class="bsub">80 °C · 6h50</span></button>
    <button id="btn4" onclick="setP(4)">ABS<span class="bsub">80 °C · 6h50</span></button>
    <button class="stop" onclick="setP(0)">PARAR<span class="bsub">Resfriar / Desligar</span></button>
  </div>
</div>
</div>

<script>
const MAX_PTS = 120;
const hist = { t1: [], t2: [], sp: [] };
const cvs = document.getElementById('grafico');
const ctx = cvs.getContext('2d');

function desenharGrafico() {
  const dpr = window.devicePixelRatio || 1;
  const rect = cvs.getBoundingClientRect();
  cvs.width = rect.width * dpr;
  cvs.height = rect.height * dpr;
  ctx.scale(dpr, dpr);

  const w = rect.width, h = rect.height;
  const padL = 38, padR = 12, padT = 14, padB = 20;
  const gw = w - padL - padR, gh = h - padT - padB;

  ctx.clearRect(0, 0, w, h);

  const todos = [...hist.t1, ...hist.t2, ...hist.sp].filter(v => v > 0);
  let minV = todos.length ? Math.floor(Math.min(...todos, 20) / 10) * 10 : 10;
  let maxV = todos.length ? Math.ceil(Math.max(...todos, 60) / 10) * 10 : 100;
  if (maxV - minV < 20) maxV = minV + 20;

  ctx.strokeStyle = '#182438';
  ctx.lineWidth = 1;
  ctx.fillStyle = '#64748b';
  ctx.font = '11px system-ui';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';

  const steps = 4;
  for (let i = 0; i <= steps; i++) {
    const val = minV + ((maxV - minV) / steps) * i;
    const y = padT + gh - (i / steps) * gh;
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(w - padR, y);
    ctx.stroke();
    ctx.fillText(Math.round(val) + '°', padL - 6, y);
  }

  function plotar(arr, cor, tracejado) {
    if (arr.length < 2) return;
    ctx.save();
    ctx.strokeStyle = cor;
    ctx.lineWidth = tracejado ? 1.8 : 2.4;
    if (tracejado) ctx.setLineDash([5, 4]);
    ctx.beginPath();
    arr.forEach((v, idx) => {
      const x = padL + (idx / (MAX_PTS - 1)) * gw;
      const y = padT + gh - ((v - minV) / (maxV - minV)) * gh;
      if (idx === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.restore();
  }

  plotar(hist.sp, '#f59e0b', true);
  plotar(hist.t2, '#f97316', false);
  plotar(hist.t1, '#10b981', false);
}

function setP(id) {
  fetch('/set?p=' + id).then(() => atualizar());
}

function atualizar() {
  fetch('/status').then(r => r.json()).then(d => {
    document.getElementById('dot').classList.add('on');
    document.getElementById('conn').innerText = 'Online';

    const al = document.getElementById('alerta');
    if (d.fase === 'FALHA SENSOR') {
      al.style.display = 'block';
      al.innerText = 'ALERTA DE SEGURANÇA: Falha na leitura dos sensores DS18B20. Aquecedor desligado automaticamente.';
    } else {
      al.style.display = 'none';
    }

    document.getElementById('t1').innerText = d.t1.toFixed(1) + ' °C';
    document.getElementById('t2').innerText = d.t2.toFixed(1) + ' °C';
    document.getElementById('amb').innerText = 'Ambiente: ' + d.tAmb.toFixed(1) + ' °C';
    document.getElementById('hum').innerText = d.hum.toFixed(1) + ' %';
    document.getElementById('fase').innerText = d.perfil + ' — ' + d.fase;
    document.getElementById('sp').innerText = d.sp.toFixed(1) + ' °C';
    document.getElementById('pwr').innerText = Math.round(d.heater / 2.55) + '% / ' + Math.round(d.fan / 2.55) + '%';
    document.getElementById('tempo').innerText = 'Decorrido: ' + d.minDecorridos + ' min / Total: ' + d.minTotal + ' min';

    const rest = Math.max(0, d.minTotal - d.minDecorridos);
    document.getElementById('restante').innerText = d.minTotal > 0 ? ('Restante: ' + rest + ' min') : 'Aguardando início';

    const pct = d.minTotal > 0 ? Math.min(100, (d.minDecorridos / d.minTotal) * 100) : 0;
    document.getElementById('prog').style.width = pct + '%';

    for (let i = 1; i <= 4; i++) {
      const b = document.getElementById('btn' + i);
      if (b) b.classList.toggle('active', d.perfilId === i);
    }

    hist.t1.push(d.t1);
    hist.t2.push(d.t2);
    hist.sp.push(d.sp);
    if (hist.t1.length > MAX_PTS) { hist.t1.shift(); hist.t2.shift(); hist.sp.shift(); }
    desenharGrafico();
  }).catch(() => {
    document.getElementById('dot').classList.remove('on');
    document.getElementById('conn').innerText = 'Sem resposta';
  });
}

window.addEventListener('resize', desenharGrafico);
setInterval(atualizar, 2000);
atualizar();
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

// ===== 1-Wire via OneWireNg =====
OneWireNg_BitBangGpio owBus(ONE_WIRE_BUS, true);
OneWireNg::Id owRom[2];
bool owConvStarted = false;
unsigned long owConvStart = 0;

void owInitPin() {
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);
}

bool owReset() {
  return (owBus.reset() == OneWireNg::EC_SUCCESS);
}

uint8_t owCrc8(const uint8_t* d, uint8_t len) {
  return OneWireNg::crc8(d, len);
}

int owScan() {
  int n = 0;
  OneWireNg::Id id;
  owBus.searchReset();
  while (n < 2 && owBus.search(id) == OneWireNg::EC_MORE) {
    if (id[0] == 0x28 && OneWireNg::crc8(id, 7) == id[7]) {
      memcpy(owRom[n], id, sizeof(OneWireNg::Id));
      n++;
    }
  }
  if (n < 2 && id[0] == 0x28 && OneWireNg::crc8(id, 7) == id[7]) {
    memcpy(owRom[n], id, sizeof(OneWireNg::Id));
    n++;
  }
  return n;
}

void owStartConversion() {
  if (owBus.reset() != OneWireNg::EC_SUCCESS) return;
  owBus.writeByte(0xCC); // SKIP ROM
  owBus.writeByte(0x44); // CONVERT T
}

bool owReadTemp(const OneWireNg::Id& rom, float& t) {
  if (owBus.reset() != OneWireNg::EC_SUCCESS) return false;
  owBus.writeByte(0x55); // MATCH ROM
  owBus.writeBytes(rom, sizeof(OneWireNg::Id));
  owBus.writeByte(0xBE); // READ SCRATCHPAD

  uint8_t sp[9];
  for (int i = 0; i < 9; i++) {
    sp[i] = owBus.readByte();
  }
  if (OneWireNg::crc8(sp, 8) != sp[8]) return false;

  int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
  t = raw / 16.0f;
  return true;
}

void lerSensores() {
  unsigned long agora = millis();
  if (bmeOk) {
    float h = bme.readHumidity();
    float t = bme.readTemperature();
    if (!isnan(h)) umid = h;
    if (!isnan(t)) tAmb = t;
  }
  if (dsCount == 0) {
    // sem sensor: tenta achar de novo a cada 5 s
    if (agora - ultimaBuscaDs > 5000UL) { ultimaBuscaDs = agora; owInitPin(); dsCount = owScan(); }
    falhaSensor = true;
    return;
  }
  if (!owConvStarted) { owStartConversion(); owConvStarted = true; owConvStart = agora; return; }
  if (agora - owConvStart < 800UL) return;       // 12 bits = ~750 ms de conversão
  owConvStarted = false;
  float v;
  if (owReadTemp(owRom[0], v) && v > -50 && v < 125 && v != 85.0f) { t1 = v; ultimoT1Ok = agora; }
  if (dsCount > 1 && owReadTemp(owRom[1], v) && v > -50 && v < 125 && v != 85.0f) { t2 = v; ultimoT2Ok = agora; }
  // sem leitura válida dos dois por 10 s = falha (o aquecedor é cortado)
  falhaSensor = (agora - ultimoT1Ok > 10000UL) || (agora - ultimoT2Ok > 10000UL);
  if (agora - ultimoT1Ok > 15000UL) dsCount = 0;   // perdeu os sensores: volta a procurar
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

// Reconecta sozinho se a rede cair e repete o IP pela USB (o navegador pode abrir a porta 
