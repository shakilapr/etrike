"""Sequential verification test API (Phase 6)."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter, Request
from pydantic import BaseModel, ConfigDict, Field

from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/tests", tags=["tests"])


class StimulusBody(BaseModel):
    type: str = Field(default="inject", description="Currently only inject")
    bus: str
    key: str
    values: dict[str, Any] = Field(default_factory=dict)


class ExpectBody(BaseModel):
    type: str = Field(
        default="message_observed",
        description="message_observed | signal_equals | signal_in_range",
    )
    bus: str | None = None
    can_id: int | None = None
    name: str | None = None
    signal: str | None = None
    equals: Any = None
    enum: str | None = None
    min: float | None = None
    max: float | None = None
    timeout_ms: float = Field(default=500, ge=10, le=30_000)


class RunTestBody(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    name: str = "verification_step"
    stimulus: StimulusBody
    expect: ExpectBody
    owner: str = "test:verification"
    # async=true returns immediately while RUNNING (preferred for UI).
    async_mode: bool = Field(default=False, alias="async")


@router.get("")
def list_tests(request: Request) -> dict:
    items = request.app.state.lifecycle.verification.list_tests()
    return {"count": len(items), "tests": items}


@router.post("")
def run_test(request: Request, body: RunTestBody) -> dict:
    life = request.app.state.lifecycle
    kwargs = dict(
        name=body.name,
        stimulus=body.stimulus.model_dump(),
        expect=body.expect.model_dump(),
        owner=body.owner,
    )
    if body.async_mode:
        result = life.verification.start(**kwargs)
    else:
        result = life.verification.run(**kwargs)
    life.diagnostics.emit(
        code=f"test.{result['disposition']}",
        title=f"Test {result['disposition']}: {body.name}",
        detail=result.get("detail") or "",
        severity="info" if result["disposition"] in ("pass", "running") else "warning",
        evidence={"test_id": result["test_id"], "disposition": result["disposition"]},
    )
    return {"test": result}


@router.get("/{test_id}")
def get_test(test_id: str, request: Request) -> dict:
    body = request.app.state.lifecycle.verification.get(test_id)
    if body is None:
        raise SessionError("test.not_found", f"test {test_id} not found", status=404)
    return {"test": body}


@router.post("/{test_id}/cancel")
def cancel_test(test_id: str, request: Request) -> dict:
    life = request.app.state.lifecycle
    result = life.verification.cancel(test_id)
    life.diagnostics.emit(
        code="test.cancel_requested",
        title=f"Test cancel requested: {test_id}",
        detail="operator cancel",
        severity="info",
        evidence={"test_id": test_id},
    )
    return {"test": result}
