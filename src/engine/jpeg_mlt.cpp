/*
 * The JPEG part is OPTIMISED code under the "gt" optimize pragma.
 *
 * The .MLT string bank: the localised text of a level.
 *
 * An .MLT file is {u32 crc; MltHeader hdr; u32 crc; payload}, read with two File_ReadChecked calls. The header is {char
 * version[4] ('v1.2' in every shipped file); u16 blockCount (7); u16 listCount (0xa9 in Lvl-14.MLT)}. The payload is
 * blockCount language blocks, each of listCount lists, each list {u8 count; count NUL-terminated strings}. Block 0 is
 * French in the shipped files; Mlt_MapLanguageToBlock maps the config language (Progress.language: 0 English, 1
 * Spanish, 2 Italian, 3 Portuguese) to blocks 1, 2, 3, 6. Load_MLT_ParseBank skips to the chosen block, copies just
 * that block into one malloc'd buffer and returns a table of pointers to its lists; the bank lives at g_pDav+0x14
 * (StringBank).
 */

#include "sdw_types.h"
#include "sdw_enums.h"
#include "sdw_classes.h"
#define SDW_INLINE_PROGRESS_GETLANGUAGE 1
#include "progress_inlines.h"
#undef SDW_INLINE_PROGRESS_GETLANGUAGE

#include "../sdk/crt.h"

typedef u8 **JSAMPARRAY;
struct jpeg_decompress_struct;
struct jpeg_error_mgr { /* 0x84 bytes */
    void (*error_exit)(jpeg_decompress_struct *cinfo);
    u8 rest[0x84 - 4];
};
struct jpeg_memory_mgr {
    void *(*alloc_small)(jpeg_decompress_struct *cinfo, int pool, u32 size);
    void *(*alloc_large)(jpeg_decompress_struct *cinfo, int pool, u32 size);
    JSAMPARRAY (*alloc_sarray)(jpeg_decompress_struct *cinfo, int pool, u32 samplesPerRow, u32 numRows);
};
struct jpeg_decompress_struct { /* 0x1d0 bytes */
    jpeg_error_mgr *err;
    jpeg_memory_mgr *mem;
    u8 _pad08[0x70 - 0x08];
    u32 output_width;
    u32 output_height;
    s32 out_color_components;
    s32 output_components;
    u8 _pad80[0x8c - 0x80];
    u32 output_scanline;
    u8 _pad90[0x1d0 - 0x90];
};
#define JPOOL_IMAGE 1
extern "C" jpeg_error_mgr *jpeg_std_error(jpeg_error_mgr *err);
extern "C" void
jpeg_create_decompress(jpeg_decompress_struct *cinfo); /* libjpeg 6 (release 6 takes only cinfo) */
extern "C" void jpeg_stdio_src(jpeg_decompress_struct *cinfo, void *file);
extern "C" int jpeg_read_header(jpeg_decompress_struct *cinfo, int requireImage);
extern "C" int jpeg_start_decompress(jpeg_decompress_struct *cinfo);
extern "C" u32 jpeg_read_scanlines(jpeg_decompress_struct *cinfo, JSAMPARRAY lines, u32 maxLines);
extern "C" int jpeg_finish_decompress(jpeg_decompress_struct *cinfo);
extern "C" void jpeg_destroy_decompress(jpeg_decompress_struct *cinfo);

/* the IJG example's error manager: the libjpeg one plus the jump buffer error_exit returns to */
struct JpegErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf setjmpBuffer;
};

/* ======================================================================================================================
 * the JPEG decoders, OPTIMISED.
 */
#pragma optimize("gt", on)

/* libjpeg error_exit: jumps back to the decoder's setjmp. */
void Jpeg_ErrorExit(jpeg_decompress_struct *cinfo)
{
    longjmp(((JpegErrorMgr *)cinfo->err)->setjmpBuffer, 1);
}

/* decodes a JPEG from file into dest as RGB555 (r bits 10-14, g 5-9, b 0-4), last row first. */
u8 Jpeg_DecodeToRgb555Flipped(void *file, u16 *dest)
{
    jpeg_decompress_struct cinfo;
    JpegErrorMgr jerr;
    JSAMPARRAY buffer;
    s32 rowStride;
    u16 *row;
    u8 *p;
    u32 x;
    u16 r, g, b;
    u8 result = 0;

    if (file != 0) {
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = Jpeg_ErrorExit;
        if (_setjmp(jerr.setjmpBuffer)) {
            jpeg_destroy_decompress(&cinfo);
            return result;
        }
        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, file);
        jpeg_read_header(&cinfo, 1);
        jpeg_start_decompress(&cinfo);
        rowStride = cinfo.output_width * cinfo.output_components;
        buffer = cinfo.mem->alloc_sarray(&cinfo, JPOOL_IMAGE, rowStride, 1);
        row = dest + (cinfo.output_height - 1) * cinfo.output_width;
        while (cinfo.output_scanline < cinfo.output_height) {
            jpeg_read_scanlines(&cinfo, buffer, 1);
            p = buffer[0];
            for (x = 0; x < cinfo.output_width; x++) {
                r = *p++;
                g = *p++;
                b = *p++;
                row[x] = ((r & 0xfff8) << 5 | (g & 0xfff8)) << 2 | (b >> 3);
            }
            row -= cinfo.output_width;
        }
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        result = 1;
    }
    return result;
}

