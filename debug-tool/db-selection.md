# Database Selection Requirements

This document outlines the technical requirements, workloads, and constraints for selecting a replacement database for the E-Trike `debug-tool`. Because the tool handles high-frequency vehicle telemetry (CAN frames), the workload is fundamentally a **Time-Series** use case.

## 1. Write Throughput (Ingestion)

- **Data Rate:** The vehicle has two physical CAN buses (High and Low) running at 500 kbps. Under heavy load, the combined traffic can easily reach **1,000 to 2,500 frames per second (FPS)**.
- **Write Pattern:** 100% Append-only. We never update a historical CAN frame once it is recorded.
- **Payload Size:** Each record is small (around 100-200 bytes). It contains: 
  - Real timestamp
  - Device timestamp (from the ESP32/CANalyst)
  - Bus name (`high` or `low`)
  - CAN ID (e.g., `0x300`)
  - Raw Data payload (up to 8 bytes)
  - Decoded JSON values (e.g., `{ speed: 50 }`)

## 2. Data Volume & Storage Capacity

- **Throughput:** At 2,000 FPS, the tool generates roughly **200 KB to 400 KB per second**.
- **Per Hour:** ~720 MB to 1.4 GB of raw telemetry data per hour of continuous testing.
- **Retention:** The DB needs an automatic Time-To-Live (TTL) or pruning mechanism. If a developer accidentally leaves the tool running overnight, it shouldn't fill up their laptop's hard drive. *(Currently, the SQLite implementation forcefully deletes anything older than the last 50,000 frames using a synchronous cleanup loop).*

## 3. Read & Query Patterns

The UI needs the database to answer three specific types of queries instantly:

1. **The "Latest State" Query (Key-Value):** 
   - *Use Case:* The dashboard needs to know the exact current state of the vehicle at all times.
   - *Query:* "What is the most recent frame for `CAN ID 0x300`?"
   - *Requirement:* This needs to be extremely fast and must not require scanning historical data.
2. **Time-Slicing (Range Queries):** 
   - *Use Case:* For graphing or downloading specific recordings.
   - *Query:* "Give me all frames on the 'high' bus between `Timestamp A` and `Timestamp B`."
3. **No Relational Joins:** The data is completely flat. We do not need complex SQL `JOIN` capabilities.

## 4. Deployment Constraints (Crucial)

- **Local Developer Tool:** This is a debugging utility running on developers' personal laptops (Windows, macOS, Linux). 
- **Footprint:** The database must be easy to spin up locally. It should either be embeddable (like SQLite/DuckDB) or trivial to run as a lightweight background service (e.g., a single Docker container). We cannot require developers to set up a heavy, multi-node enterprise cluster just to test the trike.

---

## Database Architectures to Consider

Given the above requirements, the following database architectures represent the best potential replacements for the current synchronous SQLite driver:

> [!TIP]
> **1. Dedicated Time-Series Databases (e.g., InfluxDB, VictoriaMetrics, QuestDB)**
> - **Pros:** Built specifically for this workload. Incredible append-only write speeds. Built-in time-windowing, TTL retention, and graphing integrations.
> - **Cons:** Requires a separate service to be running (usually via Docker) on the developer's machine.

> [!TIP]
> **2. In-Memory / Key-Value + Append Log (e.g., Redis)**
> - **Pros:** Extremely fast. Redis Streams are perfect for time-series ingestion, and Redis Hashes are perfect for the O(1) "Latest State" queries.
> - **Cons:** Data is lost on restart (unless explicitly configured for disk persistence), and it requires a separate service to run locally.

> [!NOTE]
> **3. Advanced Embedded Databases (e.g., DuckDB or an async SQLite wrapper)**
> - **Pros:** Zero setup for developers (it's just a local file).
> - **Cons:** To make embedded relational databases work at 2,500 FPS without blocking the Node.js event loop, we would need to write custom threading and bulk-batching logic in the backend. DuckDB excels at analytics but might struggle with thousands of tiny single-row inserts per second compared to a true time-series DB.
