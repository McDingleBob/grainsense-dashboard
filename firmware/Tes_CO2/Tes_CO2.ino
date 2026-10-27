// Tes MH-Z19C saja. Tanpa library.
// Wiring: Vin -> 5V, GND -> GND, TX sensor -> D4, RX sensor -> D15
// Serial Monitor 115200, New Line.
// Perintah: zero = kalibrasi nol (HANYA di udara luar, sudah menyala 20 menit)
//           abc1 = auto-kalibrasi nyala | abc0 = auto-kalibrasi mati

const int CO2_RX = 4;   // ke TX sensor
const int CO2_TX = 15;  // ke RX sensor
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

// Mengisi ppm dan suhu internal sensor. false kalau tidak ada jawaban.
bool bacaCO2(int &ppm, int &suhu) {
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
  suhu = r[4] - 40;
  return true;
}

void setup() {
  Serial.begin(115200);
  co2Serial.begin(9600, SERIAL_8N1, CO2_RX, CO2_TX);
  delay(1000);
  Serial.println();
  Serial.println("=== TES MH-Z19C ===");
  Serial.println("Pemanasan sekitar 3 menit sebelum angka stabil.");
  Serial.println("Perintah: zero | abc1 | abc0");
  waktuMulai = millis();
}

void loop() {
  if (Serial.available()) {
    String in = Serial.readStringUntil('\n');
    in.trim();
    if (in == "zero") {
      unsigned long menit = (millis() - waktuMulai) / 60000UL;
      if (menit < 20) {
        Serial.print(">> Baru menyala ");
        Serial.print(menit);
        Serial.println(" menit. Tunggu 20 menit DI LUAR RUANGAN, lalu ketik zero lagi.");
      } else {
        kirim(0x87, 0x00);
        Serial.println(">> Kalibrasi nol dikirim. Titik sekarang dianggap 400 ppm.");
      }
    } else if (in == "abc1") {
      kirim(0x79, 0xA0);
      Serial.println(">> Auto-kalibrasi NYALA. Biarkan menyala terus ~24 jam.");
    } else if (in == "abc0") {
      kirim(0x79, 0x00);
      Serial.println(">> Auto-kalibrasi MATI.");
    } else if (in.length() > 0) {
      Serial.println(">> Perintah tidak dikenal. Pakai: zero, abc1, abc0");
    }
  }

  if (millis() - waktuBaca >= 2000) {
    waktuBaca = millis();
    int ppm, suhu;
    unsigned long detik = (millis() - waktuMulai) / 1000;
    Serial.print("[");
    Serial.print(detik);
    Serial.print(" s] ");
    if (bacaCO2(ppm, suhu)) {
      Serial.print("CO2: ");
      Serial.print(ppm);
      Serial.print(" ppm | suhu sensor: ");
      Serial.print(suhu);
      Serial.print(" C");
      if (detik < 180) Serial.print("  (pemanasan)");
      Serial.println();
    } else {
      Serial.println("Tidak ada jawaban dari MH-Z19C. Cek Vin 5V, GND, TX->D4, RX->D15.");
    }
  }
}
