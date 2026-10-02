/*
 * The .WAR loader, mesh pass: Load_DAVnWAR calls Load_WarMeshes right after Load_WAR with the same .WAR path (and
 * ignores the result). It opens the file a second time as a Black Sheep "V2.6" file (BsFile), walks the WAR resource
 * table and the BsFile's frame directory in step, and for every mesh-type resource (WAR_RES_MESH, WAR_RES_MODEL,
 * WAR_RES_MESH_B, WAR_RES_SKY; bit 0x40 ignored) builds a Mesh from the current BsFile frame into the mesh cache
 * g_texObjects[g_texCount], keyed in g_texKeys by the resource's address in the WAR blob. 1 on success; 0 for a
 * WAR_RES_IGNORED_1 / _2 or unknown resource (the meshes built so far stay in the cache).
 * */
#define SDW_MEMBERS_BsFile BsFile(const char *path); /* BsFile_Ctor */
#define SDW_MEMBERS_Mesh Mesh();                     /* Mesh_Construct */
#include "sdw_classes.h"
#include "sdw_enums.h"

#include "draw2d.h"
#include "tex_table.h"

#define WAR_RES_TYPE(e) ((e) >> 24 & 0xff)
#define WAR_RES_OFFSET(e) ((e) & 0xffffff)

/* ----: the angle / scale / texture-coordinate helpers ---- */

/* clockwise 1/4096-turn angle, adjusted once if negative. */
float Math_Angle4096ToRadians(s16 angle)
{
    float radians = -(angle * 2.0f * 3.14159265358979f) / 4096.0f;
    if (radians < 0.0f)
        radians += 6.283185307179586f;
    return radians;
}

/* signed-byte rotation-key counterpart. */
float Math_Angle128ToRadians(s8 angle)
{
    float radians = -(angle * 2.0f * 3.14159265358979f) / 128.0f;
    if (radians < 0.0f)
        radians += 6.283185307179586f;
    return radians;
}

/* and: unsigned packed scale keys. */
float Math_U16ToUnitFloat(u16 value)
{
    return value / 1024.0f;
}
float Math_U8ToUnitFloat(u8 value)
{
    return value / 128.0f;
}

/* half-texel-corrected corner on a 256-pixel texture page. */
float Tex_CornerUV(u32 offset, s32 size, s32 base)
{
    return (offset * ((size - 1) / (float)size) + 0.5f + base) / 256.0f;
}

/* ----: the mesh pass ---- */

int Load_WarMeshes(const char *path, WarFile *war)
{
    u32 i;
    BsFile *bsFile;

    bsFile = new BsFile(path);
    bsFile->FirstFrame();
    g_texCount = 0;
    for (i = 0; i < war->header->resourceCount; i++) {
        switch (WAR_RES_TYPE(war->table[i]) & WAR_RES_TYPE_MASK) {
            case WAR_RES_IGNORED_1:
            case WAR_RES_IGNORED_2:
                goto fail;
            case WAR_RES_MESH:
            case WAR_RES_MODEL:
            case WAR_RES_MESH_B:
            case WAR_RES_SKY:
                g_texObjects[g_texCount] = new Mesh;
                g_texObjects[g_texCount]->BuildFromBsFile(g_pD3DAppMain, bsFile);
                g_texKeys[g_texCount] = WAR_RES_OFFSET(war->table[i]) + (uptr)war->blob;
                g_texCount++;
                break;
            case WAR_RES_SCENARIC:
            case WAR_RES_TYPE_8:
            case WAR_RES_TYPE_9:
            case WAR_RES_CINEMATIC:
            case WAR_RES_TYPE_27:
            case WAR_RES_COLL_GRID:
            case WAR_RES_COLL_TRIS:
            case WAR_RES_EXPORTS:
            case WAR_RES_HEADER3:
            case WAR_RES_OBJ_GRID:
            case WAR_RES_PAIRS:
            case WAR_RES_ANIM_NAMES:
                break;
            default:
                goto fail;
        }
        bsFile->NextFrame();
    }
    delete bsFile;
    return 1;
fail:
    delete bsFile;
    return 0;
}

/* ----: Load_FreeWarMeshes ---- */
/* The unused WarFile argument is present in the existing loader caller. */
void Load_FreeWarMeshes(WarFile *war)
{
    u32 index;
    for (index = 0; index < g_texCount; ++index)
        if (g_texObjects[index])
            delete g_texObjects[index];
}
