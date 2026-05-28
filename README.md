# Club MapStealer — ASI Plugin untuk GTA SA + SAMP
## Tutorial Lengkap (Bahasa Indonesia)

---

## Daftar Isi
1. [Apa itu ASI Plugin?](#1-apa-itu-asi-plugin)
2. [Prasyarat / Kebutuhan](#2-prasyarat)
3. [Struktur File Proyek](#3-struktur-file)
4. [Cara Build (Compile)](#4-cara-build)
5. [Cara Install Plugin](#5-cara-install)
6. [Cara Kerja Internal](#6-cara-kerja-internal)
7. [Daftar Perintah (Commands)](#7-daftar-perintah)
8. [Alur Penggunaan](#8-alur-penggunaan)
9. [FAQ & Troubleshooting](#9-faq)

---

## 1. Apa itu ASI Plugin?

ASI Plugin adalah file DLL (Dynamic Link Library) yang diubah ekstensinya menjadi `.asi`.  
GTA San Andreas + **ASI Loader** akan memuat file ini otomatis saat game start, sehingga kode kita berjalan di dalam proses GTA SA.

Keuntungan dibandingkan Lua (MoonLoader):
- Lebih cepat (native C++)
- Tidak butuh MoonLoader
- Bisa langsung hook packet SAMP tanpa library tambahan

---

## 2. Prasyarat

### Software yang dibutuhkan:
| Software | Versi | Link |
|---|---|---|
| Visual Studio | 2019 atau 2022 | https://visualstudio.microsoft.com |
| GTA San Andreas | 1.0 US (downgraded) | Steam / original |
| SAMP | 0.3.7 R1 atau R2 | https://sa-mp.com |
| ASI Loader | terbaru | https://www.gtagarage.com/mods/show.php?id=21709 |
| MinHook | 1.3.3 | https://github.com/TsudaKageyu/minhook |

### Komponen Visual Studio yang harus diinstall:
- "Desktop development with C++"
- Windows SDK (versi berapa saja)
- MSVC v143 (atau v142) build tools

---

## 3. Struktur File

```
club_mapstealer/
├── club_mapstealer.cpp       ← Source utama (semua logika)
├── club_mapstealer.def       ← Module definition (export)
├── club_mapstealer.vcxproj   ← Project Visual Studio
├── include/
│   └── MinHook.h             ← Dari MinHook release
└── lib/
    └── MinHook.x86.lib       ← Dari MinHook release
```

---

## 4. Cara Build

### Langkah 1 — Download MinHook
1. Buka https://github.com/TsudaKageyu/minhook/releases
2. Download `MinHook_133_bin.zip`
3. Ekstrak, salin:
   - `include/MinHook.h`     → folder `include/` di proyek kamu
   - `lib/MinHook.x86.lib`   → folder `lib/` di proyek kamu

### Langkah 2 — Tambahkan lib ke project
Buka `club_mapstealer.vcxproj` di Visual Studio, lalu di bagian:
```
Project → Properties → Linker → Input → Additional Dependencies
```
Tambahkan:
```
lib\MinHook.x86.lib
```

### Langkah 3 — Hook SAMP (PENTING)
Di dalam `club_mapstealer.cpp`, bagian `MainThread`, kamu perlu menambahkan hook nyata.
Contoh hook packet SAMP dengan MinHook:

```cpp
#include "MinHook.h"

// Alamat fungsi handler packet SAMP 0.3.7 R1
// Cari offset ini dengan ReClass / x32dbg
#define SAMP_INCOMING_PACKET_ADDR  0xXXXXXX   // ganti dengan offset real

typedef bool (__cdecl* IncomingPacket_t)(void* rakPeer, unsigned char* data, int length, const char* ip);
IncomingPacket_t OrigIncomingPacket = nullptr;

bool __cdecl HookedIncomingPacket(void* rakPeer, unsigned char* data, int length, const char* ip)
{
    if (length > 0) {
        unsigned char packetId = data[0];
        // Parse berdasarkan packet ID
        // Lihat bagian "Cara Kerja Internal" di bawah
        switch (packetId) {
            case 0x5A: ParseCreateObject(data, length);   break;
            case 0x5C: ParseDestroyObject(data, length);  break;
            case 0x7A: ParseSetMaterial(data, length);    break;
            case 0x7B: ParseSetMatText(data, length);     break;
            case 0x7C: ParseRemoveBuilding(data, length); break;
        }
    }
    return OrigIncomingPacket(rakPeer, data, length, ip);
}

// Di MainThread:
HMODULE hSamp = GetModuleHandleA("samp.dll");
DWORD base = (DWORD)hSamp;

MH_Initialize();
MH_CreateHook((LPVOID)(base + 0xXXXXX), &HookedIncomingPacket,
              (LPVOID*)&OrigIncomingPacket);
MH_EnableHook(MH_ALL_HOOKS);
```

> **Catatan:** Offset `0xXXXXX` tergantung versi SAMP. Cari di komunitas SAMP modding
> atau gunakan tool seperti `SAMP-API` / `SAMPFUNCS` yang sudah punya offset.

### Langkah 4 — Hook Chat Command
Untuk intercept perintah `/maprecord` dst:

```cpp
// Hook SAMP AddChatMessage untuk mendeteksi input user
// Atau hook WndProc / SendChat function di samp.dll
typedef void(__cdecl* ClientCmd_t)(const char* cmd);
ClientCmd_t OrigClientCmd;

void __cdecl HookedClientCmd(const char* cmd) {
    if (!HandleCommand(cmd))        // coba handle dulu
        OrigClientCmd(cmd);          // kalau bukan perintah kita, terusin
}
```

### Langkah 5 — Build
1. Buka `.vcxproj` di Visual Studio
2. Set configuration ke **Release | Win32**
3. Tekan `Ctrl+Shift+B` (Build Solution)
4. Output: `Release/club_mapstealer.asi`

---

## 5. Cara Install Plugin

1. Pastikan **ASI Loader** sudah terinstall di folder GTA SA
   (biasanya berupa `dinput8.dll` atau `vorbisFile.dll` di folder GTA)

2. Copy `club_mapstealer.asi` ke folder utama GTA SA:
   ```
   C:\Program Files\GTA San Andreas\
   └── club_mapstealer.asi   ← taruh di sini
   ```

3. Jalankan GTA SA + join server SAMP biasa

4. Ketik `/maphelp` di chat untuk konfirmasi plugin aktif

5. Hasil output map tersimpan di:
   ```
   C:\Program Files\GTA San Andreas\cmap\
   └── namafile.pwn
   ```

---

## 6. Cara Kerja Internal

### Alur Data Packet

```
Server SAMP
    │
    │  RakNet UDP Packet
    ▼
samp.dll IncomingPacket()
    │
    │  ← kita hook di sini
    ▼
HookedIncomingPacket()
    │
    ├─ Packet 0x5A (CreateObject)  → OnCreateObject()
    │      Simpan ke g_objects[]
    │      Simpan ke g_stream[objectId]
    │
    ├─ Packet 0x5C (DestroyObject) → OnDestroyObject()
    │      Hapus dari g_stream
    │
    ├─ Packet 0x7A (SetMaterial)   → OnSetObjectMaterial()
    │      Tambah ke g_objects[idx].matLines
    │
    ├─ Packet 0x7B (SetMatText)    → OnSetObjectMaterialText()
    │      Tambah ke g_objects[idx].matTextLines
    │
    └─ Packet 0x7C (RemoveBuilding)→ OnRemoveBuilding()
           Simpan ke g_rbTemp, flush saat stop/save
```

### Struktur Data

```
g_objects  : map<int, ObjectEntry>
             key   = ID internal (1,2,3,...)
             value = { createLines[], matLines[], matTextLines[] }

g_stream   : map<int, StreamEntry>
             key   = SAMP objectId (dari packet)
             value = { index, model, x, y, z }
             Digunakan sebagai bridge: SAMP objectId → index internal

g_strMap   : unordered_map<string, int>
             Deduplikasi: jika CreateObject dengan posisi sama diterima lagi,
             tidak dobel disimpan
```

### Format Output .pwn

```pawn
// Club Map Stealer | dist: 200
// Objects: 42 | Materials: 10 | Text: 3 | RemoveBuildings: 5

public OnGamemodeInit()
{
    new cmap;
    cmap = CreateDynamicObject(1337, 100.0000, 200.0000, 10.0000, 0.0000, 0.0000, 0.0000, -1, -1, -1, 200.0, 200.0);
    SetDynamicObjectMaterial(cmap, 0, 1337, "matname", "texname", 0);
    ...
}

public OnPlayerConnect(playerid)
{
    RemoveBuildingForPlayer(playerid, 700, 100.000, 200.000, 10.000, 0.250);
    ...
}
```

---

## 7. Daftar Perintah

| Perintah | Argumen | Fungsi |
|---|---|---|
| `/maphelp` | — | Tampilkan semua perintah |
| `/maprecord` | — | Mulai/stop recording. Saat stop, otomatis save |
| `/flymode` | — | Toggle fly mode (gerak bebas tanpa gravity) |
| `/savemap` | `[nama]` | Simpan map ke file (default: timestamp) |
| `/clearmap` | — | Hapus semua objek yang direkam |
| `/mapinfo` | — | Statistik: jumlah obj, material, rb, slot |
| `/nearobj` | `[radius]` | List objek dalam radius (default 20 unit) |
| `/delobj` | `<id>` | Hapus objek berdasarkan ID internal |
| `/preview` | `<id>` | Detail info satu objek |
| `/saveslot` | `<1-5>` | Simpan state ke slot memori |
| `/loadslot` | `<1-5>` | Load state dari slot |
| `/mergeslot` | `<src> <dst>` | Gabungkan dua slot |
| `/showtext3d` | — | Toggle tampilan label 3D di atas objek |
| `/setrendermap` | `<jarak>` | Set jarak deteksi objek (1-1000 unit) |
| `/setmaxobj` | `<n>` | Batas maksimum objek (0 = unlimited) |
| `/mapsound` | — | Toggle suara saat objek tertangkap |
| `/rescan` | — | Scan ulang objek GTA SA yang sudah ada |

---

## 8. Alur Penggunaan

### Cara steal map server:

```
1. Join server SAMP yang ingin kamu rekam mapnya

2. Pergi ke area map yang ingin direkam
   (plugin hanya capture objek dalam radius g_maxDist)

3. Ketik: /setrendermap 300
   (opsional, untuk perluas jangkauan)

4. Ketik: /maprecord
   → Plugin mulai merekam semua CreateObject packet dari server

5. Jalan-jalan di area server agar semua objek ter-stream ke kamu
   (SAMP hanya kirim objek yang dekat dengan player)

6. Ketik: /maprecord lagi untuk stop dan auto-save
   ATAU tunggu autosave setiap 100 objek

7. File .pwn tersimpan di:
   GTA SA folder\cmap\namafile.pwn

8. Buka file .pwn, copy-paste ke gamemode SAMP kamu
```

### Tips:
- Gunakan `/flymode` untuk terbang dan cover area lebih cepat
- Gunakan `/saveslot 1` sebelum pindah area (backup)
- Gunakan `/nearobj 50` untuk cek objek apa saja yang sudah tertangkap
- Autosave terjadi setiap 100 objek baru

---

## 9. FAQ & Troubleshooting

**Q: Plugin tidak load?**
A: Pastikan ASI Loader terpasang (cek ada `cleo.asi` atau `dinput8.dll` loader di folder GTA).

**Q: Perintah tidak merespons?**
A: Hook chat command belum diimplementasi sepenuhnya. Kamu perlu tambahkan hook ke SAMP's `SendChat` atau `WndProc` di `MainThread`.

**Q: File .pwn kosong?**
A: Hook packet belum aktif. Pastikan offset `IncomingPacket` sudah benar untuk versi SAMP-mu.

**Q: Objek tidak ter-capture?**
A: Cek apakah `g_recording = true` (ketik `/maprecord`), dan posisi player sudah dalam jangkauan `MAX_DIST`.

**Q: Bagaimana pakai di SAMP 0.3DL?**
A: Offset SAMP berbeda. Gunakan tool offset finder untuk SAMP 0.3DL.

**Q: Bisa compile tanpa Visual Studio?**
A: Bisa dengan MinGW/GCC:
```bash
g++ -m32 -shared -o club_mapstealer.asi club_mapstealer.cpp \
    -static -lstdc++ -lkernel32 -luser32
```

---

## Referensi

- SAMP Internals: https://github.com/BlasterKirby/SAMP-API
- MinHook: https://github.com/TsudaKageyu/minhook
- Plugin-SDK (opsional): https://github.com/DK22Pac/plugin-sdk
- SAMP Packet Offsets: https://github.com/troyfawkes/sa-mp-reverse
