# Dashboard GrainSense

Dashboard online untuk prototipe GrainSense: CO₂, kadar air, berat stok, suhu dan kelembapan ruang dalam serta udara masuk, dan PWM blower.

Buka: https://mcdinglebob.github.io/grainsense-dashboard/

## Sumber data (atur di `config.js`)

- `MODE: "mqtt"`: ESP32 mengirim JSON tiap 10 detik ke broker publik `broker.hivemq.com`, topik `grainsense/mcdinglebob/silo1`. Tidak perlu akun. Riwayat grafik disimpan di browser yang membuka dashboard (maksimal 24 jam).
- `MODE: "thingspeak"` (aktif sekarang, channel 3552694): ESP32 mengirim tiap 20 detik. Riwayat disimpan di server ThingSpeak.
- `MODE: "demo"` atau `?demo=1`: data contoh.

Broker publik bisa dibaca siapa saja yang tahu topiknya, jadi jangan kirim data rahasia. Jangan simpan Write API Key ThingSpeak di repo ini.
