
#include <Arduino.h>
#include <cont.h>
#include <backtrace.h>
#include <esp8266_undocumented.h>

#define ETS_PRINTF ets_uart_printf

extern "C" {

// Borrowed from mmu_iram.cpp - DEV_DEBUG_PRINT
extern "C" uint8_t rom_i2c_readReg(uint8_t block, uint8_t host_id, uint8_t reg_add);
extern "C" void rom_i2c_writeReg(uint8_t block, uint8_t host_id, uint8_t reg_add, uint8_t data);
extern IRAM_ATTR void dbg_print_prep_set_pll(void) {
#if !defined(F_CRYSTAL)
#define F_CRYSTAL 26000000
#endif
  if (F_CRYSTAL != 40000000) {
    // At Boot ROM(-BIOS) start, it assumes a 40MHz crystal.
    // If it is not, we assume a 26MHz crystal.
    // There is no support for 24MHz crustal at this time.
    if(rom_i2c_readReg(103,4,1) != 136) { // 8: 40MHz, 136: 26MHz
      // Assume 26MHz crystal
      // soc_param0: 0: 40MHz, 1: 26MHz, 2: 24MHz
      // set 80MHz PLL CPU
      rom_i2c_writeReg(103,4,1,136);
      rom_i2c_writeReg(103,4,2,145);
    }
  }
}

extern IRAM_ATTR void dbg_print_prep(bool scroll) {
  // char r103_4_1 = rom_i2c_readReg(103,4,1);
  // char r103_4_2 = rom_i2c_readReg(103,4,2);
  dbg_print_prep_set_pll();
  uart_buff_switch(0);
  if (scroll) ETS_PRINTF("\n\n");
  // ETS_PRINTF("rom_i2c_readReg(103,4,1) == %u\n", r103_4_1);
  // ETS_PRINTF("rom_i2c_readReg(103,4,2) == %u\n", r103_4_2);
}


/*
  Called from a function that needs a backtrace
  Reports directly to the serial console port.

  TODO: Consider makeing this a library function
*/
extern IRAM_ATTR void logCallTrace(void) {
    int repeat;
    void *pc, *sp, *fn;
    __asm__ __volatile__(
      "mov  %[sp], a1\n\t"
      "movi %[pc], .\n\t"
      : [pc]"=r"(pc), [sp]"=r"(sp)
      :
      : "memory");

    // Since we are only called from SPIRead() below, this line is  redundant;
    // however, if that is changed, it may be needed for printing to work.
    // dbg_print_prep(true);

    // Step back one call level into caller.
    // We want our caller to appear in the log 1st, not this function.
    xt_retaddr_callee_ex(pc, sp, NULL, &pc, &sp, &fn);
    ETS_PRINTF("\nBacktrace:");
    do {
        ETS_PRINTF(" %p:%p", pc, sp);
        repeat = xt_retaddr_callee_ex(pc, sp, NULL, &pc, &sp, &fn);
        if (fn) ETS_PRINTF(":<%p>", fn);  // estimated start of the function
        // Sometimes there will be a few register preload instructions before
        // the stack frame setup instructions.
    } while(repeat);
#if 1
    // This block cannot be used if we are called before user_init()/cont_init()
    // As is the case in this example. If you have a special situation that
    // needs this, you could set `g_pcont->pc_suspend=NULL;` from the
    // app_entry_redefinable() your sketch uses. Then, it should work for cases
    // before and after user_init() is called.
    // Further down in this example, you will see a coding trick we use to deal
    // with this issue.
    if (g_pcont->pc_suspend) {
        // Looks like we were called while the Sketch was yielding.
        // Finish traceback on the cont (loop_wrapper) stack.
        ETS_PRINTF(" 0:0");  // mark transistion
        // Extract resume context to traceback - see cont_continue in cont.S
        sp = (void*)((uintptr_t)g_pcont->sp_suspend + 24u);   // a1
        pc = *(void**)((uintptr_t)g_pcont->sp_suspend + 16u); // a0
        do {
            ETS_PRINTF(" %p:%p", pc, sp);
            repeat = xt_retaddr_callee_ex(pc, sp, NULL, &pc, &sp, &fn);
            if (fn) { ETS_PRINTF(":<%p>", fn); }
        } while(repeat);
    }
#endif
    ETS_PRINTF("\n\n");
}

///////////////////////////////////////////////////////////////////////////////
// wrapper around Boot ROM `SPIRead`
//
#ifndef ROM_SPIRead
#define ROM_SPIRead         0x40004b1cU
#endif
typedef int (*fp_SPIRead_t)(uint32_t addr, void *dest, size_t size);
#define real_SPIRead ((fp_SPIRead_t)ROM_SPIRead)
constexpr int kPrintBurstLimit = 7;
static int debug_print_burst = kPrintBurstLimit; // number of SPIRead() calls to log

extern int IRAM_ATTR SPIRead(uint32_t addr, void *dest, size_t size) {
    /*
       The very 1st read that goes by is to get the config flash size from
       image header. The NONOS SDK will update flashchip->chip_size. Then,
       additional reads are performed.
    */
    int err = real_SPIRead(addr, dest, size);

    if (debug_print_burst) {
        // Make printing work before SDK init is complete.
        // On our first print, scroll down past the Boot ROM's initial garbage
        // printing from having the wrong XTAL calibration.
        dbg_print_prep(kPrintBurstLimit == debug_print_burst);

        ETS_PRINTF("\nLog: %d = SPIRead(0x%08x, 0x%08x, %u)\n", err, addr, (uint32_t)dest, size);
        if (kPrintBurstLimit == debug_print_burst && dest) {
            // The 1st word read will contain the SPI Mode and SPI Flash Info
            ETS_PRINTF("  SPI Mode:       0x%02X\n", ((*(uint32_t*)dest) >> 16) & 0xff);
            ETS_PRINTF("  SPI Flash Info: 0x%02X\n", ((*(uint32_t*)dest) >> 24) & 0xff);
            g_pcont->pc_suspend = NULL;   // code trick to allow testing g_pcont->pc_suspend before user_init()
        }
        logCallTrace();
        debug_print_burst--;
    }

    return err;
}

};
