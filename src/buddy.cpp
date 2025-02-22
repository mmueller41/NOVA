/*
 * Buddy Allocator
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012 Udo Steinberg, Intel Corporation.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#include "assert.hpp"
#include "bits.hpp"
#include "buddy.hpp"
#include "initprio.hpp"
#include "lock_guard.hpp"
#include "stdio.hpp"
#include "string.hpp"
#include "pd.hpp"

extern char _mempool_p, _mempool_l, _mempool_f, _mempool_e;

INIT_PRIORITY (PRIO_BUDDY)
Quota Quota::init;

/*
 * Buddy Allocator
 */
INIT_PRIORITY (PRIO_BUDDY)
Buddy Buddy::allocator (reinterpret_cast<mword>(&_mempool_p),
                        reinterpret_cast<mword>(&_mempool_l),
                        reinterpret_cast<mword>(&_mempool_f),
                        reinterpret_cast<mword>(&_mempool_e) -
                        reinterpret_cast<mword>(&_mempool_l));

Buddy * Buddy::list;

Buddy::Buddy (mword phys, mword virt, mword f_addr, size_t size)
: List<Buddy>(list)
{
    // Compute maximum aligned block size
    unsigned long bit = bit_scan_reverse (size);

    // Compute maximum aligned physical block address (base)
    base = phys_to_virt (align_up (phys, 1ul << bit));

    // Convert block size to page order
    order = bit + 1 - PAGE_BITS;

    trace (TRACE_MEMORY, "POOL: %#010lx-%#010lx O:%lu",
           phys,
           phys + size,
           order);

    // Allocate block-list heads
    size -= order * sizeof *head;
    head = reinterpret_cast<Block *>(virt + size);

    // Allocate block-index storage
    size -= size / (PAGE_SIZE + sizeof *index) * sizeof *index;
    size &= ~PAGE_MASK;
    min_idx = page_to_index (virt);
    max_idx = page_to_index (virt + size);
    index = reinterpret_cast<Block *>(virt + size) - min_idx;

    for (unsigned i = 0; i < order; i++)
        head[i].next = head[i].prev = head + i;

    for (mword i = f_addr; i < virt + size; i += PAGE_SIZE)
        _free (i, Quota::init);
}

/*
 * Allocate physically contiguous memory region.
 * @param ord       Block order (2^ord pages)
 * @param zero      Zero out block content if true
 * @return          Pointer to linear memory region
 */
void *Buddy::_alloc (unsigned short ord, Quota &quota, Fill fill)
{
    Lock_guard <Spinlock> guard (lock);

    for (unsigned short j = ord; j < order; j++) {

        if (head[j].next == head + j)
            continue;

        Block *block = head[j].next;
        block->prev->next = block->next;
        block->next->prev = block->prev;
        block->ord = ord;
        block->tag = Block::Used;

        while (j-- != ord) {
            Block *buddy = block + (1ul << j);
            buddy->prev = buddy->next = head + j;
            buddy->ord = j;
            buddy->tag = Block::Free;
            head[j].next = head[j].prev = buddy;
        }

        mword virt = index_to_page (block_to_index (block));

        // Ensure corresponding physical block is order-aligned
        assert ((virt_to_phys (virt) & ((1ul << (block->ord + PAGE_BITS)) - 1)) == 0);

        if (fill)
            memset (reinterpret_cast<void *>(virt), fill == FILL_0 ? 0 : -1, 1ul << (block->ord + PAGE_BITS));

        quota.alloc(1ul << ord);

        return reinterpret_cast<void *>(virt);
    }

    return nullptr;
}

void *Buddy::alloc (unsigned short ord, Quota &quota, Fill fill)
{
    for (Buddy *b = list; b; b = b->next) {
        void * v = b->_alloc(ord, quota, fill);
        if (v) return v;
    }

    quota.dump(Pd::current);

    Console::panic ("Out of memory");
}

/*
 * Free physically contiguous memory region.
 * @param virt     Linear block base address
 */
void Buddy::_free (mword virt, Quota &quota)
{
    signed long idx = page_to_index (virt);

    // Ensure virt is within allocator range
    assert (idx >= min_idx && idx < max_idx);

    Block *block = index_to_block (idx);

    // Ensure block is marked as used
    assert (block->tag == Block::Used);

    // Ensure corresponding physical block is order-aligned
    assert ((virt_to_phys (virt) & ((1ul << (block->ord + PAGE_BITS)) - 1)) == 0);

    quota.free(1ul << block->ord);

    Lock_guard <Spinlock> guard (lock);

    unsigned short ord;
    for (ord = block->ord; ord < order - 1; ord++) {

        // Compute block index and corresponding buddy index
        signed long block_idx = block_to_index (block);
        signed long buddy_idx = block_idx ^ (1ul << ord);

        // Buddy outside mempool
        if (buddy_idx < min_idx || buddy_idx >= max_idx)
            break;

        Block *buddy = index_to_block (buddy_idx);

        // Buddy in use or fragmented
        if (buddy->tag == Block::Used || buddy->ord != ord)
            break;

        // Dequeue buddy from block list
        buddy->prev->next = buddy->next;
        buddy->next->prev = buddy->prev;

        // Merge block with buddy
        if (buddy < block)
            block = buddy;
    }

    block->ord = ord;
    block->tag = Block::Free;

    // Enqueue final-size block
    Block *h = head + ord;
    block->prev = h;
    block->next = h->next;
    block->next->prev = h->next = block;
}

void Buddy::free (mword virt, Quota &quota)
{
    for (Buddy *b = list; b; b = b->next) {
        signed long idx = b->page_to_index (virt);
        if (idx >= b->min_idx && idx < b->max_idx) {
            b->_free(virt, quota);
            return;
        }
    }

    Console::panic ("Invalid memory free");
}

void Quota::dump(void * pd, bool all)
{
    if (all) {
        trace(0, "quota after boot Quota:init - in_use=%lx limit %lx",
              Quota::init.usage(), Quota::init.upli);

        trace(0, "Pd::kern=%12p quota=%12p - in_use=%012lx limit=%012lx not_for_transfer=%012lx overrun=%lx",
              &Pd::kern, &Pd::kern.quota, Pd::kern.quota.usage(), Pd::root.quota.upli, Pd::kern.quota.notr, Pd::kern.quota.over);
        trace(0, "Pd::root=%12p quota=%12p - in_use=%012lx limit=%012lx not_for_transfer=%012lx overrun=%lx",
              &Pd::root, &Pd::root.quota, Pd::root.quota.usage(), Pd::root.quota.upli, Pd::root.quota.notr, Pd::root.quota.over);
    }

    trace(0, "Pd::this=%12p quota=%12p - in_use=%012lx limit=%012lx not_for_transfer=%012lx overrun=%lx",
          pd, &static_cast<Pd *>(pd)->quota, usage(), upli, notr, over);
}
