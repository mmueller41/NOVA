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


class Core_allocator
{
    private:
        Bit_alloc<NUM_CPU, 0> free_map{};

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

    public:
        mword alloc(Cell *claimant, unsigned int cores) {
            mword core_allocation = 0x0UL;
            for (; cores > 0; cores--)
            {
                unsigned int cpu_id = 0x0;
                while (!(cpu_id = static_cast<unsigned int>(free_map.alloc())))
                {
                    Cell *victim = cell_of_lowest_prio();
                    if (!victim || victim->_prio >= claimant->_prio || victim == claimant)
                        return core_allocation;
                    victim->reclaim_cores(cores);
                }
                Atomic::test_set_bit<mword>(core_allocation, cpu_id);
                trace(0, "Allocated core %d: allocation=%lx", cpu_id, core_allocation);
            }
            return core_allocation;
        }

        void yield(unsigned int cpu_id) {
            free_map.release(cpu_id);
        }
};

extern Core_allocator core_alloc;