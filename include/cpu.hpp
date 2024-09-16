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

#pragma once

#include "compiler.hpp"
#include "config.hpp"
#include "types.hpp"
#include "assert.hpp"
#include "macros.hpp"

class Cpu
{
    private:
        static char const * const vendor_string[];

        ALWAYS_INLINE
        static inline void check_features();

        ALWAYS_INLINE
        static inline void setup_thermal();

        ALWAYS_INLINE
        static inline void setup_sysenter();

        ALWAYS_INLINE
        static inline void setup_pcid();

    public:
        enum Vendor
        {
            UNKNOWN,
            INTEL,
            AMD
        };

        enum Core_type
        {
            INTEL_ATOM = 0x20,
            INTEL_CORE = 0x40,
        };

        enum Feature
        {
            FEAT_MCE            =  7,
            FEAT_SEP            = 11,
            FEAT_MCA            = 14,
            FEAT_X2APIC         = 21,
            FEAT_ACPI           = 22,
            FEAT_HTT            = 28,
            FEAT_MONITOR_MWAIT  = 32 * 1 +  3,
            FEAT_VMX            = 32 * 1 +  5,
            FEAT_PCID           = 32 * 1 + 17,
            FEAT_TSC_DEADLINE   = 32 * 1 + 24,
            FEAT_XSAVE          = 32 * 1 + 26,
            FEAT_CPU_TEMP       = 64,
            FEAT_PKG_TEMP       = 70,
            FEAT_HWP_7          = 71,
            FEAT_HWP_9          = 73,
            FEAT_HWP_10         = 74,
            FEAT_HWP_11         = 75,
            FEAT_SMEP           = 103,
            FEAT_SMAP           = 116,
            FEAT_1GB_PAGES      = 154,
            FEAT_RDTSCP         = 32 * 4 + 27,
            FEAT_CMP_LEGACY     = 161,
            FEAT_SVM            = 162,
            FEAT_HCFC           = 32 *  6,
            FEAT_EPB            = 32 *  6 + 3,
            FEAT_PSTATE_AMD     = 32 *  7 + 7,
            FEAT_TSC_INVARIANT  = 32 *  7 + 8,
            FEAT_MWAIT_EXT      = 32 *  8 + 0,
            FEAT_MWAIT_IRQ      = 32 *  8 + 1,
            FEAT_XSAVEOPT       = 32 * 10 + 0,
            FEAT_FPU_COMPACT    = 32 * 10 + 3
        };

        enum
        {
            EXC_DB          = 1,
            EXC_NM          = 7,
            EXC_TS          = 10,
            EXC_GP          = 13,
            EXC_PF          = 14,
            EXC_AC          = 17,
            EXC_MC          = 18,
        };

        enum
        {
            CR0_PE          = 1ul << 0,         // 0x1
            CR0_MP          = 1ul << 1,         // 0x2
            CR0_EM          = 1ul << 2,         // 0x4
            CR0_TS          = 1ul << 3,         // 0x8
            CR0_ET          = 1ul << 4,         // 0x10
            CR0_NE          = 1ul << 5,         // 0x20
            CR0_WP          = 1ul << 16,        // 0x10000
            CR0_AM          = 1ul << 18,        // 0x40000
            CR0_NW          = 1ul << 29,        // 0x20000000
            CR0_CD          = 1ul << 30,        // 0x40000000
            CR0_PG          = 1ul << 31         // 0x80000000
        };

        enum
        {
            CR4_DE          = BIT64 ( 3),
            CR4_PSE         = BIT64 ( 4),
            CR4_PAE         = BIT64 ( 5),
            CR4_MCE         = BIT64 ( 6),
            CR4_PGE         = BIT64 ( 7),
            CR4_OSFXSR      = BIT64 ( 9),
            CR4_OSXMMEXCPT  = BIT64 (10),
            CR4_VMXE        = BIT64 (13),
            CR4_SMXE        = BIT64 (14),
            CR4_PCIDE       = BIT64 (17),
            CR4_OSXSAVE     = BIT64 (18),
            CR4_SMEP        = BIT64 (20),
            CR4_SMAP        = BIT64 (21),
        };

        enum
        {
            EFER_LME        = 1UL << 8,         // 0x100
            EFER_LMA        = 1UL << 10,        // 0x400
            EFER_SVME       = 1UL << 12,        // 0x1000
        };

