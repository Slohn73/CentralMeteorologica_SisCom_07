#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>

// ========== 1. CONFIGURACION DEL PUNTO DE ACCESO (AP) ==========
const char* apSsid = "Central_Meteo_ESP32";
const char* apPassword = "meteo1314"; // Mínimo 8 caracteres
const unsigned long TIMEOUT_MS = 10000; // 10 segundos sin recibir datos

WebServer server(80);

// ========== 2. ESTRUCTURA Y ESTADO DE ESTACIONES ==========
typedef struct struct_mensaje {
  uint8_t idNodo;
  float temperatura;
  float humedad;
} struct_mensaje;

struct DatosEstacion {
  bool activa = false;
  float temperatura = 0.0;
  float humedad = 0.0;
  int rssi = -100;
  unsigned long ultimaVezMs = 0;
};

DatosEstacion est1;
DatosEstacion est2;
volatile bool hayNuevoDato = false;

// ========== 3. PAGINA WEB (HTML + JS) ==========
const char paginaWeb[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Central Meteorologica ESP-NOW</title>
  <style>
      * { box-sizing: border-box; }
      body {
        font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif;
        font-size: 20px;
        text-align: center;
        margin: 0;
        padding: 24px;
        min-height: 100vh;
        background: linear-gradient(135deg, #eef2f7 0%, #dce4f0 100%);
        color: #1e293b;
      }
      h1 {
        color: #0f172a;
        font-weight: 800;
        letter-spacing: -0.02em;
        margin: 1.2em 0 1.4em;
      }
      .grid {
        display: flex;
        flex-wrap: wrap;
        justify-content: center;
        gap: 1.8em;
        max-width: 1020px;
        margin: 0 auto;
      }
      .card {
        background: #ffffff;
        flex: 1 1 380px;
        padding: 1.6em;
        border-radius: 20px;
        border-top: 5px solid #0066cc;
        box-shadow: 0 12px 30px rgba(15, 23, 42, 0.08);
        transition: transform 0.25s ease, box-shadow 0.25s ease;
      }
      .card:hover {
        transform: translateY(-4px);
        box-shadow: 0 18px 36px rgba(15, 23, 42, 0.12);
      }
      .card-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin-bottom: 1.4em;
        padding-bottom: 0.8em;
        border-bottom: 1px solid #f1f5f9;
      }
      h2 {
        margin: 0;
        color: #0f172a;
        font-size: 1.35em;
        font-weight: 700;
      }
      .senal svg { width: 1.6em; height: 1.3em; }
      .senal svg rect { fill: #e2e8f0; transition: fill 0.3s ease; }
      .dato {
        font-size: 1.1em;
        color: #64748b;
        margin: 1em 0;
        padding-bottom: 0.8em;
        border-bottom: 1px solid #f1f5f9;
        text-align: left;
      }
      .dato span {
        float: right;
        font-size: 1.15em;
        font-weight: 700;
        color: #0f172a;
        font-variant-numeric: tabular-nums;
      }
      .barra-fondo {
        clear: both;
        width: 100%;
        height: 0.7em;
        background: #e2e8f0;
        border-radius: 999px;
        overflow: hidden;
        box-shadow: inset 0 1px 2px rgba(0,0,0,0.08);
      }
      .barra-nivel {
        height: 100%;
        width: 0%;
        border-radius: 999px;
        transition: width 0.5s cubic-bezier(0.4, 0, 0.2, 1), background-color 0.4s ease;
      }
      .estado {
        display: inline-flex;
        align-items: center;
        gap: 8px;
        font-size: 0.8em;
        font-weight: 600;
        padding: 6px 14px;
        border-radius: 999px;
        margin-top: 1em;
        background: #dcfce7;
        color: #166534;
      }
      .estado::before {
        content: "";
        width: 8px;
        height: 8px;
        border-radius: 50%;
        background: #22c55e;
      }
      .sin-datos {
        background: #fee2e2;
        color: #991b1b;
      }
      .sin-datos::before {
        background: #ef4444;
      }
      .separado { margin-bottom: 0.6em; margin-top: 0.8em; }
  </style>
  <script>
    function actualizarBarraTemp(id, activa, temp) {
      const barra = document.getElementById(id);
      if (!activa) {
        barra.style.width = '0%';
        return;
      }
      // Rango de 0 °C a 50 °C
      const porcentaje = Math.max(0, Math.min(100, (temp / 50) * 100));
      barra.style.width = porcentaje + '%';

      if (temp < 18) barra.style.backgroundColor = '#3498db';      // Frío
      else if (temp < 26) barra.style.backgroundColor = '#2ecc71'; // Templado
      else if (temp < 32) barra.style.backgroundColor = '#f39c12'; // Cálido
      else barra.style.backgroundColor = '#e74c3c';                // Calor
    }

    function actualizarBarraHum(id, activa, hum) {
      const barra = document.getElementById(id);
      if (!activa) {
        barra.style.width = '0%';
        return;
      }
      // La humedad ya viene en porcentaje (0 % a 100 %)
      const porcentaje = Math.max(0, Math.min(100, hum));
      barra.style.width = porcentaje + '%';

      if (hum < 30) barra.style.backgroundColor = '#f1c40f';       // Seco
      else if (hum < 65) barra.style.backgroundColor = '#00bcd4';  // Confortable
      else barra.style.backgroundColor = '#0066cc';                // Húmedo
    }

    function actualizarSenal(svgId, activa, rssi) {
      const barras = document.querySelectorAll('#' + svgId + ' rect');

      if (!activa || rssi === undefined) {
        barras.forEach(b => b.style.fill = '#ddd');
        return;
      }

      let nivel = 1;
      let color = '#e74c3c'; // Rojo (débil)

      if (rssi >= -55) { nivel = 4; color = '#2ecc71'; }      // Verde (excelente)
      else if (rssi >= -70) { nivel = 3; color = '#2ecc71'; } // Verde (buena)
      else if (rssi >= -80) { nivel = 2; color = '#f39c12'; } // Naranja (regular)

      barras.forEach((b, i) => {
        b.style.fill = i < nivel ? color : '#ddd';
      });
    }

    function actualizarDatos() {
      fetch('/data')
        .then(res => res.json())
        .then(d => {
          document.getElementById('t1').textContent = d.est1.activa ? d.est1.temperatura.toFixed(1) + ' °C' : '--';
          document.getElementById('h1').textContent = d.est1.activa ? d.est1.humedad.toFixed(1) + ' %' : '--';
          actualizarBarraTemp('bt1', d.est1.activa, d.est1.temperatura);
          actualizarBarraHum('bh1', d.est1.activa, d.est1.humedad);
          actualizarSenal('sig1', d.est1.activa, d.est1.rssi);

          document.getElementById('t2').textContent = d.est2.activa ? d.est2.temperatura.toFixed(1) + ' °C' : '--';
          document.getElementById('h2').textContent = d.est2.activa ? d.est2.humedad.toFixed(1) + ' %' : '--';
          actualizarBarraTemp('bt2', d.est2.activa, d.est2.temperatura);
          actualizarBarraHum('bh2', d.est2.activa, d.est2.humedad);
          actualizarSenal('sig2', d.est2.activa, d.est2.rssi);

          const s1 = document.getElementById('s1');
          s1.textContent = d.est1.activa ? 'Recibiendo datos' : 'Sin datos aún';
          s1.classList.toggle('sin-datos', !d.est1.activa);

          const s2 = document.getElementById('s2');
          s2.textContent = d.est2.activa ? 'Recibiendo datos' : 'Sin datos aún';
          s2.classList.toggle('sin-datos', !d.est2.activa);
        })
        .catch(err => console.error('Error al consultar /data:', err));
    }
    setInterval(actualizarDatos, 2000);
    window.onload = actualizarDatos;
  </script>
</head>
<body>
  <h1>Central Meteorológica (ESP-NOW)</h1>
  <div class="grid">
    <div class="card">
      <div class="card-header">
        <h2>Estación 1</h2>
        <div class="senal">
          <svg id="sig1" width="22" height="18" viewBox="0 0 22 18">
            <rect x="0"  y="12" width="4" height="6"  rx="1"/>
            <rect x="6"  y="8"  width="4" height="10" rx="1"/>
            <rect x="12" y="4"  width="4" height="14" rx="1"/>
            <rect x="18" y="0"  width="4" height="18" rx="1"/>
          </svg>
        </div>
      </div>
      <div class="dato">
        Temperatura <span id="t1">--</span>
        <div class="barra-fondo separado"><div class="barra-nivel" id="bt1"></div></div>
      </div>
      <div class="dato">
        Humedad <span id="h1">--</span>
        <div class="barra-fondo separado"><div class="barra-nivel" id="bh1"></div></div>
      </div>
      <div class="estado" id="s1">Esperando...</div>
    </div>
    <div class="card">
      <div class="card-header">
        <h2>Estación 2</h2>
        <div class="senal">
          <svg id="sig2" width="22" height="18" viewBox="0 0 22 18">
            <rect x="0"  y="12" width="4" height="6"  rx="1"/>
            <rect x="6"  y="8"  width="4" height="10" rx="1"/>
            <rect x="12" y="4"  width="4" height="14" rx="1"/>
            <rect x="18" y="0"  width="4" height="18" rx="1"/>
          </svg>
        </div>
      </div>
      <div class="dato">
        Temperatura <span id="t2">--</span>
        <div class="barra-fondo separado"><div class="barra-nivel" id="bt2"></div></div>
      </div>
      <div class="dato">
        Humedad <span id="h2">--</span>
        <div class="barra-fondo separado"><div class="barra-nivel" id="bh2"></div></div>
      </div>
      <div class="estado" id="s2">Esperando...</div>
    </div>
  </div>
</body>
</html>
)rawliteral";

// ========== 4. CALLBACK ESP-NOW ==========
void alRecibir(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len != sizeof(struct_mensaje)) return;

  struct_mensaje msg;
  memcpy(&msg, incomingData, sizeof(msg));

  if (msg.idNodo == 1) {
    est1.activa = true;
    est1.temperatura = msg.temperatura;
    est1.humedad = msg.humedad;
    est1.rssi = info->rx_ctrl->rssi;
    est1.ultimaVezMs = millis();
  } else if (msg.idNodo == 2) {
    est2.activa = true;
    est2.temperatura = msg.temperatura;
    est2.humedad = msg.humedad;
    est2.rssi = info->rx_ctrl->rssi;
    est2.ultimaVezMs = millis();
  }
  hayNuevoDato = true;
}

