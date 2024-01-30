#include "bits.hpp"
#include "cell.hpp"
#include "core_allocator.hpp"

Cell *cells[64];

void Cell::add_cores(mword cpu_map) {
    long cpu = 0;
    core_map |= cpu_map;

    //trace(0, "Adding cores from map %lx", cpu_map);

    while ((cpu = bit_scan_forward(cpu_map)) != -1)
    {
        Atomic::test_clr_bit(cpu_map, cpu);
        if (cpu >= NUM_CPU || !(_worker_scs[cpu])) {
            trace(TRACE_ERROR, "No worker found for CPU: %ld", cpu);
            continue;
        }
        //trace(0, "Adding core %ld", cpu);
        _worker_scs[cpu]->remote_enqueue();
    }
}

void Cell::reclaim_cores(unsigned int cores)
{
    for (; cores > 0; cores--) {
        unsigned int cpu = static_cast<unsigned int>(bit_scan_forward(core_map));
        unsigned long channel = _pd->worker_channels[cpu];
        Atomic::test_set_bit(cores_to_reclaim, cpu);
        __atomic_store_n(&channel, 1, __ATOMIC_SEQ_CST);
    }
    while (__atomic_load_n(&core_map, __ATOMIC_SEQ_CST))
        __builtin_ia32_pause();
}

unsigned Cell::yield_cores(mword cpu_map)
{
    long cpu = 0;
    unsigned yielded = 0;

    //trace(0, "About to yield cores %lx", map);
    while ((cpu = bit_scan_forward(cpu_map)) != -1) {
        Atomic::test_clr_bit(cpu_map, cpu);
        if (_worker_scs[cpu]) {
            Atomic::test_set_bit(cores_to_reclaim, cpu);
            mword *channel = &(_pd->worker_channels[cpu]);
            __atomic_store_n(channel, 1, __ATOMIC_SEQ_CST);
        } else {
            core_alloc.return_core(this, static_cast<unsigned int>(cpu));
            this->yield_core(static_cast<unsigned int>(cpu));
            core_alloc.yield(static_cast<unsigned int>(cpu));
        }
        yielded++;
    }
    //trace(0, "Waiting for cores to be yielded: 0x%lx", cores_to_reclaim);
    while (__atomic_load_n(&cores_to_reclaim, __ATOMIC_SEQ_CST))
        __builtin_ia32_pause();
    return yielded;
}

void Cell::update(mword mask, mword offset)
{
    trace(0, "Updating core mask of cell %p : %lx", this, mask);

    core_mask[offset] = mask;

    core_alloc.set_owner(this, mask, offset * sizeof(mword) * 8);
}
