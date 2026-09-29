// Reads one file of the root directory of a RomFS (level 3 data, as
// fsOpenDataStorageByCurrentProcess returns it) without allocating: the
// forwarder stub runs on a 16 KiB heap. Shared with RetroManager's host tests.
#ifndef RM_ROMFS_LOOKUP_H
#define RM_ROMFS_LOOKUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads exactly size bytes at offset; false on any failure.
typedef bool (*rl_read_fn)(void* ctx, uint64_t offset, void* buf, size_t size);

// Copies the file `name` of the root directory into out and NUL-terminates
// it. Returns its length, or -1 if it is missing, unreadable, contains a NUL
// byte or does not fit in cap - 1 bytes.
long rl_read_root_file(rl_read_fn read, void* ctx, const char* name, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
