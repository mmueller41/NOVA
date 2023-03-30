/*
 * Advanced Configuration and Power Interface (ACPI)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
 * 
 * Copyright (C) 2022 Michael Müller, Osnabrück University
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#include "acpi_srat.hpp"
#include "hip.hpp"
#include "stdio.hpp"
#include "config.hpp"

void Acpi_table_srat::parse() const
{
    parse_entry(Acpi_srat_entry::LAPIC, &parse_lapic);
    parse_entry(Acpi_srat_entry::MAS, &parse_mas);
    parse_entry(Acpi_srat_entry::X2APIC, &parse_x2apic);
}

void Acpi_table_srat::parse_entry(Acpi_srat_entry::Type type, void (*handler)(Acpi_srat_entry const *)) const
{
    for (Acpi_srat_entry const *ptr = sras; ptr < reinterpret_cast<Acpi_srat_entry *>
        (reinterpret_cast<mword>(this) + length); ptr = reinterpret_cast<Acpi_srat_entry *>(reinterpret_cast<mword>(ptr) + ptr->length))
        if (ptr->type == type)
            (*handler)(ptr);
}

void Acpi_table_srat::parse_lapic(Acpi_srat_entry const *ptr)
{
    Acpi_processor const *p = static_cast<Acpi_processor const *>(ptr);
    
    if (p->flags & 1) {
        uint32 numa_id = (p->domain_lo |
                                        static_cast<uint32>(p->domain_hi[0]) << 8 |
                                        static_cast<uint32>(p->domain_hi[1]) << 16 |
                                        static_cast<uint32>(p->domain_hi[2]) << 24);

        trace(TRACE_ACPI, "CPU %u - NUMA region %u", p->apic_id, numa_id);
        for (size_t id = 0; id < NUM_CPU; id++) {
            if (Cpu::apic_id[id] == p->apic_id) {
                Cpu::numa_id[id] = static_cast<uint8>(numa_id);
                break;
            }
        }
    }

    // TODO: Use this information, i.e. save it in the resource model
}

void Acpi_table_srat::parse_mas(Acpi_srat_entry const *ptr)
{
    Acpi_srat_memtry const *p = static_cast<Acpi_srat_memtry const *>(ptr);
    bool new_chunk = true;
    //bool replaced_chunk = false;
    Hip *hip = Hip::hip();
    Hip_mem *mem = reinterpret_cast<Hip_mem*>(reinterpret_cast<mword>(hip)+hip->length);

    if (p->flag_enabled) {
        uint32 numa_id = p->domain;

        size_t start = static_cast<uint64>(p->base_addr_lo) | (static_cast<uint64>(p->base_addr_hi) << 32);
        size_t size = static_cast<uint64>(p->length_lo) | (static_cast<uint64>(p->length_hi) << 32);

        trace(TRACE_ACPI, "Found new mem range at %lx--%lx of size %lx for node %d", start, (start + size), size, numa_id);

        if (p->flag_nvm) {
            trace(TRACE_ACPI, "MEM range %lx -- %lx -> NUMA node %d is non-volatile", start, (start + size), numa_id);
        } else {
            trace(TRACE_CPU, "HIP mem entries end at %lx", reinterpret_cast<mword>(mem));

            for (Hip_mem *md = hip->mem_desc; md <= mem; md++)
            {
                /* Skip reserved memory areas */
                /* Each individual type is checked seperately to print the correct type for debugging purposes. */
                if (md->type == Hip_mem::HYPERVISOR) {
                    trace(TRACE_ACPI, "mem=%p, Skipped memory region %llx of size %llx: is hypervisor memory.", md, md->addr, md->size);
                    continue;
                }

                if (md->type == Hip_mem::MB_MODULE) {
                    trace(TRACE_ACPI, "mem=%p, Skipped memory region %llx of size %llx: is multiboot module.", md, md->addr, md->size);
                    continue;
                }

                if (md->type == Hip_mem::MB2_FB) {
                    trace(TRACE_ACPI, "mem=%p, Skipped multiboot frame buffer region at %llx.", md, md->addr);
                    continue;
                }

                if (md->type == Hip_mem::ACPI_RSDT) {
                    trace(TRACE_ACPI, "mem=%p, Skipping ACPI RSDT at %llx.", md, md->addr);
                    continue;
                }

                if (md->type == Hip_mem::ACPI_XSDT) {
                    trace(TRACE_ACPI, "mem=%p, Skipping ACPI XSDT at %llx.", md, md->addr);
                    continue;
                }

                if (md->type == Hip_mem::HYP_LOG) {
                    trace(TRACE_ACPI, "mem=%p, Skipping hypervisor log buffer at %llx", md, md->addr);
                    continue;
                }

                if (md->type == Hip_mem::SYSTAB) {
                    trace(TRACE_ACPI, "mem=%p, Skipping systab at %llx", md, md->addr);
                    continue;
                }

                /* Overlap of md region with start of MAS region, just correct NUMA ID */
                if (start >= md->addr && start < (md->addr + md->size) && md->size <= size)
                {
                    trace(TRACE_ACPI, "mem=%p, Skipped memory region %lx of size %lx", md, start, size);
                    new_chunk = false;
                    md->domain = numa_id;
                    continue;
                }

                /* MAS region overlaps the md region completely, just correct NUMA ID */
                if (start < md->addr && (md->addr + md->size) < (start+size)) {
                    trace(TRACE_ACPI, "mem=%p, Updated NUMA id for memory region %llx of size %llx", md, md->addr, md->size);
                    new_chunk = false;
                    md->domain = numa_id;
                    continue;
                }

                /* Overlap of md region with start of MAS region, shrink region in HIP accordingly */
                if (start < md->addr && (start + size) >= (md->addr + md->size)) {
                    trace(TRACE_ACPI, "mem=%p, Shrink memory region %llx of size %llx to size %llx", md, md->addr, md->size, (start + size) - md->addr);
                    Acpi_table_srat::add_mementry(md, md->addr, (start + size) - md->addr, numa_id, true);
                    new_chunk = false;
                    continue;
                }

                /* If we find a memory chunk in the SRAT that is smaller than previousliy reported by multiboot,
                 * then overwrite the memory descriptor for it with the values from SRAT.
                */ 
                if (start >= md->addr && start < (md->addr + md->size) && (start + size) <= (md->addr + md->size))
                {
                    if (start == md->addr && size == md->size) // memory chunk already registered
                        break;

                    trace(TRACE_ACPI, "mem=%p: Replaced memory region %llx of size %llx with region %lx of size %lx", mem, md->addr, md->size, start, size);
                    new_chunk = false;
                    Acpi_table_srat::add_mementry(md, start, size, numa_id, true);
                    //replaced_chunk = true;
                    continue;
                }
            }
            if (new_chunk) {
                /* Discovered memory not covered by HIP memory descriptors yet, add it. */
                Acpi_table_srat::add_mementry(mem, start, size, numa_id, false);
            }
        }
    }
}

