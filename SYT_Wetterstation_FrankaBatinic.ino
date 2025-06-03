#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <HTTPClient.h>

// ==== Pin-Definitionen ====
#define DHTPIN 2
#define DHTTYPE DHT11
#define LED_PIN 8
#define NUMPIXELS 1
#define sensorPin 19

// ==== WLAN-Zugangsdaten ====
const char* ssid = "IOT";
const char* password = "20tgmiot18";


// ==== Initialisierungen ====
Adafruit_NeoPixel pixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// ==== Zustände ====
bool ledState = true;
bool shakeDetected = false;
unsigned long lastShake = 0;
unsigned long shakeCooldown = 5000;
String lastPresenceLog = "Noch keine Anwesenheit erkannt.";

String lastMeasurement = "";
unsigned long lastRead = 0;
const unsigned long interval = 20000;

const int maxDataPoints = 12;
String timestamps[maxDataPoints];
float avgTemperatures[maxDataPoints];
float avgHumidities[maxDataPoints];
int dataIndex = 0;

float tempSum = 0;
float humSum = 0;
int readingsCount = 0;
unsigned long lastAverageTime = 0;

const int lastMeasuresSize = 5;
float lastTemps[lastMeasuresSize] = {0};
float lastHums[lastMeasuresSize] = {0};
int filterIndex = 0;
bool filterFilled = false;

uint8_t ledColor[3] = {0, 0, 0}; // RGB Farbe der LED, initial schwarz (aus)
bool manualColorMode = false;


// ==== Hilfsfunktionen ====
void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  ledColor[0] = r;
  ledColor[1] = g;
  ledColor[2] = b;
  setLED(r, g, b);
}

void initTime() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) delay(500);
}

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

bool isOutlier(float newValue, float* history, int size, float tolerance = 8.0) {
  int count = filterFilled ? size : filterIndex;
  if (count == 0) return false;
  float sum = 0;
  for (int i = 0; i < count; i++) sum += history[i];
  float avg = sum / count;
  return abs(newValue - avg) > tolerance;
}

// ... (Euer vorhandener Code bis zum Ende von setup())

