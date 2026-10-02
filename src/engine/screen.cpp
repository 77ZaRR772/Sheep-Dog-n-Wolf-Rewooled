#include "sdw_types.h"
#include "../sdk/d3d7.h"
class Mat44;

#include "../sdk/crt.h"

#define SDW_MEMBERS_Screen                    \
    Screen(); /* Screen_Construct */ \
    SamScreenGeometry *GetGeometry(SamScreenGeometry *out);

#define SDW_MEMBERS_D3DApp void SetTransform(u32 state, Mat44 *m); /* inline */
struct SamScreenGeometry {
    unsigned short width, height, x, y, aspect;
}; /* as src/objects/sam.cpp */
#include "sdw_classes.h"
#define SDW_INLINE_SCREEN_LAYERS4_U16 1
#define SDW_INLINE_SCREEN_LAYERS60_U16 1
#include "screen_inlines.h"
#undef SDW_INLINE_SCREEN_LAYERS4_U16
#undef SDW_INLINE_SCREEN_LAYERS60_U16
#define SDW_INLINE_D3DAPP_SETTRANSFORM_U32_MAT44 1
#include "../app/d3dapp_inlines.h"
#undef SDW_INLINE_D3DAPP_SETTRANSFORM_U32_MAT44

#include "fixed_math.h"
#include "draw2d.h"

/* ---- globals ---- */
extern u32 *g_screenLayerBase;

/* ---- the virtual screen (512 x 240, the PS1 resolution) ---- */
Screen g_screen;

Screen::Screen()
{
    scratch4 = 0;
    scratch60 = 0;
}

Screen::~Screen()
{
    if (scratch4)
        free(scratch4);
    if (scratch60)
        free(scratch60);
}

/* once the device is up: viewport from the frustum's size, virtual screen 512 x 240, projection distance
 * 384, and the two layer-handle tables. 0 when the device is not ready. */
s32 Screen::Init(s32 unused)
{
    if (!g_pD3DAppMain->deviceReady)
        return 0;
    viewportY = 0;
    viewportX = 0;
    viewportWidth = (s16)g_pViewFrustum->viewportWidth;
    viewportHeight = (s16)g_pViewFrustum->viewportHeight;
    virtWidth = 0x200;
    virtHeight = 0xf0;
    SetProjection(0x180);
    scratch4 = malloc(4);
    scratch60 = malloc(0x60);
    return 1;
}

/* a virtual-screen x in viewport pixels. */
float Screen::ScaleX(s32 x)
{
    return x * g_pViewFrustum->viewportWidth / 512.0f;
}

/* a virtual-screen y in viewport pixels. */
float Screen::ScaleY(s32 y)
{
    return y * g_pViewFrustum->viewportHeight / 240.0f;
}

/* the index of a layer handle (g_screenLayerBase + index). */
u16 Screen::LayerIndex(u32 *layer)
{
    return (u16)(((uptr)layer - (uptr)g_screenLayerBase) >> 2);
}

/* the depth of a 2D layer: index / 120. */
float Screen::Draw2D_LayerToZ(u32 *layer)
{
    return LayerIndex(layer) / 120.0f;
}

/* the virtual screen's size, origin 0,0 and 1/aspect in 4.12. */
SamScreenGeometry *Screen::GetGeometry(SamScreenGeometry *out)
{
    SamScreenGeometry geo;
    geo.width = 0x200;
    geo.height = 0xf0;
    geo.x = geo.y = 0;
    geo.aspect = Math_FloatToFixed12_s16(1.0f / g_pViewFrustum->aspect);
    *out = geo;
    return out;
}

/* clears target and z-buffer to color. */
void Screen::Clear(u32 color)
{
    D3DApp *app = g_pD3DAppMain;
    SDW_RD(app->pD3DDevice)->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 1.0f, 0);
}

/* the PS1-style projection distance: horizontal fov from tan = 256 / dist, aspect = width / height, and
 * the projection matrix (y flipped) pushed to the device. */
void Screen::SetProjection(s32 dist)
{
    float tanHalf;
    projDist = dist;
    tanHalf = 512.0f / (projDist * 2.0f);
    g_pViewFrustum->SetFovFromTan(tanHalf);
    g_pViewFrustum->aspect = g_pViewFrustum->viewportWidth / g_pViewFrustum->viewportHeight;
    g_pViewFrustum->BuildProjectionMatrix(&g_projMatrix);
    g_projMatrix.m[1][1] = -g_projMatrix.m[1][1];
    g_pD3DAppMain->SetTransform(D3DTRANSFORMSTATE_PROJECTION, &g_projMatrix);
}
