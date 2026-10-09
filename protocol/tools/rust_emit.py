"""Rust codec emitter for the E-Trike canonical wire contract.

Consumes the same validated model as ``protocol.tools.protocol`` and renders a
``no_std`` Rust module with typed messages, pack/unpack codecs, routing and
TX-ownership metadata, and an embedded conformance test module.

Kept self-contained (its own small helpers) so it can be invoked from the
generator without a circular import.
"""

from __future__ import annotations

import re

# ── shared helpers (mirrors of protocol.tools.protocol) ────────────────


def parse_id(value: object) -> int:
    if isinstance(value, str):
        return int(value, 0)
    return int(value)


def field_limits(field: dict) -> tuple[float, float]:
    bits = field["bits"]
    if field.get("signed"):
        defaults = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1)
    else:
        defaults = (0, (1 << bits) - 1)
    factor, offset = field.get("factor", 1), field.get("offset", 0)
    return (
        field.get("min", defaults[0] * factor + offset),
        field.get("max", defaults[1] * factor + offset),
    )


def identifier(value: str) -> str:
    return re.sub(r"_+", "_", re.sub(r"[^A-Za-z0-9_]", "_", value)).strip("_").lower()


def pascal(value: str) -> str:
    return "".join(
        part[:1].upper() + part[1:].lower()
        for part in re.split(r"[^A-Za-z0-9]+", value)
        if part
    )


_RUST_KEYWORDS = {
    "as", "async", "await", "box", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let",
    "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self",
    "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where",
    "while", "yield",
}


def rust_ident(value: str) -> str:
    name = identifier(value)
    if not name:
        return "field"
    if name in _RUST_KEYWORDS:
        return name + "_"
    return name


# ── type mapping ───────────────────────────────────────────────────────


def rust_integer_type(field: dict) -> str:
    bits = field["bits"]
    width = 8 if bits <= 8 else 16 if bits <= 16 else 32 if bits <= 32 else 64
    return f"{'i' if field.get('signed') else 'u'}{width}"


def rust_type(field: dict) -> str:
    if "factor" in field or "offset" in field:
        return "f64"
    minimum, maximum = field_limits(field)
    if field["bits"] == 1 or (field["bits"] <= 8 and minimum == 0 and maximum == 1):
        return "bool"
    return rust_integer_type(field)


def rust_number(value: object) -> str:
    if isinstance(value, float):
        text = repr(float(value))
        if not any(character in text for character in ".eE"):
            text += ".0"
        return text
    return str(int(value))


def rust_literal(field: dict, value: object) -> str:
    """Render a literal of the field's Rust type (bool fields need true/false)."""
    if rust_type(field) == "bool":
        return "true" if int(value) != 0 else "false"
    return rust_number(value)


# ── checks ─────────────────────────────────────────────────────────────


def rust_checks(expression: str, field: dict, *, unpack: bool) -> list[str]:
    def error(status: str) -> str:
        return f"return Err(CodecStatus::{status});" if unpack else f"return CodecStatus::{status};"

    lines: list[str] = []
    rtype = rust_type(field)
    minimum, maximum = field_limits(field)
    if rtype == "bool":
        # A bool (1-bit, or bits<=8 with range 0..1) needs no range or enum check.
        return lines
    if rtype == "f64":
        conditions = [
            f"!{expression}.is_finite()",
            f"{expression} < {rust_number(float(minimum))}",
            f"{expression} > {rust_number(float(maximum))}",
        ]
        lines.append(f"        if {' || '.join(conditions)} {{ {error('ValueOutOfRange')} }}")
    else:
        raw_minimum = -(1 << (field["bits"] - 1)) if field.get("signed") else 0
        raw_maximum = (1 << (field["bits"] - (1 if field.get("signed") else 0))) - 1
        conditions = []
        if minimum > raw_minimum:
            conditions.append(f"{expression} < {rust_number(minimum)}")
        if maximum < raw_maximum:
            conditions.append(f"{expression} > {rust_number(maximum)}")
        if conditions:
            lines.append(f"        if {' || '.join(conditions)} {{ {error('ValueOutOfRange')} }}")

    enum_dict = field.get("enum", {})
    if enum_dict:
        bits = field["bits"]
        factor, offset = field.get("factor", 1), field.get("offset", 0)
        if field.get("signed"):
            raw_minimum, raw_maximum = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
        else:
            raw_minimum, raw_maximum = 0, (1 << bits) - 1
        min_val = field.get("min", raw_minimum * factor + offset)
        max_val = field.get("max", raw_maximum * factor + offset)
        raw_min_val = int(round((min_val - offset) / factor))
        raw_max_val = int(round((max_val - offset) / factor))
        if len(enum_dict) >= (raw_max_val - raw_min_val + 1):
            values = sorted(int(value) for value in enum_dict)
            conditions = " && ".join(f"{expression} != {rust_number(value)}" for value in values)
            lines.append(f"        if {conditions} {{ {error('InvalidEnum')} }}")
    return lines


