/* The RIFF/WAVE subset of WINMM's mmio functions that WaveFile uses, over stdio (macOS, Linux). */
#include <stdio.h>
#include <string.h>

#include <mutex>
#include <unordered_map>

#include "../sdk/mmsystem.h"

extern "C" void Sdw_ResolvePath(const char *path, char *out, size_t size); /* crt_posix_compat.cpp */

struct MMCKINFO {
    u32 ckid;
    u32 cksize;
    u32 fccType;
    u32 dwDataOffset;
    u32 dwFlags;
};

struct MmioFile {
    FILE *file;
    const unsigned char *memory;
    u32 memorySize;
    u32 position;
    unsigned char buffer[4096];
    char *bufferNext;
    char *bufferEnd;
    u32 bufferStart;
    u8 fromMemory;
};

/* An HMMIO is a number, never reused, looked up in this table, as Windows validates its handles: WaveFile::Close (as in
 * the original) closes without forgetting the handle, and WaveFile::Open closes again, which Windows answers with an
 * error. Deleting the same object twice would corrupt the heap. */
struct MmioTable {
    std::mutex lock;
    std::unordered_map<uptr, MmioFile *> files;
    uptr nextId = 1;
};

/* made on first use and never destroyed: global destructors in other files (g_streamWaves) still close handles at exit */
static MmioTable &Mmio()
{
    static MmioTable *table = new MmioTable;
    return *table;
}

static HMMIO MmioRegister(MmioFile *h)
{
    MmioTable &t = Mmio();
    std::lock_guard<std::mutex> lock(t.lock);
    uptr id = t.nextId++;
    t.files[id] = h;
    return (HMMIO)id;
}

static MmioFile *MmioLookup(HMMIO handle, bool remove = false)
{
    MmioTable &t = Mmio();
    std::lock_guard<std::mutex> lock(t.lock);
    auto it = t.files.find((uptr)handle);
    if (it == t.files.end())
        return 0;
    MmioFile *h = it->second;
    if (remove)
        t.files.erase(it);
    return h;
}

static int MmioSeek(MmioFile *h, u32 position)
{
    if (!h || (h->fromMemory && position > h->memorySize))
        return -1;
    if (h->file && fseek(h->file, (long)position, SEEK_SET) != 0)
        return -1;
    h->position = position;
    h->bufferNext = h->bufferEnd = (char *)h->buffer;
    return (int)position;
}

static int MmioReadBytes(MmioFile *h, void *dst, u32 count)
{
    u32 available;
    if (!h)
        return -1;
    if (h->fromMemory) {
        if (h->position > h->memorySize)
            return -1;
        available = h->memorySize - h->position;
        if (count > available)
            count = available;
        memcpy(dst, h->memory + h->position, count);
    } else {
        /* the FILE may be past h->position: mmioAdvance reads a whole buffer ahead */
        if (fseek(h->file, (long)h->position, SEEK_SET) != 0)
            return -1;
        count = (u32)fread(dst, 1, count, h->file);
    }
    h->position += count;
    h->bufferNext = h->bufferEnd = (char *)h->buffer; /* a direct read leaves no buffered data */
    return (int)count;
}

extern "C" HMMIO __stdcall mmioOpenA(char *name, MMIOINFO *info, DWORD)
{
    MmioFile *h = new MmioFile();
    memset(h, 0, sizeof(*h));
    if (info && info->fccIOProc == mmioFOURCC('M', 'E', 'M', ' ')) {
        h->fromMemory = 1;
        h->memory = (const unsigned char *)info->pchBuffer;
        h->memorySize = (u32)info->cchBuffer;
        h->bufferNext = h->bufferEnd = (char *)h->buffer;
        return MmioRegister(h);
    }
    char nativePath[4096];
    if (!name) {
        delete h;
        return 0;
    }
    Sdw_ResolvePath(name, nativePath, sizeof(nativePath));
    if (!(h->file = fopen(nativePath, "rb"))) {
        delete h;
        return 0;
    }
    return MmioRegister(h);
}

extern "C" MMRESULT __stdcall mmioClose(HMMIO handle, UINT)
{
    MmioFile *h = MmioLookup(handle, true);
    if (!h)
        return 5; /* MMSYSERR_INVALHANDLE: not open, or already closed */
    if (h->file)
        fclose(h->file);
    delete h;
    return 0;
}

extern "C" LONG __stdcall mmioRead(HMMIO handle, char *dst, LONG count)
{
    MmioFile *h = MmioLookup(handle);
    return count < 0 ? -1 : (LONG)MmioReadBytes(h, dst, (u32)count);
}

