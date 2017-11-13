/*
 * Copyright (c) 2009 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HAX_CORE_PAGING_H_
#define HAX_CORE_PAGING_H_

#include "../../include/hax_types.h"
#include "types.h"
#include "ia32.h"

#define PM_INVALID    0
#define PM_FLAT       1
#define PM_2LVL       2
#define PM_PAE        3
#define PM_PML4       4

#define PG_ORDER_4K   12
#define PG_ORDER_2M   21
#define PG_ORDER_4M   22
#define PG_ORDER_1G   30

#define PAGE_SIZE_4K  (1 << PG_ORDER_4K)
#define PAGE_SIZE_2M  (1 << PG_ORDER_2M)
#define PAGE_SIZE_4M  (1 << PG_ORDER_4M)
#define PAGE_SIZE_1G  (1 << PG_ORDER_1G)

static inline uint64 pgsz(uint order)
{
    return (uint64)1 << order;
}

static inline uint64 pgoffs(uint order)
{
    return pgsz(order) - 1;
}

static inline uint64 pgmask(uint order)
{
    return ~pgoffs(order);
}

// Merge page and offset into a single address, depending on order
static paddr_t get_pageoffs(paddr_t p, paddr_t o, uint order)
{
    return ((~(uint64)0 << order) & p) | (~(~(uint64)0 << order) & o);
}

static paddr_t get_pagebase(vaddr_t p, uint order)
{
    return (~(uint64)0 << order) & p;
}

#define INVALID_ADDR  ~(uint64)0
#define INVALID_PFN   INVALID_ADDR

typedef struct pte32 {
    union {
        uint32 raw;
        struct {
            uint32 p       : 1;
            uint32 rw      : 1;
            uint32 us      : 1;
            uint32 pwt     : 1;
            uint32 pcd     : 1;
            uint32 a       : 1;
            uint32 x1      : 2;
            uint32 g       : 1;
            uint32 x2      : 23;
        };
        struct {
            uint32 x3      : 6; // The 6 bits are always identical
            uint32 d       : 1;
            uint32 pat     : 1;
            uint32 x4      : 4;
            uint32 address : 20;
        } pte;
        struct {
            uint32 x5      : 7; // The 7 bits are always identical
            uint32 ps      : 1;
            uint32 x6      : 4;
            uint32 address : 20;
        } pde;
        struct {
            uint32 x7      : 7; // The 7 bits are always identical
            uint32 ps      : 1;
            uint32 x8      : 4;
            uint32 pat     : 1;
            uint32 x9      : 9;
            uint32 address : 10;
        } pde_4M;
    };
} pte32_t;

static inline bool pte32_is_superlvl(uint lvl);

static inline void pte32_set_address(pte32_t *entry, uint lvl, paddr_t addr,
                                     uint order)
{
    if (pte32_is_superlvl(lvl)) {
        entry->pde.ps = (order == PG_ORDER_4M);
    }

    if (order == PG_ORDER_4M) {
        entry->pde_4M.address = addr >> order;
    } else {
        entry->pte.address = addr >> order;
    }
}

static inline uint32 pte32_get_val(pte32_t *entry)
{
    return entry->raw;
}

static inline bool pte32_is_present(pte32_t *entry)
{
    return entry->p;
}

static inline bool pte32_is_accessed(pte32_t *entry)
{
    return entry->a;
}

static inline bool pte32_is_dirty(pte32_t *entry)
{
    return entry->pte.d;
}

static inline void pte32_set_global(pte32_t *entry, bool g)
{
    entry->g = g;
}

/*
 * Set_accessed/dirty methods have atomic and non-atomic versions.
 * Use atomic versions for guest page tables. Non-atomic versions are okay for
 * shadow page tables.
 * Functions to set dirty/accessed: USE ONLY ON LEAF ENTRIES.
 */

static inline void pte32_set_ad(pte32_t *entry, uint lvl, bool d)
{
    assert(is_leaf(lvl));
    entry->raw |= (d ? 0x60 : 0x20);
}

static inline bool pte32_atomic_set_ad(pte32_t *entry, uint lvl, bool d,
                                       pte32_t *prev)
{
    uint32 old_val, new_val;

    assert(is_leaf(lvl));

    old_val = prev->raw;
    new_val = prev->raw | (d ? 0x60 : 0x20);

    if (new_val == old_val)
        return true;

    return hax_cmpxchg32(old_val, new_val, &entry->raw);
}

static inline void pte32_set_accessed(pte32_t *entry)
{
    entry->a = 1;
}

static inline bool pte32_atomic_set_accessed(pte32_t *entry, pte32_t *prev)
{
    uint32 old_val = prev->raw;
    uint32 new_val = prev->raw | 0x20;

    if (new_val == old_val)
        return true;

    return hax_cmpxchg32(old_val, new_val, &entry->raw);
}

static inline bool pte32_get_pwt(pte32_t *entry)
{
    return entry->pwt;
}

static inline bool pte32_get_pat(pte32_t *entry)
{
    return 0;
}

static inline void pte32_set_caching(pte32_t *entry, bool pat, bool pcd,
                                     bool pwt)
{
    entry->pcd = pcd;
    entry->pwt = pwt;
}

static inline bool pte32_is_4M_page(pte32_t *entry, uint lvl)
{
    return pte32_is_superlvl(lvl) && entry->pde.ps;
}

static inline paddr_t pte32_get_address(pte32_t *entry, uint lvl,
                                        paddr_t offset)
{
    return get_pageoffs(entry->raw, offset, (pte32_is_superlvl(lvl) &&
                        entry->pde.ps) ? PG_ORDER_4M : PG_ORDER_4K);
}

