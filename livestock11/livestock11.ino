#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <FS.h>
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <MPU6050.h>
// #include <ESPmDNS.h>
// #define USE_ARDUINO_INTERRUPTS false  // ESP32 doesn't support standard interrupts well
#include <PulseSensorPlayground.h>
// IPAddress local_IP(192,168,1,184);
// IPAddress gateway(192,168,1,1);
// IPAddress subnet(255,255,255,0);
// IPAddress primaryDNS(8,8,8,8);
// IPAddress secondaryDNS(8,8,4,4);


// —– Pins —–
#define PulseWire       34     // Must be one of 34–39 on ESP32

// —– Timing —–
const unsigned long pulseInterval = 20;   // sample every 20 ms → 50 Hz

// —– Pulse Sensor —–
const int Threshold = 550;                // tweak for your finger light-absorption
PulseSensorPlayground pulseSensor;

unsigned long lastPulseTime = 0;
MPU6050 mpu;

int buzz = 25;
int shock = 27;
// Pin where DS18B20 is connected
#define ONE_WIRE_BUS 13

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

AsyncWebServer server(80);

TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 for GPS

// Geofence variables
double setLat = 5.689078555338596;
double setLng = -0.2556675127218583;
double radius = 0;
String shapeType = "";
int NUM_VERTICES = 0;
double polygonLat[25];
double polygonLng[25];

// Step detection
int stepCount = 0;
bool stepDetected = false;
float stepThreshold = 1.1;
float steptrigger = 1.0;

// Sensor data
float latestTemperature = 0.0;
float latestAzG = 0.0;
double latestLat = 0.0;
double latestLng = 0.0;n
int bpmsensor=0;

// Timing
unsigned long lastTempReadTime = 0;
const unsigned long tempInterval = 1000;
unsigned long lastStepTime = 0;
const unsigned long stepInterval = 100;
unsigned long lastConfigTime = 0;
const unsigned long configInterval = 5000;

String username = "nanayaw12";
String apiURL = "";
String collarApi = "http://192.168.8.206:5000/current-apiurl/li-55446625";


String SSID="Galaxy A13";
String password="supa1235";
// Forward declarations
void connectToWiFi( String ssid, String password);
void initSPIFFS();
void loadConfig();
bool readJSONFromFile(const char* path, JsonDocument& doc);
void initMPU();
float readVerticalAcceleration();
bool detectStep(float azG);
bool checkDistanceFromSetPoint();
bool checkInsidePolygon();
bool isInsidePolygon(double lat, double lng);
bool checkAndUpdateConfig();
bool checkAndUpdateConfig2();
bool fetchAndWriteConfig();
bool fetchAndWriteConfig2();

void sendJSONResponse(AsyncWebServerRequest *request) {
  StaticJsonDocument<200> doc;
  doc["username"] = username;
  doc["latitude"] = gps.location.lat();
  doc["longitude"] = gps.location.lng();
  float temp = latestTemperature;
  doc["temperature"] = (temp != DEVICE_DISCONNECTED_C) ? temp : -999.0;
  char timestamp[30];
  sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          gps.date.year(), gps.date.month(), gps.date.day(),
          gps.time.hour(), gps.time.minute(), gps.time.second());
  doc["timestamp"] = timestamp;
  doc["steps"] = stepCount;
  if (shapeType == "polygon") {
  doc["crossedboundary"] = checkInsidePolygon();
} else {
  doc["crossedboundary"] = checkDistanceFromSetPoint();
}
  doc["bpm"]=bpmsensor;
  doc["rumination"] = detectRumination();
  
  

  String jsonString;
  serializeJson(doc, jsonString);
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", jsonString);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}


// Rumination detection variables
unsigned long lastRuminationCheck = 0;
const unsigned long ruminationInterval = 2000; // check every 2 seconds
int ruminationCount = 0;
bool isRuminating = false;
float ruminationThreshold = 0.05; // Small movement threshold (adjustable)

