# Two images: docker build --target server | azbot (or docker compose build)

# ── Build: GameServer and viruswar_sim, with Debian's Qt 6.8 (as in CI) ─────
FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ make qt6-base-dev libgl-dev python3-dev python3-venv \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# CMake builds viruswar_sim for this venv's Python, the same one the azbot image runs
RUN python3 -m venv viruswar/.venv \
    && viruswar/.venv/bin/pip install --no-cache-dir pybind11 \
    && cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel --target GameServer viruswar_sim

# ── Server: accounts and stats live in the /data volume ─────────────────────
FROM debian:trixie-slim AS server
RUN apt-get update && apt-get install -y --no-install-recommends \
        libqt6core6t64 libqt6network6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 game \
    && install -d -o game /data
COPY --from=build /src/build/server/GameServer /usr/local/bin/GameServer
USER game
WORKDIR /data
EXPOSE 1234
ENTRYPOINT ["GameServer", "--auth-file", "/data/auth.txt", "--stats-file", "/data/stats.txt"]

# ── AZ bot: promoted models are mounted at /models ──────────────────────────
FROM debian:trixie-slim AS azbot
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-venv \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 game
COPY viruswar/az-bot/requirements.txt /tmp/requirements.txt
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir torch --index-url https://download.pytorch.org/whl/cpu \
    && /opt/venv/bin/pip install --no-cache-dir -r /tmp/requirements.txt
COPY viruswar/common /app/viruswar/common
COPY viruswar/az-bot /app/viruswar/az-bot
COPY --from=build /src/build/server/viruswar_sim*.so /app/viruswar/az-bot/
USER game
WORKDIR /app/viruswar/az-bot
ENTRYPOINT ["/opt/venv/bin/python", "play.py", "--promoted-dir", "/models", "--log", ""]
