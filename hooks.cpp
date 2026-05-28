/*
 * ============================================================
 *  Club MapStealer — Hook Implementation
 *  File: hooks.cpp
 *
 *  Covers:
 *   1. Resolve SAMP base & key pointers
 *   2. Hook SAMP incoming RakNet packet handler
 *   3. BitStream parser for each packet
 *   4. Hook SAMP chat command dispatcher
 *   5. Hook AddChatMessage (for SCM output)
 * ============================================================
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"   // https://github.com/TsudaKageyu/minhook

void OnCreateObject   (int objId,int model,float x,float y,float z,float rx,float ry,float rz);
void OnDestroyObject  (int objId);
void OnSetObjectMaterial(int objId,int matId,int model,
                         const std::string& lib,const std::string& tex,unsigned int color);
void OnSetObjectMaterialText(int objId,int matId,const std::string& text,
                              int matSize,const std::string& font,int fontSize,
                              int bold,unsigned int fontColor,unsigned int bgColor,int align);
void OnRemoveBuilding (int model,float x,float y,float z,float radius);
bool HandleCommand    (const char* cmd);
void SCM              (const char* text);

namespace SAMP_OFFSETS {
    const DWORD RAKPEER_PTR        = 0x21A0F8;
    const DWORD ADDCHATMSG         = 0x64010; 
    const DWORD SENDCHAT           = 0x4CA0;

    const DWORD PROCESS_PACKETS    = 0x7B80;

    const DWORD HANDLER_TABLE      = 0x21A100;
}

#define PACKET_CREATE_OBJECT        0x5A
#define PACKET_DESTROY_OBJECT       0x5C
#define PACKET_SET_OBJECT_MATERIAL  0x7A
#define PACKET_SET_OBJECT_MATTEXT   0x7B
#define PACKET_REMOVE_BUILDING      0x7C

class BitStream
{
public:
    const unsigned char* data;
    int   bitLen;
    int   readOffset;

    BitStream(const unsigned char* d, int byteLen)
        : data(d), bitLen(byteLen*8), readOffset(0) {}

    void SkipBits(int n) { readOffset += n; }
    void SkipBytes(int n) { readOffset += n*8; }

    bool CanRead(int bits) const { return (readOffset + bits) <= bitLen; }

    bool ReadBool() {
        if (!CanRead(1)) return false;
        bool v = (data[readOffset >> 3] >> (7 - (readOffset & 7))) & 1;
        readOffset++;
        return v;
    }

    template<typename T>
    T Read() {
        if (readOffset & 7) readOffset = (readOffset + 7) & ~7;
        T v = {};
        memcpy(&v, data + (readOffset >> 3), sizeof(T));
        readOffset += sizeof(T) * 8;
        return v;
    }

    float ReadFloat() { return Read<float>(); }

    std::string ReadString8() {
        unsigned char len = Read<unsigned char>();
        if (readOffset & 7) readOffset = (readOffset + 7) & ~7;
        std::string s;
        s.resize(len);
        memcpy(&s[0], data + (readOffset >> 3), len);
        readOffset += len * 8;
        return s;
    }

    std::string ReadString16() {
        unsigned short len = Read<unsigned short>();
        if (readOffset & 7) readOffset = (readOffset + 7) & ~7;
        std::string s;
        s.resize(len);
        memcpy(&s[0], data + (readOffset >> 3), len);
        readOffset += len * 8;
        return s;
    }
};

static void ParseCreateObject(const unsigned char* data, int len)
{
    BitStream bs(data, len);
    bs.SkipBytes(1);

    unsigned short objectId = bs.Read<unsigned short>();
    unsigned short modelId  = bs.Read<unsigned short>();
    float x  = bs.ReadFloat();
    float y  = bs.ReadFloat();
    float z  = bs.ReadFloat();
    float rx = bs.ReadFloat();
    float ry = bs.ReadFloat();
    float rz = bs.ReadFloat();

    OnCreateObject(objectId, modelId, x, y, z, rx, ry, rz);
}

static void ParseDestroyObject(const unsigned char* data, int len)
{
    BitStream bs(data, len);
    bs.SkipBytes(1);
    unsigned short objectId = bs.Read<unsigned short>();
    OnDestroyObject(objectId);
}

static void ParseSetObjectMaterial(const unsigned char* data, int len)
{
    BitStream bs(data, len);
    bs.SkipBytes(1);

    unsigned short objectId  = bs.Read<unsigned short>();
    unsigned char  matIdx    = bs.Read<unsigned char>();
    unsigned short modelId   = bs.Read<unsigned short>();
    std::string    txdName   = bs.ReadString8();
    std::string    texName   = bs.ReadString8();
    unsigned int   color     = bs.Read<unsigned int>();

    OnSetObjectMaterial(objectId, matIdx, modelId, txdName, texName, color);
}

static void ParseSetObjectMaterialText(const unsigned char* data, int len)
{
    BitStream bs(data, len);
    bs.SkipBytes(1);

    unsigned char  matSize0  = bs.Read<unsigned char>();
    unsigned short objectId  = bs.Read<unsigned short>();
    unsigned char  matIdx    = bs.Read<unsigned char>();
    std::string    text      = bs.ReadString16();
    unsigned char  matSize   = bs.Read<unsigned char>();
    std::string    fontFace  = bs.ReadString8();
    unsigned char  fontSize  = bs.Read<unsigned char>();
    unsigned char  bold      = bs.Read<unsigned char>();
    unsigned int   fontColor = bs.Read<unsigned int>();
    unsigned int   backColor = bs.Read<unsigned int>();
    unsigned char  align     = bs.Read<unsigned char>();

    (void)matSize0;
    OnSetObjectMaterialText(objectId, matIdx, text, matSize, fontFace,
                            fontSize, bold, fontColor, backColor, align);
}

static void ParseRemoveBuilding(const unsigned char* data, int len)
{
    BitStream bs(data, len);
    bs.SkipBytes(1);

    int   modelId = bs.Read<int>();
    float x       = bs.ReadFloat();
    float y       = bs.ReadFloat();
    float z       = bs.ReadFloat();
    float radius  = bs.ReadFloat();

    OnRemoveBuilding(modelId, x, y, z, radius);
}

typedef void (__thiscall* ProcessPackets_t)(void*);
ProcessPackets_t OrigProcessPackets = nullptr;

static void* GetRakPeer()
{
    HMODULE hSamp = GetModuleHandleA("samp.dll");
    if (!hSamp) return nullptr;
    void** ppRak = reinterpret_cast<void**>((DWORD)hSamp + SAMP_OFFSETS::RAKPEER_PTR);
    return ppRak ? *ppRak : nullptr;
}

typedef void (*PacketHandler_t)(unsigned char* data, int len, const char* ip, unsigned short port);

static PacketHandler_t OrigHandler_CreateObject   = nullptr;
static PacketHandler_t OrigHandler_DestroyObject  = nullptr;
static PacketHandler_t OrigHandler_SetMaterial    = nullptr;
static PacketHandler_t OrigHandler_SetMatText     = nullptr;
static PacketHandler_t OrigHandler_RemoveBuilding = nullptr;

static void MyHandler_CreateObject(unsigned char* d, int len, const char* ip, unsigned short port)
{
    ParseCreateObject(d, len);
    if (OrigHandler_CreateObject) OrigHandler_CreateObject(d, len, ip, port);
}
static void MyHandler_DestroyObject(unsigned char* d, int len, const char* ip, unsigned short port)
{
    ParseDestroyObject(d, len);
    if (OrigHandler_DestroyObject) OrigHandler_DestroyObject(d, len, ip, port);
}
static void MyHandler_SetMaterial(unsigned char* d, int len, const char* ip, unsigned short port)
{
    ParseSetObjectMaterial(d, len);
    if (OrigHandler_SetMaterial) OrigHandler_SetMaterial(d, len, ip, port);
}
static void MyHandler_SetMatText(unsigned char* d, int len, const char* ip, unsigned short port)
{
    ParseSetObjectMaterialText(d, len);
    if (OrigHandler_SetMatText) OrigHandler_SetMatText(d, len, ip, port);
}
static void MyHandler_RemoveBuilding(unsigned char* d, int len, const char* ip, unsigned short port)
{
    ParseRemoveBuilding(d, len);
    if (OrigHandler_RemoveBuilding) OrigHandler_RemoveBuilding(d, len, ip, port);
}

static bool HookHandlerTable()
{
    HMODULE hSamp = GetModuleHandleA("samp.dll");
    if (!hSamp) return false;

    PacketHandler_t* table = reinterpret_cast<PacketHandler_t*>(
        (DWORD)hSamp + SAMP_OFFSETS::HANDLER_TABLE);

    DWORD oldProt;
    VirtualProtect(table, sizeof(PacketHandler_t) * 256, PAGE_READWRITE, &oldProt);

    OrigHandler_CreateObject   = table[PACKET_CREATE_OBJECT];
    OrigHandler_DestroyObject  = table[PACKET_DESTROY_OBJECT];
    OrigHandler_SetMaterial    = table[PACKET_SET_OBJECT_MATERIAL];
    OrigHandler_SetMatText     = table[PACKET_SET_OBJECT_MATTEXT];
    OrigHandler_RemoveBuilding = table[PACKET_REMOVE_BUILDING];

    table[PACKET_CREATE_OBJECT]        = MyHandler_CreateObject;
    table[PACKET_DESTROY_OBJECT]       = MyHandler_DestroyObject;
    table[PACKET_SET_OBJECT_MATERIAL]  = MyHandler_SetMaterial;
    table[PACKET_SET_OBJECT_MATTEXT]   = MyHandler_SetMatText;
    table[PACKET_REMOVE_BUILDING]      = MyHandler_RemoveBuilding;

    VirtualProtect(table, sizeof(PacketHandler_t) * 256, oldProt, &oldProt);
    return true;
}

typedef void (__cdecl* AddChatMsg_t)(const char*, DWORD);
static AddChatMsg_t g_AddChatMsg = nullptr;

extern AddChatMsg_t g_SampAddChat;
AddChatMsg_t g_SampAddChat = nullptr;

static bool HookAddChatMessage()
{
    HMODULE hSamp = GetModuleHandleA("samp.dll");
    if (!hSamp) return false;

    g_SampAddChat = reinterpret_cast<AddChatMsg_t>(
        (DWORD)hSamp + SAMP_OFFSETS::ADDCHATMSG);

    return g_SampAddChat != nullptr;
}

typedef void (__cdecl* SendChat_t)(const char*);
static SendChat_t OrigSendChat = nullptr;

static void __cdecl HookedSendChat(const char* text)
{
    if (text && text[0] == '/') {
        if (HandleCommand(text)) return;
    }
    if (OrigSendChat) OrigSendChat(text);
}

static bool HookSendChat()
{
    HMODULE hSamp = GetModuleHandleA("samp.dll");
    if (!hSamp) return false;

    LPVOID target = reinterpret_cast<LPVOID>((DWORD)hSamp + SAMP_OFFSETS::SENDCHAT);

    if (MH_CreateHook(target, &HookedSendChat, (LPVOID*)&OrigSendChat) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK)
        return false;

    return true;
}

bool InitHooks()
{
    if (MH_Initialize() != MH_OK) return false;

    bool ok = true;
    ok &= HookAddChatMessage();
    ok &= HookSendChat();
    ok &= HookHandlerTable();

    return ok;
}

void ShutdownHooks()
{
    HMODULE hSamp = GetModuleHandleA("samp.dll");
    if (hSamp) {
        PacketHandler_t* table = reinterpret_cast<PacketHandler_t*>(
            (DWORD)hSamp + SAMP_OFFSETS::HANDLER_TABLE);
        DWORD oldProt;
        VirtualProtect(table, sizeof(PacketHandler_t)*256, PAGE_READWRITE, &oldProt);
        if (OrigHandler_CreateObject)   table[PACKET_CREATE_OBJECT]       = OrigHandler_CreateObject;
        if (OrigHandler_DestroyObject)  table[PACKET_DESTROY_OBJECT]      = OrigHandler_DestroyObject;
        if (OrigHandler_SetMaterial)    table[PACKET_SET_OBJECT_MATERIAL] = OrigHandler_SetMaterial;
        if (OrigHandler_SetMatText)     table[PACKET_SET_OBJECT_MATTEXT]  = OrigHandler_SetMatText;
        if (OrigHandler_RemoveBuilding) table[PACKET_REMOVE_BUILDING]     = OrigHandler_RemoveBuilding;
        VirtualProtect(table, sizeof(PacketHandler_t)*256, oldProt, &oldProt);
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}
