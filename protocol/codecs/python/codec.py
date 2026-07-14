from __future__ import annotations

from collections.abc import Mapping, MutableMapping

from protocol.generated.python.etrike_protocol import METADATA

from . import generated, pwt, seb, ses
from .types import CodecStatus, Frame

_CUSTOM_DECODERS = {
    "ses:vcu_ses_req": ses.decode_command,
    "ses:ses_status": ses.decode_status,
    "ses:ses_err_info": ses.decode_error_info,
    "ses:ses_version": ses.decode_version,
    "ses:ses_test": ses.decode_test,
    "seb:vcu_seb_req": seb.decode_command,
    "seb:seb_status": seb.decode_status,
    "seb:seb_err_info": seb.decode_error_info,
    "seb:seb_version": seb.decode_version,
    "seb:seb_test": seb.decode_test,
}


def decode(message: str, frame: Frame) -> tuple[CodecStatus, dict[str, object] | None]:
    decoder = _CUSTOM_DECODERS.get(message)
    if decoder is not None:
        return decoder(frame)
    if message == "pwt:pwt_dcdc_cmd":
        return pwt.decode_dcdc_command(frame)
    if message not in METADATA:
        return "wrong_message_id", None
    return generated.decode(message, frame)


def decode_into(message: str, frame: Frame, output: MutableMapping[str, object]) -> CodecStatus:
    status, value = decode(message, frame)
    if status == "ok" and value is not None:
        output.clear()
        output.update(value)
    return status


def encode(message: str, values: Mapping[str, object], *, bus: str) -> tuple[CodecStatus, Frame | None]:
    if message == "ses:vcu_ses_req":
        if bus != ses.BUS:
            return "wrong_message_id", None
        return ses.encode_command(values)
    if message == "seb:vcu_seb_req":
        if bus != seb.BUS:
            return "wrong_message_id", None
        return seb.encode_command(values)
    if message == "pwt:pwt_dcdc_cmd":
        if bus != pwt.BUS:
            return "wrong_message_id", None
        return pwt.encode_dcdc_command(values)
    metadata = METADATA.get(message)
    if metadata is None:
        return "wrong_message_id", None
    if metadata["codec"]["strategy"] == "custom":
        raise ValueError(f"{message} has no dedicated encoder")
    return generated.encode(message, values, bus=bus)
