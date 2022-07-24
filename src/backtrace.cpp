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
#include <esp8266_undocumented.h>
#include <mmu_iram.h>

#include "backtrace.h"

#ifndef BACKTRACE_MAX_RETRY
#define BACKTRACE_MAX_RETRY 3
#endif

#ifndef BACKTRACE_MAX_LOOKBACK
#define BACKTRACE_MAX_LOOKBACK 512
#endif

#ifdef ESP_DEBUG_BACKTRACE_CPP
#define ETS_PRINTF ets_uart_printf
#else
#define ETS_PRINTF(...)
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


#if ESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION
// Do byte pointer accesses and let the Exception handler resolve each read
// into a 32-bit access.
inline int idx(void *a, uint32_t b) { return ((uint8_t*)a)[b]; }
inline int sidx(void *a, uint32_t b) { return ((sint8_t*)a)[b]; }
#else
// Use performance macros to access IRAM byte data w/o generating an exception.
inline int idx(void *a, uint32_t b) { return mmu_get_uint8((void*)((uintptr_t)(a) + (b))); }
inline int sidx(void *a, uint32_t b) { return ((sint8_t)mmu_get_uint8((void*)((uintptr_t)(a) + (b)))); }
#endif

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
static int find_s32i_a0_a1(uint32_t pc, uint32_t off) {
    int a0_off = -1;  // Assume failed

    // For the xtensa instruction set, it looks like, bit 0x08 on the LSB is the
    // instruction size bit. set => two bytes / clear => 3 bytes
    //
    // Scan forward
    for (uint8_t *p0 = (uint8_t *)(pc - off);
         (uintptr_t)p0 < pc;
         p0 = (idx(p0, 0) & 0x08) ? &p0[2] : &p0[3]) {
        //
        // 02 61 zz s32i   a0, a1, n  (n = zz * 4)
        //
        if (idx(p0, 0) == 0x02 && idx(p0, 1) == 0x61) {
            a0_off = 4 * idx(p0, 2);
            break;
        } else
        //
        // 09 z1    s32i.n a0, a1, n  (n = z * 4)
        //
        if (idx(p0, 0) == 0x09 && (idx(p0, 1) & 0x0F) == 0x01) {
            a0_off = 4 * (idx(p0, 1) >> 4);
            break;
        }
    }
    return a0_off;
}

static bool verify_path_ret_to_pc(uint32_t pc, uint32_t off) {
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
int xt_retaddr_callee(const void *i_pc, const void *i_sp, const void *i_lr, void **o_pc, void **o_sp)
{
    uint32_t lr = (uint32_t)i_lr; // last return ??
    uint32_t pc = (uint32_t)i_pc;
    uint32_t sp = (uint32_t)i_sp;

    uint32_t off = 0;
    const uint32_t text_size = prev_text_size(pc);

    // The question is how agressively should we keep looking.
    // For now, keep searching until both BACKTRACE_MAX_RETRY and
    // BACKTRACE_MAX_LOOKBACK are exhaused.  BACKTRACE_MAX_LOOKBACK defaults to
    // 512 bytes and BACKTRACE_MAX_RETRY to 1.
    for (size_t retry = 0;
        ((retry < BACKTRACE_MAX_RETRY) || (off < BACKTRACE_MAX_LOOKBACK)) &&
          (off < text_size) && pc;
        retry++, off++)
    {
        pc = (uint32_t)i_pc;
        sp = (uint32_t)i_sp;

        // Scan backward 1 byte at a time looking for a stack reserve or ret.n
        // This requires special handling to read IRAM/IROM/FLASH 1 byte at a time.
        for (; off < text_size; off++) {
            // What about 12d1xx   ADDMI a1, a1, -32768..32512 (-128..127 shifted by 8)?
            // Not likely to be useful. This is mostly used at the start of an
            // Exception frame. No need to see what an Interrupt interrupted. More
            // interesting to see what the interrupt did to cause an exception.
            // When we need to look behind an Exception frame, those start point
            // values are passed in.
            uint8_t *pb = (uint8_t *)(pc - off);
            //
            // 12 c1 xx   ADDI a1, a1, -128..127
            //
            if (idx(pb, 0) == 0x12 && idx(pb, 1) == 0xc1) {
                const int stk_size = sidx(pb, 2); //((int8_t *)pb)[2];

                // Skip ADDIs that are clearing previous stack usage or not a multiple of 16.
                if (stk_size >= 0 || stk_size % 16 != 0) {
                    continue;
                }
                // Negative stack size, stack space creation/reservation and multiple of 16

                if (off <= 3) {
                    pc = lr;
                    ETS_PRINTF("\noff <= 3\n");

                } else {
                    int a0_offset = find_s32i_a0_a1(pc, off);
                    if (a0_offset < 0 || a0_offset >= -stk_size) {
                        continue;
                    } else {
                        uint32_t *sp_a0 = (uint32_t *)((uintptr_t)sp + (uintptr_t)a0_offset);
                        ETS_PRINTF("\npc:sp 0x%08X:0x%08X, stk_size: %d, a0_offset: %d, %p(0x%08x)\n", pc, sp, stk_size, a0_offset, sp_a0, *sp_a0);
                        pc = *sp_a0;
                    }
                }

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
                if (!stk_size || stk_size >= 2048) {
                    // Zero or negative keep looking
                    continue;
                }
                //
                // r0 11 c0   SUB a1, a1, r
                //
                bool found = false;
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
                    continue;
                }

                int a0_offset = find_s32i_a0_a1(pc, off);
                if (a0_offset < 0 || a0_offset >= stk_size) {
                    continue;
                } else {
                    pc = *(uint32_t *)(sp + a0_offset);
                }

                sp += stk_size;

                break;
            } else
            // Most fail to find, land here.
            //
            // 0d f0      	RET.N
            //
            if (idx(pb, 0) == 0x0d && idx(pb, 1) == 0xf0) {
                ETS_PRINTF("\nRET.N pb: 0x%08X\n", (uint32_t)pb);

                if (!verify_path_ret_to_pc(pc, off)) {
                    continue;
                }

                if (off <= 3) {
                    pc = lr;
                    break;
                }

                continue;
            }
        }
        if (xt_pc_is_valid((void *)pc))
            break;
    }

    //
    // Save only if successful
    //
    if (off < text_size) {
        *o_sp = (void *)sp;
        *o_pc = (void *)pc;
        if (xt_pc_is_valid(*o_pc)) {
            // We changed the output registers anyway. So the caller can
            // evaluate what to do next.
            return 1;
        }
    }

    return 0;
}

struct BACKTRACE_PC_SP xt_return_address_ex(int lvl)
{
    void *i_sp;
    void *i_pc;

    void *o_pc = NULL;
    void *o_sp;

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
void *xt_return_address(int lvl) {
    return xt_return_address_ex(lvl).pc;
}
#else

void *xt_return_address(int lvl)
{
    void *i_sp;
    void *i_pc;

    void *o_pc = NULL;
    void *o_sp;

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
