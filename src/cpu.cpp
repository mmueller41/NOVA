/*
 * Central Processing Unit (CPU)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
 * Copyright (C) 2015 Alexander Boettcher, Genode Labs GmbH
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

#include "bits.hpp"
#include "cmdline.hpp"
#include "counter.hpp"
#include "gdt.hpp"
#include "hip.hpp"
#include "idt.hpp"
#include "lapic.hpp"
#include "mca.hpp"
#include "msr.hpp"
#include "pd.hpp"
#include "stdio.hpp"
#include "svm.hpp"
#include "tss.hpp"
#include "vmx.hpp"

char const * const Cpu::vendor_string[] =
{
    "Unknown",
    "GenuineIntel",
    "AuthenticAMD"
};

mword       Cpu::boot_lock;

// Order of these matters
unsigned    Cpu::online;
uint8       Cpu::acpi_id[NUM_CPU];
uint8       Cpu::apic_id[NUM_CPU];

unsigned    Cpu::id;
unsigned    Cpu::hazard;
uint8       Cpu::package[NUM_CPU];
uint8       Cpu::core[NUM_CPU];
uint8       Cpu::thread[NUM_CPU];

Cpu::Vendor Cpu::vendor;
uint8       Cpu::platform[NUM_CPU];
uint8       Cpu::family[NUM_CPU];
uint8       Cpu::model[NUM_CPU];
uint8       Cpu::stepping[NUM_CPU];
uint8       Cpu::core_type[NUM_CPU];
unsigned    Cpu::brand;
unsigned    Cpu::patch[NUM_CPU];
unsigned    Cpu::row;

uint32      Cpu::name[12];
uint32      Cpu::features[11];
bool        Cpu::bsp;
bool        Cpu::preemption;
unsigned    Cpu::mwait_hint;

void Cpu::check_features()
{
    unsigned top = 0, tpp = 1, cpp = 1;

    uint32 eax = 0, ebx = 0, ecx = 0, edx = 0;

    cpuid (0, eax, ebx, ecx, edx);

    size_t v;
    for (v = sizeof (vendor_string) / sizeof (*vendor_string); --v;)
        if (*reinterpret_cast<uint32 const *>(vendor_string[v] + 0) == ebx &&
            *reinterpret_cast<uint32 const *>(vendor_string[v] + 4) == edx &&
            *reinterpret_cast<uint32 const *>(vendor_string[v] + 8) == ecx)
            break;

    vendor = Vendor (v);

    if (vendor == INTEL) {
        Msr::write<uint64>(Msr::IA32_BIOS_SIGN_ID, 0);
        platform[Cpu::id] = static_cast<unsigned>(Msr::read<uint64>(Msr::IA32_PLATFORM_ID) >> 50) & 7;
    }

    switch (static_cast<uint8>(eax)) {
        case 0x1a ... 0xff:
            eax = ebx = ecx = edx = 0;
            cpuid (0x1a, 0, eax, ebx, ecx, edx);
            core_type[Cpu::id] = uint8((eax >> 24) & 0xffu);
            [[fallthrough]];
        default:
            eax = ebx = ecx = edx = 0;
            cpuid (0xd, 0, eax, ebx, ecx, edx);
            Fpu::hst_xsv.xcr = Fpu::managed & ((uint64(edx) << 32) | eax);

            eax = ebx = ecx = edx = 0;
            cpuid (0xd, 1, features[10], ebx, ecx, edx);
            Fpu::hst_xsv.xss = Fpu::managed & ((uint64(edx) << 32) | ecx);

            Fpu::compact = !!(Cpu::feature (Cpu::FEAT_FPU_COMPACT));

            [[fallthrough]];
        case 0x7 ... 0xc:
            eax = ebx = ecx = edx = 0;
            cpuid (0x7, 0, eax, features[3], ecx, edx);
            /* hybrid flag (edx & (1u << 15)) */
            [[fallthrough]];
        case 0x6:
            eax = ebx = ecx = edx = 0;
            cpuid (0x6, features[2], ebx, features[6], edx);
            [[fallthrough]];
        case 0x5:
            eax = ebx = ecx = edx = 0;
            cpuid (0x5, 0, eax, ebx, features[8], features[9]);
            [[fallthrough]];
        case 0x4:
            eax = ebx = ecx = edx = 0;
            cpuid (0x4, 0, eax, ebx, ecx, edx);
            cpp = (eax >> 26 & 0x3f) + 1;
            [[fallthrough]];
        case 0x1 ... 0x3:
            eax = ebx = ecx = edx = 0;
            cpuid (0x1, eax, ebx, features[1], features[0]);
            family[Cpu::id]   = ((eax >> 8 & 0xf) + (eax >> 20 & 0xff)) & 0xff;
            model[Cpu::id]    = ((eax >> 4 & 0xf) + (eax >> 12 & 0xf0)) & 0xff;
            stepping[Cpu::id] =  eax & 0xf;
            brand    =  ebx & 0xff;
            top      =  ebx >> 24;
            tpp      =  ebx >> 16 & 0xff;
    }

    patch[Cpu::id] = static_cast<unsigned>(Msr::read<uint64>(Msr::IA32_BIOS_SIGN_ID) >> 32);

    eax = ebx = ecx = edx = 0;
    cpuid (0x80000000, eax, ebx, ecx, edx);

    unsigned smt = 0;

    if (eax & 0x80000000) {
        switch (static_cast<uint8>(eax)) {
            case 0x1e ... 0xff:
                if (vendor == AMD && family[Cpu::id] >= 0x17) {
                    eax = ebx = ecx = edx = 0;
                    cpuid (0x8000001e, eax, ebx, ecx, edx);
                    smt = ((ebx >> 8) & 0xff) + 1;
                }
                [[fallthrough]];
            default:
                eax = ebx = ecx = edx = 0;
                cpuid (0x8000000a, Vmcb::svm_version, ebx, ecx, Vmcb::svm_feature);

                if (ebx < Space_mem::asid_alloc.max())
                    Space_mem::asid_alloc.reserve(ebx, Space_mem::asid_alloc.max() - ebx);

                [[fallthrough]];
            case 0x8 ... 0x9:
                if (vendor == AMD && smt) {
                    eax = ebx = ecx = edx = 0;
                    cpuid (0x80000008, eax, ebx, tpp, edx);
                    if ((tpp >> 12) & 0xf)
                        tpp = 1 << ((tpp >> 12) & 0xf);
                    else
                        tpp = (tpp & 0xff) + 1;

                    cpp = tpp / smt;
                }
                [[fallthrough]];
            case 0x7:
                eax = ebx = ecx = edx = 0;
                cpuid (0x80000007, eax, ebx, ecx, features[7]);
                [[fallthrough]];
            case 0x4 ... 0x6:
                eax = ebx = ecx = edx = 0;
                cpuid (0x80000004, name[8], name[9], name[10], name[11]);
                [[fallthrough]];
            case 0x3:
                eax = ebx = ecx = edx = 0;
                cpuid (0x80000003, name[4], name[5], name[6], name[7]);
                [[fallthrough]];
            case 0x2:
                eax = ebx = ecx = edx = 0;
                cpuid (0x80000002, name[0], name[1], name[2], name[3]);
                [[fallthrough]];
            case 0x1:
                eax = ebx = ecx = edx = 0;
                cpuid (0x80000001, eax, ebx, features[5], features[4]);
        }

        if (vendor == AMD && smt)
            defeature (FEAT_CMP_LEGACY);
    }

    if (feature (FEAT_CMP_LEGACY))
        cpp = tpp;

    unsigned tpc = tpp / cpp;
    unsigned long t_bits = bit_scan_reverse (tpc - 1) + 1;
    unsigned long c_bits = bit_scan_reverse (cpp - 1) + 1;

    thread[Cpu::id]  = (top            & ((1u << t_bits) - 1)) & 0xff;
    core[Cpu::id]    = (top >>  t_bits & ((1u << c_bits) - 1)) & 0xff;
    package[Cpu::id] = (top >> (t_bits + c_bits)) & 0xff;

    // Disable C1E on AMD Rev.F and beyond because it stops LAPIC clock
    if (vendor == AMD)
        if (family[Cpu::id] == 0x10 || (family[Cpu::id] == 0xf && model[Cpu::id] >= 0x40))
            Msr::write (Msr::AMD_IPMR, Msr::read<uint32>(Msr::AMD_IPMR) & ~(3ul << 27));

    // enable PAT if available
    eax = ebx = ecx = edx = 0;
    cpuid (0x1, eax, ebx, ecx, edx);
    if (edx & (1 << 16)) {
        uint32 cr_pat = Msr::read<uint32>(Msr::IA32_CR_PAT) & 0xffff00ff;

        cr_pat |= 1 << 8;
        Msr::write<uint32>(Msr::IA32_CR_PAT, cr_pat);
    } else
        trace (0, "warning: no PAT support");
}

