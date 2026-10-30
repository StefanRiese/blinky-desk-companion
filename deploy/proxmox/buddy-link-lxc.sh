#!/usr/bin/env bash
# Creates a small Debian container (LXC) on a Proxmox VE host and installs buddy-link in it,
# so Desk Buddies on the network can use it (they find it via mDNS).
#
# Run on the Proxmox host as root:
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/StefanRiese/blinky-desk-companion/main/deploy/proxmox/buddy-link-lxc.sh)"
# or with options:
#   ./buddy-link-lxc.sh --ctid 120 --storage local-zfs --ip 192.168.1.20/24 --gw 192.168.1.1
#   ./buddy-link-lxc.sh --update 120          # install the latest buddy-link into an existing container
#
# Options:
#   --ctid N            container id (default: next free id)
#   --hostname NAME     (default: deskbuddy)
#   --storage NAME      storage for the root disk (default: local-lvm)
#   --tpl-storage NAME  storage for the template (default: local)
#   --bridge NAME       network bridge (default: vmbr0)
#   --ip dhcp|CIDR      (default: dhcp; a fixed address or DHCP reservation is recommended)
#   --gw ADDRESS        gateway for a fixed --ip
#   --memory MB         (default: 128)
#   --disk GB           (default: 2)
#   --version vX.Y.Z    buddy-link release (default: the latest release; while there is only a
#                       pre-release, the newest pre-release)
#   --binary PATH       use this buddy-link binary instead of downloading a release
#   --update CTID       only replace buddy-link in an existing container
set -euo pipefail

REPO="StefanRiese/blinky-desk-companion"
CTID="" HOSTNAME="deskbuddy" STORAGE="local-lvm" TPL_STORAGE="local" BRIDGE="vmbr0"
IP="dhcp" GW="" MEMORY=128 DISK=2 VERSION="latest" BINARY="" UPDATE=""

die() { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

# newest_url prints the download URL of buddy-link-linux-ARCH from the newest release that has it,
# pre-releases included (GitHub's "latest" link skips them), but not the rolling build from
# main (latest-main). The API lists newest first.
newest_url() {
  curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=30" |
    sed -n "s|.*\"browser_download_url\": *\"\(https://[^\"]*/buddy-link-linux-$1\)\".*|\1|p" |
    grep -v "/download/latest-main/" | sed -n 1p
}

while [ $# -gt 0 ]; do
  case "$1" in
    --ctid) CTID="$2"; shift 2 ;;
    --hostname) HOSTNAME="$2"; shift 2 ;;
    --storage) STORAGE="$2"; shift 2 ;;
    --tpl-storage) TPL_STORAGE="$2"; shift 2 ;;
    --bridge) BRIDGE="$2"; shift 2 ;;
    --ip) IP="$2"; shift 2 ;;
    --gw) GW="$2"; shift 2 ;;
    --memory) MEMORY="$2"; shift 2 ;;
    --disk) DISK="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --binary) BINARY="$2"; shift 2 ;;
    --update) UPDATE="$2"; shift 2 ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    *) die "unknown option $1 (see --help)" ;;
  esac
done

command -v pct >/dev/null || die "pct not found: run this on a Proxmox VE host"
[ "$(id -u)" = 0 ] || die "run as root"

ARCH="$(dpkg --print-architecture)"
case "$ARCH" in amd64|arm64) ;; *) die "unsupported architecture $ARCH" ;; esac

# --- buddy-link binary -------------------------------------------------------------------
if [ -z "$BINARY" ]; then
  if [ "$VERSION" = "latest" ]; then
    url="https://github.com/$REPO/releases/latest/download/buddy-link-linux-$ARCH"
    if ! curl -fsLI -o /dev/null "$url"; then # 404 while there is no regular release
      info "no regular release yet: taking the newest pre-release"
      url="$(newest_url "$ARCH")" || url=""
      [ -n "$url" ] || die "no release with buddy-link-linux-$ARCH found (use --binary PATH)"
    fi
  else
    url="https://github.com/$REPO/releases/download/$VERSION/buddy-link-linux-$ARCH"
  fi
  BINARY="$(mktemp)"
  trap 'rm -f "$BINARY"' EXIT
  info "downloading $url"
  curl -fsSL -o "$BINARY" "$url" || die "download failed (is there a release yet? otherwise use --binary PATH)"
fi
[ -f "$BINARY" ] || die "binary $BINARY not found"

install_into() {
  local ct="$1"
  info "installing buddy-link into container $ct"
  pct push "$ct" "$BINARY" /tmp/buddy-link --perms 0755
  # Go uses the system CA certificates for HTTPS (weather, APIs).
  pct exec "$ct" -- sh -c 'test -e /etc/ssl/certs/ca-certificates.crt || (apt-get update -qq && apt-get install -y -qq ca-certificates)'
  pct exec "$ct" -- /tmp/buddy-link install --serial off
  pct exec "$ct" -- rm -f /tmp/buddy-link
}

if [ -n "$UPDATE" ]; then
  pct status "$UPDATE" >/dev/null 2>&1 || die "container $UPDATE not found"
  install_into "$UPDATE"
  pct exec "$UPDATE" -- /usr/local/bin/buddy-link version
  exit 0
fi

# --- container -----------------------------------------------------------------------
CTID="${CTID:-$(pvesh get /cluster/nextid)}"
pct status "$CTID" >/dev/null 2>&1 && die "container $CTID already exists (use --update $CTID to update buddy-link)"

info "looking for a Debian template"
pveam update >/dev/null || true
TPL=""
for v in 13 12; do
  TPL="$(pveam available --section system | awk -v v="debian-$v-standard" '$2 ~ v {print $2}' | sort -V | tail -1)"
  [ -n "$TPL" ] && break
done
[ -n "$TPL" ] || die "no Debian template found (pveam available --section system)"
if ! pveam list "$TPL_STORAGE" | grep -q "$TPL"; then
  info "downloading template $TPL"
  pveam download "$TPL_STORAGE" "$TPL" >/dev/null
fi

net="name=eth0,bridge=$BRIDGE,ip=$IP"
[ -n "$GW" ] && net="$net,gw=$GW"
info "creating container $CTID ($HOSTNAME, $TPL)"
pct create "$CTID" "$TPL_STORAGE:vztmpl/$TPL" \
  --hostname "$HOSTNAME" --description "Blinky host service (buddy-link)" \
  --memory "$MEMORY" --swap 0 --cores 1 --rootfs "$STORAGE:$DISK" \
  --net0 "$net" --unprivileged 1 --features nesting=1 --onboot 1 --start 1 >/dev/null

info "waiting for the network"
addr=""
for _ in $(seq 1 30); do
  addr="$(pct exec "$CTID" -- hostname -I 2>/dev/null | awk '{print $1}')" || true
  [ -n "$addr" ] && break
  sleep 2
done
[ -n "$addr" ] || die "container $CTID got no IP address (check --bridge/--ip)"

install_into "$CTID"

cat <<EOF

Blinky host is running in container $CTID ($HOSTNAME) at $addr.

  Devices on this network find it by themselves (mDNS) when their host link is "auto".
  Otherwise set the TCP address $addr:7531 in the setup page:
  https://stefanriese.github.io/blinky-desk-companion/

  Logs:     pct exec $CTID -- journalctl -u buddy-link -f
  Devices:  pct exec $CTID -- buddy-ctl devices
  Config:   /etc/buddy-link/config.yaml in the container
  Update:   run this script again with --update $CTID
EOF
