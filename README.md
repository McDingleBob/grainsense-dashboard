# Dashboard GrainSense

Dashboard online untuk prototipe GrainSense: CO₂, kadar air, berat stok, suhu dan kelembapan ruang dalam serta udara masuk, dan PWM blower.

ESP32 mengirim data ke ThingSpeak tiap 20 detik, lalu halaman ini membacanya dan diperbarui otomatis.

- Atur Channel ID di `config.js`. Kalau dibiarkan kosong, dashboard tampil dalam mode demo.
- Bisa juga lewat alamat: `?channel=ID_CHANNEL`.
- Jangan simpan Write API Key di repo ini. Kunci itu hanya untuk firmware ESP32.
