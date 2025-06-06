FROM ubuntu:24.04

ARG TOOLCHAIN_VERSION

# Stops tzdata interactive configuration.
ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    ca-certificates wget git
# Do NOT be smart here and clean the cache because the container is used in the
# stateful context.

RUN wget -q https://s3-eu-west-1.amazonaws.com/deps.memgraph.io/toolchain-${TOOLCHAIN_VERSION}/toolchain-${TOOLCHAIN_VERSION}-binaries-ubuntu-24.04-amd64.tar.gz \
    -O toolchain-${TOOLCHAIN_VERSION}-binaries-ubuntu-24.04-amd64.tar.gz \
    && tar xzvf toolchain-${TOOLCHAIN_VERSION}-binaries-ubuntu-24.04-amd64.tar.gz -C /opt \
    && rm toolchain-${TOOLCHAIN_VERSION}-binaries-ubuntu-24.04-amd64.tar.gz

# Install toolchain run deps and memgraph build deps
ARG GIT_REF
SHELL ["/bin/bash", "-c"]
RUN git clone --branch ${GIT_REF} https://github.com/memgraph/memgraph.git \
    && cd memgraph \
    && ./environment/os/ubuntu-24.04.sh install TOOLCHAIN_RUN_DEPS \
    && ./environment/os/ubuntu-24.04.sh install MEMGRAPH_BUILD_DEPS \
    && cd .. && rm -rf memgraph

# Create mg user and set as default
RUN useradd -m -s /bin/bash mg
USER mg

# Install rust
ARG RUST_VERSION
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y \
    && . "$HOME/.cargo/env" \
    && rustup default ${RUST_VERSION}

# Fix node
ARG NODE_VERSION
RUN curl https://raw.githubusercontent.com/creationix/nvm/master/install.sh | bash \
    && . ~/.nvm/nvm.sh \
    && nvm install ${NODE_VERSION} \
    && nvm use ${NODE_VERSION}

ENTRYPOINT ["sleep", "infinity"]
