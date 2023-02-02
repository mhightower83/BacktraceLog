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

/*
  Save a copy of backtrace results in an IRAM or DRAM buffer possitioned to not
  be reused or overwritten at restart.

  void backtraceLog_report(Print& out, bool clear);
  Print saved backtrace data and optionally clear the data.

  To build with backtrace support define the number of function call levels with
  DEBUG_ESP_BACKTRACELOG_MAX. Minimum allowed values is 4. If defined value is
  between 0 and 4, it is reset to 32.

  RTC memory 192 32-bit words total - data stays valid through sleep and EXT_RST
  0                                 64         96                           192
  |                                 |<-eboot-->|<---user available space---->|
  |                                 | 32 words |          96 words           |
  |<----------system data---------->|<--------------user data--------------->|
  | 64 32-bit words (256 bytes)     |     128 32-bit words (512 bytes)       |

  When doing OTA upgrades, the first 32 words of the user data area is used by
  eboot (struct eboot_command). Offset 64 through 95 can be overwritten.

  DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET - set to start word for RTC memory
  Cannot be less than 64 or more then 192

  TODO -
    * think about init time, when to best handle, for an early crash capture
    * Post HWDT processing
      * identify active stack at crash
      * yielded contexts will be easy to trace
        * PC & SP are available
      * active stack will need clues.
        * `-fno-omit-frame-pointer` may help
        * ??
    * hmm, eh_frames! this is looking like a never ending projet.
      Stopping, This does what I need for now.
      ref: https://stackoverflow.com/questions/14091231/what-do-the-eh-frame-and-eh-frame-hdr-sections-store-exactly
      ref: http://web.archive.org/web/20130111101034/http://blog.mozilla.org/respindola/2011/05/12/cfi-directives
*/
#include <Arduino.h>
#include <user_interface.h>
#include <umm_malloc/umm_malloc.h>
#include <core_esp8266_non32xfer.h>
#include <cont.h>
#include "BacktraceLog.h"

#pragma GCC optimize("Os")

#ifndef ARDUINO_ESP8266_VERSION_DEC
#ifdef ARDUINO_ESP8266_MAJOR
#define ARDUINO_ESP8266_VERSION_DEC ( \
  ARDUINO_ESP8266_MAJOR * 10000 + \
  ARDUINO_ESP8266_MINOR *   100 + \
  ARDUINO_ESP8266_REVISION          )
#else
// Assume current
#define ARDUINO_ESP8266_VERSION_DEC 30100
#endif
#endif

#if ARDUINO_ESP8266_VERSION_DEC > 30002
#define PC_SUSPEND pc_suspend
#define SP_SUSPEND sp_suspend
#else
// Arduino ESP8266 v3.0.2 and before
#define PC_SUSPEND pc_ret
#define SP_SUSPEND sp_ret
#endif

#if (DEBUG_ESP_BACKTRACELOG_MAX > 0)
#include "backtrace.h"

union BacktraceLogUnion {
    struct BACKTRACE_LOG log;
    uint32_t word32[sizeof(struct BACKTRACE_LOG) / sizeof(uint32_t)];
};

// Need a durable log buffer - as in long life, persisting across reboots.
// Options are to use IRAM or noinit DRAM, with an option to backup to user RTC

union BacktraceLogUnion *pBT = NULL;

#if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
#if (DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET < 64) || (DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET >= 192)
#error "DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET offset is out of range (64 - 192) for user RTC memory"
#endif
#endif

#if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
static struct RTC_STATUS {
    size_t size;  // Size set to zero when not available, bytes
    size_t max_depth;
} rtc_status __attribute__((section(".noinit")));

constexpr size_t baseSize32BacktraceLog = offsetof(union BacktraceLogUnion, log.pc) / sizeof(uint32_t);
#endif

#if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
// assert(sizeof(struct BACKTRACE_LOG) <= 512 - DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET_OFFSET);
#endif

// The block should be in 8-byte increments and fall on an 8-byte alignment.
#define IRAM_RESERVE_SZ ((sizeof(union BacktraceLogUnion) + 7) & ~7)

extern struct rst_info resetInfo;

