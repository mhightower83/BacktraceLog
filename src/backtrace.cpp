//
// Copyright 2022 M Hightower
//
// Copyright 2019-2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
  File source: https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/source/backtrace.c

  Adapted for use with Arduino ESP8266 using the NONOS SDK on 2022.
    * Comments added!
    * Enhanced code range checking (prev_text_size)
    * Added 2nd r0 stack save location to check (xt_retaddr_callee)
    * generally made more robust.
    * Works without the need of an exception handler for IRAM/FLASH byte access

  This algorithm may not be perfect; however, it mostly functional.
  Its results are greatly improved, when the whole build uses, compiler option
  "-fno-optimize-sibling-calls". This will increase stack usage; however, it
  leaves more evidence for the backtrace to work with.
*/
#include <stdint.h>
#include <stddef.h>

#include <c_types.h>
#include <esp8266_peri.h>
#include <esp8266_undocumented.h>
#include <mmu_iram.h>

#include "backtrace.h"

#pragma GCC optimize("Os")

#ifndef BACKTRACE_MAX_RETRY
#define BACKTRACE_MAX_RETRY 3
#endif

#ifndef BACKTRACE_MAX_LOOKBACK
#define BACKTRACE_MAX_LOOKBACK 1024
#endif

#ifdef DEBUG_ESP_BACKTRACE_CPP
#define ETS_PRINTF ets_uart_printf
#else
#define ETS_PRINTF(...)
#endif

#ifndef BACKTRACE_IN_IRAM
#define BACKTRACE_IN_IRAM 0
#endif


#ifndef MMU_IRAM_SIZE
#error "Missing MMU_IRAM_SIZE"
#endif
#define CONFIG_SOC_IRAM_SIZE MMU_IRAM_SIZE
// #include "esp8266/eagle_soc.h"
// #include <eagle_soc.h>

#define FLASH_BASE                      (0x40200000)
#define ROM_BASE                        (0x40000000)
#define ROM_CODE_END                    (0x4000e328)
#define IS_ROM_CODE(a)                  ((size_t)(a) >= ROM_BASE && (size_t)(a) < ROM_CODE_END)

