# The Atomic Sensor Pipeline

In embedded systems running Real-Time Operating Systems (RTOS), multiple tasks often need to share data. For example, a CAN receive task parses a steering angle, and a control loop task uses that angle to calculate the next movement.

A common pitfall is to protect this shared data using a mutex. However, mutexes introduce **priority inversion** and **blocking**—if a high-priority CAN RX task tries to acquire a mutex held by a low-priority diagnostic task, the CAN task blocks, potentially dropping critical frames.

To avoid this, the E-Trike architecture uses an **Atomic Sensor Pipeline**, heavily relying on lock-free data structures (specifically C++ `std::atomic`).

---

## 1. The Pipeline Concept

The architecture strictly separates tasks into three distinct stages:

1. **Input (Write-Only):** The `can_rx` task receives messages, decodes the payload, and writes the latest value to an atomic variable.
2. **Processing (Read-Write):** The `control` task reads the latest atomic sensor values, runs physics or state machines, and writes setpoints to new atomic variables.
3. **Output (Read-Only):** The `can_tx` task reads the atomic setpoints and sends them onto the bus.

This creates a unidirectional flow of data. 

---

## 2. Lock-Free Atomics

Instead of wrapping a float or integer in a mutex, it is declared as a `std::atomic<T>`.

```cpp
// rt_state.h
extern std::atomic<float> g_steer_angle_actual;
extern std::atomic<float> g_steer_angle_setpoint;
```

When the hardware CPU updates an atomic variable, it guarantees that the read or write happens in a single, indivisible step. You will never read a "half-written" float.

### Memory Order Relaxed

Because we only care about the latest sensor value (and we aren't using the variable to synchronize complex memory operations across cores), we use `std::memory_order_relaxed`.

```cpp
// Inside CAN RX task
g_steer_angle_actual.store(decoded_angle, std::memory_order_relaxed);

// Inside Control task
float current_angle = g_steer_angle_actual.load(std::memory_order_relaxed);
```

This tells the compiler to avoid inserting expensive memory barrier instructions, ensuring maximum performance while still providing atomic updates.

---

## 3. When to use Queues instead of Atomics

While atomics are perfect for continuous state (e.g., "what is the current speed?"), they are terrible for **events** (e.g., "did the user press the ESTOP button?").

If a button is pressed twice rapidly, an atomic boolean flag (`g_estop_flag = true`) might only be read once by the control loop, effectively dropping the second press. 

Therefore, the E-Trike architecture uses a hybrid approach:
- **Atomics** are used for continuous sensor data and setpoints (where only the latest value matters).
- **FreeRTOS Queues** are used for state transitions, safety events, and CAN frame dispatching (where every single event must be processed in order, without dropping).

By combining an atomic sensor pipeline with event queues, the system achieves maximum throughput without priority inversion or dropped safety critical events.
