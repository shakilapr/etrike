/*
 * timeapi.h — MinGW shim for MSVC Windows SDK header.
 *
 * The FreeRTOS MSVC-MingW port includes <timeapi.h> for
 * timeGetTime / timeBeginPeriod / timeEndPeriod.
 * On MinGW/GCC 14+, the legacy multimedia APIs are not
 * exposed by default. This shim provides the minimal
 * declarations directly.
 */
#pragma once
#include "timeapi_win32.h"
