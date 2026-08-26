# Club Mapstealer

> ASI plugin for GTA San Andreas + SAMP — intercepts object packets from a server in real-time and exports them to a ready-to-use `.pwn` file.

![Platform](https://img.shields.io/badge/platform-GTA%20SA%20%2B%20SAMP-orange)
![Language](https://img.shields.io/badge/language-C%2B%2B17-blue)
![SAMP](https://img.shields.io/badge/SAMP-0.3.7%20R1%2FR2-green)
![Build](https://img.shields.io/badge/build-MSVC%20%7C%20MinGW-lightgrey)

---

## How It Works

The plugin is loaded automatically by ASI Loader when GTA SA starts. It hooks `IncomingPacket` inside `samp.dll` using MinHook, intercepting every `CreateObject`, `SetMaterial`, `SetMaterialText`, and `RemoveBuilding` packet sent by the server and storing them in memory. When recording is stopped, all data is flushed to a `.pwn` file.

```
Server SAMP
    │  RakNet UDP
    ▼
samp.dll → IncomingPacket()
    │  ← hooked here
    ▼
HookedIncomingPacket()
    ├── 0x5A  CreateObject    → store in g_objects[]
    ├── 0x5C  DestroyObject   → remove from g_stream[]
    ├── 0x7A  SetMaterial     → append to object entry
    ├── 0x7B  SetMaterialText → append to object entry
    └── 0x7C  RemoveBuilding  → store in g_rbTemp[]
```

---

## Prerequisites

| Software | Version |
|---|---|
| Visual Studio | 2019 / 2022 |
| GTA San Andreas | 1.0 US (downgraded) |
| SAMP | 0.3.7 R1 or R2 |
| ASI Loader | latest |
| MinHook | 1.3.3 |

**Required Visual Studio components:**
- Desktop development with C++
- Windows SDK (any version)
- MSVC v143 or v142 build tools

---

## Project Structure

```
club_mapstealer/
├── club_mapstealer.cpp       # main source
├── club_mapstealer.def       # module definition (export)
├── club_mapstealer.vcxproj   # Visual Studio project
├── include/
│   └── MinHook.h
└── lib/
    └── MinHook.x86.lib
```

---

## Build

### 1. Download MinHook

Download [`MinHook_133_bin.zip`](https://github.com/TsudaKageyu/minhook/releases) and copy:

```
MinHook.h        → include/MinHook.h
MinHook.x86.lib  → lib/MinHook.x86.lib
```

### 2. Add the dependency to the project

```
Project → Properties → Linker → Input → Additional Dependencies
```

Add: `lib\MinHook.x86.lib`

### 3. Hook IncomingPacket

Inside `MainThread`, hook `samp.dll`:

```cpp
#include "MinHook.h"

typedef bool (__cdecl* IncomingPacket_t)(void* rakPeer, unsigned char* data, int length, const char* ip);
IncomingPacket_t OrigIncomingPacket = nullptr;

bool __cdecl HookedIncomingPacket(void* rakPeer, unsigned char* data, int length, const char* ip)
{
    if (length > 0) {
        switch (data[0]) {
            case 0x5A: ParseCreateObject(data, length);   break;
            case 0x5C: ParseDestroyObject(data, length);  break;
            case 0x7A: ParseSetMaterial(data, length);    break;
            case 0x7B: ParseSetMatText(data, length);     break;
            case 0x7C: ParseRemoveBuilding(data, length); break;
        }
    }
    return OrigIncomingPacket(rakPeer, data, length, ip);
}

// In MainThread:
HMODULE hSamp = GetModuleHandleA("samp.dll");
DWORD base = (DWORD)hSamp;

MH_Initialize();
MH_CreateHook((LPVOID)(base + 0xXXXXX), &HookedIncomingPacket, (LPVOID*)&OrigIncomingPacket);
MH_EnableHook(MH_ALL_HOOKS);
```

> **Note:** The offset `0xXXXXX` differs per SAMP version. Find it using ReClass / x32dbg or refer to [sa-mp-reverse](https://github.com/troyfawkes/sa-mp-reverse).

### 4. Hook chat commands

```cpp
typedef void(__cdecl* ClientCmd_t)(const char* cmd);
ClientCmd_t OrigClientCmd;

void __cdecl HookedClientCmd(const char* cmd) {
    if (!HandleCommand(cmd))
        OrigClientCmd(cmd);
}
```

### 5. Compile

Set configuration to **Release | Win32**, then:

```
Ctrl+Shift+B
```

Output: `Release/club_mapstealer.asi`

Alternatively with MinGW:

```bash
g++ -m32 -shared -o club_mapstealer.asi club_mapstealer.cpp \
    -static -lstdc++ -lkernel32 -luser32
```

---

## Installation

1. Make sure ASI Loader is installed in your GTA SA folder (`dinput8.dll` or `vorbisFile.dll`)
2. Copy `club_mapstealer.asi` to the root GTA SA folder
3. Launch GTA SA and join a SAMP server
4. Type `/maphelp` in chat to confirm the plugin is active

Output is saved to: `<GTA SA folder>\cmap\`

---

## Usage

```
1. Join the server whose map you want to record

2. Move to the target area
   (the plugin only captures objects within g_maxDist radius)

3. Optional: extend the capture range
   /setrendermap 300

4. Start recording
   /maprecord

5. Walk around the server area
   SAMP only streams objects close to the player's position

6. Stop and save
   /maprecord  (or wait for autosave every 100 objects)

7. Open the .pwn file at: GTA SA folder\cmap\filename.pwn
```

**Tips:**
- Use `/flymode` to cover the area faster
- Use `/saveslot 1` before moving to a new area as a backup
- Use `/nearobj 50` to check which objects have been captured

---

## Commands

| Command | Arguments | Description |
|---|---|---|
| `/maphelp` | — | Show all available commands |
| `/maprecord` | — | Toggle recording; auto-saves on stop |
| `/flymode` | — | Toggle fly mode (no gravity) |
| `/savemap` | `[name]` | Save to file (default: timestamp) |
| `/clearmap` | — | Clear all recorded objects |
| `/mapinfo` | — | Stats: object count, materials, rb, slots |
| `/nearobj` | `[radius]` | List objects within radius (default 20 units) |
| `/delobj` | `<id>` | Delete an object by internal ID |
| `/preview` | `<id>` | Show detailed info for one object |
| `/saveslot` | `<1-5>` | Save current state to a memory slot |
| `/loadslot` | `<1-5>` | Load state from a slot |
| `/mergeslot` | `<src> <dst>` | Merge two slots |
| `/showtext3d` | — | Toggle 3D labels above objects |
| `/setrendermap` | `<dist>` | Set object detection range (1–1000) |
| `/setmaxobj` | `<n>` | Maximum object limit (0 = unlimited) |
| `/mapsound` | — | Toggle sound when an object is captured |
| `/rescan` | — | Re-scan existing GTA SA objects |

---

## Output Format

```pawn
// Club Map Stealer | dist: 200
// Objects: 42 | Materials: 10 | Text: 3 | RemoveBuildings: 5

public OnGamemodeInit()
{
    new cmap;
    cmap = CreateDynamicObject(1337, 100.0000, 200.0000, 10.0000, 0.0000, 0.0000, 0.0000, -1, -1, -1, 200.0, 200.0);
    SetDynamicObjectMaterial(cmap, 0, 1337, "matname", "texname", 0);
}

public OnPlayerConnect(playerid)
{
    RemoveBuildingForPlayer(playerid, 700, 100.000, 200.000, 10.000, 0.250);
}
```

---

## Troubleshooting

**Plugin doesn't load**
Make sure ASI Loader is installed — check for `dinput8.dll` or another loader file in your GTA SA folder.

**Commands don't respond**
The chat command hook isn't fully implemented. Add a hook to `SendChat` or `WndProc` inside `MainThread`.

**Empty .pwn file**
The packet hook isn't active. Make sure the `IncomingPacket` offset is correct for your SAMP version.

**Objects not being captured**
Verify `g_recording = true` (type `/maprecord`) and that your player position is within `MAX_DIST`.

**SAMP 0.3DL**
Offsets differ from 0.3.7. Use an offset finder tool for the correct version.

---

## References

- [SAMP-API](https://github.com/BlasterKirby/SAMP-API) — SAMP internals
- [MinHook](https://github.com/TsudaKageyu/minhook) — hooking library
- [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) — GTA SA plugin SDK (optional)
- [sa-mp-reverse](https://github.com/troyfawkes/sa-mp-reverse) — SAMP packet offsets