// Function to detect rumination
bool detectRumination() {
  loadRuminationConfig();
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  // Convert to G
  float axG = ax / 16384.0;
  float ayG = ay / 16384.0;
  float azG = az / 16384.0;

  // Calculate total movement
  float movement = sqrt(axG * axG + ayG * ayG + azG * azG) - 1.0; // remove gravity

  movement = abs(movement);

  Serial.print("Movement magnitude: ");
  Serial.println(movement);

  if (movement < ruminationThreshold) {
    ruminationCount++;
    if (ruminationCount >= 3) {  // must be small movement 3 times consecutively
      isRuminating = true;
    }
  } else {
    ruminationCount = 0;
    isRuminating = false;
  }

  return isRuminating;
}

// Helper function to load rumination threshold from config
void loadRuminationConfig() {
  StaticJsonDocument<512> doc;
  if (readJSONFromFile("/config.json", doc)) {
    if (doc.containsKey("ruminationThreshold")) {
      ruminationThreshold = doc["ruminationThreshold"].as<float>();
    }
  }
}
void readcredential(){
  File file = SPIFFS.open("/credential.json", FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, file);

  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

   SSID= doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  collarApi = doc["collarApi"].as<String>();
  WiFi.disconnect(true);
  
  
  file.close();


}


void setupServer() {
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendJSONResponse(request);
  });

  server.on("taser", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204, "text/plain", "");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
  });  

  server.on("/post-settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data received");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    String body(reinterpret_cast<char*>(data), len);
    Serial.println("Received POST data:");
    Serial.println(body);

    File file = SPIFFS.open("/config.json", FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open /config.json");
      return;
    }

    file.write((const uint8_t*)body.c_str(), body.length());
    file.close();
    Serial.println("POST body saved to /config.json");

    loadConfig();  // Consider moving this after confirming the file write
  });

  server.on("/post-url", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data received");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    String urlbody(reinterpret_cast<char*>(data), len);
    Serial.println("Received POST data:");
    Serial.println(urlbody);

    File file = SPIFFS.open("/url.json", FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open /url.json");
      return;
    }

    file.write((const uint8_t*)urlbody.c_str(), urlbody.length());
    file.close();
    Serial.println("POST urlbody saved to /url.json");
  });


  server.on("/test", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data received");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    String urlbody(reinterpret_cast<char*>(data), len);
    Serial.println("Received POST data:");
    Serial.println(urlbody);
      digitalWrite(buzz, HIGH);
      digitalWrite(shock, HIGH);
      delay(3000);
      digitalWrite(buzz, LOW);
      digitalWrite(shock, LOW);
   
    Serial.println("testing complete");
  });

  server.begin();
  Serial.println("Async Server started");
}


void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, 4, 2);
  pinMode(buzz, OUTPUT);
  pinMode(shock, OUTPUT);

  Serial.println("GPS Module Testing...");

  initSPIFFS();        // Mount filesystem first
  readcredential();    // Read SSID/password from file
  connectToWiFi(SSID, password); // Connect after credentials are loaded

  setupServer();
  loadConfig();
  loadRuminationConfig();

  sensors.begin();
  Wire.begin();
  initMPU();

  pulseSensor.analogInput(PulseWire);
  pulseSensor.setThreshold(Threshold);
  if (pulseSensor.begin()) {
    Serial.println("PulseSensor initialized.");
  } else {
    Serial.println("PulseSensor failed to begin.");
  }
}


