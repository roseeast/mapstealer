/*
 *  Club MapStealer - ASI Plugin for GTA SA + SAMP
 *
 *  Requires:
 *    - SAMP 0.3.7 R1/R2
 *    - plugin-sdk / bass headers (optional for sound)
 *    - raknet hooks via SAMP internals
 *
 *  Build: VS2019+  |  Platform: Win32  |  /MT
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <direct.h>
#include <algorithm>

#define SAMP_INFO_OFFSET            0x21A0F8
#define SAMP_RAKPEER_OFFSET         0x4

typedef void(__cdecl* AddChatMsg_t)(const char* msg, DWORD color);
AddChatMsg_t AddChatMsg = nullptr;

struct CVector { float x, y, z; };

inline DWORD GetPlayerPed()
{
    DWORD* pp = reinterpret_cast<DWORD*>(0xB6F5F0);
    return pp ? *pp : 0;
}

inline CVector GetPedCoords(DWORD ped)
{
    CVector v{};
    if (ped) {
        v.x = *reinterpret_cast<float*>(ped + 0x30);
        v.y = *reinterpret_cast<float*>(ped + 0x34);
        v.z = *reinterpret_cast<float*>(ped + 0x38);
    }
    return v;
}

inline float Dist3D(float x1,float y1,float z1, float x2,float y2,float z2)
{
    float dx=x2-x1, dy=y2-y1, dz=z2-z1;
    return sqrtf(dx*dx+dy*dy+dz*dz);
}

#define PACKET_OBJECT_CREATE         0x5A
#define PACKET_OBJECT_DESTROY        0x5C
#define PACKET_OBJECT_SET_MATERIAL   0x7A
#define PACKET_OBJECT_SET_MAT_TEXT   0x7B
#define PACKET_REMOVE_BUILDING       0x7C

struct ObjectEntry {
    std::vector<std::string> createLines;
    std::vector<std::string> matLines;
    std::vector<std::string> matTextLines;
};

struct StreamEntry {
    int   index;
    int   model;
    float px, py, pz;
};

struct RemoveBuildingEntry {
    int   modelId;
    float x, y, z, radius;
};

struct Slot {
    std::map<int, ObjectEntry> objects;
    int objCount;
    std::unordered_map<std::string, int> strMap;
    std::vector<std::string> rbList;
    std::unordered_map<std::string, bool> rbSet;
    std::string label;
};

static const char* DIR_NAME      = "cmap";
static const char* VAR_NAME      = "cmap";
static const int   AUTOSAVE_INT  = 100;
static const int   MAX_SLOTS     = 5;

static bool  g_recording      = false;
static bool  g_show3d         = true;
static bool  g_soundEnable    = true;
static float g_maxDist        = 200.0f;
static float g_drawDist       = 200.0f;
static int   g_maxObjects     = 0;
static std::string g_lastFile;
static int   g_autosaveNext   = AUTOSAVE_INT;

static std::map<int, ObjectEntry>              g_objects;
static int                                      g_objCount = 0;
static std::unordered_map<std::string, int>    g_strMap;
static std::map<int, StreamEntry>              g_stream;

static std::vector<std::string>                g_rbList;
static std::unordered_map<std::string, bool>   g_rbSet;
static std::vector<RemoveBuildingEntry>        g_rbTemp;

static Slot  g_slots[MAX_SLOTS + 1];
static bool  g_slotUsed[MAX_SLOTS + 1] = {};

static bool   g_flymode        = false;
static CVector g_origPos        = {};
static float  g_origHealth     = 100.f;

static std::string EscapePawn(const std::string& s)
{
    std::string r;
    for (char c : s) {
        if      (c=='\\') r += "\\\\";
        else if (c=='"')  r += "\\\"";
        else if (c=='\n') r += "\\n";
        else if (c=='\r') r += "\\r";
        else if (c=='\t') r += "\\t";
        else r += c;
    }
    return r;
}

static std::string SanitizeFilename(const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        if (c=='\\'||c=='/'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')
            c = '_';
    return r;
}

static std::string FormatFloat(float f, int decimals=4)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << f;
    return ss.str();
}

static void SCM(const char* text)
{
    if (AddChatMsg) {
        std::string full = std::string("{FFFFFF}({00BFFF}Club MapStealer{FFFFFF}) ") + text;
        AddChatMsg(full.c_str(), 0xFFFFFFFF);
    }
}
static void SCM(const std::string& s) { SCM(s.c_str()); }

static std::string BuildOutput(int& cObj, int& cMat, int& cMtt, int& cRb)
{
    cObj=cMat=cMtt=cRb=0;
    std::ostringstream out;
    out << "public OnGamemodeInit()\n{\n";
    out << "    new " << VAR_NAME << ";\n";

    for (auto& kv : g_objects) {
        auto& e = kv.second;
        for (auto& s : e.createLines)  { out << "    " << s << "\n"; cObj++; }
        for (auto& s : e.matLines)     { out << "    " << s << "\n"; cMat++; }
        for (auto& s : e.matTextLines) { out << "    " << s << "\n"; cMtt++; }
    }
    out << "}\n\npublic OnPlayerConnect(playerid)\n{\n";
    for (auto& s : g_rbList) { out << "    " << s << "\n"; cRb++; }
    out << "}\n";
    return out.str();
}

static std::string WriteFile(const std::string& filename)
{
    _mkdir(DIR_NAME);
    std::string path = std::string(DIR_NAME) + "\\" + SanitizeFilename(filename) + ".pwn";

    int cObj=0, cMat=0, cMtt=0, cRb=0;
    std::string body = BuildOutput(cObj, cMat, cMtt, cRb);

    std::ofstream f(path);
    if (f.is_open()) {
        f << "// Club Map Stealer | dist: " << (int)g_maxDist << "\n";
        f << "// Objects: " << cObj
          << " | Materials: " << cMat
          << " | Text: " << cMtt
          << " | RemoveBuildings: " << cRb << "\n\n";
        f << body;
        f.close();
    }
    return path;
}

static void FlushRemoveBuildings()
{
    DWORD ped = GetPlayerPed();
    CVector pp = GetPedCoords(ped);
    float limit = g_maxDist > 0 ? g_maxDist : 400.f;

    for (auto& v : g_rbTemp) {
        float d = Dist3D(pp.x,pp.y,pp.z, v.x,v.y,v.z);
        if (d <= limit) {
            char buf[256];
            sprintf_s(buf, "RemoveBuildingForPlayer(playerid, %d, %.3f, %.3f, %.3f, %.3f);",
                v.modelId, v.x, v.y, v.z, v.radius);
            std::string s(buf);
            if (!g_rbSet.count(s)) {
                g_rbSet[s] = true;
                g_rbList.push_back(s);
            }
        }
    }
    g_rbTemp.clear();
}

static void ResetState()
{
    g_objects.clear();
    g_objCount = 0;
    g_strMap.clear();
    g_stream.clear();
    g_rbList.clear();
    g_rbSet.clear();
    g_rbTemp.clear();
    g_show3d  = true;
    g_lastFile = "";
    g_autosaveNext = AUTOSAVE_INT;
}

static std::string MakeCreateLine(int modelId,
    float x,float y,float z, float rx,float ry,float rz)
{
    char buf[512];
    sprintf_s(buf,
        "%s = CreateDynamicObject(%d, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, -1, -1, -1, %.1f, %.1f);",
        VAR_NAME, modelId, x,y,z, rx,ry,rz, g_drawDist, g_drawDist);
    return std::string(buf);
}

static std::string GetServerPrefix()
{
    char buf[64];
    sprintf_s(buf, "server_%lld", (long long)time(nullptr));
    return std::string(buf);
}

static void CmdMapHelp()
{
    SCM("{AAAAAA}========== {00BFFF}MapStealer Commands {AAAAAA}==========");
    SCM("{FFFFFF}/maprecord{AAAAAA} - {B0B0B0}Start/stop recording");
    SCM("{FFFFFF}/flymode{AAAAAA}   - {B0B0B0}Toggle fly mode");
    SCM("{FFFFFF}/savemap [name]{AAAAAA} - {B0B0B0}Save current map");
    SCM("{FFFFFF}/clearmap{AAAAAA} - {B0B0B0}Clear all recorded objects");
    SCM("{FFFFFF}/mapinfo{AAAAAA}  - {B0B0B0}Show statistics");
    SCM("{FFFFFF}/nearobj [r]{AAAAAA} - {B0B0B0}List objects within radius");
    SCM("{FFFFFF}/delobj <id>{AAAAAA} - {B0B0B0}Delete object by ID");
    SCM("{FFFFFF}/preview <id>{AAAAAA} - {B0B0B0}Show detailed object info");
    SCM("{FFFFFF}/saveslot <1-5>{AAAAAA} - {B0B0B0}Save to slot");
    SCM("{FFFFFF}/loadslot <1-5>{AAAAAA} - {B0B0B0}Load from slot");
    SCM("{FFFFFF}/mergeslot <src> <dst>{AAAAAA} - {B0B0B0}Merge two slots");
    SCM("{FFFFFF}/showtext3d{AAAAAA} - {B0B0B0}Toggle 3D labels");
    SCM("{FFFFFF}/setrendermap <d>{AAAAAA} - {B0B0B0}Set render distance");
    SCM("{FFFFFF}/setmaxobj <n>{AAAAAA} - {B0B0B0}Set max objects (0=unlimited)");
    SCM("{FFFFFF}/mapsound{AAAAAA} - {B0B0B0}Toggle capture sound");
    SCM("{FFFFFF}/rescan{AAAAAA}   - {B0B0B0}Re-scan nearby objects");
}

static void CmdMapRecord()
{
    if (g_flymode) { SCM("{FF4C4C}Disable flymode first."); return; }
    g_recording = !g_recording;
    if (g_recording) {
        g_show3d = true;
        char buf[128];
        sprintf_s(buf, "{00FF88}Recording started  {AAAAAA}dist: {FFFFFF}%.0f", g_maxDist > 0 ? g_maxDist : 400.f);
        SCM(buf);
        if (g_maxObjects > 0) {
            sprintf_s(buf, "{AAAAAA}max objects: {FFFFFF}%d", g_maxObjects);
            SCM(buf);
        }
    } else {
        FlushRemoveBuildings();
        std::string fname = GetServerPrefix();
        std::string path  = WriteFile(fname);
        g_lastFile = path;

        int cObj=0,cMat=0,cMtt=0;
        for (auto& kv : g_objects) {
            cObj += (int)kv.second.createLines.size();
            cMat += (int)kv.second.matLines.size();
            cMtt += (int)kv.second.matTextLines.size();
        }
        char buf[256];
        sprintf_s(buf, "{AAAAAA}saved  obj:{FFFFFF}%d  {AAAAAA}rb:{FFFFFF}%d  {AAAAAA}mat:{FFFFFF}%d  {AAAAAA}txt:{FFFFFF}%d",
            cObj, (int)g_rbList.size(), cMat, cMtt);
        SCM(buf);
        sprintf_s(buf, "{AAAAAA}file: {FFFFFF}%s", path.c_str());
        SCM(buf);
        ResetState();
    }
}

static void CmdSaveMap(const char* arg)
{
    if (g_objCount == 0) { SCM("{AAAAAA}no objects to save."); return; }
    std::string fname;
    if (arg && strlen(arg) > 0) fname = arg;
    else { char buf[32]; sprintf_s(buf,"map_%lld",(long long)time(nullptr)); fname=buf; }
    fname = SanitizeFilename(fname);
    std::string path = WriteFile(fname);
    g_lastFile = path;
    char buf[256];
    sprintf_s(buf,"{AAAAAA}saved to {FFFFFF}%s", path.c_str());
    SCM(buf);
}

static void CmdClearMap()
{
    int c = g_objCount;
    ResetState();
    char buf[64];
    sprintf_s(buf, "{AAAAAA}cleared {FFFFFF}%d {AAAAAA}objects.", c);
    SCM(buf);
}

static void CmdMapInfo()
{
    int cObj=0,cMat=0,cMtt=0;
    for (auto& kv : g_objects) {
        cObj += (int)kv.second.createLines.size();
        cMat += (int)kv.second.matLines.size();
        cMtt += (int)kv.second.matTextLines.size();
    }
    char buf[256];
    sprintf_s(buf, "%s  {AAAAAA}flymode: %s",
        g_recording ? "{00FF88}RECORDING" : "{FF4C4C}STOPPED",
        g_flymode   ? "{00FF88}ON"        : "{FF4C4C}OFF");
    SCM(buf);
    if (g_maxObjects > 0)
        sprintf_s(buf,"{AAAAAA}objects: {FFFFFF}%d / %d", cObj, g_maxObjects);
    else
        sprintf_s(buf,"{AAAAAA}objects: {FFFFFF}%d", cObj);
    SCM(buf);
    sprintf_s(buf,"{AAAAAA}rb: {FFFFFF}%d  {AAAAAA}mat: {FFFFFF}%d  {AAAAAA}txt: {FFFFFF}%d",
        (int)g_rbList.size(), cMat, cMtt);
    SCM(buf);
    if (!g_lastFile.empty()) {
        sprintf_s(buf,"{AAAAAA}last file: {FFFFFF}%s", g_lastFile.c_str());
        SCM(buf);
    }
    SCM("{AAAAAA}sound: {FFFFFF}" + std::string(g_soundEnable?"ON":"OFF"));

    std::string slotInfo = "{AAAAAA}slots: ";
    for (int i=1;i<=MAX_SLOTS;i++) {
        char sb[64];
        if (g_slotUsed[i])
            sprintf_s(sb,"{FFFFFF}%d{AAAAAA}(%s) ",i,g_slots[i].label.c_str());
        else
            sprintf_s(sb,"{555555}%d{AAAAAA}(empty) ",i);
        slotInfo += sb;
    }
    SCM(slotInfo);
}

static void CmdNearObj(const char* arg)
{
    if (g_objCount == 0) { SCM("{AAAAAA}no objects recorded."); return; }
    float radius = arg&&strlen(arg)>0 ? (float)atof(arg) : 20.f;
    DWORD ped = GetPlayerPed();
    CVector pp = GetPedCoords(ped);
    int found = 0;

    for (auto& kv : g_stream) {
        if (found >= 15) break;
        auto& se = kv.second;
        float d = Dist3D(pp.x,pp.y,pp.z, se.px,se.py,se.pz);
        if (d <= radius && g_objects.count(se.index)) {
            auto& e = g_objects[se.index];
            char buf[256];
            sprintf_s(buf,
                "{AAAAAA}id:{FFFFFF}%d  {AAAAAA}model:{FFFFFF}%d  {AAAAAA}dist:{FFFFFF}%.1f  {AAAAAA}mat:{FFFFFF}%d{AAAAAA}/%d",
                se.index, se.model, d,
                (int)e.matLines.size(), (int)e.matTextLines.size());
            SCM(buf);
            found++;
        }
    }
    char buf[128];
    if (found==0)
        sprintf_s(buf,"{AAAAAA}no objects within {FFFFFF}%.0f {AAAAAA}units.", radius);
    else
        sprintf_s(buf,"{AAAAAA}found {FFFFFF}%d {AAAAAA}objects within {FFFFFF}%.0f {AAAAAA}units.", found, radius);
    SCM(buf);
}

static void CmdDelObj(const char* arg)
{
    if (!g_recording) { SCM("{AAAAAA}not recording."); return; }
    int id = arg ? atoi(arg) : 0;
    if (id < 1 || !g_objects.count(id)) { SCM("{FF4C4C}Object ID not found."); return; }
    for (auto& s : g_objects[id].createLines) g_strMap.erase(s);
    g_objects.erase(id);
    g_objCount--;
    char buf[64];
    sprintf_s(buf,"{AAAAAA}deleted object {FFFFFF}%d", id);
    SCM(buf);
}

static void CmdPreview(const char* arg)
{
    int id = arg ? atoi(arg) : 0;
    if (id < 1 || !g_objects.count(id)) { SCM("{AAAAAA}invalid object ID."); return; }
    auto& e = g_objects[id];
    int model = 0;
    if (!e.createLines.empty()) {
        const char* p = strstr(e.createLines[0].c_str(), "CreateDynamicObject(");
        if (p) model = atoi(p + 20);
    }
    char buf[256];
    sprintf_s(buf,"{00BFFF}=== Object %d ===", id); SCM(buf);
    sprintf_s(buf,"{FFFFFF}Model:{AAAAAA} %d", model); SCM(buf);
    sprintf_s(buf,"{FFFFFF}Materials:{AAAAAA} %d", (int)e.matLines.size()); SCM(buf);
    sprintf_s(buf,"{FFFFFF}Mat Texts:{AAAAAA} %d", (int)e.matTextLines.size()); SCM(buf);
    if (!e.matLines.empty()) {
        std::string s = e.matLines[0].substr(0, 80);
        SCM("{B0B0B0}First material:"); SCM(s);
    }
}

static void CmdSaveSlot(const char* arg)
{
    int n = arg ? atoi(arg) : 0;
    if (n<1||n>MAX_SLOTS) { char b[64]; sprintf_s(b,"{AAAAAA}usage: /saveslot [1-%d]",MAX_SLOTS); SCM(b); return; }
    if (g_objCount==0) { SCM("{AAAAAA}no objects to save."); return; }
    g_slots[n].objects   = g_objects;
    g_slots[n].objCount  = g_objCount;
    g_slots[n].strMap    = g_strMap;
    g_slots[n].rbList    = g_rbList;
    g_slots[n].rbSet     = g_rbSet;
    char lb[32]; sprintf_s(lb,"%dobj",g_objCount);
    g_slots[n].label = lb;
    g_slotUsed[n] = true;
    char buf[128];
    sprintf_s(buf,"{AAAAAA}slot {FFFFFF}%d {AAAAAA}saved  {FFFFFF}%d {AAAAAA}objects.", n, g_objCount);
    SCM(buf);
}

static void CmdLoadSlot(const char* arg)
{
    int n = arg ? atoi(arg) : 0;
    if (n<1||n>MAX_SLOTS) { char b[64]; sprintf_s(b,"{AAAAAA}usage: /loadslot [1-%d]",MAX_SLOTS); SCM(b); return; }
    if (!g_slotUsed[n]) { char b[64]; sprintf_s(b,"{AAAAAA}slot {FFFFFF}%d {AAAAAA}is empty.",n); SCM(b); return; }
    g_objects   = g_slots[n].objects;
    g_objCount  = g_slots[n].objCount;
    g_strMap    = g_slots[n].strMap;
    g_rbList    = g_slots[n].rbList;
    g_rbSet     = g_slots[n].rbSet;
    g_stream.clear();
    char buf[128];
    sprintf_s(buf,"{AAAAAA}slot {FFFFFF}%d {AAAAAA}loaded  {FFFFFF}%d {AAAAAA}objects.", n, g_objCount);
    SCM(buf);
}

static void CmdMergeSlot(const char* arg)
{
    if (!arg||!strlen(arg)) { SCM("{AAAAAA}usage: /mergeslot <src> <dst>"); return; }
    int src=0,dst=0;
    sscanf_s(arg,"%d %d",&src,&dst);
    if (src<1||src>MAX_SLOTS||dst<1||dst>MAX_SLOTS) { SCM("{AAAAAA}invalid slot numbers."); return; }
    if (!g_slotUsed[src]) { char b[64]; sprintf_s(b,"{AAAAAA}slot {FFFFFF}%d {AAAAAA}is empty.",src); SCM(b); return; }
    if (!g_slotUsed[dst]) { g_slots[dst] = Slot(); g_slots[dst].objCount=0; g_slotUsed[dst]=true; }

    int nextId = g_slots[dst].objCount + 1;
    for (auto& kv : g_slots[src].objects) {
        g_slots[dst].objects[nextId] = kv.second;
        for (auto& s : kv.second.createLines)
            g_slots[dst].strMap[s] = nextId;
        nextId++;
    }
    for (auto& rb : g_slots[src].rbList) {
        if (!g_slots[dst].rbSet.count(rb)) {
            g_slots[dst].rbSet[rb] = true;
            g_slots[dst].rbList.push_back(rb);
        }
    }
    g_slots[dst].objCount = nextId - 1;
    char lb[32]; sprintf_s(lb,"%dobj",g_slots[dst].objCount);
    g_slots[dst].label = lb;
    char buf[128];
    sprintf_s(buf,"{AAAAAA}merged slot {FFFFFF}%d {AAAAAA}into {FFFFFF}%d {AAAAAA}(total {FFFFFF}%d{AAAAAA})",
        src, dst, g_slots[dst].objCount);
    SCM(buf);
}

static void CmdSetRenderMap(const char* arg)
{
    float v = arg ? (float)atof(arg) : 0;
    if (v>=1&&v<=1000) {
        g_maxDist = v;
        char buf[64]; sprintf_s(buf,"{AAAAAA}render distance: {FFFFFF}%.0f units",v); SCM(buf);
    } else { SCM("{AAAAAA}usage: /setrendermap [distance] (1-1000)"); }
}

static void CmdSetMaxObj(const char* arg)
{
    int v = arg ? atoi(arg) : -1;
    if (v>=0) {
        g_maxObjects = v;
        if (v==0) SCM("{AAAAAA}object limit: {FFFFFF}unlimited");
        else { char buf[64]; sprintf_s(buf,"{AAAAAA}object limit: {FFFFFF}%d",v); SCM(buf); }
    } else { SCM("{AAAAAA}usage: /setmaxobj [count] (0=unlimited)"); }
}

static void CmdMapSound()
{
    g_soundEnable = !g_soundEnable;
    SCM(std::string("{AAAAAA}sound: {FFFFFF}") + (g_soundEnable?"ON":"OFF"));
}

static void CmdShowText3d()
{
    if (g_objCount==0) { SCM("{AAAAAA}no objects recorded."); return; }
    g_show3d = !g_show3d;
    char buf[64];
    sprintf_s(buf,"{AAAAAA}3D labels: {FFFFFF}%s  {AAAAAA}(%d obj)",
        g_show3d?"ON":"OFF", g_objCount);
    SCM(buf);
}

void OnCreateObject(int objectId, int modelId, float x, float y, float z,
                    float rx, float ry, float rz)
{
    if (!g_recording) return;
    DWORD ped = GetPlayerPed();
    CVector pp = GetPedCoords(ped);
    float limit = g_maxDist > 0 ? g_maxDist : 400.f;
    if (Dist3D(pp.x,pp.y,pp.z, x,y,z) > limit) return;
    if (g_maxObjects > 0 && g_objCount >= g_maxObjects) return;

    std::string line = MakeCreateLine(modelId, x,y,z, rx,ry,rz);

    int idx;
    if (g_strMap.count(line)) {
        idx = g_strMap[line];
    } else {
        g_objCount++;
        idx = g_objCount;
        ObjectEntry e;
        e.createLines.push_back(line);
        g_objects[idx] = e;
        g_strMap[line] = idx;
    }

    StreamEntry se;
    se.index = idx;
    se.model = modelId;
    se.px = x; se.py = y; se.pz = z;
    g_stream[objectId] = se;
}

void OnDestroyObject(int objectId)
{
    g_stream.erase(objectId);
}

void OnSetObjectMaterial(int objectId, int materialId, int modelId,
                         const std::string& libName, const std::string& texName,
                         unsigned int color)
{
    if (!g_recording) return;
    if (!g_stream.count(objectId)) return;
    int idx = g_stream[objectId].index;
    if (!g_objects.count(idx)) return;

    char buf[512];
    sprintf_s(buf, "SetDynamicObjectMaterial(%s, %d, %d, \"%s\", \"%s\", %u);",
        VAR_NAME, materialId, modelId,
        EscapePawn(libName).c_str(), EscapePawn(texName).c_str(), color);
    std::string s(buf);
    auto& matLines = g_objects[idx].matLines;
    if (std::find(matLines.begin(), matLines.end(), s) == matLines.end())
        matLines.push_back(s);
}

void OnSetObjectMaterialText(int objectId, int materialId,
    const std::string& text, int materialSize,
    const std::string& fontName, int fontSize, int bold,
    unsigned int fontColor, unsigned int bgColor, int align)
{
    if (!g_recording) return;
    if (!g_stream.count(objectId)) return;
    int idx = g_stream[objectId].index;
    if (!g_objects.count(idx)) return;

    char buf[1024];
    sprintf_s(buf,
        "SetDynamicObjectMaterialText(%s, %d, \"%s\", %d, \"%s\", %d, %d, %u, %u, %d);",
        VAR_NAME, materialId, EscapePawn(text).c_str(), materialSize,
        EscapePawn(fontName).c_str(), fontSize, bold, fontColor, bgColor, align);
    std::string s(buf);
    auto& mttLines = g_objects[idx].matTextLines;
    if (std::find(mttLines.begin(), mttLines.end(), s) == mttLines.end())
        mttLines.push_back(s);
}

void OnRemoveBuilding(int modelId, float x, float y, float z, float radius)
{
    DWORD ped = GetPlayerPed();
    CVector pp = GetPedCoords(ped);
    float limit = g_maxDist > 0 ? g_maxDist : 400.f;
    if (Dist3D(pp.x,pp.y,pp.z, x,y,z) > limit) return;
    g_rbTemp.push_back({modelId,x,y,z,radius});
}

bool HandleCommand(const char* cmdRaw)
{
    if (!cmdRaw || cmdRaw[0] != '/') return false;
    const char* p = cmdRaw + 1;

    char cmd[64] = {};
    const char* args = strchr(p, ' ');
    if (args) {
        size_t len = args - p;
        if (len >= sizeof(cmd)) len = sizeof(cmd)-1;
        memcpy(cmd, p, len);
        args++;
    } else {
        strncpy_s(cmd, p, _TRUNCATE);
        args = "";
    }

    for (char* c = cmd; *c; c++) *c = (char)tolower(*c);

    if (!strcmp(cmd,"maphelp"))     { CmdMapHelp(); return true; }
    if (!strcmp(cmd,"maprecord"))   { CmdMapRecord(); return true; }
    if (!strcmp(cmd,"savemap"))     { CmdSaveMap(args); return true; }
    if (!strcmp(cmd,"clearmap"))    { CmdClearMap(); return true; }
    if (!strcmp(cmd,"mapinfo"))     { CmdMapInfo(); return true; }
    if (!strcmp(cmd,"nearobj"))     { CmdNearObj(args); return true; }
    if (!strcmp(cmd,"delobj"))      { CmdDelObj(args); return true; }
    if (!strcmp(cmd,"preview"))     { CmdPreview(args); return true; }
    if (!strcmp(cmd,"saveslot"))    { CmdSaveSlot(args); return true; }
    if (!strcmp(cmd,"loadslot"))    { CmdLoadSlot(args); return true; }
    if (!strcmp(cmd,"mergeslot"))   { CmdMergeSlot(args); return true; }
    if (!strcmp(cmd,"showtext3d"))  { CmdShowText3d(); return true; }
    if (!strcmp(cmd,"setrendermap")){ CmdSetRenderMap(args); return true; }
    if (!strcmp(cmd,"setmaxobj"))   { CmdSetMaxObj(args); return true; }
    if (!strcmp(cmd,"mapsound"))    { CmdMapSound(); return true; }
    if (!strcmp(cmd,"rescan"))      {
        if (!g_recording) { SCM("{AAAAAA}not recording."); return true; }
        SCM("{AAAAAA}rescan: scan nearby GTA world objects...");
        return true;
    }
    if (!strcmp(cmd,"flymode")) {
        g_flymode = !g_flymode;
        SCM(g_flymode ? "{00FF88}Flymode ON - WASD to move, Space up, LShift down"
                      : "{FF4C4C}Flymode OFF");
        return true;
    }
    return false;
}

static void TickLoop()
{
    static int hudCounter = 0;

    if (!g_recording) return;

    if (g_objCount >= g_autosaveNext && g_objCount > 0) {
        FlushRemoveBuildings();
        char fname[64];
        sprintf_s(fname, "autosave_%lld", (long long)time(nullptr));
        std::string path = WriteFile(fname);
        g_lastFile = path;
        g_autosaveNext = g_objCount + AUTOSAVE_INT;
        char buf[256];
        sprintf_s(buf, "{FFD700}autosave  {AAAAAA}obj:{FFFFFF}%d  {AAAAAA}-> {FFFFFF}%s",
            g_objCount, path.c_str());
        SCM(buf);
    }

    hudCounter++;
    if (hudCounter >= 60) {
        hudCounter = 0;
        int cObj=0,cMat=0,cMtt=0;
        for (auto& kv : g_objects) {
            cObj += (int)kv.second.createLines.size();
            cMat += (int)kv.second.matLines.size();
            cMtt += (int)kv.second.matTextLines.size();
        }
    }
}

static DWORD WINAPI MainThread(LPVOID)
{
    while (!GetModuleHandleA("samp.dll"))
        Sleep(500);

    Sleep(2000);

    SCM("{00BFFF}Club MapStealer {AAAAAA}loaded  /maphelp for commands");

    while (true) {
        TickLoop();
        Sleep(17);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
