/* net.cpp -- ENet lockstep multiplayer core. See net.h for the design. */
/* NOTE: include SDL3 + enet BEFORE sdw_classes.h: that header forward-declares
 * `struct FILE;` which conflicts with glibc's `typedef struct _IO_FILE FILE;`
 * once a real <stdio.h> is pulled in afterwards. */
#define SDW_HAVE_STDIO 1
#include <SDL3/SDL.h>
#include <enet/enet.h>

#include "sdw_types.h"
#include "sdw_enums.h"
#include "sdw_classes.h"

#include "net.h"
#include "crc32.h"
#include "../app/app_main.h" /* g_scnObjects, g_scnObjectCount, Camera g_camera */
#include <cstring>
#include <cstdio>
#include <cstdlib>

/* ---- engine globals (defined elsewhere) ---- */
extern s32 g_frameCount;   /* time.cpp: the lockstep clock */
extern u32 g_gameFlags;    /* set/cleared by list.cpp / scenaric side */
extern Pad g_pad;          /* input.cpp */
extern Wolf *g_pWolf;      /* game.cpp: the LOCAL player's wolf */

/* ---- tunables ---- */
#define NET_DEFAULT_PORT      7654
#define NET_RING_SIZE         64    /* remote snapshot ring capacity */
#define NET_GATE_TIMEOUT_MS   5000  /* stop waiting for the peer after this long */
#define NET_CHECKSUM_PERIOD_MS 4000
#define NET_WIRE_LEN          12    /* INPUT: type(1) slot(1) frame(4) buttons(2) lx ly rx ry(4) */

/* ---- session state ---- */
static int s_active = 0;
static int s_host = 0;
static int s_connected = 0;
static int s_ready = 0; /* HELLO exchanged */
static ENetAddress s_addr;
static ENetHost *s_server = NULL;
static ENetPeer *s_peer = NULL;
static char s_joinHost[128] = "";
static unsigned short s_port = NET_DEFAULT_PORT;

/* ---- lockstep buffers ---- */
static s32 s_lastSentFrame = -1;
static PadFrame s_lastLocalRaw = {{0}};
static u8 s_pendingInput[NET_WIRE_LEN];
static int s_pendingInputValid = 0;

struct RemoteSlot {
    s32 frame;
    PadFrame pad;
};
static RemoteSlot s_ring[NET_RING_SIZE];
static int s_head = 0;
static int s_count = 0;

static s32 s_gateNeededFrame = 0; /* the frame whose remote input the gate waits for */
static int s_stallEvents = 0;
static int s_gateStalled = 0;

/* ---- level sync ---- */
int g_netRemoteWolfPending = 0;
static int s_pendingStart = 0;
static s32 s_pendingStartLevel = -1;
static u32 s_startFrame = 0;

/* ---- checksums ---- */
static u32 s_localChecksum = 0;
static u32 s_remoteChecksum = 0;
static int s_desync = 0;
static u32 s_lastChecksumTick = 0;

/* ---- little-endian wire helpers ---- */
static void Net_WriteU32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}
static u32 Net_ReadU32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void Net_SendPacket(const u8 *data, size_t len)
{
    if (!s_peer)
        return;
    ENetPacket *pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    if (pkt)
        enet_peer_send(s_peer, 0, pkt);
}

/* ---- local input capture / send ---- */
void Net_CaptureLocalRaw(const PadFrame *raw)
{
    s_lastLocalRaw = *raw;
}

void Net_SendLocalInput()
{
    if (!s_connected)
        return;
    u8 *m = s_pendingInput;
    s_lastSentFrame = g_frameCount + NET_INPUT_DELAY;
    m[0] = NET_MSG_INPUT;
    m[1] = 0;
    Net_WriteU32(m + 2, (u32)s_lastSentFrame);
    m[6] = (u8)s_lastLocalRaw.buttons;
    m[7] = (u8)(s_lastLocalRaw.buttons >> 8);
    m[8] = s_lastLocalRaw.leftX;
    m[9] = s_lastLocalRaw.leftY;
    m[10] = s_lastLocalRaw.rightX;
    m[11] = s_lastLocalRaw.rightY;
    s_pendingInputValid = 1;
    Net_SendPacket(m, NET_WIRE_LEN);
}

