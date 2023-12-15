#include "bits.hpp"
#include "cell.hpp"

Cell *cells[64];

void Cell::add_cores(mword cpu_map) {
    long cpu = 0;
    core_map |= cpu_map;

    trace(0, "Adding cores from map %lx", cpu_map);

    while ((cpu = bit_scan_forward(cpu_map)) != -1)
    {
        Atomic::test_clr_bit(cpu_map, cpu);
        if (cpu >= NUM_CPU || !(_worker_scs[cpu])) {
            trace(0, "No worker found for CPU: %ld", cpu);
            continue;
        }
        trace(0, "Adding core %ld", cpu);
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