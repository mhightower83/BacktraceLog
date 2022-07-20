/*@create-file:build.opt@
// See library BacktraceLog ReadMe.md for details

-DESP_DEBUG_BACKTRACELOG_MAX=32
-fno-optimize-sibling-calls
-fno-omit-frame-pointer

// When this works you will still see previous crash logs
// after EXT_RST and sleep.
// User RTC memory starts at offset 64. In this example are skipping the 1st 32
// words of user RTC memory to stay clear of eboot usage.
//-DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96

// -DESP_DEBUG_BACKTRACELOG_SHOW=1
-DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1
//
// -DESP_DEBUG_BACKTRACE_CPP=1
// -DESP_DEBUG_BACKTRACEIRAMLOG_CPP=1

-DUSE_WIFI=1
*/

/*@create-file:build.opt:debug@
-fno-optimize-sibling-calls
-fno-omit-frame-pointer

-DESP_DEBUG_BACKTRACELOG_MAX=32
-DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96
// -DESP_DEBUG_BACKTRACELOG_SHOW=1
// -DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1
// -DESP_DEBUG_BACKTRACE_CPP=1
// -DESP_DEBUG_BACKTRACEIRAMLOG_CPP=1
// -DUSE_WIFI=1
*/
