/*@create-file:build.opt@
// See library BacktraceLog ReadMe.md for details

-fno-optimize-sibling-calls

// For this example, use IRAM for backtrace.cpp. For logging before SDK starts.
// The APIs must be available at all times.
-DBACKTRACE_IN_IRAM=1

*/

/*@create-file:build.opt:debug@

-fno-optimize-sibling-calls

// For this example, use IRAM for backtrace.cpp. For logging before SDK starts.
// The APIs must be available at all times.
-DBACKTRACE_IN_IRAM=1

*/


#ifndef TRACECALLALT_INO_GLOBALS_H
#define TRACECALLALT_INO_GLOBALS_H
#if !defined(__ASSEMBLER__)
#ifdef DEBUG_ESP_PORT
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...) __asm__ __volatile__("" ::: "a0", "memory")
#else
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...)
#endif
#endif
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