extern "C" {
#if BACKTRACE_IN_IRAM
IRAM_ATTR static uint32_t prev_text_size(const uint32_t pc);
IRAM_ATTR int xt_pc_is_valid(const void *pc);
IRAM_ATTR static int find_s32i_a0_a1(uint32_t pc, uint32_t off);
IRAM_ATTR static int find_addim_ax_a1(uint32_t pc, uint32_t off, int ax);
IRAM_ATTR static bool verify_path_ret_to_pc(uint32_t pc, uint32_t off);
IRAM_ATTR int xt_retaddr_callee(const void * const i_pc, const void * const i_sp, const void * const i_lr, const void **o_pc, const void **o_sp);
IRAM_ATTR int xt_retaddr_callee_ex(const void * const i_pc, const void * const i_sp, const void * const i_lr, const void **o_pc, const void **o_sp, const void **o_fn);
IRAM_ATTR struct BACKTRACE_PC_SP xt_return_address_ex(int lvl);
IRAM_ATTR const void *xt_return_address(int lvl);
IRAM_ATTR static uint8_t _idx(void *a);
#endif


// Copied from mmu_iram.h - We have a special need to read IRAM code. In a debug
// build the orginal would have validated the address range. And, panic at the
// attempt to access the IRAM code area. Orignal comments stripped.
static inline __attribute__((always_inline))
uint8_t _get_uint8(const void *p8) {
  void *v32 = (void *)((uintptr_t)p8 & ~(uintptr_t)3u);
  uint32_t val;
  __builtin_memcpy(&val, v32, sizeof(uint32_t));
  asm volatile ("" :"+r"(val)); // inject 32-bit dependency
  uint32_t pos = ((uintptr_t)p8 & 3u) * 8u;
  val >>= pos;
  return (uint8_t)val;
}


// Stay on the road. Return 0 for not a valid code pointer,
//   or how far backward we can scan.
// Changes: Tightened up code range comparisons.
// TODO: Make sure I didn't break it!
static uint32_t prev_text_size(const uint32_t pc)
{
    uint32_t size;
    extern uint32_t _text_start, _text_end, _flash_code_end;

    // This covers compiled IRAM code
    if (pc > (uint32_t)&_text_start && pc < (uint32_t)&_text_end) {
        size = pc - (uint32_t )&_text_start;

    } else if (IS_ROM_CODE(pc)) {
        size = pc - ROM_BASE;

    // This should separate flash code from flash strings and data (PROGMEM)
    // Hmm, this works for Arduino ESP8266 built stuff, what about the SDK?
    // Where are its strings and data stored in flash?
    // Assume this is good for now.
    } else if (pc > (uint32_t)FLASH_BASE && pc < (uint32_t)&_flash_code_end) {
        size = pc - FLASH_BASE;

    // Most likely not code.
    } else {
        size = 0;
    }

    return size;
}

int xt_pc_is_valid(const void *pc)
{
    return prev_text_size((uint32_t)pc) ? 1 : 0;
}


#if BACKTRACE_IN_IRAM
#define CACHE_READ_EN_BIT               BIT8    // eagle_soc.h in RTOS_SDK

#ifndef ROM_SPIRead
#define ROM_SPIRead         0x40004b1cU
#endif
typedef int (*fp_SPIRead_t)(uint32_t addr, void *dest, size_t size);
#define real_SPIRead ((fp_SPIRead_t)ROM_SPIRead)

// Use performance macros to access IRAM byte data w/o generating an exception.
static uint8_t _idx(void *c) {
    static uintptr_t addr = 0;
    static uint8_t data[4] __attribute__((aligned(4)));

    if (FLASH_BASE > (uintptr_t)c || (SPIRDY & CACHE_READ_EN_BIT)) {
        return _get_uint8(c);
    } else if ((FLASH_BASE + 1024*1024) > (uintptr_t)c) {
        // We have to directly read from flash
        if (((uintptr_t)c & ~3) != addr) {
            addr = (uintptr_t)c & ~3;
            real_SPIRead(addr - FLASH_BASE, &data[0], sizeof(data));
        }
        return data[(uintptr_t)c & 3u];
    }
    return 0;
}

#else
static inline uint8_t _idx(void *a) __attribute__((always_inline));
static inline uint8_t _idx(void *a) { return _get_uint8(a); }
// static inline uint8_t _idx(void *a) { return *(uint8_t*)(a); } // for debugging -Og makes it simple
#endif

static inline int idx(void *a, uint32_t b) __attribute__((always_inline));
static inline int sidx(void *a, uint32_t b) __attribute__((always_inline));

static inline int idx(void *a, uint32_t b) { return _idx((void*)((uintptr_t)a + b)); }
static inline int sidx(void *a, uint32_t b) { return (sint8_t)_idx((void*)((uintptr_t)a + b)); }

static int find_addim_ax_a1(uint32_t pc, uint32_t off, int ax) {
    // returns an additional adjustment, if any, to reach a0 stored value
    int a0_off = -1;  // Assume failed
    if (1 == ax) {
        // a1 needs no adjustment
        return 0;
    }
    for (uint8_t *p0 = (uint8_t *)(pc - off);
        (uintptr_t)p0 < pc;
        p0 = (idx(p0, 0) & 0x08) ? &p0[2] : &p0[3]) {
        //
        // y2 d1 xx  addmi   ay, a1, (xx * 4)
        //
        if (idx(p0, 0) == (0x02 | (ax << 4)) && idx(p0, 1) == 0xd1) {
            a0_off = sidx(p0, 2) * 256;
            // We don't expect negative values
            // let negative values implicitly fail
            break;
        }
    }
    return a0_off;
}
// #pragma GCC optimize("Og")  // This one breaks with the div 0 example


// For a definitive return value, we look for a0 save.
// The current GNU compiler appears to store at +12 for a size 16 stack;
// however, some other compiler or version will save at 0.
// (maybe it is xtensa?)
//
// If we truely found the stack add instruction, then we should be able to scan
// forward looking for a0 being saved and where.
//
// Except, if the function being evaluated never calls another function there is
// no need to save a0 on the stack. Thus, this case would fail. Hmm, however,
// when using profiler (-finstrument-functions) every function does call another
// function forcing a0 to always be saved.
//
static
int find_s32i_a0_a1(uint32_t pc, uint32_t off) {
    int a0_off = -1;  // Assume failed

    // For the xtensa instruction set, it looks like, bit 0x08 on the LSB is the
    // instruction size bit. set => two bytes / clear => 3 bytes
    //
    // Scan forward
    for (uint8_t *p0 = (uint8_t *)(pc - off);
        (uintptr_t)p0 < pc;
        p0 = (idx(p0, 0) & 0x08) ? &p0[2] : &p0[3]) {
        //
        // 02 6x zz s32i   a0, ax, n  (n = zz * 4)
        //
        if (idx(p0, 0) == 0x02 && (idx(p0, 1) & 0xF0) == 0x60) {
            int ax = idx(p0, 1) & 0x0F;
            // Check for addmi ax, a1, n
            a0_off = find_addim_ax_a1((uint32_t)p0, (uintptr_t)p0 - (pc - off), ax);
            if (a0_off >= 0) a0_off += 4 * idx(p0, 2);
            break;
        } else
        //
        // 09 zx    s32i.n a0, ax, n  (n = z * 4)
        //
        if (idx(p0, 0) == 0x09) {
            int ax = idx(p0, 1) & 0x0F;
            // Check for addmi ax, a1, n
            a0_off = find_addim_ax_a1((uint32_t)p0, (uintptr_t)p0 - (pc - off), ax);
            if (a0_off >= 0) a0_off += 4 * (idx(p0, 1) >> 4);
            break;
        }
    }
    return a0_off;
}

static
bool verify_path_ret_to_pc(uint32_t pc, uint32_t off) {
    uint8_t *p0 = (uint8_t *)(pc - off);
    for (;
         (uintptr_t)p0 < pc;
         p0 = (idx(p0, 0) & 0x08) ? &p0[2] : &p0[3]);

    return ((uintptr_t)p0 == pc);
}

// Changes/Improvements:
//  * Do not alter output if detection failed.
//  * Monitor for A0 register save instruction, to get the correct
//    return address offset.
//  * Fix MOVI / SUB combo. Now handles any register selection and the two
//    instruction do not have to be consecutive.
//
// Returns true (1) on success
// int xt_retaddr_callee(const void *i_pc, const void *i_sp, const void *i_lr, void **o_pc, void **o_sp)
int xt_retaddr_callee_ex(const void * const i_pc, const void * const i_sp, const void * const i_lr, const void **o_pc, const void **o_sp, const void **o_fn)
{
    uint32_t lr = (uint32_t)i_lr; // last return ??
    uint32_t pc = (uint32_t)i_pc;
    uint32_t sp = (uint32_t)i_sp;
    uint32_t fn = 0;
    *o_fn = (void*)fn;

    uint32_t off = 2;
    const uint32_t text_size = prev_text_size(pc);

    // Most of the time "lr" will be set to the value in register "A0" which
    // very likely will be the return address when in a leaf function.
    // Otherwise, it could be anything. Test and disqualify early maybe allowing
    // better guesses later.
    if (!xt_pc_is_valid((void *)lr)) {
        lr = 0;
    }

    // The question is how agressively should we keep looking.
    //
    // For now, keep searching BACKTRACE_MAX_RETRY are exhaused.
    //
    // A "ret.n" match represents a fail. BACKTRACE_MAX_LOOKBACK allows the
    // inner loop search to continue as long as "off" is less than
    // BACKTRACE_MAX_LOOKBACK.

    for (size_t retry = 0;
        (retry < BACKTRACE_MAX_RETRY) && (off < text_size) && pc;
        retry++, off++)
    {
        pc = (uint32_t)i_pc;
        sp = (uint32_t)i_sp;
        fn = 0;

        // Scan backward 1 byte at a time looking for a stack reserve or ret.n
        // This requires special handling to read IRAM/IROM/FLASH 1 byte at a time.
        for (; off < text_size; off++) {
            // What about 12d1xx   ADDMI a1, a1, -32768..32512 (-128..127 shifted by 8)?
            // Not likely to be useful. This is mostly used at the start of an
            // Exception frame. No need to see what an Interrupt interrupted. More
            // interesting to see what the interrupt did to cause an exception.
            // When we need to look behind an Exception frame, those start point
            // values are passed in.
            uint8_t *pb = (uint8_t *)((uintptr_t)pc - off);
            /*
              Exception "C" wrapper handler does not create a stack frame, it is
              jump to after the stack frame is setup. Thus, backward scanning
              will fail or worse give a false result. As a backstop check for
              _xtos_c_wrapper_handler.
            */
            if ((uintptr_t)pb == (uintptr_t)_xtos_c_wrapper_handler) {
                // Leave stepping over the Exception frame to the caller.
                // pc = ((uint32_t*)sp)[0];
                // sp += 256;
                pc = 0;
                fn = (uint32_t)pb;
                break;
            }
            //
            // 12 c1 xx   ADDI a1, a1, -128..127
            //
            if (idx(pb, 0) == 0x12 && idx(pb, 1) == 0xc1) {
                const int stk_size = sidx(pb, 2); //((int8_t *)pb)[2];
                //? ETS_PRINTF("\nmaybe - addi: pb 0x%08X, stk_size: %d\n", (uint32_t)pb, stk_size);

                // Skip ADDIs that are clearing previous stack usage or not a multiple of 16.
                if (stk_size >= 0 || stk_size % 16 != 0) {
                    //? ETS_PRINTF("\nstk_size: %d, (%% 16 = %d)\n", stk_size, stk_size % 16);
                    continue;
                }
                // Negative stack size, stack space creation/reservation and multiple of 16

// TODO: rework or think about using `pc = lr;` when find_s32i_a0_a1(pc, off); fails.
#if 0
                if (off <= 3) {
                    pc = lr;
                    ETS_PRINTF("\noff <= 3\n");

                } else {
                    int a0_offset = find_s32i_a0_a1(pc, off);
                    if (a0_offset < 0 || a0_offset >= -stk_size) {
                        continue;
                    } else {
                        uint32_t *sp_a0 = (uint32_t *)((uintptr_t)sp + (uintptr_t)a0_offset);
                        ETS_PRINTF("\naddi: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d, %p(0x%08x)\n", pc, sp, stk_size, a0_offset, sp_a0, *sp_a0);
                        fn = (pc - off) & ~3;
                        pc = *sp_a0;
                    }
                }
#else
                int a0_offset = find_s32i_a0_a1(pc, off);
                if (a0_offset < 0) {
                    //? ETS_PRINTF("\n!addi: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d\n", pc, sp, stk_size, a0_offset);
                    continue;
                    // pc = lr;
                } else if (a0_offset >= -stk_size) {
                    //? ETS_PRINTF("\n!addi: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d\n", pc, sp, stk_size, a0_offset);
                    continue;
                } else {
                    uint32_t *sp_a0 = (uint32_t *)((uintptr_t)sp + (uintptr_t)a0_offset);
                    ETS_PRINTF("\naddi: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d, %p(0x%08x)\n", pc, sp, stk_size, a0_offset, sp_a0, *sp_a0);
                    fn = (pc - off) & ~3; // function entry points are aligned 4
                    pc = *sp_a0;
                }
#endif
                // Get back to the caller's stack
                sp -= stk_size;

                break;
            } else
            // The orignal code here had three bugs:
            //  1. It assumed a9 would be the only register used for setting the
            //     stack size.
            //  2. It assumed the SUB instruction would be imediately after the
            //     MOVI instruction.
            //  3. stk_size calculation needed a shift 8 left for the high 4 bits.
            //     This test would never match up to any code in the Boot ROM
            //
            // Solution to use:
            // Look for MOVI a?, -2048..2047
            // On match search forward through 32 bytes, for SUB a1, a1, a?
            // I think this will at least work for the Boot ROM code
            //
            // r2 Ax yz   MOVI r, -2048..2047
            //
            if ((idx(pb, 0) & 0x0F) == 0x02 && (idx(pb, 1) & 0xF0) == 0xa0) {
                int stk_size = ((idx(pb, 1) & 0x0F)<<8) + idx(pb, 2);
                stk_size |= (0 != (stk_size & BIT(11))) ? 0xFFFFF000 : 0;

                //+ ETS_PRINTF("\nmaybe - movi: pb 0x%08X, stk_size: %d\n", (uint32_t)pb, stk_size);
                // With negative stack_size look for an add instruction
                // With a positive stack_size look for a sub instruction
                if (-2048 > stk_size || stk_size >= 2048 || 0 == stk_size || 0 != (3 & stk_size)) {
                    continue;
                }

                bool found = false;
                if (0 < stk_size) {
                    //
                    // r0 11 c0   SUB a1, a1, r
                    //
                    for (uint8_t *psub = &pb[3];
                         psub < &pb[32];            // Expect a match within 32 bytes
                         psub = (uint8_t*)((idx(psub, 0) & 0x80) ? ((uintptr_t)psub + 2) : ((uintptr_t)psub + 3))) {
                        if ((idx(psub, 0) & 0x0F) == 0x00 &&
                             idx(psub, 1) == 0x11 &&
                             idx(psub, 2) == 0xc0 &&
                            (idx(pb, 0) & 0xF0) == (idx(psub, 0) & 0xF0)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        //? ETS_PRINTF("\n!found sub\n");
                        continue;
                    }
                    int a0_offset = find_s32i_a0_a1(pc, off);
                    if (a0_offset < 0) {
                        // pc = lr;
                        //? ETS_PRINTF("\n!sub: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d\n", pc, sp, stk_size, a0_offset);
                        continue;
                    } else if (a0_offset >= stk_size) {
                        //? ETS_PRINTF("\n!sub: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d\n", pc, sp, stk_size, a0_offset);
                        continue;
                    } else {
                        // fn = pc - off;
                        // pc = *(uint32_t *)(sp + a0_offset);
                        uint32_t *sp_a0 = (uint32_t *)((uintptr_t)sp + (uintptr_t)a0_offset);
                        ETS_PRINTF("\nsub: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d, %p(0x%08x)\n", pc, sp, stk_size, a0_offset, sp_a0, *sp_a0);
                        fn = (pc - off) & ~3; // function entry points are aligned 4
                        pc = *sp_a0;
                    }

                    sp += stk_size;
                } else {
                    //
                    // 11 rA   ADD.n a1, a1, r
                    //
                    for (uint8_t *psub = &pb[3];
                         psub < &pb[32];            // Expect a match within 32 bytes
                         psub = (uint8_t*)((idx(psub, 0) & 0x80) ? ((uintptr_t)psub + 2) : ((uintptr_t)psub + 3))) {
                        if ( idx(psub, 1) == 0x11 &&
                            (idx(psub, 0) & 0x0F) == 0x0A &&
                            (idx(pb, 0) & 0xF0) == (idx(psub, 0) & 0xF0)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // Repeat with 3 byte add - untested
                        //
                        // r0 11 80   add a1, a1, r
                        //
                        for (uint8_t *psub = &pb[3];
                             psub < &pb[32];            // Expect a match within 32 bytes
                             psub = (uint8_t*)((idx(psub, 0) & 0x80) ? ((uintptr_t)psub + 2) : ((uintptr_t)psub + 3))) {
                            if ((idx(psub, 0) & 0x0F) == 0x00 &&
                                 idx(psub, 1) == 0x11 &&
                                 idx(psub, 2) == 0x80 &&
                                (idx(pb, 0) & 0xF0) == (idx(psub, 0) & 0xF0)) {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        continue;
                    }
                    int a0_offset = find_s32i_a0_a1(pc, off);
                    if (a0_offset < 0) {
                        // pc = lr;
                        //? ETS_PRINTF("\n!add: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d\n", pc, sp, stk_size, a0_offset);
                        continue;
                    } else if (a0_offset >= -stk_size) {
                        //? ETS_PRINTF("\n!add: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d\n", pc, sp, stk_size, a0_offset);
                        continue;
                    } else {
                        // fn = pc - off;
                        // pc = *(uint32_t *)(sp + a0_offset);
                        uint32_t *sp_a0 = (uint32_t *)((uintptr_t)sp + (uintptr_t)a0_offset);
                        ETS_PRINTF("\nadd: pc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d, %p(0x%08x)\n", pc, sp, stk_size, a0_offset, sp_a0, *sp_a0);
                        fn = (pc - off) & ~3; // function entry points are aligned 4
                        pc = *sp_a0;
                    }

                    sp -= stk_size;
                }
                break;
            } else
            // Most fail to find, land here. The question is how aggressively
            // should we keep looking. Limit with BACKTRACE_MAX_LOOKBACK bytes
            // back from the start "pc".
            //
            // 0d f0     RET.N
            // 80 00 00  RET          # missing in original code!
            //
            if ((idx(pb, 0) == 0x0d && idx(pb, 1) == 0xf0) ||
                (idx(pb, 0) == 0x80 && idx(pb, 1) == 0x00 && idx(pb, 2) == 0x00)) {
                ETS_PRINTF("\nRET(.N) pb: 0x%08X\n", (uint32_t)pb);

                // Make sure pc is reachable. Follow the code back to PC.
                if (!verify_path_ret_to_pc(pc, off)) {
                    continue;
                }

                // Conciderations: we bumped into what may be a ret.
                // It could be misaligned junk that looks like a ret.
                // If there are two or three zero's after the ret, that would be
                // more convicing.
                // Stratagy, check zeros following "ret".
                // Do they pad out to align 4?
                // TODO: revisit this for refinement and verification if it is needed, etc.
                // Maybe consider stopping if lr is defined or try harder when not??
                // We are more likely to have an "lr" with a leaf function

                // More organized thoughts:
                //
                // With a leaf function we will only find a ret or ret.n  Maybe
                // no stack frame setup and a0 may not be saved if it is
                // present.
                //
                // On a recusive trace if "lr" is set, it is most likely the
                // first call. We may or may not be looking at a leaf function
                // where  there is no stack frame setup. Thus, "ret"s are more
                // significant.
                //
                // When "lr" is null, we should expect a stack frame setup and
                // "ret"s are less significant.
                //
                // For a Soft WDT event, the recursion starts off with a null
                // "lr" guess.
                //

                if (off <= 8 || off > BACKTRACE_MAX_LOOKBACK) {
                    fn = 0;
                    pc = lr;
                    break;
                }

                continue;
            }
        }
        if (off >= text_size) {
            ETS_PRINTF("\n >=text_size: 0x%08X(%d) off: 0x%08X - sp: 0x%08x, pc: 0x%08x, fn: 0x%08x\n", text_size, text_size, off, sp, pc, fn);
            break;
        } else
        if (xt_pc_is_valid((void *)pc)) {
            break;
        } else {
            ETS_PRINTF("\n!valid - sp: 0x%08x, pc: 0x%08x, fn: 0x%08x\n", sp, pc, fn);
        }
    }

    //
    // Save only if successful
    //
    if (off < text_size) {
      //+ TODO these two should be moved back into the if()
        *o_sp = (void *)sp;
        *o_pc = (void *)pc;
        *o_fn = (void *)fn;
        if (xt_pc_is_valid(*o_pc)) {
            // We changed the output registers anyway. So the caller can
            // evaluate what to do next.
            return 1;
        }
        ETS_PRINTF("\n!valid2 - sp: 0x%08x, pc: 0x%08x, fn: 0x%08x\n", sp, pc, fn);
    } else {
        ETS_PRINTF("\n >=text_size2: 0x%04X(%d) - sp: 0x%08x, pc: 0x%08x, fn: 0x%08x\n", text_size, text_size, sp, pc, fn);
    }

    return 0;
}
#pragma GCC optimize("Os")

int xt_retaddr_callee(const void * const i_pc, const void * const i_sp, const void * const i_lr, const void **o_pc, const void **o_sp)
{
    const void *o_fn; // ignored
    return xt_retaddr_callee_ex(i_pc, i_sp, i_lr, o_pc, o_sp, &o_fn);
}


struct BACKTRACE_PC_SP xt_return_address_ex(int lvl)
{
    const void *i_sp;
    const void *i_pc;

    const void *o_pc = NULL;
    const void *o_sp;

    __asm__ __volatile__(
      "mov  %[sp], a1\n\t"
      "movi %[pc], .\n\t"
      : [pc]"=r"(i_pc), [sp]"=r"(i_sp)
      :
      : "memory");

    // The net effect of calling this function raises level up by 2.
    // We will need to skip over two more levels
    lvl += 2;
    while(lvl-- && xt_retaddr_callee(i_pc, i_sp, NULL, &o_pc, &o_sp)) {
        i_pc = o_pc;
        i_sp = o_sp;
    }

    struct BACKTRACE_PC_SP pc_sp = {NULL, NULL};
    if (xt_pc_is_valid(o_pc)) {
        pc_sp.pc = o_pc;
        pc_sp.sp = o_sp;
    }

    return pc_sp;
}


#if 1
const void *xt_return_address(int lvl) {
    return xt_return_address_ex(lvl).pc;
}
#else

const void *xt_return_address(int lvl)
{
    const void *i_sp;
    const void *i_pc;

    const void *o_pc = NULL;
    const void *o_sp;

    __asm__ __volatile__(
      "mov  %[sp], a1\n\t"
      "movi %[pc], .\n\t"
      : [pc]"=r"(i_pc), [sp]"=r"(i_sp)
      :
      : "memory");

    // The net effect of calling this function raises level up by 2.
    // We will need to skip over two more levels
    lvl += 2;
    while(lvl-- && xt_retaddr_callee(i_pc, i_sp, NULL, &o_pc, &o_sp)) {
        i_pc = o_pc;
        i_sp = o_sp;
    }

    return xt_pc_is_valid(o_pc) ? o_pc : NULL;
}
#endif

}; //extern "C"
