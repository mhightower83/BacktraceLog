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

  void backtrace_report(Print& out, bool clear);
  Print saved backtrace data and optionally clear the data.

  To build with backtrace support define the number of function call levels with
  ESP_DEBUG_BACKTRACELOG_MAX. Minimum allowed values is 4. If defined value is
  between 0 and 4, it is reset to 32.

  RTC memory 192 32-bit words total - data stays valid through sleep and EXT_RST
  0                                 64                                       192
  |<----------system data---------->|<--------------user data--------------->|
  | 64 32-bit words (256 bytes)     |     128 32-bit words (512 bytes)       |

  ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER - set to start word for RTC memory
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
#include "BacktraceLog.h"

#if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
#include "backtrace.h"

// Need a durable log buffer - as in long life, persisting across reboots.
// Options are to use IRAM or noinit DRAM, with an option to backup to user RTC

union BacktraceLogUnion {
    struct BacktraceLog log;
    uint32_t word32[sizeof(struct BacktraceLog) / sizeof(uint32_t)];
};
union BacktraceLogUnion *pBT = NULL;

#if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
#if (ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER < 64) || (ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER >= 192)
#error "ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER offset is out of range (64 - 192) for user RTC memory"
#endif
#endif

#if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
struct RTC_Status {
    size_t size;  // Size set to zero when not available, bytes
    size_t max_depth;
} rtc_status __attribute__((section(".noinit")));

constexpr size_t baseSize32BacktraceLog = offsetof(union BacktraceLogUnion, log.pc) / sizeof(uint32_t);
#endif

#if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
// assert(sizeof(struct BacktraceLog) <= 512 - ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET);
#endif

// The block should be in 8-byte increments and fall on an 8-byte alignment.
#define IRAM_RESERVE_SZ ((sizeof(union BacktraceLogUnion) + 7UL) & ~7UL)

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
    return (REASON_WDT_RST <= resetInfo.reason && REASON_SOFT_RESTART >= resetInfo.reason);
}

int backtraceLogRead(uint32_t *p, size_t sz) {
    if (NULL == pBT) return 0;

    size_t limit = (sz < pBT->log.count) ? sz : pBT->log.count;
    for (size_t i = 0; i < limit; i++) {
        p[i] = (uint32_t)pBT->log.pc[i];
    }

    return limit;
}

int backtraceLogRead(struct BacktraceLog *p) {
    if (NULL == pBT || NULL == p) return 0;

    int sz = sizeof(struct BacktraceLog);
    ets_memcpy(p, pBT, sz); // handles IRAM copy
    return sz;
}

