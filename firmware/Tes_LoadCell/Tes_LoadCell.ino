// Tes load cell + HX711. Library: "HX711" by Bogdan Necula.
// Wiring HX711 : VCC -> 3V3, GND -> GND, DT -> D16 (RX2), SCK -> D17 (TX2)
// Wiring load cell ke HX711: merah -> E+, hitam -> E-, putih -> A-, hijau -> A+
// Serial Monitor 115200. Tiap 0,5 detik tampil angka mentah.
// Cara tes: biarkan kosong dulu, lalu tekan/taruh benda di load cell dan lihat angkanya.

#include "HX711.h"

const int DT_PIN  = 16;
const int SCK_PIN = 17;

HX711 scale;
long nolAwal = 0;
bool adaNol = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== TES LOAD CELL / HX711 ===");
  scale.begin(DT_PIN, SCK_PIN);
}

void loop() {
  if (!scale.wait_ready_timeout(1000)) {
    Serial.println("[GAGAL] HX711 tidak menjawab. Cek VCC 3V3, GND, DT -> D16, SCK -> D17.");
    delay(1000);
    return;
  }

  long mentah = scale.read_average(5);
  if (!adaNol) { nolAwal = mentah; adaNol = true; }
  long beda = mentah - nolAwal;

  Serial.print("Mentah: ");
  Serial.print(mentah);
  Serial.print(" | beda dari awal: ");
  Serial.print(beda);

  // Petunjuk dari angka yang terbaca
  if (mentah == 0 || mentah == -1) Serial.print("  <- mencurigakan, cek kabel DT/SCK");
  else if (mentah >= 8388000 || mentah <= -8388000) Serial.print("  <- mentok, cek kabel E+/E-/A+/A- load cell");
  else if (abs(beda) > 1000) Serial.print("  <- load cell MERESPONS");
  Serial.println();
  delay(500);
}
