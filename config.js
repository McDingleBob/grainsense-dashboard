// ===== Pengaturan dashboard GrainSense =====
// MODE:
//   "mqtt"       = data live dari broker MQTT publik (tanpa daftar akun).
//                  Riwayat grafik disimpan di browser yang membuka dashboard (maks 24 jam).
//   "thingspeak" = data dari channel ThingSpeak (riwayat tersimpan di server).
//   "demo"       = data contoh.
window.GRAINSENSE = {
  MODE: "mqtt",

  // MQTT: harus sama persis dengan MQTT_TOPIC di firmware GrainSense_MQTT.ino
  MQTT_URL: "wss://broker.hivemq.com:8884/mqtt",
  MQTT_TOPIC: "grainsense/mcdinglebob/silo1",

  // ThingSpeak (dipakai kalau MODE: "thingspeak")
  CHANNEL_ID: "",     // contoh: "2712345"
  READ_API_KEY: "",   // kosongkan kalau channel di-set Public

  REFRESH_DETIK: 20   // seberapa sering tampilan diperbarui
};
