/*
 * Secure Virtual Machine (SVM)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012 Udo Steinberg, Intel Corporation.
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

#include "queue.hpp"
#include "utcb.hpp"
#include "slab.hpp"

class Quota;

class Vmcb
{
    public:
        union {
            char pad[1024];
            struct {
                uint32      intercept_cr;           // 0x0
                uint32      intercept_dr;           // 0x4
                uint32      intercept_exc;          // 0x8
                uint32      intercept_cpu[2];       // 0xc
                uint32      reserved1[11];          // 0x14
                uint64      base_io;                // 0x40
                uint64      base_msr;               // 0x48
                uint64      tsc_offset;             // 0x50
                uint32      asid;                   // 0x58
                uint32      tlb_control;            // 0x5c
                uint64      int_control;            // 0x60
                uint64      int_shadow;             // 0x68
                uint64      exitcode;               // 0x70
                uint64      exitinfo1;              // 0x78
                uint64      exitinfo2;              // 0x80
                uint64      exitintinfo;            // 0x88
                uint64      npt_control;            // 0x90
                uint32      reserved2[4];           // 0x98
                uint64      inj_control;            // 0xa8
                uint64      npt_cr3;                // 0xb0
                uint64      lbr;                    // 0xb8
            };
        };

union {
    char pad2[PAGE_SIZE - 1024];
    struct {
        Utcb_segment        es, cs, ss, ds, fs, gs, gdtr, ldtr, idtr, tr;
        char                reserved3[48];
        uint64              efer;
        char                reserved4[112];
        uint64              cr4, cr3, cr0, dr7, dr6, rflags, rip;
        char                reserved5[88];
        uint64              rsp;
        char                reserved6[24];
        uint64              rax, star, lstar, cstar, sfmask, kernel_gs_base;
        uint64              sysenter_cs, sysenter_esp, sysenter_eip, cr2, nrip;
        char                reserved7[24];
        uint64              g_pat;
     };
};

        static Paddr        root        CPULOCAL;
        static unsigned     asid_ctr    CPULOCAL;
        static uint32       svm_version CPULOCAL;
        static uint32       svm_feature CPULOCAL;

        static mword const fix_cr0_set = 0;
        static mword const fix_cr0_clr = 0;
        static mword const fix_cr4_set = 0;
        static mword const fix_cr4_clr = 0;

        enum Ctrl0
        {
            CPU_INTR        = 1ul << 0,
            CPU_NMI         = 1ul << 1,
            CPU_INIT        = 1ul << 3,
            CPU_VINTR       = 1ul << 4,
            CPU_INVD        = 1ul << 22,
            CPU_HLT         = 1ul << 24,
            CPU_INVLPG      = 1ul << 25,
            CPU_IO          = 1ul << 27,
            CPU_MSR         = 1ul << 28,
            CPU_SHUTDOWN    = 1ul << 31,
        };

        enum Ctrl1
        {
            CPU_VMLOAD      = 1ul << 2,
            CPU_VMSAVE      = 1ul << 3,
            CPU_CLGI        = 1ul << 5,
            CPU_SKINIT      = 1ul << 6,
        };

        static mword const force_ctrl0 =    CPU_INTR    |
                                            CPU_NMI     |
                                            CPU_INIT    |
                                            CPU_INVD    |
                                            CPU_HLT     |
                                            CPU_IO      |
                                            CPU_MSR     |
                                            CPU_SHUTDOWN;

        static mword const force_ctrl1 =    CPU_VMLOAD  |
                                            CPU_VMSAVE  |
                                            CPU_CLGI    |
                                            CPU_SKINIT;

        ALWAYS_INLINE
        static inline void *operator new (size_t, Quota &quota)
        {
            return Buddy::allocator.alloc (0, quota, Buddy::FILL_0);
        }

        static void destroy(Vmcb &, Quota &);

        Vmcb (Quota &quota, mword, mword, unsigned);

        ALWAYS_INLINE
        inline Vmcb(unsigned const id)
        {
            asid = id;
            flush();
        }

        ALWAYS_INLINE
        inline void flush()
        {
            asm volatile ("vmsave" : : "a" (Buddy::ptr_to_phys (this)) : "memory");
        }

        ALWAYS_INLINE
        inline void adjust_rip (mword len)
        {
            rip += len;

            if (int_shadow)
                int_shadow = 0;
        }

        static bool has_npt() { return Vmcb::svm_feature & 1; }
        static bool has_urg() { return true; }

        static void init();
};

class Vmcb_state
{
    friend class Queue<Vmcb_state>;

    private:

        static Slab_cache        cache;
        static Queue<Vmcb_state> queue CPULOCAL;

        Vmcb_state * prev { };
        Vmcb_state * next { };

        uint16 const cpu;

        Vmcb_state              (Vmcb_state const &);
        Vmcb_state & operator = (Vmcb_state const &);

        bool queued() { return prev || next; }

    public:

        Vmcb & vmcb;

        ALWAYS_INLINE
        static inline void *operator new (size_t, Quota &quota) { return cache.alloc(quota); }

        Vmcb_state(Vmcb &v, uint16 cpuid) : cpu(cpuid), vmcb(v) { }

        ~Vmcb_state()
        {
            if (queued())
                trace (0, "%s not de-queued", __func__);
        }

        inline void make_current()
        {
            if (Cpu::id == cpu && !queued())
                queue.enqueue(this);
        }

        inline void clear()
        {
            if (Cpu::id == cpu && queued())
                queue.dequeue(this);
        }

        static void flush_all_vmcb()
        {
            queue.for_each([](auto &vmcb_state) { vmcb_state.vmcb.flush(); });
        }

        static void destroy(Vmcb_state * const remove, Quota &quota)
        {
            if (!remove)
                return;

            Vmcb::destroy(remove->vmcb, quota);

            remove->~Vmcb_state();
            cache.free (remove, quota);
        }
};
