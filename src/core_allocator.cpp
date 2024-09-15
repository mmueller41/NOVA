#include "core_allocator.hpp"
#include "cell.hpp"

Core_allocator core_alloc;

mword Core_allocator::alloc(Cell *claimant, int cores)
{
    mword core_allocation = 0x0UL;
    unsigned int allocated = 0;
    mword *mask = claimant->core_mask;

    for (; cores > 0; cores--)
    {
        unsigned int cpu_id = 0x0;
        bool borrowed = false;
        unsigned short trials = 0;
        unsigned int reclaimed = 0;

        /* First we try to allocate a core from the pre-reserved cores of clamaint, only
        if the claiming cell has exhaused its reserved cores, we try to allocate a core from
        the global core allocator. This way the cores initially assigned to the cell by Hoitaja are used first, before allocating cores from other cells. */

        /* First try to allocate a core from claimant's reserve pool */
        cpu_id = static_cast<unsigned int>(free_map.alloc_with_mask(mask));
        
        if (!cpu_id)
        {
            /* If there are no idle cores, check wether we can reclaim cores that claimant borrowed to another cell.
            This is performed asynchronously to avoid a deadlock, if the borrower it self claims cores from the claimant. However, we return the cores we could already allocate, if any. */
            //trace(TRACE_CPU, "Reclaiming %d cores.", cores);
            reclaimed = reclaim_cores(claimant, cores);
            allocated += reclaimed;
            //trace(TRACE_CPU, "Reclaimed %d cores", reclaimed);
            cores -= allocated;
            if (cores <= 0)
            {
                claimant->calc_stealing_limit(allocated);
                return core_allocation;
            }

        }

        if (cores > 0) {
            /* If this fails, we try to get an idle core for three times */
            for (; !cpu_id && trials < 3; trials++) {
                cpu_id = static_cast<unsigned int>(free_map.alloc_with_mask(idle_mask));
                if (cpu_id)
                {
                    break;
                }
            }
        } 

        if (!cpu_id) {
            break;
        }

        assert(!(claimant->has_core(cpu_id)));

        /* If we allocated an idling core, we have to check wether it is one of the claimant's own cores or if it is owned by another cell. For the latter case, the claimant has to be recorded as a borrower of this core. This is needed so that the owner can reclaim the core, if necessary. */
        Atomic::test_set_bit<mword>(core_allocation, cpu_id);
        
        borrowed = !is_owner(claimant, cpu_id);
        allocated++;

        if (borrowed) {
            claimant->borrowed_cores |= (1UL << cpu_id);
            borrowers[cpu_id].lock.lock();
            Cell volatile *previous = __atomic_load_n(&borrowers[cpu_id].cell, __ATOMIC_SEQ_CST);
            if (previous && (previous->_pd->worker_channels[cpu_id].yield_flag == 1)) {
                return_core(const_cast<Cell*>(previous), cpu_id);
                allocated--;
                borrowers[cpu_id].lock.unlock();
                continue;
            }
            __atomic_store_n(&borrowers[cpu_id].cell, claimant, __ATOMIC_SEQ_CST);
            borrowers[cpu_id].lock.unlock();
        }
        else
        {
            owners[cpu_id].cell = claimant;
            /* If we have already requested the core previously, don't count it twice. */
            //if (claimant->requested_core(cpu_id))
            //    allocated--;
        }

        //trace(0, "Allocated core %u for %p", cpu_id, claimant);
        //free_map.dump_trace();
        /* Mark the core as busy now, by clearing the corresponding bit in the idle cores bitmap */
        Atomic::test_clr_bit<mword>(idle_mask[cpu_id / NUM_CPU], cpu_id % NUM_CPU);
    }

    claimant->calc_stealing_limit(allocated);

    return core_allocation;
}

