"""pytest fixtures shared with the AgentRedactor GUI E2E tests."""

import asyncio
from collections.abc import AsyncIterator

import aiohttp
import pytest
import pytest_asyncio

from mock_llm import MockLLM


@pytest.fixture(scope="session")
def event_loop():
    """Provide a session-scoped event loop for pytest-asyncio."""
    loop = asyncio.get_event_loop_policy().new_event_loop()
    yield loop
    loop.close()


@pytest_asyncio.fixture
async def client() -> AsyncIterator[aiohttp.ClientSession]:
    async with aiohttp.ClientSession() as session:
        yield session


@pytest_asyncio.fixture
async def mock_llm() -> AsyncIterator[MockLLM]:
    async with MockLLM() as llm:
        yield llm
