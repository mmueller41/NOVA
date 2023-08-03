/**
 * @file pmc.hpp
 * @author Michael Müller (michael.mueller@uos.de)
 * @brief Performance Monitoring Counters (PMC) (architecture-independent part)
 * @version 0.1
 * @date 2023-03-24
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include "pd.hpp"
#include "slab.hpp"
#include "cpu.hpp"
#include "amd_hpc.hpp"
#include "intel_hpc.hpp"
#include "list.hpp"
#include "pmc_type.h"

class Pmc : public List<Pmc>
{
    public:
        typedef Pmc_type Type;

    private:
        mword _pmc_event;
        mword _pmc_counter;
        Pmc::Type _type;
        unsigned char _id;
        bool _active;

    public:
        ALWAYS_INLINE
        inline void save() {
            if (Cpu::vendor == Cpu::AMD)
                Amd_hpc::save(static_cast<Amd_hpc::Counter>(_id), _type, &_pmc_counter, &_pmc_event);
        }

        ALWAYS_INLINE
        inline void restore() {
            if (Cpu::vendor == Cpu::AMD)
                Amd_hpc::restore(static_cast<Amd_hpc::Counter>(_id), _type, &_pmc_counter, &_pmc_event);
        }

        ALWAYS_INLINE
        inline unsigned char id() { return _id; }

        ALWAYS_INLINE
        inline void start() {
            if (Cpu::vendor == Cpu::AMD) {
                Amd_hpc::start(static_cast<Amd_hpc::Event_selector>(_id), _type);
                _active = true;
            }
        }

        ALWAYS_INLINE
        inline void stop(bool by_user) {
            if (Cpu::vendor == Cpu::AMD) {
                Amd_hpc::stop(static_cast<Amd_hpc::Event_selector>(_id), _type);
                _active = !by_user;
            }
        }

        ALWAYS_INLINE
        inline void reset(mword val = 0x0) {
            if (Cpu::vendor == Cpu::AMD)
                Amd_hpc::reset(static_cast<Amd_hpc::Counter>(_id), _type, val);
        }

        ALWAYS_INLINE
        inline mword read()
        {
            if (Cpu::vendor == Cpu::AMD)
                return Amd_hpc::read(static_cast<Amd_hpc::Counter>(_id), _type);
            else
                return 0;
        }

        ALWAYS_INLINE
        inline mword read_event()
        {
            if (Cpu::vendor == Cpu::AMD)
                return Amd_hpc::read_event(static_cast<Amd_hpc::Counter>(_id), _type);
            else
                return 0;
        }

        ALWAYS_INLINE
        inline bool active() { return _active; }

        ALWAYS_INLINE
        inline bool running() { 
            if (Cpu::vendor == Cpu::AMD)
                return Amd_hpc::running(static_cast<Amd_hpc::Counter>(_id), _type);
            return false;
        }

        ALWAYS_INLINE
        inline mword counter() { return _pmc_counter; }

        ALWAYS_INLINE
        inline mword event() { return _pmc_event;  }

        ALWAYS_INLINE
        inline Type type() { return _type; }

        ALWAYS_INLINE
        inline Pmc *next_pmc() { return next; }

        ALWAYS_INLINE
        static inline void *operator new(size_t, Pd &pd) { return pd.pmc_cache.alloc(pd.quota); }

        ALWAYS_INLINE
        static inline void destroy(Pmc *obj, Pd &pd) { obj->~Pmc();
            pd.pmc_cache.free(obj, pd.quota);
        }

        ALWAYS_INLINE
        static inline Pmc* find(Pd &pd, unsigned int id, unsigned short cpu, Type type)
        {
            for (Pmc *pmc = pd.pmcs[cpu]; pmc; pmc = pmc->next) {
                if (pmc->_id == id && pmc->_type == type)
                    return pmc;
            }
            return nullptr;
        }

        Pmc(Pd &pd, unsigned char id, unsigned cpu, Type type, mword event, mword mask, mword flags) : List<Pmc> (pd.pmcs[cpu]), _pmc_event(0), _pmc_counter(0), _type(type), _id(id), _active(false)
        {
            if (Cpu::vendor == Cpu::AMD)
                Amd_hpc::setup(static_cast<Amd_hpc::Event_selector>(_id), event, mask, flags, _type);
            pd.pmc_user = true;
        }

        ~Pmc()
        {
            if (Cpu::vendor == Cpu::AMD) {
                Amd_hpc::stop(static_cast<Amd_hpc::Event_selector>(_id), _type);
            }
        }
};