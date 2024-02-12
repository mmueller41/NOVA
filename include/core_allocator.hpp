/*
 * Core Allocator
 * 
 * Copyright (C) 2023 Michael Müller
 */
#pragma once

#include "bits.hpp"
#include "list.hpp"
#include "stdio.hpp"
#include "lock_guard.hpp"
#include "bit_alloc.hpp"

class Cell;
class Core_allocator
{
    private:
        /* Bit allocator and free map for CPU cores */
        Bit_alloc<NUM_CPU, 0> free_map{};
        /* Bitmap saving which cores are idle */
        mword idle_mask[NUM_CPU / (sizeof(mword) * 8)];
        Cell *owners[NUM_CPU]; /* saves which cell owns a certain core */
        /* borrowers saves which cell has borrowed which CPU core */
        volatile Cell *borrowers[NUM_CPU];
        Spinlock dump_lock{};

        unsigned reclaim_cores(Cell *claimant, unsigned int);

    public:
        mword alloc(Cell *, unsigned int);

        void yield(Cell *yielder, unsigned int cpu_id);

        void dump_allocation() {
            free_map.dump_trace();
        }

        bool reserve(Cell *, mword const);

        ALWAYS_INLINE
        inline bool is_owner(Cell *claimant, mword const id) {
            return (claimant == owners[id]);
        }

        void set_owner(Cell *, mword const);

        void set_owner_of_range(Cell *owner, mword start, mword end) {
            for (mword cpu = start; cpu < end; cpu++)
                set_owner(owner, cpu);
        }

        void set_owner(Cell *owner, mword bits, mword offset) {
            if (bits == 0)
                return;
            long cpu = 0;

            while ((cpu = bit_scan_forward(bits)) != -1) {
                set_owner(owner, cpu + offset);
                bits &= ~(1UL << cpu);
            }
        }

        void return_core(Cell *, unsigned);

        bool borrowed(Cell *cell, unsigned cpu) {
            return borrowers[cpu] == cell;
        }

        void dump_cells();

        bool valid_allocation();
};

extern Core_allocator core_alloc;