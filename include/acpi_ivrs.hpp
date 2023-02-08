/*
 * ACPI - AMD I/O Virtualization Reporting Structure (IVRS)
 *
 * Copyright (C) 2020 Alexander Boettcher <alexander.boettcher@genode-labs.com>
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

#pragma once

#include "acpi_table.hpp"

class Acpi_device_entry
{
    public:
        uint8  type;
        uint16 devid;
        uint8  setting;
        uint8  handle;
        uint16 pdevid;
        uint8  variety;

        enum Dte
        {
            DTE_0           = 0x00,
            DTE_ALL         = 0x01,
            DTE_ONE         = 0x02,
            DTE_START       = 0x03,
            DTE_END         = 0x04,
            DTE_ALIAS_ONE   = 0x42,
            DTE_ALIAS_START = 0x43,
            DTE_EXT_ONE     = 0x46,
            DTE_EXT_START   = 0x47,
            DTE_SPECIAL     = 0x48
        };
} PACKED;

class Acpi_ivdb
{
    public:
        uint8  type;
        uint8  flags;
        uint16 length;
        uint16 id;
        uint16 cap_offset;
        uint64 base;
        uint16 pciseg;
        uint16 info;
        uint32 feature;
        union {
            uint32 entry_10;
            uint32 efr_low;
        };
        uint32 efr_high;
        uint8  unused[8];
        uint32 entry_11_40;

        enum Type
        {
            IVHD_10 = 0x10,
            IVHD_11 = 0x11,
            IVHD_40 = 0x40,
        };

        template <typename FUNC>
        inline void for_each_entry(Type const ivrs, FUNC const &fn) const
        {
            unsigned len = 8;

            void const * ptr = (ivrs == IVHD_10) ? &entry_10 : &entry_11_40;
            for (Acpi_device_entry const *e = reinterpret_cast<Acpi_device_entry const *>(ptr);
                 e < reinterpret_cast<Acpi_device_entry const *>(reinterpret_cast<mword>(this) + length);
                 e = reinterpret_cast<Acpi_device_entry const *>(reinterpret_cast<mword>(e) + len))
            {
                if (e->type < 64) len = 4;
                else if (e->type < 128) len = 8;

                fn(*e);

                /* variable length unsupported, len=? */
                if (e->type >= 128) break;
            }
        }
} PACKED;

class Acpi_ivhd : public Acpi_ivdb
{
    public:

        INIT
        void parse(Acpi_ivdb::Type, bool) const;
};

class Acpi_table_ivrs : public Acpi_table
{
    private:

        template <typename FUNC>
        inline void for_each(FUNC const &fn) const
        {
            for (Acpi_ivdb const *r = ivdb;
                 r < reinterpret_cast<Acpi_ivdb *>(reinterpret_cast<mword>(this) + length);
                 r = reinterpret_cast<Acpi_ivdb *>(reinterpret_cast<mword>(r) + r->length))
                if (fn(*r)) break;
        }

    public:
        uint32     ivinfo;
        uint8      reserved[8];
        Acpi_ivdb  ivdb[];

        INIT
        void parse() const;

        void init() const;
};