/* ---- remote snapshot ring ---- */
static void Net_RemotePush(s32 frame, const PadFrame *pad)
{
    /* ignore snapshots for frames we have already gated past */
    if (frame <= s_gateNeededFrame && s_count > 0)
        return;
    /* duplicate of the newest frame (we resend every loop iteration): overwrite in place */
    if (s_count > 0) {
        int last = (s_head + s_count - 1) % NET_RING_SIZE;
        if (s_ring[last].frame == frame) {
            s_ring[last].pad = *pad;
            return;
        }
    }
    if (s_count >= NET_RING_SIZE) {
        s_head = (s_head + 1) % NET_RING_SIZE;
        s_count--;
    }
    int tail = (s_head + s_count) % NET_RING_SIZE;
    s_ring[tail].frame = frame;
    s_ring[tail].pad = *pad;
    s_count++;
}

int Net_RemoteInputForFrame(s32 frame, PadFrame *out)
{
    while (s_count > 0 && s_ring[s_head].frame < frame) {
        s_head = (s_head + 1) % NET_RING_SIZE; /* stale: drop */
        s_count--;
    }
    if (s_count > 0 && s_ring[s_head].frame == frame) {
        *out = s_ring[s_head].pad;
        s_head = (s_head + 1) % NET_RING_SIZE;
        s_count--;
        return 1;
    }
    return 0;
}

/* ---- rolling checksum of the shared simulation state ---- */
extern Wolf *Net_FindRemoteWolf(); /* wolf_clone.cpp */

static u32 Net_ComputeChecksum()
{
    if (!g_pWolf || !(g_gameFlags & GF_LEVEL_LOADED_A))
        return 0;
    struct {
        s16 x, y, z;
        u16 facing;
        u8 state, mode, surface;
        u16 scale;
        u8 padIdx;
    } rec;
    memset(&rec, 0, sizeof(rec));
    rec.x = g_pWolf->pos.x;
    rec.y = g_pWolf->pos.y;
    rec.z = g_pWolf->pos.z;
    rec.facing = (u16)g_pWolf->rot.y;
    rec.state = g_pWolf->state;
    rec.mode = g_pWolf->mode;
    rec.surface = g_pWolf->surface;
    rec.scale = (u16)g_pWolf->scale;
    u32 crc = Crc32((const u8 *)&rec, (int)sizeof(rec));
    Wolf *remote = Net_FindRemoteWolf();
    if (remote) {
        rec.x = remote->pos.x;
        rec.y = remote->pos.y;
        rec.z = remote->pos.z;
        rec.facing = (u16)remote->rot.y;
        rec.state = remote->state;
        rec.padIdx = 1;
        crc = Crc32((const u8 *)&rec, (int)sizeof(rec)) ^ crc;
    }
    u8 nb[2] = {(u8)g_scnObjectCount, (u8)(g_scnObjectCount >> 8)};
    return Crc32(nb, 2) ^ crc ^ ((u32)g_frameCount * 2654435761u);
}

static void Net_MaybeSendChecksum()
{
    u32 now = SDL_GetTicks();
    if (now - s_lastChecksumTick < NET_CHECKSUM_PERIOD_MS)
        return;
    s_lastChecksumTick = now;
    s_localChecksum = Net_ComputeChecksum();
    u8 m[10];
    m[0] = NET_MSG_CHECKSUM;
    m[1] = 0;
    Net_WriteU32(m + 2, (u32)g_frameCount);
    Net_WriteU32(m + 6, s_localChecksum);
    Net_SendPacket(m, sizeof(m));
}

/* ---- messages ---- */
/* peek without consuming: does the ring hold exactly `frame`? */
int Net_RemoteInputReadyForFrame(s32 frame)
{
    if (s_count == 0)
        return 0;
    /* drop stale entries that sit at the head */
    while (s_count > 0 && s_ring[s_head].frame < frame) {
        s_head = (s_head + 1) % NET_RING_SIZE;
        s_count--;
    }
    return s_count > 0 && s_ring[s_head].frame == frame;
}

