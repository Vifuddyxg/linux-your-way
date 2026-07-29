#include <stdio.h>
#include <string.h>
#include "lyw.h"

#define M(c) (1u << (c))

static int push(finding_t *f, int max, int n, int st, unsigned mask, const char *msg)
{
    if (n < max) {
        f[n].status = st;
        f[n].mask = mask;
        snprintf(f[n].msg, sizeof f[n].msg, "%s", msg);
    }
    return n + 1;
}

static int pm_is_binary(int pm)
{
    return pm == P_PACMAN || pm == P_XBPS || pm == P_APK ||
           pm == P_PORTAGE || pm == P_NIX || pm == P_GUIX || pm == P_CRUX;
}

/* package managers whose LUKS initramfs wiring build.sh actually generates */
static int pm_has_luks_path(int pm)
{
    return pm == P_PACMAN || pm == P_XBPS || pm == P_APK || pm == P_PORTAGE;
}

/* package managers with an automated graphics/network install path */
static int pm_has_pkg_path(int pm)
{
    return pm == P_PACMAN || pm == P_XBPS || pm == P_APK ||
           pm == P_PORTAGE || pm == P_CUSTOM;
}

/* inits where services are wired by hand (no packaged service scripts) */
static int init_is_diy(int init)
{
    return init == I_FINIT || init == I_SYSVINIT || init == I_BUSYBOX ||
           init == I_SHEPHERD || init == I_CUSTOM;
}

/* is the kernel actually built from source with this config? */
static int kernel_from_source(const int s[CAT_COUNT])
{
    if (s[CAT_KBUILD] != KB_PRECOMPILED) return 1;
    if (s[CAT_KERNEL] == K_CUSTOM || s[CAT_KERNEL] == K_RT) return 1;
    /* hardened/zen have packages only on some managers (see kernel_pkg) */
    if (s[CAT_KERNEL] == K_HARDENED && s[CAT_PM] != P_PACMAN) return 1;
    if (s[CAT_KERNEL] == K_ZEN && s[CAT_PM] != P_PACMAN) return 1;
    return 0;
}

static int eval_libc_pm(const int s[CAT_COUNT], finding_t *f, int max, int n)
{
    if (s[CAT_LIBC] == L_UCLIBC && s[CAT_PM] != P_NONE && s[CAT_PM] != P_CUSTOM)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_LIBC)|M(CAT_PM),
                 "no mainstream repo ships uClibc-ng; use 'none'/custom manager (buildroot territory)");
    if (s[CAT_LIBC] == L_UCLIBC && (s[CAT_PM] == P_NONE || s[CAT_PM] == P_CUSTOM))
        n = push(f, max, n, ST_MANUAL, M(CAT_LIBC),
                 "uClibc-ng: you build the whole userland against it yourself (buildroot/crosstool-ng)");
    if (s[CAT_LIBC] == L_MUSL && s[CAT_INIT] == I_SYSTEMD)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_LIBC)|M(CAT_INIT),
                 "systemd requires glibc; it does not build against musl");
    if (s[CAT_LIBC] == L_MUSL && s[CAT_PM] == P_PACMAN)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_LIBC)|M(CAT_PM),
                 "Arch/Artix binary repos are glibc-only; pacman + musl has no repo");
    if (s[CAT_LIBC] == L_GLIBC && s[CAT_PM] == P_APK)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_LIBC)|M(CAT_PM),
                 "Alpine repos are musl-only; apk + glibc has no repo");
    if (s[CAT_PM] == P_XBPS && s[CAT_INIT] == I_SYSTEMD)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_PM)|M(CAT_INIT),
                 "Void ships no systemd packages; XBPS + systemd is a dead end");
    if (s[CAT_PM] == P_APK && s[CAT_INIT] == I_SYSTEMD)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_PM)|M(CAT_INIT),
                 "Alpine ships no systemd packages");
    if (s[CAT_LIBC] == L_MUSL && s[CAT_PM] == P_GUIX)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_LIBC)|M(CAT_PM),
                 "Guix System is glibc-based");
    if (s[CAT_LIBC] == L_MUSL && s[CAT_PM] == P_CRUX)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_LIBC)|M(CAT_PM),
                 "CRUX is glibc-based");
    if (s[CAT_NET] == N_NETWORKD && s[CAT_INIT] != I_SYSTEMD)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_NET)|M(CAT_INIT),
                 "systemd-networkd is part of systemd; pick another init or network stack");
    return n;
}

