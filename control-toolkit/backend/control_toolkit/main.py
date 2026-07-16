"""FastAPI application factory.

Single-process, single-worker Uvicorn. The lifespan owns shared services via
Lifecycle on app.state.

Run locally:
    uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8000
"""

from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI

from control_toolkit import __version__
from control_toolkit.api import (
    analysis,
    control,
    events,
    evidence,
    hmi,
    injections,
    logs,
    protocol_api,
    recordings,
    sessions,
    settings,
    state,
    status,
    stream,
    synthetic,
    tests_api,
)
from control_toolkit.api.errors import register_exception_handlers
from control_toolkit.config import ToolkitConfig
from control_toolkit.services.lifecycle import Lifecycle


def create_app(config: ToolkitConfig | None = None) -> FastAPI:
    config = config or ToolkitConfig.from_env()
    lifecycle = Lifecycle(config)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        await lifecycle.startup()
        try:
            yield
        finally:
            await lifecycle.shutdown()

    app = FastAPI(title=config.title, version=__version__, lifespan=lifespan)
    app.state.config = config
    app.state.lifecycle = lifecycle
    register_exception_handlers(app)

    prefix = config.api_prefix
    app.include_router(status.router, prefix=prefix)
    app.include_router(state.router, prefix=prefix)
    app.include_router(protocol_api.router, prefix=prefix)
    app.include_router(sessions.router, prefix=prefix)
    app.include_router(settings.router, prefix=prefix)
    app.include_router(injections.router, prefix=prefix)
    app.include_router(synthetic.router, prefix=prefix)
    app.include_router(hmi.router, prefix=prefix)
    app.include_router(analysis.router, prefix=prefix)
    app.include_router(control.router, prefix=prefix)
    app.include_router(recordings.router, prefix=prefix)
    app.include_router(events.router, prefix=prefix)
    app.include_router(evidence.router, prefix=prefix)
    app.include_router(tests_api.router, prefix=prefix)
    app.include_router(logs.router, prefix=prefix)
    app.include_router(stream.router, prefix=prefix)
    return app


app = create_app()
