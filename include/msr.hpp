/*
 * Model-Specific Registers (MSR)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
 * Copyright (C) 2017-2023 Alexander Boettcher
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

#include "arch.hpp"
#include "compiler.hpp"
#include "types.hpp"

struct Utcb;
struct Kobject;

class Msr
{
    private:

        static void user_access_amd(Utcb &);
        static void user_access_intel(Utcb &);

    public:

        // MSRs starting with IA32_ are architectural
        enum Register
        {
            DUMMY_MWAIT_HINT        = 0x0,
            IA32_TSC                = 0x10,
            IA32_PLATFORM_ID        = 0x17,
            IA32_APIC_BASE          = 0x1b,
            IA32_FEATURE_CONTROL    = 0x3a,
            IA32_BIOS_SIGN_ID       = 0x8b,
            IA32_SMM_MONITOR_CTL    = 0x9b,
            MSR_FSB_FREQ            = 0xcd,
            MSR_PLATFORM_INFO       = 0xce,
            IA32_MPERF              = 0xe7,
            IA32_APERF              = 0xe8,
            IA32_MTRR_CAP           = 0xfe,
            IA32_SYSENTER_CS        = 0x174,
            IA32_SYSENTER_ESP       = 0x175,
            IA32_SYSENTER_EIP       = 0x176,
            IA32_MCG_CAP            = 0x179,
            IA32_MCG_STATUS         = 0x17a,
            IA32_MCG_CTL            = 0x17b,
            IA32_THERM_INTERRUPT    = 0x19b,
            IA32_THERM_STATUS       = 0x19c,
            IA32_MISC_ENABLE        = 0x1a0,
            MSR_TEMPERATURE_TARGET  = 0x1a2,
            IA32_ENERGY_PERF_BIAS   = 0x1b0,
            IA32_THERM_PKG_STATUS   = 0x1b1,
            IA32_DEBUG_CTL          = 0x1d9,
            IA32_MTRR_PHYS_BASE     = 0x200,
            IA32_MTRR_PHYS_MASK     = 0x201,
            IA32_MTRR_FIX64K_BASE   = 0x250,
            IA32_MTRR_FIX16K_BASE   = 0x258,
            IA32_MTRR_FIX4K_BASE    = 0x268,
            IA32_CR_PAT             = 0x277,
            IA32_MTRR_DEF_TYPE      = 0x2ff,

            IA32_MCI_CTL            = 0x400,
            IA32_MCI_STATUS         = 0x401,

            IA32_VMX_BASIC          = 0x480,
            IA32_VMX_CTRL_PIN       = 0x481,
            IA32_VMX_CTRL_CPU0      = 0x482,
            IA32_VMX_CTRL_EXIT      = 0x483,
            IA32_VMX_CTRL_ENTRY     = 0x484,
            IA32_VMX_CTRL_MISC      = 0x485,
            IA32_VMX_CR0_FIXED0     = 0x486,
            IA32_VMX_CR0_FIXED1     = 0x487,
            IA32_VMX_CR4_FIXED0     = 0x488,
            IA32_VMX_CR4_FIXED1     = 0x489,
            IA32_VMX_VMCS_ENUM      = 0x48a,
            IA32_VMX_CTRL_CPU1      = 0x48b,
            IA32_VMX_EPT_VPID       = 0x48c,

            IA32_VMX_TRUE_PIN       = 0x48d,
            IA32_VMX_TRUE_CPU0      = 0x48e,
            IA32_VMX_TRUE_EXIT      = 0x48f,
            IA32_VMX_TRUE_ENTRY     = 0x490,

            MSR_CORE_C1_RESIDENCY   = 0x660,
            MSR_CORE_C3_RESIDENCY   = 0x3fc,
            MSR_CORE_C6_RESIDENCY   = 0x3fd,
            MSR_CORE_C7_RESIDENCY   = 0x3fe,

            MSR_PKG_C2_RESIDENCY    = 0x60d,
            MSR_PKG_C3_RESIDENCY    = 0x3f8,
            MSR_PKG_C6_RESIDENCY    = 0x3f9,
            MSR_PKG_C7_RESIDENCY    = 0x3fa,
            MSR_PKG_C8_RESIDENCY    = 0x630,
            MSR_PKG_C9_RESIDENCY    = 0x631,
            MSR_PKG_C10_RESIDENCY   = 0x632,

            IA32_DS_AREA            = 0x600,

            MSR_RAPL_POWER_UNIT     = 0x606,

            MSR_PKG_POWER_LIMIT     = 0x610,
            MSR_PKG_ENERGY_STATUS   = 0x611,
            MSR_PKG_PERF_STATUS     = 0x613,
            MSR_PKG_POWER_INFO      = 0x614,

            MSR_DRAM_POWER_LIMIT    = 0x618,
            MSR_DRAM_ENERGY_STATUS  = 0x619,
            MSR_DRAM_PERF_STATUS    = 0x61b,
            MSR_DRAM_POWER_INFO     = 0x61c,

            MSR_PP0_POWER_LIMIT     = 0x638,
            MSR_PP0_ENERGY_STATUS   = 0x639,
            MSR_PP0_POLICY          = 0x63a,
            MSR_PP0_PERF_STATUS     = 0x63b,

            MSR_PP1_POWER_LIMIT     = 0x640,
            MSR_PP1_ENERGY_STATUS   = 0x641,
            MSR_PP1_POLICY          = 0x642,

            IA32_TSC_DEADLINE       = 0x6e0,

            IA32_PM_ENABLE          = 0x770,
            IA32_HWP_CAPABILITIES   = 0x771,
            IA32_HWP_REQUEST_PKG    = 0x772,
            IA32_HWP_REQUEST        = 0x774,

            IA32_EXT_XAPIC          = 0x800,

            IA32_XSS                = 0xda0,        // XSAVE

            IA32_EFER               = 0xc0000080,
            IA32_STAR               = 0xc0000081,
            IA32_LSTAR              = 0xc0000082,
            IA32_CSTAR              = 0xc0000083,
            IA32_SFMASK             = 0xc0000084,
            IA32_FS_BASE            = 0xc0000100,
            IA32_GS_BASE            = 0xc0000101,
            IA32_KERNEL_GS_BASE     = 0xc0000102,
            IA32_TSC_AUX            = 0xc0000103,

            AMD_IPMR                = 0xc0010055,
            AMD_PSTATE_LIMIT        = 0xc0010061,
            AMD_PSTATE_CTRL         = 0xc0010062,
            AMD_PSTATE_STATUS       = 0xc0010063,
            AMD_SVM_VM_CR           = 0xc0010114,
            AMD_SVM_HSAVE_PA        = 0xc0010117,
        };

        enum Feature_Control
        {
            FEATURE_LOCKED          = 1ul << 0,
            FEATURE_VMX_I_SMX       = 1ul << 1,
            FEATURE_VMX_O_SMX       = 1ul << 2
        };

        template <typename T>
        ALWAYS_INLINE
        static inline T read (Register msr)
        {
            mword h, l;
            asm volatile ("rdmsr" : "=a" (l), "=d" (h) : "c" (msr));
            return static_cast<T>(static_cast<uint64>(h) << 32 | l);
        }

        template <typename T>
        ALWAYS_INLINE
        static inline void write (Register msr, T val)
        {
            asm volatile ("wrmsr" : : "a" (static_cast<mword>(val)), "d" (static_cast<mword>(static_cast<uint64>(val) >> 32)), "c" (msr));
        }

        ALWAYS_INLINE
        static inline bool guard_read (Register const &msr, uint64 &value)
        {
            uint32 high { }, low { };
            bool fault { };

            /*
             * rdmsr is skipped on GP fault and the fact is tracked in CF flag
             * - see Ec::fixup in Ec::handle_exc_gp
             */
            asm volatile ("clc;" /* clear CF flag */
                          "1: rdmsr; 2:"
                          ".section .fixup,\"a\"; .align 8;" EXPAND (WORD) " 1b,2b; .previous"
                          : "=@ccc" (fault), "=a" (low), "=d" (high)
                          : "c" (msr));

            value = (uint64(high) << 32) | uint64(low);

            return not fault;
        }

        ALWAYS_INLINE
        static inline bool guard_write (Register const &msr, uint64 const val)
        {
            bool fault { };

            /*
             * wrmsr is skipped on GP fault and the fact is tracked in CF flag
             * - see Ec::fixup in Ec::handle_exc_gp
             */
            asm volatile ("clc;" /* clear CF flag */
                          "1: wrmsr; 2:"
                          ".section .fixup,\"a\"; .align 8;" EXPAND (WORD) " 1b,2b; .previous"
                          : "=@ccc"(fault)
                          : "a" (mword(val)), "d" (mword(val >> 32)), "c" (msr));

            return not fault;
        }

        static Kobject * msr_cap;

        static void user_access(Utcb &);
};
