/* net.h -- ENet lockstep multiplayer (host = player 1 / local wolf, one client = player 2 / remote wolf).
 *
 * Design: fixed-step lockstep. Both peers run the same level; each frame every peer sends its pad snapshot
 * stamped with the game frame number it was sampled at (Input_Poll after Game_Frame_2). Snapshots are applied
 * NET_INPUT_DELAY frames later, so the command is already buffered on both sides before either simulates it and
 * inputs never cross causality. Net_LockstepGate() runs before Time_Update(): it pumps ENet, keeps sending the
 * latest snapshot (unacked reliable traffic retransmits automatically) and stalls the loop until the snapshot for
 * the next simulated frame has arrived on both sides -- worst-case one RTT of extra latency per frame, which the
 * delay absorbs. A checksum exchange every 4 seconds flags desyncs in the F3 overlay.
 *
 * The remote pad is injected into g_pad2.raw by Input_Poll (src/engine/input.cpp); Wolf::Update/HandleMessage
 * already select g_pad2 for playerIndex == 1. The second Wolf is spawned by src/game/wolf_clone.cpp when a
 * START_LEVEL message arrives; g_pWolf stays bound to the LOCAL player. */
#ifndef SDW_ENGINE_NET_H
#define SDW_ENGINE_NET_H

#include "sdw_types.h"

struct PadFrame; /* sdw_classes.h */

/* ---- protocol ---- */
enum NetMsg {
    NET_MSG_HELLO       = 1, /* client -> host, at connect: magic + version */
    NET_MSG_START_LEVEL = 2, /* host -> client: level id + start frame + spawn offset of the remote wolf */
    NET_MSG_INPUT       = 3, /* both ways: frame-stamped pad snapshot (see below) */
    NET_MSG_CHECKSUM    = 4, /* both ways: rolling state hash, desync detection */
};

/* INPUT payload, little-endian wire order: u8 msgType, u32 frame, u16 buttons, s8 lx, ly, rx, ry */
#define NET_WOLF_SPAWN_OFFSET 96 /* world units the remote wolf spawns beside the local one */
#define NET_INPUT_DELAY       8   /* frames of look-ahead before an input is simulated: absorbs one RTT */
#define NET_INPUT_DELAY_CATCHUP 8 /* wolf_clone.cpp: extra buffered frames allowed during a scene reload */

/* ---- session ---- */
int  Net_Init(const char *netOpt);   /* "--net-host[=port]" / "--net-join=host[:port]"; 0 ok, <0 disabled/error */
void Net_Shutdown();
int  Net_IsActive();                 /* enet initialised and a session configured */
int  Net_IsHost();                   /* we opened the listening host */
int  Net_IsConnected();              /* the peer channel is up */

/* ---- lockstep clock ---- */
void Net_LockstepGate();             /* Main_Loop, before Time_Update: pump, send, wait for the needed frame */
void Net_SendLocalInput();           /* Input_Poll, after Pad_ReadRaw(&g_pad): queue this frame's snapshot */
int  Net_RemoteInputForFrame(s32 frame, PadFrame *out); /* 1 when the snapshot for `frame` is buffered */

/* ---- level sync / remote wolf ---- */
void Net_NotifyLevelStart(s32 levelId);        /* called where GF_LEVEL_LOADED_A is first set (list.cpp) */
void Net_OnStartLevel(s32 levelId, u32 startFrame); /* receiver side: spawn the remote wolf clone */
extern int g_netRemoteWolfPending;             /* set by the receiver, consumed by Scn_LoadLevelIntoWorld */

/* ---- debug stats (F3 overlay) ---- */
typedef struct NetStats {
    const char *role;      /* "off", "host", "client" */
    int connected;
    int pendingStartLevel; /* START_LEVEL received, waiting for the level to load */
    u32 rttMs;             /* ENet round-trip time estimate */
    s32 localFrame;        /* last frame our input was stamped with */
    s32 remoteFrame;       /* newest remote snapshot buffered */
    int bufferedFrames;    /* remote snapshots waiting to be consumed */
    int gateStalled;       /* the gate had to wait for the peer during the last second */
    int stallEvents;
    u32 localChecksum;
    u32 remoteChecksum;
    int desync;            /* a checksum mismatch was seen */
} NetStats;
void Net_GetStats(NetStats *out);

#endif /* SDW_ENGINE_NET_H */
