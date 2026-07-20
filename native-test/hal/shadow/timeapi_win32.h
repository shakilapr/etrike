/*
 * timeapi_win32.h — Self-contained shim for MinGW GCC 14+.
 *
 * The FreeRTOS MSVC-MingW port uses <timeapi.h> which is an MSVC
 * header. On MinGW, these types and functions are in <mmsystem.h>,
 * but on newer GCC/MinGW the multimedia timers API is not exposed
 * by default.
 *
 * Prefer MinGW's own declarations when they are available.  Including
 * mmsystem.h here is safe before the FreeRTOS port includes windows.h and
 * avoids redeclaring TIMECAPS/timeGetDevCaps on current MinGW toolchains.
 */
#pragma once

#include <windows.h>
#include <mmsystem.h>
