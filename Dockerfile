FROM debian:bookworm-slim AS build-stage

ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && \
    apt-get --yes upgrade && \
    apt-get --yes install git build-essential automake && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN adduser --shell /bin/false builder
COPY proxycheck.c /home/builder/proxycheck.c
RUN chown builder:builder /home/builder/proxycheck.c
USER builder

WORKDIR /home/builder
ARG GIT_REPO=https://github.com/tinyproxy/tinyproxy.git
ARG GIT_TAG=1.11.2
RUN git clone --depth 1 $GIT_REPO --branch $GIT_TAG tinyproxy

WORKDIR /home/builder/tinyproxy
RUN ./autogen.sh && \
    LDFLAGS=-static ./configure \
      --prefix= \
      --disable-xtinyproxy \
      --disable-filter \
      --disable-upstream \
      --disable-reverse \
      --disable-transparent \
      --disable-manpage_support
RUN make

# Built alongside tinyproxy itself, in the same trusted, from-source builder
# stage — same static-linking approach, same base image, nothing pulled in
# from anywhere less trusted than tinyproxy's own build already is.
WORKDIR /home/builder
RUN gcc -O2 -Wall -Wextra -static -o proxycheck proxycheck.c

FROM scratch AS run-stage

ARG USER_ID=1000

COPY --from=build-stage --chown=0 /home/builder/tinyproxy/src/tinyproxy /
COPY --from=build-stage --chown=0 /home/builder/proxycheck /

USER ${USER_ID}

# Exec form (no shell involved) — matches the rest of this image, which has
# none. See proxycheck.c for what this actually checks: a real proxied
# HTTP request through the running tinyproxy, not just "is the port open."
# --retries smooths over a single transient upstream blip rather than
# flipping to unhealthy on the first one.
HEALTHCHECK --interval=30s --timeout=10s --retries=5 CMD ["/proxycheck"]

CMD ["/tinyproxy", "-d", "-c", "/tinyproxy.conf"]
