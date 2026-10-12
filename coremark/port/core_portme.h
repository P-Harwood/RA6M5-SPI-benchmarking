/*
Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Original Author: Shay Gal-on
*/

/* Topic : Description
        This file contains configuration constants required to execute on
   different platforms
*/

/* Port : Zephyr RTOS on Renesas RA6M5 (Cortex-M33)
        Derived from the "simple" port. The configuration constants below are
   the sample port's, with the values changed where the platform differs: there
   is no time.h and no stdio, so the timer is the kernel tick counter and
   ee_printf goes to the Zephyr console. core_*.c and coremark.h are the
   unmodified EEMBC sources; only this file and <core_portme.c> are platform
   specific.
*/
#ifndef CORE_PORTME_H
#define CORE_PORTME_H
/************************/
/* Data types and settings */
/************************/
/* stddef.h for ee_size_t below; the sample port picks size_t up from stdio.h,
   which this platform does not use. */
#include <stddef.h>

/* Configuration : HAS_FLOAT
        Define to 1 if the platform supports floating point.

        The Cortex-M33 core on this part has an FPU, and
   CONFIG_CBPRINTF_FP_SUPPORT=y in prj.conf lets the console print %f, so the
   report is emitted with seconds and iterations/sec rather than integers.
*/
#ifndef HAS_FLOAT
#define HAS_FLOAT 1
#endif
/* Configuration : HAS_TIME_H
        Define to 1 if platform has the time.h header file,
        and implementation of functions thereof.

        Set to 0: timing is done with the Zephyr kernel tick counter, see
   <core_portme.c>.
*/
#ifndef HAS_TIME_H
#define HAS_TIME_H 0
#endif
/* Configuration : USE_CLOCK
        Define to 1 if platform has the time.h header file,
        and implementation of functions thereof.

        Set to 0: there is no clock() on this platform.
*/
#ifndef USE_CLOCK
#define USE_CLOCK 0
#endif
/* Configuration : HAS_STDIO
        Define to 1 if the platform has stdio.h.
*/
#ifndef HAS_STDIO
#define HAS_STDIO 0
#endif
/* Configuration : HAS_PRINTF
        Define to 1 if the platform has stdio.h and implements the printf
   function.

        Set to 0: <coremark.h> then leaves ee_printf as a function, which
   <core_portme.c> implements on top of the Zephyr console.
*/
#ifndef HAS_PRINTF
#define HAS_PRINTF 0
#endif
int ee_printf(const char *fmt, ...);

/* Configuration : CORE_TICKS
        Define type of return from the timing functions.
 */
typedef unsigned int CORE_TICKS;

/* Configuration : EE_TICKS_PER_SEC
        Ticks per second of the timer used by <start_time> and <stop_time>.

        The sample port derives this in <core_portme.c> from CLOCKS_PER_SEC. It
   is declared here instead because the application also reports it at start-up.
   The kernel runs with a periodic tick (CONFIG_TICKLESS_KERNEL=n in prj.conf),
   so the clock is announced every tick whatever the application does with
   timeouts.
*/
#define EE_TICKS_PER_SEC CONFIG_SYS_CLOCK_TICKS_PER_SEC

/* Configuration : ITERATIONS
        Fixed number of iterations per execution.

        The sample port takes this from the command line (seed 4) and lets
   <core_main.c> auto-calibrate when it is 0. Here it is fixed at compile time
   so that every execution is the same amount of work and the run time is
   purely a function of the CPU share CoreMark received. Override per build with
   -DEXTRA_CFLAGS=-DITERATIONS=<n>. CoreMark only calls a result valid if one
   execution takes >= 10 secs.
*/
#ifndef ITERATIONS
#define ITERATIONS 10000
#endif
#if ITERATIONS <= 0
#error "ITERATIONS must be > 0 (auto-calibration is not supported by this port)"
#endif