void loop() {
  // AsyncWebServer is event-driven—no handleClient needed
if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // remove whitespace and newlines

    Serial.println(command);

    if (command.indexOf("cc=") != -1) {
      Serial.println("yes");
      changecred(command);
    } 
    if (command=="test"){
      Serial.print("testing shock and buzzer ");
      digitalWrite(buzz, HIGH);
      digitalWrite(shock, HIGH);
      delay(3000);
      digitalWrite(buzz, LOW);
      digitalWrite(shock, LOW);
      Serial.print("done testing ");
      

    }
    else {
      Serial.println("no");
    }
  }
  // GPS processing
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    gps.encode(c);
    if (gps.location.isUpdated()) {
      latestLat = gps.location.lat();
      latestLng = gps.location.lng();
      if (shapeType == "polygon") checkInsidePolygon();
      else checkDistanceFromSetPoint();
    }
  }

  unsigned long now = millis();

  if (now - lastTempReadTime >= tempInterval) {
    sensors.requestTemperatures();
    latestTemperature = sensors.getTempCByIndex(0);
    lastTempReadTime = now;
    Serial.print("Temp: "); Serial.println(latestTemperature);
  }

  if (now - lastPulseTime >= pulseInterval) {
    lastPulseTime = now;
    int bpm = pulseSensor.getBeatsPerMinute();
    if (pulseSensor.sawStartOfBeat()) {
      bpmsensor=bpm;
      Serial.print("BPM: "); Serial.println(bpm);
    }
  }

  if (now - lastStepTime >= stepInterval) {
    latestAzG = readVerticalAcceleration();
    if (detectStep(latestAzG)) {
      stepCount++;
      Serial.print("Step Count: "); Serial.println(stepCount);
    }
    lastStepTime = now;
  }

  if (now - lastConfigTime >= configInterval) {
    // loadConfig();
    // if (checkAndUpdateConfig()) fetchAndWriteConfig();
    // if (checkAndUpdateConfig2()) fetchAndWriteConfig2();
    Serial.println(shapeType);
    lastConfigTime = now;
  }
    if (now - lastRuminationCheck >= ruminationInterval) {
    lastRuminationCheck = now;
    if (detectRumination()) {
      Serial.println("Animal is ruminating 🐄");
    } else {
      Serial.println("Animal is NOT ruminating");
    }
  }

}


void changecred(String input) {
  int equalIndex = input.indexOf('=');
  int commaIndex = input.indexOf(',');
  int hyphenIndex = input.indexOf('|');

  if (equalIndex == -1 || commaIndex == -1 || hyphenIndex == -1) {
    Serial.println("Invalid format. Expected: cc=SSID,PASSWORD|ANY");
    return;
  }

  // Extract substrings
  String ssid = input.substring(equalIndex + 1, commaIndex);
  String pass = input.substring(commaIndex + 1, hyphenIndex);
  String api = input.substring(hyphenIndex + 1);

  ssid.trim();
  pass.trim();
  api.trim();

  // Print extracted values for debugging
  Serial.println("New SSID: " + ssid);
  Serial.println("New Password: " + pass);
  Serial.println("New API: " + api);

  // Save to JSON file
  StaticJsonDocument<200> doc;
  doc["ssid"] = ssid;
  doc["password"] = pass;
  doc["collarApi"] = api;

  File file = SPIFFS.open("/credential.json", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open credential.json for writing");
    return;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write to credential.json");
  } else {
    Serial.println("Credentials saved successfully");
  }

  file.close();

  // Update in-memory variables and reconnect
  SSID = ssid;
  password = pass;
  collarApi = api;

  WiFi.disconnect(true);
  delay(1000);
  connectToWiFi(SSID, password);
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) Serial.println("SPIFFS Mount Failed");
  else Serial.println("SPIFFS Mounted");
}

void connectToWiFi(String ssid, String password) {
  Serial.printf("Connecting to %s", ssid);
  WiFi.setHostname("li-55446625"); 
  WiFi.begin(ssid.c_str(), password.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500); Serial.print("."); retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else Serial.println("\nFailed to connect to WiFi.");
  
}

void loadConfig() {
  File f = SPIFFS.open("/config.json", "r");
  if (!f) { Serial.println("Failed to open config"); return; }
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, f)) { Serial.println("Config parse error"); f.close(); return; }
  f.close();
  shapeType = doc["shape"].as<String>();
}

bool readJSONFromFile(const char* path, JsonDocument& doc) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  return !e;
}

void initMPU() {
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 failed"); while (1);
  } else Serial.println("MPU6050 ready");
}

float readVerticalAcceleration() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  return az / 16384.0;
}

bool detectStep(float azG) {
  StaticJsonDocument<1024> cfg;
  if (readJSONFromFile("/config.json", cfg)) {
    steptrigger = cfg["steptrigger"].as<float>();
    stepThreshold = cfg["stepThreshold"].as<float>();
  }
  if (!stepDetected && azG > stepThreshold) { stepDetected = true; return true; }
  if (stepDetected && azG < steptrigger) stepDetected = false;
  return false;
}

