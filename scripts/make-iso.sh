#!/bin/bash
# make-iso.sh — runs INSIDE the Artix build container (see Dockerfile/build-iso.sh).
# 1) builds the [lyw] package repo (the TUI, from the local source tree)
# 2) assembles the artools iso profile
# 3) runs buildiso to produce the ISO into /out
# Recipe adapted from Wheatley Linux.
set -euo pipefail

ROOT=/lyw
REPO=$ROOT/repo/x86_64           # local pacman repo (db + packages)
OUT=/out

echo ">>> [1/4] refresh keyrings"
pacman -Sy --noconfirm --needed artix-keyring || true
pacman-key --populate artix 2>/dev/null || true

echo ">>> [2/4] build lyw-tui -> [lyw] repo"
mkdir -p "$REPO"
# tar the local source next to the PKGBUILD (makepkg can't take ../.. paths)
tar -czf "$ROOT/packages/lyw-tui/lyw-src.tar.gz" -C "$ROOT" Makefile src
chown -R builder:builder "$ROOT/repo" "$ROOT/packages"
repo-add -q "$REPO/lyw.db.tar.gz" 2>/dev/null || true
grep -q '^\[lyw\]' /etc/pacman.conf || cat >> /etc/pacman.conf <<EOF

[lyw]
SigLevel = Optional TrustAll
Server = file://$REPO
EOF
pacman -Sy --noconfirm >/dev/null 2>&1 || true

for pkg in lyw-tui; do
    echo "    -- $pkg"
    ( cd "$ROOT/packages/$pkg" && \
      sudo -u builder makepkg -f --syncdeps --noconfirm --skippgpcheck )
    cp "$ROOT/packages/$pkg"/*.pkg.tar.* "$REPO"/ 2>/dev/null || true
    repo-add -q "$REPO/lyw.db.tar.gz" "$REPO"/*.pkg.tar.* >/dev/null 2>&1 || true
    pacman -Sy --noconfirm >/dev/null 2>&1 || true
done
repo-add "$REPO/lyw.db.tar.gz" "$REPO"/*.pkg.tar.* 2>/dev/null || \
    repo-add "$REPO/lyw.db.tar.zst" "$REPO"/*.pkg.tar.*

# make the [lyw] repo visible to buildiso's pacman
install -Dm644 "$ROOT/repo/pacman.conf" /etc/pacman.conf
sed -i "s|file:///usr/share/lyw/repo|file://$REPO|" /etc/pacman.conf
# build-host only: keep pacman's scriptlet sandbox off (this is the container's
# config, not the target's).
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf
pacman -Sy --noconfirm || true

# buildiso installs the live rootfs with ITS OWN pacman config
# (/usr/share/artools/pacman.conf.d/iso-*-x86_64.conf), not /etc/pacman.conf —
# so the [lyw] repo must be added there too, or the rootfs install fails with
# "target not found". SigLevel is relaxed for the build only.
for isoconf in /usr/share/artools/pacman.conf.d/iso*-x86_64.conf; do
    [ -f "$isoconf" ] || continue
    grep -q '^\[lyw\]' "$isoconf" && continue
    cat >> "$isoconf" <<EOF

[lyw]
SigLevel = Optional TrustAll
Server = file://$REPO
EOF
done

echo ">>> [3/4] assemble iso profile"
# start from the official artix iso-profiles, layer our 'lyw' profile on top
PROFILES=/usr/share/artools/iso-profiles
[ -d "$PROFILES" ] || PROFILES=$(buildiso -q 2>/dev/null; echo /usr/share/artools/iso-profiles)
git clone --depth=1 https://gitea.artixlinux.org/artix/iso-profiles "$PROFILES" 2>/dev/null || true
cp -r "$ROOT/iso-profile/lyw" "$PROFILES/lyw"

# carry the built repo + pacman.conf into the live root so the live system
# can install lyw-tui and generated build.sh scripts can reuse the config
DEST="$PROFILES/lyw/root-overlay/usr/share/lyw"
mkdir -p "$DEST"
cp -a "$REPO" "$DEST/repo"
install -Dm644 "$ROOT/repo/pacman.conf" "$DEST/pacman.conf"

echo ">>> [4/4] buildiso"
mkdir -p "$OUT"
# Write the finished ISO under the bind-mounted /var/lib/artools so it survives
# the --rm container (artools' default ISO_POOL is ephemeral).
ISO_POOL=/var/lib/artools/iso
mkdir -p "$ISO_POOL"
if grep -q '^[#[:space:]]*ISO_POOL=' /etc/artools/artools-iso.conf; then
    sed -i "s|^[#[:space:]]*ISO_POOL=.*|ISO_POOL=$ISO_POOL|" /etc/artools/artools-iso.conf
else
    echo "ISO_POOL=$ISO_POOL" >> /etc/artools/artools-iso.conf
fi
# De-Artix the ISO identity: buildiso hardcodes the volume label "ARTIX_YYYYMM"
# and prefixes the filename with "artix". Label -> LYW_, drop the prefix so the
# file is just "lyw-runit-...".
sed -i 's/iso_label="ARTIX_/iso_label="LYW_/' /usr/bin/buildiso
sed -i 's/local vars=("artix")/local vars=()/' /usr/bin/buildiso
# Quiet live boot: kernel/udev chatter on tty1 scribbles over the TUI.
# kopts is hardcoded in artools' initcpio.sh (no profile hook), so patch it.
sed -i "s/kopts+=('overlay=livefs')/kopts+=('overlay=livefs' 'quiet' 'loglevel=3' 'udev.log_priority=3')/" \
    /usr/share/artools/lib/iso/initcpio.sh

# clear stale ISOs from the (persistent) pool so only this build's ISO remains
rm -f "$ISO_POOL"/lyw/*.iso 2>/dev/null || true

buildiso -p lyw -i runit || {
    echo "!! buildiso failed — check artools version / profile keys" >&2
    exit 1
}
rm -f "$OUT"/*.iso 2>/dev/null || true
find "$ISO_POOL" /var/lib/artools /root/artools-workspace /var/cache/artools \
    -name '*.iso' -exec cp -v {} "$OUT/" \; 2>/dev/null || true
echo ">>> ISO(s) in $OUT:"; ls -lh "$OUT" || true
