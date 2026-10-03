/* wolf_clone.cpp -- the second, network-driven Wolf ("clone" of the level's Ralph).
 *
 * Flow: net.cpp receives START_LEVEL from the host and calls WolfClone_OnStartLevelReceived(). On the client that
 * triggers a local load of the same scene (Game_ChangeScene); on the host the level is already running. When the
 * level is loaded on either side, WolfClone_Update() (hooked into Game_Frame_2) builds the remote wolf:
 *   - Scenaric_CreateObject(CLASSID_WOLF, record) runs the registered factory Wolf_Create: full model sets, vtable,
 *     particle trails, prop/splash bodies -- identical to the single-player wolf. The factory overwrites g_pWolf and
 *     zeroes g_wolfInstanceCount; we restore g_pWolf to the LOCAL wolf (the first Wolf in the object list) so camera,
 *     HUD, goals and checkpoints keep following us, and force the clone to playerIndex = 1 so Wolf::Update /
 *     StateMachine / HandleMessage read g_pad2 -- which Input_Poll feeds from the lockstep ring (input.cpp).
 *   - PostLoadInit() gives it movement banks, collision boxes, ground snap and respawn state. Its tail aims the
 *     shared camera and fades the screen: those are re-asserted for the LOCAL wolf right after.
 *   - It spawns NET_WOLF_SPAWN_OFFSET units beside the local wolf. */
/* wolf.h must be the FIRST sdw_classes consumer in this TU: it defines the SDW_MEMBERS_* hooks and
 * includes sdw_classes.h itself (the header's include guard then keeps the bare include below inert). */
#include "wolf.h" /* class Wolf + its inline members -- this TU is part of the wolf module */

#include "sdw_types.h"
#include "sdw_enums.h"
#include "sdw_classes.h"
#include "../engine/scenaric.h"        /* Scenaric_CreateObject, AddToWorld via classes */
#include "../engine/object_lookup.h"   /* Scenaric_FindByClass */
#include "../engine/net.h"
#include "../app/app_main.h" /* Camera g_camera */

extern u32 g_gameFlags;      /* sdw_enums GameFlags: GF_LEVEL_LOADED_A/B */
extern s32 g_frameCount;

static Wolf *s_remoteWolf = NULL;
static int s_spawnPending = 0;
static int s_changeSceneRequested = 0;
static s32 s_startLevel = -1;

/* ---- hook called by net.cpp when a START_LEVEL message arrives ---- */
void WolfClone_OnStartLevelReceived(s32 levelId, u32 startFrame)
{
    (void)startFrame;
    /* the host's Game_Frame_2 announces the scene every frame while connected: only react to a real change */
    if (Net_IsHost()) {
        if (s_startLevel == levelId && (s_spawnPending || s_remoteWolf))
            return;
    }
    s_spawnPending = 1;
    s_startLevel = levelId;
    if (!Net_IsHost())
        s_changeSceneRequested = 1; /* the client must load the announced scene first */
}

Wolf *Net_FindRemoteWolf()
{
    return s_remoteWolf;
}

/* drop the clone reference when the world is torn down between levels */
void WolfClone_ResetForNewLevel()
{
    s_remoteWolf = NULL;
    s_startLevel = -1; /* the next START_LEVEL (or the host's per-frame announce) re-arms the spawn */
}

/* first Wolf in the object list == the one the level loader created == the LOCAL player's */
static Wolf *Net_FindLocalWolf()
{
    ScnObject *found[2];
    s32 n = Scenaric_FindByClass(CLASSID_WOLF, found, 2);
    if (n <= 0)
        return NULL;
    return (Wolf *)found[0];
}

static void WolfClone_Spawn()
{
    s_spawnPending = 0;

    Wolf *local = Net_FindLocalWolf();
    if (!local)
        return;
    if (s_remoteWolf && s_remoteWolf->IsInWorld())
        return; /* already present (host answering a late join) */

    /* clone the level's own Wolf WAR record so models, props and fly/restriction boxes match exactly */
    u8 *rec = (u8 *)local->record;
    Wolf *remote = (Wolf *)Scenaric_CreateObject(CLASSID_WOLF, rec);
    if (!remote)
        return;

    /* undo the factory's single-player assumptions */
    g_pWolf = local;            /* g_pWolf must stay bound to the LOCAL player (~1079 refs incl. goal/checkpoint) */
    g_wolfInstanceCount = 1;    /* the next factory call would assign playerIndex 1 anyway */
    remote->playerIndex = 1;    /* Update/StateMachine/HandleMessage pick g_pad2 with this */
    remote->camOverride = 0xFF; /* never claims the camera */
    extern void Hud_ResetActionPrompt(); /* game HUD: re-bind the prompt to the local wolf */
    Hud_ResetActionPrompt();

    Vec3s spawnPos = local->pos;
    spawnPos.x += NET_WOLF_SPAWN_OFFSET;
    remote->AddToWorld(&spawnPos);
    remote->PostLoadInit(); /* runs the full init; its tail touches the shared camera/fade -- fixed below */

    /* PostLoadInit aimed the camera at the clone and started its fade; re-bind both to the local wolf */
    Camera_Reset();
    g_camera.rot.y = (s16)(-local->Facing() & 0xfff);
    extern void Fade_StartOut(s32 ticks);
    Fade_StartOut(0x1000); /* matches the normal level-start fade-out */

    remote->savedRespawnPos = spawnPos;
    remote->savedRespawnFacing = remote->Facing();
    s_remoteWolf = remote;
}

/* per-frame hook (Game_Frame_2): drive the scene change and the delayed spawn */
void WolfClone_Update()
{
    if (s_changeSceneRequested && !Net_IsHost()) {
        s_changeSceneRequested = 0;
        if (!(g_gameFlags & GF_LEVEL_LOADED_A)) {
            extern int Net_RemoteInputReadyForFrame(s32 frame);
            extern s32 g_frameCount;
            /* the gate needs one buffered remote snapshot to keep simulating; the reload below takes several
             * seconds of loading -- wait until the peer's inputs have caught up so the game does not stall */
            if (!Net_RemoteInputReadyForFrame(g_frameCount + NET_INPUT_DELAY_CATCHUP))
                return;
            /* GotoScene on the very same scene short-circuits inside Progress::GotoScene (same path -> no reload),
             * so force the full free+reload that also re-seeds the RNG and Time_Init */
            extern void Game_FreeLevel();
            extern void Game_ReloadLevel();
            Game_FreeLevel();
            Game_ReloadLevel();
        }
    }
    if (!s_spawnPending)
        return;
    if (!(g_gameFlags & GF_LEVEL_LOADED_A) || !(g_gameFlags & GF_LEVEL_LOADED_B))
        return; /* wait until the scene is fully installed */
    WolfClone_Spawn();
}
