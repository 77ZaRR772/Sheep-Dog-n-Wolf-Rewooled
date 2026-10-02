#include "sdw_types.h"
#include "../sdk/crt.h"

/* ------------------------------------------------------------------------------------------------ file helpers */

/* the file's length, leaving its position where it was. */
u32 Bs_FileSize(FILE *f)
{
    long n;
    long pos;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, pos, SEEK_SET);
    return n;
}

/* reads a whole file into a new[]'d buffer; NULL with *sizeOut = 0 when it cannot be opened. */
void *Bs_LoadFile(const char *path, u32 *sizeOut)
{
    void *buf;
    FILE *f;

    f = fopen(path, "rb");
    if (f == 0) {
        *sizeOut = 0;
        return 0;
    }
    *sizeOut = Bs_FileSize(f);
    buf = new char[*sizeOut];
    fread(buf, 1, *sizeOut, f);
    fclose(f);
    return buf;
}

/* writes a buffer to a file (mode "w"). No callers. */
void Bs_WriteFile(const char *path, const void *buf, u32 size)
{
    FILE *f;

    f = fopen(path, "w");
    fwrite(buf, 1, size, f);
    fclose(f);
}