void Cpu::setup_thermal()
{
    Msr::write (Msr::IA32_THERM_INTERRUPT, 0x10);
}

void Cpu::setup_sysenter()
{
#ifdef __i386__
    Msr::write<mword>(Msr::IA32_SYSENTER_CS,  SEL_KERN_CODE);
    Msr::write<mword>(Msr::IA32_SYSENTER_ESP, reinterpret_cast<mword>(&Tss::run.sp0));
    Msr::write<mword>(Msr::IA32_SYSENTER_EIP, reinterpret_cast<mword>(&entry_sysenter));
#else
    Msr::write<mword>(Msr::IA32_STAR,  static_cast<mword>(SEL_USER_CODE) << 48 | static_cast<mword>(SEL_KERN_CODE) << 32);
    Msr::write<mword>(Msr::IA32_LSTAR, reinterpret_cast<mword>(&entry_sysenter));
    Msr::write<mword>(Msr::IA32_SFMASK, Cpu::EFL_DF | Cpu::EFL_IF | Cpu::EFL_NT | Cpu::EFL_TF);
#endif
}

void Cpu::setup_pcid()
{
#ifdef __x86_64__
    if (EXPECT_FALSE (Cmdline::nopcid))
#endif
        defeature (FEAT_PCID);

    if (EXPECT_FALSE (!feature (FEAT_PCID)))
        return;

    set_cr4 (get_cr4() | Cpu::CR4_PCIDE);
}

