/*
  GrainSense - Sketch tes semua modul
  Board : DOIT ESP32 DEVKIT V1 (ESP32 Arduino core 2.x atau 3.x)
  Library (install lewat Library Manager):
    - "HX711" by Bogdan Necula
    - "Adafruit SHT31 Library" by Adafruit (pilih INSTALL ALL)
  MH-Z19C dibaca langsung lewat UART, tidak perlu library.

  Serial Monitor: 115200 baud, line ending "New Line".
  Perintah:
    500      kalibrasi load cell dengan benda 500 g (angka = berat dalam gram)
    t        tare (nol-kan) load cell
    f-21.5   pakai faktor kalibrasi yang sudah dicatat (contoh -21.5)
    p50      PWM blower 50% (0-100)
    +  / -   PWM naik / turun 10%
    s        sweep PWM 0 -> 100 -> 0
    i        cek ulang semua modul
    zero     kalibrasi nol MH-Z19C (= 400 ppm), HANYA di udara luar
             setelah sensor menyala minimal 20 menit (zero! = paksa)
    ?        tampilkan daftar perintah
*/

#include <Wire.h>
#include "HX711.h"
#include "Adafruit_SHT31.h"

// ===== Pin (sesuai Wiring GrainSense) =====
const int DT_PIN    = 16;  // HX711 DT   -> RX2
const int SCK_PIN   = 17;  // HX711 SCK  -> TX2
const int SDA_PIN   = 21;  // SHT3X #1 SDA -> D21 (bus Wire)
const int SCL_PIN   = 22;  // SHT3X #1 SCL -> D22
const int SDA2_PIN  = 19;  // SHT3X #2 SDA -> D19 (bus Wire1)
const int SCL2_PIN  = 18;  // SHT3X #2 SCL -> D18
const int CO2_RX    = 4;   // ESP32 RX  <- TX MH-Z19C
const int CO2_TX    = 15;  // ESP32 TX  -> RX MH-Z19C
const int SOIL_PIN  = 34;  // AOUT soil moisture
const int PWM_PIN   = 26;  // PWM/TRIG modul MOSFET

// ===== PWM =====
const int PWM_FREQ = 1000;  // Hz
const int PWM_RES  = 8;     // 0..255
const int PWM_CH   = 0;     // hanya dipakai di core 2.x
int dutyPersen = 0;

// ===== MH-Z19C auto-kalibrasi (ABC) =====
// false = mati (sesuai proposal, untuk ruang simpan tertutup)
// true  = sensor membetulkan titik 400 ppm sendiri tiap ~24 jam,
//         memakai bacaan terendah selama sensor menyala terus
const bool CO2_ABC = false;

// ===== Soil moisture: isi setelah kalibrasi =====
// Catat nilai mentah saat sensor di udara (kering) dan di air (basah).
int SOIL_KERING = 3000;
int SOIL_BASAH  = 1300;

// ===== Objek sensor =====
HX711 scale;
// Modul SHT3X 4 pin tidak punya pin ADR, jadi keduanya beralamat 0x44.
// Karena itu tiap sensor memakai bus I2C sendiri.
Adafruit_SHT31 shtDalam(&Wire);   // 0x44 di GPIO21/22
Adafruit_SHT31 shtMasuk(&Wire1);  // 0x44 di GPIO19/18
HardwareSerial co2Serial(2);

bool adaHX = false, adaDalam = false, adaMasuk = false, adaCO2 = false;
bool sudahKalibrasi = false;
unsigned long waktuTampil = 0;
unsigned long waktuMulai = 0;

// ---------- PWM ----------
void setDuty(int persen) {
  persen = constrain(persen, 0, 100);
  dutyPersen = persen;
  int duty = map(persen, 0, 100, 0, (1 << PWM_RES) - 1);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PWM_PIN, duty);
#else
  ledcWrite(PWM_CH, duty);
#endif
  Serial.print(">> PWM blower: ");
  Serial.print(persen);
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
  while (co2Serial.available()) co2Serial.read();  // buang sisa data
  kirimCO2(0x86, 0x00);
  byte r[9];
  unsigned long t0 = millis();
  int n = 0;
  while (n < 9 && millis() - t0 < 300) {
    if (co2Serial.available()) {
      byte c = co2Serial.read();
      if (n == 0 && c != 0xFF) continue;  // tunggu byte awal
      r[n++] = c;
    }
  }
  if (n < 9 || r[1] != 0x86 || r[8] != checksumCO2(r)) return -1;
  return r[2] * 256 + r[3];
}

