/**
 * @file amd_hpc.hpp
 * @author Michael Müller (michael.mueller@uos.de)
 * @brief Performance counter interface for AMD processors
 * @version 0.1
 * @date 2022-12-13
 * 
 * @copyright Copyright (c) 2022 Michael Müller, Osnabrück University
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

#include "msr.hpp"
#include "types.hpp"
#include "stdio.hpp"
#include "ec.hpp"
#include "pmc_type.h"

class Amd_hpc
{
    public:
        typedef Pmc_type Type;

        enum Event_selector
        {
            // Core event selectors
            CORE_SEL_BASE = 0xc0010200,
            // CCX event selectors
            CCX_SEL_BASE = 0xc0010230,
        };

        enum Counter
        {
            CORE_CTR_BASE = 0xc0010201,
            CCX_CTR_BASE = 0xc0010231,
        };

        static inline void setup(Event_selector sel, mword event, mword mask, mword flags, Type type)
        {
            mword val = (flags) | (((mask)&0xFF) << 8) | (event & 0xFF);
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + sel * 2) : (CCX_SEL_BASE + sel * 2));

            Msr::write<mword>(msr, val);
        }

        static inline void start(Event_selector sel, Type type) 
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + sel * 2) : (CCX_SEL_BASE + sel * 2));
            mword old = Msr::read<mword>(msr);
            old |= (1 << 22);
            Msr::write(msr, old);
        }

        static inline void stop(Event_selector sel, Type type) 
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + sel * 2) : (CCX_SEL_BASE + sel * 2));
            mword old = Msr::read<mword>(msr);
            old &= ~(1 << 22);
            Msr::write(msr, old);
        }

        static inline void reset(Counter ctr, Type type, mword val = 0x0)
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_CTR_BASE + ctr * 2) : (CCX_CTR_BASE + ctr * 2));
            Msr::write(msr, val);
        }

        static inline mword read(Counter ctr, Type type)
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_CTR_BASE + ctr * 2) : (CCX_CTR_BASE + ctr * 2));
            return Msr::read<mword>(msr);
        }

        static inline mword read_event(Counter ctr, Type type)
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + ctr * 2) : (CCX_SEL_BASE + ctr * 2));
            return Msr::read<mword>(msr);
        }

        static inline bool running(Counter ctr, Type type)
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + ctr * 2) : (CCX_SEL_BASE + ctr * 2));
            mword evt = Msr::read<mword>(msr);
            return (((evt >> 22)&0x1) == 1);
        }

        static inline void save(Counter ctr, Type type, mword *ctr_store, mword *evt_store)
        {
            *ctr_store = read(ctr, type);
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + ctr * 2) : (CCX_SEL_BASE + ctr + 2));
            *evt_store = Msr::read<mword>(msr);
        }

        static inline void restore(Counter ctr, Type type, mword *ctr_store, mword *evt_store)
        {
            Msr::Register msr = static_cast<Msr::Register>((type == CORE) ? (CORE_SEL_BASE + ctr * 2) : (CCX_SEL_BASE + ctr + 2));
            Msr::write(msr, *evt_store);

            reset(ctr, type, *ctr_store);
        }
};
