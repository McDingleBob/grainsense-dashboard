// ===== Pengaturan dashboard GrainSense =====
// Isi dengan data channel ThingSpeak kamu (menu Channels > My Channels).
// Kalau CHANNEL_ID dibiarkan kosong, dashboard tampil dengan data contoh (mode demo).
window.GRAINSENSE = {
  CHANNEL_ID: "",     // contoh: "2712345"
  READ_API_KEY: "",   // kosongkan kalau channel di-set Public
  REFRESH_DETIK: 20   // seberapa sering dashboard mengambil data baru
};
