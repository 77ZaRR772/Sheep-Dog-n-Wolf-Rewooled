#ifndef SDW_ENGINE_TEX_SCROLL_H
#define SDW_ENGINE_TEX_SCROLL_H

/* The functions and globals tex_scroll.cpp defines, declared once for every file that uses them. */
#include "sdw_types.h"

struct DavBitmapRec;

void TexScroll_AddRect(const DavBitmapRec *rec, s8 step);
void TexScroll_FreeAll();
void TexScroll_Init();
void TexScroll_UpdateAll();

#endif
