/*
 *   Copyright 2022 M Hightower
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

// #ifdef ESP_DEBUG_BACKTRACELOG_MAX
/*
  Save a copy of backtrace results in an IRAM buffer possitioned to not be
  reused or overwritten at restart.

  void backtrace_report(Print& out, bool clear);
  Print saved backtrace data and optionally clear the data.

  To build with backtrace support define the number of function call levels with
  ESP_DEBUG_BACKTRACELOG_MAX. Minimum allowed values is 4. If defined value is
  between 0 and 4, it is reset to 32.
*/
#include <Arduino.h>
#include <user_interface.h>
#include <umm_malloc/umm_malloc.h>
#include <core_esp8266_non32xfer.h>
#include "BacktraceLog.h"

#if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
#include "backtrace.h"

// durable - as in long life, persisting across reboots.
struct BacktraceLog {
    uint32_t bootCounter;
    uint32_t chksum;
    uint32_t crashCount;
    struct rst_info rst_info;
    uint32_t count;
    void* log[ESP_DEBUG_BACKTRACELOG_MAX];
};
struct BacktraceLog *backtrace_log = NULL;

// The block should be in 8-byte increments and fall on an 8-byte alignment.
#define IRAM_RESERVE_SZ ((sizeof(struct BacktraceLog) + 7UL) & ~7UL)

extern struct rst_info resetInfo;

/*
  Define a function to determine if IRAM stored data is valid. The criteria used
  here can vary with how exhaustively you want the process to be.

  In this example, we are just going to look at the reset cause and assume all
  is well in certain situations. For this example, we include
  REASON_EXT_SYS_RST as a possible case for IRAM not being valid. The problem
  here is some devices will indicate REASON_EXT_SYS_RST for the Power-on case.

  If you wanted to be able to isolate the power-on case from a
  REASON_EXT_SYS_RST, you could add additional logic to set and verify a CRC or
  XOR sum on the IRAM data (or just a section of the IRAM data).
*/
inline bool is_ram_persistent(void) {
    return (REASON_WDT_RST <= resetInfo.reason && REASON_SOFT_RESTART >= resetInfo.reason);
}