void backtraceLogReport(Print& out) {
    out.printf_P(PSTR("Backtrace Crash Report\r\n"));

    if (NULL == pBT) {
        out.printf_P(PSTR("  Insufficient IRAM for log buffer.\r\n"));
        return;
    }

    out.printf_P(PSTR("  Boot Count: %u\r\n"), pBT->log.bootCounter);
    #if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
    #if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
    out.printf_P(PSTR("  Config: IRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    out.printf_P(PSTR("  Config: DRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif
    #elif ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
    out.printf_P(PSTR("  Config: IRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    out.printf_P(PSTR("  Config: DRAM log buffer: %u bytes, MAX backtrace: %u levels\r\n"), sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif

    if (pBT->log.crashCount) {
        out.printf_P(PSTR("  Crash count: %u\r\n"), pBT->log.crashCount);
    }
    if (pBT->log.count) {
        out.printf_P(PSTR("  Reset Reason: %u\r\n"), pBT->log.rst_info.reason);
        if (pBT->log.rst_info.reason < 100) {
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

void backtraceLogClear(Print& out) {
    (void)out;
    backtrace_clear();
}

int backtraceLogAvailable() {
    if (NULL == pBT) return 0;
    return pBT->log.count;
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

#if 0
static uint32_t do_checksum(union BacktraceLogUnion *pBT) {
    uint32_t chksum = 0;
    if (pBT) {
        chksum = (uint32_t)pBT + (uint32_t)custom_crash_callback +
            pBT->log.crashCount + pBT->log.count;
        if (0 == chksum) chksum = 1;
    }
    return chksum;
}
#else
static uint32_t do_checksum(union BacktraceLogUnion *p) {
    uint32_t chksum = 0x80000000u;
    if (p) {
        chksum = xorChecksum16(&p->log.max,
            offsetof(union BacktraceLogUnion, log.pc)/2
            - offsetof(union BacktraceLogUnion, log.max)/2
            + ESP_DEBUG_BACKTRACELOG_MAX * 2);
    }
    return chksum;
}
#endif

void backtrace_report(int (*ets_printf_P)(const char *fmt, ...)) {
    if (NULL == ets_printf_P) {
        ets_printf_P = umm_info_safe_printf_P;
    }

    ets_printf_P(PSTR("Backtrace Crash Report\r\n"));

    if (NULL == pBT) {
        ets_printf_P(PSTR("  Insufficient IRAM for log buffer.\r\n"));
        return;
    }

    ets_printf_P(PSTR("  Boot Count: %u\r\n"), pBT->log.bootCounter);
    #if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
    #if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
    ets_printf_P(PSTR("  Config: IRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #else
    ets_printf_P(PSTR("  Config: DRAM log buffer w/RTC(%u): %u bytes, MAX backtrace: %u levels\r\n"), rtc_status.size, sizeof(union BacktraceLogUnion), pBT->log.max);
    #endif
    #elif ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
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

void backtrace_clear(void) {
    if (pBT) {
        size_t start_wd = offsetof(union BacktraceLogUnion, log.crashCount) / sizeof(uint32_t);
        size_t sz = offsetof(union BacktraceLogUnion, log.pc)
                  - offsetof(union BacktraceLogUnion, log.crashCount)
                  + sizeof(pBT->log.pc[0]) * pBT->log.max;
        // memset(&pBT->log.crashCount, 0, sz);
        memset(&pBT->word32[start_wd], 0, sz);

#if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
        if (rtc_status.size) {
            system_rtc_mem_write(ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER, &pBT->word32[0], rtc_status.size);
        }
#endif
    }
}

void custom_crash_callback(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end) {
    (void)stack_end;
    void *i_pc, *i_sp, *lr, *pc, *sp;

    if (NULL == pBT) {
        return;
    }

    memcpy(&pBT->log.rst_info, rst_info, sizeof(struct rst_info));

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

    ETS_PRINTF2("\n\nBacktrace Crash Reporter - User space:\n ");
    SHOW_PRINTF("\nBacktrace:");
    pBT->log.crashCount++;
    pBT->log.count = 0;

    int repeat;
    do {
        i_pc = pc;
        i_sp = sp;
        ETS_PRINTF2(" %p:%p", pc, sp);
        SHOW_PRINTF(" %p:%p", pc, sp);
        if (pBT->log.count >= pBT->log.max) {
            break;
        }
        pBT->log.pc[pBT->log.count++] = pc;
        repeat = xt_retaddr_callee(i_pc, i_sp, lr, &pc, &sp);
        lr = NULL;
    } while(repeat);
    ETS_PRINTF2("\n\n");
    SHOW_PRINTF("\n\n");

    pBT->log.chksum = do_checksum(pBT);

#if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
    if (rtc_status.size) {
        system_rtc_mem_write(ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER, &pBT->word32[0], rtc_status.size);
    }
#endif
    // backtrace_report(umm_info_safe_printf_P);
}


void backtrace_init(union BacktraceLogUnion *p, size_t max, bool force) {
    if (p) {
        if (force || p->log.chksum != do_checksum(p)) {
            memset(&p->word32[0], 0, sizeof(union BacktraceLogUnion));
            p->log.max = max;
        }
        p->log.bootCounter++;
        p->log.chksum = do_checksum(p);
    }
}

#if ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
static void rtc_check_init(union BacktraceLogUnion *pBT) {
    rtc_status.size = 0;
    rtc_status.max_depth = 0;

    int free_rtc = (192 - ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER) - baseSize32BacktraceLog;
    if (free_rtc >= ESP_DEBUG_BACKTRACELOG_MIN) {
        rtc_status.max_depth = (free_rtc < ESP_DEBUG_BACKTRACELOG_MAX) ? free_rtc : ESP_DEBUG_BACKTRACELOG_MAX;
        rtc_status.size = 4 * (baseSize32BacktraceLog + rtc_status.max_depth);
        system_rtc_mem_read(ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER, &pBT->word32[0], rtc_status.size);
        if (pBT->log.max == rtc_status.max_depth && pBT->log.chksum == do_checksum(pBT)) {
            pBT->log.bootCounter++;
            pBT->log.chksum = do_checksum(pBT);
        } else {
            backtrace_init(pBT, rtc_status.max_depth, true);
        }
        system_rtc_mem_write(ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER, &pBT->word32[0], rtc_status.size);
    }
}
#else
static inline void rtc_check_init(union BacktraceLogUnion *pBT) {
    (void)pBT;
}
#endif


/////////////////////////////////////////////////////////////////////////////
//  For the case of an IRAM log buffer, initialization occurs through:
//    * umm_init_iram() - MMU_IRAM_HEAP
//    * preinit()       - no MMU_IRAM_HEAP
//
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
        pBT = (union BacktraceLogUnion *)(sec_heap + sec_heap_sz);
        #else
        pBT = (union BacktraceLogUnion *)sec_heap);
        umm_init_iram_ex((void *)(sec_heap + IRAM_RESERVE_SZ), sec_heap_sz, true);
        #endif
    } else {
        // Available IRAM is too tight. Give priority to IRAM Heap.
        pBT = NULL;
        if (sec_heap_sz >= (3 * (UMM_OVERHEAD_ADJUST + sizeof(uint32_t)))) {
            umm_init_iram_ex((void *)sec_heap, sec_heap_sz, true);
        }
    }

    backtrace_init(pBT, ESP_DEBUG_BACKTRACELOG_MAX, !is_mem_valid() );
    rtc_check_init(pBT);

#if ESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION
#if defined(NON32XFER_HANDLER) || defined(MMU_IRAM_HEAP)
    // Already handled from user_init()
#else
    install_non32xfer_exception_handler();
#endif
#endif
}

#else // #if defined(MMU_IRAM_HEAP)
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
        pBT = (union BacktraceLogUnion *)iram_buffer;
        backtrace_init(pBT, ESP_DEBUG_BACKTRACELOG_MAX, !is_mem_valid() );
        rtc_check_init(pBT);

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
#endif // #if defined(MMU_IRAM_HEAP)

///////////////////////////////////////////////////////////////////////////////
// For the case of a DRAM log buffer, initialization occurs through preinit()
//
#else // #if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
/*
  Reserve some uninitialized DRAM in the noinit section. We manage the zeroing
  of the memory so we can store data across reboots for the Backtrace Log.
*/
union BacktraceLogUnion _pBT __attribute__((section(".noinit")));

void preinit(void) {
    pBT = &_pBT;
    bool zero = !is_mem_valid();
    backtrace_init(pBT, ESP_DEBUG_BACKTRACELOG_MAX, zero);
    rtc_check_init(pBT);
}
#endif //#if ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER


}; // extern "C" {
#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
