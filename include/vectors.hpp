/*
 * Interrupt Vectors
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
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

#include "config.hpp"

#define VEC_GSI         (NUM_EXC)
#define VEC_LVT         (VEC_GSI + NUM_GSI)
#define VEC_MSI         (VEC_LVT + NUM_LVT)
#define VEC_IPI         (VEC_MSI + NUM_MSI)
#define VEC_MAX         (VEC_IPI + NUM_IPI)

#define VEC_LVT_TIMER   (VEC_LVT + 0)
#define VEC_LVT_ERROR   (VEC_LVT + 3)
#define VEC_LVT_PERFM   (VEC_LVT + 4)
#define VEC_LVT_THERM   (VEC_LVT + 5)

#define VEC_MSI_DMAR    (VEC_MSI + 0)

#define VEC_IPI_RRQ     (VEC_IPI + 0)
#define VEC_IPI_RKE     (VEC_IPI + 1)
#define VEC_IPI_IDL     (VEC_IPI + 2)
#define VEC_IPI_HLT     (VEC_IPI + 3)

#if (VEC_IPI_HLT - VEC_IPI + 1 != NUM_IPI)
#error "IPI misonfiguration"
#endif
