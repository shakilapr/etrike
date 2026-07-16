"""Sequential verification test API (Phase 6)."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

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
    name: str = "verification_step"
    stimulus: StimulusBody
    expect: ExpectBody
    owner: str = "test:verification"


@router.get("")
def list_tests(request: Request) -> dict:
    items = request.app.state.lifecycle.verification.list_tests()
    return {"count": len(items), "tests": items}


@router.post("")
def run_test(request: Request, body: RunTestBody) -> dict:
    life = request.app.state.lifecycle
    result = life.verification.run(
        name=body.name,
        stimulus=body.stimulus.model_dump(),
        expect=body.expect.model_dump(),
        owner=body.owner,
    )
    life.diagnostics.emit(
        code=f"test.{result['disposition']}",
        title=f"Test {result['disposition']}: {body.name}",
        detail=result.get("detail") or "",
        severity="info" if result["disposition"] == "pass" else "warning",
        evidence={"test_id": result["test_id"], "disposition": result["disposition"]},
    )
    return {"test": result}


@router.get("/{test_id}")
def get_test(test_id: str, request: Request) -> dict:
    body = request.app.state.lifecycle.verification.get(test_id)
    if body is None:
        raise SessionError("test.not_found", f"test {test_id} not found", status=404)
    return {"test": body}
