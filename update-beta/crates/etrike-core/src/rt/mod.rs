//! RT-only policy.
//!
//! Phase P4 scaffold: the authority gate (asymmetric ESTOP clear) and the CAN
//! I/O mapping for RT's authority inputs and drive output. Steering and
//! kinematics land here next.

pub mod gate;
pub mod wire;

pub use gate::AuthorityGate;