void Cpu::init(bool resume)
{
    if (!resume)
        for (void (**func)() = &CTORS_L; func != &CTORS_C; (*func++)()) ;

    Gdt::build();
    Tss::build();

    // Initialize exception handling
    Gdt::load();
    Tss::load();
    Idt::load();

    if (!resume) {
        Lapic::init_cpuid();

        if (Cpu::bsp)
            Lapic::ap_code_prepare();
    }

    /* handle case running on machine with too many CPUs */
    if (id >= NUM_CPU) {
        boot_lock++;
        shutdown();
    }

    /*
     * hwdev_addr is decremented by PCI & IOAPIC & IOMMU objects and
     * moves towards HV_GLOBAL_CPUS. If intersection happens we will run into
     * corruption issues, so detect the case and stop early.
     */
    if (hwdev_addr < HV_GLOBAL_CPUS + NUM_CPU * PAGE_SIZE) {
        trace (0, "Too many CPUS and PCI & IOAPIC & IOMMU devices");
        shutdown();
    }

    static_assert (HV_GLOBAL_MAX / PAGE_SIZE >= NUM_CPU, "Too many CPUs configured");

    // Initialize CPU number and check features
    check_features();

    Lapic::init(Cpu::feature(Cpu::Feature::FEAT_TSC_INVARIANT));

    if (!resume) {
        row = Console_vga::con.spinner (id);

        Paddr phys; mword attr;
        Pd::kern.Space_mem::loc[id] = Hptp (Hpt::current());
        Pd::kern.Space_mem::loc[id].lookup (CPU_LOCAL_DATA, phys, attr);
        Pd::kern.Space_mem::insert (Pd::kern.quota, HV_GLOBAL_CPUS + id * PAGE_SIZE, 0, Hpt::HPT_NX | Hpt::HPT_G | Hpt::HPT_W | Hpt::HPT_P, phys);
        Hpt::ord = min (Hpt::ord, feature (FEAT_1GB_PAGES) ? 26UL : 17UL);
    }

    if (EXPECT_TRUE (feature (FEAT_ACPI)))
        setup_thermal();

    if (EXPECT_TRUE (feature (FEAT_SEP)))
        setup_sysenter();

    setup_pcid();

    mword cr4 = get_cr4();

    if (EXPECT_TRUE (feature (FEAT_SMEP ))) cr4 |= Cpu::CR4_SMEP;
    if (EXPECT_TRUE (feature (FEAT_SMAP ))) cr4 |= Cpu::CR4_SMAP;
#ifdef __x86_64__
    if (EXPECT_TRUE (feature (FEAT_XSAVE))) cr4 |= Cpu::CR4_OSXSAVE;
#else
    Cpu::defeature (Cpu::FEAT_XSAVE);
#endif

    if (cr4 != get_cr4())
        set_cr4 (cr4);

    {
        Fpu::probe();

        /* XSAVE may be disabled by FPU::probe if state is too large */
        if (cr4 & Cpu::CR4_OSXSAVE && !feature(FEAT_XSAVE)) {
            cr4 &= ~mword(Cpu::CR4_OSXSAVE);
            set_cr4 (cr4);
        }
    }

    Vmcs::init();
    Vmcb::init();

    Mca::init();

    if (EXPECT_FALSE (Cmdline::hlt)) {
        Cpu::defeature (Cpu::FEAT_MONITOR_MWAIT);
        Cpu::defeature (Cpu::FEAT_MWAIT_EXT);
        Cpu::defeature (Cpu::FEAT_MWAIT_IRQ);
    }

    trace (TRACE_CPU, "CORE:%02x:%02x:%x %x:%x:%x:%x [%x] %s%.48s %s%s%s%s",
           package[Cpu::id], core[Cpu::id], thread[Cpu::id], family[Cpu::id],
           model[Cpu::id], stepping[Cpu::id], platform[Cpu::id], patch[Cpu::id],
           core_type[Cpu::id] == 0x00 ? ""   :
           core_type[Cpu::id] == Cpu::INTEL_CORE ? "P " :
           core_type[Cpu::id] == Cpu::INTEL_ATOM ? "E " : "? ",
           reinterpret_cast<char *>(name),
           Cpu::feature (Cpu::FEAT_MONITOR_MWAIT) ? "MWAIT" : "HLT",
           Cpu::feature (Cpu::FEAT_MWAIT_EXT) ? "+E" : "",
           Cpu::feature (Cpu::FEAT_MWAIT_IRQ) ? "+I" : "",
           cr4 & Cpu::CR4_OSXSAVE ? " X" : "");

    if (!resume)
        Hip::add_cpu();

    if (Cpu::feature (Cpu::FEAT_RDTSCP))
        Msr::write<uint64>(Msr::IA32_TSC_AUX, Cpu::id);

    Cpu::mwait_hint = ~0U; /* invalid */

    boot_lock++;
}
