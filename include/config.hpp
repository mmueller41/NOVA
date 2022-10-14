/*
 * Configuration
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
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

#define CFG_VER         8

#define NUM_CPU         64
#define NUM_IRQ         16
#define NUM_EXC         32
#define NUM_VMI         256
#define NUM_GSI         192
#define NUM_LVT         6
#define NUM_MSI         1
#define NUM_IPI         3

#define SPN_SCH         0
#define SPN_HLP         1
#define SPN_RCU         2
#define SPN_VFI         4
#define SPN_VFL         5
#define SPN_LVT         7
#define SPN_IPI         (SPN_LVT + NUM_LVT)
#define SPN_GSI         (SPN_IPI + NUM_IPI)

#define VM_EXIT_RECALL   (NUM_VMI - 1)
#define VM_EXIT_STARTUP  (NUM_VMI - 2)
#define VM_EXIT_INVSTATE (NUM_VMI - 3)
#define VM_EXIT_NPT      (NUM_VMI - 4)
#define VM_EXIT_NOSUPP   (NUM_VMI - 5)