static void Net_OnConnect()
{
    s_connected = 1;
    s_gateNeededFrame = g_frameCount + NET_INPUT_DELAY;
    u8 hello[4] = {NET_MSG_HELLO, 1, 0, 0};
    Net_SendPacket(hello, 4);
    /* a late joiner gets the scene announced to it as soon as its level is installed (see list.cpp), so nothing
     * is sent here beyond the handshake */
}

static void Net_OnDisconnect()
{
    s_connected = 0;
    s_ready = 0;
    s_peer = NULL;
    s_count = 0;
    s_pendingInputValid = 0;
    g_netRemoteWolfPending = 0;
}

static void Net_OnPacket(const u8 *d, size_t len)
{
    if (len < 2)
        return;
    switch (d[0]) {
        case NET_MSG_HELLO:
            s_ready = 1;
            break;
        case NET_MSG_START_LEVEL:
            if (len >= 10 && !s_host) {
                s32 levelId = (s32)Net_ReadU32(d + 2);
                s_startFrame = Net_ReadU32(d + 6);
                s_pendingStart = 1;
                s_pendingStartLevel = levelId;
                g_netRemoteWolfPending = 1;
                extern void WolfClone_OnStartLevelReceived(s32 levelId, u32 startFrame);
                WolfClone_OnStartLevelReceived(levelId, s_startFrame);
            }
            break;
        case NET_MSG_INPUT:
            if (len >= NET_WIRE_LEN) {
                PadFrame pf;
                memset(&pf, 0, sizeof(pf));
                pf.status = 0;
                pf.typeLen.type = PADTYPE_ANALOG;
                pf.typeLen.len = 8;
                pf.buttons = (u16)d[6] | ((u16)d[7] << 8);
                pf.leftX = d[8];
                pf.leftY = d[9];
                pf.rightX = d[10];
                pf.rightY = d[11];
                Net_RemotePush((s32)Net_ReadU32(d + 2), &pf);
            }
            break;
        case NET_MSG_CHECKSUM:
            if (len >= 10) {
                u32 theirCrc = Net_ReadU32(d + 6);
                s_remoteChecksum = theirCrc;
                if (s_localChecksum != 0 && theirCrc != 0)
                    s_desync = (theirCrc != s_localChecksum);
            }
            break;
    }
}

/* ---- pump ---- */
static void Net_Pump(int waitMs)
{
    if (!s_server)
        return;
    ENetEvent ev;
    int rc;
    if (waitMs > 0)
        rc = enet_host_service(s_server, &ev, waitMs);
    else
        rc = enet_host_check_events(s_server, &ev);
    for (; rc > 0; rc = enet_host_check_events(s_server, &ev)) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                s_peer = ev.peer;
                Net_OnConnect();
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                Net_OnPacket(ev.packet->data, ev.packet->dataLength);
                enet_packet_destroy(ev.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                Net_OnDisconnect();
                break;
            default:
                break;
        }
    }
    /* client keeps retrying until the host answers */
    if (!s_host && !s_peer)
        s_peer = enet_host_connect(s_server, &s_addr, 1, 0);
    enet_host_flush(s_server);
}

/* ---- the frame gate: run before Time_Update so both peers simulate identical frame sequences ---- */
void Net_LockstepGate()
{
    if (!s_active || !s_server)
        return;
    if (!(g_gameFlags & GF_LEVEL_LOADED_A)) {
        Net_Pump(1); /* menus and cines run free */
        return;
    }
    if (!s_connected) {
        Net_Pump(4);
        return;
    }
    if (s_pendingInputValid)
        Net_SendPacket(s_pendingInput, NET_WIRE_LEN); /* reliable resend until acked */
    Net_MaybeSendChecksum();

    u32 startTick = SDL_GetTicks();
    int stalled = 0;
    while (!Net_RemoteInputReadyForFrame(s_gateNeededFrame)) {
        Net_Pump(stalled ? 1 : 4);
        stalled = 1;
        if (!s_connected)
            break;
        if (SDL_GetTicks() - startTick > NET_GATE_TIMEOUT_MS) {
            s_gateNeededFrame++; /* peer silent too long: skip a frame instead of freezing */
            s_stallEvents++;
            break;
        }
    }
    s_gateStalled = stalled;
    /* the snapshot for this frame is buffered: the next Time_Update advances g_frameCount to it */
    if (Net_RemoteInputReadyForFrame(s_gateNeededFrame))
        s_gateNeededFrame++;
    Net_Pump(0);
}