def raw_expression(field: dict, source: str) -> str:
    rtype = rust_type(field)
    if rtype == "bool":
        return f"(if {source} {{ 1u64 }} else {{ 0u64 }})"
    if rtype == "f64":
        factor, offset = field.get("factor", 1), field.get("offset", 0)
        return (
            f"(detail::round(({source} - {rust_number(float(offset))}) / "
            f"{rust_number(float(factor))}) as i64 as u64)"
        )
    if field.get("signed"):
        return f"({source} as i64 as u64)"
    return f"({source} as u64)"


def decode_expression(field: dict, raw: str) -> str:
    rtype = rust_type(field)
    bits = field["bits"]
    if rtype == "bool":
        return f"({raw} != 0)"
    if rtype == "f64":
        factor, offset = field.get("factor", 1), field.get("offset", 0)
        scaled = (
            f"detail::sign_extend({raw}, {bits}u8) as f64"
            if field.get("signed")
            else f"{raw} as f64"
        )
        return f"({scaled} * {rust_number(float(factor))} + {rust_number(float(offset))})"
    if field.get("signed"):
        return f"detail::sign_extend({raw}, {bits}u8) as {rtype}"
    return f"{raw} as {rtype}"


# ── message rendering ──────────────────────────────────────────────────


def render_message(message: dict) -> list[str]:
    type_name = pascal(message["name"])
    fields = message["layout"]["fields"]
    little = message["byte_order"] == "little"
    instances = sorted(
        message["instances"], key=lambda value: (value["bus"], parse_id(value["id"]))
    )
    primary = instances[0]

    lines = ["#[derive(Debug, Clone, Copy, PartialEq)]", f"pub struct {type_name} {{"]
    for field in fields:
        lines.append(f"    pub {rust_ident(field['key'])}: {rust_type(field)},")
    if not fields:
        lines.append("    pub _reserved: (),")
    lines.append("}")
    lines.append("")
    lines.append(f"impl {type_name} {{")
    lines.append(f'    pub const KEY: &\'static str = "{message["owner"]}:{message["key"]}";')
    lines.append(f"    pub const ID: u32 = 0x{parse_id(primary['id']):X};")
    lines.append(f"    pub const DLC: usize = {message['dlc']};")
    lines.append(f"    pub const CYCLE_MS: u32 = {primary['cycle_ms']};")
    lines.append(f"    pub const EXTENDED: bool = {str(primary['frame_format'] == 'extended').lower()};")
    for instance in instances:
        iname = pascal(instance["bus"]).upper()
        lines.append(f"    pub const {iname}_ID: u32 = 0x{parse_id(instance['id']):X};")
        lines.append(f"    pub const {iname}_CYCLE_MS: u32 = {instance['cycle_ms']};")
    for field in fields:
        member = pascal(field["key"]).upper()
        rtype = rust_type(field)
        if rtype == "bool":
            continue
        if "constant" in field:
            lines.append(f"    pub const {member}: {rtype} = {rust_number(field['constant'])};")
        for raw_value, label in sorted(field.get("enum", {}).items(), key=lambda item: int(item[0])):
            variant = f"{member}_{pascal(label).upper()}"
            lines.append(f"    pub const {variant}: {rtype} = {rust_number(raw_value)};")
    lines.append("")

    # pack
    lines.append("    pub fn pack(&self, out: &mut [u8]) -> CodecStatus {")
    lines.append("        if out.len() != Self::DLC { return CodecStatus::UnexpectedLength; }")
    for field in fields:
        member = rust_ident(field["key"])
        if "constant" in field:
            lines.append(
                f"        if self.{member} != {rust_literal(field, field['constant'])} "
                "{ return CodecStatus::ConstantMismatch; }"
            )
        lines += rust_checks(f"self.{member}", field, unpack=False)
    lines.append("        let mut payload = [0u8; 8];")
    for field in fields:
        member = rust_ident(field["key"])
        raw = raw_expression(field, f"self.{member}")
        lines.append(
            f"        detail::insert(&mut payload, {field['byte']}, {field['bit']}, "
            f"{field['bits']}, {str(little).lower()}, {raw});"
        )
    lines.append("        out.copy_from_slice(&payload[..Self::DLC]);")
    lines.append("        CodecStatus::Ok")
    lines.append("    }")
    lines.append("")

    # unpack
    lines.append("    pub fn unpack(data: &[u8]) -> Result<Self, CodecStatus> {")
    lines.append("        if data.len() != Self::DLC { return Err(CodecStatus::UnexpectedLength); }")
    defaults = []
    for field in fields:
        member = rust_ident(field["key"])
        if "constant" in field:
            defaults.append(f"{member}: {rust_literal(field, field['constant'])}")
        elif rust_type(field) == "bool":
            defaults.append(f"{member}: false")
        elif rust_type(field) == "f64":
            defaults.append(f"{member}: 0.0")
        else:
            defaults.append(f"{member}: 0")
    if not fields:
        defaults.append("_reserved: ()")
    lines.append(f"        let mut value = Self {{ {', '.join(defaults)} }};")
    for field in fields:
        member = rust_ident(field["key"])
        lines.append(
            f"        let raw_{member} = detail::extract(data, {field['byte']}, "
            f"{field['bit']}, {field['bits']}, {str(little).lower()});"
        )
        if "constant" in field:
            lines.append(
                f"        if raw_{member} != {int(field['constant'])}u64 "
                "{ return Err(CodecStatus::ConstantMismatch); }"
            )
        if rust_type(field) == "bool" and field["bits"] > 1:
            lines.append(f"        if raw_{member} > 1 {{ return Err(CodecStatus::ValueOutOfRange); }}")
        lines.append(f"        value.{member} = {decode_expression(field, f'raw_{member}')};")
        lines += rust_checks(f"value.{member}", field, unpack=True)
    lines.append("        Ok(value)")
    lines.append("    }")
    lines.append("}")
    lines.append("")
    return lines


