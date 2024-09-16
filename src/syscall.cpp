/*
 * System-Call Interface
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
 * Copyright (C) 2012-2023 Alexander Boettcher, Genode Labs GmbH
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

#include "iommu_intel.hpp"
#include "gsi.hpp"
#include "hip.hpp"
#include "hpet.hpp"
#include "lapic.hpp"
#include "pci.hpp"
#include "pt.hpp"
#include "sm.hpp"
#include "stdio.hpp"
#include "syscall.hpp"
#include "utcb.hpp"
#include "vectors.hpp"
#include "acpi.hpp"
#include "ioapic.hpp"
#include "amd_hpc.hpp"
#include "intel_hpc.hpp"
#include "pmc.hpp"
#include "cell.hpp"
#include "core_allocator.hpp"

unsigned rpc_bench_cores = 0;
unsigned long enqueue_delays[64];

template <Sys_regs::Status S, bool T>
void Ec::sys_finish()
{
    if (T)
        current->clr_timeout();

    current->regs.set_status (S);

    if (current->xcpu_sm)
        xcpu_return();

    if (Pd::current->quota.hit_limit() && S != Sys_regs::QUO_OOM)
        trace (TRACE_OOM, "warning: insufficient resources %lu/%lu", Pd::current->quota.usage(), Pd::current->quota.limit());

    ret_user_sysexit();
}

void Ec::activate()
{
    Ec *ec = this;

    // XXX: Make the loop preemptible
    for (Sc::ctr_link = 0; ec->partner; ec = ec->partner)
        Sc::ctr_link++;

    if (EXPECT_FALSE (ec->blocked()))
        ec->block_sc();

    ec->make_current();
}

template <bool C>
void Ec::delegate()
{
    Ec *ec = current->rcap;
    assert (ec);

    Ec *src = C ? ec : current;
    Ec *dst = C ? current : ec;

    bool user = C || ((dst->cont == ret_user_sysexit) || (dst->cont == xcpu_return));

    dst->pd->xfer_items (src->pd,
                         user ? dst->utcb->xlt : Crd (0),
                         user ? dst->utcb->del : Crd (Crd::MEM, (dst->cont == ret_user_iret ? dst->regs.cr2 : dst->regs.nst_fault) >> PAGE_BITS),
                         src->utcb->xfer(),
                         user ? dst->utcb->xfer() : nullptr,
                         src->utcb->ti());

    if (Cpu::hazard & HZD_OOM) {
        if (dst->pd->quota.hit_limit())
            trace (TRACE_OOM, "warning: insufficient resources %lx/%lx", dst->pd->quota.usage(), dst->pd->quota.limit());

        Cpu::hazard &= ~HZD_OOM;
        current->oom_delegate(dst, ec, src, user, C);
    }
}

template <void (*C)()>
void Ec::send_msg()
{
    Exc_regs *r = &current->regs;

    Kobject *obj = Space_obj::lookup (current->evt + r->dst_portal).obj();
    if (EXPECT_FALSE (obj->type() != Kobject::PT)) {
        trace(TRACE_ERROR, "Portal %lu not found", (current->evt + r->dst_portal));
        die("PT not found");
    }

        Pt *pt = static_cast<Pt *>(obj);
        Ec *ec = pt->ec;

        if (EXPECT_FALSE(current->cpu != ec->xcpu))
            die("PT wrong CPU");

        if (EXPECT_TRUE(!ec->cont))
        {
            current->cont = C;
            current->set_partner(ec);
            current->regs.mtd = pt->mtd.val;
            ec->cont = recv_kern;
            ec->regs.set_pt(pt->id);
            ec->regs.set_ip(pt->ip);
            ec->make_current();
    }

    ec->help (send_msg<C>);

    die ("IPC Timeout");
}

void Ec::sys_call()
{
    /*extern unsigned rpc_bench_cores;
    static unsigned count[NUM_CPU];
    static unsigned long delays[NUM_CPU];
    __atomic_fetch_add(&count[Cpu::id], 1, __ATOMIC_SEQ_CST);
    
    unsigned long start = rdtsc();*/
    Sys_call *s = static_cast<Sys_call *>(current->sys_regs());

    Kobject *obj = Space_obj::lookup (s->pt()).obj();
    if (EXPECT_FALSE (obj->type() != Kobject::PT))
        sys_finish<Sys_regs::BAD_CAP>();

    Pt *pt = static_cast<Pt *>(obj);
    Ec *ec = pt->ec;

    if (Pd::current->quota.hit_limit()) {

        if (!current->pt_oom)
            sys_finish<Sys_regs::QUO_OOM>();

        if (current->xcpu_sm) {
            current->regs.set_status (Sys_regs::QUO_OOM, false);
            xcpu_return();
        }

        current->oom_call_cpu (current->pt_oom, current->pt_oom->id, sys_call, sys_call);
        sys_finish<Sys_regs::QUO_OOM>();
    }
    /*unsigned long end = rdtsc();
    delays[Cpu::id] += (end - start);

    if (__atomic_load_n(&count[Cpu::id], __ATOMIC_SEQ_CST)%1000 == 0) {
        trace(0, "{\"tas-delay\": %lu, \"lock\": \"Ec::syscall()\", \"cores\": %u},", delays[Cpu::id]/2, rpc_bench_cores);
        delays[Cpu::id] = 0;
    }*/

    if (EXPECT_FALSE (current->cpu != ec->xcpu))
        Ec::sys_xcpu_call();

    if (EXPECT_TRUE (!ec->cont)) {
        current->cont = current->xcpu_sm ? xcpu_return : ret_user_sysexit;
        current->set_partner (ec);
        ec->cont = recv_user;
        ec->regs.set_pt (pt->id);
        ec->regs.set_ip (pt->ip);
        ec->make_current();
    }

    if (EXPECT_TRUE (!(s->flags() & Sys_call::DISABLE_BLOCKING)))
        ec->help (sys_call);

    sys_finish<Sys_regs::COM_TIM>();
}

void Ec::recv_kern()
{
    Ec *ec = current->rcap;

    bool fpu = false;

    if (ec->cont == ret_user_iret)
        fpu = current->utcb->load_exc (&ec->regs);
    else if (ec->cont == ret_user_vmresume)
        fpu = current->utcb->load_vmx (&ec->regs);
    else if (ec->cont == ret_user_vmrun)
        fpu = current->utcb->load_svm (&ec->regs);

    if (EXPECT_FALSE (fpu)) {
        current->utcb->fpu_mr([&](void *data){ ec->export_fpu_data(data); });
    }
    ec->transfer_pmcs(current);

    ret_user_sysexit();
}

void Ec::recv_user()
{
    Ec *ec = current->rcap;

    ec->utcb->save (current->utcb);

    if (EXPECT_FALSE (ec->utcb->tcnt()))
        delegate<true>();

    ret_user_sysexit();
}

void Ec::reply (void (*c)(), Sm * sm)
{
    current->cont = c;

    if (EXPECT_FALSE (current->glb))
        Sc::schedule (true);

    Ec *ec = current->rcap;

    if (EXPECT_FALSE (!ec))
        Sc::current->ec->activate();

    bool clr = ec->clr_partner();

    if (Sc::current->ec == ec && (Sc::current->disable || Sc::current->last_ref()))
        Sc::schedule (true);

    if (sm)
        sm->dn (false, 0, ec, clr);

    if (!clr)
        Sc::current->ec->activate();

    ec->make_current();
}