        enum
        {
            EFL_CF      = 1ul << 0,             // 0x1
            EFL_PF      = 1ul << 2,             // 0x4
            EFL_AF      = 1ul << 4,             // 0x10
            EFL_ZF      = 1ul << 6,             // 0x40
            EFL_SF      = 1ul << 7,             // 0x80
            EFL_TF      = 1ul << 8,             // 0x100
            EFL_IF      = 1ul << 9,             // 0x200
            EFL_DF      = 1ul << 10,            // 0x400
            EFL_OF      = 1ul << 11,            // 0x800
            EFL_IOPL    = 3ul << 12,            // 0x3000
            EFL_NT      = 1ul << 14,            // 0x4000
            EFL_RF      = 1ul << 16,            // 0x10000
            EFL_VM      = 1ul << 17,            // 0x20000
            EFL_AC      = 1ul << 18,            // 0x40000
            EFL_VIF     = 1ul << 19,            // 0x80000
            EFL_VIP     = 1ul << 20,            // 0x100000
            EFL_ID      = 1ul << 21             // 0x200000
        };

        struct alignas(64) idle_flag {
            alignas(64) bool volatile idle;
        };

        static mword boot_lock asm("boot_lock");

        static unsigned online;
        static uint8    acpi_id[NUM_CPU];
        static uint8    apic_id[NUM_CPU];
        static uint8    numa_id[NUM_CPU];

        static uint8    package[NUM_CPU];
        static uint8    core[NUM_CPU];
        static uint8    thread[NUM_CPU];

        static uint8    platform[NUM_CPU];
        static uint8    family[NUM_CPU];
        static uint8    model[NUM_CPU];
        static uint8    stepping[NUM_CPU];
        static uint8    core_type[NUM_CPU];
        static unsigned patch[NUM_CPU];

        static struct idle_flag idle[NUM_CPU];

        static unsigned id                  CPULOCAL_HOT;
        static unsigned hazard              CPULOCAL_HOT;
        static Vendor   vendor              CPULOCAL;
        static unsigned brand               CPULOCAL;
        static unsigned row                 CPULOCAL;

        static uint32 name[12]              CPULOCAL;
        static uint32 features[11]          CPULOCAL;
        static bool bsp                     CPULOCAL;
        static bool preemption              CPULOCAL;
        static unsigned mwait_hint          CPULOCAL;

        static void init(bool = false);

        ALWAYS_INLINE
        static inline bool feature (Feature f)
        {
            return features[f / 32] & 1U << f % 32;
        }

        ALWAYS_INLINE
        static inline void defeature (Feature f)
        {
            features[f / 32] &= ~(1U << f % 32);
        }

        ALWAYS_INLINE
        static inline void preempt_disable()
        {
            assert (preemption);

            asm volatile ("cli" : : : "memory");
            preemption = false;
        }

        ALWAYS_INLINE
        static inline void preempt_enable()
        {
            assert (!preemption);

            preemption = true;
            asm volatile ("sti" : : : "memory");
        }

        ALWAYS_INLINE
        static inline bool preempt_status()
        {
            mword flags = 0;
            asm volatile ("pushf; pop %0" : "=r" (flags));
            return flags & 0x200;
        }

        ALWAYS_INLINE
        static inline void preemption_point()
        {
            asm volatile ("sti; nop; cli" : : : "memory");
        }

        ALWAYS_INLINE
        static inline void cpuid (unsigned leaf, uint32 &eax, uint32 &ebx, uint32 &ecx, uint32 &edx)
        {
            asm volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (leaf));
        }

        ALWAYS_INLINE
        static inline void cpuid (unsigned leaf, unsigned subleaf, uint32 &eax, uint32 &ebx, uint32 &ecx, uint32 &edx)
        {
            asm volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (leaf), "c" (subleaf));
        }

        ALWAYS_INLINE
        static unsigned find_by_apic_id (unsigned x)
        {
            for (unsigned i = 0; i < NUM_CPU; i++)
                if (apic_id[i] == x)
                    return i;

            return ~0U;
        }

        ALWAYS_INLINE
        static void halt_or_mwait(auto const &halt, auto const &mwait)
        {
            if (!Cpu::feature (Cpu::FEAT_MONITOR_MWAIT) || mwait_hint == ~0U) {
                halt();
                return;
            }

            if (Cpu::feature (Cpu::FEAT_MWAIT_EXT))
                mwait(mwait_hint);
            else
                mwait(0u);
        }
};
