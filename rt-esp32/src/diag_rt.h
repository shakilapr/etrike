#pragma once
// RT Phase B diagnostic reporter accessor.
//
// Unlike MTR (where managers expose set_diag()), the RT fault detectors live in
// free functions / FreeRTOS task bodies, so they share one global DiagnosticManager
// reached via rt::diag(). Report-only — raising never changes any reaction path.
#include "shared/diagnostics.h"

namespace rt {

inline etrike::diagnostics::DiagnosticManager& diag() {
    static etrike::diagnostics::DiagnosticManager mgr;
    return mgr;
}

}  // namespace rt
