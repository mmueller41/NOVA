/*
 * Execution Context
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
 * Copyright (C) 2012-2020 Alexander Boettcher, Genode Labs GmbH.
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

#include "counter.hpp"
#include "fpu.hpp"
#include "mtd.hpp"
#include "pd.hpp"
#include "pmc.hpp"
#include "queue.hpp"
#include "regs.hpp"
#include "sc.hpp"
#include "timeout_hypercall.hpp"
#include "tss.hpp"
#include "si.hpp"
#include "cmdline.hpp"
#include "stdio.hpp"

class Utcb;
class Sm;
class Pt;
class Sys_ec_ctrl;
class Pmc;
class Cell;
class Ec : public Kobject, public Refcount, public Queue<Sc>
{
    friend class Queue<Ec>;
    friend class Sc;
    friend class Pt;
    friend class Pmc;
    friend class Cell;

private:
    void (*cont)() ALIGNED(16);
    Cpu_regs regs{};
    Ec *rcap{nullptr};
    Utcb *utcb{nullptr};
    Refptr<Pd> pd;
    Ec *partner;
    Ec *prev;
    Ec *next;
    Fpu *fpu;

    mword sp{0};
    union
    {
        struct
        {
            uint16 cpu;
            uint16 glb;
        };
        uint32 xcpu;
        };
        unsigned const evt;
        Timeout_hypercall timeout;
        mword          user_utcb;

        Sm *         xcpu_sm;
        Pt *         pt_oom;

        uint64      tsc  { 0 };
        uint64      time { 0 };
        uint64      time_m { 0 };

        static uint64 killed_time[NUM_CPU];

        static Sm * auth_suspend;

        REGPARM (1)
        static void handle_exc (Exc_regs *) asm ("exc_handler");

        NORETURN
        static void handle_vmx() asm ("vmx_handler");

        NORETURN
        static void handle_svm() asm ("svm_handler");

        NORETURN
        static void handle_tss() asm ("tss_handler");

        static void handle_exc_nm();
        static bool handle_exc_ts (Exc_regs *);
        static bool handle_exc_gp (Exc_regs *);
        static bool handle_exc_pf (Exc_regs *);

        static inline uint8 ifetch (mword);

        NORETURN
        static inline void svm_exception (mword);

        NORETURN
        static inline void svm_cr(mword);

        NORETURN
        static inline void svm_invlpg();

        NORETURN
        static inline void vmx_exception();

        NORETURN
        static inline void vmx_extint();

        NORETURN
        static inline void vmx_invlpg();

        NORETURN
        static inline void vmx_cr();

        static bool fixup (mword &);

        NOINLINE
        static void handle_hazard (mword, void (*)());

        static void pre_free (Rcu_elem * a)
        {
            Ec * e = static_cast<Ec *>(a);

            assert(e);

            // remove mapping in page table
            if (e->user_utcb) {
                e->pd->remove_utcb(e->user_utcb);
                e->pd->Space_mem::insert (e->pd->quota, e->user_utcb, 0, 0, 0);
                e->user_utcb = 0;
            }

            // XXX If e is on another CPU and there the fpowner - this check will fail.
            // XXX For now the destruction is delayed until somebody else grabs the FPU.
            if (fpowner == e) {
                assert (Sc::current->cpu == e->cpu);

                bool zero = fpowner->del_ref();
                assert (!zero);

                fpowner      = nullptr;
                if (Cmdline::fpu_lazy) {
                    assert (!(Cpu::hazard & HZD_FPU));
                    Fpu::disable();
                    assert (!(Cpu::hazard & HZD_FPU));
                }
            }
        }

        ALWAYS_INLINE
        static inline void destroy (Ec *obj, Pd &pd) { obj->~Ec(); pd.ec_cache.free (obj, pd.quota); }

        ALWAYS_INLINE
        inline bool idle_ec() { return ec_idle == this; }

        static void free (Rcu_elem * a)
        {
            Ec * e = static_cast<Ec *>(a);

            if (e->regs.vtlb) {
                trace(0, "leaking memory - vCPU EC memory re-usage not supported");
                return;
            }

            if (e->del_ref()) {
                assert(e != Ec::current);
                Ec::destroy (e, *e->pd);
            }
        }

        ALWAYS_INLINE
        inline Sys_regs *sys_regs() { return &regs; }

        ALWAYS_INLINE
        inline Exc_regs *exc_regs() { return &regs; }

        ALWAYS_INLINE
        inline void set_partner (Ec *p)
        {
            partner = p;
            bool ok = partner->add_ref();
            assert (ok);
            partner->rcap = this;
            ok = partner->rcap->add_ref();
            assert (ok);
            Sc::ctr_link++;
        }

        ALWAYS_INLINE
        inline unsigned clr_partner()
        {
            assert (partner == current);
            if (partner->rcap) {
                bool last = partner->rcap->del_ref();
                assert (!last);
                partner->rcap = nullptr;
            }
            bool last = partner->del_ref();
            assert (!last);
            partner = nullptr;
            return Sc::ctr_link--;
        }

        ALWAYS_INLINE
        inline void redirect_to_iret()
        {
            regs.REG(sp) = regs.ARG_SP;
            regs.REG(ip) = regs.ARG_IP;
        }

        void load_fpu();
        void save_fpu();

        void transfer_fpu (Ec *);
        void flush_fpu ();

        void transfer_pmcs(Ec *);
        void save_pmcs();
        void restore_pmcs();
        void restart_pmcs();
        void stop_pmcs();

        Ec(const Ec&);
        Ec &operator = (Ec const &);

    public:
        static Ec *current CPULOCAL_HOT;
        static Ec *fpowner CPULOCAL;
        static Ec *ec_idle CPULOCAL;
        static Ec *pmc_owner CPULOCAL_HOT;

        bool is_worker{false};

        Ec (Pd *, void (*)(), unsigned);
        Ec (Pd *, mword, Pd *, void (*)(), unsigned, unsigned, mword, mword, Pt *);
        Ec (Pd *, Pd *, void (*f)(), unsigned, Ec *);
        Ec (Pd *, Pd *, void (*f)(), unsigned, Ec *, Pt *);

        ~Ec();

        ALWAYS_INLINE
        inline void add_tsc_offset (uint64 const t)
        {
            regs.add_tsc_offset (t);
        }

        ALWAYS_INLINE
        inline bool blocked() const { return next || !cont; }

        ALWAYS_INLINE
        inline void set_timeout (uint64 t, Sm *s)
        {
            if (EXPECT_FALSE (t))
                timeout.enqueue (t, s);
        }

        ALWAYS_INLINE
        inline void clr_timeout()
        {
            if (EXPECT_FALSE (timeout.active()))
                timeout.dequeue();
        }

        ALWAYS_INLINE
        inline void set_si_regs(mword sig, mword cnt)
        {
            regs.ARG_2 = sig;
            regs.ARG_3 = cnt;
        }

        ALWAYS_INLINE NORETURN
        inline void make_current()
        {
            /*extern unsigned rpc_bench_cores;
            static unsigned count[NUM_CPU];
            static unsigned long delays[NUM_CPU];
            __atomic_fetch_add(&count[Cpu::id], 1, __ATOMIC_SEQ_CST);

            unsigned long start = rdtsc();*/
            if (EXPECT_FALSE (current->del_rcu()))
                Rcu::call (current);

            if (!Cmdline::fpu_lazy) {
                if (!idle_ec()) {
                    if (!current->utcb && !this->utcb)
                        assert(!(Cpu::hazard & HZD_FPU));

                    transfer_fpu(this);
                    assert(fpowner == this);
                }

                Cpu::hazard &= ~HZD_FPU;
            }

            if (!idle_ec()) {
                    transfer_pmcs(this);
            }

            check_hazard_tsc_aux();

            uint64 const t = rdtsc();

            current->time += t - current->tsc;

            current = this;

            current->tsc = t;

            bool ok = current->add_ref();
            assert (ok);

            Tss::run.sp0 = reinterpret_cast<mword>(exc_regs() + 1);
            /*unsigned long end = rdtsc();
            delays[Cpu::id] += (end - start);

            if (__atomic_load_n(&count[Cpu::id], __ATOMIC_SEQ_CST)%1000 == 0) {
                trace(0, "{\"tas-delay\": %lu, \"lock\": \"Ec::make_current()\", \"cores\": %u},", delays[Cpu::id]/2, rpc_bench_cores);
                delays[Cpu::id] = 0;
            }*/

            pd->make_current();

            asm volatile ("mov %0," EXPAND (PREG(sp);) "jmp *%1" : : "g" (CPU_LOCAL_STCK + PAGE_SIZE), "q" (cont) : "memory"); UNREACHED;
        }

        ALWAYS_INLINE
        inline void check_hazard_tsc_aux()
        {
            if (!Cpu::feature (Cpu::FEAT_RDTSCP))
                return;

            bool const current_is_vm = (current->regs.vmcb_state || current->regs.vmcs_state);
            bool const next_is_vm    = (this->regs.vmcb_state    || this->regs.vmcs_state);

            if (!current_is_vm && !next_is_vm)
                return;

            if (current_is_vm && !next_is_vm) {
                if (current->regs.tsc_aux != Cpu::id)
                    this->regs.set_hazard (HZD_TSC_AUX);
                return;
            }

            if (!current_is_vm && next_is_vm) {
                if (Cpu::id != this->regs.tsc_aux)
                    this->regs.set_hazard (HZD_TSC_AUX);
                return;
            }

            if (current->regs.tsc_aux != this->regs.tsc_aux)
                this->regs.set_hazard (HZD_TSC_AUX);
        }

        ALWAYS_INLINE
        static inline Ec *remote (unsigned c)
        {
            return *reinterpret_cast<volatile typeof current *>(reinterpret_cast<mword>(&current) - CPU_LOCAL_DATA + HV_GLOBAL_CPUS + c * PAGE_SIZE);
        }

        NOINLINE
        void help (void (*c)())
        {
            if (EXPECT_FALSE (cont == dead))
                return;

            current->cont = c;

            /* permit re-scheduling in case of long chain or livelock loop */
            Cpu::preemption_point();
            if (EXPECT_FALSE (Cpu::hazard & HZD_SCHED))
                Sc::schedule (false);

            Counter::print<1,16> (++Counter::helping, Console_vga::COLOR_LIGHT_WHITE, SPN_HLP);

            if (EXPECT_TRUE ((++Sc::ctr_loop % 100) == 0))
                Console::print("Long helping chain");

            activate();
        }

        NOINLINE
        void block_sc()
        {
            {   Lock_guard <Spinlock> guard (lock);

                if (!blocked())
                    return;

                bool ok = Sc::current->add_ref();
                assert (ok);

                assert(Sc::current);
                enqueue(Sc::current);
            }

            Sc::schedule (true);
        }

        ALWAYS_INLINE
        inline void release (void (*c)())
        {
            if (c)
                cont = c;

            Lock_guard <Spinlock> guard (lock);

            for (Sc *s; dequeue (s = head()); ) {
                if (EXPECT_TRUE(!s->last_ref()) || s->ec->partner) {
                    s->remote_enqueue(false);
                    continue;
                }

                Rcu::call(s);
            }
        }

        ALWAYS_INLINE
        inline Cell *cell() { return pd->cell; }

        ALWAYS_INLINE
        inline unsigned cpu_id() { return cpu; }

        HOT NORETURN
        static void ret_user_sysexit();

        HOT NORETURN
        static void ret_user_iret() asm ("ret_user_iret");

        HOT
        static void chk_kern_preempt() asm ("chk_kern_preempt");

        NORETURN
        static void ret_user_vmresume();

        NORETURN
        static void ret_user_vmrun();

        NORETURN
        static void ret_xcpu_reply();

        template <void (*)()>
        NORETURN
        static void ret_xcpu_reply_oom();

        template <Sys_regs::Status S, bool T = false>

        NOINLINE NORETURN
        static void sys_finish();

        NORETURN
        void activate();

        template <void (*)()>
        NORETURN
        static void send_msg();

        HOT NORETURN
        static void recv_kern();

        HOT NORETURN
        static void recv_user();

        HOT NORETURN
        static void reply (void (*)() = nullptr, Sm * = nullptr);

        HOT NORETURN
        static void sys_call();

        HOT NORETURN
        static void sys_reply();

        NORETURN
        static void sys_create_pd();

        NORETURN
        static void sys_create_ec();

        NORETURN
        static void sys_create_sc();

        NORETURN
        static void sys_create_pt();

        NORETURN
        static void sys_create_sm();

        NORETURN
        static void sys_revoke();

        NORETURN
        static void sys_misc();

        NORETURN
        static void sys_ec_ctrl();

        NORETURN
        static void sys_sc_ctrl();

        NORETURN
        static void sys_pt_ctrl();

        NORETURN
        static void sys_sm_ctrl();

        NORETURN
        static void sys_pd_ctrl();
        
        NORETURN
        static void sys_assign_pci();

        NORETURN
        static void sys_assign_gsi();

        NORETURN
        static void sys_xcpu_call();

        NORETURN
        static void sys_yield();

        NORETURN
        static void sys_mxinit();

        NORETURN
        static void sys_alloc_cores();

        NORETURN
        static void sys_core_allocation();

        NORETURN
        static void sys_create_cell();

        NORETURN
        static void sys_cell_ctrl();

        NORETURN
        static void sys_console_ctrl();

        NORETURN
        static void sys_cpuid();

        template <void (*)()>
        NORETURN
        static void sys_xcpu_call_oom();

        NORETURN
        static void idle();

        NORETURN
        static void xcpu_return();

        template <void (*)()>
        NORETURN
        static void oom_xcpu_return();

        NORETURN
        static void root_invoke();

        template <bool>
        static void delegate();

        NORETURN
        static void dead() { die ("IPC Abort"); }

        NORETURN
        static void die (char const *, Exc_regs * = &current->regs);

        static void idl_handler();

        static void hlt_prepare();

        NORETURN
        static void hlt_handler();

        ALWAYS_INLINE
        static inline void *operator new (size_t, Pd &pd) { return pd.ec_cache.alloc(pd.quota); }

        template <void (*)()>
        NORETURN
        void oom_xcpu(Pt *, mword, mword);

        NORETURN
        void oom_delegate(Ec *, Ec *, Ec *, bool, bool);

        NORETURN
        void oom_call(Pt *, mword, mword, void (*)(), void (*)());

        NORETURN
        void oom_call_cpu(Pt *, mword, void (*)(), void (*)());

        template <void(*C)()>
        static void check(mword, bool = true);

        bool migrate(Capability &, Ec *, Sys_ec_ctrl const &);

        ALWAYS_INLINE
        void inline measured() { time_m = time; }
};
