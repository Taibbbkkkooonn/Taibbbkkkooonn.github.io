#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

#define LORA_SS    5
#define LORA_RST   2
#define LORA_DIO0  4
#define LORA_BAND  433E6

#define DHT_PIN    32
#define SOIL_PIN   34
#define DHTTYPE    DHT11

#define RL1_PIN    26
#define RL2_PIN    27

#define NODE_ID    3

#define SPI_SCK    18
#define SPI_MISO   19
#define SPI_MOSI   23

DHT dht(DHT_PIN, DHTTYPE);

bool rl1_trang_thai = false;
bool rl2_trang_thai = false;

unsigned long thoi_gian_gui_cuoi = 0;
const unsigned long chu_ky_gui_tu_dong = 0;

int SOIL_KHO = 4095;
int SOIL_UOT = 0;

unsigned long dem_loi_lora = 0;
const unsigned long NGUONG_LOI_LORA = 5;

void reset_lora() {
  Serial.println("RESET LORA...");
  LoRa.end();
  delay(50);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("LOI LORA SAU KHI RESET");
  } else {
    Serial.println("RESET LORA OK");
    dem_loi_lora = 0;
  }
}

void tang_loi_lora(const char *ly_do) {
  Serial.print("LOI LORA: ");
  Serial.println(ly_do);
  dem_loi_lora++;
  Serial.print("DEM LOI LORA = ");
  Serial.println(dem_loi_lora);
  if (dem_loi_lora >= NGUONG_LOI_LORA) {
    reset_lora();
  }
}

String doc_goi_lora() {
  String s = "";
  while (LoRa.available()) {
    s += (char)LoRa.read();
  }
  s.trim();
  return s;
}

void gui_phan_hoi(float nhiet_do, float do_am, int do_am_dat, bool rl1, bool rl2) {
  String goi = "STA," + String(NODE_ID) + "," +
               String(nhiet_do, 1) + "," +
               String(do_am, 0) + "," +
               String(do_am_dat) + "," +
               String(rl1 ? 1 : 0) + "," +
               String(rl2 ? 1 : 0);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(goi);
  LoRa.endPacket(true);
  LoRa.receive();

  dem_loi_lora = 0;

  Serial.print("GUI PHAN HOI: ");
  Serial.println(goi);
}

int doc_soil_trung_binh(int so_mau) {
  long tong = 0;
  for (int i = 0; i < so_mau; i++) {
    tong += analogRead(SOIL_PIN);
    delay(2);
  }
  return (int)(tong / so_mau);
}

int chuyen_soil_ve_phan_tram(int raw) {
  if (raw < 0) raw = 0;
  if (raw > 4095) raw = 4095;

  if (SOIL_KHO == SOIL_UOT) return 0;

  int phan_tram = map(raw, SOIL_KHO, SOIL_UOT, 100, 0);

  if (phan_tram < 0) phan_tram = 0;
  if (phan_tram > 100) phan_tram = 100;

  return phan_tram;
}

// parse "CMD,<id>,<r1>,<r2>" co the co rac truoc
bool parse_cmd(String goi, int &id_dich, int &rl1_cmd, int &rl2_cmd) {
  goi.trim();
  int idx = goi.indexOf("CMD,");
  if (idx < 0) {
    tang_loi_lora("PARSE CMD: KHONG THAY 'CMD,'");
    return false;
  }
  if (idx > 0) {
    goi = goi.substring(idx);
  }

  int p1 = goi.indexOf(',');
  if (p1 < 0) {
    tang_loi_lora("PARSE CMD: KHONG CO DAU PHAY 1");
    return false;
  }

  int p2 = goi.indexOf(',', p1 + 1);
  if (p2 < 0) {
    tang_loi_lora("PARSE CMD: KHONG CO DAU PHAY 2");
    return false;
  }

  int p3 = goi.indexOf(',', p2 + 1);
  if (p3 < 0) {
    tang_loi_lora("PARSE CMD: KHONG CO DAU PHAY 3");
    return false;
  }

  String s_id  = goi.substring(p1 + 1, p2);
  String s_rl1 = goi.substring(p2 + 1, p3);
  String s_rl2 = goi.substring(p3 + 1);

  s_id.trim();
  s_rl1.trim();
  s_rl2.trim();

  id_dich = s_id.toInt();
  rl1_cmd = (int)s_rl1.toFloat();
  rl2_cmd = (int)s_rl2.toFloat();

  Serial.print("PARSE CMD OK: id_dich=");
  Serial.print(id_dich);
  Serial.print(" rl1_cmd=");
  Serial.print(rl1_cmd);
  Serial.print(" rl2_cmd=");
  Serial.println(rl2_cmd);

  return true;
}