// ---------- Cek semua modul ----------
void scanI2C(TwoWire &bus, const char *nama) {
  Serial.print("Scan ");
  Serial.print(nama);
  Serial.print(": ");
  int jumlah = 0;
  for (byte addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Serial.print("0x");
      Serial.print(addr, HEX);
      Serial.print(" ");
      jumlah++;
    }
  }
  if (jumlah == 0) Serial.print("tidak ada perangkat (cek solderan & kabel SDA/SCL)");
  Serial.println();
}

void cekModul() {
  Serial.println();
  Serial.println("----- CEK MODUL -----");

  scanI2C(Wire, "I2C #1 (21/22)");
  scanI2C(Wire1, "I2C #2 (19/18)");
  adaDalam = shtDalam.begin(0x44);
  adaMasuk = shtMasuk.begin(0x44);
  Serial.println(adaDalam ? "[OK]    SHT3X #1 ruang dalam (0x44, GPIO21/22)" : "[GAGAL] SHT3X #1 ruang dalam (0x44, GPIO21/22)");
  Serial.println(adaMasuk ? "[OK]    SHT3X #2 udara masuk (0x44, GPIO19/18)" : "[GAGAL] SHT3X #2 udara masuk (0x44, GPIO19/18)");

  adaHX = scale.wait_ready_timeout(1000);
  Serial.println(adaHX ? "[OK]    HX711" : "[GAGAL] HX711 (cek DT=16/RX2, SCK=17/TX2, VCC 3V3)");

  adaCO2 = bacaCO2() >= 0;
  Serial.println(adaCO2 ? "[OK]    MH-Z19C" : "[GAGAL] MH-Z19C (cek Vin 5V, TX->D4, RX->D15)");

  int soil = analogRead(SOIL_PIN);
  Serial.print("[INFO]  Soil moisture mentah = ");
  Serial.print(soil);
  if (soil < 50 || soil > 4050) Serial.print("  (mencurigakan, cek kabel AOUT->D34)");
  Serial.println();

  Serial.println("[INFO]  PWM blower di D26, uji dengan perintah p50 atau s");
  Serial.println("---------------------");
}

void bantuan() {
  Serial.println("Perintah: 500 = kalibrasi 500 g | t = tare | f-21.5 = pakai faktor");
  Serial.println("          p50 = PWM 50% | + / - | s = sweep | i = cek modul | ? = bantuan");
  Serial.println("          zero = kalibrasi nol CO2 di udara luar (setelah menyala 20 menit)");
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===== GRAINSENSE - TES SEMUA MODUL =====");

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
  setDuty(0);  // blower mati saat mulai

  delay(1000);
  kirimCO2(0x79, CO2_ABC ? 0xA0 : 0x00);  // atur auto-kalibrasi (ABC) MH-Z19C
  Serial.println(CO2_ABC ? "MH-Z19C: auto-kalibrasi ABC NYALA" : "MH-Z19C: auto-kalibrasi ABC mati");
  cekModul();

  if (adaHX) {
    Serial.println("Kosongkan load cell, tare dalam 3 detik...");
    delay(3000);
    scale.tare(20);
    Serial.println("Tare selesai.");
  }
  Serial.println("Catatan: MH-Z19C butuh pemanasan sekitar 3 menit sebelum angkanya stabil.");
  bantuan();
  waktuMulai = millis();
}

