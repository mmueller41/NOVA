/*
 * Event Counters
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

#include "counter.hpp"
#include "stdio.hpp"
#include "x86.hpp"
#include "cmdline.hpp"

unsigned    Counter::ipi[NUM_IPI];
unsigned    Counter::lvt[NUM_LVT];
unsigned    Counter::gsi[NUM_GSI];
unsigned    Counter::exc[NUM_EXC];
unsigned    Counter::vmi[NUM_VMI];
unsigned    Counter::vtlb_gpf;
unsigned    Counter::vtlb_hpf;
unsigned    Counter::vtlb_fill;
unsigned    Counter::vtlb_flush;
unsigned    Counter::schedule;
unsigned    Counter::helping;
unsigned    Counter::ec_fpu;
unsigned    Counter::ecs;
unsigned    Counter::pds;
uint64      Counter::fpu_nm;
uint64      Counter::switch_ec;
uint64      Counter::cycles_idle;

void Counter::dump()
{
    trace (0, "TIME: (L) %16llu", rdtsc());
    trace (0, "IDLE: (L) %16llu", Counter::cycles_idle);
    trace (0, "VGPF: (L) %16u", Counter::vtlb_gpf);
    trace (0, "VHPF: (L) %16u", Counter::vtlb_hpf);
    trace (0, "VFIL: (L) %16u", Counter::vtlb_fill);
    trace (0, "VFLU: (L) %16u", Counter::vtlb_flush);
    trace (0, "SCHD: (L) %16u", Counter::schedule);
    trace (0, "HELP: (L) %16u", Counter::helping);
    trace (0, "ECSW: (G) %16llu", Counter::switch_ec);
    trace (0, "FPSW: (G) %16llu %s", Counter::fpu_nm, Cmdline::fpu_eager ? "eager" : "lazy");
    trace (0, "ECs : (G) %16u", Counter::ecs);
    trace (0, "PDs : (G) %16u", Counter::pds + 2); /* +2 due to kern PD and root PD */
    trace (0, "ECFP: (G) %16u", Counter::ec_fpu);

    Counter::vtlb_gpf = Counter::vtlb_hpf = Counter::vtlb_fill = Counter::vtlb_flush = Counter::schedule = Counter::helping = 0;
    Counter::switch_ec = Counter::fpu_nm = 0;

    for (unsigned i = 0; i < sizeof (Counter::ipi) / sizeof (*Counter::ipi); i++)
        if (Counter::ipi[i]) {
            trace (0, "IPI %#4x: %12u", i, Counter::ipi[i]);
            Counter::ipi[i] = 0;
        }

    for (unsigned i = 0; i < sizeof (Counter::lvt) / sizeof (*Counter::lvt); i++)
        if (Counter::lvt[i]) {
            trace (0, "LVT %#4x: %12u", i, Counter::lvt[i]);
            Counter::lvt[i] = 0;
        }

    for (unsigned i = 0; i < sizeof (Counter::gsi) / sizeof (*Counter::gsi); i++)
        if (Counter::gsi[i]) {
            trace (0, "GSI %#4x: %12u", i, Counter::gsi[i]);
            Counter::gsi[i] = 0;
        }

    for (unsigned i = 0; i < sizeof (Counter::exc) / sizeof (*Counter::exc); i++)
        if (Counter::exc[i]) {
            trace (0, "EXC %#4x: %12u", i, Counter::exc[i]);
            Counter::exc[i] = 0;
        }

    for (unsigned i = 0; i < sizeof (Counter::vmi) / sizeof (*Counter::vmi); i++)
        if (Counter::vmi[i]) {
            trace (0, "VMI %#4x: %12u", i, Counter::vmi[i]);
            Counter::vmi[i] = 0;
        }
}
