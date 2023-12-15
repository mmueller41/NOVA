#pragma once

#include "list.hpp"
#include "ec.hpp"
#include "sc.hpp"
#include "pd.hpp"
#include "buddy.hpp"

extern Cell *cells[64];

class Cell : public List<Cell>
{
    friend class Ec;

private:
    Pd *_pd;
    Ec *_workers[NUM_CPU];
    Sc *_worker_scs[NUM_CPU];
    unsigned int _active_workers{1};
    mword core_map{0};

public:
    volatile mword cores_to_reclaim{0};
    unsigned int _prio;

    Cell(Pd *pd, unsigned short prio) : List<Cell>(cells[prio]), _pd(pd), _prio(prio) { _pd->cell = this; }

    void reclaim_cores(unsigned int cores);
    void add_cores(mword cpu_map);
    void yield_core(unsigned int core)
    {
        Atomic::test_clr_bit<mword>(core_map, core);
        Atomic::test_clr_bit<volatile mword>(cores_to_reclaim, core);
    }

    static void *operator new (size_t, Pd &pd) {
        return pd.cell_cache.alloc(pd.quota);
    }
};