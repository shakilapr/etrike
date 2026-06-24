/*
 * timeapi.h — MinGW shim for MSVC Windows SDK header.
 *
 * The FreeRTOS MSVC-MingW port includes <timeapi.h> for
 * timeGetTime / timeBeginPeriod / timeEndPeriod.
 * On MinGW, these are declared in <mmsystem.h>.
 */
#pragma once
#include <mmsystem.h>