static int eval_boot(const int s[CAT_COUNT], finding_t *f, int max, int n)
{
    int bios = s[CAT_FIRMWARE] == FW_BIOS;

    if (bios && s[CAT_BOOT] == B_SDBOOT)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_FIRMWARE)|M(CAT_BOOT),
                 "systemd-boot is UEFI-only");
    if (bios && s[CAT_BOOT] == B_REFIND)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_FIRMWARE)|M(CAT_BOOT),
                 "rEFInd is UEFI-only");
    if (bios && s[CAT_BOOT] == B_EFISTUB)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_FIRMWARE)|M(CAT_BOOT),
                 "direct EFI boot needs UEFI firmware");
    if (!bios && s[CAT_BOOT] == B_SYSLINUX)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_FIRMWARE)|M(CAT_BOOT),
                 "the generated Syslinux path is BIOS/extlinux only; on UEFI pick another loader");
    if (s[CAT_BOOT] == B_SYSLINUX && s[CAT_FS] == F_BTRFS)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_BOOT)|M(CAT_FS),
                 "extlinux cannot read zstd-compressed btrfs; use ext4 (or LUKS's ext4 /boot)");
    if (bios && s[CAT_BOOT] == B_LIMINE)
        n = push(f, max, n, ST_MANUAL, M(CAT_FIRMWARE)|M(CAT_BOOT),
                 "Limine v8+ reads only FAT: on BIOS you must give it a FAT /boot yourself");
    if (s[CAT_BOOT] == B_EFISTUB)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_BOOT),
                 "direct EFI: no menu, no fallback; kernel updates must re-copy to the ESP");
    if (s[CAT_BOOT] == B_EFISTUB && s[CAT_CRYPT] == E_LUKS)
        n = push(f, max, n, ST_MANUAL, M(CAT_BOOT)|M(CAT_CRYPT),
                 "EFISTUB + LUKS: verify the initrd= path in the efibootmgr entry after install");
    if (s[CAT_BOOT] == B_SDBOOT && s[CAT_INIT] != I_SYSTEMD)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_BOOT)|M(CAT_INIT),
                 "systemd-boot works without systemd as init, but it is a less-trodden path");
    if (!pm_has_pkg_path(s[CAT_PM]) &&
        (s[CAT_BOOT] == B_REFIND || s[CAT_BOOT] == B_SYSLINUX))
        n = push(f, max, n, ST_MANUAL, M(CAT_BOOT)|M(CAT_PM),
                 "no package source for this bootloader here; install it into the target yourself");

    if (s[CAT_SECBOOT] == SB_SBCTL) {
        if (bios)
            n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_SECBOOT)|M(CAT_FIRMWARE),
                     "Secure Boot is a UEFI feature");
        else if (s[CAT_PM] != P_PACMAN)
            n = push(f, max, n, ST_MANUAL, M(CAT_SECBOOT)|M(CAT_PM),
                     "sbctl signing is automated on the pacman base only; elsewhere sign by hand");
        else if (s[CAT_BOOT] == B_GRUB)
            n = push(f, max, n, ST_MANUAL, M(CAT_SECBOOT)|M(CAT_BOOT),
                     "GRUB under Secure Boot needs shim or an all-modules-embedded signed image; sbctl alone is not enough");
        else
            n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_SECBOOT),
                     "sbctl: firmware must be in setup mode; enrolling keys can brick badly-made UEFIs");
    }
    return n;
}

