#ifndef SDW_ENGINE_TIME_H
#define SDW_ENGINE_TIME_H

/* The functions and globals time.cpp defines, declared once for every file that uses them. */
#include "sdw_types.h"

class Timer;

extern s32 g_dtRaw;
extern u8 g_framesThisSecond;
extern Timer *g_pTimer;
extern u32 g_rawTime;
extern s32 g_rawTimeMs;
extern s32 g_renderWorldFlag;
void Time_Init();
void Time_Pause();
void Time_Resume();
void Time_Update();

#endif