# ── conformance tests ──────────────────────────────────────────────────


def sample(field: dict, which: str) -> str:
    rtype = rust_type(field)
    if "constant" in field:
        return rust_number(field["constant"])
    if rtype == "bool":
        return "false" if which == "lo" else "true"
    if field.get("enum"):
        values = sorted(int(value) for value in field["enum"])
        return str(values[0] if which == "lo" else values[-1])
    minimum, maximum = field_limits(field)
    if rtype == "f64":
        return rust_number(float(minimum) if which == "lo" else float(maximum))
    return str(int(minimum) if which == "lo" else int(maximum))


def render_tests(validated: dict) -> list[str]:
    lines = ["", "#[cfg(test)]", "mod conformance {", "    use super::*;", "    use crate::CodecStatus;", ""]
    for key, message in sorted(validated["messages"].items()):
        if message["codec"]["strategy"] != "generated":
            continue
        fields = message["layout"]["fields"]
        type_name = pascal(message["name"])
        function_name = identifier(key)
        has_f64 = any(rust_type(field) == "f64" for field in fields)
        lo = ", ".join(f"{rust_ident(field['key'])}: {sample(field, 'lo')}" for field in fields)
        hi = ", ".join(f"{rust_ident(field['key'])}: {sample(field, 'hi')}" for field in fields)
        if not fields:
            lo = hi = "_reserved: ()"
        dlc = message["dlc"]
        lines += [
            "    #[test]",
            f"    fn round_trip_{function_name}() {{",
            f"        for value in [ {type_name} {{ {lo} }}, {type_name} {{ {hi} }} ] {{",
            "            let mut payload = [0u8; 8];",
            f"            assert_eq!(value.pack(&mut payload[..{dlc}]), CodecStatus::Ok);",
            f"            let decoded = {type_name}::unpack(&payload[..{dlc}]).unwrap();",
        ]
        if not has_f64:
            lines.append("            assert_eq!(decoded, value);")
        lines += [
            "            let mut again = [0u8; 8];",
            f"            assert_eq!(decoded.pack(&mut again[..{dlc}]), CodecStatus::Ok);",
            f"            assert_eq!(again[..{dlc}], payload[..{dlc}]);",
            "        }",
            "    }",
            "",
        ]
    lines += [
        "    #[test]",
        "    fn tx_owners_unique() {",
        "        for (index, owner) in TX_OWNERS.iter().enumerate() {",
        '            if owner.sender == "Any" { continue; }',
        "            for other in &TX_OWNERS[index + 1..] {",
        '                if other.sender == "Any" { continue; }',
        "                assert!(!(owner.bus == other.bus && owner.id == other.id),",
        '                    "duplicate TX owner for {} 0x{:X}", owner.bus, owner.id);',
        "            }",
        "        }",
        "    }",
        "",
        "    #[test]",
        "    fn metadata_nonempty() {",
        "        assert!(!MESSAGES.is_empty());",
        "        assert!(!ROUTES.is_empty());",
        "    }",
        "}",
        "",
    ]
    return lines