/* Definitions : COMPILER_VERSION, COMPILER_FLAGS, MEM_LOCATION
        Initialize these strings per platform
*/
#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC" __VERSION__
#else
#define COMPILER_VERSION "Please put compiler version here (e.g. gcc 4.1)"
#endif
#endif
#ifndef COMPILER_FLAGS
#if defined(CONFIG_SPEED_OPTIMIZATIONS)
#define COMPILER_FLAGS "-O2 (CONFIG_SPEED_OPTIMIZATIONS)"
#elif defined(CONFIG_SIZE_OPTIMIZATIONS)
#define COMPILER_FLAGS "-Os (CONFIG_SIZE_OPTIMIZATIONS)"
#elif defined(CONFIG_NO_OPTIMIZATIONS)
#define COMPILER_FLAGS "-O0 (CONFIG_NO_OPTIMIZATIONS)"
#else
#define COMPILER_FLAGS "zephyr default"
#endif
#endif
#ifndef MEM_LOCATION
#define MEM_LOCATION "STATIC"
#endif

/* Data Types :
        To avoid compiler issues, define the data types that need ot be used for
   8b, 16b and 32b in <core_portme.h>.

        *Imprtant* :
        ee_ptr_int needs to be the data type used to hold pointers, otherwise
   coremark may fail!!!
*/
typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed int     ee_s32;
typedef double         ee_f32;
typedef unsigned char  ee_u8;
typedef unsigned int   ee_u32;
typedef ee_u32         ee_ptr_int;
typedef size_t         ee_size_t;
/* align_mem :
        This macro is used to align an offset to point to a 32b value. It is
   used in the Matrix algorithm to initialize the input memory blocks.
*/
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

/* Configuration : SEED_METHOD
        Defines method to get seed values that cannot be computed at compile
   time.

        Valid values :
        SEED_ARG - from command line.
        SEED_FUNC - from a system function.
        SEED_VOLATILE - from volatile variables.
*/
#ifndef SEED_METHOD
#define SEED_METHOD SEED_VOLATILE
#endif

/* Configuration : MEM_METHOD
        Defines method to get a block of memry.

        Valid values :
        MEM_MALLOC - for platforms that implement malloc and have malloc.h.
        MEM_STATIC - to use a static memory array.
        MEM_STACK - to allocate the data block on the stack (NYI).

        MEM_STATIC keeps the 2K working set off the CoreMark thread's stack.
*/
#ifndef MEM_METHOD
#define MEM_METHOD MEM_STATIC
#endif

/* Configuration : MULTITHREAD
        Define for parallel execution

        Valid values :
        1 - only one context (default).
        N>1 - will execute N copies in parallel.

        Note :
        If this flag is defined to more then 1, an implementation for launching
   parallel contexts must be defined.

        Two sample implementations are provided. Use <USE_PTHREAD> or <USE_FORK>
   to enable them.

        It is valid to have a different implementation of <core_start_parallel>
   and <core_end_parallel> in <core_portme.c>, to fit a particular architecture.

        One context here: the benchmark runs in whichever Zephyr thread calls
   <coremark_main>, and MEM_STATIC forbids more than one anyway.
*/
#ifndef MULTITHREAD
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0
#endif

/* Configuration : MAIN_HAS_NOARGC
        Needed if platform does not support getting arguments to main.

        Valid values :
        0 - argc/argv to main is supported
        1 - argc/argv to main is not supported

        Note :
        This flag only matters if MULTITHREAD has been defined to a value
   greater then 1.
*/
#ifndef MAIN_HAS_NOARGC
#define MAIN_HAS_NOARGC 1
#endif

/* Configuration : MAIN_HAS_NORETURN
        Needed if platform does not support returning a value from main.

        Valid values :
        0 - main returns an int, and return value will be 0.
        1 - platform does not support returning a value from main
*/
#ifndef MAIN_HAS_NORETURN
#define MAIN_HAS_NORETURN 0
#endif

/* Function : coremark_main
        The benchmark entry point as seen by the application.

        <core_main.c> defines main(). CMakeLists.txt compiles that one file with
   -Dmain=coremark_main so that the application keeps its own Zephyr main(). One
   call is one complete CoreMark execution.
*/
int coremark_main(void);

/* Variable : default_num_contexts
        Not used for this simple port, must contain the value 1.
*/
extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S
{
    ee_u8 portable_id;
} core_portable;

/* target specific init/fini */
void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) \
    && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

#endif /* CORE_PORTME_H */