bool Ec::migrate(Capability &cap_e, Ec *ec_r, Sys_ec_ctrl const &r)
{
    if (EXPECT_FALSE (!Hip::cpu_online (r.cpu())))
        return false;

    if (EXPECT_FALSE (cap_e.obj()->type() != Kobject::EC))
        return false;

    Ec * const ec_m = static_cast<Ec *>(cap_e.obj());

    if (ec_m->pd->quota.hit_limit(4)) {
        Cpu::hazard |= HZD_OOM;
        return false;
    }

    if (EXPECT_FALSE((r.crd().type() != Crd::OBJ) || r.crd().order() != 0))
        return false;

    if (EXPECT_FALSE(ec_m->xcpu_sm || !ec_m->utcb || ec_m != ec_r))
        return false;

    mword const pt_sel = r.ec() + 1;
    mword const sc_sel = r.ec() + 2;

    Capability cap_pt = Space_obj::lookup (pt_sel);
    if (EXPECT_FALSE (cap_pt.obj()->type() != Kobject::PT))
        return false;

    Capability cap_sc = Space_obj::lookup (sc_sel);
    if (EXPECT_FALSE (cap_sc.obj()->type() != Kobject::SC))
        return false;

    Pt * const pt = static_cast<Pt *>(cap_pt.obj());
    Sc * const sc = static_cast<Sc *>(cap_sc.obj());

    if ((pt->ec->cpu != r.cpu()) || (sc->ec != ec_m))
        return false;

    Ec *new_ec = new (*ec_m->pd) Ec (Pd::current, ec_m->pd, ec_m->cont, r.cpu(), ec_m, pt);
    Sc *new_sc = new (*new_ec->pd) Sc (Pd::current, new_ec, *sc);

    Pd::current->revoke<Space_obj>(r.ec(), 0, 0x1f, true, false);
    if (!Space_obj::insert_root (Pd::current->quota, new_ec)) {
        trace (TRACE_ERROR, "migrated EC not added to Space_obj");
        delete new_sc;
        Rcu::call(new_ec); /* due to fpu, utcb */
        return false;
    }

    Pd::current->revoke<Space_obj>(sc_sel, 0, 0x1f, true, false);
    if (!Space_obj::insert_root (Pd::current->quota, new_sc)) {
        trace (TRACE_ERROR, "migrated SC not added to Space_obj");
        Pd::current->revoke<Space_obj>(r.ec(), 0, 0x1f, true, false);
        delete new_sc;
        Rcu::call(new_ec); /* due to fpu, utcb */
        return false;
    }

    Crd const crd = r.crd();
    Crd dst_crd { Crd::OBJ, new_ec->evt + crd.base(), crd.order(), crd.attr() };
    Crd src_crd { Crd::OBJ, r.ec(), crd.order(), crd.attr() };

    new_ec->pd->del_crd (Pd::current, dst_crd, src_crd);

    if (Cpu::hazard & HZD_OOM)
        trace (0, "Delegation of migrated EC cap failed");

    new_sc->remote_enqueue();

    return true;
}

void Ec::sys_reply()
{
    Ec *ec = current->rcap;
    Sm *sm = nullptr;

    if (EXPECT_TRUE (ec)) {

        enum { SYSCALL_REPLY = 1 };

        Sys_reply *r = static_cast<Sys_reply *>(current->sys_regs());

        if (EXPECT_FALSE (current->cont == sys_reply && current->regs.status() != SYSCALL_REPLY)) {
            sm = reinterpret_cast<Sm *>(r->sm_kern());
            current->regs.set_pt(SYSCALL_REPLY);
        } else {
            if (EXPECT_FALSE (r->sm())) {
                Capability cap = Space_obj::lookup (r->sm());
                if (EXPECT_TRUE (cap.obj()->type() == Kobject::SM && (cap.prm() & 2)))
                    sm = static_cast<Sm *>(cap.obj());
            }
        }

        if (EXPECT_FALSE (sm)) {
            if (ec->cont == ret_user_sysexit)
                ec->cont = sys_call;
            else if (ec->cont == xcpu_return)
                ec->regs.set_status (Sys_regs::BAD_HYP, false);
            else if (ec->cont == sys_reply) {
                assert (ec->regs.status() == SYSCALL_REPLY);
                ec->regs.set_pt(reinterpret_cast<mword>(sm));
                assert (ec->regs.status() != SYSCALL_REPLY);
                reply();
            }
        }

        Utcb *src = current->utcb;

        if (EXPECT_FALSE (src->tcnt()))
            delegate<false>();

        bool fpu = false;

        assert (current->cont != ret_xcpu_reply);

        if (EXPECT_TRUE ((ec->cont == ret_user_sysexit) || ec->cont == xcpu_return))
            src->save (ec->utcb);
        else if (ec->cont == ret_user_iret)
            fpu = src->save_exc (&ec->regs);
        else if (ec->cont == ret_user_vmresume)
            fpu = src->save_vmx (&ec->regs);
        else if (ec->cont == ret_user_vmrun)
            fpu = src->save_svm (&ec->regs);

        if (EXPECT_FALSE (fpu)) {
            src->fpu_mr([&](void *data){ ec->import_fpu_data(data); });
        }

        current->transfer_pmcs(ec);
    }

    reply(nullptr, sm);
}

template <void(*C)()>
void Ec::check(mword r, bool call)
{
    if (Pd::current->quota.hit_limit(r)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu (%lu)", __func__, __LINE__, Pd::current->quota.usage(), Pd::current->quota.limit(), r);

        if (Ec::current->pt_oom && call)
            Ec::current->oom_call_cpu (Ec::current->pt_oom, Ec::current->pt_oom->id, C, C);

        sys_finish<Sys_regs::QUO_OOM>();
    }
}