def render_rust(validated: dict, semantic_hash: str, network_hash: str) -> str:
    lines = [
        "// Generated by protocol.tools.rust_emit. Do not edit.",
        "// Rust codec for the E-Trike wire contract (no_std).",
        "",
        f'pub const SEMANTIC_HASH: &str = "{semantic_hash}";',
        "pub const WIRE_HASH: &str = SEMANTIC_HASH;",
        f'pub const NETWORK_HASH: &str = "{network_hash}";',
        "",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub enum CodecStrategy { Generated, Profile, Custom }",
        "#[derive(Debug, Clone, Copy, PartialEq, Eq)]",
        "pub enum RouteSemantics { SameFrame, Regenerated }",
        "",
        "#[derive(Debug, Clone, Copy)]",
        "pub struct MessageMeta { pub key: &'static str, pub bus: &'static str, pub id: u32, pub dlc: u8, pub extended: bool, pub strategy: CodecStrategy }",
        "#[derive(Debug, Clone, Copy)]",
        "pub struct RouteMeta { pub key: &'static str, pub message: &'static str, pub from: &'static str, pub to: &'static str, pub semantics: RouteSemantics }",
        "#[derive(Debug, Clone, Copy)]",
        "pub struct TxOwner { pub bus: &'static str, pub id: u32, pub sender: &'static str }",
        "",
    ]

    rows = []
    for key, message in sorted(validated["messages"].items()):
        strategy = {"generated": "Generated", "profile": "Profile", "custom": "Custom"}[
            message["codec"]["strategy"]
        ]
        for instance in sorted(message["instances"], key=lambda value: (value["bus"], parse_id(value["id"]))):
            rows.append(
                f'    MessageMeta {{ key: "{key}", bus: "{instance["bus"]}", '
                f'id: 0x{parse_id(instance["id"]):X}, dlc: {message["dlc"]}, '
                f'extended: {str(instance["frame_format"] == "extended").lower()}, '
                f"strategy: CodecStrategy::{strategy} }},"
            )
    route_rows = [
        f'    RouteMeta {{ key: "{route["key"]}", message: "{route["message"]}", '
        f'from: "{route["from"]}", to: "{route["to"]}", '
        f'semantics: RouteSemantics::{"SameFrame" if route["semantics"] == "same_frame" else "Regenerated"} }},'
        for route in validated["network"]["routes"]
    ]
    owner_rows = []
    for _, message in sorted(validated["messages"].items()):
        for instance in sorted(message["instances"], key=lambda value: (value["bus"], parse_id(value["id"]))):
            owner_rows.append(
                f'    TxOwner {{ bus: "{instance["bus"]}", '
                f'id: 0x{parse_id(instance["id"]):X}, sender: "{instance["sender"]}" }},'
            )

    lines += [
        "pub const MESSAGES: &[MessageMeta] = &[",
        *rows,
        "];",
        "",
        "pub const ROUTES: &[RouteMeta] = &[",
        *route_rows,
        "];",
        "",
        "pub const TX_OWNERS: &[TxOwner] = &[",
        *owner_rows,
        "];",
        "",
        "/// Look up a message instance by bus name and CAN id.",
        "pub fn lookup(bus: &str, id: u32) -> Option<MessageMeta> {",
        "    MESSAGES.iter().copied().find(|meta| meta.bus == bus && meta.id == id)",
        "}",
        "",
        "pub mod detail {",
        "    pub fn extract(source: &[u8], byte: usize, bit: u8, width: u8, little: bool) -> u64 {",
        "        if little {",
        "            let mut value = 0u64;",
        "            let mut index = 0u8;",
        "            while index < width {",
        "                let absolute = byte * 8 + bit as usize + index as usize;",
        "                value |= (((source[absolute / 8] >> (absolute % 8)) & 1) as u64) << index;",
        "                index += 1;",
        "            }",
        "            return value;",
        "        }",
        "        if bit as usize + width as usize <= 8 {",
        "            let mask = if width >= 64 { u64::MAX } else { (1u64 << width) - 1 };",
        "            return ((source[byte] as u64) >> bit) & mask;",
        "        }",
        "        let mut value = 0u64;",
        "        let mut index = 0usize;",
        "        while index < (width / 8) as usize {",
        "            value = (value << 8) | source[byte + index] as u64;",
        "            index += 1;",
        "        }",
        "        value",
        "    }",
        "",
        "    pub fn insert(destination: &mut [u8; 8], byte: usize, bit: u8, width: u8, little: bool, value: u64) {",
        "        if little {",
        "            let mut index = 0u8;",
        "            while index < width {",
        "                let absolute = byte * 8 + bit as usize + index as usize;",
        "                let mask = 1u8 << (absolute % 8);",
        "                if ((value >> index) & 1) != 0 {",
        "                    destination[absolute / 8] |= mask;",
        "                } else {",
        "                    destination[absolute / 8] &= !mask;",
        "                }",
        "                index += 1;",
        "            }",
        "            return;",
        "        }",
        "        if bit as usize + width as usize <= 8 {",
        "            let value_mask = if width >= 64 { u64::MAX } else { (1u64 << width) - 1 };",
        "            let mask = ((value_mask << bit) & 0xFF) as u8;",
        "            destination[byte] = (destination[byte] & !mask) | (((value << bit) as u8) & mask);",
        "            return;",
        "        }",
        "        let count = (width / 8) as usize;",
        "        let mut index = 0usize;",
        "        while index < count {",
        "            destination[byte + count - 1 - index] = (value >> (index * 8)) as u8;",
        "            index += 1;",
        "        }",
        "    }",
        "",
        "    pub fn sign_extend(value: u64, width: u8) -> i64 {",
        "        if width >= 64 { return value as i64; }",
        "        let sign = 1u64 << (width - 1);",
        "        ((value ^ sign).wrapping_sub(sign)) as i64",
        "    }",
        "",
        "    /// Round half away from zero without a floating-point stdlib.",
        "    pub fn round(value: f64) -> f64 {",
        "        if value >= 0.0 { (value + 0.5) as i64 as f64 } else { (value - 0.5) as i64 as f64 }",
        "    }",
        "}",
        "",
    ]

    for _, message in sorted(validated["messages"].items()):
        if message["codec"]["strategy"] == "generated":
            lines += render_message(message)

    lines += render_tests(validated)
    return "\n".join(lines) + "\n"