static int eval_kernel(const int s[CAT_COUNT], finding_t *f, int max, int n)
{
    int src = kernel_from_source(s);

    if (s[CAT_KERNEL] == K_CUSTOM && s[CAT_KBUILD] == KB_PRECOMPILED)
        n = push(f, max, n, ST_MANUAL, M(CAT_KERNEL)|M(CAT_KBUILD),
                 "a custom kernel cannot be precompiled; pick a local build mode");
    if (s[CAT_KERNEL] == K_ZEN && s[CAT_PM] != P_PACMAN &&
        s[CAT_KBUILD] == KB_PRECOMPILED)
        n = push(f, max, n, ST_MANUAL, M(CAT_KERNEL)|M(CAT_PM),
                 "no zen kernel package in this repo; it will be built from source (hybrid)");
    if (s[CAT_KERNEL] == K_HARDENED && s[CAT_PM] != P_PACMAN)
        n = push(f, max, n, ST_MANUAL, M(CAT_KERNEL)|M(CAT_PM),
                 "linux-hardened is packaged on Arch/Artix; here it becomes a source build with hardening flags (not the full patchset)");
    if (s[CAT_KERNEL] == K_RT)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_KERNEL),
                 "PREEMPT_RT: source build with CONFIG_PREEMPT_RT=y (needs kernel >= 6.12)");

    if (src) {
        if (s[CAT_KMOD] == KM_MONO && s[CAT_KBUILD] == KB_LOCAL)
            n = push(f, max, n, ST_MANUAL, M(CAT_KMOD)|M(CAT_KBUILD),
                     "monolithic + default config: =m options silently become =n; use menuconfig or your own .config");
        if (s[CAT_KINITRD] == KI_BUILTIN)
            n = push(f, max, n, ST_MANUAL, M(CAT_KINITRD),
                     "built-in initramfs: you must point CONFIG_INITRAMFS_SOURCE at your own cpio (menuconfig)");
        if (s[CAT_KINITRD] == KI_NONE)
            n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_KINITRD),
                     "no initramfs: root fs and disk drivers must be =y or the kernel cannot mount root");
        if (s[CAT_KINITRD] == KI_NONE && s[CAT_CRYPT] == E_LUKS)
            n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_KINITRD)|M(CAT_CRYPT),
                     "LUKS root cannot be unlocked without an initramfs");
    } else {
        if (s[CAT_KMOD] == KM_MONO)
            n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_KMOD)|M(CAT_KBUILD),
                     "distro kernel packages are modular; a monolithic kernel needs a source build");
        if (s[CAT_KINITRD] != KI_SEPARATE)
            n = push(f, max, n, ST_MANUAL, M(CAT_KINITRD)|M(CAT_KBUILD),
                     "precompiled kernels generate their initramfs via package hooks; this choice only applies to source builds");
    }
    return n;
}

