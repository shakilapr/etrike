"""Managed software-in-the-loop runtime controls for Computer mode."""

from fastapi import APIRouter, Request

router = APIRouter(prefix="/simulation", tags=["simulation"])


@router.get("")
def status(request: Request) -> dict:
    return {"simulation": request.app.state.lifecycle.simulation_status()}


@router.post("/start")
def start(request: Request) -> dict:
    return {"simulation": request.app.state.lifecycle.start_native_sil()}


@router.post("/stop")
def stop(request: Request) -> dict:
    return {"simulation": request.app.state.lifecycle.stop_native_sil()}