void setup() {
  Serial.begin(115200);
  pixel.begin(); // <-- Hierher verschoben, sollte nur einmal aufgerufen werden
  setLED(255, 0, 255); // Initialfarbe beim Start

  dht.begin();
  pinMode(sensorPin, INPUT);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  setLED(0, 0, 0); // Nach erfolgreicher WLAN-Verbindung
  Serial.println("WLAN verbunden!");
  Serial.println(WiFi.localIP());

  initTime();

  // ==== Webserver-Routen ====
  server.on("/", []() {
    server.send(200, "text/html", R"rawliteral(
      <!DOCTYPE html>
      <html lang="de">
      <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Wetterstation Übersicht</title>
        <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
        <style>
          body { font-family: Arial; background: #eef2f3; margin: 0; padding: 0; }
          header { background-color: #3a7bd5; color: white; padding: 20px; text-align: center; }
          main { padding: 20px; }
          .data-box { background-color: white; border-radius: 10px; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.2); margin-bottom: 20px; text-align: center; }
          .data-box h2 { margin: 0; font-size: 2em; }
          canvas { max-width: 100%; margin-top: 20px; }
        </style>
      </head>
      <body>
        <header>
          <h1>🌦 Wetterstation Übersicht</h1>
          <h3>Angelina Glatzl, Franka Batinic und Elena Vuckovic</h3>
          <p>Live-Daten von Temperatur und Luftfeuchtigkeit</p>
        </header>
        <main>
          <div class="data-box"><h2 id="currentData">Lade aktuelle Messung...</h2></div>
          <div class="data-box"><h3>📈 Temperaturverlauf (ø alle 5 Min.)</h3><canvas id="tempChart"></canvas></div>
          <div class="data-box"><h3>💧 Luftfeuchtigkeitsverlauf (ø alle 5 Min.)</h3><canvas id="humChart"></canvas></div>
          <div class="data-box"><h3>🎨 LED Farbe auswählen</h3><input type="color" id="ledColor" value="#000000"></div>
          <div class="data-box">
            <h3>💡 LED Steuerung</h3>
            <button id="toggleLedButton">LED An/Aus</button>
            <p id="ledStatusText">Status: unbekannt</p>
          </div>
        </main>
        <div class="data-box"><h3>🕵 Anwesenheits-Logbuch</h3><p id="logData">Lade Anwesenheitsdaten...</p></div>
        <script>
          async function fetchCurrentData() {
            const res = await fetch("/data");
            const text = await res.text();
            document.getElementById("currentData").textContent = text;
          }
          async function fetchChartData(url) {
            const res = await fetch(url);
            const text = await res.text();
            const lines = text.trim().split("\n");
            const labels = [], data = [];
            lines.forEach(line => {
              const [time, value] = line.split(",");
              labels.push(time.slice(11, 16));
              data.push(parseFloat(value));
            });
            return { labels, data };
          }
          async function drawCharts() {
            const tempData = await fetchChartData("/chartdata");
            const humData = await fetchChartData("/humiditychartdata");
            new Chart(document.getElementById('tempChart').getContext('2d'), {
              type: 'line',
              data: {
                labels: tempData.labels,
                datasets: [{
                  label: 'Temperatur (°C)',
                  data: tempData.data,
                  borderColor: 'orange',
                  backgroundColor: 'rgba(255,165,0,0.2)',
                  tension: 0.3,
                  fill: true
                }]
              }
            });
            new Chart(document.getElementById('humChart').getContext('2d'), {
              type: 'line',
              data: {
                labels: humData.labels,
                datasets: [{
                  label: 'Luftfeuchtigkeit (%)',
                  data: humData.data,
                  borderColor: 'blue',
                  backgroundColor: 'rgba(0,0,255,0.1)',
                  tension: 0.3,
                  fill: true
                }]
              }
            });
          }
          fetchCurrentData();
          drawCharts();
          async function fetchLogData() {
            const res = await fetch("/logbuch");
            const text = await res.text();
            document.getElementById("logData").textContent = text;
          }
          fetchLogData();
          function hexToRgb(hex) {
            const bigint = parseInt(hex.slice(1), 16);
            return { r: (bigint >> 16) & 255, g: (bigint >> 8) & 255, b: bigint & 255 };
          }
          document.getElementById("ledColor").addEventListener("input", function () {
            const { r, g, b } = hexToRgb(this.value);
            fetch(/setcolor?r=${r}&g=${g}&b=${b}, { method: "GET" });
          });
          function setInitialColor() {
            fetch("/getcolor")
              .then(res => res.json())
              .then(data => {
                const hex = "#" + data.r.toString(16).padStart(2, '0') +
                                     data.g.toString(16).padStart(2, '0') +
                                     data.b.toString(16).padStart(2, '0');
                document.getElementById("ledColor").value = hex;
              });
          }
          window.onload = () => {
            fetchCurrentData();
            drawCharts();
            setInitialColor();
          };
          document.getElementById("toggleLedButton").addEventListener("click", async function () {
            const res = await fetch("/toggleLED");
            const statusText = await res.text();
            document.getElementById("ledStatusText").textContent = "Status: " + statusText;
          });

        </script>
      </body>
      </html>
    )rawliteral");
  });

  server.on("/data", []() {
    server.send(200, "text/plain", lastMeasurement);
  });

  server.on("/toggleLED", []() {
    ledState = !ledState;
    if (ledState) {
      // NeoPixel an: z.B. grün
      // Wenn der manuelle Modus aktiv ist, soll die zuletzt gesetzte Farbe beibehalten werden, nicht auf grün zurückgesetzt werden.
      if (manualColorMode) {
        setLEDColor(ledColor[0], ledColor[1], ledColor[2]);
      } else {
        setLED(0, 255, 0); // Standardfarbe, wenn nicht im manuellen Modus
      }
    } else {
      // NeoPixel aus
      setLED(0, 0, 0);
    }
    server.send(200, "text/plain", ledState ? "LED eingeschaltet" : "LED ausgeschaltet");
  });

  server.on("/chartdata", []() {
    String data = "";
    for (int i = 0; i < maxDataPoints; i++) {
      int index = (dataIndex + i) % maxDataPoints;
      if (timestamps[index] != "") {
        data += timestamps[index] + "," + String(avgTemperatures[index], 2) + "\n";
      }
    }
    server.send(200, "text/plain", data);
  });

  server.on("/humiditychartdata", []() {
    String data = "";
    for (int i = 0; i < maxDataPoints; i++) {
      int index = (dataIndex + i) % maxDataPoints;
      if (timestamps[index] != "") {
        data += timestamps[index] + "," + String(avgHumidities[index], 2) + "\n";
      }
    }
    server.send(200, "text/plain", data);
  });

  server.on("/logbuch", []() {
    server.send(200, "text/plain", lastPresenceLog);
  });

  // Diese Routen MÜSSEN im setup() sein!
  server.on("/setcolor", []() {
    if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
      uint8_t r = server.arg("r").toInt();
      uint8_t g = server.arg("g").toInt();
      uint8_t b = server.arg("b").toInt();
      manualColorMode = true; // Manuellen Modus aktivieren
      ledState = true; // LED einschalten, wenn eine Farbe gesetzt wird
      setLEDColor(r, g, b);
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Fehlende Parameter");
    }
  });

  server.on("/getcolor", []() {
    String json = "{";
    json += "\"r\":" + String(ledColor[0]) + ",";
    json += "\"g\":" + String(ledColor[1]) + ",";
    json += "\"b\":" + String(ledColor[2]) + "}";
    server.send(200, "application/json", json);
  });

  // setLEDColor(ledColor[0], ledColor[1], ledColor[2]); // Diese Zeile ist hier nicht nötig, da setLEDColor die globale ledColor aktualisiert

  server.begin();
}

