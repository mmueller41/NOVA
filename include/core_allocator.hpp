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
        struct alignas(64) aligned_cell_container {
            alignas(64) Cell volatile *cell{nullptr};
            alignas(64) Spinlock lock{};
        };

        /* Bit allocator and free map for CPU cores */
        alignas(64) Bit_alloc<NUM_CPU, 0> free_map{};
        /* Bitmap saving which cores are idle */
        alignas(64) mword idle_mask[NUM_CPU / (sizeof(mword) * 8)];
        alignas(64) struct aligned_cell_container owners[NUM_CPU]{}; /* saves which cell owns a certain core */
        /* borrowers saves which cell has borrowed which CPU core */
        alignas(64) struct aligned_cell_container borrowers[NUM_CPU];
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
            return (claimant == owners[id].cell);
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
            return borrowers[cpu].cell == cell;
        }

        void dump_cells();

        bool valid_allocation();

        void init_habitat(mword offset, mword size)
        {
            trace(0, "Created habitat of size %lu starting with CPU %lu", size, offset);
            free_map.reserve(0, offset);
            free_map.reserve(offset+size, NUM_CPU-size);
            free_map.dump_trace();
        }
};

extern Core_allocator core_alloc;