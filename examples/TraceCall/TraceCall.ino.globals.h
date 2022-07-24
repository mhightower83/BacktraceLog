/*@create-file:build.opt@
// See library BacktraceLog ReadMe.md for details

// Maximum backtrace addresses to save
-DESP_DEBUG_BACKTRACELOG_MAX=64

// Print backtrace after postmortem
// -DESP_DEBUG_BACKTRACELOG_SHOW=1

-fno-optimize-sibling-calls

// Use IRAM log buffer instead of DRAM
// -DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1

// Backup log buffer to User RTC memory
// -DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96

-DUSE_WIFI=1
// -DUSE_TELNET=1
*/

/*@create-file:build.opt:debug@

-fno-optimize-sibling-calls

// Maximum backtrace addresses to save
-DESP_DEBUG_BACKTRACELOG_MAX=64

// Print backtrace after postmortem
-DESP_DEBUG_BACKTRACELOG_SHOW=1

// Use IRAM log buffer instead of DRAM
// -DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1

// Backup log buffer to User RTC memory
// -DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96

-DUSE_WIFI=1
// -DUSE_TELNET=1
*/


#ifndef SIMPLEDEMO_INO_GLOBALS_H
#define SIMPLEDEMO_INO_GLOBALS_H
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
