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

// File source: https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/include/esp8266/backtrace.h

#ifndef _BACKTRACE_H
#define _BACKTRACE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if PC pointer locates at code section
 *
 * @param pc  the PC pointer
 *
 * @return 1 if valid or 0 if invalid
 */
int xt_pc_is_valid(const void *pc);

/**
 * @brief Detected recursively and serach the previous PC and SP
 *
 * @param i_pc  the PC address for forward recursive detection
 * @param i_sp  the SP address for forward recursive detection
 * @param i_lr  the LR address, maybe it the previous PC address
 * @param o_pc  the detected previous PC address
 * @param o_pc  the detected previous SP address
 *
 * @return 1 if found or 0 if not found
 */
int xt_retaddr_callee(const void * const i_pc, const void * const i_sp, const void * const i_lr, const void **o_pc, const void **o_sp);

/*
 * @param o_fn  the detected current current function entry
 */
int xt_retaddr_callee_ex(const void * const i_pc, const void * const i_sp, const void * const i_lr, const void **o_pc, const void **o_sp, const void **o_fn);

/**
 * @brief These functions may be used to get information about the callers of a function.
 *
 *        Using this API instead of "__builtin_return_address".
 *
 *        This function returns the return address of the current function, or of one of its callers.
 *        The level argument is number of frames to scan up the call stack. A value of 0 yields the
 *        return address of the current function, a value of 1 yields the return address of the caller
 *        of the current function, and so forth..
 *
 * @param lvl  caller level
 *
 * @return the return address of the current function
 */
const void *xt_return_address(int lvl);



struct BACKTRACE_PC_SP {
  const void *pc;
  const void *sp;
};
/**
 * Similar to xt_return_address, except returns return address and matching
 * stack frame.
 */
struct BACKTRACE_PC_SP xt_return_address_ex(int lvl);

#ifdef __cplusplus
}
#endif

#endif // _BACKTRACE_H