static int eval_userland(const int s[CAT_COUNT], finding_t *f, int max, int n)
{
    if (s[CAT_CORE] == C_BUSYBOX && s[CAT_INIT] == I_SYSTEMD)
        n = push(f, max, n, ST_MANUAL, M(CAT_CORE)|M(CAT_INIT),
                 "systemd expects a full userland; BusyBox core needs manual surgery");
    if (s[CAT_CORE] == C_BUSYBOX && pm_is_binary(s[CAT_PM]) && s[CAT_PM] != P_APK)
        n = push(f, max, n, ST_MANUAL, M(CAT_CORE)|M(CAT_PM),
                 "only Alpine ships BusyBox as the core userland; swapping coreutils out is manual");
    if (s[CAT_CORE] == C_TOYBOX)
        n = push(f, max, n, ST_MANUAL, M(CAT_CORE),
                 "no distro ships Toybox as core: a static toybox is installed, the coreutils swap is yours");
    if (s[CAT_SHELL] == SH_ASH && s[CAT_CORE] == C_GNU)
        n = push(f, max, n, ST_OK, M(CAT_SHELL),
                 "ash comes from busybox: the busybox package is installed alongside GNU coreutils");
    if (s[CAT_SHELL] != SH_BASH && !pm_has_pkg_path(s[CAT_PM]) &&
        s[CAT_PM] != P_NIX && s[CAT_PM] != P_GUIX)
        n = push(f, max, n, ST_MANUAL, M(CAT_SHELL)|M(CAT_PM),
                 "no package source for this shell here; install it yourself");
    if (s[CAT_TC] == TC_LLVM && s[CAT_PM] == P_PORTAGE)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_TC)|M(CAT_PM),
                 "LLVM-only Gentoo is a real but sharp-edged profile; the stage3 still carries GCC");
    if (s[CAT_TC] != TC_GNU && !pm_has_pkg_path(s[CAT_PM]))
        n = push(f, max, n, ST_MANUAL, M(CAT_TC)|M(CAT_PM),
                 "no package source for the LLVM toolchain here");
    if (s[CAT_TCPROFILE] == TP_PERF)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_TCPROFILE),
                 "-march=native binaries will not run on older CPUs; flags apply to what is compiled locally");
    if (s[CAT_TCPROFILE] == TP_HARDENED && s[CAT_PM] != P_PORTAGE)
        n = push(f, max, n, ST_MANUAL, M(CAT_TCPROFILE)|M(CAT_PM),
                 "hardened flags reach only local builds; binary packages keep their own flags (Gentoo can rebuild everything)");

    /* security layer */
    if (s[CAT_SECURITY] == SEC_SELINUX && s[CAT_PM] != P_PORTAGE)
        n = push(f, max, n, ST_INCOMPATIBLE, M(CAT_SECURITY)|M(CAT_PM),
                 "SELinux needs rebuilt/labeled packages; only Gentoo has real support");
    if (s[CAT_SECURITY] == SEC_SELINUX && s[CAT_PM] == P_PORTAGE)
        n = push(f, max, n, ST_MANUAL, M(CAT_SECURITY),
                 "SELinux on Gentoo: switch to an SELinux profile, install policies, relabel — all manual");
    if (s[CAT_SECURITY] == SEC_APPARMOR && s[CAT_PM] == P_XBPS)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_SECURITY)|M(CAT_PM),
                 "AppArmor on Void: package exists, profile coverage is thin");
    if (s[CAT_SECURITY] == SEC_APPARMOR &&
        (s[CAT_PM] == P_APK || !pm_has_pkg_path(s[CAT_PM])))
        n = push(f, max, n, ST_MANUAL, M(CAT_SECURITY)|M(CAT_PM),
                 "no automated AppArmor path here; install profiles and lsm= by hand");
    if (s[CAT_SECURITY] == SEC_APPARMOR && kernel_from_source(s) &&
        s[CAT_KBUILD] != KB_OWNCONFIG)
        n = push(f, max, n, ST_MANUAL, M(CAT_SECURITY)|M(CAT_KBUILD),
                 "source-built kernels need CONFIG_SECURITY_APPARMOR=y (menuconfig) for AppArmor");
    if (s[CAT_SECURITY] >= SEC_FIREWALL && s[CAT_SECURITY] != SEC_SELINUX &&
        !pm_has_pkg_path(s[CAT_PM]))
        n = push(f, max, n, ST_MANUAL, M(CAT_SECURITY)|M(CAT_PM),
                 "no package source for nftables here; the ruleset is written but install it yourself");
    if (s[CAT_SECURITY] >= SEC_FIREWALL && s[CAT_SECURITY] != SEC_SELINUX &&
        init_is_diy(s[CAT_INIT]))
        n = push(f, max, n, ST_MANUAL, M(CAT_SECURITY)|M(CAT_INIT),
                 "no packaged nftables service for this init; load the ruleset from your rc scripts");
    if (s[CAT_PRIVESC] != PE_NONE && !pm_has_pkg_path(s[CAT_PM]) &&
        s[CAT_PM] != P_NIX && s[CAT_PM] != P_GUIX)
        n = push(f, max, n, ST_MANUAL, M(CAT_PRIVESC)|M(CAT_PM),
                 "no package source for sudo/doas here; install it yourself");
    return n;
}

