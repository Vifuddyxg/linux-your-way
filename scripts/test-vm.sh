#!/bin/sh
# LYW VM test harness — build inside a live ISO, then boot the result.
#
#   scripts/test-vm.sh iso <live.iso>   boot a live ISO with the target disk
#                                       attached and lyw-out/ shared via 9p
#   scripts/test-vm.sh boot             boot the installed disk (UEFI)
#   scripts/test-vm.sh clean            remove the disk image + UEFI vars
#
# Inside the live system:
#   mount -t 9p -o trans=virtio lywshare /mnt
#   DISK=/dev/vda sh /mnt/build.sh
#
# Overrides: IMG=, SIZE=, MEM=, OUT=, GFX="-device virtio-vga -display sdl"
set -eu
cd "$(dirname "$0")/.."

IMG=${IMG:-lyw-test.img}
SIZE=${SIZE:-20G}
MEM=${MEM:-4G}
OUT=${OUT:-lyw-out}
# virtio-vga-gl needs qemu built with virgl (USE=virgl); plain virtio-vga always works
GFX=${GFX:--device virtio-vga -display gtk}

OVMF=$(ls /usr/share/edk2/OvmfX64/OVMF_CODE.fd \
          /usr/share/edk2-ovmf/OVMF_CODE.fd \
          /usr/share/OVMF/OVMF_CODE.fd 2>/dev/null | head -1)
[ -n "$OVMF" ] || { echo "OVMF not found — emerge sys-firmware/edk2-bin" >&2; exit 1; }
VARS=lyw-ovmf-vars.fd
[ -f "$VARS" ] || cp "$(dirname "$OVMF")/OVMF_VARS.fd" "$VARS"

run() {
  # $GFX is intentionally unquoted: it holds multiple arguments
  exec qemu-system-x86_64 -enable-kvm -m "$MEM" -cpu host -smp 4 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF" \
    -drive if=pflash,format=raw,file="$VARS" \
    -drive file="$IMG",format=raw,if=virtio \
    -nic user,model=virtio-net-pci \
    $GFX "$@"
}

case "${1:-}" in
iso)
  [ -n "${2:-}" ] || { echo "usage: $0 iso <live.iso>" >&2; exit 2; }
  [ -f "$IMG" ] || qemu-img create -f raw "$IMG" "$SIZE"
  [ -d "$OUT" ] || { echo "$OUT/ missing — run ./lyw first" >&2; exit 1; }
  echo "In the live system:"
  echo "  mount -t 9p -o trans=virtio lywshare /mnt"
  echo "  DISK=/dev/vda sh /mnt/build.sh"
  run -cdrom "$2" -boot d \
    -virtfs "local,path=$OUT,mount_tag=lywshare,security_model=none"
  ;;
boot)
  [ -f "$IMG" ] || { echo "no $IMG — install first with: $0 iso <live.iso>" >&2; exit 1; }
  run
  ;;
clean)
  rm -f "$IMG" "$VARS"
  ;;
*)
  echo "usage: $0 iso <live.iso> | boot | clean" >&2
  exit 2
  ;;
esac
