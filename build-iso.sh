#!/usr/bin/env bash
# build-iso.sh — build the LYW Live ISO on any host with Docker.
# Spins an Artix build container (artools), builds the [lyw] package repo,
# and runs buildiso. The finished ISO lands in ./out/.
# Recipe adapted from Wheatley Linux (same fixes, no CachyOS repo).
set -euo pipefail
cd "$(dirname "$0")"

IMAGE=lyw-build
OUT="$PWD/out"
# buildiso layers livefs over rootfs with overlayfs. Its workdir must live on a
# real disk fs (ext4) that supports being an overlayfs upperdir — Docker's own
# overlay2 rootfs does NOT (overlay-on-overlay is rejected), so we bind-mount a
# host dir. .pkgcache persists the package cache so re-runs don't re-download.
WORK="$PWD/.artools-work"
PKGCACHE="$PWD/.pkgcache"
mkdir -p "$OUT"

echo ">>> building image '$IMAGE' (Artix + artools)"
# --network=host: this host has no default docker 'bridge' network
docker build --network=host -t "$IMAGE" .

# created after the build so they don't bloat the build context
mkdir -p "$WORK" "$PKGCACHE"

echo ">>> running ISO build (privileged — needs loop devices / overlayfs)"
RUN_TTY=""; [ -t 1 ] && RUN_TTY="-it"   # only attach a TTY when interactive
docker run --rm $RUN_TTY \
    --privileged \
    --network=host \
    -v /dev:/dev \
    -v "$OUT:/out" \
    -v "$WORK:/var/lib/artools" \
    -v "$PKGCACHE:/var/cache/pacman/pkg" \
    "$IMAGE"

echo
echo ">>> done. ISO(s):"
ls -lh "$OUT"
echo
echo "Test it in a VM:   scripts/test-vm.sh iso out/<file>.iso"
echo "Write to USB:      sudo dd if=out/<file>.iso of=/dev/sdX bs=4M status=progress oflag=sync"
