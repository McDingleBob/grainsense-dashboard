/*
  GrainSense IoT - kirim data sensor ke ThingSpeak supaya bisa dilihat
  di dashboard online (GitHub Pages).

  Board : DOIT ESP32 DEVKIT V1 (ESP32 Arduino core 2.x atau 3.x)
  Library (install lewat Library Manager):
    - "HX711" by Bogdan Necula
    - "Adafruit SHT31 Library" by Adafruit (pilih INSTALL ALL)
  WiFi dan HTTPClient sudah bawaan ESP32, tidak perlu install.

  Sensor yang belum dipasang tidak masalah: datanya tidak dikirim dan di
  dashboard tampil "--". Jadi sketch ini bisa dipakai sekarang (baru CO2 +
  soil moisture) maupun nanti saat semua modul sudah terpasang.

  Kolom (field) ThingSpeak:
    field1 CO2 (ppm)            field5 Kelembapan ruang dalam (%)
    field2 Soil (mentah 0-4095) field6 Suhu udara masuk (C)
    field3 Berat (g)            field7 Kelembapan udara masuk (%)
    field4 Suhu ruang dalam (C) field8 PWM blower (%)

  Serial Monitor: 115200 baud, line ending "New Line".
  Perintah:
    t        tare (nol-kan) load cell, kosongkan wadah dulu
    500      kalibrasi load cell: taruh benda 500 g lalu ketik 500
             (angka = berat benda dalam gram)
    f-21.5   pakai faktor kalibrasi yang sudah dicatat
    Hasil tare dan kalibrasi disimpan di memori ESP32, jadi tidak hilang
    saat ESP32 mati atau di-reset.
    p50      PWM blower 50% (0-100)
    zero     kalibrasi nol MH-Z19C (= 400 ppm), HANYA di udara luar
             setelah sensor menyala minimal 20 menit
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Preferences.h>
#include "HX711.h"
#include "Adafruit_SHT31.h"

// ===== WAJIB DIISI =====
const char *WIFI_SSID     = "NAMA_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";
const char *TS_WRITE_KEY  = "ISI_WRITE_API_KEY";  // dari ThingSpeak > API Keys

// Akun ThingSpeak gratis: minimal 15 detik antar kiriman. 20 detik aman.
const unsigned long INTERVAL_KIRIM = 20000UL;

// ===== Pin (sesuai Wiring GrainSense) =====
const int DT_PIN    = 16;  // HX711 DT
const int SCK_PIN   = 17;  // HX711 SCK
const int SDA_PIN   = 21;  // SHT3X #1 ruang dalam
const int SCL_PIN   = 22;
// SHT3X #2 udara masuk bisa dipasang dengan salah satu cara:
//  a) di bus yang sama (GPIO21/22) dengan pin AD/ADR ke 3V3 -> alamat 0x45
//  b) di bus kedua GPIO19 (SDA) / GPIO18 (SCL) -> alamat 0x44
// Firmware mencoba keduanya otomatis.
const int SDA2_PIN  = 19;
const int SCL2_PIN  = 18;
const int CO2_RX    = 4;   // ESP32 RX  <- TX MH-Z19C
const int CO2_TX    = 15;  // ESP32 TX  -> RX MH-Z19C
const int SOIL_PIN  = 34;  // AOUT soil moisture
const int PWM_PIN   = 26;  // PWM/TRIG modul MOSFET

// ===== Kalibrasi load cell =====
// 0 = pakai hasil kalibrasi yang tersimpan di ESP32 (perintah 500 / f...)
// Kalau sudah punya angka dari GrainSense_Tes, boleh langsung diisi di sini.
float FAKTOR_HX = 0;
const bool CO2_ABC = false;

// ===== PWM =====
const int PWM_FREQ = 1000;
const int PWM_RES  = 8;
const int PWM_CH   = 0;    // hanya dipakai di core 2.x
int dutyPersen = 0;

HX711 scale;
Adafruit_SHT31 shtDalam(&Wire);
Adafruit_SHT31 shtMasukA(&Wire);   // cara a: bus 21/22, alamat 0x45
Adafruit_SHT31 shtMasukB(&Wire1);  // cara b: bus 19/18, alamat 0x44
Adafruit_SHT31 *shtMasuk = &shtMasukA;
HardwareSerial co2Serial(2);
Preferences memori;

bool adaHX = false, adaDalam = false, adaMasuk = false;
unsigned long waktuKirim = 0;
unsigned long waktuMulai = 0;
unsigned long waktuCobaWiFi = 0;

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

// ---------- Baca semua sensor lalu kirim ----------
void tambah(String &data, int no, float nilai, int desimal) {
  if (data.length()) data += "&";
  data += "field";
  data += no;
  data += "=";
  data += String(nilai, desimal);
}

void bacaDanKirim() {
  // Sensor yang gagal dibaca tidak ditambahkan ke URL
  String url = "";  // isi field yang akan dikirim, contoh field1=612&field2=3276

  Serial.print("CO2: ");
  int ppm = bacaCO2();
  if (ppm >= 0) {
    tambah(url, 1, ppm, 0);
    Serial.print(ppm);
    if (millis() - waktuMulai < 180000UL) Serial.print(" (pemanasan)");
  } else Serial.print("--");

  // Soil dikirim sebagai nilai mentah ADC (0-4095): makin basah, makin kecil
  int soil = bacaSoil();
  tambah(url, 2, soil, 0);
  Serial.print(" | Soil: ");
  Serial.print(soil);
  if (soil < 50) Serial.print(" (terlalu kecil, cek AOUT->D34 dan VCC)");

  Serial.print(" | Berat: ");
  if (adaHX && FAKTOR_HX != 0 && scale.wait_ready_timeout(200)) {
    float g = scale.get_units(5);
    tambah(url, 3, g, 1);
    Serial.print(g, 1);
    Serial.print(" g");
  } else if (adaHX && scale.wait_ready_timeout(200)) {
    // Belum dikalibrasi: tampilkan angka mentah supaya kelihatan sensornya merespons
    Serial.print("-- (belum dikalibrasi, mentah: ");
    Serial.print(scale.get_value(5), 0);
    Serial.print(")");
  } else if (adaHX) Serial.print("-- (HX711 tidak menjawab)");
  else Serial.print("--");

  Serial.print(" | Dalam: ");
  if (adaDalam) {
    float t = shtDalam.readTemperature(), h = shtDalam.readHumidity();
    if (!isnan(t)) tambah(url, 4, t, 1);
    if (!isnan(h)) tambah(url, 5, h, 1);
    Serial.print(t, 1); Serial.print("C "); Serial.print(h, 0); Serial.print("%");
  } else Serial.print("--");

  Serial.print(" | Masuk: ");
  if (adaMasuk) {
    float t = shtMasuk->readTemperature(), h = shtMasuk->readHumidity();
    if (!isnan(t)) tambah(url, 6, t, 1);
    if (!isnan(h)) tambah(url, 7, h, 1);
    Serial.print(t, 1); Serial.print("C "); Serial.print(h, 0); Serial.print("%");
  } else Serial.print("--");

  tambah(url, 8, dutyPersen, 0);
  Serial.print(" | PWM: ");
  Serial.print(dutyPersen);
  Serial.println("%");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(">> Tidak terkirim: WiFi belum tersambung.");
    return;
  }
  Serial.print(">> Kirim: ");
  Serial.println(url);
  // Kirim lewat HTTPS (POST). HTTP biasa sering dicegat jaringan seluler/hotspot
  // sehingga dibalas "400 Bad Request".
  WiFiClientSecure klien;
  klien.setInsecure();  // tidak cek sertifikat, cukup untuk prototipe
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(klien, "https://api.thingspeak.com/update");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int kode = http.POST("api_key=" + String(TS_WRITE_KEY) + "&" + url);
  String balasan = http.getString();
  http.end();
  // ThingSpeak membalas nomor entri (>0) kalau berhasil, "0" kalau ditolak
  if (kode == 200 && balasan.toInt() > 0) {
    Serial.print(">> Terkirim ke ThingSpeak, entri #");
    Serial.println(balasan);
  } else if (kode == 200) {
    Serial.println(">> Ditolak ThingSpeak: kiriman terlalu cepat (<15 detik).");
  } else {
    Serial.print(">> Gagal kirim, kode HTTP: ");
    Serial.print(kode);
    Serial.print(" | balasan: ");
    Serial.println(balasan);
    if (kode == 400 || kode == 401)
      Serial.println(">> Biasanya Write API Key salah. Salin ulang dari ThingSpeak > API Keys > Write API Key.");
  }
}

// ---------- Load cell ----------
void pakaiFaktor(float f) {
  FAKTOR_HX = f;
  scale.set_scale(f);
  memori.putFloat("faktor", f);
  Serial.print(">> Faktor kalibrasi: ");
  Serial.print(f, 4);
  Serial.println(" (disimpan di ESP32)");
}

// ---------- Perintah Serial ----------
void prosesPerintah(String in) {
  in.trim();
  if (in.length() == 0) return;
  char c = in.charAt(0);
  if (in == "t" || in == "T") {
    if (!adaHX) { Serial.println(">> HX711 tidak terdeteksi."); return; }
    scale.tare(20);
    memori.putLong("offset", scale.get_offset());
    Serial.println(">> Tare selesai dan disimpan.");
  } else if (c == 'f' || c == 'F') {
    float f = in.substring(1).toFloat();
    if (f == 0) { Serial.println(">> Faktor tidak valid."); return; }
    pakaiFaktor(f);
  } else if (isDigit(c)) {
    // Kalibrasi: benda dengan berat diketahui sudah ditaruh di load cell
    float beratAsli = in.toFloat();
    if (!adaHX) { Serial.println(">> HX711 tidak terdeteksi."); return; }
    if (beratAsli <= 0) { Serial.println(">> Berat tidak valid."); return; }
    float nilai = scale.get_value(20);
    Serial.print(">> Angka mentah dengan benda: ");
    Serial.println(nilai, 0);
    if (abs(nilai) < 100) {
      Serial.println(">> Hampir 0, load cell tidak merasakan bendanya.");
      Serial.println(">> Kemungkinan: tare (t) dilakukan saat benda masih di atas, atau kabel E+/E-/A+/A- load cell ke HX711 salah/longgar.");
      Serial.println(">> Coba: angkat benda, ketik t, taruh benda lagi, tunggu angka mentah berubah, lalu ketik beratnya.");
      return;
    }
    pakaiFaktor(nilai / beratAsli);
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
    Serial.println(">> Perintah: t | <berat gram, mis. 500> | f<faktor> | p<0-100> | zero");
  }
}

// Tampilkan alamat I2C yang terdeteksi di bus 21/22 (0x44 = SHT AD ke GND, 0x45 = SHT AD ke 3V3)
void scanI2C() {
  Serial.print("Scan I2C GPIO21/22: ");
  int n = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.print("0x"); Serial.print(a, HEX); Serial.print(" "); n++; }
  }
  Serial.println(n ? "" : "tidak ada perangkat");
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
  if (shtMasukA.begin(0x45)) {
    adaMasuk = true; shtMasuk = &shtMasukA;
    Serial.println("SHT3X udara masuk: GPIO21/22 alamat 0x45 (pin AD ke 3V3)");
  } else if (shtMasukB.begin(0x44)) {
    adaMasuk = true; shtMasuk = &shtMasukB;
    Serial.println("SHT3X udara masuk: GPIO19/18 alamat 0x44");
  }
  scanI2C();
  adaHX = scale.wait_ready_timeout(1000);
  Serial.println(adaDalam ? "[OK] SHT3X ruang dalam" : "[--] SHT3X ruang dalam tidak terpasang");
  Serial.println(adaMasuk ? "[OK] SHT3X udara masuk" : "[--] SHT3X udara masuk tidak terpasang");
  Serial.println(adaHX ? "[OK] HX711" : "[--] HX711 tidak terpasang");
  Serial.println(bacaCO2() >= 0 ? "[OK] MH-Z19C" : "[--] MH-Z19C tidak menjawab (cek Vin 5V, TX->D4, RX->D15)");

  memori.begin("grainsense", false);
  if (adaHX) {
    // Pakai titik nol yang tersimpan supaya stok di silo tidak ikut di-nol-kan saat reset
    if (memori.isKey("offset")) {
      scale.set_offset(memori.getLong("offset"));
      Serial.println("Load cell: titik nol (tare) dari memori.");
    } else {
      Serial.println("Load cell: tare pertama, pastikan wadah kosong...");
      scale.tare(20);
      memori.putLong("offset", scale.get_offset());
    }
    if (FAKTOR_HX == 0) FAKTOR_HX = memori.getFloat("faktor", 0);
    if (FAKTOR_HX != 0) {
      scale.set_scale(FAKTOR_HX);
      Serial.print("Load cell: faktor kalibrasi ");
      Serial.println(FAKTOR_HX, 4);
    } else {
      Serial.println("Load cell belum dikalibrasi: taruh benda yang beratnya diketahui, lalu ketik beratnya (gram), mis. 500.");
    }
  }

  mulaiWiFi();
  waktuMulai = millis();
  waktuKirim = millis() - INTERVAL_KIRIM;  // langsung kirim sekali
}

// ---------- Loop ----------
void loop() {
  if (Serial.available()) prosesPerintah(Serial.readStringUntil('\n'));
  jagaWiFi();
  if (millis() - waktuKirim >= INTERVAL_KIRIM) {
    waktuKirim = millis();
    bacaDanKirim();
  }
}
