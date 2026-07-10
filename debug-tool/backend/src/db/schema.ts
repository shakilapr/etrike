export const SQLITE_SCHEMA = `
CREATE TABLE IF NOT EXISTS can_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    ts_us       INTEGER NOT NULL,
    seq         INTEGER NOT NULL,
    ts_device   INTEGER NOT NULL,
    bus         TEXT NOT NULL CHECK(bus IN ('high','low')),
    can_id      TEXT NOT NULL,
    can_name    TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    decoded     TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_frames_bus_id_ts ON can_frames(bus, can_id, ts_us);
CREATE INDEX IF NOT EXISTS idx_frames_ts_us ON can_frames(ts_us);

CREATE TABLE IF NOT EXISTS injected_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    ts_us       INTEGER NOT NULL,
    seq         INTEGER NOT NULL,
    bus         TEXT NOT NULL CHECK(bus IN ('high','low')),
    can_id      TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    status      TEXT,
    correlation_id TEXT
);
CREATE INDEX IF NOT EXISTS idx_injected_ts_us ON injected_frames(ts_us);
CREATE INDEX IF NOT EXISTS idx_injected_correlation ON injected_frames(correlation_id);

CREATE TABLE IF NOT EXISTS recordings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    label       TEXT,
    started_at  REAL NOT NULL,
    stopped_at  REAL,
    frame_count INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_recordings_stopped ON recordings(stopped_at);

CREATE TABLE IF NOT EXISTS recording_frames (
    recording_id INTEGER NOT NULL REFERENCES recordings(id),
    frame_id     INTEGER NOT NULL REFERENCES can_frames(id),
    PRIMARY KEY (recording_id, frame_id)
);
CREATE INDEX IF NOT EXISTS idx_recording_frames_frame ON recording_frames(frame_id);

CREATE TABLE IF NOT EXISTS runtime_state (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL
);
`;
