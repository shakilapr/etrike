/*
 * timeapi_win32.h — Self-contained shim for MinGW GCC 14+.
 *
 * The FreeRTOS MSVC-MingW port uses <timeapi.h> which is an MSVC
 * header. On MinGW, these types and functions are in <mmsystem.h>,
 * but on newer GCC/MinGW the multimedia timers API is not exposed
 * by default.
 *
 * This header provides the minimal declarations needed.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MMNOSOUND
typedef struct {
    unsigned int wPeriodMin;
    unsigned int wPeriodMax;
} TIMECAPS;
#endif

/* Declared in winmm.dll, linked via -lwinmm */
unsigned int __stdcall timeGetDevCaps(TIMECAPS *ptc, unsigned int cbtc);
unsigned int __stdcall timeBeginPeriod(unsigned int uPeriod);
unsigned int __stdcall timeEndPeriod(unsigned int uPeriod);

#ifdef __cplusplus
}
#endif
