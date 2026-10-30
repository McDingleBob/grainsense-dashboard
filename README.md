# Dashboard GrainSense

Dashboard online untuk prototipe GrainSense: CO₂, kadar air, berat stok, suhu dan kelembapan ruang dalam serta udara masuk, dan PWM blower.

Buka: https://mcdinglebob.github.io/grainsense-dashboard/

## Sumber data (atur di `config.js`)

- `MODE: "mqtt"` (aktif sekarang): ESP32 (sketch `GrainSense_MQTT`) mengirim JSON tiap 5 detik ke broker publik `broker.hivemq.com`, topik `grainsense/mcdinglebob/silo1`. Tidak perlu akun. Riwayat grafik disimpan di browser yang membuka dashboard (maksimal 24 jam).
- `MODE: "thingspeak"` (channel 3552694): ESP32 mengirim tiap 20 detik pakai `GrainSense_IoT`. Riwayat disimpan di server ThingSpeak.
- `MODE: "demo"` atau `?demo=1`: data contoh.

Broker publik bisa dibaca siapa saja yang tahu topiknya, jadi jangan kirim data rahasia. Jangan simpan Write API Key ThingSpeak di repo ini.

## Firmware ESP32 (folder `firmware/`)

| Sketch | Fungsi |
|---|---|
| `GrainSense_MQTT` | Firmware utama (aktif): baca semua sensor, kirim lewat MQTT tiap 5 detik |
| `GrainSense_IoT` | Alternatif: kirim ke ThingSpeak lewat HTTPS tiap 20 detik |
| `GrainSense_Tes` | Tes semua modul lewat Serial Monitor |
| `Tes_CO2`, `Tes_CO2_Soil` | Tes MH-Z19C dan soil moisture |
| `Tes_LoadCell` | Tes load cell + HX711 (angka mentah) |

Board: DOIT ESP32 DEVKIT V1. Library: HX711 (Bogdan Necula), Adafruit SHT31, dan PubSubClient untuk versi MQTT.

Sebelum upload, isi `WIFI_SSID` dan `WIFI_PASSWORD` (dan `TS_WRITE_KEY` untuk versi ThingSpeak). Nilai-nilai itu sengaja dikosongkan di repo ini.