bool Core_allocator::reserve(Cell *reservant, mword const id)
{
    if (!reservant)
        return false;

    /* It is possible that reservant is a newly created cell or just woke up and its core at <id> has been borrowed by another cell. If that's the case, we reclaim the core <id> for <reservant> by commanding the borrower to yield the core. */
    borrowers[id].lock.lock();
    Cell *borrower = const_cast<Cell *>(__atomic_load_n(&borrowers[id].cell, __ATOMIC_SEQ_CST));

    if (owners[id].cell == reservant && borrower) {
        // trace(0, "%p: Need to reclaim CPU %ld from %p", reservant, id, borrower);
        borrower->yield_cores((1UL << id));
    } else if (owners[id].cell != reservant) {
        borrowers[id].lock.unlock();
        return false;
    }
    borrowers[id].lock.unlock();
    //trace(0, "Reserving CPU %ld for cell %p", id, reservant);
    free_map.reserve(id);
    Atomic::set_mask(reservant->core_map, (1ul << id));

    return true;
}

void Core_allocator::set_owner(Cell *owner, mword const id)
{
    if (owners[id].cell != nullptr && owners[id].cell != owner) {
        Cell *other = const_cast<Cell*>(owners[id].cell);
        borrowers[id].cell = other;
        other->borrowed_cores |= (1UL << id);
    }
    owners[id].cell = owner;
}

void Core_allocator::return_core(Cell *borrower, unsigned cpu)
{
    borrower->yield_core(cpu);
    if (borrowers[cpu].cell == borrower)
        __atomic_store_n(&borrowers[cpu].cell, 0, __ATOMIC_SEQ_CST);
    Atomic::test_set_bit<mword>(const_cast<Cell*>(owners[cpu].cell)->core_map, cpu);
    Atomic::test_clr_bit<mword>(const_cast<Cell *>(owners[cpu].cell)->requested_cores, cpu);
    // trace(0, "Returned core %d to cell %p, cmap=%lx", cpu, owners[cpu], owners[cpu]->core_map);
    // const_cast<Cell*>(owners[cpu].cell)->wake_core(cpu);
}

void Core_allocator::dump_cells()
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

bool Core_allocator::valid_allocation()
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

void Core_allocator::yield(Cell *yielder, unsigned cpu_id)
{
    if (!yielder)
        return;
    if (yielder->yielded(cpu_id))
        return;
    if ((yielder->core_map & (1ul<<cpu_id)) || (is_owner(yielder, cpu_id))) {
        Atomic::test_set_bit<mword>(idle_mask[cpu_id / NUM_CPU], cpu_id % NUM_CPU);
        free_map.release(cpu_id);
        /* A yield request and a volutary yield may overlap. In this case the yield request would get lost and the allocator would return the wrong number of cores allocated. To mitigate this, we check whether a yield requests has been filed immediatly after freeing. */ 
        if (yielder->_pd->worker_channels[cpu_id].yield_flag) {
            /* And try to reserve the core. If this worked, we return the core properly. 
            if (free_map.reserve(cpu_id)) {
                return_core(yielder, cpu_id);
                return;
            } */
        }
        //trace(0, "Cell %p yielded CPU %u, owner of CPU is %p", yielder, cpu_id, owners[cpu_id].cell);
    }
    yielder->yield_core(cpu_id, false);
}

unsigned Core_allocator::reclaim_cores(Cell *claimant, unsigned int cores)
{
    mword *mask = claimant->core_mask;
    unsigned reclaimed = 0;
    for (unsigned long i = 0; i < NUM_CPU / (sizeof(mword) * 8); i++)
    {
        mword yield_mask = mask[i];
        long cpu = 0;
        while ((cpu = bit_scan_forward((yield_mask))) != -1) {
            if (reclaimed == cores) {
                break;
            }
            
            yield_mask &= ~(1UL << cpu);
            //trace(0, "Will try to reclaim core %lu for %p from %p", cpu, claimant, borrowers[cpu]);
            borrowers[cpu].lock.lock();
            Cell *borrower = const_cast<Cell *>(__atomic_load_n(&borrowers[cpu].cell, __ATOMIC_SEQ_CST));
            if (borrower && borrower != claimant)
            {
                if (cpu == Cpu::id)
                    continue;
                //trace(0, "Reclaiming core %lu for %p from %p", cpu, claimant, borrower);
                Atomic::test_set_bit(claimant->requested_cores, cpu);
                reclaimed += borrower->yield_cores(1UL << cpu);
            }
            borrowers[cpu].lock.unlock();
        }
    }
    return reclaimed;
}