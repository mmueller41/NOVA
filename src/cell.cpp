#include "bits.hpp"
#include "cell.hpp"
#include "core_allocator.hpp"

Cell *cells[64];

void Cell::add_cores(mword cpu_map) {
    long cpu = 0;
    Atomic::set_mask(core_map, cpu_map);

    while ((cpu = bit_scan_forward(cpu_map)) != -1)
    {
        Channel *chan = Pd::current->worker_channels ? &Pd::current->worker_channels[cpu] : nullptr;
    
        if (!chan)
            continue;

        chan->limit = static_cast<unsigned short>(limit);
        chan->remainder = static_cast<unsigned short>(remainder);

        Atomic::test_clr_bit(cpu_map, cpu);
        if (cpu >= NUM_CPU || !(_worker_scs[cpu])) {
            trace(TRACE_CPU, "No worker found for CPU: %ld", cpu);
            continue;
        }
        _worker_sms[cpu]
            ->up();
    }
}
/*
void Cell::reclaim_cores(unsigned int cores)
{
    if (!Pd::current->worker_channels)
        return;
    
    Channel *chan = Pd::current->worker_channels ? &Pd::current->worker_channels[Cpu::id] : nullptr;

    if (chan)
        chan->delta_setflag = rdtsc();

    for (; cores > 0; cores--)
    {
        unsigned int cpu = static_cast<unsigned int>(bit_scan_forward(core_map));
        if (_pd->worker_channels && _worker_scs[cpu]) {
            if (cores_to_reclaim & (1 << cpu))
                continue;
            Atomic::test_set_bit(cores_to_reclaim, cpu);
            _pd->worker_channels[cpu].yield_flag = 1;
        }
        else
        {
            core_alloc.return_core(this, static_cast<unsigned int>(cpu));
            const_cast<Cell *>(core_alloc.owner(cpu))->wake_core(static_cast<int>(cpu));
        }
    }
    if (chan)
        chan->delta_setflag = rdtsc() - chan->delta_setflag;

}*/

unsigned Cell::yield_cores(mword cpu_map, bool release)
{
    long cpu = 0;
    unsigned yielded = 0;

    Channel *chan = Pd::current->worker_channels ? &Pd::current->worker_channels[Cpu::id] : nullptr;

    if (chan)
        chan->delta_setflag = rdtsc();
    while ((cpu = bit_scan_forward(cpu_map)) != -1)
    {
        Atomic::test_clr_bit(cpu_map, cpu);
        if (_pd->worker_channels && _worker_scs[cpu]) {

            if (_pd->worker_channels[cpu].yield_flag)
                continue;
            /*if (_workers[cpu]->blocked())
                continue;*/
            Atomic::test_set_bit(cores_to_reclaim, cpu);
            short expect = 0;
            bool will_sleep = !__atomic_compare_exchange_n(&(_pd->worker_channels[cpu].yield_flag), &expect, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
            
            if (will_sleep)
                continue;

            //_pd->worker_channels[cpu].yield_flag = 1;
            // trace(TRACE_CPU, "Requested core %ld", cpu);
        }
        else
        {
            core_alloc.return_core(this, static_cast<unsigned int>(cpu));
            const_cast<Cell *>(core_alloc.owner(cpu))->wake_core(static_cast<int>(cpu));
        }
        if (release) {
            core_alloc.yield(this, static_cast<unsigned int>(cpu));
        }
        yielded++;
    }
    if (chan)
        chan->delta_setflag = rdtsc() - chan->delta_setflag;
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