/* ---- level announcement (host side) ---- */
void Net_NotifyLevelStart(s32 levelId)
{
    if (!s_active || !s_connected)
        return;
    u8 m[10];
    m[0] = NET_MSG_START_LEVEL;
    m[1] = 0;
    Net_WriteU32(m + 2, (u32)levelId);
    s_startFrame = (u32)g_frameCount + NET_INPUT_DELAY;
    Net_WriteU32(m + 6, s_startFrame);
    Net_SendPacket(m, sizeof(m));
    Net_Pump(0);
}

void Net_OnStartLevel(s32 levelId, u32 startFrame)
{
    extern void WolfClone_OnStartLevelReceived(s32 levelId, u32 startFrame);
    WolfClone_OnStartLevelReceived(levelId, startFrame);
}

/* ---- stats ---- */
void Net_GetStats(NetStats *out)
{
    memset(out, 0, sizeof(*out));
    out->role = !s_active ? "off" : (s_host ? "host" : "client");
    out->connected = s_connected;
    out->pendingStartLevel = s_pendingStart;
    out->rttMs = (s_peer && s_connected) ? (u32)s_peer->roundTripTime : 0;
    out->localFrame = s_lastSentFrame;
    out->remoteFrame = s_count > 0 ? s_ring[(s_head + s_count - 1) % NET_RING_SIZE].frame : -1;
    out->bufferedFrames = s_count;
    out->gateStalled = s_gateStalled;
    out->stallEvents = s_stallEvents;
    out->localChecksum = s_localChecksum;
    out->remoteChecksum = s_remoteChecksum;
    out->desync = s_desync;
}

/* ---- init / shutdown ---- */
static int Net_ParseOption(const char *opt)
{
    if (strncmp(opt, "host", 4) == 0) {
        s_host = 1;
        if (opt[4] == '=')
            s_port = (unsigned short)atoi(opt + 5);
        return 0;
    }
    if (strncmp(opt, "join=", 5) == 0) {
        s_host = 0;
        const char *v = opt + 5;
        const char *colon = strrchr(v, ':');
        if (colon && colon[1]) {
            size_t n = (size_t)(colon - v);
            if (n >= sizeof(s_joinHost))
                n = sizeof(s_joinHost) - 1;
            memcpy(s_joinHost, v, n);
            s_joinHost[n] = 0;
            s_port = (unsigned short)atoi(colon + 1);
        } else {
            snprintf(s_joinHost, sizeof(s_joinHost), "%s", v);
        }
        return s_joinHost[0] ? 0 : -1;
    }
    return -1;
}

int Net_Init(const char *netOpt)
{
    if (!netOpt || !netOpt[0])
        return -1;
    if (Net_ParseOption(netOpt) < 0)
        return -1;
    if (enet_initialize() != 0)
        return -2;

    memset(&s_addr, 0, sizeof(s_addr));
    if (s_host) {
        s_addr.host = ENET_HOST_ANY;
    } else {
        if (enet_address_set_host(&s_addr, s_joinHost) < 0) {
            enet_deinitialize();
            return -3;
        }
    }
    s_addr.port = s_port;

    s_server = enet_host_create(s_host ? &s_addr : NULL, 1, 1, 0, 0);
    if (!s_server) {
        enet_deinitialize();
        return -4;
    }
    if (!s_host) {
        s_peer = enet_host_connect(s_server, &s_addr, 1, 0);
        if (!s_peer) {
            enet_host_destroy(s_server);
            s_server = NULL;
            enet_deinitialize();
            return -5;
        }
    }
    s_active = 1;
    s_lastChecksumTick = SDL_GetTicks();
    return 0;
}

void Net_Shutdown()
{
    if (!s_active)
        return;
    if (s_peer)
        enet_peer_disconnect_now(s_peer, 0);
    if (s_server)
        enet_host_destroy(s_server);
    s_server = NULL;
    s_peer = NULL;
    s_active = 0;
    s_connected = 0;
    enet_deinitialize();
}

int Net_IsActive() { return s_active; }
int Net_IsHost() { return s_host; }
int Net_IsConnected() { return s_connected; }
