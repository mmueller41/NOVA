#include "bits.hpp"
#include "cell.hpp"
#include "core_allocator.hpp"

Cell *cells[64];

void Cell::add_cores(mword cpu_map) {
    long cpu = 0;
    Atomic::set_mask(core_map, cpu_map);

    while ((cpu = bit_scan_forward(cpu_map)) != -1)
    {
        Atomic::test_clr_bit(cpu_map, cpu);
        if (cpu >= NUM_CPU || !(_worker_scs[cpu])) {
            //trace(TRACE_ERROR, "No worker found for CPU: %ld", cpu);
            continue;
        }
        _worker_sms[cpu]->up();
    }
}

void Cell::reclaim_cores(unsigned int cores)
{
    for (; cores > 0; cores--)
    {
        unsigned int cpu = static_cast<unsigned int>(bit_scan_forward(core_map));
        if (_pd->worker_channels && _worker_scs[cpu]) {
            if (cores_to_reclaim & (1<<cpu))
                continue;
            /*if (_workers[cpu]->blocked())
                continue;*/
            Atomic::test_set_bit(cores_to_reclaim, cpu);
            unsigned long volatile *channel = &(_pd->worker_channels[cpu]);
            __atomic_store_n(channel, 1, __ATOMIC_SEQ_CST);
        }
        else
        {
            core_alloc.return_core(this, static_cast<unsigned int>(cpu));
        }
    }
}

unsigned Cell::yield_cores(mword cpu_map, bool release)
{
    long cpu = 0;
    unsigned yielded = 0;

    while ((cpu = bit_scan_forward(cpu_map)) != -1) {
        Atomic::test_clr_bit(cpu_map, cpu);
        if (_pd->worker_channels && _worker_scs[cpu]) {
            if (cores_to_reclaim & (1<<cpu))
                continue;
            /*if (_workers[cpu]->blocked())
                continue;*/
            Atomic::test_set_bit(cores_to_reclaim, cpu);
            unsigned long volatile *channel = &(_pd->worker_channels[cpu]);
            __atomic_store_n(channel, 1, __ATOMIC_SEQ_CST);
        }
        else
        {
            core_alloc.return_core(this, static_cast<unsigned int>(cpu));
        }
        if (release)
            core_alloc.yield(this, static_cast<unsigned int>(cpu));
        yielded++;
    }
    return yielded;
}

void Cell::update(mword mask, mword offset)
{
    trace(0, "Updating core mask of cell %p : %lx", this, mask);

    core_mask[offset] = mask;

    core_alloc.set_owner(this, mask, offset * sizeof(mword) * 8);
}

void Cell::remove_worker(unsigned cpu)
{
    if (!_worker_sms[cpu])
        return;

    Sm::destroy(_worker_sms[cpu], *_pd);
}

Cell::~Cell()
{
    yield_cores(core_map);

    for (int cpu = 0; cpu < NUM_CPU; cpu++) {
        if (_workers[cpu])
            Ec::destroy(_workers[cpu], *_pd);
    }

}