/*
  Works with array of uint16_t values, 2 byte increments.
  Minimum length 4 bytes.
  len16 should be number of short values, sizeof()/2
*/
static uint16_t xorChecksum16(void *p, size_t len16, uint16 xsum16 = 0)
{
    size_t len32 = len16 / 2;
    uint32_t *x = (uint32_t *)p;
    uint32 xsum = xsum16;
    size_t i;
    for (i = 0; i < len32; i++) {
        xsum ^= x[i];
    }

    xsum = ((xsum >> 16) ^ (uint16_t)xsum);
    if ((len16 & 1)) {
        xsum ^= *((uint16_t *)&x[i]);
    }
    return (uint16_t)xsum;
}

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
inline bool is_mem_valid(void) {
    // "mem" refers to IRAM or noinit DRAM. RTC mem is different.
    return (
      REASON_DEFAULT_RST != resetInfo.reason &&  // Not power-on
      REASON_DEEP_SLEEP_AWAKE != resetInfo.reason &&
      REASON_EXT_SYS_RST >= resetInfo.reason
    );
}

int BacktraceLog::read(uint32_t *p, size_t sz) {
    if (NULL == pBT) return 0;

    size_t limit = (sz < pBT->log.count) ? sz : pBT->log.count;
    for (size_t i = 0; i < limit; i++) {
        p[i] = (uint32_t)pBT->log.pc[i];
    }

    return limit;
}

int BacktraceLog::read(struct BACKTRACE_LOG *p) {
    if (NULL == pBT || NULL == p) return 0;

    int sz = sizeof(struct BACKTRACE_LOG);
    ets_memcpy(p, pBT, sz); // will handle IRAM copy
    return sz;
}

