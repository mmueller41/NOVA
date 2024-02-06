/*
 * Core Allocator
 * 
 * Copyright (C) 2023 Michael Müller
 */
#pragma once

#include "bits.hpp"
#include "cell.hpp"
#include "ec.hpp"
#include "list.hpp"
#include "stdio.hpp"
#include "lock_guard.hpp"

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

        ALWAYS_INLINE
        inline Cell *cells_of_prio(unsigned int prio) {
            return cells[prio];
        }

        inline Cell* cell_of_lowest_prio() {
            for (unsigned int prio = 63; prio > 0; prio--) {
                if (cells_of_prio(prio)) {
                    return cells[prio];
                }
            }
            return nullptr;
        }

        inline unsigned reclaim_cores(Cell *claimant, unsigned int)
        {
            mword *mask = claimant->core_mask;
            unsigned reclaimed = 0;
            for (unsigned long i = 0; i < NUM_CPU / (sizeof(mword) * 8); i++)
            {
                mword yield_mask = mask[i];
                long cpu = 0;
                while ((cpu = bit_scan_forward((yield_mask))) != -1) {
                    yield_mask &= ~(1UL << cpu);
                    //trace(0, "Will try to reclaim core %lu for %p from %p", cpu, claimant, borrowers[cpu]);
                    if (borrowers[cpu] && borrowers[cpu] != claimant)
                    {
                        if (cpu == Cpu::id)
                            continue;
                        //trace(0, "Reclaiming core %lu for %p from %p", cpu, claimant, borrowers[cpu]);
                        Cell *borrower = const_cast<Cell*>(__atomic_load_n(&borrowers[cpu], __ATOMIC_SEQ_CST));
                        reclaimed += borrower->yield_cores(1UL << cpu);
                    }
                }
            }
            return reclaimed;
        }


    public:
        mword alloc(Cell *claimant, unsigned int cores) {
            mword core_allocation = 0x0UL;
            mword *mask = claimant->core_mask;


            for (; cores > 0; cores--)
            {
                unsigned int cpu_id = 0x0;
                bool borrowed = false;
                unsigned short trials = 0;

                /* First we try to allocate a core from the pre-reserved cores of clamaint, only
                if the claiming cell has exhaused its reserved cores, we try to allocate a core from
                the global core allocator. This way the cores initially assigned to the cell by Hoitaja are used first, before allocating cores from other cells. */

                /* First try to allocate a core from claimant's reserve pool */
                cpu_id = static_cast<unsigned int>(free_map.alloc_with_mask(mask));

                if (!cpu_id) {
                    /* If this fails, we try to get an idle core for three times */
                    for (; !cpu_id && trials < 3; trials++) 
                        cpu_id = static_cast<unsigned int>(free_map.alloc_with_mask(idle_mask));
                } 

                if (!cpu_id)
                {
                    /* If there are no idle cores, check wether we can reclaim cores that claimant borrowed to another cell.
                    This is performed asynchronously to avoid a deadlock, if the borrower it self claims cores from the claimant. However, we return the cores we could already allocate, if any. */
                    reclaim_cores(claimant, cores);
                    return core_allocation;
                }

                /* If we allocated an idling core, we have to check wether it is one of the claimant's own cores or if it is owned by another cell. For the latter case, the claimant has to be recorded as a borrower of this core. This is needed so that the owner can reclaim the core, if necessary. */
                Atomic::test_set_bit<mword>(core_allocation, cpu_id);
                borrowed = !is_owner(claimant, cpu_id);

                if (borrowed) {
                    claimant->borrowed_cores |= (1UL << cpu_id);
                    __atomic_store_n(&borrowers[cpu_id], claimant, __ATOMIC_SEQ_CST);
                }
                else
                {
                    owners[cpu_id] = claimant;
                }

                /* Mark the core as busy now, by clearing the corresponding bit in the idle cores bitmap */
                Atomic::test_clr_bit<mword>(idle_mask[cpu_id / NUM_CPU], cpu_id % NUM_CPU);
            }
            return core_allocation;
        }

        void yield(unsigned int cpu_id) {
            Atomic::test_set_bit<mword>(idle_mask[cpu_id / NUM_CPU], cpu_id % NUM_CPU);
            free_map.release(cpu_id);
        }

        void dump_allocation() {
            free_map.dump_trace();
        }

        bool reserve(Cell *reservant, mword const id) {
            /* It is possible that reservant is a newly created cell or just woke up and its core at <id> has been borrowed by another cell. If that's the case, we reclaim the core <id> for <reservant> by commanding the borrower to yield the core. */
            if (owners[id] == reservant && __atomic_load_n(&borrowers[id], __ATOMIC_SEQ_CST)) {
                Cell *borrower = const_cast<Cell *>(__atomic_load_n(&borrowers[id], __ATOMIC_SEQ_CST));
                //trace(0, "%p: Need to reclaim CPU %ld from %p", reservant, id, borrower);
                borrower->yield_cores((1UL << id));
            } else if (owners[id] != reservant) {
                return false;
            }
            //trace(0, "Reserving CPU %ld for cell %p", id, reservant);
            free_map.reserve(id);
            reservant->core_map |= (1UL << id);
                return true;
            
        }

        ALWAYS_INLINE
        inline bool is_owner(Cell *claimant, mword const id) {
            return (claimant == owners[id]);
        }

        ALWAYS_INLINE
        inline void set_owner(Cell *owner, mword const id) {
            if (owners[id] != nullptr && owners[id] != owner) {
                Cell *other = owners[id];
                borrowers[id] = other;
                other->borrowed_cores |= (1UL << id);
            }
            owners[id] = owner;
        }

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

        void return_core(Cell *borrower, unsigned cpu) {
            if (borrowers[cpu] == borrower)
                __atomic_store_n(&borrowers[cpu], 0, __ATOMIC_SEQ_CST);
            Atomic::test_set_bit<mword>(owners[cpu]->core_map, cpu);
            owners[cpu]->wake_core(cpu);
        }

        bool borrowed(Cell *cell, unsigned cpu) {
            return borrowers[cpu] == cell;
        }

        inline void dump_cells() 
        {
            Lock_guard<Spinlock> guard(dump_lock);
            trace(0, "---------<Allocations>---------");
            for (unsigned int prio = 0; prio < 63; prio++)
            {
                for (Cell *cell = cells[prio]; cell != nullptr; cell = cell->next) {
                    trace(0, "{\"cell\": %p, \"mask\": %lx, \"allocation\": %lu},", cell, cell->core_mask[0], cell->core_map);
                }
            }
            trace(0, "------------------------------");
        }

        inline bool valid_allocation() 
        {
            Cell *possesors[NUM_CPU];
            memset(possesors, 0, sizeof(possesors));
            for (unsigned int prio = 0; prio < 63; prio++)
            {
                for (Cell *cell = cells[prio]; cell != nullptr; cell = cell->next) {
                    mword map = __atomic_load_n(&cell->core_map, __ATOMIC_SEQ_CST);
                    for (long cpu = bit_scan_forward(map); cpu != -1;) {
                        if (possesors[cpu] != nullptr)
                            return false;
                        possesors[cpu] = cell;
                    }
                }
            }
            return true;
        }
};

extern Core_allocator core_alloc;