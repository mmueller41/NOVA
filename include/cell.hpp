#pragma once

#include "list.hpp"
#include "ec.hpp"
#include "sc.hpp"
#include "sm.hpp"
#include "pd.hpp"
#include "buddy.hpp"

extern Cell *cells[64];
class Core_allocator;

class alignas(64) Cell : public List<Cell>
{
    friend class Ec;
    friend class Core_allocator;

private:
    Pd *_pd;
    Ec *_workers[NUM_CPU];
    Sc *_worker_scs[NUM_CPU];
    Sm *_worker_sms[NUM_CPU];
    unsigned int _active_workers{1};
    alignas(64) mword core_map{0};

public:
    volatile mword cores_to_reclaim{0};
    unsigned int _prio;
    mword borrowed_cores{0};
    mword core_mask[NUM_CPU/(sizeof(mword) *8)];

    Cell(Pd *pd, unsigned short prio) : List<Cell>(cells[prio]), _pd(pd),  _prio{prio}  { _pd->cell = this; }

    Cell(Pd *pd, unsigned short prio, mword mask, mword start) : List<Cell>(cells[prio]), _pd(pd), _prio(prio) { _pd->cell = this;
        core_mask[start] = mask;

        trace(0, "Created new cell %p wtih initial allocation: %lx", this, core_mask[0]);
    }

    Cell(const Cell& copy) : List<Cell>(cells[copy._prio]), _pd(copy._pd), _prio(copy._prio) {

    }

    void reclaim_cores(unsigned int cores);
    void add_cores(mword cpu_map);
    void yield_core(unsigned int core)
    {
        Atomic::test_clr_bit<mword>(core_map, core);
        Atomic::test_clr_bit<volatile mword>(cores_to_reclaim, core);
        borrowed_cores &= ~(1UL << core);
        if (_pd->worker_channels) {
            __atomic_store_n(&_pd->worker_channels[core], 0, __ATOMIC_SEQ_CST);
            //trace(0, "%p: channel %d : %ld", this, core, _pd->worker_channels[core]);
        }
    }

    ALWAYS_INLINE
    inline void wake_core(unsigned int core)
    {
        if (_worker_sms[core])
            _worker_sms[core]->up();
    }

    static void *operator new (size_t, Pd &pd) {
        return pd.cell_cache.alloc(pd.quota);
    }

    void update(mword mask, mword offset);

    unsigned yield_cores(mword cpu_map, bool release=false);

    bool yielded(unsigned cpu) {
        return (core_map & (1ul << cpu)) && _workers[cpu] && _workers[cpu]->blocked();
    }

    void remove_worker(unsigned cpu);

    Cell &operator=(const Cell& other) {
        this->_prio = other._prio;
        this->core_map = other.core_map;

        return *this;
    }

    ~Cell();

};