extern "C" LONG __stdcall mmioSeek(HMMIO handle, LONG offset, int origin)
{
    MmioFile *h = MmioLookup(handle);
    s64 target;
    if (!h)
        return -1;
    if (origin == SEEK_SET)
        target = offset;
    else if (origin == SEEK_CUR)
        target = (s64)h->position + offset;
    else if (origin == SEEK_END && h->fromMemory)
        target = (s64)h->memorySize + offset;
    else
        return -1;
    if (target < 0 || target > 0xffffffffLL)
        return -1;
    return (LONG)MmioSeek(h, (u32)target);
}

extern "C" MMRESULT __stdcall mmioDescend(HMMIO handle, MMCKINFO *chunk, const MMCKINFO *parent, UINT flags)
{
    MmioFile *h = MmioLookup(handle);
    u32 start, id, size, type;
    if (!h || !chunk)
        return 1;
    if (!parent && flags == 0) {
        start = h->position;
        if (MmioReadBytes(h, &id, 4) != 4 || MmioReadBytes(h, &size, 4) != 4)
            return 1;
        chunk->ckid = id;
        chunk->cksize = size;
        chunk->fccType = 0;
        chunk->dwDataOffset = start + 8;
        chunk->dwFlags = 0;
        if (id == mmioFOURCC('R', 'I', 'F', 'F') || id == mmioFOURCC('L', 'I', 'S', 'T')) {
            if (MmioReadBytes(h, &type, 4) != 4)
                return 1;
            chunk->fccType = type;
        }
        return 0;
    }
    if (!parent || flags != MMIO_FINDCHUNK)
        return 1;
    while (h->position + 8 <= parent->dwDataOffset + parent->cksize) {
        start = h->position;
        if (MmioReadBytes(h, &id, 4) != 4 || MmioReadBytes(h, &size, 4) != 4)
            return 1;
        if (id == chunk->ckid) {
            chunk->ckid = id;
            chunk->cksize = size;
            chunk->fccType = 0;
            chunk->dwDataOffset = start + 8;
            chunk->dwFlags = 0;
            return 0;
        }
        if (MmioSeek(h, start + 8 + size + (size & 1)) < 0)
            return 1;
    }
    return 1;
}

extern "C" MMRESULT __stdcall mmioAscend(HMMIO handle, MMCKINFO *chunk, UINT)
{
    MmioFile *h = MmioLookup(handle);
    if (!h || !chunk || MmioSeek(h, chunk->dwDataOffset + chunk->cksize + (chunk->cksize & 1)) < 0)
        return 1;
    return 0;
}

extern "C" MMRESULT __stdcall mmioGetInfo(HMMIO handle, MMIOINFO *info, UINT)
{
    MmioFile *h = MmioLookup(handle);
    if (!h || !info)
        return 1;
    info->pchBuffer = (char *)h->buffer;
    h->bufferStart = h->position;
    info->pchNext = h->bufferNext = (char *)h->buffer;
    info->pchEndRead = h->bufferEnd = (char *)h->buffer;
    info->lBufOffset = (s32)h->position;
    info->hmmio = handle;
    return 0;
}

extern "C" MMRESULT __stdcall mmioAdvance(HMMIO handle, MMIOINFO *info, UINT)
{
    MmioFile *h = MmioLookup(handle);
    u32 count;
    if (!h || !info)
        return 1;
    /* The next buffer follows the one just consumed: callers advance repeatedly and only mmioSetInfo at the end. */
    if (h->bufferEnd != (char *)h->buffer)
        h->position = h->bufferStart + (u32)(h->bufferEnd - (char *)h->buffer);
    h->bufferStart = h->position;
    if (h->fromMemory) {
        count = h->memorySize - h->position;
        if (count > sizeof(h->buffer))
            count = sizeof(h->buffer);
        memcpy(h->buffer, h->memory + h->position, count);
    } else {
        fseek(h->file, (long)h->position, SEEK_SET);
        count = (u32)fread(h->buffer, 1, sizeof(h->buffer), h->file);
    }
    h->bufferNext = (char *)h->buffer;
    h->bufferEnd = (char *)h->buffer + count;
    info->pchNext = h->bufferNext;
    info->pchEndRead = h->bufferEnd;
    info->pchBuffer = (char *)h->buffer;
    return 0;
}

extern "C" MMRESULT __stdcall mmioSetInfo(HMMIO handle, const MMIOINFO *info, UINT)
{
    MmioFile *h = MmioLookup(handle);
    if (!h || !info || info->pchNext < (char *)h->buffer || info->pchNext > (char *)h->bufferEnd)
        return 1;
    h->position = h->bufferStart + (u32)(info->pchNext - (char *)h->buffer);
    h->bufferNext = info->pchNext;
    return 0;
}
