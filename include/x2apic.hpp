#pragma once

#include "compiler.hpp"
#include "memory.hpp"
#include "msr.hpp"
#include "x86.hpp"
#include "cpu.hpp"

class X2apic
{
    private:
        inline static uint32 read_msr_direct() {
            uint32 value;
            do {
                unsigned long a__, b__;
                __asm__ __volatile__("rdmsr"
                                     : "=a"(a__), "=d"(b__)
                                     : "c"(Msr::Register::IA32_APIC_BASE));
                value = static_cast<uint32>(a__ | (b__ << 32));
            } while (0);
            return value;
        }

        inline static void write_msr_direct(uint32 value)
        {
            __asm__ __volatile__("wrmsr" ::"c"(Msr::Register::IA32_APIC_BASE), "a"(static_cast<uint32>(static_cast<uint64>(value))), "d"((static_cast<uint64>(value)) >> 32));
        }

    public:
        static bool enabled() { return (read_msr_direct() >> 10) & 0x1; }
        static bool available() {
            uint32 eax, ebx, ecx, edx;
            Cpu::cpuid(0, eax, ebx, ecx, edx);
            return (ecx >> Cpu::FEAT_X2APIC)&0x1;  
        }
        static void enable() { write_msr_direct(read_msr_direct() | (1 << 10)); }
};