void backtraceReport(Print& out) {
    out.printf_P(PSTR("\r\n\r\nBacktrace Crash Report\r\n"));

    if (NULL == backtrace_log) {
        out.printf_P(PSTR("  Insufficient IRAM for log buffer.\r\n"));
        return;
    }

    out.printf_P(PSTR("  Boot Count: %u\r\n"), backtrace_log->bootCounter);
#if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
    out.printf_P(PSTR("  Config: IRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(struct BacktraceLog), ESP_DEBUG_BACKTRACELOG_MAX);
#else
    out.printf_P(PSTR("  Config: DRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(struct BacktraceLog), ESP_DEBUG_BACKTRACELOG_MAX);
#endif
    if (backtrace_log->crashCount) {
        out.printf_P(PSTR("  Crash count: %u\r\n"), backtrace_log->crashCount);
    }
    if (backtrace_log->count) {
        out.printf_P(PSTR("  Reset Reason: %u\r\n"), backtrace_log->rst_info.reason);
        if (backtrace_log->rst_info.reason < 100) {
            out.printf_P(PSTR("  Exception (%d):\r\n  epc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\r\n"),
                backtrace_log->rst_info.exccause, backtrace_log->rst_info.epc1,
                backtrace_log->rst_info.epc2, backtrace_log->rst_info.epc3,
                backtrace_log->rst_info.excvaddr, backtrace_log->rst_info.depc);
        }
        out.printf("  Backtrace:");
        for (size_t i = 0; i < backtrace_log->count; i++) {
            out.printf_P(PSTR(" %p"), backtrace_log->log[i]);
        }
        out.printf_P(PSTR("\r\n"));
        if (0x4000050cu == (uint32_t)backtrace_log->log[backtrace_log->count - 1]) {
            out.printf_P(PSTR("  Backtrace Context: level 1 Interrupt Handler\r\n"));
        }
    } else {
        out.printf_P(PSTR("  Backtrace empty\r\n"));
    }
}

void backtraceClear(Print& out) {
    (void)out;
    if (backtrace_log) {
        memset(backtrace_log, 0, sizeof(struct BacktraceLog));
    }
}

bool isBacktrace() {
    if (backtrace_log) {
        return (0 != backtrace_log->count);
    }
    return false;
}

extern "C" {
void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);
int umm_info_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define ETS_PRINTF(fmt, ...) umm_info_safe_printf_P(PSTR(fmt), ##__VA_ARGS__)

#if defined(ESP_DEBUG_BACKTRACELOG_CPP)
#define ETS_PRINTF2 ETS_PRINTF
#define SHOW_PRINTF(fmt, ...)
#elif defined(ESP_DEBUG_BACKTRACELOG_SHOW)
#define ETS_PRINTF2(fmt, ...)
#define SHOW_PRINTF ETS_PRINTF
#else
#define ETS_PRINTF2(fmt, ...)
#define SHOW_PRINTF(fmt, ...)
#endif

static uint32_t do_checksum(struct BacktraceLog *backtrace_log) {
    uint32_t chksum = 0;
    if (backtrace_log) {
        chksum = (uint32_t)backtrace_log + (uint32_t)custom_crash_callback +
            backtrace_log->crashCount + backtrace_log->count;
        if (0 == chksum) chksum = 1;
    }
    return chksum;
}

void backtrace_report(int (*ets_printf_P)(const char *fmt, ...)) {
    if (NULL == ets_printf_P) {
        ets_printf_P = umm_info_safe_printf_P;
    }

    ets_printf_P(PSTR("\n\nBacktrace Crash Report\n"));

    if (NULL == backtrace_log) {
        ets_printf_P(PSTR("  Insufficient IRAM for log buffer.\n"));
        return;
    }

    if (backtrace_log->crashCount) {
        ets_printf_P(PSTR("  Boot Count: %u\n"), backtrace_log->bootCounter);
        ets_printf_P(PSTR("  IRAM log buffer: %u bytes, MAX backtrace: %u levels\n"), sizeof(struct BacktraceLog), ESP_DEBUG_BACKTRACELOG_MAX);
        ets_printf_P(PSTR("  Reset Reason %u\n"), backtrace_log->rst_info.reason);
        if (backtrace_log->rst_info.reason < 100) {
            ets_printf_P(PSTR("  Exception (%d):\n  epc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\n"),
                backtrace_log->rst_info.exccause, backtrace_log->rst_info.epc1,
                backtrace_log->rst_info.epc2, backtrace_log->rst_info.epc3,
                backtrace_log->rst_info.excvaddr, backtrace_log->rst_info.depc);
        }
        ets_printf_P(PSTR(     "  Crash count: %u\n "), backtrace_log->crashCount);
        for (size_t i = 0; i < backtrace_log->count; i++) {
            ets_printf_P(PSTR(" %p"), backtrace_log->log[i]);
        }
        ets_printf_P(PSTR("\n"));
        if (0x4000050cu == (uint32_t)backtrace_log->log[backtrace_log->count - 1]) {
            ets_printf_P(PSTR("  Backtrace Context: level 1 Interrupt Handler\n"));
        }
    }
}

void backtrace_clear(void) {
    if (backtrace_log) {
        memset(backtrace_log, 0, sizeof(struct BacktraceLog));
    }
}

void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end) {
    (void)stack_end;
    void *i_pc, *i_sp, *lr, *pc, *sp;

    if (NULL == backtrace_log) {
        return;
    }

    memcpy(&backtrace_log->rst_info, rst_info, sizeof(struct rst_info));

    // Assume no exception frame to work with. As with software abort/panic/...
    struct __exception_frame * frame = NULL;
    if (rst_info->reason < 100) {
        // There is an exceptiom frame - recover pointer
        frame = (struct __exception_frame * )(stack - 256u);
    }
    if (frame) {
        // pc = (void*)rst_info->epc1;
        // lr = (void*)frame->a0;
        // sp = (void*)stack; // or ((uintptr_t)frame + 256);

        // Postmortem may have changed and the offsets for adjusting before the
        // exception frame could be stale.
        // Use this alternative method, backward search for the start of the
        // exception frame from here.
        struct BACKTRACE_PC_SP pc_sp = xt_return_address_ex(0);
        pc = pc_sp.pc;
        sp = pc_sp.sp;

        lr = NULL;
        ETS_PRINTF2("\n\nBacktrace Crash Reporter - Exception space:\n ");
        int repeat;
        do {
            i_pc = pc;
            i_sp = sp;
            ETS_PRINTF2(" %p:%p", i_pc, i_sp);
            repeat = xt_retaddr_callee(i_pc, i_sp, lr, &pc, &sp);
            lr = NULL;
        } while (repeat > 0);
        ETS_PRINTF2("\n");
        ETS_PRINTF2("  Frame: 0x%08x, Backtrace Frame: 0x%08x\n", (uintptr_t)frame, (uint32_t)i_sp);
        ETS_PRINTF2("  i_pc: 0x%08x, pc: 0x%08x\n", (uint32_t)i_pc, (uint32_t)pc);
        ETS_PRINTF2("  i_sp: 0x%08x, sp: 0x%08x\n", (uint32_t)i_sp, (uint32_t)sp);

        frame = (struct __exception_frame * )sp;
        uint32_t epc1 = rst_info->epc1;
        uint32_t exccause = rst_info->exccause;
        lr = (void*)frame->a0;

        bool div_zero = (exccause == 0) && (epc1 == 0x4000dce5u);
        if (div_zero) {
            exccause = 6;
            // In place of the detached 'ILL' instruction., redirect attention
            // back to the code that called the ROM divide function.
            __asm__ __volatile__("rsr.excsave1 %0\n\t" : "=r"(epc1) :: "memory");
            lr = NULL;
            backtrace_log->rst_info.exccause = exccause;
            backtrace_log->rst_info.epc1 = epc1;
        }
        pc = (void*)epc1;
        sp = (void*)((uintptr_t)frame + 256); // Step back before the exception occured
    } else {
        struct BACKTRACE_PC_SP pc_sp = xt_return_address_ex(1);
        pc = pc_sp.pc;
        sp = pc_sp.sp;
        lr = NULL;
    }

    ETS_PRINTF2("\n\nBacktrace Crash Reporter - User space:\n ");
    SHOW_PRINTF("\nBacktrace:");
    backtrace_log->crashCount++;
    backtrace_log->count = 0;

    int repeat;
    do {
        i_pc = pc;
        i_sp = sp;
        ETS_PRINTF2(" %p:%p", pc, sp);
        SHOW_PRINTF(" %p", pc);
        if (backtrace_log->count >= ESP_DEBUG_BACKTRACELOG_MAX) {
            break;
        }
        backtrace_log->log[backtrace_log->count++] = pc;
        repeat = xt_retaddr_callee(i_pc, i_sp, lr, &pc, &sp);
        lr = NULL;
    } while(repeat);
    ETS_PRINTF2("\n\n");
    SHOW_PRINTF("\n\n");

    backtrace_log->chksum = do_checksum(backtrace_log);

    // backtrace_report(umm_info_safe_printf_P);
}


/////////////////////////////////////////////////////////////////////////////
#if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
/*
  Create a block of unmanaged and isolated IRAM for special uses.

  This is done by reducing the size of the managed 2nd Heap (Shared) at
  initialization time.
*/
#include <sys/config.h>  // For config/core-isa.h
extern "C" void _text_end(void);

#if defined(MMU_IRAM_HEAP)
extern "C" void umm_init_iram(void) {
    /*
      Calculate the start of 2nd heap, staying clear of possible segment alignment
      adjustments and checksums. These can affect the persistence of data across
      reboots.
    */
    uintptr_t sec_heap = (uintptr_t)_text_end + 32;
    sec_heap &= ~7;

    size_t sec_heap_sz = sec_heap - (uintptr_t)XCHAL_INSTRAM1_VADDR;
    if (sec_heap_sz < MMU_IRAM_SIZE) {
        sec_heap_sz = MMU_IRAM_SIZE - sec_heap_sz;
    } else {
        sec_heap_sz = 0;
    }
    if (sec_heap_sz >= IRAM_RESERVE_SZ + 3 * (UMM_OVERHEAD_ADJUST + sizeof(uint32_t))) {
        sec_heap_sz -= IRAM_RESERVE_SZ;  // Shrink IRAM heap
        #if 0 //swap location
        umm_init_iram_ex((void *)sec_heap, sec_heap_sz, true);
        backtrace_log = (struct BacktraceLog *)(sec_heap + sec_heap_sz);
        #else
        backtrace_log = (struct BacktraceLog *)sec_heap);
        umm_init_iram_ex((void *)(sec_heap + IRAM_RESERVE_SZ), sec_heap_sz, true);
        #endif
    } else {
        // Available IRAM is too tight. Give priority to IRAM Heap.
        backtrace_log = NULL;
        if (sec_heap_sz >= (3 * (UMM_OVERHEAD_ADJUST + sizeof(uint32_t)))) {
            umm_init_iram_ex((void *)sec_heap, sec_heap_sz, true);
        }
    }
    if (backtrace_log) {
        if (!is_ram_persistent()) {
            memset(backtrace_log, 0, sizeof(struct BacktraceLog));
        }
        backtrace_log->bootCounter++;
    }
#if ESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION
#if defined(NON32XFER_HANDLER) || defined(MMU_IRAM_HEAP)
    // Already handled from user_init()
#else
    install_non32xfer_exception_handler();
#endif
#endif
}

#else
// Nobody is using left over IRAM, grab a block after _text_end.
void preinit(void) {
    uintptr_t iram_buffer = (uintptr_t)_text_end + 32;
    iram_buffer &= ~7;
    size_t iram_buffer_sz = iram_buffer - (uintptr_t)XCHAL_INSTRAM1_VADDR;
    if (iram_buffer_sz < MMU_IRAM_SIZE) {
        iram_buffer_sz = MMU_IRAM_SIZE - iram_buffer_sz;
    } else {
        iram_buffer_sz = 0;
    }
    if (IRAM_RESERVE_SZ <= iram_buffer_sz) {
        backtrace_log = (struct BacktraceLog *)iram_buffer;
        if (!is_ram_persistent() || backtrace_log->chksum != do_checksum(backtrace_log)) {
            memset(backtrace_log, 0, sizeof(struct BacktraceLog));
        }
        backtrace_log->bootCounter++;
        #if 0
        // If you had another structure to allocate, calculate the next available
        // IRAM location and size available.
        iram_buffer += IRAM_RESERVE_SZ;
        iram_buffer_sz -= IRAM_RESERVE_SZ;
        #endif
    }
#if ESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION
#if defined(NON32XFER_HANDLER) || defined(MMU_IRAM_HEAP)
    // Already handled from user_init()
#else
    install_non32xfer_exception_handler();
#endif
#endif
}
#endif

#else // #if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
/*
  Reserve some uninitialized DRAM in the noinit section. We manage the zeroing
  of the memory so we can store data across reboots for the Backtrace Log.
*/
struct BacktraceLog _backtrace_log __attribute__((section(".noinit")));

void preinit(void) {
    backtrace_log = &_backtrace_log;
    if (!is_ram_persistent() || backtrace_log->chksum != do_checksum(backtrace_log)) {
        memset(backtrace_log, 0, sizeof(struct BacktraceLog));
    }
}
#endif //#if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
};
#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
// #endif // #ifdef ESP_DEBUG_BACKTRACK