bool checkDistanceFromSetPoint() {
  StaticJsonDocument<200> cfg;
  if (readJSONFromFile("/config.json", cfg)) {
    setLat = cfg["latitude"].as<double>();
    setLng = cfg["longitude"].as<double>();
    radius = cfg["radius"].as<double>();
  }
  double d = TinyGPSPlus::distanceBetween(
    gps.location.lat(), gps.location.lng(), setLat, setLng);
  Serial.printf("Distance: %.2f m\n", d);
  float difference = d - radius;
  bool outside=false;
if (difference > 40) {
  outside=true;
  // Too far outside, turn everything off
  digitalWrite(buzz, LOW);
  digitalWrite(shock, LOW);
}
else if (difference > 0) {
  outside=true;
  // Outside the radius,apply high shock and sound buzzer briefly
  digitalWrite(buzz, HIGH);
  digitalWrite(shock, HIGH);
  delay(2000);
  digitalWrite(buzz, LOW);
  digitalWrite(shock, LOW);
  // delay(4000);
}
else if (difference >= -1) {
  outside=false;
  // Within 2 meters inside the boundary, sound buzzer as warning
  digitalWrite(buzz, HIGH);
  digitalWrite(shock, LOW);
}
else {
  // Safely inside
  outside=false;
  digitalWrite(buzz, LOW);
  digitalWrite(shock, LOW);
}
  return outside;
}

bool checkInsidePolygon() {
  // double lat = gps.location.lat(), lng = gps.location.lng();
  // bool inside = isInsidePolygon(lat, lng);
  double distance;
  bool inside = isInsidePolygon(gps.location.lat(), gps.location.lng(), distance);
  if (inside) {
    digitalWrite(buzz, LOW); 
    digitalWrite(shock, LOW);
  } 
  // else if(distance<=0){
  //   digitalWrite(buzz, HIGH); digitalWrite(shock, HIGH);
  //   delay(2000);
  //   digitalWrite(buzz, LOW); digitalWrite(shock, LOW);
  // }
  else if (distance>=40) {
    digitalWrite(buzz, LOW);
     digitalWrite(shock, LOW);

    }
    else {
    digitalWrite(buzz, HIGH); digitalWrite(shock, HIGH);
    delay(2000);
    digitalWrite(buzz, LOW); digitalWrite(shock, LOW);
    // delay(4000);
  }
  return inside;
}




double pointToSegmentDistanceMeters(double lat, double lon,
                                    double lat1, double lon1,
                                    double lat2, double lon2) {
  // Vector projection to get closest point on segment
  double dx = lat2 - lat1;
  double dy = lon2 - lon1;

  if (dx == 0 && dy == 0) {
    // The segment is a single point
    return gps.distanceBetween(lat, lon, lat1, lon1);
  }

  double t = ((lat - lat1) * dx + (lon - lon1) * dy) / (dx * dx + dy * dy);
  t = max(0.0, min(1.0, t));  // Clamp t to [0, 1]

  double projLat = lat1 + t * dx;
  double projLon = lon1 + t * dy;

  return gps.distanceBetween(lat, lon, projLat, projLon);
}

bool isInsidePolygon(double lat, double lng, double &outDistanceMeters) {
  StaticJsonDocument<1024> cfg;
  if (readJSONFromFile("/config.json", cfg)) {
    JsonArray latA = cfg["latitudeList"].as<JsonArray>();
    JsonArray lngA = cfg["longitudeList"].as<JsonArray>();
    NUM_VERTICES = latA.size();
    for (int i = 0; i < NUM_VERTICES; i++) {
      polygonLat[i] = latA[i];
      polygonLng[i] = lngA[i];
    }
  }

  bool inside = false;
  double minDistance = 1e9;

  for (int i = 0, j = NUM_VERTICES - 1; i < NUM_VERTICES; j = i++) {
    // Ray casting algorithm
    bool intersect = ((polygonLat[i] > lat) != (polygonLat[j] > lat)) &&
                     (lng < (polygonLng[j] - polygonLng[i]) * (lat - polygonLat[i]) /
                            (polygonLat[j] - polygonLat[i]) + polygonLng[i]);
    if (intersect) inside = !inside;

    // Compute distance to the edge
    double dist = pointToSegmentDistanceMeters(lat, lng, polygonLat[i], polygonLng[i], polygonLat[j], polygonLng[j]);
    if (dist < minDistance) minDistance = dist;
  }

  outDistanceMeters = inside ? 0.0 : minDistance;
  return inside;
}



