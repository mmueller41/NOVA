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

#include "acpi_ivrs.hpp"
#include "cmdline.hpp"
#include "hip.hpp"
#include "hpet.hpp"
#include "ioapic.hpp"
#include "iommu_amd.hpp"
#include "pci.hpp"
#include "pd.hpp"
#include "console.hpp"
 
void Acpi_ivhd::parse(Acpi_ivdb::Type const ivdb, bool const efeat_valid) const
{
    Iommu::Amd * const iommu = new (Pd::kern.quota) Iommu::Amd (static_cast<Paddr>(base), id, efeat_valid);

    if (iommu->invalid())
        return;

    /* invalid */
    unsigned rid     = ~0U;
    unsigned rid_eff = 1U << 16;
    uint8    setting = 0;

    for_each_entry(ivdb, [&](Acpi_device_entry const &e) {
        switch (e.type) {
            case Acpi_device_entry::Dte::DTE_0 : break;
            case Acpi_device_entry::Dte::DTE_ONE:
                iommu->alloc_dtb(0, e.devid, e.devid, e.setting);
                Pci::claim_dev_single(iommu, e.devid);
                break;
            case Acpi_device_entry::Dte::DTE_ALL:
                iommu->alloc_dtb(9, 0, (1u << 16) - 1, e.setting);
                Pci::claim_all(iommu);
                break;
            case Acpi_device_entry::Dte::DTE_START:
                rid     = e.devid;
                rid_eff = 1U << 16; /* invalid */
                setting = e.setting;
                break;
            case Acpi_device_entry::Dte::DTE_END:
            {
                if (rid > e.devid) return;
                if (rid >= 1U << 16) return;

                unsigned const mem = 32U * (e.devid - rid + 1);
                long o = bit_scan_reverse(mem);
                if ((1U << o) < mem) o++;
                if (o < 12) o = 12;

                iommu->alloc_dtb((o - 12) & 0xff, uint16(rid), uint16(e.devid), setting);

                for (unsigned r = rid; r < e.devid; r++) {
                    Pci::claim_dev_single(iommu, r);

                    if (rid_eff < (1U << 16))
                        iommu->alias(uint16(rid), uint16(rid_eff));
                }
                /* reset to invalid */
                rid     = ~0U;
                rid_eff = 1U << 16;
                setting = 0;
                break;
            }
            case Acpi_device_entry::Dte::DTE_SPECIAL:
                if (e.variety == 1) {
                    iommu->alloc_dtb(0, e.pdevid, e.pdevid, e.setting);
                    Ioapic::claim_dev(e.pdevid, e.handle, iommu);
                }
                if (e.variety == 2) {
                    iommu->alloc_dtb(0, e.pdevid, e.pdevid, e.setting);
                    Hpet::claim_dev(e.pdevid, e.handle);
                }
                break;
            case Acpi_device_entry::Dte::DTE_ALIAS_ONE:
                iommu->alloc_dtb(0, e.devid, e.devid, e.setting);
                Pci::claim_dev_single(iommu, e.devid);
                iommu->alias(e.devid, e.pdevid);
                break;
            case Acpi_device_entry::Dte::DTE_EXT_ONE:
                /* XXX ext_set ATS (31 bit) */
                iommu->alloc_dtb(0, e.devid, e.devid, e.setting);
                Pci::claim_dev_single(iommu, e.devid);
                break;
            case Acpi_device_entry::Dte::DTE_EXT_START:
                /* XXX ext_set ATS (31 bit) */
                rid     = e.devid;
                setting = e.setting;
                break;
            case Acpi_device_entry::Dte::DTE_ALIAS_START:
                rid     = e.devid;
                rid_eff = e.pdevid;
                setting = e.setting;
                break;
            default:
                Console::print("dte type unknown %#x", e.type);
        }
    });
}

void Acpi_table_ivrs::parse() const
{
    if (!Cmdline::iommu_amd)
        return;

    for_each([&](Acpi_ivdb const &r) {
        switch (r.type) {
            case Acpi_ivdb::IVHD_10:
            {
                bool use_ivhd_10 = true;
                if (ivinfo & 0x1) { /* ivhd 11 and 40 are reserved if not set */
                    for_each([&](Acpi_ivdb const &m){
                        if (m.type == Acpi_ivdb::IVHD_11 && m.id == r.id) {
                            use_ivhd_10 = false;
                            return true;
                        }
                        return false;
                    });
                }
                if (!use_ivhd_10)
                    break;

                static_cast<Acpi_ivhd const &>(r).parse(Acpi_ivdb::IVHD_10, ivinfo & 0x1);
                break;
            }
            case Acpi_ivdb::IVHD_11:
                static_cast<Acpi_ivhd const &>(r).parse(Acpi_ivdb::IVHD_11, ivinfo & 0x1);
                break;
            default:
                Console::print("ivdb type ignored %u", r.type);
                break;
        }
        return false;
    });

    Hip::set_feature (Hip::FEAT_IOMMU);
}

void Acpi_table_ivrs::init() const
{
    if (!Cmdline::iommu_amd)
        return;

    Iommu::Amd::enable ();
}
