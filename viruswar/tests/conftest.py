"""Tests run from viruswar/: ../.venv/bin/python -m pytest tests (or .venv/bin/python -m pytest tests)."""

import os
import sys

# The AZ bot's modules (and, through its symlinks, common/ and viruswar_sim)
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "az-bot"))
