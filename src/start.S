/*
 * Startup Code
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012 Udo Steinberg, Intel Corporation.
 *
 * Copyright (C) 2017-2022 Alexander Boettcher, Genode Labs GmbH
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

#include "arch.hpp"
#include "memory.hpp"
#include "selectors.hpp"

/*
 * Initialization Code
 */
.section .init

.globl                  __start_bsp

/*
 * Multiboot v1 Header
 */
__boot_header_mbi1:     .long   0x1badb002
                        .long   0x0
                        .long  -0x1badb002
/*
 * Multiboot v2 Header
 */
                        .long   0x0            /* align to 8 byte for mbi2 */
__boot_header_mbi2_s:   .long   0xe85250d6
                        .long   0x0
                        .long   (__boot_header_mbi2_e - __boot_header_mbi2_s)
                        .long  -(0xe85250d6 + (__boot_header_mbi2_e - __boot_header_mbi2_s))
                        /* end tag */
                        .word   0x0 /* type */
                        .word   0x0 /* flags */
                        .long   0x8 /* size */
__boot_header_mbi2_e:

/*
 * Global Descriptor Table (GDT)
 */
.align                  8, 0x90
__boot_gdt:             .word   0
                        .word   __boot_gdt__ - __boot_gdt - 1
                        .long   __boot_gdt
#ifdef __x86_64__
                        .quad   0x00a09b0000000000
                        .quad   0x00a0930000000000
                        .quad   0x00a0fb0000000000
                        .quad   0x00a0f30000000000
#else
                        .quad   0x00cf9b000000ffff
                        .quad   0x00cf93000000ffff
                        .quad   0x00cffb000000ffff
                        .quad   0x00cff3000000ffff
#endif
__boot_gdt__:

.macro                  INIT_STATE
                        mov     $SEL_KERN_DATA, %ecx
                        mov     %ecx, %ss
                        mov     $SEL_USER_DATA, %ecx
                        mov     %ecx, %ds
                        mov     %ecx, %es
                        mov     %ecx, %fs
                        mov     %ecx, %gs
                        mov     $STACK, %esp
.endm

.macro                  INIT_PAGING
#ifdef __x86_64__
                        mov     %eax, %edi /* backup - used by multiboot */
                        mov     $0xc0000080, %ecx
                        rdmsr
                        or      $0x901, %eax
                        wrmsr
                        mov     $0x6b8, %edx
                        mov     %edi, %eax /* restore - used by multiboot */
#else
                        mov     $0x698, %edx
#endif
                        mov     %edx, %cr4
                        mov     $PDBR, %edx
                        mov     %edx, %cr3
                        mov     $0x8001002b, %edx
                        mov     %edx, %cr0
.endm

/*
 * AP Startup Code
 */
.code16

__start_ap:             INIT_PAGING
                        lgdtl   %cs:__gdt_desc - __start_ap
                        ljmpl   $SEL_KERN_CODE, $__start_ap_high

__gdt_desc:             .word   __boot_gdt__ - __boot_gdt - 1
                        .long   __boot_gdt

/*
 * BSP Startup Code
 */
.code32

__start_bsp:            mov     $(__start_bsp - __start_ap), %ecx
                        mov     $__start_ap, %esi
                        mov     $AP_BOOT_PADDR, %edi
                        rep     movsb
#ifdef __x86_64__
                        mov     $(LVL3L + 0x27), %ecx
                        mov     %ecx, LVL4  + SIZE * (LOAD_ADDR >> (3 * PTE_BPL + PAGE_BITS) & ((1 << PTE_BPL) - 1))
                        mov     $(LVL2L + 0x27), %ecx
                        mov     %ecx, LVL3L + SIZE * (LOAD_ADDR >> (2 * PTE_BPL + PAGE_BITS) & ((1 << PTE_BPL) - 1))

                        mov     $(LVL3H + 0x27), %ecx
                        mov     %ecx, LVL4  + SIZE * (LINK_ADDR >> (3 * PTE_BPL + PAGE_BITS) & ((1 << PTE_BPL) - 1))
                        mov     $(LVL2H + 0x27), %ecx
                        mov     %ecx, LVL3H + SIZE * (LINK_ADDR >> (2 * PTE_BPL + PAGE_BITS) & ((1 << PTE_BPL) - 1))
#endif
                        mov     $0xe3, %ecx
                        mov     $LVL2L, %edi
1:                      mov     %ecx, (%edi)
                        add     $(1 << (PTE_BPL + PAGE_BITS)), %ecx
                        add     $SIZE, %edi
                        cmp     $LOAD_E, %ecx
                        jb      1b

                        mov     $(LINK_P + 0x1e3), %ecx
                        mov     $(LVL2H + SIZE * (LINK_ADDR >> (PTE_BPL + PAGE_BITS) & ((1 << PTE_BPL) - 1))), %edi
1:                      mov     %ecx, (%edi)
                        add     $(1 << (PTE_BPL + PAGE_BITS)), %ecx
                        add     $SIZE, %edi
                        cmp     $LINK_E, %ecx
                        jb      1b

                        INIT_PAGING
                        lgdt    __boot_gdt + 2
                        ljmp    $SEL_KERN_CODE, $_start_bsp_high

/*
 * Common Startup Code
 */
#ifdef __x86_64__
.code64
#endif

/* 32/64 bit entry for boot CPU - BSP */
_start_bsp_high:        INIT_STATE
                        /* REG(ax) contain MBI1/2 magic value  */
                        /* REG(bx) contain MBI1/2 data pointer */
                        /* move ax/bx according to REGPARM(2) calling convention */
#ifdef __x86_64__
                        mov     %REG(ax), %REG(di)
                        mov     %REG(bx), %REG(si)
#else
                        mov     %REG(bx), %edx
#endif
                        call    init
                        jmp     __start_all


/* 32/64 bit AP code entry and entry on resume for *all* CPUs */
__start_ap_high:        INIT_STATE
                        mov     $0, %REG(bx) /* make explicit to avoid confusion */
                        jmp 2f
1:                      pause
                        /* swap bx (==0) with boot_lock (0 or 1) */
2:                      xchg    %REG(bx), boot_lock
                        test    %REG(bx), %REG(bx)
                        /* if !bx then the $STACK can be used with kern_ptab_setup */
                        je      1b

__start_all:
3:                      call    kern_ptab_setup
                        jmp     4f

.text

4:                      mov     %REG(ax), %cr3
                        /* switch to stack backed physically different per CPU */
                        mov     $(CPU_LOCAL_STCK + PAGE_SIZE), %REG(sp)
                        jmp     bootstrap
