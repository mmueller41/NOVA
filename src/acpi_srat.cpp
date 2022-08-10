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
#include "stdio.hpp"

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
    }

    // TODO: Use this information, i.e. save it in the resource model
}

void Acpi_table_srat::parse_mas(Acpi_srat_entry const *ptr)
{
    Acpi_srat_memtry const *p = static_cast<Acpi_srat_memtry const *>(ptr);

    if (p->flag_enabled) {
        uint32 numa_id = p->domain;

        size_t start = static_cast<uint64>(p->base_addr_lo) | (static_cast<uint64>(p->base_addr_hi) << 32);
        size_t size = static_cast<uint64>(p->length_lo) | (static_cast<uint64>(p->length_hi) << 32);

        if (p->flag_nvm) {
            trace(TRACE_ACPI, "MEM range %lx -- %lx -> NUMA node %d is non-volatile", start, (start + size), numa_id);
        } else {
            trace(TRACE_ACPI, "MEM range %lx -- %lx -> NUMA node %d is regular", start, (start + size), numa_id);
        }
    }
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