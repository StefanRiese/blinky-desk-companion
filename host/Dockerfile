# buddy-link container image: the Blinky host service for servers (Proxmox, NAS, Docker hosts).
# Built by deploy/docker/compose.yml (no image is published), or by hand:
# Devices connect over TCP port 7531. Serial is off: USB devices belong to a buddy-link on the
# computer they are plugged into.
#
#   docker build -t buddy-link host/
#   docker run -d --name buddy-link --network host -v buddy-link-config:/config buddy-link
#   docker exec buddy-link buddy-ctl devices
#
# Cross-compiles on the build machine for each target platform (amd64, arm64, arm/v7).
FROM --platform=$BUILDPLATFORM golang:1.27-trixie AS build
ARG TARGETOS TARGETARCH TARGETVARIANT VERSION=dev
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN GOARM="${TARGETVARIANT#v}" CGO_ENABLED=0 GOOS="$TARGETOS" GOARCH="$TARGETARCH" \
    go build -trimpath -ldflags "-s -w -X main.version=$VERSION" -o /out/bin/ ./cmd/buddy-link ./cmd/buddy-ctl \
 && mkdir -p /out/config

# Distroless: CA certificates and a non-root user, nothing else (no shell).
FROM gcr.io/distroless/static-debian13:nonroot
COPY --from=build /out/bin/ /usr/local/bin/
# /config holds config.yaml (optional) and the control API token; owned by the nonroot user.
COPY --from=build --chown=65532:65532 /out/config /config
ENV BUDDY_LINK_CONFIG_DIR=/config \
    BUDDY_LINK_TCP_ENABLED=true \
    BUDDY_LINK_SERIAL_ENABLED=false
VOLUME /config
EXPOSE 7531
HEALTHCHECK --interval=60s --timeout=5s CMD ["buddy-ctl", "status"]
ENTRYPOINT ["buddy-link"]
CMD ["run"]