static inline paddr_t pae_get_address(paddr_t entry, uint lvl, paddr_t offset)
{
    return get_pageoffs(entry, offset, (pte32_is_superlvl(lvl) &&
                        (entry & 0x80)) ? PG_ORDER_2M : PG_ORDER_4K);
}

static inline uint pte32_get_idxbit(uint lvl)
{
    return 12 + 10 * lvl;
}

static inline paddr_t pte32_get_cr3_mask(void)
{
    return ~(paddr_t)0xfff;
}

static inline uint pte32_get_idxmask(uint lvl)
{
    return 0x3ff;
}

static inline uint pte32_get_idx(uint lvl, vaddr_t va)
{
    return (va >> pte32_get_idxbit(lvl)) & pte32_get_idxmask(lvl);
}

static inline uint pae_get_idx(uint lvl, vaddr_t va)
{
    return (va >> (12 + 9 * lvl)) & 0x1ff;
}

static inline bool pte32_is_superlvl(uint lvl)
{
    return lvl == 1;
}

static inline bool pte32_is_leaf(pte32_t *entry, uint lvl)
{
    return (lvl == 0) || (pte32_is_superlvl(lvl) && entry->pde_4M.ps);
}

static inline bool pae_is_leaf(pte32_t *entry, uint lvl)
{
    return (lvl == 0) || (pte32_is_superlvl(lvl) && entry->pde_4M.ps);
}

// Returns true if reserved bits are set
static inline bool pte32_check_rsvd(pte32_t *entry, uint lvl)
{
    return pte32_is_superlvl(lvl) && entry->pde_4M.ps &&
           (entry->raw & (1U << 21));
}

// Returns true if reserved bits are set
static inline bool pae_check_rsvd(pte32_t *entry, uint lvl)
{
    uint32 reserved_mask = (((uint32)1 << 21) - 1) - (((uint32)1 << 13) - 1);
    return pte32_is_superlvl(lvl) && entry->pde_4M.ps &&
           (entry->raw & reserved_mask);
}

typedef struct pte64 {
    union {
        uint64 raw;
        struct {
            uint64 p       : 1;
            uint64 rw      : 1;
            uint64 us      : 1;
            uint64 pwt     : 1;
            uint64 pcd     : 1;
            uint64 a       : 1;
            uint64 x1      : 2;
            uint64 g       : 1;
            uint64 lock    : 1;
            uint64 x2      : 2;
            uint64 addr    : 28;
            uint64 rsvd    : 23;
            uint64 xd      : 1;
        };
        struct {
            uint64 x3      : 6; // The 6 bits are always identical
            uint64 d       : 1;
            uint64 pat     : 1;
            uint64 x4      : 4;
            uint64 address : 28;
            uint64 x5      : 24;
        } pte;
        struct {
            uint64 x6      : 7; // The bits are identical or reserved
            uint64 ps      : 1; // Page size
            uint64 x7      : 4;
            uint64 address : 28;
            uint64 x8      : 24;
        } pde;
    };
} pte64_t;

static inline void pte64_set_address(pte64_t *entry, paddr_t addr)
{
    entry->pte.address = addr >> PG_ORDER_4K;
}

static inline void pte64_clear_entry(pte64_t *entry)
{
    entry->raw = 0;
}

static inline uint32 pte64_get_val(pte64_t *entry)
{
    return entry->raw;
}

static inline bool pte64_is_present(pte64_t *entry)
{
    return entry->p;
}

static inline void pte64_set_global(pte64_t *entry, uint lvl, bool g)
{
    entry->g = g;
}

/*
 * Set_accessed/dirty methods have atomic and non-atomic versions.
 * Use atomic versions for guest page tables. Non-atomic versions are okay for
 * shadow page tables.
 * Functions to set dirty/accessed: USE ONLY ON LEAF ENTRIES
 */

static inline void pte64_set_ad(pte64_t *entry, uint lvl, bool d)
{
    assert(is_leaf(lvl));
    entry->raw |= (d ? 0x60 : 0x20);
}

static inline void pte64_set_accessed(pte64_t *entry, uint lvl)
{
    if (lvl != 2)
        entry->a = true;
}

static inline bool pte64_get_pcd(pte64_t *entry)
{
    return entry->pcd;
}

static inline bool pte64_get_pwt(pte64_t *entry)
{
    return entry->pwt;
}

static inline bool pte64_get_pat(pte64_t *entry)
{
    return 0;
}

static inline void pte64_set_caching(pte64_t *entry, bool pat, bool pcd,
                                     bool pwt)
{
    entry->pcd = pcd;
    entry->pwt = pwt;
}

static inline void pte64_set_entry(pte64_t *entry, uint lvl, paddr_t addr,
                                   bool us, bool w, bool x)
{
    entry->raw &= 0xe00;

    if (lvl != 2) {
        // since PDPT in PAE has no US,RW
        entry->rw = w;
        entry->us = us;
        entry->xd = !x;
    }

    entry->p = 1;
    pte64_set_address(entry, addr);
}

static inline uint pte64_get_idxbit(uint lvl)
{
    return 12 + 9 * lvl;
}

static inline uint pte64_get_order(uint lvl)
{
    return 12 + 9 * lvl;
}

static inline paddr_t pte64_get_cr3_mask(pte64_t *entry)
{
    return ~(paddr_t)0x1f;
}

static inline uint pte64_get_idxmask(uint lvl)
{
    return 0x1ff;
}

static inline uint pte64_get_idx(uint lvl, vaddr_t va)
{
    return (va >> pte64_get_idxbit(lvl)) & pte64_get_idxmask(lvl);
}

#endif  // HAX_CORE_PAGING_H_
