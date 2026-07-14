#pragma once
// Kinematics resolver type alias — Compile-Time Policy Pattern.
//
// This is the ONLY file that contains an #if on ETRIKE_RT_KINEMATICS_RESOLVER.
// All other files (main.cpp, tests) use rt::ActiveResolver and are completely
// clean of branching logic.
//
// ETRIKE_RT_KINEMATICS_RESOLVER = 0  →  rt::ActiveResolver = rt::PhysicsModel
// ETRIKE_RT_KINEMATICS_RESOLVER = 1  →  rt::ActiveResolver = rt::DirectResolver
//
// docs/rt-sys-feature-configuration-and-test-plan.md §"Kinematics resolver strategy"

#include "build_config.h"  // validates ETRIKE_RT_KINEMATICS_RESOLVER

#if ETRIKE_RT_KINEMATICS_RESOLVER == 1
#  include "direct_resolver.h"
   namespace rt { using ActiveResolver = DirectResolver; }
#else
#  include "physics_model.h"
   namespace rt { using ActiveResolver = PhysicsModel; }
#endif
