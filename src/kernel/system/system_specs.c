// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: system_specs.c, system specifications handling.

// This repository is licensed under the GNU General Public License.

/*
 * Highest address the boot chain may hand out.
 *
 * This used to SUM the usable E820 lengths.  That is a memory *quantity*,
 * not an address: on a 5 MB machine the conventional-memory hole plus the
 * firmware reservation at the top turn 5.1 MB of mapped RAM into a 4.5 MB
 * sum, and every allocator here treats the result as an ADDRESS ceiling
 * (shadow plane, DOOM zone).  The sum then landed ~1 KB BELOW the loader
 * handoff block on the 5 MB desktop, so the plane allocator got zero
 * pixels, handed the compositor a NULL plane and the machine triple
 * faulted.  The loader already uses the highest usable end address for
 * the same decision; the kernel now matches it.
 */
static uint32_t detect_total_ram_bytes(const MultibootInfo *mbi) {
    if (mbi != NULL && (mbi->flags & (1u << 6)) != 0 && mbi->mmap_addr != 0 && mbi->mmap_length != 0) {
        uint64_t top = 0;
        uintptr_t cursor = (uintptr_t)mbi->mmap_addr;
        uintptr_t end = cursor + mbi->mmap_length;

        while (cursor + sizeof(uint32_t) <= end) {
            const MultibootMmapEntry *entry = (const MultibootMmapEntry *)cursor;
            if (cursor + entry->size + sizeof(uint32_t) > end) {
                break;
            }
            if (entry->type == 1u) {
                uint64_t region_end = entry->base_addr + entry->length;
                if (region_end > top) {
                    top = region_end;
                }
            }
            cursor += entry->size + sizeof(uint32_t);
        }

        if (top > 0xFFFFFFFFu) {
            top = 0xFFFFFFFFu;
        }
        if (top >= 4u * 1024u * 1024u) {
            return (uint32_t)top;
        }
    }

    if (mbi != NULL && (mbi->flags & 1u) != 0) {
        uint32_t total_kb = mbi->mem_lower + mbi->mem_upper;
        return total_kb * 1024u;
    }

    return 32u * 1024u * 1024u;
}
