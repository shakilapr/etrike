export const SQLITE_SCHEMA = `
CREATE TABLE can_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    ts_device   REAL NOT NULL,
    can_id      TEXT NOT NULL,
    can_name    TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    decoded     TEXT NOT NULL
);
CREATE INDEX idx_frames_id_ts ON can_frames(can_id, ts_real);
CREATE INDEX idx_frames_ts    ON can_frames(ts_real);

CREATE TABLE injected_frames (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL NOT NULL,
    can_id      TEXT NOT NULL,
    dlc         INTEGER NOT NULL,
    data        BLOB NOT NULL,
    request_id  TEXT NOT NULL,
    response    TEXT
);

CREATE TABLE recordings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    label       TEXT,
    started_at  REAL NOT NULL,
    stopped_at  REAL,
    frame_count INTEGER DEFAULT 0
);

CREATE TABLE recording_frames (
    recording_id INTEGER NOT NULL REFERENCES recordings(id),
    frame_id     INTEGER NOT NULL REFERENCES can_frames(id),
    PRIMARY KEY (recording_id, frame_id)
);
`;
