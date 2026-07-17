"""FastAPI application factory (workplan §1.1).

Single-process, single-worker (architecture §4.5). The lifespan owns the shared
singletons via :class:`vtc.services.lifecycle.Lifecycle`, attached to
``app.state`` so routers reach them through ``request.app.state``.

Run locally:
    uvicorn vtc.main:app --host 127.0.0.1 --port 8000
"""

from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI

from vtc import __version__
from vtc.api import protocol_api, sessions, state, status, stream
from vtc.api.errors import register_exception_handlers
from vtc.config import VtcConfig
from vtc.services.lifecycle import Lifecycle


def create_app(config: VtcConfig | None = None) -> FastAPI:
    config = config or VtcConfig.from_env()
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
    app.include_router(stream.router, prefix=prefix)
    app.include_router(sessions.router, prefix=prefix)
    return app


# Module-level app for `uvicorn vtc.main:app`.
app = create_app()
