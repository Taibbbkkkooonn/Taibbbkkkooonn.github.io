#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID     "Zao"
#define WIFI_PASS     "11111111"
#define FIREBASE_URL  "https://smartfarm-chatbot-default-rtdb.firebaseio.com/data.json"

#define LORA_SS    5
#define LORA_RST   2
#define LORA_DIO0  4
#define LORA_BAND  433E6

#define SPI_SCK    18
#define SPI_MISO   19
#define SPI_MOSI   23

#define SO_NODE    3

struct NodeData {
  int id;
  float t;
  float h;
  float soil;
  unsigned long last_update;
};

NodeData node1 = {1, 0, 0, 0, 0};
NodeData node2 = {2, 0, 0, 0, 0};
NodeData node3 = {3, 0, 0, 0, 0};

int rl1_cmd[SO_NODE + 1] = {0, 0, 0, 0};
int rl2_cmd[SO_NODE + 1] = {0, 0, 0, 0};

unsigned long chu_ky_poll_ms = 1200;
unsigned long thoi_gian_poll_cuoi = 0;

unsigned long chu_ky_day_firebase_ms = 2000;
unsigned long thoi_gian_day_firebase_cuoi = 0;

bool co_phan_hoi_trong_vong = false;

String doc_goi_lora() {
  String s = "";
  while (LoRa.available()) {
    s += (char)LoRa.read();
  }
  s.trim();
  return s;
}

void gui_lenh_den_node(int id_dich, int rl1, int rl2) {
  String goi = "CMD," + String(id_dich) + "," + String(rl1) + "," + String(rl2);

  LoRa.beginPacket();
  LoRa.print(goi);
  LoRa.endPacket();

  Serial.print("GUI LENH: ");
  Serial.println(goi);
}

bool tach_phan_hoi(String goi, int &id_gui, float &nhiet_do, float &do_am, int &do_am_dat, int &rl1, int &rl2) {
  if (!goi.startsWith("STA,")) return false;

  int p1 = goi.indexOf(',');
  int p2 = goi.indexOf(',', p1 + 1);
  int p3 = goi.indexOf(',', p2 + 1);
  int p4 = goi.indexOf(',', p3 + 1);
  int p5 = goi.indexOf(',', p4 + 1);
  int p6 = goi.indexOf(',', p5 + 1);

  if (p6 < 0) return false;

  id_gui    = goi.substring(p1 + 1, p2).toInt();
  nhiet_do  = goi.substring(p2 + 1, p3).toFloat();
  do_am     = goi.substring(p3 + 1, p4).toFloat();
  do_am_dat = goi.substring(p4 + 1, p5).toInt();
  rl1       = goi.substring(p5 + 1, p6).toInt();
  rl2       = goi.substring(p6 + 1).toInt();

  return true;
}

void cap_nhat_node(int id_gui, float nhiet_do, float do_am, int do_am_dat) {
  if (id_gui == 1) { node1.t = nhiet_do; node1.h = do_am; node1.soil = do_am_dat; node1.last_update = millis(); }
  if (id_gui == 2) { node2.t = nhiet_do; node2.h = do_am; node2.soil = do_am_dat; node2.last_update = millis(); }
  if (id_gui == 3) { node3.t = nhiet_do; node3.h = do_am; node3.soil = do_am_dat; node3.last_update = millis(); }
}

// HÀM MỚI: chờ và xử lý MỌI phản hồi trong một khung thời gian, không khóa theo ID
bool doi_phan_hoi_trong_khung_thoi_gian(unsigned long timeout_ms) {
  unsigned long t0 = millis();
  bool nhan_duoc = false;

  while (millis() - t0 < timeout_ms) {
    int packet_size = LoRa.parsePacket();
    if (packet_size) {
      String goi = doc_goi_lora();
      if (goi.length() == 0) continue;

      int id_gui;
      float nhiet_do;
      float do_am;
      int do_am_dat;
      int rl1;
      int rl2;

      if (tach_phan_hoi(goi, id_gui, nhiet_do, do_am, do_am_dat, rl1, rl2)) {
        Serial.print("PHAN HOI NODE ");
        Serial.print(id_gui);
        Serial.print(": T=");
        Serial.print(nhiet_do, 1);
        Serial.print("C H=");
        Serial.print(do_am, 0);
        Serial.print("% SOIL=");
        Serial.print(do_am_dat);
        Serial.print("% RL1=");
        Serial.print(rl1);
        Serial.print(" RL2=");
        Serial.println(rl2);

        co_phan_hoi_trong_vong = true;
        cap_nhat_node(id_gui, nhiet_do, do_am, do_am_dat);
        nhan_duoc = true;
      } else {
        Serial.print("NHAN GOI LA: ");
        Serial.println(goi);
      }
    }
    delay(1);
    yield();
  }

  if (!nhan_duoc) {
    Serial.println("KHONG NHAN PHAN HOI TRONG KHUNG THOI GIAN");
  }

  return nhan_duoc;
}

