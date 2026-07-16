"""FastAPI application factory (workplan §1.1).

Single-process, single-worker (architecture §4.5). The lifespan owns the shared
singletons via :class:`control_toolkit.services.lifecycle.Lifecycle`, attached to
``app.state`` so routers reach them through ``request.app.state``.

Run locally:
    uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8000
"""

from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI

from control_toolkit import __version__
from control_toolkit.api import protocol_api, state, status, stream
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

    prefix = config.api_prefix
    app.include_router(status.router, prefix=prefix)
    app.include_router(state.router, prefix=prefix)
    app.include_router(protocol_api.router, prefix=prefix)
    app.include_router(stream.router, prefix=prefix)
    return app


# Module-level app for `uvicorn control_toolkit.main:app`.
app = create_app()
