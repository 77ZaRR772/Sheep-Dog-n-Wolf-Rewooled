#define SDW_MEMBERS_WaveFile                                                                                    \
    WaveFile(); /* WaveFile_Construct */ /* The two halves of Open, forced inline (see the header). */ \
    __forceinline s32 OpenDisk(char *name);                                                                     \
    __forceinline s32 OpenMemory(char *name, u32 size);
#include "sdw_classes.h"

#include "../sdk/win32.h"
#include "../sdk/mmsystem.h"
#include "../sdk/crt.h"

/* the RIFF WAVE chunk ids, named here: the SDK has no constants for them (its samples write mmioFOURCC at each use) */
#define FOURCC_DATA mmioFOURCC('d', 'a', 't', 'a')
#define FOURCC_WAVE mmioFOURCC('W', 'A', 'V', 'E')
#define FOURCC_FMT mmioFOURCC('f', 'm', 't', ' ')

void *operator new(uptr);
void operator delete(void *);

/* ================================================================ WaveFile (vtable), the DX7 CWaveSoundRead */

WaveFile::WaveFile()
{
    format = 0;
}

WaveFile::~WaveFile()
{
    Close();
    if (format) {
        delete format;
        format = 0;
    }
}

/* opens a .wav file (fromMemory 0) or `size` bytes of RIFF image at name (fromMemory 1, the 'MEM ' IOProc), parses the
 * header into ckRiff / format and descends to the 'data' chunk. */
__forceinline s32 WaveFile::OpenDisk(char *name)
{
    s32 hr = E_FAIL;
    HMMIO h;

    h = mmioOpenA(name, 0, MMIO_ALLOCBUF | MMIO_READ);
    if (h) {
        if ((hr = ReadRiffHeader(h, &ckRiff, &format)) >= 0) {
            hmmio = h;
            hr = S_OK;
        } else {
            mmioClose(h, 0);
        }
    }
    return hr;
}

__forceinline s32 WaveFile::OpenMemory(char *name, u32 size)
{
    s32 hr = E_FAIL;
    HMMIO h;
    MMIOINFO info;

    memset(&info, 0, sizeof info);
    info.pchBuffer = name;
    info.cchBuffer = size;
    info.fccIOProc = FOURCC_MEM;
    h = mmioOpenA(0, &info, MMIO_READ);
    if (h) {
        if ((hr = ReadRiffHeader(h, &ckRiff, &format)) >= 0) {
            hmmio = h;
            hr = S_OK;
        } else {
            mmioClose(h, 0);
        }
    }
    return hr;
}

s32 WaveFile::Open(char *name, u8 fromMemory, u32 size)
{
    s32 hr;

    Close();
    if (format) {
        delete format;
        format = 0;
    }
    if (!fromMemory)
        hr = OpenDisk(name);
    else
        hr = OpenMemory(name, size);
    if (hr >= 0)
        hr = ResetFile();
    return hr;
}

/* seeks back to the start of the RIFF payload and descends into 'data' again (restoring ckData.cksize). */
s32 WaveFile::ResetFile()
{
    MMCKINFO *riff = &ckRiff;
    MMCKINFO *ck = &ckData;
    s32 hr = E_FAIL;

    if (mmioSeek(hmmio, riff->dwDataOffset + 4, SEEK_SET) != -1) {
        ck->ckid = FOURCC_DATA;
        if (mmioDescend(hmmio, ck, riff, MMIO_FINDCHUNK) == 0)
            return S_OK;
    }
    return hr;
}

s32 WaveFile::Read(u32 size, u8 *dest, u32 *read)
{
    return ReadMmio(hmmio, size, dest, &ckData, read);
}

/* closes the handle but neither frees `format` nor clears hmmio. */
s32 WaveFile::Close()
{
    mmioClose(hmmio, 0);
    return S_OK;
}

/* the DX7 WaveReadFile: copies up to min(size, ck->cksize) bytes through the mmio buffer. */
s32 WaveFile::ReadMmio(HMMIO h, u32 size, u8 *dest, MMCKINFO *ck, u32 *read)
{
    MMIOINFO info;
    s32 hr = E_FAIL;
    u32 n;
    u32 i;

    *read = 0;
    if (mmioGetInfo(h, &info, 0) == 0) {
        n = size;
        if (n > ck->cksize)
            n = ck->cksize;
        ck->cksize -= n;
        for (i = 0; i < n; i++) {
            if (info.pchNext == info.pchEndRead) {
                if (mmioAdvance(h, &info, MMIO_READ) != 0)
                    return E_FAIL;
                if (info.pchNext == info.pchEndRead)
                    return E_FAIL;
            }
            dest[i] = *info.pchNext;
            info.pchNext++;
        }
        if (mmioSetInfo(h, &info, 0) == 0) {
            *read = n;
            hr = S_OK;
        }
    }
    return hr;
}

s32 WaveFile::ReadRiffHeader(HMMIO__ *h, MMCKINFO *riff, WAVEFORMATEX **format)
{
    s32 result = E_FAIL;
    PCMWAVEFORMAT pcm;
    MMCKINFO chunk;
    *format = 0;
    if (mmioDescend(h, riff, 0, 0) == 0 && riff->ckid == FOURCC_RIFF && riff->fccType == FOURCC_WAVE) {
        chunk.ckid = FOURCC_FMT;
        if (mmioDescend(h, &chunk, riff, MMIO_FINDCHUNK) == 0 && chunk.cksize >= 16 &&
            mmioRead(h, (char *)&pcm, 16) == 16) {
            if (pcm.wFormatTag == WAVE_FORMAT_PCM) {
                *format = (WAVEFORMATEX *)operator new(18);
                if (*format) {
                    *(PCMWAVEFORMAT *)*format =
                        pcm;
                    (*format)->cbSize = 0;
                    result = S_OK;
                }
            } else {
                u32 extra = 0;
                if (mmioRead(h, (char *)&extra, 2) == 2) {
                    *format = (WAVEFORMATEX *)operator new(18 + extra);
                    if (*format) {
                        *(PCMWAVEFORMAT *)*format =
                            pcm;
                        (*format)->cbSize = (u16)extra;
                        if (mmioRead(h, (char *)*format + 18, extra) != extra) {
                            operator delete(*format);
                            *format = 0;
                        } else
                            result = S_OK;
                    }
                }
            }
            if (result == S_OK && mmioAscend(h, &chunk, 0) != 0) {
                operator delete(*format);
                result = E_FAIL;
                *format = 0;
            }
        }
    }
    return result;
}