static int eval_init_pm(const int s[CAT_COUNT], finding_t *f, int max, int n)
{
    if (s[CAT_PM] == P_XBPS && s[CAT_INIT] == I_DINIT)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM)|M(CAT_INIT),
                 "Void is runit-native; dinit service files must be written by hand");
    if (s[CAT_PM] == P_XBPS && s[CAT_INIT] == I_OPENRC)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM)|M(CAT_INIT),
                 "Void is runit-native; OpenRC on Void is a manual conversion");
    if (s[CAT_PM] == P_APK && s[CAT_INIT] == I_DINIT)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM)|M(CAT_INIT),
                 "Alpine services are OpenRC scripts; dinit needs hand-written services");
    if (s[CAT_PM] == P_PORTAGE &&
        (s[CAT_INIT] == I_DINIT || s[CAT_INIT] == I_RUNIT))
        n = push(f, max, n, ST_MANUAL, M(CAT_PM)|M(CAT_INIT),
                 "no official stage3 for this init; start from the OpenRC stage3 and swap manually");
    if (s[CAT_PM] == P_CRUX)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM),
                 "CRUX bootstrap is semi-manual (set CRUX_ROOTFS_URL); ports compile via pkgmk");
    if (s[CAT_PM] == P_CRUX && s[CAT_INIT] != I_CUSTOM)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM)|M(CAT_INIT),
                 "CRUX uses its own BSD-style rc scripts; the chosen init needs manual wiring");
    if (s[CAT_PM] == P_CUSTOM)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM),
                 "custom manager: provide lyw-out/custom-pm.sh defining lyw_bootstrap + lyw_install");
    if (s[CAT_INIT] == I_CUSTOM)
        n = push(f, max, n, ST_MANUAL, M(CAT_INIT),
                 "custom init: provide lyw-out/custom-init/ (copied into /, must yield /sbin/init)");
    if (s[CAT_INIT] == I_S6 && s[CAT_PM] != P_PACMAN)
        n = push(f, max, n, ST_MANUAL, M(CAT_INIT)|M(CAT_PM),
                 "packaged s6 service scripts exist mainly on Artix; elsewhere you write them by hand");
    if (s[CAT_INIT] == I_66 && s[CAT_PM] != P_PACMAN)
        n = push(f, max, n, ST_MANUAL, M(CAT_INIT)|M(CAT_PM),
                 "66 is packaged for Artix; on other bases you build and wire it yourself");
    if (s[CAT_INIT] == I_FINIT)
        n = push(f, max, n, ST_MANUAL, M(CAT_INIT),
                 "Finit is not packaged on these bases: built from source (libuev+libite+finit); REVIEW /etc/finit.conf");
    if (s[CAT_INIT] == I_SYSVINIT)
        n = push(f, max, n, ST_MANUAL, M(CAT_INIT),
                 "sysvinit is built from source; you own /etc/inittab and the rc scripts");
    if (s[CAT_INIT] == I_SHEPHERD && s[CAT_PM] != P_GUIX)
        n = push(f, max, n, ST_MANUAL, M(CAT_INIT)|M(CAT_PM),
                 "Shepherd outside Guix System is a source build (Guile) with hand-wired services");
    if (s[CAT_NET] != N_NONE && init_is_diy(s[CAT_INIT]) && s[CAT_INIT] != I_CUSTOM)
        n = push(f, max, n, ST_MANUAL, M(CAT_NET)|M(CAT_INIT),
                 "no packaged services for this init: network daemons start from your inittab/rc scripts");
    if (s[CAT_GFX] != G_NONE && init_is_diy(s[CAT_INIT]))
        n = push(f, max, n, ST_MANUAL, M(CAT_GFX)|M(CAT_INIT),
                 "the display manager has no service integration under this init; start it from your rc scripts");
    if (s[CAT_PM] == P_NONE && s[CAT_INSTALL] != IN_SOURCE)
        n = push(f, max, n, ST_MANUAL, M(CAT_PM)|M(CAT_INSTALL),
                 "no package manager: base is a static BusyBox rootfs, updates are manual");
    if (s[CAT_LIBC] == L_MUSL && s[CAT_PM] == P_NIX)
        n = push(f, max, n, ST_MANUAL, M(CAT_LIBC)|M(CAT_PM),
                 "nixpkgs on musl (pkgsMusl) has limited binary cache coverage");
    if (s[CAT_CRYPT] == E_LUKS && !pm_has_luks_path(s[CAT_PM]))
        n = push(f, max, n, ST_MANUAL, M(CAT_CRYPT)|M(CAT_PM),
                 "LUKS initramfs unlocking is not generated for this manager; wire cryptsetup yourself");
    if (s[CAT_NET] != N_NONE && !pm_has_pkg_path(s[CAT_PM]) &&
        s[CAT_PM] != P_NIX && s[CAT_PM] != P_GUIX)
        n = push(f, max, n, ST_MANUAL, M(CAT_NET)|M(CAT_PM),
                 "no package source for network tools here; BusyBox udhcpc or manual install");
    if (s[CAT_GFX] != G_NONE && !pm_has_pkg_path(s[CAT_PM]) &&
        s[CAT_PM] != P_NIX && s[CAT_PM] != P_GUIX)
        n = push(f, max, n, ST_MANUAL, M(CAT_GFX)|M(CAT_PM),
                 "no automated graphics install for this package manager");
    if (s[CAT_INSTALL] == IN_SOURCE)
        n = push(f, max, n, ST_MANUAL, M(CAT_INSTALL),
                 "full source build: expect hours of compile time");
    return n;
}