/* decodes a JPEG from file only to report its size: every row is read into a scratch row and dropped. */
u8 Jpeg_GetDimensions(void *file, u32 *widthOut, u32 *heightOut)
{
    jpeg_decompress_struct cinfo;
    JpegErrorMgr jerr;
    JSAMPARRAY buffer;
    s32 rowStride;
    u8 result = 0;

    if (file != 0) {
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = Jpeg_ErrorExit;
        if (_setjmp(jerr.setjmpBuffer)) {
            jpeg_destroy_decompress(&cinfo);
            return result;
        }
        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, file);
        jpeg_read_header(&cinfo, 1);
        jpeg_start_decompress(&cinfo);
        *widthOut = cinfo.output_width;
        *heightOut = cinfo.output_height;
        rowStride = cinfo.output_width * cinfo.output_components; /* the IJG example's row_stride */
        buffer = cinfo.mem->alloc_sarray(&cinfo, JPOOL_IMAGE, rowStride, 1);
        while (cinfo.output_scanline < cinfo.output_height)
            jpeg_read_scanlines(&cinfo, buffer, 1);
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        result = 1;
    }
    return result;
}

#pragma optimize("", on)

/* ======================================================================================================================
 * the .MLT string bank.
 */

/* ---- the game's own helpers ---- */
u16 Str_Length(const char *s);
#include "file.h"
#include "progress.h"
void Debug_Printf(const char *fmt, ...); /* an empty stub in the retail build */

void Mlt_MapLanguageToBlock(u16 *lang);

/* free() a block the caller owns and forget it */
#define MLT_FREE(p) \
    if (p) {        \
        free(p);    \
        (p) = 0;    \
    }

/* skips `block` language blocks of `listCount` lists each, copies the next block into one malloc'd buffer and
 * returns a malloc'd table of listCount pointers to its lists (NULL when listCount is 0). No bounds check: a block index
 * past the file's last block walks off the payload. */
char **Load_MLT_ParseBank(char *data, u16 listCount, u16 block)
{
    char *src;
    u32 k;
    u32 j;
    u32 i;
    char *dst;
    u32 total;
    char **table;
    u8 count;

    total = 0;
    if (listCount == 0)
        return 0;
    table = (char **)malloc(listCount * sizeof(char *));
    Mlt_MapLanguageToBlock(&block);
    for (i = 0; i < block; i++) {
        for (j = 0; j < listCount; j++) {
            count = *data++;
            for (k = 0; k < count; k++)
                data += Str_Length(data) + 1;
        }
    }
    src = data;
    for (i = 0; i < listCount; i++) {
        count = *src++;
        total++;
        for (j = 0; j < count; j++) {
            total += Str_Length(src) + 1;
            src += Str_Length(src) + 1;
        }
    }
    dst = (char *)malloc(total);
    memcpy(dst, data, total);
    data = dst;
    for (i = 0; i < listCount; i++) {
        table[i] = data;
        count = *data++;
        for (j = 0; j < count; j++)
            data += Str_Length(data) + 1;
    }
    return table;
}

/* frees a table made by Load_MLT_ParseBank: lists[0] is the start of the one string buffer. */
void StringBank_FreeLists(char **lists, u16 count)
{
    if (lists) {
        if (count)
            MLT_FREE(lists[0]);
        MLT_FREE(lists);
    }
}

/* loads the .MLT at path into bank for the current config language. 0, or -1 on a missing file or a failed
 * (CRC-checked) read. A file with no payload leaves the bank empty. */
s32 Load_MLT(char *path, StringBank *bank)
{
    FileHandle file;
    u32 maxSize;
    MltHeader hdr;
    char *data;

    data = 0;
    if (File_Open(path, &file) <= 0) {
        Debug_Printf("Load_MLT: File not found\n");
        return -1;
    }
    maxSize = 40000; /* written, never read */
    if (File_ReadChecked(&file, &hdr, 8) < 0) {
        Debug_Printf("Load_MLT: File Read Error\n");
        File_Close(&file);
        return -1;
    }
    if (file.remaining > 4) {
        data = (char *)malloc(file.remaining - 4);
        if (File_ReadChecked(&file, data, file.remaining - 4) < 0) {
            Debug_Printf("Load_MLT: Loading remainder\n");
            goto failed;
        }
        if (g_pProgress->GetLanguage() >= hdr.blockCount)
            Debug_Printf("\nLoad_MLT: (WARNING) Current language not supported\n");
        bank->listCount = hdr.listCount;
        bank->lists = Load_MLT_ParseBank(data, bank->listCount, g_pProgress->GetLanguage());
        MLT_FREE(data);
    } else {
        bank->listCount = 0;
        bank->lists = 0;
    }
    File_Close(&file);
    return 0;
failed:
    MLT_FREE(data);
    File_Close(&file);
    return -1;
}

/* frees the bank and zeroes it. Always 0. */
s32 Load_FreeMLT(StringBank *bank)
{
    if (bank) {
        StringBank_FreeLists(bank->lists, bank->listCount);
        bank->listCount = 0;
        bank->lists = 0;
    }
    return 0;
}

/* config language (0 English, 1 Spanish, 2 Italian, 3 Portuguese) -> .MLT block (1, 2, 3, 6), in place. */
void Mlt_MapLanguageToBlock(u16 *lang)
{
    switch (*lang) {
        case GAME_LANG_ENGLISH:
            *lang = MLT_BLOCK_ENGLISH;
            break;
        case GAME_LANG_SPANISH:
            *lang = MLT_BLOCK_SPANISH;
            break;
        case GAME_LANG_ITALIAN:
            *lang = MLT_BLOCK_ITALIAN;
            break;
        case GAME_LANG_BRAZILIAN:
            *lang = MLT_BLOCK_BRAZILIAN;
            break;
    }
}