void Ec::sys_create_pd()
{
    check<sys_create_pd>(0, false);

    Sys_create_pd *r = static_cast<Sys_create_pd *>(current->sys_regs());

    trace (TRACE_SYSCALL, "EC:%p SYS_CREATE PD:%#lx", current, r->sel());

    Capability cap = Space_obj::lookup (r->pd());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD) || !(cap.prm() & 1UL << Kobject::PD)) {
        trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, r->pd());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd * pd_src = static_cast<Pd *>(cap.obj());

    if (r->limit_lower() > r->limit_upper())
        sys_finish<Sys_regs::BAD_PAR>();

    if (pd_src->quota.hit_limit(1)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu", __func__, __LINE__, pd_src->quota.usage(), pd_src->quota.limit());
        sys_finish<Sys_regs::QUO_OOM>();
    }

    Pd *pd = new (Pd::current->quota) Pd (Pd::current, r->sel(), cap.prm());

    if (!pd->quota.set_limit(r->limit_lower(), r->limit_upper(), pd_src->quota)) {
        trace (0, "Insufficient kernel memory for creating new PD");
        delete pd;
        sys_finish<Sys_regs::BAD_PAR>();
    }

    if (!Space_obj::insert_root (pd->quota, pd)) {
        trace (TRACE_ERROR, "%s: Non-NULL CAP (%#lx)", __func__, r->sel());
        delete pd;
        sys_finish<Sys_regs::BAD_CAP>();
    }

    if (Cpu::hazard & HZD_OOM) {
        Cpu::hazard &= ~HZD_OOM;
        delete pd;
        sys_finish<Sys_regs::QUO_OOM>();
    }

    Crd crd = r->crd();
    pd->del_crd (Pd::current, Crd (Crd::OBJ), crd);

    if (Cpu::hazard & HZD_OOM) {
        Cpu::hazard &= ~HZD_OOM;
        delete pd;
        sys_finish<Sys_regs::QUO_OOM>();
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_create_ec()
{
    check<sys_create_ec>(0, false);

    Sys_create_ec *r = static_cast<Sys_create_ec *>(current->sys_regs());

    trace (TRACE_SYSCALL, "EC:%p SYS_CREATE EC:%#lx CPU:%#x UTCB:%#lx ESP:%#lx EVT:%#x", current, r->sel(), r->cpu(), r->utcb(), r->esp(), r->evt());

    if (EXPECT_FALSE (!Hip::cpu_online (r->cpu()))) {
        trace (TRACE_ERROR, "%s: Invalid CPU (%#x)", __func__, r->cpu());
        sys_finish<Sys_regs::BAD_CPU>();
    }

    if (EXPECT_FALSE (!r->utcb() && !(Hip::feature() & (Hip::FEAT_VMX | Hip::FEAT_SVM)))) {
        trace (TRACE_ERROR, "%s: VCPUs not supported", __func__);
        sys_finish<Sys_regs::BAD_FTR>();
    }

    Capability cap_pd = Space_obj::lookup (r->pd());
    if (EXPECT_FALSE (cap_pd.obj()->type() != Kobject::PD) || !(cap_pd.prm() & 1UL << Kobject::EC)) {
        trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, r->pd());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *pd = static_cast<Pd *>(cap_pd.obj());

    if (pd->quota.hit_limit(7)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu", __func__, __LINE__, pd->quota.usage(), pd->quota.limit());
        sys_finish<Sys_regs::QUO_OOM>();
    }

    if (EXPECT_FALSE (r->utcb() >= USER_ADDR || r->utcb() & PAGE_MASK || !pd->insert_utcb (pd->quota, pd->mdb_cache, r->utcb()))) {
        trace (TRACE_ERROR, "%s: Invalid UTCB address (%#lx)", __func__, r->utcb());
        sys_finish<Sys_regs::BAD_PAR>();
    }

    Capability cap_pt = Space_obj::lookup (r->sel() + 1);
    Pt *pt = cap_pt.obj()->type() == Kobject::PT ? static_cast<Pt *>(cap_pt.obj()) : nullptr;

    Ec *ec = new (*pd) Ec (Pd::current, r->sel(), pd, r->flags() & 1 ? static_cast<void (*)()>(send_msg<ret_user_iret>) : nullptr, r->cpu(), r->evt(), r->utcb(), r->esp(), pt);
    
    if (pd->worker_channels && pd->cell->_workers[r->cpu()]) {
        //core_alloc.reserve(pd->cell, r->cpu());
        trace(TRACE_ERROR, "%s: A worker is already registered for %p at CPU %u", __func__, pd->cell, r->cpu());
        Ec::destroy(ec, *ec->pd);
        sys_finish<Sys_regs::BAD_CPU>();
    }

    if (pd->worker_channels && !pd->cell->_workers[r->cpu()]) {
        pd->cell->_workers[r->cpu()] = ec;
        ec->is_worker = true;
        Sm *sm = new (*pd) Sm(Pd::current, 0, 0);
        if (!sm) {
            trace(TRACE_ERROR, "%s: Unable to create worker for %p at CPU %u", __func__, pd->cell, r->cpu());
            Ec::destroy(ec, *ec->pd);
            sys_finish<Sys_regs::QUO_OOM>();
        }
        pd->cell->_worker_sms[r->cpu()] = sm;
    }

    if (!Space_obj::insert_root (pd->quota, ec)) {
        trace (TRACE_ERROR, "%s: Non-NULL CAP (%#lx)", __func__, r->sel());
        Ec::destroy (ec, *ec->pd);
        sys_finish<Sys_regs::BAD_CAP>();
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_create_sc()
{
    check<sys_create_sc>(0, false);

    Sys_create_sc *r = static_cast<Sys_create_sc *>(current->sys_regs());

    trace (TRACE_SYSCALL, "EC:%p SYS_CREATE SC:%#lx EC:%#lx P:%#x Q:%#x", current, r->sel(), r->ec(), r->qpd().prio(), r->qpd().quantum());

    Capability cap = Space_obj::lookup (r->pd());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD) || !(cap.prm() & 1UL << Kobject::SC)) {
        trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, r->pd());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *pd = static_cast<Pd *>(cap.obj());

    if (pd->quota.hit_limit(2)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu", __func__, __LINE__, pd->quota.usage(), pd->quota.limit());
        sys_finish<Sys_regs::QUO_OOM>();
    }

    Capability cap_sc = Space_obj::lookup (r->ec());
    if (EXPECT_FALSE (cap_sc.obj()->type() != Kobject::EC) || !(cap_sc.prm() & 1UL << Kobject::SC)) {
        trace (TRACE_ERROR, "%s: Non-EC CAP (%#lx)", __func__, r->ec());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Ec *ec = static_cast<Ec *>(cap_sc.obj());

    if (EXPECT_FALSE (!ec->glb)) {
        trace (TRACE_ERROR, "%s: Cannot bind SC", __func__);
        sys_finish<Sys_regs::BAD_CAP>();
    }

    if (EXPECT_FALSE (!r->qpd().prio() || !r->qpd().quantum() || (r->qpd().prio() >= Sc::priorities))) {
        trace (TRACE_ERROR, "%s: Invalid QPD", __func__);
        sys_finish<Sys_regs::BAD_PAR>();
    }

    /* All worker SCs of all cells shall have the same priority, because
       the core allocator assumes that a worker thread cannot starve due to having a lower priority than another on the same core. */
    unsigned int prio = (ec->pd->cell) ? 64 : r->qpd().prio();

    Sc *sc = new (*ec->pd) Sc (Pd::current, r->sel(), ec, ec->cpu, prio, r->qpd().quantum());

    if (ec->pd->cell && ec->pd->worker_channels) {
        if (ec->pd->cell->_worker_scs[ec->cpu] != nullptr) {
            trace(TRACE_ERROR, "%s: A worker SC has already been created for CPU %d", __func__, ec->cpu);
            delete sc;
            sys_finish<Sys_regs::BAD_CPU>();
        }
        ec->pd->cell->_worker_scs[ec->cpu] = sc;
    }

    if (!Space_obj::insert_root(pd->quota, sc))
    {
        trace(TRACE_ERROR, "%s: Non-NULL CAP (%#lx)", __func__, r->sel());
        delete sc;
        sys_finish<Sys_regs::BAD_CAP>();
    }

    sc->remote_enqueue();

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_create_pt()
{
    check<sys_create_pt>(0, false);

    Sys_create_pt *r = static_cast<Sys_create_pt *>(current->sys_regs());

    trace (TRACE_SYSCALL, "EC:%p SYS_CREATE PT:%#lx EC:%#lx EIP:%#lx", current, r->sel(), r->ec(), r->eip());

    if (EXPECT_FALSE (r->eip() >= USER_ADDR)) {
        trace (TRACE_ERROR, "%s: Invalid instruction pointer (%#lx)", __func__, r->eip());
        sys_finish<Sys_regs::BAD_PAR>();
    }

    Capability cap = Space_obj::lookup (r->pd());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD) || !(cap.prm() & 1UL << Kobject::PT)) {
        trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, r->pd());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *pd = static_cast<Pd *>(cap.obj());

    if (pd->quota.hit_limit(2)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu", __func__, __LINE__, pd->quota.usage(), pd->quota.limit());
        sys_finish<Sys_regs::QUO_OOM>();
    }

    Capability cap_ec = Space_obj::lookup (r->ec());
    if (EXPECT_FALSE (cap_ec.obj()->type() != Kobject::EC) || !(cap_ec.prm() & 1UL << Kobject::PT)) {
        trace (TRACE_ERROR, "%s: Non-EC CAP (%#lx)", __func__, r->ec());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Ec *ec = static_cast<Ec *>(cap_ec.obj());

    if (EXPECT_FALSE (ec->glb)) {
        trace (TRACE_ERROR, "%s: Cannot bind PT", __func__);
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Pt *pt = new (*ec->pd) Pt (Pd::current, r->sel(), ec, r->mtd(), r->eip());
    if (!pt) {
        trace(TRACE_ERROR, "%s: Failed to alloc PT", __func__);
        sys_finish<Sys_regs::BAD_CAP>();
    }
    if (!Space_obj::insert_root (pd->quota, pt)) {
        trace (TRACE_ERROR, "%s: Non-NULL CAP (%#lx): node_order=%lu", __func__, r->sel(), pt->node_order);
        Pt::destroy (pt);
        sys_finish<Sys_regs::BAD_CAP>();
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_create_sm()
{
    check<sys_create_sm>(0, false);

    Sys_create_sm *r = static_cast<Sys_create_sm *>(current->sys_regs());

    trace (TRACE_SYSCALL, "EC:%p SYS_CREATE SM:%#lx CNT:%lu", current, r->sel(), r->cnt());

    Capability cap = Space_obj::lookup (r->pd());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD) || !(cap.prm() & 1UL << Kobject::SM)) {
        trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, r->pd());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *pd = static_cast<Pd *>(cap.obj());

    if (pd->quota.hit_limit(1)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu", __func__, __LINE__, pd->quota.usage(), pd->quota.limit());
        sys_finish<Sys_regs::QUO_OOM>();
    }

    Sm * sm;

    if (r->sm()) {
        /* check for valid SM to be chained with */
        Capability cap_si = Space_obj::lookup (r->sm());
        if (EXPECT_FALSE (cap_si.obj()->type() != Kobject::SM)) {
            trace (TRACE_ERROR, "%s: Non-SM CAP (%#lx)", __func__, r->sm());
            sys_finish<Sys_regs::BAD_CAP>();
        }

        Sm * si = static_cast<Sm *>(cap_si.obj());
        if (si->is_signal()) {
            /* limit chaining to solely one level */
            trace (TRACE_ERROR, "%s: SM CAP (%#lx) is signal", __func__, r->sm());
            sys_finish<Sys_regs::BAD_CAP>();
        }

        sm = new (*Pd::current) Sm (Pd::current, r->sel(), 0, si, r->cnt());
    } else
        sm = new (*Pd::current) Sm (Pd::current, r->sel(), r->cnt());

    if (!Space_obj::insert_root (pd->quota, sm)) {
        trace (TRACE_ERROR, "%s: Non-NULL CAP (%#lx)", __func__, r->sel());
        Sm::destroy(sm, *pd);
        sys_finish<Sys_regs::BAD_CAP>();
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_revoke()
{
    Sys_revoke *r = static_cast<Sys_revoke *>(current->sys_regs());

    trace (TRACE_SYSCALL, "EC:%p SYS_REVOKE", current);

    Pd * pd = Pd::current;

    if (current->cont != sys_revoke) {
        if (r->remote()) {
            Capability cap = Space_obj::lookup (r->pd());
            if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD)) {
                trace (TRACE_ERROR, "%s: Bad PD CAP (%#lx)", __func__, r->pd());
                sys_finish<Sys_regs::BAD_CAP>();
            }
            pd = static_cast<Pd *>(cap.obj());
            if (!pd->add_ref())
                sys_finish<Sys_regs::BAD_CAP>();
        }
        current->cont = sys_revoke;

        r->rem(pd);
    } else
        pd = reinterpret_cast<Pd *>(r->pd());

    pd->rev_crd (r->crd(), r->self(), true, r->keep());

    current->cont = sys_finish<Sys_regs::SUCCESS>;
    r->rem(nullptr);

    if (r->remote() && pd->del_rcu())
        Rcu::call(pd);

    if (EXPECT_FALSE (r->sm())) {
        Capability cap_sm = Space_obj::lookup (r->sm());
        if (EXPECT_FALSE (cap_sm.obj()->type() == Kobject::SM && (cap_sm.prm() & 1))) {
            Sm *sm = static_cast<Sm *>(cap_sm.obj());
            sm->add_to_rcu();
        }
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_misc()
{
    check<sys_misc>(2);

    Sys_misc *s = static_cast<Sys_misc *>(current->sys_regs());

    switch (s->flags()) {
    case Sys_misc::SYS_ACPI_SUSPEND: {

        Capability cap = Space_obj::lookup (s->pd_snd());
        if (!Ec::auth_suspend || cap.obj() != Ec::auth_suspend)
            sys_finish<Sys_regs::BAD_CAP>();

        current->cont = sys_finish<Sys_regs::SUCCESS>;

        Ioapic::for_each([](auto ioapic) {
            if (!ioapic->suspend(Pd::root.quota))
                sys_finish<Sys_regs::BAD_PAR>();
        });

        if (!Acpi::suspend(uint8(s->sleep_type_a()), uint8(s->sleep_type_b())))
            sys_finish<Sys_regs::BAD_PAR>();

        /* never reached */
        sys_finish<Sys_regs::BAD_PAR>();
    }
    case Sys_misc::SYS_DELEGATE: {
        trace (TRACE_SYSCALL, "EC:%p SYS_DELEGATE PD:%lx->%lx T:%d B:%#lx", current, s->pd_snd(), s->pd_dst(), s->crd().type(), s->crd().base());

        Kobject *obj_dst = Space_obj::lookup (s->pd_dst()).obj();
        if (EXPECT_FALSE (obj_dst->type() != Kobject::PD)) {
            trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, s->pd_dst());
            sys_finish<Sys_regs::BAD_CAP>();
        }
        Kobject *obj_snd = Space_obj::lookup (s->pd_snd()).obj();
        if (EXPECT_FALSE (obj_snd->type() != Kobject::PD)) {
            trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, s->pd_dst());
            sys_finish<Sys_regs::BAD_CAP>();
        }

        Pd * pd_dst = static_cast<Pd *>(obj_dst);
        Pd * pd_snd = static_cast<Pd *>(obj_snd);

        pd_dst->xfer_items (pd_snd,
                            Crd (0),
                            s->crd(),
                            current->utcb->xfer(),
                            nullptr,
                            current->utcb->ti());

        if (Cpu::hazard & HZD_OOM) {
           Cpu::hazard &= ~HZD_OOM;
           sys_finish<Sys_regs::QUO_OOM>();
        }

        sys_finish<Sys_regs::SUCCESS>();
    }
    case Sys_misc::SYS_LOOKUP: {
        trace (TRACE_SYSCALL, "EC:%p SYS_LOOKUP T:%d B:%#lx", current, s->crd().type(), s->crd().base());

        Space *space; Mdb *mdb;
        if ((space = Pd::current->subspace (s->crd().type())) && (mdb = space->tree_lookup (s->crd().base())))
            s->crd() = Crd (s->crd().type(), mdb->node_base, mdb->node_order, mdb->node_attr);
        else
            s->crd() = Crd (0);

        sys_finish<Sys_regs::SUCCESS>();
    }
    default:
        sys_finish<Sys_regs::BAD_PAR>();
    }
}

void Ec::sys_ec_ctrl()
{
    check<sys_ec_ctrl>(1);

    Sys_ec_ctrl *r = static_cast<Sys_ec_ctrl *>(current->sys_regs());

    switch (r->op()) {
        case 0:
        {
            Capability cap = Space_obj::lookup (r->ec());
            if (EXPECT_FALSE (cap.obj()->type() != Kobject::EC || !(cap.prm() & 1UL << 0))) {
               trace (TRACE_ERROR, "%s: Bad EC CAP (%#lx)", __func__, r->ec());
               sys_finish<Sys_regs::BAD_CAP>();
            }

            Ec *ec = static_cast<Ec *>(cap.obj());

            if (!(ec->regs.hazard() & HZD_RECALL)) {

                ec->regs.set_hazard (HZD_RECALL);

                if (Cpu::id != ec->cpu && Ec::remote (ec->cpu) == ec) {
                    Lapic::send_ipi (ec->cpu, VEC_IPI_RKE);
                    if (r->state())
                        sys_finish<Sys_regs::COM_TIM>();
                }
            }

            if (!(r->state() && !current->vcpu()))
                break;

            Cpu_regs regs(ec->regs);

            regs.mtd = Mtd::GPR_ACDB |
                       Mtd::GPR_BSD |
#ifdef __x86_64__
                       Mtd::GPR_R8_R15 |
#endif
                       Mtd::RSP |
                       Mtd::RIP_LEN |
                       Mtd::RFLAGS |
                       Mtd::QUAL;

            if (((ec->cont != ret_user_iret) && (ec->cont != recv_kern))) {
                /* in syscall */
                regs.REG(ip) = ec->regs.ARG_IP;
                regs.REG(sp) = ec->regs.ARG_SP;
            }

            /*
             * Find out if the EC is in exception handling state, which is the
             * case if it has called an exception handler portal. The exception
             * numbers in the comparison are the ones handled as exception in
             * 'entry.S'. Page fault exceptions are not of interest for GDB,
             * which is currently the only user of this status information.
             */
            if ((ec->cont == ret_user_iret) &&
                (ec->partner != nullptr) && (ec->partner->cont == recv_kern) &&
                ((regs.dst_portal <= 0x01) ||
                 ((regs.dst_portal >= 0x03) && (regs.dst_portal <= 0x07)) ||
                 ((regs.dst_portal >= 0x0a) && (regs.dst_portal <= 0x0d)) ||
                 ((regs.dst_portal >= 0x10) && (regs.dst_portal <= 0x13)))) {
                /* 'regs.err' will be transferred into utcb->qual[0] */
                regs.err = 1;
            } else
                regs.err = 0;

            bool fpu = current->utcb->load_exc (&regs);
            /* we don't really reload state of different threads - ignore */
            (void)fpu;
            break;
        }

        case 1: /* yield */
            current->cont = sys_finish<Sys_regs::SUCCESS>;
            Sc::schedule (false, false);
            break;

        case 2: /* helping */
        {
            Kobject *obj = Space_obj::lookup (r->ec()).obj();

            if (EXPECT_FALSE (obj->type() != Kobject::EC))
                sys_finish<Sys_regs::BAD_CAP>();

            Ec *ec = static_cast<Ec *>(obj);

            if (EXPECT_FALSE(ec->cpu != current->cpu))
                sys_finish<Sys_regs::BAD_CPU>();

            if (EXPECT_FALSE(ec->vcpu() || ec->blocked() || ec->partner || ec->pd != Ec::current->pd || !ec->utcb || (r->cnt() != ec->utcb->tls)))
                sys_finish<Sys_regs::BAD_PAR>();

            current->cont = sys_finish<Sys_regs::SUCCESS>;
            ec->make_current();

            break;
        }

        case 3: /* re-schedule */
            current->cont = sys_finish<Sys_regs::SUCCESS>;
            Sc::schedule (false, true);
            break;

        case 4: /* migrate */
        {
            if (!current->rcap)
                sys_finish<Sys_regs::BAD_PAR>();

            Capability cap = Space_obj::lookup (r->ec());

            if (!Ec::current->migrate(cap, current->rcap, *r)) {
                if (!(Cpu::hazard & HZD_OOM))
                    sys_finish<Sys_regs::BAD_PAR>();

                Cpu::hazard &= ~HZD_OOM;
                sys_finish<Sys_regs::QUO_OOM>();
            }

            break;
        }

        case 5: /* execution time */
        {
            Kobject *obj = Space_obj::lookup (r->ec()).obj();

            if (EXPECT_FALSE (obj->type() != Kobject::EC))
                sys_finish<Sys_regs::BAD_CAP>();

            Ec *ec = static_cast<Ec *>(obj);

            uint32 dummy;
            r->set_time (div64 (ec->time * 1000, Lapic::freq_tsc, &dummy));
            ec->measured();
            break;
        }

        case 6: /* hpc_setup */
        {
            current->transfer_pmcs(current);

            Sys_hpc_ctrl *hc = static_cast<Sys_hpc_ctrl *>(current->sys_regs());

            Pmc *pmc = new (*(current->pd)) Pmc(*(current->pd), static_cast<unsigned char>(hc->sel()), current->cpu, static_cast<Pmc::Type>(hc->type()), hc->event(), hc->mask(), hc->pc_flags());

            if (!pmc)
                sys_finish<Sys_regs::QUO_OOM>();

            pmc->reset();

            break;
        }

        case 7: /* hpc_start */
        {
            Sys_hpc_ctrl *hc = static_cast<Sys_hpc_ctrl *>(current->sys_regs());
            Pmc *pmc = Pmc::find(*(current->pd), static_cast<unsigned char>(hc->sel()), current->cpu, static_cast<Pmc::Type>(hc->type()));

            if (!pmc)
                sys_finish<Sys_regs::BAD_PAR>();

            pmc->start();

            break;
        }

        case 8: /* hpc_stop */
        {
            Sys_hpc_ctrl *hc = static_cast<Sys_hpc_ctrl *>(current->sys_regs());
            Pmc *pmc = Pmc::find(*(current->pd), static_cast<unsigned char>(hc->sel()), current->cpu, static_cast<Pmc::Type>(hc->type()));

            if (!pmc)
                sys_finish<Sys_regs::BAD_PAR>();

            pmc->stop(true);

            break;
        }

        case 9: /* hpc_reset */
        {
            Sys_hpc_ctrl *hc = static_cast<Sys_hpc_ctrl *>(current->sys_regs());

            Pmc *pmc = Pmc::find(*(current->pd), static_cast<unsigned char>(hc->sel()), current->cpu, static_cast<Pmc::Type>(hc->type()));
            if (!pmc)
                sys_finish<Sys_regs::BAD_PAR>();

            pmc->reset(hc->event());
            break;
        }

        case 10: /* hpc_read */
        {
            Sys_hpc_ctrl *hc = static_cast<Sys_hpc_ctrl *>(current->sys_regs());
            rpc_bench_cores = static_cast<unsigned>(hc->sel());
            
            Pmc *pmc = nullptr;
            //Pmc::find(*(current->pd), static_cast<unsigned char>(hc->sel()), current->cpu, static_cast<Pmc::Type>(hc->type()));

            if (!pmc)
            {
                if (hc->type() >= NUM_CPU) {
                    r->set_value(Cpu::id);
                }
                else
                    r->set_value(enqueue_delays[hc->type()]);
                break;
            }
                //sys_finish<Sys_regs::BAD_PAR>();

            mword val = pmc->read();
            
            r->set_time(val);
            break;
        }

        case 11: /* get vcpu state */
        {
            Capability cap = Space_obj::lookup (r->ec());
            if (EXPECT_FALSE (cap.obj()->type() != Kobject::EC || !(cap.prm() & 1UL << 0))) {
                trace (TRACE_ERROR, "%s: Bad EC CAP (%#lx)", __func__, r->ec());
                sys_finish<Sys_regs::BAD_CAP>();
            }
            Ec *ec = static_cast<Ec *>(cap.obj());

            if (EXPECT_FALSE (current->cpu != ec->cpu)) {
                trace (TRACE_ERROR, "%s: Called from remote CPU", __func__);
                sys_finish<Sys_regs::BAD_CPU>();
            }

            if (!(ec->regs.hazard() & HZD_RECALL))
                ec->regs.set_hazard (HZD_RECALL);

            Cpu_regs regs(ec->regs);
            regs.mtd = r->mtd_value();

            bool fpu = false;

            if (ec->vcpu() && (Hip::feature() & Hip::FEAT_SVM))
                fpu = current->utcb->load_svm (&regs);
            else if (ec->vcpu() && (Hip::feature() & Hip::FEAT_VMX))
                fpu = current->utcb->load_vmx (&regs);
            else {
                trace (TRACE_ERROR, "%s: Bad EC CAP (%#lx)", __func__, r->ec());
                sys_finish<Sys_regs::BAD_CAP>();
            }

            if (EXPECT_FALSE (fpu)) {
                current->utcb->fpu_mr([&](void *data){ ec->export_fpu_data(data); });
            }

            sys_finish<Sys_regs::SUCCESS>();
        }
        case 12: /* set vcpu state */
        {
            Capability cap = Space_obj::lookup (r->ec());
            if (EXPECT_FALSE (cap.obj()->type() != Kobject::EC || !(cap.prm() & 1UL << 0))) {
                trace (TRACE_ERROR, "%s: Bad EC CAP (%#lx)", __func__, r->ec());
                sys_finish<Sys_regs::BAD_CAP>();
            }

            Ec *ec = static_cast<Ec *>(cap.obj());

            if (EXPECT_FALSE (current->cpu != ec->cpu)) {
                trace (TRACE_ERROR, "%s: Called from remote CPU", __func__);
                sys_finish<Sys_regs::BAD_CPU>();
            }

            bool fpu = false;
            Utcb *src = current->utcb;

            if (ec->vcpu() && (Hip::feature() & Hip::FEAT_SVM))
                fpu = src->save_svm (&ec->regs);
            else if (ec->vcpu() && (Hip::feature() & Hip::FEAT_VMX))
                fpu = src->save_vmx (&ec->regs);
            else {
                trace (TRACE_ERROR, "%s: Bad EC CAP (%#lx)", __func__, r->ec());
                sys_finish<Sys_regs::BAD_CAP>();
            }

            if (EXPECT_FALSE (fpu)) {
                src->fpu_mr([&](void *data){ ec->import_fpu_data(data); });
            }

            if (!r->recall() && (ec->regs.hazard() & HZD_RECALL))
                ec->regs.clr_hazard(HZD_RECALL);

            ec->regs.dst_portal = VM_EXIT_RECALL;

            sys_finish<Sys_regs::SUCCESS>();
        }

        case 13: /* selective & guarded MSR access */
        {
            if (!current->utcb)
                sys_finish<Sys_regs::BAD_PAR>();

            Capability cap = Space_obj::lookup (r->ec());
            if (!Msr::msr_cap || cap.obj() != Msr::msr_cap)
                sys_finish<Sys_regs::BAD_CAP>();

            Msr::user_access(*(current->utcb));
            break;
        }

        default:
            sys_finish<Sys_regs::BAD_PAR>();
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_sc_ctrl()
{
    check<sys_sc_ctrl>(1);

    Sys_sc_ctrl *r = static_cast<Sys_sc_ctrl *>(current->sys_regs());

    Capability cap = Space_obj::lookup (r->sc());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::SC || !(cap.prm() & 1UL << 0))) {
        trace (TRACE_ERROR, "%s: Bad SC CAP (%#lx)", __func__, r->sc());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Sc *sc = static_cast<Sc *>(cap.obj());

    uint64 sc_time = sc->time;
    uint64 ec_time = 0;

    if (EXPECT_FALSE (r->op() && sc->space == static_cast<Space_obj *>(&Pd::kern))) {
        if (r->op() == 1)
            sc_time = Sc::cross_time[sc->cpu];
        else if (r->op() == 2)
            sc_time = Sc::killed_time[sc->cpu];
        else if (r->op() == 3) {
            sc_time = Sc::killed_time[sc->cpu];
            ec_time = Ec::killed_time[sc->cpu];
        }
        else
            sys_finish<Sys_regs::BAD_PAR>();
    } else
        sc->measured();

    if (r->op() == 3) { /* sc and ec time requested */
        if (!ec_time) {
            Kobject *obj = Space_obj::lookup (r->ec()).obj();

            if (EXPECT_FALSE (obj->type() == Kobject::EC)) {
                Ec *ec = static_cast<Ec *>(obj);

                ec_time = ec->time;
                ec->measured();
            }
        }

        uint32 dummy;
        r->set_time (div64 (sc_time * 1000, Lapic::freq_tsc, &dummy),
                     div64 (ec_time * 1000, Lapic::freq_tsc, &dummy));

        sys_finish<Sys_regs::SUCCESS>();
    }

    uint32 dummy;
    r->set_time (div64 (sc_time * 1000, Lapic::freq_tsc, &dummy));

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_pt_ctrl()
{
    check<sys_pt_ctrl>(1);

    Sys_pt_ctrl *r = static_cast<Sys_pt_ctrl *>(current->sys_regs());

    Capability cap = Space_obj::lookup (r->pt());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PT || !(cap.prm() & Pt::PERM_CTRL))) {
        trace (TRACE_ERROR, "%s: Bad PT CAP (%#lx)", __func__, r->pt());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Pt *pt = static_cast<Pt *>(cap.obj());

    pt->set_id (r->id());

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_sm_ctrl()
{
    check<sys_sm_ctrl>(1);

    Sys_sm_ctrl *r = static_cast<Sys_sm_ctrl *>(current->sys_regs());
    Capability cap = Space_obj::lookup (r->sm());

    if (EXPECT_FALSE (cap.obj()->type() != Kobject::SM || !(cap.prm() & 1UL << r->op()))) {
//        trace (TRACE_ERROR, "%s: Bad SM CAP (%#lx)", __func__, r->sm());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Sm *sm = static_cast<Sm *>(cap.obj());

    switch (r->op()) {

        case 0:
            sm->submit();
            break;

        case 1:
            if (sm->space == static_cast<Space_obj *>(&Pd::kern)) {
                Gsi::unmask (static_cast<unsigned>(sm->node_base - NUM_CPU));
                if (sm->is_signal())
                    break;
            }

            if (sm->is_signal())
                sys_finish<Sys_regs::BAD_CAP>();

            current->cont = Ec::sys_finish<Sys_regs::SUCCESS, true>;
            sm->dn (r->zc(), r->time());
            break;
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_pd_ctrl()
{
    check<sys_pd_ctrl>(1);

    Sys_pd_ctrl *r = static_cast<Sys_pd_ctrl *>(current->sys_regs());

    Capability cap = Space_obj::lookup (r->src());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD)) {
        trace (TRACE_ERROR, "%s: Bad src PD CAP (%#lx)", __func__, r->src());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *src = static_cast<Pd *>(cap.obj());

    if (r->dbg()) {
        r->dump(src->quota.limit(), src->quota.usage());
        sys_finish<Sys_regs::SUCCESS>();
    }

    Capability cap_pd = Space_obj::lookup (r->dst());
    if (EXPECT_FALSE (cap_pd.obj()->type() != Kobject::PD)) {
        trace (TRACE_ERROR, "%s: Bad dst PD CAP (%#lx)", __func__, r->dst());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *dst = static_cast<Pd *>(cap_pd.obj());

    if (!src->quota.transfer_to(dst->quota, r->tra())) {
        trace (TRACE_ERROR, "%s: PD %p has insufficient kernel memory quota", __func__, src);
        sys_finish<Sys_regs::BAD_PAR>();
    }

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_assign_pci()
{
    check<sys_assign_pci>(4);

    Sys_assign_pci *r = static_cast<Sys_assign_pci *>(current->sys_regs());

    Kobject *obj = Space_obj::lookup (r->pd()).obj();
    if (EXPECT_FALSE (obj->type() != Kobject::PD)) {
        trace (TRACE_ERROR, "%s: Non-PD CAP (%#lx)", __func__, r->pd());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Pd * pd = static_cast<Pd *>(obj);

    if (pd->dom_id == Space_mem::NO_DOMAIN_ID)
        sys_finish<Sys_regs::BAD_DEV>();

    if (pd->quota.hit_limit(4)) {
        trace(TRACE_OOM, "%s:%u - not enough resources %lu/%lu", __func__, __LINE__, pd->quota.usage(), pd->quota.limit());
        sys_finish<Sys_regs::QUO_OOM>();
    }

    Paddr phys; unsigned rid;
    if (EXPECT_FALSE (!pd->Space_mem::lookup (r->dev(), phys) || (rid = Pci::phys_to_rid (phys)) == ~0U || rid >= 65536U)) {
        trace (TRACE_ERROR, "%s: Non-DEV CAP (%#lx)", __func__, r->dev());
        sys_finish<Sys_regs::BAD_DEV>();
    }

    auto * const iommu = Pci::find_iommu (r->hnt());
    if (EXPECT_FALSE (!iommu)) {
        trace (TRACE_ERROR, "%s: Invalid Hint (%#lx)", __func__, r->hnt());
        sys_finish<Sys_regs::BAD_DEV>();
    }

    iommu->assign (static_cast<uint16>(rid), static_cast<Pd *>(obj));

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_assign_gsi()
{
    check<sys_assign_gsi>(2);

    Sys_assign_gsi *r = static_cast<Sys_assign_gsi *>(current->sys_regs());

    if (EXPECT_FALSE (!Hip::cpu_online (r->cpu()))) {
        trace (TRACE_ERROR, "%s: Invalid CPU (%#x)", __func__, r->cpu());
        sys_finish<Sys_regs::BAD_CPU>();
    }

    Kobject *obj = Space_obj::lookup (r->sm()).obj();
    if (EXPECT_FALSE (obj->type() != Kobject::SM)) {
        trace (TRACE_ERROR, "%s: Non-SM CAP (%#lx)", __func__, r->sm());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Sm *sm = static_cast<Sm *>(obj);

    if (EXPECT_FALSE (sm->space != static_cast<Space_obj *>(&Pd::kern))) {
        trace (TRACE_ERROR, "%s: Non-GSI SM (%#lx)", __func__, r->sm());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    if (r->si() != ~0UL) {
        Kobject *obj_si = Space_obj::lookup (r->si()).obj();
        if (EXPECT_FALSE (obj_si->type() != Kobject::SM)) {
            trace (TRACE_ERROR, "%s: Non-SI CAP (%#lx)", __func__, r->si());
            sys_finish<Sys_regs::BAD_CAP>();
        }

        Sm *si = static_cast<Sm *>(obj_si);

        if (si == sm) {
            sm->chain(nullptr);
            sys_finish<Sys_regs::SUCCESS>();
        }

        if (EXPECT_FALSE (si->space == static_cast<Space_obj *>(&Pd::kern))) {
            trace (TRACE_ERROR, "%s: Invalid-SM CAP (%#lx)", __func__, r->si());
            sys_finish<Sys_regs::BAD_CAP>();
        }

        sm->chain(si);
    }

    Paddr phys; unsigned rid = 0, gsi = static_cast<unsigned>(sm->node_base - NUM_CPU);

     /*
      * On Genode: If r->dev() != 0 (device virtual address of config space),
      * assume MSI is wanted. If there is already a GSI at requested location
      * set error and leave.
      */
    if (EXPECT_FALSE (Gsi::gsi_table[gsi].ioapic && r->dev())) {
        sys_finish<Sys_regs::BAD_DEV>();
    }

    if (EXPECT_FALSE (!Gsi::gsi_table[gsi].ioapic && (!Pd::current->Space_mem::lookup (r->dev(), phys) || ((rid = Pci::phys_to_rid (phys)) == ~0U && (rid = Hpet::phys_to_rid (phys)) == ~0U)))) {
        trace (TRACE_ERROR, "%s: Non-DEV CAP (%#lx)", __func__, r->dev());
        sys_finish<Sys_regs::BAD_DEV>();
    }

    if (r->cfg()) {
        Gsi::gsi_table[gsi].trg = r->trg();
        Gsi::gsi_table[gsi].pol = r->pol();
    }

    r->set_msi (Gsi::set (gsi, r->cpu(), rid));

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_xcpu_call()
{
    /*extern unsigned rpc_bench_cores;
    static unsigned count[NUM_CPU];
    static unsigned long delays[NUM_CPU];
    __atomic_fetch_add(&count[Cpu::id], 1, __ATOMIC_SEQ_CST);
    
    unsigned long start = rdtsc();*/
    Sys_call *s = static_cast<Sys_call *>(current->sys_regs());

    Capability cap = Space_obj::lookup (s->pt());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PT)) {
        trace (TRACE_ERROR, "%s: Bad PT CAP (%#lx)", __func__, s->pt());
        sys_finish<Sys_regs::BAD_CAP>();
    }

    Pt *pt = static_cast<Pt *>(cap.obj());
    Ec *ec = pt->ec;

    if (EXPECT_FALSE (current->cpu == ec->cpu || !(cap.prm() & Pt::PERM_XCPU))) {
        trace (TRACE_ERROR, "%s: Bad CPU", __func__);
        sys_finish<Sys_regs::BAD_CPU>();
    }

    enum { UNUSED = 0, CNT = 0 };

    if (!current->sc_xcpu) {
        current->xcpu_sm = new (*Pd::current) Sm (Pd::current, UNUSED, CNT);
        current->ec_xcpu = new (*Pd::current) Ec (Pd::current, Pd::current, Ec::sys_call, ec->cpu, current);

        if (!current->ec_xcpu->rcap) {
            trace (0, "xCPU construction failure");

            Ec::destroy(current->ec_xcpu, *Pd::current);
            Sm::destroy(current->xcpu_sm, *Pd::current);

            current->ec_xcpu = nullptr;
            current->xcpu_sm = nullptr;

            sys_finish<Sys_regs::BAD_PAR>();
        }

        current->sc_xcpu = new (*Pd::current) Sc (Pd::current, current->ec_xcpu, current->ec_xcpu->cpu, Sc::current);

        current->sc_xcpu->add_ref();

    } else {
        bool sc_unused = Lapic::pause_loop_until(1, [&] {
            return !current->sc_xcpu->last_ref(); });

        if (!sc_unused) {
            trace (0, "xCPU EC still in use");
            sys_finish<Sys_regs::COM_TIM>();
        }

        current->xcpu_sm = new (*Pd::current) Sm (Pd::current, UNUSED, CNT);
        current->ec_xcpu->xcpu_clone(*current, ec->cpu);
        current->sc_xcpu->xcpu_clone(*Sc::current, ec->cpu);

        current->sc_xcpu->add_ref();
    }

    current->cont = ret_xcpu_reply;

    current->sc_xcpu->remote_enqueue();

    current->xcpu_sm->dn (false, 0);
    

    ret_xcpu_reply();
}

void Ec::ret_xcpu_reply()
{
    if (current->xcpu_sm) {
        Rcu::call(current->xcpu_sm);
        current->xcpu_sm = nullptr;
    }

    if (current->regs.status() != Sys_regs::SUCCESS) {
        current->cont = sys_call;
        current->regs.set_status (Sys_regs::SUCCESS, false);
    } else
        current->cont = ret_user_sysexit;
    /*unsigned long end = rdtsc();
    delays[Cpu::id] += (end - start);

    if (__atomic_load_n(&count[Cpu::id], __ATOMIC_SEQ_CST)%1000 == 0) {
        trace(0, "{\"tas-delay\": %lu, \"lock\": \"Ec::ret_xcpu_reply\", \"cores\": %u},", delays[Cpu::id]/2, rpc_bench_cores);
        delays[Cpu::id] = 0;
    }*/

    current->make_current();
}

void Ec::sys_yield()
{
    Sys_yield *r = static_cast<Sys_yield *>(current->sys_regs());
    Cell *cell = current->pd->cell;
    Cell volatile *owner = core_alloc.owner(Cpu::id);
    Channel *chan = owner ? &owner->_pd->worker_channels[Cpu::id] : nullptr;

    if (!cell) {
        trace(TRACE_ERROR, "No cell found on CPU %d", Cpu::id);
        sys_finish<Sys_regs::BAD_CAP>();
    }
    
    if (!current->is_worker) {
        trace(TRACE_ERROR, "Tried to yield non-worker on CPU %d", Cpu::id);
        sys_finish<Sys_regs::BAD_CAP>();
    }

    switch (r->op()) {
        /* If the worker thread shall sleep we release the core */
        case Sys_yield::RETURN_CORE: {
            if (chan) {
                chan->delta_enter = rdtsc() - cell->_pd->worker_channels[Cpu::id].delta_enter;
                chan->delta_block = rdtsc();
            }
            /* If, however, the core shall be returned to its owner,
            we return it via the core allocator and let it activate the
            owner's corresponding worker. There is no need to release the core, as it would be allocated immediately by its owner anyway. */
            if (core_alloc.borrowed(cell, Cpu::id))
            {
                core_alloc.return_core(cell, Cpu::id);
                const_cast<Cell *>(owner)->wake_core(Cpu::id);
                    //trace(TRACE_CPU, "Cell %p returned CPU %u, cmap=%lu", cell, Cpu::id, cell->core_map);
            }
            break;
        }
        case Sys_yield::SLEEP: {
            core_alloc.yield(cell, Cpu::id);
            //trace(0, "Cell %p yielded CPU %d, flags=%d", cell, Cpu::id, r->op());
            break;
        }

        case Sys_yield::NO_BLOCK: {
            
            core_alloc.yield(cell, Cpu::id);
            //trace(0, "Cell %p yielded CPU %d without blocking worker", cell, Cpu::id);
            break;
        }
    }

    /* Put the yielding worker to sleep */
    if (r->op() != Sys_yield::NO_BLOCK) {
        current->cont = Ec::sys_finish<Sys_regs::SUCCESS, true>;
        Cpu::delta_block[Cpu::id] = rdtsc();
        cell->_worker_sms[Cpu::id]->dn(false, 0, current, true);
    }
    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_mxinit()
{
    check<sys_mxinit>(1);

    Sys_mxinit *r = static_cast<Sys_mxinit *>(current->sys_regs());

    Pd *pd = current->pd;
    trace(0, "Setting channel for cell of prio %d to %lx", r->prio(), r->flag());
    
    unsigned long channel_gva = r->flag();

    unsigned long *channel_hva = static_cast<unsigned long*>(Buddy::alloc(1, pd->quota, Buddy::FILL_0));

    pd->Space_mem::insert(pd->quota, channel_gva, 0, Hpt::HPT_U | Hpt::HPT_W | Hpt::HPT_P, Buddy::ptr_to_phys(channel_hva));

    pd->mxinit(r->entry(), channel_hva);

    trace(TRACE_CPU, "Cell has %lu channels", r->entry());

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_alloc_cores()
{
    check<sys_alloc_cores>(1);

    Sys_alloc_core *r = static_cast<Sys_alloc_core*>(current->sys_regs());
    Cell *cell = current->pd->cell;

    if (!cell)
        sys_finish<Sys_regs::BAD_CAP>();

    cell->_pd->worker_channels[Cpu::id].delta_enter = rdtsc() - cell->_pd->worker_channels[Cpu::id].delta_enter;

    unsigned long start_alloc = rdtsc();
    mword cores = core_alloc.alloc(cell, r->count());
    unsigned long end_alloc = rdtsc();
    cell->_pd->worker_channels[Cpu::id].delta_alloc = end_alloc - start_alloc;
    //trace(TRACE_CPU, "Allocated %d cores", cell->remainder);
    if (!cell->remainder)
    {
        //trace(TRACE_ERROR, "No more cores available for %p: cmap = %lx", cell, cell->core_map);
        sys_finish<Sys_regs::BAD_CPU>();
    }

    unsigned long start_activate = rdtsc();
    cell->add_cores(cores);
    unsigned long end_activate = rdtsc();
    cell->_pd->worker_channels[Cpu::id].delta_activate = end_activate - start_activate;

    cell->_pd->worker_channels[Cpu::id].delta_return = rdtsc();
    r->set_allocated(cores);
    r->set_remainder(cell->remainder);

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_reserve_core()
{
    check<sys_reserve_core>(1);

    Sys_reserve_core *r = static_cast<Sys_reserve_core *>(current->sys_regs());
    Cell *cell = current->pd->cell;

    if (!cell)
        sys_finish<Sys_regs::BAD_CAP>();

    if (!core_alloc.is_owner(cell, r->core()))
        sys_finish<Sys_regs::BAD_CPU>();

    cell->wake_core(r->core());

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_core_allocation()
{
    check<sys_core_allocation>(1);

    Sys_core_alloc *r = static_cast<Sys_core_alloc*>(current->sys_regs());
    Cell *my_cell = current->pd->cell;
    if (r->flags())
        r->set_val(my_cell->core_mask[0]);
    else
        r->set_val(__atomic_load_n(&my_cell->core_map, __ATOMIC_SEQ_CST));
    current->pd->worker_channels[Cpu::id].delta_block = Cpu::delta_block[Cpu::id];
    current->pd->worker_channels[Cpu::id].delta_return = Cpu::delta_return[Cpu::id];

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_create_cell()
{
    check<sys_create_cell>(1);

    Sys_create_cell *r = static_cast<Sys_create_cell *>(current->sys_regs());

    trace(0, "Creating new cell with mask %lx and offset %lu ", r->mask(), r->start());

    Capability cap = Space_obj::lookup(r->sel());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD)) {
        trace(TRACE_ERROR, "%s: Bad PD CAP (%#lx)", __func__, r->sel());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *pd = static_cast<Pd *>(cap.obj());
    if (!pd->cell) {
        pd->cell = new (*pd) Cell(pd, r->prio(), r->mask(), r->start());
    } else {
        pd->cell->update(r->mask(), r->start());
    }

    unsigned long first_cpu = bit_scan_forward(r->mask());

    core_alloc.set_owner(pd->cell, r->mask(), r->start() * sizeof(mword) * 8);
    core_alloc.reserve(pd->cell, first_cpu);
    trace(0, "Reserved CPU %ld for cell %p", first_cpu, pd->cell);

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_cell_ctrl()
{
    check<sys_cell_ctrl>(1);

    Sys_cell_ctrl *r = static_cast<Sys_cell_ctrl *>(current->sys_regs());

    Capability cap = Space_obj::lookup(r->sel());
    if (EXPECT_FALSE (cap.obj()->type() != Kobject::PD)) {
        trace(TRACE_ERROR, "%s: Bad PD CAP (%#lx)", __func__, r->sel());
        sys_finish<Sys_regs::BAD_CAP>();
    }
    Pd *pd = static_cast<Pd *>(cap.obj());
    Cell *cell = pd->cell;
    cell->update(r->mask(), r->index());

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_console_ctrl()
{
    check<sys_console_ctrl>(1);

    Sys_console_ctrl *r = static_cast<Sys_console_ctrl *>(current->sys_regs());


    switch (r->flags()) {
        case Sys_console_ctrl::LOCK: {
            //Console::lock_console();
            break;
        }
        case Sys_console_ctrl::UNLOCK: {
            //Console::unlock_console();
            break;
        }
    }
    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_create_habitat()
{
    check<sys_create_habitat>(1);

    Sys_create_habitat *r = static_cast<Sys_create_habitat*>(current->sys_regs());
    core_alloc.init_habitat(r->offset(), r->size());

    sys_finish<Sys_regs::SUCCESS>();
}

void Ec::sys_cpuid()
{
    check<sys_cpuid>(1);

    Sys_cpuid *r = static_cast<Sys_cpuid *>(current->sys_regs());
    r->set_val(Cpu::id);

    sys_finish<Sys_regs::SUCCESS>();
}

extern "C"
void (*const syscall[])() =
{
    &Ec::sys_call,
    &Ec::sys_reply,
    &Ec::sys_create_pd,
    &Ec::sys_create_ec,
    &Ec::sys_create_sc,
    &Ec::sys_create_pt,
    &Ec::sys_create_sm,
    &Ec::sys_revoke,
    &Ec::sys_misc,
    &Ec::sys_ec_ctrl,
    &Ec::sys_sc_ctrl,
    &Ec::sys_pt_ctrl,
    &Ec::sys_sm_ctrl,
    &Ec::sys_assign_pci,
    &Ec::sys_assign_gsi,
    &Ec::sys_pd_ctrl,
    &Ec::sys_yield,
    &Ec::sys_mxinit,
    &Ec::sys_alloc_cores,
    &Ec::sys_core_allocation,
    &Ec::sys_create_cell,
    &Ec::sys_cell_ctrl,
    &Ec::sys_console_ctrl,
    &Ec::sys_cpuid,
    &Ec::sys_reserve_core,
    &Ec::sys_create_habitat,
};

template void Ec::sys_finish<Sys_regs::COM_ABT>();
template void Ec::send_msg<Ec::ret_user_vmresume>();
template void Ec::send_msg<Ec::ret_user_vmrun>();
template void Ec::send_msg<Ec::ret_user_iret>();
template void Ec::send_msg<Ec::ret_user_sysexit>();
