#include <stdio.h>
#include <string.h>
#include "gen.h"

/* emit a pkg() shell function so later sections are manager-agnostic */
void emit_pkg_helper(FILE *fp, const int s[CAT_COUNT])
{
    fputs("pkg() {\n", fp);
    switch (s[CAT_PM]) {
    case P_PACMAN:
        fputs("  $CHROOT \"$ROOT\" pacman -S --noconfirm --needed \"$@\"\n", fp);
        break;
    case P_XBPS:
        fputs("  XBPS_ARCH=$XARCH xbps-install -y -r \"$ROOT\" -R \"$REPO\" \"$@\"\n", fp);
        break;
    case P_APK:
        fputs("  ./sbin/apk.static -X \"$AMIRROR/main\" -X \"$AMIRROR/community\" \\\n"
              "    -U --allow-untrusted -p \"$ROOT\" add \"$@\"\n", fp);
        break;
    case P_PORTAGE:
        fputs("  # long builds; add --getbinpkg to use the official Gentoo binhost\n"
              "  $CHROOT \"$ROOT\" emerge --noreplace \"$@\"\n", fp);
        break;
    case P_CUSTOM:
        fputs("  lyw_install \"$ROOT\" \"$@\"\n", fp);
        break;
    default:
        fputs("  echo \"pkg: no package manager for: $*\" >&2\n", fp);
        break;
    }
    fputs("}\n\n", fp);
}

/* emit svc_enable(): marks services to start at boot, per init + manager */
void emit_svc_helper(FILE *fp, const int s[CAT_COUNT])
{
    fputs("svc_enable() {  # enable services at boot\n"
          "  for s in \"$@\"; do\n", fp);
    switch (s[CAT_INIT]) {
    case I_SYSTEMD:
        fputs("    $CHROOT \"$ROOT\" systemctl enable \"$s\"\n", fp);
        break;
    case I_OPENRC:
        fputs("    $CHROOT \"$ROOT\" rc-update add \"$s\" default || \\\n"
              "      echo \"NOTE: rc-update add $s default (after first boot)\" >&2\n", fp);
        break;
    case I_RUNIT:
        if (s[CAT_PM] == P_XBPS)
            fputs("    ln -sf \"/etc/sv/$s\" \"$ROOT/etc/runit/runsvdir/default/\"\n", fp);
        else if (s[CAT_PM] == P_PACMAN)
            fputs("    ln -sf \"/etc/runit/sv/$s\" \"$ROOT/etc/runit/runsvdir/default/\"\n", fp);
        else
            fputs("    echo \"NOTE: link $s into the active runsvdir after first boot\" >&2\n", fp);
        break;
    case I_DINIT:
        if (s[CAT_PM] == P_PACMAN)
            fputs("    mkdir -p \"$ROOT/etc/dinit.d/boot.d\"\n"
                  "    ln -sf \"../$s\" \"$ROOT/etc/dinit.d/boot.d/$s\"\n", fp);
        else
            fputs("    echo \"NOTE: write a dinit service for $s and enable it after boot\" >&2\n", fp);
        break;
    case I_S6:
        if (s[CAT_PM] == P_PACMAN)
            fputs("    $CHROOT \"$ROOT\" s6-service add default \"$s\" || \\\n"
                  "      echo \"NOTE: s6-service add default $s (after first boot)\" >&2\n", fp);
        else
            fputs("    echo \"NOTE: write s6 service scripts for $s yourself\" >&2\n", fp);
        break;
    case I_66:
        if (s[CAT_PM] == P_PACMAN)
            fputs("    $CHROOT \"$ROOT\" 66 enable \"$s\" || \\\n"
                  "      echo \"NOTE: 66 enable $s (after first boot)\" >&2\n", fp);
        else
            fputs("    echo \"NOTE: wire $s into 66 yourself\" >&2\n", fp);
        break;
    default: /* sysvinit, busybox-init, shepherd, custom */
        fputs("    echo \"NOTE: start $s from your init's boot scripts\" >&2\n", fp);
        break;
    }
    fputs("  done\n", fp);
    if (s[CAT_INIT] == I_S6 && s[CAT_PM] == P_PACMAN)
        fputs("  $CHROOT \"$ROOT\" s6-db-reload || true\n", fp);
    fputs("}\n\n", fp);
}

