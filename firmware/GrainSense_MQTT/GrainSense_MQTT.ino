/*
  GrainSense MQTT - kirim data sensor lewat MQTT (broker publik HiveMQ,
  tanpa daftar akun) supaya bisa dilihat di dashboard online:
  https://mcdinglebob.github.io/grainsense-dashboard/

  Board : DOIT ESP32 DEVKIT V1 (ESP32 Arduino core 2.x atau 3.x)
  Library (install lewat Library Manager):
    - "HX711" by Bogdan Necula
    - "Adafruit SHT31 Library" by Adafruit (pilih INSTALL ALL)
    - "PubSubClient" by Nick O'Leary
  WiFi sudah bawaan ESP32, tidak perlu install.

  Sensor yang belum dipasang tidak masalah: datanya tidak dikirim dan di
  dashboard tampil "--". Jadi sketch ini bisa dipakai sekarang (baru CO2 +
  soil moisture) maupun nanti saat semua modul sudah terpasang.

  Data dikirim sebagai JSON ke topik MQTT_TOPIC, contoh:
    {"ts":1793078400,"co2":612,"soil":15,"berat":4980.2,"suhuDalam":27.4,
     "rhDalam":68.1,"suhuMasuk":25.0,"rhMasuk":66.0,"pwm":0}
  ts = waktu (detik sejak 1970, dari internet/NTP).

  Serial Monitor: 115200 baud, line ending "New Line".
  Perintah:
    t        tare (nol-kan) load cell
    f-21.5   pakai faktor kalibrasi load cell (lihat GrainSense_Tes)
    p50      PWM blower 50% (0-100)
    zero     kalibrasi nol MH-Z19C (= 400 ppm), HANYA di udara luar
             setelah sensor menyala minimal 20 menit
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <Wire.h>
#include "HX711.h"
#include "Adafruit_SHT31.h"

// ===== WAJIB DIISI =====
const char *WIFI_SSID     = "NAMA_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";

// Harus sama dengan MQTT_TOPIC di config.js dashboard
const char *MQTT_HOST  = "broker.hivemq.com";
const int   MQTT_PORT  = 1883;
const char *MQTT_TOPIC = "grainsense/mcdinglebob/silo1";

const unsigned long INTERVAL_KIRIM = 10000UL;  // kirim tiap 10 detik

// ===== Pin (sesuai Wiring GrainSense) =====
const int DT_PIN    = 16;  // HX711 DT
const int SCK_PIN   = 17;  // HX711 SCK
const int SDA_PIN   = 21;  // SHT3X #1 ruang dalam
const int SCL_PIN   = 22;
const int SDA2_PIN  = 19;  // SHT3X #2 udara masuk
const int SCL2_PIN  = 18;
const int CO2_RX    = 4;   // ESP32 RX  <- TX MH-Z19C
const int CO2_TX    = 15;  // ESP32 TX  -> RX MH-Z19C
const int SOIL_PIN  = 34;  // AOUT soil moisture
const int PWM_PIN   = 26;  // PWM/TRIG modul MOSFET

// ===== Kalibrasi (isi dari hasil GrainSense_Tes / Tes_CO2_Soil) =====
int   SOIL_KERING = 3000;  // nilai mentah sensor di udara
int   SOIL_BASAH  = 1300;  // nilai mentah sensor di air
float FAKTOR_HX   = 0;     // faktor load cell, 0 = belum dikalibrasi
const bool CO2_ABC = false;

// ===== PWM =====
const int PWM_FREQ = 1000;
const int PWM_RES  = 8;
const int PWM_CH   = 0;    // hanya dipakai di core 2.x
int dutyPersen = 0;

HX711 scale;
Adafruit_SHT31 shtDalam(&Wire);
Adafruit_SHT31 shtMasuk(&Wire1);
HardwareSerial co2Serial(2);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool adaHX = false, adaDalam = false, adaMasuk = false;
unsigned long waktuKirim = 0;
unsigned long waktuMulai = 0;
unsigned long waktuCobaWiFi = 0;
unsigned long waktuCobaMqtt = 0;

// ---------- PWM ----------
void setDuty(int persen) {
  dutyPersen = constrain(persen, 0, 100);
  int duty = map(dutyPersen, 0, 100, 0, (1 << PWM_RES) - 1);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PWM_PIN, duty);
#else
  ledcWrite(PWM_CH, duty);
#endif
  Serial.print(">> PWM blower: ");
  Serial.print(dutyPersen);
  Serial.println("%");
}

// ---------- MH-Z19C ----------
byte checksumCO2(const byte *b) {
  byte s = 0;
  for (int i = 1; i < 8; i++) s += b[i];
  return 0xFF - s + 1;
}

void kirimCO2(byte cmd, byte b3) {
  byte pkt[9] = {0xFF, 0x01, cmd, b3, 0, 0, 0, 0, 0};
  pkt[8] = checksumCO2(pkt);
  co2Serial.write(pkt, 9);
}

// Mengembalikan ppm, atau -1 kalau tidak ada jawaban / data rusak
int bacaCO2() {
  while (co2Serial.available()) co2Serial.read();
  kirimCO2(0x86, 0x00);
  byte r[9];
  unsigned long t0 = millis();
  int n = 0;
  while (n < 9 && millis() - t0 < 300) {
    if (co2Serial.available()) {
      byte c = co2Serial.read();
      if (n == 0 && c != 0xFF) continue;
      r[n++] = c;
    }
  }
  if (n < 9 || r[1] != 0x86 || r[8] != checksumCO2(r)) return -1;
  return r[2] * 256 + r[3];
}

// Rata-rata 20 bacaan supaya tidak loncat-loncat
int bacaSoil() {
  long total = 0;
  for (int i = 0; i < 20; i++) {
    total += analogRead(SOIL_PIN);
    delay(2);
  }
  return total / 20;
}

// ---------- WiFi ----------
void mulaiWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menyambung ke WiFi ");
  Serial.print(WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi tersambung, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi belum tersambung (cek nama & password). Dicoba lagi otomatis.");
  }
  waktuCobaWiFi = millis();
}

// Coba sambung ulang tiap 30 detik tanpa menghentikan pembacaan sensor
void jagaWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - waktuCobaWiFi < 30000UL) return;
  waktuCobaWiFi = millis();
  Serial.println(">> WiFi putus, mencoba sambung ulang...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ---------- MQTT ----------
// Coba sambung ke broker tiap 5 detik kalau putus
void jagaMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) { mqttClient.loop(); return; }
  if (millis() - waktuCobaMqtt < 5000UL) return;
  waktuCobaMqtt = millis();
  String id = "grainsense-esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print(">> Menyambung ke broker MQTT... ");
  if (mqttClient.connect(id.c_str())) Serial.println("OK");
  else { Serial.print("gagal, kode "); Serial.println(mqttClient.state()); }
}

// ---------- Baca semua sensor lalu kirim ----------
void tambah(String &json, const char *nama, float nilai, int desimal) {
  json += ",\"";
  json += nama;
  json += "\":";
  json += String(nilai, desimal);
}

void bacaDanKirim() {
  // Sensor yang gagal dibaca tidak dimasukkan ke JSON
  time_t sekarang = time(nullptr);
  String json = "{\"ts\":";
  json += sekarang > 1600000000 ? String((uint32_t)sekarang) : String(0);

  Serial.print("CO2: ");
  int ppm = bacaCO2();
  if (ppm >= 0) {
    tambah(json, "co2", ppm, 0);
    Serial.print(ppm);
    if (millis() - waktuMulai < 180000UL) Serial.print(" (pemanasan)");
  } else Serial.print("--");

  int soil = bacaSoil();
  int persen = constrain(map(soil, SOIL_KERING, SOIL_BASAH, 0, 100), 0, 100);
  Serial.print(" | Soil: ");
  if (soil >= 50 && soil <= 4050) {
    tambah(json, "soil", persen, 0);
    Serial.print(persen);
    Serial.print("%");
  } else Serial.print("-- (cek kabel AOUT->D34)");

  Serial.print(" | Berat: ");
  if (adaHX && FAKTOR_HX != 0 && scale.wait_ready_timeout(200)) {
    float g = scale.get_units(5);
    tambah(json, "berat", g, 1);
    Serial.print(g, 1);
    Serial.print(" g");
  } else Serial.print("--");

  Serial.print(" | Dalam: ");
  if (adaDalam) {
    float t = shtDalam.readTemperature(), h = shtDalam.readHumidity();
    if (!isnan(t)) tambah(json, "suhuDalam", t, 1);
    if (!isnan(h)) tambah(json, "rhDalam", h, 1);
    Serial.print(t, 1); Serial.print("C "); Serial.print(h, 0); Serial.print("%");
  } else Serial.print("--");

  Serial.print(" | Masuk: ");
  if (adaMasuk) {
    float t = shtMasuk.readTemperature(), h = shtMasuk.readHumidity();
    if (!isnan(t)) tambah(json, "suhuMasuk", t, 1);
    if (!isnan(h)) tambah(json, "rhMasuk", h, 1);
    Serial.print(t, 1); Serial.print("C "); Serial.print(h, 0); Serial.print("%");
  } else Serial.print("--");

  tambah(json, "pwm", dutyPersen, 0);
  json += "}";
  Serial.print(" | PWM: ");
  Serial.print(dutyPersen);
  Serial.println("%");

  if (!mqttClient.connected()) {
    Serial.println(">> Tidak terkirim: belum tersambung ke WiFi/broker.");
    return;
  }
  // retained = true: dashboard yang baru dibuka langsung dapat nilai terakhir
  if (mqttClient.publish(MQTT_TOPIC, json.c_str(), true)) {
    Serial.print(">> Terkirim: ");
    Serial.println(json);
  } else {
    Serial.println(">> Gagal kirim MQTT.");
  }
}

// ---------- Perintah Serial ----------
void prosesPerintah(String in) {
  in.trim();
  if (in.length() == 0) return;
  char c = in.charAt(0);
  if (in == "t" || in == "T") {
    if (!adaHX) { Serial.println(">> HX711 tidak terdeteksi."); return; }
    scale.tare(20);
    Serial.println(">> Tare selesai.");
  } else if (c == 'f' || c == 'F') {
    float f = in.substring(1).toFloat();
    if (f == 0) { Serial.println(">> Faktor tidak valid."); return; }
    FAKTOR_HX = f;
    scale.set_scale(f);
    Serial.print(">> Faktor dipakai: ");
    Serial.println(f, 4);
  } else if (c == 'p' || c == 'P') {
    setDuty(in.substring(1).toInt());
  } else if (in == "zero") {
    if (millis() - waktuMulai < 20UL * 60000UL) {
      Serial.println(">> Tunggu sampai sensor menyala 20 menit di udara luar, lalu ketik zero lagi.");
      return;
    }
    kirimCO2(0x87, 0x00);
    Serial.println(">> Kalibrasi nol CO2 dikirim. Titik sekarang dianggap 400 ppm.");
  } else {
    Serial.println(">> Perintah: t | f<faktor> | p<0-100> | zero");
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===== GRAINSENSE IoT =====");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire1.begin(SDA2_PIN, SCL2_PIN);
  scale.begin(DT_PIN, SCK_PIN);
  co2Serial.begin(9600, SERIAL_8N1, CO2_RX, CO2_TX);
  analogReadResolution(12);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PWM_PIN, PWM_FREQ, PWM_RES);
#else
  ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(PWM_PIN, PWM_CH);
#endif
  setDuty(0);

  delay(1000);
  kirimCO2(0x79, CO2_ABC ? 0xA0 : 0x00);

  adaDalam = shtDalam.begin(0x44);
  adaMasuk = shtMasuk.begin(0x44);
  adaHX = scale.wait_ready_timeout(1000);
  Serial.println(adaDalam ? "[OK] SHT3X ruang dalam" : "[--] SHT3X ruang dalam tidak terpasang");
  Serial.println(adaMasuk ? "[OK] SHT3X udara masuk" : "[--] SHT3X udara masuk tidak terpasang");
  Serial.println(adaHX ? "[OK] HX711" : "[--] HX711 tidak terpasang");
  Serial.println(bacaCO2() >= 0 ? "[OK] MH-Z19C" : "[--] MH-Z19C tidak menjawab (cek Vin 5V, TX->D4, RX->D15)");

  if (adaHX) {
    scale.tare(20);
    if (FAKTOR_HX != 0) scale.set_scale(FAKTOR_HX);
    else Serial.println("Load cell belum dikalibrasi: berat tidak dikirim. Isi FAKTOR_HX.");
  }

  mulaiWiFi();
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // jam dari internet
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(512);
  waktuMulai = millis();
  waktuKirim = millis() - INTERVAL_KIRIM;  // langsung kirim sekali
}

// ---------- Loop ----------
void loop() {
  if (Serial.available()) prosesPerintah(Serial.readStringUntil('\n'));
  jagaWiFi();
  jagaMqtt();
  if (millis() - waktuKirim >= INTERVAL_KIRIM) {
    waktuKirim = millis();
    bacaDanKirim();
  }
}
