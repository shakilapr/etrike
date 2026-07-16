"""Dictionary / bit-grid field maps for vendor *opaque* codecs.

YAML contracts mark SES/SEB frames as ``layout.kind: opaque`` because of
overlapping multiplexed bytes — the generated catalog therefore has no
``layout.fields``. The custom Python codecs still decode named signals
(``protocol/codecs/python/ses.py``, ``seb.py``).

These maps are **display + metadata only**: same keys as the custom codecs,
positions from can-dictionary / vendor codec, never used to re-implement encode.
"""

from __future__ import annotations

from typing import Any

# Catalog key → list of field dicts (protocol layout shape).
# Keys must match custom codec decode/encode value names.

VENDOR_FIELDS: dict[str, list[dict[str, Any]]] = {
    # ── SES (steer-by-wire) ───────────────────────────────────────────
    "ses:vcu_ses_req": [
        {
            "key": "alignment_enable",
            "byte": 0,
            "bit": 0,
            "bits": 1,
            "comment": "SES angle initial alignment enable (0=off, 1=centering).",
        },
        {
            "key": "control_enable",
            "byte": 0,
            "bit": 1,
            "bits": 1,
            "comment": "Angle-control enable (must be 1 for active steer).",
        },
        {
            "key": "target_angle_raw",
            "byte": 2,
            "bit": 0,
            "bits": 16,
            "signed": True,
            "unit": "raw",
            "min": -32768,
            "max": 32767,
            "comment": "Target steer angle, vendor raw (little-endian i16 @ B2–B3).",
        },
        {
            "key": "target_speed_raw",
            "byte": 4,
            "bit": 0,
            "bits": 8,
            "min": 125,
            "max": 525,
            "unit": "deg/s",
            "comment": "Target angle speed low byte; high bits muxed with enables on B5.",
        },
        {
            "key": "rolling_counter",
            "byte": 5,
            "bit": 4,
            "bits": 4,
            "min": 0,
            "max": 15,
            "comment": "Life-signal rolling counter (0–15). B5 also holds enable bits.",
        },
        {
            "key": "vehicle_speed_raw",
            "byte": 6,
            "bit": 0,
            "bits": 8,
            "min": 0,
            "max": 255,
            "unit": "km/h",
            "comment": "Vehicle speed populated by RT for EPS assist.",
        },
        {
            "key": "checksum",
            "byte": 7,
            "bit": 0,
            "bits": 8,
            "min": 0,
            "max": 255,
            "comment": "XOR(bytes[0..6]) ^ 0xFF.",
        },
    ],
    "ses:ses_status": [
        {
            "key": "angle_aligned",
            "byte": 0,
            "bit": 0,
            "bits": 1,
            "comment": "Center-finding status. 0=finding, 1=aligned/found.",
        },
        {
            "key": "control_mode",
            "byte": 0,
            "bit": 1,
            "bits": 2,
            "min": 0,
            "max": 3,
            "enum": {"0": "Manual", "1": "Automatic", "2": "Reserved", "3": "Reserved"},
            "comment": "Control mode feedback from EPS-C.",
        },
        {
            "key": "error_status",
            "byte": 0,
            "bit": 6,
            "bits": 2,
            "min": 0,
            "max": 3,
            "enum": {
                "0": "Normal",
                "1": "L1_Warning",
                "2": "L2_General",
                "3": "L3_Severe",
            },
            "comment": "Aggregated error level (L3 → RT ESTOP path).",
        },
        {
            "key": "steering_angle_raw",
            "byte": 2,
            "bit": 0,
            "bits": 16,
            "min": 0,
            "max": 65535,
            "unit": "raw",
            "factor": 0.1,
            "offset": -3000,
            "comment": "Steering angle raw u16. Engineering ° = raw×0.1 − 3000 (0° ≈ raw 30000).",
        },
        {
            "key": "target_angle_speed_raw",
            "byte": 4,
            "bit": 0,
            "bits": 16,
            "signed": True,
            "factor": 0.5,
            "min": 0,
            "max": 1480,
            "unit": "deg/s",
            "comment": "Angle-speed feedback i16 @ B4–B5 (overlaps torque on B5).",
        },
        {
            "key": "steering_torque_raw",
            "byte": 5,
            "bit": 0,
            "bits": 8,
            "factor": 0.1,
            "offset": -12.1,
            "min": -12,
            "max": 12,
            "unit": "Nm",
            "comment": "Steering-wheel torque (muxed with speed high byte; 0 Nm ≈ raw 121).",
        },
        {
            "key": "rolling_counter_enabled",
            "byte": 6,
            "bit": 0,
            "bits": 1,
            "comment": "Life-signal enable feedback (1=valid).",
        },
        {
            "key": "checksum_enabled",
            "byte": 6,
            "bit": 1,
            "bits": 1,
            "comment": "Checksum enable feedback (1=valid).",
        },
        {
            "key": "rolling_counter",
            "byte": 6,
            "bit": 4,
            "bits": 4,
            "min": 0,
            "max": 15,
            "comment": "Rolling counter echo 0–15.",
        },
        {
            "key": "checksum",
            "byte": 7,
            "bit": 0,
            "bits": 8,
            "min": 0,
            "max": 255,
            "comment": "Checksum feedback = XOR(bytes[0..6]) ^ 0xFF.",
        },
    ],
    "ses:ses_err_info": [
        {"key": "ecu_under_volt", "byte": 0, "bit": 0, "bits": 1, "comment": "Controller under voltage [L2]."},
        {"key": "ecu_over_volt", "byte": 0, "bit": 1, "bits": 1, "comment": "Controller over voltage [L2]."},
        {"key": "can_com_err", "byte": 0, "bit": 2, "bits": 1, "comment": "CAN communication fault [L1]."},
        {"key": "ecu_temp_err", "byte": 0, "bit": 3, "bits": 1, "comment": "Controller temp fault [L1]."},
        {"key": "domain_sc", "byte": 0, "bit": 4, "bits": 1, "comment": "Domain drive short circuit [L2]."},
        {"key": "domain_v", "byte": 0, "bit": 5, "bits": 1, "comment": "Domain drive voltage fault [L2]."},
        {"key": "domain_t", "byte": 0, "bit": 6, "bits": 1, "comment": "Domain drive temperature fault [L2]."},
        {"key": "temp_sensor", "byte": 0, "bit": 7, "bits": 1, "comment": "Temperature sensor fault."},
        {"key": "angle_p_oc", "byte": 1, "bit": 0, "bits": 1, "comment": "Angle sensor pri open circuit [L3]."},
        {"key": "angle_p_af", "byte": 1, "bit": 1, "bits": 1, "comment": "Angle sensor pri out of range [L3]."},
        {"key": "angle_s_oc", "byte": 1, "bit": 2, "bits": 1, "comment": "Angle sensor sec open circuit [L3]."},
        {"key": "angle_s_af", "byte": 1, "bit": 3, "bits": 1, "comment": "Angle sensor sec out of range [L3]."},
        {"key": "sensor_pow", "byte": 1, "bit": 4, "bits": 1, "comment": "Sensor power fault [L2]."},
        {"key": "alignment_fault", "byte": 1, "bit": 5, "bits": 1, "comment": "Centering / alignment fault [L1]."},
        {"key": "over_angle", "byte": 1, "bit": 6, "bits": 1, "comment": "Over-angle fault [L2]."},
        {"key": "str_mtr_stall", "byte": 1, "bit": 7, "bits": 1, "comment": "Motor stall fault [L1]."},
        {"key": "mtr_curt_fault", "byte": 2, "bit": 0, "bits": 1, "comment": "Motor current fault [L2]."},
        {"key": "sensor_cl", "byte": 2, "bit": 1, "bits": 1, "comment": "Sensor 5V power fault [L2]."},
        {"key": "torq_t1_oc", "byte": 2, "bit": 2, "bits": 1, "comment": "Torque sensor T1 open circuit [L3]."},
        {"key": "torq_t1_af", "byte": 2, "bit": 3, "bits": 1, "comment": "Torque sensor T1 out of range [L3]."},
        {"key": "torq_t2_oc", "byte": 2, "bit": 4, "bits": 1, "comment": "Torque sensor T2 open circuit [L3]."},
        {"key": "torq_t2_af", "byte": 2, "bit": 5, "bits": 1, "comment": "Torque sensor T2 out of range [L3]."},
        {"key": "sent_angle", "byte": 2, "bit": 6, "bits": 1, "comment": "Angle error [L1]."},
        {"key": "str_mtr_idling", "byte": 2, "bit": 7, "bits": 1, "comment": "Motor idling fault [L2]."},
        {"key": "eprom", "byte": 3, "bit": 0, "bits": 1, "comment": "EEPROM fault [L2]."},
        {
            "key": "veh_spd_snapshot",
            "byte": 7,
            "bit": 0,
            "bits": 8,
            "min": 0,
            "max": 255,
            "unit": "km/h",
            "comment": "Vehicle speed snapshot at fault.",
        },
    ],
    "ses:ses_version": [
        {
            "key": "software_raw",
            "byte": 0,
            "bit": 0,
            "bits": 8,
            "factor": 0.01,
            "min": 0,
            "max": 2.55,
            "comment": "Software version raw (e.g. 0x64 → 1.00 if factor 0.01).",
        },
        {
            "key": "hardware_raw",
            "byte": 1,
            "bit": 0,
            "bits": 8,
            "factor": 0.1,
            "min": 0,
            "max": 25.5,
            "comment": "Hardware version raw (e.g. 0x0D → 1.3 if factor 0.1).",
        },
    ],
    "ses:ses_test": [
        {
            "key": "motor_current_raw",
            "byte": 1,
            "bit": 0,
            "bits": 16,
            "signed": True,
            "factor": 0.0078125,
            "unit": "A",
            "min": 0,
            "max": 60,
            "comment": "Motor current telemetry (B1–B2 little-endian i16).",
        },
        {
            "key": "ecu_temperature_raw",
            "byte": 3,
            "bit": 0,
            "bits": 16,
            "factor": 0.5,
            "unit": "degC",
            "comment": "ECU temperature (B3–B4 u16).",
        },
        {
            "key": "supply_voltage_raw",
            "byte": 5,
            "bit": 0,
            "bits": 16,
            "factor": 0.00390625,
            "unit": "V",
            "min": 0,
            "max": 18,
            "comment": "Supply voltage (B5–B6 u16).",
        },
    ],
    # ── SEB (brake-by-wire) ───────────────────────────────────────────
    "seb:vcu_seb_req": [
        {"key": "alignment_enable", "byte": 0, "bit": 0, "bits": 1, "comment": "Calibration / alignment enable."},
        {"key": "control_enable", "byte": 0, "bit": 1, "bits": 1, "comment": "Active brake control enable."},
        {
            "key": "control_mode",
            "byte": 0,
            "bit": 2,
            "bits": 1,
            "enum": {"0": "Stroke", "1": "Pressure"},
            "comment": "0=stroke command, 1=pressure command (byte 3 mux).",
        },
        {"key": "auto_brake", "byte": 0, "bit": 3, "bits": 1, "comment": "Auto-brake / emergency trigger."},
        {
            "key": "stroke_request_raw",
            "byte": 2,
            "bit": 0,
            "bits": 16,
            "min": 0,
            "max": 65535,
            "unit": "raw",
            "comment": "Stroke request (mode 0 uses full u16 @ B2–B3).",
        },
        {
            "key": "pressure_request_raw",
            "byte": 3,
            "bit": 0,
            "bits": 8,
            "min": 0,
            "max": 100,
            "unit": "raw",
            "comment": "Pressure request (mode 1 uses B3; overlaps stroke high byte).",
        },
        {
            "key": "rolling_counter",
            "byte": 6,
            "bit": 4,
            "bits": 4,
            "min": 0,
            "max": 15,
            "comment": "Life-signal counter; enables forced 1 on low bits of B6.",
        },
        {
            "key": "checksum",
            "byte": 7,
            "bit": 0,
            "bits": 8,
            "comment": "XOR(bytes[0..6]) ^ 0xFF.",
        },
    ],
    "seb:seb_status": [
        {"key": "alignment_status", "byte": 0, "bit": 0, "bits": 1, "comment": "Alignment feedback (1=aligned)."},
        {"key": "control_enabled", "byte": 0, "bit": 1, "bits": 1, "comment": "Control enable feedback."},
        {
            "key": "control_mode",
            "byte": 0,
            "bit": 2,
            "bits": 2,
            "min": 0,
            "max": 3,
            "enum": {"0": "Stroke", "1": "Pressure", "2": "Reserved", "3": "Reserved"},
            "comment": "Active control mode feedback.",
        },
        {"key": "auto_brake_status", "byte": 0, "bit": 4, "bits": 1, "comment": "Auto-brake status feedback."},
        {
            "key": "error_status",
            "byte": 0,
            "bit": 6,
            "bits": 2,
            "enum": {
                "0": "Normal",
                "1": "L1_Minor",
                "2": "L2_General",
                "3": "L3_Severe",
            },
            "comment": "Aggregated fault level.",
        },
        {
            "key": "stroke_value_raw",
            "byte": 2,
            "bit": 0,
            "bits": 16,
            "unit": "raw",
            "comment": "Stroke feedback u16 @ B2–B3 (muxed with pressure on B3).",
        },
        {
            "key": "pressure_value_raw",
            "byte": 3,
            "bit": 0,
            "bits": 8,
            "unit": "raw",
            "comment": "Pressure feedback (mode-dependent; overlaps stroke high byte).",
        },
        {
            "key": "angle_value_raw",
            "byte": 5,
            "bit": 0,
            "bits": 16,
            "signed": True,
            "factor": 0.5,
            "unit": "raw",
            "comment": "Angle feedback i16 @ B5–B6 (overlaps security bits on B6).",
        },
        {"key": "rolling_counter_enabled", "byte": 6, "bit": 0, "bits": 1, "comment": "Life-signal enable feedback."},
        {"key": "checksum_enabled", "byte": 6, "bit": 1, "bits": 1, "comment": "Checksum enable feedback."},
        {
            "key": "rolling_counter",
            "byte": 6,
            "bit": 4,
            "bits": 4,
            "min": 0,
            "max": 15,
            "comment": "Rolling counter echo.",
        },
        {
            "key": "checksum",
            "byte": 7,
            "bit": 0,
            "bits": 8,
            "comment": "Checksum feedback.",
        },
    ],
    "seb:seb_err_info": [
        {
            "key": "raw",
            "byte": 0,
            "bit": 0,
            "bits": 64,
            "comment": "Vendor fault bitmap (codec returns opaque raw payload).",
        },
    ],
    "seb:seb_version": [
        {
            "key": "software_raw",
            "byte": 0,
            "bit": 0,
            "bits": 8,
            "comment": "Software version raw byte.",
        },
        {
            "key": "hardware_raw",
            "byte": 1,
            "bit": 0,
            "bits": 8,
            "comment": "Hardware version raw byte.",
        },
    ],
    "seb:seb_test": [
        {
            "key": "motor_current_raw",
            "byte": 1,
            "bit": 0,
            "bits": 16,
            "signed": True,
            "unit": "raw",
            "comment": "Motor current telemetry i16 @ B1–B2.",
        },
        {
            "key": "ecu_temperature_raw",
            "byte": 3,
            "bit": 0,
            "bits": 16,
            "unit": "raw",
            "comment": "ECU temperature u16 @ B3–B4.",
        },
        {
            "key": "supply_voltage_raw",
            "byte": 5,
            "bit": 0,
            "bits": 16,
            "unit": "raw",
            "comment": "Supply voltage u16 @ B5–B6.",
        },
    ],
}


def fields_for_catalog_key(key: str) -> list[dict[str, Any]]:
    """Return vendor field list for a catalog key, or empty list."""
    return list(VENDOR_FIELDS.get(key) or [])


def resolve_layout_fields(catalog_key: str, layout: dict[str, Any] | None) -> list[dict[str, Any]]:
    """Prefer generated layout.fields; fall back to vendor opaque maps."""
    layout = layout or {}
    raw = layout.get("fields") or []
    if isinstance(raw, list) and raw:
        return [f for f in raw if isinstance(f, dict)]
    return fields_for_catalog_key(catalog_key)


def field_meta_map(catalog_key: str, layout: dict[str, Any] | None = None) -> dict[str, dict[str, Any]]:
    """Map field key → field dict for enum/unit/factor lookups."""
    out: dict[str, dict[str, Any]] = {}
    for f in resolve_layout_fields(catalog_key, layout):
        k = str(f.get("key") or f.get("name") or "")
        if k:
            out[k] = f
    return out