/* Artix base packages that pull in the chosen init (+ elogind glue) */
static const char *artix_init_pkgs(int init)
{
    switch (init) {
    case I_DINIT:  return "dinit elogind-dinit";
    case I_RUNIT:  return "runit elogind-runit";
    case I_OPENRC: return "openrc elogind-openrc";
    case I_S6:     return "s6-base elogind-s6";
    case I_66:     return "66 elogind-66";
    default:       return NULL; /* systemd, DIY inits: no Artix init package */
    }
}

static const char *toolchain_pkgs(int pm)
{
    switch (pm) {
    case P_PACMAN:  return "base-devel git";
    case P_XBPS:    return "gcc make git";
    case P_APK:     return "build-base git";
    case P_PORTAGE: return "dev-vcs/git"; /* toolchain is in the stage3 */
    default:        return NULL;
    }
}

/* inits installed on top of the base by hand: finit, sysvinit, busybox, shepherd */
static void emit_diy_init(FILE *fp, const int s[CAT_COUNT])
{
    const char *tc = toolchain_pkgs(s[CAT_PM]);
    switch (s[CAT_INIT]) {
    case I_FINIT:
        fputs("  msg 'Init: Finit — built from source (troglobit/finit + libuev + libite)'\n", fp);
        if (tc) fprintf(fp, "  pkg %s\n", tc);
        else    fputs("  # MANUAL: a C toolchain + git must exist in the target\n", fp);
        switch (s[CAT_PM]) {
        case P_PACMAN:  fputs("  pkg autoconf automake libtool pkgconf\n", fp); break;
        case P_XBPS:    fputs("  pkg autoconf automake libtool pkg-config\n", fp); break;
        case P_APK:     fputs("  pkg autoconf automake libtool pkgconf linux-headers\n", fp); break;
        case P_PORTAGE: fputs("  # autotools are part of the stage3\n", fp); break;
        default:        fputs("  # MANUAL: autotools must exist in the target\n", fp); break;
        }
        fputs("  for repo in libuev libite finit; do\n"
              "    $CHROOT \"$ROOT\" sh -c \"rm -rf /tmp/$repo && \\\n"
              "      git clone --depth 1 https://github.com/troglobit/$repo /tmp/$repo && \\\n"
              "      cd /tmp/$repo && ./autogen.sh && ./configure --prefix=/usr \\\n"
              "        --sysconfdir=/etc --localstatedir=/var && \\\n"
              "      make && make install\"\n"
              "  done\n"
              "  ln -sf ../sbin/finit \"$ROOT/sbin/init\" 2>/dev/null || \\\n"
              "    ln -sf finit \"$ROOT/sbin/init\"\n"
              "  # minimal finit.conf — REVIEW: no service definitions are generated for you\n"
              "  cat > \"$ROOT/etc/finit.conf\" <<'EOF'\n"
              "# Linux Your Way — minimal Finit configuration. REVIEW AND EXTEND.\n"
              "runlevel 2\n"
              "tty [12345] /dev/tty1 115200 linux\n"
              "tty [12345] /dev/tty2 115200 linux\n"
              "EOF\n"
              "  echo 'NOTE: Finit services live in /etc/finit.d/ — write them yourself' >&2\n", fp);
        break;
    case I_SYSVINIT:
        fputs("  msg 'Init: SysVinit — built from source (you own /etc/inittab)'\n", fp);
        if (tc) fprintf(fp, "  pkg %s\n", tc);
        else    fputs("  # MANUAL: a C toolchain + git must exist in the target\n", fp);
        fputs("  $CHROOT \"$ROOT\" sh -c 'rm -rf /tmp/sysvinit && \\\n"
              "    git clone --depth 1 https://git.savannah.nongnu.org/git/sysvinit.git /tmp/sysvinit && \\\n"
              "    make -C /tmp/sysvinit && make -C /tmp/sysvinit install'\n"
              "  # minimal inittab — REVIEW and extend: no rc scripts are generated for you\n"
              "  cat > \"$ROOT/etc/inittab\" <<'EOF'\n"
              "id:3:initdefault:\n"
              "si::sysinit:/bin/sh -c 'mount -o remount,rw /; mount -a'\n"
              "1:2345:respawn:/sbin/agetty 38400 tty1 linux\n"
              "2:2345:respawn:/sbin/agetty 38400 tty2 linux\n"
              "ca::ctrlaltdel:/sbin/shutdown -r now\n"
              "EOF\n", fp);
        break;
    case I_BUSYBOX:
        fputs("  msg 'Init: BusyBox init'\n", fp);
        if (s[CAT_PM] == P_PORTAGE)
            fputs("  pkg sys-apps/busybox\n", fp);
        else if (s[CAT_PM] == P_PACMAN || s[CAT_PM] == P_XBPS || s[CAT_PM] == P_APK)
            fputs("  pkg busybox\n", fp);
        fputs("  BB=$($CHROOT \"$ROOT\" sh -c 'command -v busybox')\n"
              "  [ -n \"$BB\" ] || { echo 'busybox not found in target' >&2; exit 1; }\n"
              "  ln -sf \"$BB\" \"$ROOT/sbin/init\"\n"
              "  cat > \"$ROOT/etc/inittab\" <<EOF\n"
              "::sysinit:/bin/mount -o remount,rw /\n"
              "::sysinit:/bin/mount -a\n"
              "tty1::respawn:$BB getty 38400 tty1\n"
              "tty2::respawn:$BB getty 38400 tty2\n"
              "::ctrlaltdel:/sbin/reboot\n"
              "::shutdown:/bin/umount -a -r\n"
              "EOF\n", fp);
        break;
    case I_SHEPHERD:
        fputs("  msg 'Init: GNU Shepherd — MANUAL'\n"
              "  echo 'Shepherd as PID 1 outside Guix is not automated: install guile +'\n"
              "  echo 'shepherd (source or your repos), then boot with init=/path/to/shepherd'\n"
              "  echo 'and write /etc/shepherd.scm. On Guix System it is the native init.'\n", fp);
        break;
    }
}

