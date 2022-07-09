#ifndef HWDTSTACKDUMP_INO_GLOBALS_H
#define HWDTSTACKDUMP_INO_GLOBALS_H

/*@create-file:build.opt@
//
// -finstrument-functions
// -finstrument-functions-exclude-function-list=app_entry,mmu_wrap_irom_fn,stack_thunk_get_,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
// -finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k,backtrace,StackThunk
// 
// -fno-optimize-sibling-calls
// -fno-omit-frame-pointer

*/

/*@create-file:build.opt:debug@
// For this block to work, you must have
// `mkbuildoptglobals.extra_flags={build.debug_port}` in `platform.local.txt`
// Or move contents to the block with the signature "@create-file:build.opt@"

// compiler command-line options to accommodate tracking last call before HWDT
-finstrument-functions
-finstrument-functions-exclude-function-list=app_entry,mmu_wrap_irom_fn,stack_thunk_get_,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
-finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k,backtrace,StackThunk


// By removing "sibling and tail recursive calls" optimization, it can remove
// some of the gaps in the traceable stack content. Preserves stack frames
// created at each level as you call down to the next. Thus leaving a complete
// trail to follow back up after a crash.
-fno-optimize-sibling-calls


// Adds a pointer at the end of the stack frame highest (last) address -8
// just before the return address at -12. Not necessary for this library.
// It may help to get your bearings when looking at a stack dump.
// The stack dump will annotate the line where it occurs with a `<` mark.
// If you are looking for specific data in the stack, you may find this option
// useful.
-fno-omit-frame-pointer


// Options for HWDT Stack Dump

// Alter the UART serial speed used for printing the Hardware WDT reset stack
// dump. Without this option on an HWDT reset, the existing default speed of
// 115200 bps will be used. If you are using this default speed, you can skip
// this option. Note this option only changes the speed while the stack dump is
// printing. Prior settings are restored.
// -DDEBUG_ESP_HWDT_UART_SPEED=19200
// -DDEBUG_ESP_HWDT_UART_SPEED=74880
// -DDEBUG_ESP_HWDT_UART_SPEED=115200
// -DDEBUG_ESP_HWDT_UART_SPEED=230400

// HWDT Stack Dump defaults to print a simple introduction to let you know the
// tool is active and in the build. At power-on, this may not be viewable on
// some devices. Use the DEBUG_ESP_HWDT_UART_SPEED option above to improve.
// Or uncomment line below to turn off greeting
// -DDEBUG_ESP_HWDT_PRINT_GREETING=0

// Demos
// -DDEMO_THUNK=1
// -DDEMO_NOEXTRA4K=1
// -DDEMO_WIFI=1
*/

#if defined(__cplusplus)
// Defines kept private to .cpp modules
//#pragma message("__cplusplus has been seen")
#endif
#if !defined(__cplusplus) && !defined(__ASSEMBLER__)
// Defines kept private to .c modules
#endif
#if defined(__ASSEMBLER__)
// Defines kept private to assembler modules
#endif
#endif