void loop() {
  server.handleClient();

  int sensorValue = digitalRead(sensorPin);
  if (sensorValue == HIGH && millis() - lastShake > shakeCooldown) {
    Serial.println("💥 Erschütterung erkannt!");
    shakeDetected = true;
    lastShake = millis();

    // Blinkt nur, wenn LED an ist und nicht im manuellen Farbmodus
    if (ledState && !manualColorMode) {
      setLED(255, 0, 255); // Magenta
      delay(500);
      // Nach dem Blinken die ursprüngliche Farbe wiederherstellen
      setLED(ledColor[0], ledColor[1], ledColor[2]);
    }
  }

  if (lastRead == 0 || millis() - lastRead >= interval) {
    lastRead = millis();

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (!isnan(humidity) && !isnan(temperature)) {
      if (isOutlier(temperature, lastTemps, lastMeasuresSize) || isOutlier(humidity, lastHums, lastMeasuresSize)) {
        Serial.println("⚠ Ausreißer ignoriert.");
        return;
      }

      lastTemps[filterIndex] = temperature;
      lastHums[filterIndex] = humidity;
      filterIndex = (filterIndex + 1) % lastMeasuresSize;
      if (filterIndex == 0) filterFilled = true;

      String time = getFormattedTime();
      lastMeasurement = "Zeit: " + time + " - Feuchtigkeit: " + String(humidity, 2) + "%" + ", Temperatur: " + String(temperature, 2) + "°C";
      Serial.println(lastMeasurement);

      tempSum += temperature;
      humSum += humidity;
      readingsCount++;

      // Die Temperatur-basierte LED-Steuerung nur ausführen, wenn nicht im manuellen Farbmodus
      if (ledState && !manualColorMode) {
        if (temperature > 30) setLED(255, 165, 0);     // orange
        else if (temperature >= 17.0) setLED(0, 255, 0);  // grün
        else setLED(0, 0, 255);                     // blau
      }

      // Diese Zeilen müssen HIER ENTFERNT WERDEN, da sie schon im setup() sind.
      // server.on("/setcolor", ...
      // server.on("/getcolor", ...
      // pixel.begin();
      // setLED(255, 0, 0);
      // setLEDColor(ledColor[0], ledColor[1], ledColor[2]);


      if (millis() - lastAverageTime >= 300000 || lastAverageTime == 0) {
        avgTemperatures[dataIndex] = tempSum / readingsCount;
        avgHumidities[dataIndex] = humSum / readingsCount;
        timestamps[dataIndex] = time;
        dataIndex = (dataIndex + 1) % maxDataPoints;
        tempSum = 0;
        humSum = 0;
        readingsCount = 0;
        lastAverageTime = millis();
      }
    } else {
      Serial.println("❌ Fehler beim Lesen vom DHT11");
    }
  }
}