#include "romfs_lookup.h"

#include <string.h>

#define RL_MAX_NAME 256

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t* p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

// Header: u64 size (0x50), then offset/size pairs for the directory hash,
// directory meta, file hash and file meta tables, then the data offset.
// File entry: parent, sibling, u64 data offset, u64 size, hash next, name
// size, name (padded to 4). The file meta table is walked linearly.
long rl_read_root_file(rl_read_fn read, void* ctx, const char* name, char* out, size_t cap) {
    uint8_t header[0x50];
    if (cap == 0 || !read(ctx, 0, header, sizeof header) || le64(header) != 0x50) return -1;
    const uint64_t fileMeta = le64(header + 0x38), fileMetaSize = le64(header + 0x40), data = le64(header + 0x48);
    const size_t nameLen = strlen(name);
    if (nameLen >= RL_MAX_NAME) return -1;

    uint64_t at = 0;
    while (at <= fileMetaSize && fileMetaSize - at >= 0x20) {
        uint8_t entry[0x20];
        if (!read(ctx, fileMeta + at, entry, sizeof entry)) return -1;
        const uint32_t parent = le32(entry), entryNameLen = le32(entry + 28);
        const uint64_t offset = le64(entry + 8), size = le64(entry + 16);
        if (entryNameLen > fileMetaSize - at - 0x20) return -1;
        if (parent == 0 && entryNameLen == nameLen) {
            char entryName[RL_MAX_NAME];
            if (!read(ctx, fileMeta + at + 0x20, entryName, nameLen)) return -1;
            if (memcmp(entryName, name, nameLen) == 0) {
                if (size >= cap || (size > 0 && !read(ctx, data + offset, out, (size_t)size))) return -1;
                if (memchr(out, '\0', (size_t)size) != NULL) return -1;
                out[size] = '\0';
                return (long)size;
            }
        }
        at += 0x20 + ((entryNameLen + 3u) & ~3u);
    }
    return -1;
}