// ---------- Perintah Serial ----------
void prosesPerintah(String in) {
  in.trim();
  if (in.length() == 0) return;
  char c = in.charAt(0);

  if (in == "?") {
    bantuan();
  } else if (in == "i" || in == "I") {
    cekModul();
  } else if (in == "t" || in == "T") {
    if (!adaHX) { Serial.println(">> HX711 tidak terdeteksi."); return; }
    scale.tare(20);
    Serial.println(">> Tare selesai.");
  } else if (c == 'p' || c == 'P') {
    setDuty(in.substring(1).toInt());
  } else if (in == "+") {
    setDuty(dutyPersen + 10);
  } else if (in == "-") {
    setDuty(dutyPersen - 10);
  } else if (in == "s" || in == "S") {
    for (int p = 0; p <= 100; p += 10) { setDuty(p); delay(300); }
    for (int p = 100; p >= 0; p -= 10) { setDuty(p); delay(300); }
  } else if (in == "zero" || in == "zero!") {
    unsigned long menit = (millis() - waktuMulai) / 60000UL;
    if (in == "zero" && menit < 20) {
      Serial.print(">> Sensor baru menyala ");
      Serial.print(menit);
      Serial.println(" menit. Tunggu sampai 20 menit di udara luar, lalu ketik zero lagi.");
      Serial.println(">> (Ketik zero! untuk memaksa, tapi hasilnya bisa kurang akurat.)");
      return;
    }
    kirimCO2(0x87, 0x00);  // zero point calibration: titik ini dianggap 400 ppm
    Serial.println(">> Kalibrasi nol CO2 dikirim. Titik sekarang dianggap 400 ppm.");
    Serial.println(">> Tunggu 1-2 menit, angka CO2 di udara luar seharusnya sekitar 400-450 ppm.");
  } else if (c == 'f' || c == 'F') {
    float f = in.substring(1).toFloat();
    if (f == 0) { Serial.println(">> Faktor tidak valid."); return; }
    scale.set_scale(f);
    sudahKalibrasi = true;
    Serial.print(">> Faktor dipakai: ");
    Serial.println(f, 4);
  } else {
    float beratAsli = in.toFloat();
    if (beratAsli <= 0) { Serial.println(">> Perintah tidak dikenal. Ketik ? untuk bantuan."); return; }
    if (!adaHX) { Serial.println(">> HX711 tidak terdeteksi."); return; }
    float nilai = scale.get_value(20);
    if (abs(nilai) < 1000) {
      Serial.println(">> Nilainya terlalu kecil. Bendanya sudah ditaruh belum?");
      return;
    }
    float faktor = nilai / beratAsli;
    scale.set_scale(faktor);
    sudahKalibrasi = true;
    Serial.print(">> FAKTOR KALIBRASI: ");
    Serial.println(faktor, 4);
    Serial.println(">> Catat angka ini! Lain kali cukup ketik f<angka>.");
  }
}

// ---------- Loop ----------
void loop() {
  if (Serial.available()) prosesPerintah(Serial.readStringUntil('\n'));

  if (millis() - waktuTampil < 2000) return;
  waktuTampil = millis();

  // Berat
  Serial.print("Berat: ");
  if (adaHX && scale.wait_ready_timeout(200)) {
    if (sudahKalibrasi) { Serial.print(scale.get_units(5), 1); Serial.print(" g"); }
    else { Serial.print(scale.get_value(5), 0); Serial.print(" (mentah)"); }
  } else {
    Serial.print("--");
  }

  // Suhu & kelembapan
  Serial.print(" | Dalam: ");
  if (adaDalam) {
    Serial.print(shtDalam.readTemperature(), 1); Serial.print("C ");
    Serial.print(shtDalam.readHumidity(), 0);    Serial.print("%");
  } else Serial.print("--");

  Serial.print(" | Masuk: ");
  if (adaMasuk) {
    Serial.print(shtMasuk.readTemperature(), 1); Serial.print("C ");
    Serial.print(shtMasuk.readHumidity(), 0);    Serial.print("%");
  } else Serial.print("--");

  // CO2
  Serial.print(" | CO2: ");
  int ppm = bacaCO2();
  if (ppm >= 0) {
    adaCO2 = true;
    Serial.print(ppm); Serial.print(" ppm");
    if (millis() - waktuMulai < 180000UL) Serial.print(" (pemanasan)");
  } else Serial.print("--");

  // Soil
  int soil = analogRead(SOIL_PIN);
  int persen = constrain(map(soil, SOIL_KERING, SOIL_BASAH, 0, 100), 0, 100);
  Serial.print(" | Soil: ");
  Serial.print(soil);
  Serial.print(" (~");
  Serial.print(persen);
  Serial.print("%)");

  // PWM
  Serial.print(" | PWM: ");
  Serial.print(dutyPersen);
  Serial.println("%");
}
