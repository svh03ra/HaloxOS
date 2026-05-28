// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: system_specs.c, system specifications handling.
// This repository is licensed under the GNU General Public License.

static uint32_t detect_total_ram_bytes(const MultibootInfo *mbi) {
    if (mbi != NULL && (mbi->flags & (1u << 6)) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0) {
        uint64_t total = 0;
        uintptr_t cursor = (uintptr_t)mbi->mmap_addr;
        uintptr_t end = cursor + mbi->mmap_length;

        while (cursor + sizeof(uint32_t) <= end) {
            const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;
            if (cursor + entry->size + sizeof(uint32_t) > end) {
                break;
            }
            if (entry->type == 1u || entry->type == 3u) {
                total += entry->length;
            }
            cursor += entry->size + sizeof(uint32_t);
        }

        if (total > 0xFFFFFFFFu) {
            total = 0xFFFFFFFFu;
        }
        if (total >= 4u * 1024u * 1024u) {
            return (uint32_t)total;
        }
    }

    if (mbi != NULL && (mbi->flags & 1u) != 0) {
        uint32_t total_kb = mbi->mem_lower + mbi->mem_upper;
        return total_kb * 1024u;
    }

    return 32u * 1024u * 1024u;
}
