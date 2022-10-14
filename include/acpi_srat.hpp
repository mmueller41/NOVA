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

#pragma once

#include "acpi_table.hpp"

#pragma pack(1)

class Acpi_srat_entry 
{
    public:
        uint8 type;
        uint8 length;

        enum Type
        {
            LAPIC = 0,
            MAS = 1,
            X2APIC = 2,
            GICCAS = 3,
            GICITS = 4,
            GIAS = 5
        };
};

class Acpi_processor : public Acpi_srat_entry
{

public:
    uint8 domain_lo;
    uint8 apic_id;
    uint32 flags;
    uint8 local_sapic_eid;
    uint8 domain_hi[3];
    uint32 clock_domain;
};

class Acpi_srat_memtry : public Acpi_srat_entry
{
    public:
        uint32 domain;
        uint16 reserved1;
        uint32 base_addr_lo;
        uint32 base_addr_hi;
        uint32 length_lo;
        uint32 length_hi;
        uint32 reserved2;
        uint32 flag_enabled : 1,
            flag_hotplug : 1,
            flag_nvm : 1, : 29;
        uint64 reserved;
};

class Acpi_srat_x2apic : public Acpi_srat_entry
{
    public:
        uint16 reserved;
        uint32 domain;
        uint32 apic_id;
        uint32 flags;
        uint32 clock_domain;
        uint32 reserved2;
};

class Acpi_srat_gicc : public Acpi_srat_entry
{
    public:
        uint32 domain;
        uint32 acpi_processor_uid;
        uint32 flag_enabled : 1, : 31;
        uint32 clock_domain;
};

class Acpi_srat_gicits : public Acpi_srat_entry
{
    public:
        uint32 domain;
        uint16 reserved;
        uint32 its_id;
};

class Acpi_srat_gias : public Acpi_srat_entry
{
    public:
        uint8 reserved;
        uint8 dev_handle_type;
        uint32 domain;
        union {
            struct {
                uint64 hid;
                uint32 uid;
                uint32 reserved;
            } acpi_handle;
            struct {
                uint16 segment;
                uint16 bdf;
                uint32 reserved[3];
            } pci_handle;
        };
        uint32 flag_enabled : 1,
            flag_arch_transact : 1,
                               : 30;

        enum Handle_type
        {
            ACPI = 0,
            PCI = 1
        };
};

/*
 * System Resource Affinity Table
 */
class Acpi_table_srat : public Acpi_table
{
    private:
        INIT static void parse_lapic(Acpi_srat_entry const *);

        INIT static void parse_mas(Acpi_srat_entry const *);

        INIT static void parse_x2apic(Acpi_srat_entry const *);

        INIT static void parse_giccas(Acpi_srat_entry const *);

        INIT static void parse_gicits(Acpi_srat_entry const *);

        INIT static void parse_gias(Acpi_srat_entry const *);

        INIT void parse_entry(Acpi_srat_entry::Type, void (*)(Acpi_srat_entry const *)) const;
    
    public:
        uint32 reserved;
        uint64 reserved2;
        Acpi_srat_entry sras[];

        INIT void parse() const;
};

struct Acpi_srat_hip_mem {
    uint64 addr;
    uint64 len;
    uint32 type;
};

#pragma pack()
