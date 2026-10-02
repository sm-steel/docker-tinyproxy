# docker-tinyproxy

A minimal, statically-linked [tinyproxy](https://tinyproxy.github.io/) image.

Multi-stage build: tinyproxy is compiled from its own upstream source
(`github.com/tinyproxy/tinyproxy`, pinned tag) in a `debian:bookworm-slim`
builder stage, then only the resulting static binary is copied into a
`FROM scratch` final image — no shell, no package manager, no OS at all
beyond what the container runtime itself provides.

The image also includes `proxycheck`, a small statically-linked helper
built alongside tinyproxy in the same builder stage, used as the image's
`HEALTHCHECK`. It makes one real authenticated HTTP request to the running
tinyproxy instance and checks for a successful response — not just "is the
port open." The request is for tinyproxy's built-in stat host
(`tinyproxy.stats`), which tinyproxy answers itself, so the check never
depends on the outside network. Don't override `StatHost` in your
`tinyproxy.conf`. See `proxycheck.c` for exactly what it does. It reads
its credential from the `HEALTHCHECK_AUTH` environment variable
(`user:password`) — set that to whatever BasicAuth user in your
`tinyproxy.conf` you want the healthcheck to authenticate as.

## Usage

Mount your own `tinyproxy.conf` and run:

```yaml
services:
  tinyproxy:
    image: ghcr.io/<owner>/docker-tinyproxy:1.11.2
    ports:
      - "8888:8888"
    environment:
      HEALTHCHECK_AUTH: "someuser:somepassword"
    volumes:
      - ./tinyproxy.conf:/tinyproxy.conf:ro
```

## Versioning

Image tags track tinyproxy's own upstream version numbers exactly
(`ghcr.io/<owner>/docker-tinyproxy:1.11.2` builds tinyproxy's own `1.11.2`
tag) — pushing a `vX.Y.Z` git tag here builds and publishes that version.

## Building locally

```sh
docker build -t docker-tinyproxy:local --build-arg GIT_TAG=1.11.2 .
```