static int eval_experimental(const int s[CAT_COUNT], finding_t *f, int max, int n)
{
    if (s[CAT_PM] == P_APK && s[CAT_INIT] == I_RUNIT)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_PM)|M(CAT_INIT),
                 "runit exists in Alpine but service coverage is thin");
    if (s[CAT_INIT] == I_66 && s[CAT_PM] == P_PACMAN)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_INIT)|M(CAT_PM),
                 "66 lives in the Artix galaxy repo; service coverage is thinner than dinit/runit");
    if (s[CAT_INIT] == I_BUSYBOX)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_INIT),
                 "BusyBox init: one tiny inittab, no supervision — daemons start from boot scripts");
    if (s[CAT_PM] == P_NIX)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_PM),
                 "Nix as the main system manager: v1 backend generates a flake skeleton only");
    if (s[CAT_PM] == P_GUIX && s[CAT_INIT] != I_SHEPHERD)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_PM)|M(CAT_INIT),
                 "Guix: v1 generates a config.scm skeleton; Guix System uses Shepherd, not your init pick");
    if (s[CAT_PM] == P_GUIX && s[CAT_INIT] == I_SHEPHERD)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_PM),
                 "Guix + Shepherd is the native pairing; v1 still only generates the config.scm skeleton");
    if (s[CAT_PM] == P_PORTAGE && s[CAT_LIBC] == L_MUSL)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_PM)|M(CAT_LIBC),
                 "musl stage3 exists but package coverage is smaller");
    if (s[CAT_PM] == P_PORTAGE && s[CAT_INSTALL] == IN_BINARY)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_PM)|M(CAT_INSTALL),
                 "binary-only Gentoo relies on official binhost coverage");
    if (s[CAT_GFX] == G_NIRI)
        n = push(f, max, n, ST_EXPERIMENTAL, M(CAT_GFX),
                 "Niri is a young compositor; expect rough edges");
    return n;
}

int compat_eval(const int s[CAT_COUNT], finding_t *f, int max)
{
    int n = 0;
    n = eval_libc_pm(s, f, max, n);
    n = eval_boot(s, f, max, n);
    n = eval_kernel(s, f, max, n);
    n = eval_userland(s, f, max, n);
    n = eval_init_pm(s, f, max, n);
    n = eval_experimental(s, f, max, n);
    return n;
}

int compat_overall(const int s[CAT_COUNT])
{
    finding_t f[MAX_FINDINGS];
    int n = compat_eval(s, f, MAX_FINDINGS), worst = ST_OK;
    if (n > MAX_FINDINGS) n = MAX_FINDINGS;
    for (int i = 0; i < n; i++)
        if (f[i].status > worst) worst = f[i].status;
    return worst;
}

int compat_option_status(const int sel[CAT_COUNT], int cat, int opt,
                         unsigned answered)
{
    int s[CAT_COUNT];
    memcpy(s, sel, sizeof s);
    s[cat] = opt;

    finding_t f[MAX_FINDINGS];
    int n = compat_eval(s, f, MAX_FINDINGS), worst = ST_OK;
    if (n > MAX_FINDINGS) n = MAX_FINDINGS;
    for (int i = 0; i < n; i++) {
        if (!(f[i].mask & M(cat))) continue;
        if (f[i].mask & ~M(cat) & ~answered) continue; /* involves an unanswered layer */
        if (f[i].status > worst) worst = f[i].status;
    }
    return worst;
}

const char *status_glyph(int st)
{
    switch (st) {
    case ST_OK:           return "✓";
    case ST_EXPERIMENTAL: return "◐";
    case ST_MANUAL:       return "⚠";
    default:              return "✗";
    }
}

const char *status_word(int st)
{
    switch (st) {
    case ST_OK:           return "stable";
    case ST_EXPERIMENTAL: return "experimental";
    case ST_MANUAL:       return "manual-config-needed";
    default:              return "incompatible";
    }
}
