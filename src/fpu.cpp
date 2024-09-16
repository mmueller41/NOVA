/*
 * Floating Point Unit (FPU)
 * Streaming SIMD Extensions (SSE)
 * Advanced Vector Extensions (AVX)
 *
 * Copyright (C) 2019 Julian Stecklina, Cyberus Technology GmbH.
 * Copyright (C) 2019-2024 Udo Steinberg, BedRock Systems, Inc.
 * Copyright (C) 2024 Alexander Boettcher
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

#include "fpu.hpp"

Fpu::State_xsv Fpu::hst_xsv;

ALIGNED(Fpu::alignment) static Fpu empty;

void Fpu::init()
{
    empty.load();
}

void Fpu::save()
{
#ifdef __x86_64__
    if (Cpu::feature (Cpu::FEAT_XSAVE)) {
        if (compact)
            asm volatile ("xsaves64 %0" : "=m" (*this)
                                        :  "d" (unsigned(managed >> 32)),
                                           "a" (unsigned(managed))
                                        : "memory");
        else
        if (Cpu::feature (Cpu::FEAT_XSAVEOPT))
            asm volatile ("xsaveopt %0" : "=m" (*this)
                                        :  "d" (unsigned(managed >> 32)),
                                           "a" (unsigned(managed))
                                        : "memory");
        else
            asm volatile ("xsave64 %0" : "=m" (*this)
                                       :  "d" (unsigned(managed >> 32)),
                                          "a" (unsigned(managed))
                                       : "memory");
    }
    else
#endif
        asm volatile ("fxsave %0" : "=m" (*this));
}

void Fpu::load()
{
#ifdef __x86_64__
    if (Cpu::feature (Cpu::FEAT_XSAVE))
        if (compact)
            asm volatile ("xrstors64  %0" : : "m" (*this),
                                              "d" (unsigned(managed >> 32)),
                                              "a" (unsigned(managed))
                                          : "memory");
        else
            asm volatile ("xrstor64  %0" : : "m" (*this),
                                             "d" (unsigned(managed >> 32)),
                                             "a" (unsigned(managed))
                                         : "memory");
    else
#endif
        asm volatile ("fxrstor %0" : : "m" (*this));
}

void Fpu::probe()
{
    if (Cpu::bsp)
        empty = Fpu();

    if (!Cpu::feature (Cpu::FEAT_XSAVE))
        return;

    // Enable supervisor state components in IA32_XSS
    if (compact)
        Msr::write (Msr::IA32_XSS, hst_xsv.xss);

    // Enable user state components in XCR0
    set_xcr (0, hst_xsv.xcr);

    unsigned size = 0, dummy = 0; 
    Cpu::cpuid (0xd, compact, dummy, size, dummy, dummy);

    /* Use largest context size reported by any CPU */
    Fpu::size = max (Fpu::size, static_cast<size_t>(size));

    if (Fpu::size > sizeof(Fpu)) {
        trace(0, "FPU: size %zu too large -> use legacy X87 FPU", Fpu::size);
        Cpu::defeature (Cpu::FEAT_XSAVE);
        Fpu::size = 512;
    }
}
