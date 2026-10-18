# Build with `sandcastle docker build-image`; its AGENT_UID/GID arguments match the host.
FROM ubuntu:24.04
LABEL keep=true
ENV DEBIAN_FRONTEND=noninteractive

# README's Linux app/WebView toolchain plus Pi and formatting/linting tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake curl git pkg-config ripgrep \
    libwebkit2gtk-4.1-dev clang-tidy-18 \
    python3-pip xz-utils xvfb \
  && ln -s /usr/bin/clang-tidy-18 /usr/local/bin/clang-tidy \
  && rm -rf /var/lib/apt/lists/*
ARG CLANG_FORMAT_VERSION=22.1.3
ARG UV_VERSION=0.11.6
RUN pip3 install --no-cache-dir --break-system-packages clang-format==${CLANG_FORMAT_VERSION} uv==${UV_VERSION}

# Ubuntu 24.04's apt Node (18) is too old for Pi (>=22.19).
ARG NODE_VERSION=22.19.0
RUN set -eu; \
  case "$(dpkg --print-architecture)" in \
    amd64) arch=x64; sha=c0649af18e6a24f6fe5535a3e86b341dd49a8e71117c8b68bde973ef834f16f2 ;; \
    arm64) arch=arm64; sha=0b2d9f564b6594222a62c82e1df2efe119dd4a4aff29644f4dd325bf360b6bcc ;; \
    *) echo 'Unsupported Node architecture' >&2; exit 1 ;; \
  esac; \
  archive="node-v${NODE_VERSION}-linux-${arch}.tar.xz"; \
  curl -fsSLo /tmp/node.tar.xz "https://nodejs.org/dist/v${NODE_VERSION}/${archive}"; \
  echo "${sha}  /tmp/node.tar.xz" | sha256sum -c -; \
  tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1; \
  rm /tmp/node.tar.xz
ARG PI_VERSION=1.2.0
RUN npm install -g --ignore-scripts --no-audit --no-fund @earendil-works/pi-coding-agent@${PI_VERSION}

ARG AGENT_UID=1000
ARG AGENT_GID=1000
# Ubuntu's default user/group are 1000:1000; match Sandcastle's build args.
RUN groupmod -o -g "$AGENT_GID" -n agent ubuntu \
  && usermod -o -u "$AGENT_UID" -g "$AGENT_GID" -d /home/agent -m -l agent ubuntu \
  && mkdir -p /home/agent/.pi/agent /home/agent/.agents/skills \
  && chown -R "$AGENT_UID:$AGENT_GID" /home/agent
ENV HOME=/home/agent
USER ${AGENT_UID}:${AGENT_GID}

# Install the pinned Harness Kit checkout, including its Pi package and agent dependencies.
ARG HARNESS_KIT_REF=bc96832240863548b801389cd4eb1958bfa7acd8
RUN git init -q /home/agent/harness-kit \
  && cd /home/agent/harness-kit \
  && git fetch -q --depth 1 https://github.com/pcheng17/harness-kit.git "$HARNESS_KIT_REF" \
  && git checkout -q FETCH_HEAD \
  && uv run --locked harness-kit install --harness pi \
  && mkdir -p /home/agent/.pi/sandcastle-profile \
  && ln -s /home/agent/.pi/sandcastle-profile/auth.json /home/agent/.pi/agent/auth.json \
  && ln -s /home/agent/.pi/sandcastle-profile/models-store.json /home/agent/.pi/agent/models-store.json

WORKDIR /home/agent
# Sandcastle bind-mounts the worktree at /home/agent/workspace at container start.
ENTRYPOINT ["sleep", "infinity"]