void emit_base(FILE *fp, const int s[CAT_COUNT])
{
    fputs("base_install() {\n", fp);
    switch (s[CAT_PM]) {
    case P_PACMAN:
        if (s[CAT_INIT] == I_SYSTEMD) {
            fputs("  msg 'Base: Arch via pacstrap'\n"
                  "  command -v pacstrap >/dev/null 2>&1 || {\n"
                  "    msg 'pacstrap not on this host: fetching arch-install-scripts'\n"
                  "    mkdir -p /tmp/lyw-arch\n"
                  "    AIS=$(curl -s \"$ARCHMIRROR/extra/os/x86_64/\" \\\n"
                  "      | grep -oE 'arch-install-scripts-[^\"]*\\.pkg\\.tar\\.zst' | head -1)\n"
                  "    curl -L \"$ARCHMIRROR/extra/os/x86_64/$AIS\" | tar --zstd -x -C /tmp/lyw-arch\n"
                  "    PATH=/tmp/lyw-arch/usr/bin:$PATH\n"
                  "    CHROOT=arch-chroot   # arch-chroot ships in the same package\n"
                  "  }\n"
                  "  # non-Arch host (e.g. the LYW live ISO) has no Arch repos or keyring:\n"
                  "  # bootstrap with an explicit config, SigLevel Never for this step only;\n"
                  "  # the target gets a real populated keyring right after.\n"
                  "  {\n"
                  "    printf '[options]\\nArchitecture = x86_64\\nSigLevel = Never\\n'\n"
                  "    printf '[core]\\nServer = %s/core/os/x86_64\\n' \"$ARCHMIRROR\"\n"
                  "    printf '[extra]\\nServer = %s/extra/os/x86_64\\n' \"$ARCHMIRROR\"\n"
                  "  } > /tmp/lyw-arch-pacman.conf\n"
                  "  pacstrap -C /tmp/lyw-arch-pacman.conf -K \"$ROOT\" base linux-firmware\n"
                  "  printf 'Server = %s/$repo/os/$arch\\n' \"$ARCHMIRROR\" > \"$ROOT/etc/pacman.d/mirrorlist\"\n"
                  "  $CHROOT \"$ROOT\" pacman-key --init\n"
                  "  $CHROOT \"$ROOT\" pacman-key --populate archlinux\n", fp);
        } else {
            const char *ip = artix_init_pkgs(s[CAT_INIT]);
            if (ip)
                fprintf(fp,
                  "  msg 'Base: Artix via basestrap (needs artools-base on the host)'\n"
                  "  basestrap \"$ROOT\" base %s linux-firmware\n", ip);
            else
                fputs("  msg 'Base: Artix via basestrap, no init package (installed below)'\n"
                      "  basestrap \"$ROOT\" base linux-firmware\n", fp);
        }
        break;
    case P_XBPS:
        fputs("  msg 'Base: Void via xbps-install'\n"
              "  command -v xbps-install >/dev/null 2>&1 || {\n"
              "    msg 'xbps not on this host: fetching static xbps'\n"
              "    mkdir -p /tmp/lyw-xbps\n"
              "    curl -L https://repo-default.voidlinux.org/static/xbps-static-latest.x86_64-musl.tar.xz \\\n"
              "      | tar -xJ -C /tmp/lyw-xbps\n"
              "    PATH=/tmp/lyw-xbps/usr/bin:$PATH\n"
              "  }\n"
              "  XBPS_ARCH=$XARCH xbps-install -Sy -r \"$ROOT\" -R \"$REPO\" base-system\n"
              "  cp /etc/resolv.conf \"$ROOT/etc/\"\n", fp);
        break;
    case P_APK:
        fputs("  msg 'Base: Alpine via apk.static'\n"
              "  APKTOOLS=$(curl -s \"$AMIRROR/main/x86_64/\" \\\n"
              "    | grep -oE 'apk-tools-static-[^\"]+\\.apk' | head -1)\n"
              "  curl -LO \"$AMIRROR/main/x86_64/$APKTOOLS\"\n"
              "  tar -xzf \"$APKTOOLS\" sbin/apk.static\n"
              "  ./sbin/apk.static -X \"$AMIRROR/main\" -U --allow-untrusted \\\n"
              "    -p \"$ROOT\" --initdb add alpine-base\n"
              "  cp /etc/resolv.conf \"$ROOT/etc/\"\n", fp);
        if (s[CAT_INIT] == I_OPENRC)
            fputs("  ./sbin/apk.static -X \"$AMIRROR/main\" -U --allow-untrusted -p \"$ROOT\" add openrc\n", fp);
        else if (s[CAT_INIT] == I_RUNIT)
            fputs("  ./sbin/apk.static -X \"$AMIRROR/community\" -X \"$AMIRROR/main\" \\\n"
                  "    -U --allow-untrusted -p \"$ROOT\" add runit\n", fp);
        break;
    case P_PORTAGE: {
        const char *flavor = s[CAT_LIBC] == L_MUSL ? "musl"
                           : s[CAT_INIT] == I_SYSTEMD ? "systemd" : "openrc";
        fprintf(fp,
              "  msg 'Base: Gentoo stage3 (%s)'\n"
              "  GMIRROR=${GMIRROR:-https://distfiles.gentoo.org/releases/amd64/autobuilds}\n"
              "  STAGE=$(curl -s \"$GMIRROR/latest-stage3-amd64-%s.txt\" \\\n"
              "    | awk '!/^#/ && /stage3/{print $1; exit}')\n"
              "  curl -L \"$GMIRROR/$STAGE\" -o /tmp/lyw-stage3.tar.xz\n"
              "  tar -xpf /tmp/lyw-stage3.tar.xz -C \"$ROOT\" \\\n"
              "    --xattrs-include='*.*' --numeric-owner\n"
              "  cp /etc/resolv.conf \"$ROOT/etc/\"\n", flavor, flavor);
        break;
    }
    case P_NIX:
        fputs("  msg 'Base: Nix — v1 generates a flake skeleton (flake.nix, next to this script)'\n"
              "  echo 'Edit flake.nix, then: nixos-install --root \"$ROOT\" --flake .#lyw'\n"
              "  echo 'The rest of this script does not apply to the Nix path.'\n"
              "  exit 0\n", fp);
        break;
    case P_GUIX:
        fputs("  msg 'Base: Guix — v1 generates a config.scm skeleton (next to this script)'\n"
              "  echo 'From a Guix host/live system: guix system init /mnt/.../config.scm \"$ROOT\"'\n"
              "  echo 'The rest of this script does not apply to the Guix path.'\n"
              "  exit 0\n", fp);
        break;
    case P_CRUX:
        fputs("  msg 'Base: CRUX rootfs (semi-manual)'\n"
              "  : \"${CRUX_ROOTFS_URL:?Set CRUX_ROOTFS_URL to a CRUX rootfs tarball (see crux.nu handbook)}\"\n"
              "  curl -L \"$CRUX_ROOTFS_URL\" -o /tmp/lyw-crux.tar\n"
              "  tar -xpf /tmp/lyw-crux.tar -C \"$ROOT\" --numeric-owner\n", fp);
        break;
    case P_NONE:
        fputs("  msg 'Base: static BusyBox rootfs (no package manager)'\n"
              "  mkdir -p \"$ROOT\"/bin \"$ROOT\"/sbin \"$ROOT\"/etc \"$ROOT\"/proc \\\n"
              "    \"$ROOT\"/sys \"$ROOT\"/dev \"$ROOT\"/tmp \"$ROOT\"/root \"$ROOT\"/boot\n"
              "  curl -Lo \"$ROOT/bin/busybox\" \\\n"
              "    https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox\n"
              "  chmod +x \"$ROOT/bin/busybox\"\n"
              "  for a in $(\"$ROOT/bin/busybox\" --list); do\n"
              "    ln -sf busybox \"$ROOT/bin/$a\"\n"
              "  done\n"
              "  printf 'root::0:0:root:/root:/bin/sh\\n' > \"$ROOT/etc/passwd\"\n", fp);
        break;
    case P_CUSTOM:
        fputs("  msg 'Base: custom package manager'\n"
              "  [ -f \"$SCRIPTDIR/custom-pm.sh\" ] || {\n"
              "    echo 'missing custom-pm.sh next to build.sh (must define lyw_bootstrap and lyw_install)' >&2\n"
              "    exit 1\n"
              "  }\n"
              "  . \"$SCRIPTDIR/custom-pm.sh\"\n"
              "  lyw_bootstrap \"$ROOT\"\n", fp);
        break;
    }
    if (s[CAT_INIT] == I_CUSTOM)
        fputs("  msg 'Installing custom init'\n"
              "  [ -d \"$SCRIPTDIR/custom-init\" ] || {\n"
              "    echo 'missing custom-init/ next to build.sh' >&2; exit 1; }\n"
              "  cp -a \"$SCRIPTDIR/custom-init/.\" \"$ROOT/\"\n"
              "  [ -x \"$ROOT/sbin/init\" ] || echo 'WARNING: /sbin/init missing or not executable' >&2\n", fp);
    if (s[CAT_CORE] == C_BUSYBOX && s[CAT_PM] != P_NONE && s[CAT_PM] != P_APK)
        fputs("  # MANUAL: BusyBox as core userland on top of this base —\n"
              "  # install the busybox package and replace coreutils symlinks yourself.\n", fp);
    if (s[CAT_CORE] == C_TOYBOX)
        fputs("  msg 'Core userland: Toybox (static binary; the coreutils swap is yours)'\n"
              "  curl -Lo \"$ROOT/usr/local/bin/toybox\" http://landley.net/toybox/bin/toybox-x86_64\n"
              "  chmod +x \"$ROOT/usr/local/bin/toybox\"\n"
              "  # MANUAL: symlink the applets you want, e.g.:\n"
              "  #   for a in $(toybox); do ln -s toybox /usr/local/bin/$a; done\n", fp);
    fputs("}\n\n", fp);
}

