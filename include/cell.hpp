#pragma once

#include "list.hpp"
#include "ec.hpp"
#include "sc.hpp"
#include "sm.hpp"
#include "pd.hpp"
#include "buddy.hpp"

extern Cell *cells[64];
class Core_allocator;

struct Channel {
    volatile unsigned short yield_flag;
    volatile unsigned short limit;
    volatile unsigned short remainder;
    unsigned short padding;
    unsigned long delta_alloc;
    unsigned long delta_activate;
    unsigned long delta_setflag;
    unsigned long delta_findborrower;
    unsigned long delta_block;
    unsigned long delta_enter;
    unsigned long delta_return;
};

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
    unsigned int limit{0};
    unsigned int remainder{0};
    alignas(64) mword requested_cores{0};

    unsigned int calc_stealing_limit(unsigned int W) {
        if (!W)
            return 0;
        limit = static_cast<unsigned int>(_pd->mx_worker()) / W;
        remainder = W;
        return remainder;
    }

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

    //void reclaim_cores(unsigned int cores);
    void add_cores(mword cpu_map);
    void yield_core(unsigned int core, bool clear_flag = true)
    {
        Atomic::test_clr_bit<mword>(core_map, core);
        Atomic::test_clr_bit<volatile mword>(cores_to_reclaim, core);
        borrowed_cores &= ~(1UL << core);
        if (clear_flag && _pd->worker_channels) {
            //unsigned long *channel = reinterpret_cast<unsigned long*>(&_pd->worker_channels[core]);
            ;
            //
            _pd->worker_channels[core].yield_flag = 0;
            //__atomic_store_n(channel, 0, __ATOMIC_SEQ_CST);
            // trace(0, "%p: channel %d : %ld", this, core, _pd->worker_channels[core]);
        }
    }

    ALWAYS_INLINE
    inline bool has_core(unsigned int core)
    {
        return this->core_map & (1UL << core);
    }

    ALWAYS_INLINE
    inline bool requested_core(unsigned int core)
    {
        return this->requested_cores & (1UL << core);
    }

    ALWAYS_INLINE
    inline void wake_core(unsigned int core)
    {
        
        Channel *chan = Pd::current->worker_channels ? &Pd::current->worker_channels[core] : nullptr;

        chan->limit = static_cast<unsigned short>(limit);

        if (_worker_sms[core])
            _worker_sms[core]->up();
        else {
            trace(TRACE_ERROR, "Worker on CPU %u not found for cell.", core);
        }
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