void Acpi_table_srat::add_mementry(Hip_mem *me, uint64 start, size_t size, uint32 numa_id, bool replacement ) 
{
    //trace(TRACE_ACPI, "mem=: MEM range %x -- %llx -> NUMA node %d is regular", start, (start + size), numa_id);
    if (replacement)
        trace(TRACE_ACPI, "memory block is replaced at %p with %llx of size %lx at node %d", me, start, size, numa_id);
    
    struct Acpi_srat_hip_mem block { start, size, 1 };
    Hip::add_mem<Acpi_srat_hip_mem>(me, &block, numa_id);
    
    if (!replacement)
        Hip::hip()->length = static_cast<uint16>(reinterpret_cast<mword>(me) - reinterpret_cast<mword>(Hip::hip()));
}

void Acpi_table_srat::parse_gias(Acpi_srat_entry const *ptr)
{
    // not implemented yet
    trace(TRACE_ACPI, "Skipping GIAS at %p", ptr);
    return;
}

void Acpi_table_srat::parse_giccas(Acpi_srat_entry const *ptr)
{
    trace(TRACE_ACPI, "Skipping GICCAS at %p", ptr);
    return;
}

void Acpi_table_srat::parse_gicits(Acpi_srat_entry const *ptr)
{
    trace(TRACE_ACPI, "Skipping GICITS at %p", ptr);
    return;
}

void Acpi_table_srat::parse_x2apic(Acpi_srat_entry const *ptr)
{
    Acpi_srat_x2apic const *p = static_cast<Acpi_srat_x2apic const *>(ptr);

    if (p->flags & 1)
    {
        uint32 numa_id = p->domain;

        trace(TRACE_ACPI, "CPU with x2APIC %u - NUMA region %u", p->apic_id, numa_id);
    }
}