/*@create-file:build.opt@
// See library BacktraceLog ReadMe.md for details

-DESP_DEBUG_BACKTRACELOG_MAX=32
-fno-optimize-sibling-calls

// When this works you will still see previous crash logs
// after EXT_RST and sleep.
// In this example are skipping the 1st 64 words of user RTC memory
-DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER=128

// -DESP_DEBUG_BACKTRACELOG_SHOW=1
// -DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1
//
// -DESP_DEBUG_BACKTRACE_CPP=1
// -DESP_DEBUG_BACKTRACEIRAMLOG_CPP=1
*/