void poll_1_vong() {
  co_phan_hoi_trong_vong = false;

  for (int id = 1; id <= SO_NODE; id++) {
    int r1 = rl1_cmd[id] ? 1 : 0;
    int r2 = rl2_cmd[id] ? 1 : 0;

    gui_lenh_den_node(id, r1, r2);

    // Chờ phản hồi chung, gói nào về cũng xử lý theo ID trong payload
    doi_phan_hoi_trong_khung_thoi_gian(500);

    delay(80);
  }

  // Hốt thêm các gói về trễ sau khi đã gửi đủ 3 node
  doi_phan_hoi_trong_khung_thoi_gian(200);
}

void doc_lenh_tu_firebase_http() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setTimeout(1500);
  http.setReuse(false);
  http.begin(FIREBASE_URL);

  int code = http.GET();
  Serial.print("GET code = ");
  Serial.println(code);

  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);

    if (!deserializeJson(doc, payload)) {
      int a1 = doc["devices"]["A"]["pump"]["state"] == "on" ? 1 : 0;
      int a2 = doc["devices"]["A"]["light"]["state"] == "on" ? 1 : 0;
      int b1 = doc["devices"]["B"]["pump"]["state"] == "on" ? 1 : 0;
      int b2 = doc["devices"]["B"]["light"]["state"] == "on" ? 1 : 0;
      int c1 = doc["devices"]["C"]["pump"]["state"] == "on" ? 1 : 0;
      int c2 = doc["devices"]["C"]["light"]["state"] == "on" ? 1 : 0;

      rl1_cmd[1] = a1; rl2_cmd[1] = a2;
      rl1_cmd[2] = b1; rl2_cmd[2] = b2;
      rl1_cmd[3] = c1; rl2_cmd[3] = c2;

      Serial.printf("CMD A: R1=%d R2=%d | CMD B: R1=%d R2=%d | CMD C: R1=%d R2=%d\n",
                    a1, a2, b1, b2, c1, c2);
    } else {
      Serial.println("Loi JSON");
    }
  }

  http.end();
}

void day_trang_thai_len_firebase_http() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setTimeout(1500);
  http.setReuse(false);
  http.begin(FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(1536);

  if (co_phan_hoi_trong_vong) {
    doc["online"] = 1;
  }

  JsonObject sensors = doc.createNestedObject("sensors");
  JsonObject A = sensors.createNestedObject("A");
  JsonObject B = sensors.createNestedObject("B");
  JsonObject C = sensors.createNestedObject("C");

  A["air_temp_c"] = String(node1.t, 1);
  A["air_humi"]   = String(node1.h, 0);
  A["soil_pct"]   = String(node1.soil, 0);

  B["air_temp_c"] = String(node2.t, 1);
  B["air_humi"]   = String(node2.h, 0);
  B["soil_pct"]   = String(node2.soil, 0);

  C["air_temp_c"] = String(node3.t, 1);
  C["air_humi"]   = String(node3.h, 0);
  C["soil_pct"]   = String(node3.soil, 0);

  String json;
  serializeJson(doc, json);

  int codePatch = http.PATCH(json);
  Serial.print("PATCH code = ");
  Serial.println(codePatch);

  http.end();
}

void ket_noi_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("KET NOI WIFI");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WIFI OK IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("KHOI DONG TRUNG TAM LORA - POLL 3 NODE + FIREBASE SAU VONG");

  ket_noi_wifi();

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("LOI LORA");
    while (true) { delay(1000); }
  }

  Serial.println("LORA OK");
}

void loop() {
  unsigned long now = millis();

  if (now - thoi_gian_poll_cuoi >= chu_ky_poll_ms) {
    thoi_gian_poll_cuoi = now;

    poll_1_vong();

    doc_lenh_tu_firebase_http();

    if (co_phan_hoi_trong_vong || (now - thoi_gian_day_firebase_cuoi >= chu_ky_day_firebase_ms)) {
      thoi_gian_day_firebase_cuoi = now;
      day_trang_thai_len_firebase_http();
    }

    Serial.println("---- TRANG THAI ----");
    Serial.printf("Node1: %.1fC %.0f%% %.0f%% dat\n", node1.t, node1.h, node1.soil);
    Serial.printf("Node2: %.1fC %.0f%% %.0f%% dat\n", node2.t, node2.h, node2.soil);
    Serial.printf("Node3: %.1fC %.0f%% %.0f%% dat\n", node3.t, node3.h, node3.soil);
  }

  delay(1);
  yield();
}