void BacktraceLog::report(Print& out) {
    out.printf_P(PSTR("Backtrace Crash Report\r\n"));

    if (NULL == pBT) {
        out.printf_P(PSTR("  Log buffer not defined\r\n"));
#ifdef DEBUB_ESP_PORT
        out.printf_P(PSTR("  Recheck initialization options: preinit alternate name, etc.\r\n"));
#endif
        return;
    }

    out.printf_P(PSTR("  Boot Count: %u\r\n"), pBT->log.bootCounter);
    #if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
    #if DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
    out.printf_P(PSTR("  Config: IRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    out.printf_P(PSTR("  Config: DRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif
    #elif DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
    out.printf_P(PSTR("  Config: IRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    out.printf_P(PSTR("  Config: DRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif

    if (pBT->log.crashCount) {
        out.printf_P(PSTR("  Crash count: %u\r\n"), pBT->log.crashCount);
    }
    if (pBT->log.count) {
        out.printf_P(PSTR("  Reset Reason: %u\r\n"), pBT->log.rst_info.reason);
        if (100 > pBT->log.rst_info.reason && REASON_WDT_RST != pBT->log.rst_info.reason) {
            out.printf_P(PSTR("  Exception (%d):\r\n  epc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\r\n"),
                pBT->log.rst_info.exccause, pBT->log.rst_info.epc1,
                pBT->log.rst_info.epc2, pBT->log.rst_info.epc3,
                pBT->log.rst_info.excvaddr, pBT->log.rst_info.depc);
        }
        out.printf("  Backtrace:");
        for (size_t i = 0; i < pBT->log.count; i++) {
            out.printf_P(PSTR(" %p"), pBT->log.pc[i]);
        }
        out.printf_P(PSTR("\r\n"));
        if (0x4000050cu == (uint32_t)pBT->log.pc[pBT->log.count - 1]) {
            out.printf_P(PSTR("  Backtrace Context: level 1 Interrupt Handler\r\n"));
        }
    } else {
        out.printf_P(PSTR("  Backtrace empty\r\n"));
    }
}

void BacktraceLog::clear(Print& out) {
    (void)out;
    backtraceLog_clear();
}

int BacktraceLog::available() {
    if (NULL == pBT) return 0;
    return pBT->log.count;
}

extern "C" {
void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);
int umm_info_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define ETS_PRINTF(fmt, ...) umm_info_safe_printf_P(PSTR(fmt), ##__VA_ARGS__)

#if defined(DEBUG_ESP_BACKTRACELOG_CPP)
#define ETS_PRINTF2 ETS_PRINTF
#define SHOW_PRINTF(fmt, ...)
#elif DEBUG_ESP_BACKTRACELOG_SHOW
#define ETS_PRINTF2(fmt, ...)
#define SHOW_PRINTF ETS_PRINTF
#else
#define ETS_PRINTF2(fmt, ...)
#define SHOW_PRINTF(fmt, ...)
#endif

static uint32_t do_checksum(union BacktraceLogUnion *p) {
    uint32_t chksum = 0x80000000u;
    if (p) {
        chksum = xorChecksum16(&p->log.max,
            offsetof(union BacktraceLogUnion, log.pc)/2
            - offsetof(union BacktraceLogUnion, log.max)/2
            + DEBUG_ESP_BACKTRACELOG_MAX * 2);
    }
    return chksum;
}

void backtraceLog_report(int (*ets_printf_P)(const char *fmt, ...)) {
    if (NULL == ets_printf_P) {
        ets_printf_P = umm_info_safe_printf_P;
    }

    ets_printf_P(PSTR("Backtrace Crash Report\r\n"));

    if (NULL == pBT) {
        ets_printf_P(PSTR("  Insufficient IRAM for log buffer.\r\n"));
        return;
    }

    ets_printf_P(PSTR("  Boot Count: %u\r\n"), pBT->log.bootCounter);
    #if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
    #if DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
    ets_printf_P(PSTR("  Config: IRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    ets_printf_P(PSTR("  Config: DRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif
    #elif DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
    ets_printf_P(PSTR("  Config: IRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    ets_printf_P(PSTR("  Config: DRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif

    if (pBT->log.crashCount) {
        ets_printf_P(PSTR("  Crash count: %u\r\n"), pBT->log.crashCount);
    }
    if (pBT->log.count) {
        ets_printf_P(PSTR("  Reset Reason: %u\r\n"), pBT->log.rst_info.reason);
        if (pBT->log.rst_info.reason < 100) {
            ets_printf_P(PSTR("  Exception (%d):\r\n  epc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\r\n"),
                pBT->log.rst_info.exccause, pBT->log.rst_info.epc1,
                pBT->log.rst_info.epc2, pBT->log.rst_info.epc3,
                pBT->log.rst_info.excvaddr, pBT->log.rst_info.depc);
        }
        ets_printf("  Backtrace:");
        for (size_t i = 0; i < pBT->log.count; i++) {
            ets_printf_P(PSTR(" %p"), pBT->log.pc[i]);
        }
        ets_printf_P(PSTR("\r\n"));
        if (0x4000050cu == (uint32_t)pBT->log.pc[pBT->log.count - 1]) {
            ets_printf_P(PSTR("  Backtrace Context: level 1 Interrupt Handler\r\n"));
        }
    } else {
        ets_printf_P(PSTR("  Backtrace empty\r\n"));
    }
}

void backtraceLog_clear(void) {
    if (pBT) {
        size_t start_wd = offsetof(union BacktraceLogUnion, log.crashCount) / sizeof(uint32_t);
        size_t sz = offsetof(union BacktraceLogUnion, log.pc)
                  - offsetof(union BacktraceLogUnion, log.crashCount)
                  + sizeof(pBT->log.pc[0]) * pBT->log.max;
        // memset(&pBT->log.crashCount, 0, sz);
        memset(&pBT->word32[start_wd], 0, sz);

#if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
        if (rtc_status.size) {
            // No prior NONOS SDK initialization needed to call.
            system_rtc_mem_write(DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET, &pBT->word32[0], rtc_status.size);
        }
#endif
    }
}

void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end) {
    (void)stack_end;
    const void *i_pc, *i_sp, *lr, *pc, *sp;
    [[maybe_unused]] const void *fn;
    int repeat;

    if (NULL == pBT) {
        return;
    }

    backtraceLog_begin(rst_info);
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
        const struct BACKTRACE_PC_SP pc_sp = xt_return_address_ex(0);
        pc = pc_sp.pc;
        sp = pc_sp.sp;

        lr = NULL;
        ETS_PRINTF2("\n\nBacktrace Crash Reporter - Exception space:\n ");
        do {
            i_pc = pc;
            i_sp = sp;
            ETS_PRINTF2(" %p:%p", i_pc, i_sp);
            repeat = xt_retaddr_callee_ex(i_pc, i_sp, NULL, &pc, &sp, &fn);
            if (fn) { ETS_PRINTF2(":<%p>", fn); }
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
            pBT->log.rst_info.exccause = exccause;
            pBT->log.rst_info.epc1 = epc1;
        }
        pc = (void*)epc1;
        sp = (void*)((uintptr_t)frame + 256); // Step back before the exception occured
    } else {
        struct BACKTRACE_PC_SP pc_sp = xt_return_address_ex(1);
        pc = pc_sp.pc;
        sp = pc_sp.sp;
        lr = NULL;
    }
    ETS_PRINTF2("  i_pc: 0x%08x, i_sp: 0x%08x, i_lr: 0x%08x\n", (uint32_t)pc, (uint32_t)sp, (uint32_t)lr);


    ETS_PRINTF2("\n\nBacktrace Crash Reporter - User space:\n ");
    SHOW_PRINTF("\nBacktrace:");
    do {
        ETS_PRINTF2(" %p:%p", pc, sp);
        SHOW_PRINTF(" %p:%p", pc, sp);
        backtraceLog_write(pc);
        repeat = xt_retaddr_callee_ex(pc, sp, lr, &pc, &sp, &fn);
        if (fn) { ETS_PRINTF2(":<%p>", fn); }
        if (fn) { SHOW_PRINTF(":<%p>", fn); }  // estimated start of the function
        lr = NULL;
    } while(repeat);
    if (g_pcont->PC_SUSPEND) {
        // Looks like we crashed while the Sketch was yielding.
        // Finish traceback on the cont (loop_wrapper) stack.
        ETS_PRINTF2(" 0:0");  // mark transistion
        SHOW_PRINTF(" 0:0");
        backtraceLog_write(NULL);
        // Extract resume context to traceback - see cont_continue in cont.S
        sp = (void*)((uintptr_t)g_pcont->SP_SUSPEND + 24u);   // a1
        pc = *(void**)((uintptr_t)g_pcont->SP_SUSPEND + 16u); // a0
        do {
            ETS_PRINTF2(" %p:%p", pc, sp);
            SHOW_PRINTF(" %p:%p", pc, sp);
            backtraceLog_write(pc);
            repeat = xt_retaddr_callee_ex(pc, sp, NULL, &pc, &sp, &fn);
            if (fn) { ETS_PRINTF2(":<%p>", fn); }
            if (fn) { SHOW_PRINTF(":<%p>", fn); }
        } while(repeat);
    }
    backtraceLog_fin();

    ETS_PRINTF2("\n\n");
    SHOW_PRINTF("\n\n");
}


void backtraceLog_init(union BacktraceLogUnion *p, size_t max, bool force) {
    if (p) {
        if (force || p->log.chksum != do_checksum(p)) {
            memset(&p->word32[0], 0, sizeof(union BacktraceLogUnion));
            p->log.max = max;
        }
        p->log.bootCounter++;
        p->log.chksum = do_checksum(p);
    }
}

#if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
static void rtc_check_init(union BacktraceLogUnion *pBT) {
    rtc_status.size = 0;
    rtc_status.max_depth = 0;

    int free_rtc = (192 - DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET) - baseSize32BacktraceLog;
    if (free_rtc >= DEBUG_ESP_BACKTRACELOG_MIN) {
        rtc_status.max_depth = (free_rtc < DEBUG_ESP_BACKTRACELOG_MAX) ? free_rtc : DEBUG_ESP_BACKTRACELOG_MAX;
        rtc_status.size = 4 * (baseSize32BacktraceLog + rtc_status.max_depth);
        system_rtc_mem_read(DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET, &pBT->word32[0], rtc_status.size);
        if (pBT->log.max == rtc_status.max_depth && pBT->log.chksum == do_checksum(pBT)) {
            pBT->log.bootCounter++;
            pBT->log.chksum = do_checksum(pBT);
        } else {
            backtraceLog_init(pBT, rtc_status.max_depth, true);
        }
        system_rtc_mem_write(DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET, &pBT->word32[0], rtc_status.size);
    }
}
#else
static inline void rtc_check_init(union BacktraceLogUnion *pBT) {
    (void)pBT;
}
#endif

////////////////////////////////////////////////////////////////////////////////
//  Log Buffer init can take - one of three paths and can be in DRAM or IRAM.
//    1) IRAM unmanaged. Carveout handled at preinit(). If you have multiple data
//       uses for IRAM, I suggest using the method illustrated here for carving
//       up left over IRAM memory.
//    2) IRAM along side MMU_IRAM_HEAP. Carveout handled in umm_init_iram()
//    3) DRAM is the simplest and can be handle as preinit()
//       (TODO maybe - or class instantiation)
//
#if DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
/*
  Create a block of unmanaged and isolated IRAM for special uses.

  This is done by reducing the size of the managed 2nd Heap (Shared) at
  initialization time.
*/
#include <sys/config.h>  // For config/core-isa.h
void _text_end(void);

static struct BACKTRACELOG_MEM_INFO set_pBT(void) {
    uintptr_t iram_buffer = (uintptr_t)_text_end + 32;
    iram_buffer &= ~7;
    ssize_t iram_buffer_sz = iram_buffer - (uintptr_t)XCHAL_INSTRAM1_VADDR;
    if (iram_buffer_sz < MMU_IRAM_SIZE) {
        iram_buffer_sz = MMU_IRAM_SIZE - iram_buffer_sz;
    } else {
        iram_buffer_sz = 0;
    }

    if ((ssize_t)IRAM_RESERVE_SZ <= iram_buffer_sz) {
        if (pBT && pBT->log.chksum == do_checksum(pBT)) {
            // we need the computation; however, avoid double init
        } else {
            pBT = (union BacktraceLogUnion *)iram_buffer;
            bool zero = !is_mem_valid() && pBT;
            backtraceLog_init(pBT, DEBUG_ESP_BACKTRACELOG_MAX, zero);
            rtc_check_init(pBT);
        }

        // If you had another structure to allocate, calculate the next available
        // IRAM location and size available.
        iram_buffer += IRAM_RESERVE_SZ;
        iram_buffer_sz -= IRAM_RESERVE_SZ;
    } else {
        pBT = NULL;
    }

    struct BACKTRACELOG_MEM_INFO mem;
    mem.addr = NULL;
    mem.sz = 0;
    if (iram_buffer_sz > 0) {
        mem.addr = (void *)iram_buffer;
        mem.sz = iram_buffer_sz;
    }
    return mem;
}

#if defined(MMU_IRAM_HEAP)
void umm_init_iram(void) {
    /*
      Calculate the start of 2nd heap, staying clear of possible segment alignment
      adjustments and checksums. These can affect the persistence of data across
      reboots.
    */
    struct BACKTRACELOG_MEM_INFO sec_heap = set_pBT();
#ifdef DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB
    sec_heap = DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB(sec_heap.addr, sec_heap.sz);
#endif
    if (sec_heap.sz) {
        umm_init_iram_ex((void *)sec_heap.addr, sec_heap.sz, true);
    }
}

#else // #if defined(MMU_IRAM_HEAP)
// Nobody is using left over IRAM, grab a block after _text_end.
void SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG(void) {
#ifdef DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB
    struct BACKTRACELOG_MEM_INFO iram_buffer = set_pBT();
    // If you had another structure to allocate, iram_buffer has the next
    // address and remaining size available
    DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB(iram_buffer.addr, iram_buffer.sz);
#else
    (void)set_pBT();
#endif
}
#endif // #if defined(MMU_IRAM_HEAP)

///////////////////////////////////////////////////////////////////////////////
// For the case of a DRAM log buffer, initialization occurs through preinit()
//
#else // #if DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
/*
  Reserve some uninitialized DRAM in the noinit section. We manage the zeroing
  of the memory so we can store data across reboots for the Backtrace Log.
*/
union BacktraceLogUnion _pBT __attribute__((section(".noinit")));

static struct BACKTRACELOG_MEM_INFO set_pBT(void) {
    // At boot pBT loaded from flash as zero.
    if (pBT && pBT->log.chksum == do_checksum(pBT)) {
        // If not zero, we have already run; check checksum.
    } else {
        pBT = &_pBT;
        bool zero = !is_mem_valid();
        backtraceLog_init(pBT, DEBUG_ESP_BACKTRACELOG_MAX, zero);
        rtc_check_init(pBT);
    }
    struct BACKTRACELOG_MEM_INFO empty;
    empty.sz = 0;
    empty.addr = NULL;
    return empty;
}

void SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG(void) {  // void preinit(void);
    (void)set_pBT();
}
#endif //#if DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER

///////////////////////////////////////////////////////////////////////////////
//
//  May be called as a result of HWDT callback hwdt_pre_sdk_init().
//  set_pBT must handle duplicate calls
//
void backtraceLog_begin(struct rst_info *reset_info) {
    set_pBT();
    if (NULL == pBT) return;

    if (reset_info) {
        memcpy(&pBT->log.rst_info, reset_info, sizeof(struct rst_info));
    } else {
        memset(&pBT->log.rst_info, 0, sizeof(struct rst_info));
    }
    pBT->log.crashCount++;
    pBT->log.count = 0;
}

// continue logging after a data break flag
void backtraceLog_append(void) {
    set_pBT();
    if (NULL == pBT) return;
    pBT->log.pc[pBT->log.count++] = 0;  // data break flag, separator or record marker
}

void backtraceLog_fin(void) {
    if (NULL == pBT) return;

    pBT->log.chksum = do_checksum(pBT);
#if DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
    if (rtc_status.size) {
        system_rtc_mem_write(DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET, &pBT->word32[0], rtc_status.size);
    }
#endif
}

void backtraceLog_write(const void * const pc) {
    if (NULL == pBT) return;

    if (pBT && pBT->log.max > pBT->log.count) {
        pBT->log.pc[pBT->log.count++] = pc;
    }
}

}; // extern "C" {
#endif // #if (DEBUG_ESP_BACKTRACELOG_MAX > 0)