// parse id trong goi STA/TA/...: <header>,<id>,...
bool parse_id_trong_goi(String goi, int &id_dich) {
  goi.trim();
  int p1 = goi.indexOf(',');
  if (p1 < 0) {
    tang_loi_lora("PARSE ID: KHONG CO DAU PHAY 1");
    return false;
  }
  int p2 = goi.indexOf(',', p1 + 1);
  String s_id;
  if (p2 < 0) {
    s_id = goi.substring(p1 + 1);
  } else {
    s_id = goi.substring(p1 + 1, p2);
  }
  s_id.trim();
  if (s_id.length() == 0) {
    tang_loi_lora("PARSE ID: TRONG");
    return false;
  }
  id_dich = s_id.toInt();
  Serial.print("PARSE ID OK: id_dich=");
  Serial.println(id_dich);
  return true;
}

void gui_sta_hien_tai() {
  float nhiet_do = dht.readTemperature();
  float do_am = dht.readHumidity();
  int soil_raw = doc_soil_trung_binh(10);
  int do_am_dat = chuyen_soil_ve_phan_tram(soil_raw);

  if (isnan(nhiet_do)) nhiet_do = 0;
  if (isnan(do_am)) do_am = 0;

  Serial.print("SOIL RAW TB = ");
  Serial.print(soil_raw);
  Serial.print(" | SOIL % = ");
  Serial.println(do_am_dat);

  gui_phan_hoi(nhiet_do, do_am, do_am_dat, rl1_trang_thai, rl2_trang_thai);
}

void xu_ly_goi(String goi) {
  Serial.print("NHAN GOI RAW: ");
  Serial.println(goi);

  if (goi.length() == 0) {
    tang_loi_lora("GOI RONG");
    return;
  }

  bool da_gui_sta = false;

  // LENH DIEU KHIEN CMD
  if (goi.indexOf("CMD,") >= 0) {
    int id_dich = 0;
    int rl1_cmd = 0;
    int rl2_cmd = 0;

    if (parse_cmd(goi, id_dich, rl1_cmd, rl2_cmd)) {
      Serial.print("ID DICH: ");
      Serial.println(id_dich);

      if (id_dich == NODE_ID) {
        rl1_trang_thai = (rl1_cmd == 1);
        rl2_trang_thai = (rl2_cmd == 1);

        digitalWrite(RL1_PIN, rl1_trang_thai ? HIGH : LOW);
        digitalWrite(RL2_PIN, rl2_trang_thai ? HIGH : LOW);

        Serial.print("DA THUC THI: RL1=");
        Serial.print(rl1_trang_thai);
        Serial.print(" RL2=");
        Serial.println(rl2_trang_thai);

        gui_sta_hien_tai();
        da_gui_sta = true;
        dem_loi_lora = 0;
      } else {
        Serial.println("KHONG PHAI LENH CHO NODE NAY");
      }
    } else {
      Serial.println("PARSE CMD LOI, BO QUA CMD");
    }
  }

  // GOI QUERY STA/TA/... CHI PHAN HOI KHI ID DUNG
  if (!da_gui_sta) {
    if (goi.indexOf("STA") >= 0 || goi.indexOf("TA") >= 0) {
      int id_dich = 0;
      if (parse_id_trong_goi(goi, id_dich)) {
        if (id_dich == NODE_ID) {
          Serial.println("GOI STA/TA DUNG ID -> GUI STA HIEN TAI");
          gui_sta_hien_tai();
          da_gui_sta = true;
          dem_loi_lora = 0;
        } else {
          Serial.print("STA/TA NHUNG ID KHONG KHOP, ID DICH = ");
          Serial.println(id_dich);
        }
      }
    } else {
      Serial.println("GOI KHONG CO CMD/STA/TA, BO QUA");
      tang_loi_lora("GOI KHONG CO TU KHOA");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.print("KHOI DONG NODE ID = ");
  Serial.println(NODE_ID);

  dht.begin();

  pinMode(SOIL_PIN, INPUT);

  pinMode(RL1_PIN, OUTPUT);
  pinMode(RL2_PIN, OUTPUT);
  digitalWrite(RL1_PIN, LOW);
  digitalWrite(RL2_PIN, LOW);
  rl1_trang_thai = false;
  rl2_trang_thai = false;

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("LOI LORA");
    while (true) { delay(1000); }
  }

  LoRa.receive();

  Serial.println("LORA OK");
}

void loop() {
  int packet_size = LoRa.parsePacket();
  if (packet_size) {
    if (packet_size > 255) {
      tang_loi_lora("PACKET SIZE QUA LON");
      while (LoRa.available()) {
        LoRa.read();
      }
    } else {
      String goi = doc_goi_lora();
      Serial.print("RAW GOI: ");
      Serial.println(goi);
      if (goi.length() > 0) {
        xu_ly_goi(goi);
      } else {
        tang_loi_lora("GOI DOC DUOC RONG");
      }
    }
  }

  delay(1);
}
