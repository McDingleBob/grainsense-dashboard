// Tes MH-Z19C + Soil Moisture v1.2. Tanpa library.
// Wiring MH-Z19C : Vin -> 5V, GND -> GND, TX sensor -> D4, RX sensor -> D15
// Wiring Soil    : VCC -> 3V3, GND -> GND, AOUT -> D34
// Serial Monitor 115200, New Line.
// Perintah: abc1 / abc0 = auto-kalibrasi CO2 nyala / mati
//           zero   = kalibrasi nol CO2 (HANYA di luar ruangan, setelah 20 menit)

const int CO2_RX = 4;     // ke TX sensor
const int CO2_TX = 15;    // ke RX sensor
const int SOIL_PIN = 34;  // ke AOUT sensor

HardwareSerial co2Serial(2);
unsigned long waktuMulai = 0;
unsigned long waktuBaca = 0;

byte checksum(const byte *b) {
  byte s = 0;
  for (int i = 1; i < 8; i++) s += b[i];
  return 0xFF - s + 1;
}

void kirim(byte cmd, byte b3) {
  byte pkt[9] = {0xFF, 0x01, cmd, b3, 0, 0, 0, 0, 0};
  pkt[8] = checksum(pkt);
  co2Serial.write(pkt, 9);
}

bool bacaCO2(int &ppm) {
  while (co2Serial.available()) co2Serial.read();
  kirim(0x86, 0x00);
  byte r[9];
  int n = 0;
  unsigned long t0 = millis();
  while (n < 9 && millis() - t0 < 300) {
    if (co2Serial.available()) {
      byte c = co2Serial.read();
      if (n == 0 && c != 0xFF) continue;
      r[n++] = c;
    }
  }
  if (n < 9 || r[1] != 0x86 || r[8] != checksum(r)) return false;
  ppm = r[2] * 256 + r[3];
  return true;
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

void setup() {
  Serial.begin(115200);
  co2Serial.begin(9600, SERIAL_8N1, CO2_RX, CO2_TX);
  analogReadResolution(12);  // 0..4095
  delay(1000);
  Serial.println();
  Serial.println("=== TES CO2 + SOIL MOISTURE ===");
  Serial.println("Perintah: abc1 | abc0 | zero");
  waktuMulai = millis();
}

void loop() {
  if (Serial.available()) {
    String in = Serial.readStringUntil('\n');
    in.trim();
    if (in == "abc1") {
      kirim(0x79, 0xA0);
      Serial.println(">> Auto-kalibrasi CO2 NYALA. Biarkan menyala terus ~24 jam.");
    } else if (in == "abc0") {
      kirim(0x79, 0x00);
      Serial.println(">> Auto-kalibrasi CO2 MATI.");
    } else if (in == "zero") {
      unsigned long menit = (millis() - waktuMulai) / 60000UL;
      if (menit < 20) {
        Serial.print(">> Baru menyala ");
        Serial.print(menit);
        Serial.println(" menit. Tunggu 20 menit DI LUAR RUANGAN, lalu ketik zero lagi.");
      } else {
        kirim(0x87, 0x00);
        Serial.println(">> Kalibrasi nol CO2 dikirim. Titik sekarang dianggap 400 ppm.");
      }
    } else if (in.length() > 0) {
      Serial.println(">> Perintah tidak dikenal. Pakai: abc1, abc0, zero");
    }
  }

  if (millis() - waktuBaca >= 2000) {
    waktuBaca = millis();
    unsigned long detik = (millis() - waktuMulai) / 1000;
    Serial.print("[");
    Serial.print(detik);
    Serial.print(" s] ");

    int ppm;
    if (bacaCO2(ppm)) {
      Serial.print("CO2: ");
      Serial.print(ppm);
      Serial.print(" ppm");
      if (detik < 180) Serial.print(" (pemanasan)");
    } else {
      Serial.print("CO2: tidak ada jawaban (cek Vin 5V, TX->D4, RX->D15)");
    }

    int soil = bacaSoil();
    Serial.print(" | Soil (analog 0-4095): ");
    Serial.print(soil);
    if (soil < 50) Serial.print(" (terlalu kecil, cek AOUT->D34 dan VCC)");
    if (soil > 4050) Serial.print(" (mentok atas, cek GND)");
    Serial.println();
  }
}