// ========== 5. RUTAS HTTP ==========
void handleRoot() {
  server.send(200, "text/html", paginaWeb);
}

void handleData() {
  String json = "{";
  json += "\"est1\":{\"activa\":" + String(est1.activa ? "true" : "false") +
          ",\"temperatura\":" + String(est1.temperatura, 1) +
          ",\"humedad\":" + String(est1.humedad, 1) +
          ",\"rssi\":" + String(est1.rssi) + "},";
  json += "\"est2\":{\"activa\":" + String(est2.activa ? "true" : "false") +
          ",\"temperatura\":" + String(est2.temperatura, 1) +
          ",\"humedad\":" + String(est2.humedad, 1) +
          ",\"rssi\":" + String(est2.rssi) + "}";
  json += "}";
  server.send(200, "application/json", json);
}

// ========== 6. SETUP Y LOOP ==========
void setup() {
  Serial.begin(115200);
  delay(500);

  // Modo dual para recibir ESP-NOW y crear su propia red Wi-Fi (AP)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPassword);

  Serial.print("Red Wi-Fi creada: ");
  Serial.println(apSsid);
  Serial.print("Abre en tu navegador: http://");
  Serial.println(WiFi.softAPIP()); // Normalmente http://192.168.4.1

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al inicializar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(alRecibir);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Servidor web iniciado en puerto 80.");
}

void loop() {
  server.handleClient();

  unsigned long ahora = millis();
  if (est1.activa && (ahora - est1.ultimaVezMs > TIMEOUT_MS)) {
    est1.activa = false;
  }
  if (est2.activa && (ahora - est2.ultimaVezMs > TIMEOUT_MS)) {
    est2.activa = false;
  }

  if (hayNuevoDato) {
    hayNuevoDato = false;
    Serial.printf("E1 [%.1f C, %.1f %%] | E2 [%.1f C, %.1f %%]\n",
                  est1.temperatura, est1.humedad,
                  est2.temperatura, est2.humedad);
  }
}