/* DIY inits are installed after prep_chroot (they need a working chroot) */
void emit_altinit_call(FILE *fp, const int s[CAT_COUNT])
{
    if (s[CAT_INIT] != I_FINIT && s[CAT_INIT] != I_SYSVINIT &&
        s[CAT_INIT] != I_BUSYBOX && s[CAT_INIT] != I_SHEPHERD)
        return;
    fputs("init_install() {\n", fp);
    emit_diy_init(fp, s);
    fputs("}\n\n", fp);
}

void emit_skeletons(const int s[CAT_COUNT], const char *dir)
{
    char path[512];
    if (s[CAT_PM] == P_NIX) {
        snprintf(path, sizeof path, "%s/flake.nix", dir);
        FILE *fp = fopen(path, "w");
        if (!fp) return;
        fprintf(fp,
            "# Linux Your Way — Nix flake skeleton (v1: starting point, not a full system)\n"
            "{\n"
            "  description = \"LYW system (%s + %s)\";\n"
            "  inputs.nixpkgs.url = \"github:NixOS/nixpkgs/nixos-unstable\";\n"
            "  outputs = { self, nixpkgs }: {\n"
            "    nixosConfigurations.lyw = nixpkgs.lib.nixosSystem {\n"
            "      system = \"x86_64-linux\";\n"
            "      modules = [ ({ pkgs, ... }: {\n"
            "        boot.loader.systemd-boot.enable = true;\n"
            "        boot.loader.efi.canTouchEfiVariables = true;\n"
            "        fileSystems.\"/\".device = \"/dev/disk/by-label/lyw-root\";\n"
            "        fileSystems.\"/\".fsType = \"%s\";\n"
            "        system.stateVersion = \"25.11\";\n"
            "      }) ];\n"
            "    };\n"
            "  };\n"
            "}\n",
            categories[CAT_LIBC].opts[s[CAT_LIBC]].val,
            categories[CAT_INIT].opts[s[CAT_INIT]].val,
            categories[CAT_FS].opts[s[CAT_FS]].val);
        fclose(fp);
    }
    if (s[CAT_PM] == P_GUIX) {
        snprintf(path, sizeof path, "%s/config.scm", dir);
        FILE *fp = fopen(path, "w");
        if (!fp) return;
        fprintf(fp,
            ";; Linux Your Way — Guix System skeleton (v1: starting point)\n"
            "(use-modules (gnu))\n"
            "(use-package-modules certs)\n\n"
            "(operating-system\n"
            "  (host-name \"lyw\")\n"
            "  (timezone \"Europe/Bucharest\")\n"
            "  (bootloader (bootloader-configuration\n"
            "                (bootloader grub-efi-bootloader)\n"
            "                (targets '(\"/boot/efi\"))))\n"
            "  (file-systems (cons (file-system\n"
            "                        (device (file-system-label \"lyw-root\"))\n"
            "                        (mount-point \"/\")\n"
            "                        (type \"%s\"))\n"
            "                      %%base-file-systems))\n"
            "  (packages (cons nss-certs %%base-packages)))\n",
            categories[CAT_FS].opts[s[CAT_FS]].val);
        fclose(fp);
    }
}
