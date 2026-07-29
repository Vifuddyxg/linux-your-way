#include <string.h>
#include "lyw.h"

syscfg_t g_syscfg;

static const option_t o_firmware[] = {
    { "UEFI", "uefi", "Modern firmware boot (ESP + EFI binaries). Auto-detected on this machine." },
    { "BIOS", "bios", "Legacy boot (MBR code / GPT BIOS-boot partition). Old machines and some VMs." },
};

static const option_t o_libc[] = {
    { "glibc",     "glibc",  "Maximum binary compatibility. Works with every binary repo." },
    { "musl",      "musl",   "Small and simple. Best with Void-musl/Alpine repos or source builds." },
    { "uClibc-ng", "uclibc", "Embedded-focused libc. No mainstream distro repo ships it — buildroot territory." },
};

static const option_t o_core[] = {
    { "GNU Coreutils", "gnu",     "The full GNU userland: coreutils, bash, grep, sed, tar." },
    { "BusyBox",       "busybox", "One static binary provides the whole core userland." },
    { "Toybox",        "toybox",  "BSD-licensed multiplexer (Android's userland). Installed as a static binary; you wire the symlinks." },
};

static const option_t o_shell[] = {
    { "Bash", "bash", "The GNU shell. The default nearly everywhere; most scripts assume it." },
    { "Dash", "dash", "Small, fast POSIX /bin/sh. Great for scripts, spartan interactively." },
    { "Ash",  "ash",  "BusyBox's shell. Tiny; comes from the busybox binary." },
    { "Zsh",  "zsh",  "Rich interactive shell (completion, themes)." },
    { "Fish", "fish", "Friendly interactive shell. Not POSIX-compatible." },
};

static const option_t o_init[] = {
    { "dinit",        "dinit",        "Modern, fast, dependency-based init. Artix ships it natively." },
    { "runit",        "runit",        "Tiny and rock solid. Native on Void, supported on Artix." },
    { "OpenRC",       "openrc",       "Gentoo/Alpine/Artix service manager. The classic non-systemd choice." },
    { "s6",           "s6",           "skarnet's supervision suite (s6 + s6-rc). Artix ships it natively." },
    { "66",           "66",           "Service manager built on s6, from Obarun. Artix galaxy package." },
    { "Finit",        "finit",        "Fast SysV-style init with supervision (troglobit). Built from source." },
    { "systemd",      "systemd",      "The mainstream choice. Requires glibc. Most desktop-friendly." },
    { "SysVinit",     "sysvinit",     "The classic UNIX init: /etc/inittab + rc scripts. Built from source." },
    { "BusyBox init", "busybox-init", "BusyBox's built-in init: one tiny /etc/inittab, no service manager." },
    { "GNU Shepherd", "shepherd",     "Guile-based init from GNU. Native on Guix System, manual elsewhere." },
    { "Your own",     "custom",       "Bring your own /sbin/init: place it in lyw-out/custom-init/." },
};

static const option_t o_pm[] = {
    { "pacman",           "pacman",  "Arch/Artix binary repos: huge, fast, rolling." },
    { "XBPS",             "xbps",    "Void Linux repos. Binary repos for BOTH glibc and musl." },
    { "apk",              "apk",     "Alpine's manager. musl-only, tiny, very fast." },
    { "Portage",          "portage", "Gentoo: stage3 base + source builds with USE flags." },
    { "Nix",              "nix",     "Declarative store-based packages, rollbacks, generations." },
    { "Guix",             "guix",    "GNU's declarative manager (Scheme). Shepherd-based system." },
    { "pkgutils (CRUX)",  "crux",    "CRUX ports: hands-on, BSD-flavoured, compile via pkgmk." },
    { "none",             "none",    "No package manager. Static BusyBox base, everything manual." },
    { "Your own",         "custom",  "Your own manager: define lyw_bootstrap/lyw_install in lyw-out/custom-pm.sh." },
};

static const option_t o_kernel[] = {
    { "Linux LTS",      "lts",      "Long-term support kernel. Boring and reliable." },
    { "Linux (latest)", "vanilla",  "Newest stable kernel from upstream." },
    { "Linux Zen",      "zen",      "Desktop/gaming tuned: better latency and responsiveness." },
    { "Linux hardened", "hardened", "Security-hardened kernel. Packaged on Arch/Artix; elsewhere a hardened-flag source build." },
    { "PREEMPT_RT",     "rt",       "Real-time preemption (mainline since 6.12). Built from source with PREEMPT_RT=y." },
    { "Custom / fork",  "custom",   "Your own tree: any git URL + tag, compiled locally. Set them later in Settings." },
};

static const option_t o_kbuild[] = {
    { "Precompiled package",     "precompiled", "Take the distro kernel package. Fastest." },
    { "Compile for this machine","local",       "Build locally with the default config + your toolchain profile flags." },
    { "make menuconfig",         "menuconfig",  "Open menuconfig during the build to tune everything." },
    { "Use your .config",        "own-config",  "Drop your .config next to build.sh and it gets used." },
};

static const option_t o_kcc[] = {
    { "GCC",   "gcc",   "The default kernel compiler. Always works." },
    { "Clang", "clang", "Build with LLVM=1 (clang + lld). Upstream-supported; host needs clang/lld." },
};

static const option_t o_kcompress[] = {
    { "zstd", "zstd", "Fast decompression, good ratio. The modern default." },
    { "gzip", "gzip", "The classic. Universally supported." },
    { "xz",   "xz",   "Smallest image, slowest boot decompression." },
    { "lz4",  "lz4",  "Fastest boot, biggest image." },
};

static const option_t o_kmod[] = {
    { "With modules", "modules",    "Normal modular kernel; drivers load on demand." },
    { "Monolithic",   "monolithic", "CONFIG_MODULES=n: every driver built in. You must configure ALL needed drivers yourself." },
};

static const option_t o_kinitrd[] = {
    { "Separate initramfs", "separate", "Standard: an initramfs file next to the kernel (generated when tools exist)." },
    { "Built into kernel",  "builtin",  "CONFIG_INITRAMFS_SOURCE: cpio embedded in the image. You provide the cpio (menuconfig)." },
    { "None",               "none",     "No initramfs: root fs + disk drivers must be =y and root= resolvable by the kernel." },
};

static const option_t o_tc[] = {
    { "GCC + Binutils",     "gnu",  "The GNU toolchain. What every distro is built with." },
    { "Clang + LLVM + LLD", "llvm", "The LLVM toolchain installed in the target." },
    { "Both",               "both", "GCC and LLVM side by side; pick per project." },
};

static const option_t o_tcprofile[] = {
    { "Max compatibility", "compat",   "-O2, no -march: binaries run on any x86_64. The sane default." },
    { "Optimize for size", "size",     "-Os (and CC_OPTIMIZE_FOR_SIZE for source kernels)." },
    { "Performance",       "perf",     "-O3 -march=native. Binaries tied to this CPU family. LTO/PGO stay per-project." },
    { "Debug",             "debug",    "-Og -g, symbols kept. Bigger and slower; for development." },
    { "Hardened",          "hardened", "FORTIFY, strong stack protector, RELRO/PIE where the base supports it." },
};

static const option_t o_gfx[] = {
    { "None (TTY only)",  "none",    "No graphical stack. Servers, appliances, minimal systems." },
    { "Openbox (X11)",    "openbox", "Classic light stacking WM on Xorg. Greeter + session included." },
    { "XFCE (X11)",       "xfce",    "Full lightweight desktop environment on Xorg." },
    { "Sway (Wayland)",   "sway",    "i3-compatible Wayland compositor. Mature and stable." },
    { "Niri (Wayland)",   "niri",    "Scrollable-tiling Wayland compositor. Young but exciting." },
    { "nvwm (X11)",       "nvwm",    "Light BSP tiling WM, built from its git source during install." },
};

static const option_t o_boot[] = {
    { "GRUB",         "grub",         "Works everywhere: UEFI, BIOS, every filesystem. Dual-boot via os-prober." },
    { "Limine",       "limine",       "Modern, simple, fast. UEFI first-class; BIOS needs a FAT /boot (v8+ reads only FAT)." },
    { "systemd-boot", "systemd-boot", "Minimal UEFI boot menu. UEFI only." },
    { "rEFInd",       "refind",       "Graphical UEFI manager; auto-detects kernels and other OSes. UEFI only." },
    { "Syslinux",     "syslinux",     "extlinux on the boot filesystem. BIOS only here; no compressed btrfs." },
    { "Direct EFI",   "efistub",      "No bootloader: firmware boots the kernel via efibootmgr. UEFI only, no menu." },
};

static const option_t o_secboot[] = {
    { "Off",              "off",   "Secure Boot disabled or not enforced. The default." },
    { "sbctl (own keys)", "sbctl", "Enroll your own keys with sbctl and sign loader + kernel. Firmware must be in setup mode." },
};

static const option_t o_fs[] = {
    { "ext4",  "ext4",  "The safe default. Simple and proven." },
    { "Btrfs", "btrfs", "zstd compression, @/@home subvolumes, snapshot-ready." },
};

static const option_t o_crypt[] = {
    { "None",  "none",  "Unencrypted disk. Simplest setup." },
    { "LUKS2", "luks2", "Encrypted root (ESP + /boot stay clear). Passphrase at boot." },
};

static const option_t o_net[] = {
    { "dhcpcd",          "dhcpcd",          "Just DHCP on ethernet. Small and simple." },
    { "NetworkManager",  "networkmanager",  "Full-featured: Wi-Fi, VPN, desktop applets." },
    { "iwd + dhcpcd",    "iwd",             "Modern Wi-Fi daemon + DHCP. The Finix-style stack." },
    { "systemd-networkd","systemd-networkd","Declarative networking, part of systemd." },
    { "None",            "none",            "No network configuration. Air-gapped or manual." },
};

static const option_t o_login[] = {
    { "Ask for login",   "password",       "Normal login prompt; passwords set from your Settings answers." },
    { "Root autologin",  "root-autologin", "Boot straight into a root shell, no password. Testing/appliances only." },
};

static const option_t o_privesc[] = {
    { "sudo",         "sudo", "The standard. wheel group gets full sudo access." },
    { "doas (BSD)",   "doas", "OpenBSD's small, auditable sudo alternative. 'permit persist :wheel'." },
    { "None (su only)","none", "No privilege escalation tool; use su with the root password." },
};

static const option_t o_security[] = {
    { "None",                 "none",     "No extra security layer. You harden it yourself." },
    { "Firewall (nftables)",  "firewall", "nftables ruleset: drop inbound, allow lo/established/ICMP." },
    { "Firewall + hardening", "harden",   "nftables + BSD-flavoured sysctl lockdown (kptr, dmesg, ptrace, bpf)." },
    { "AppArmor",             "apparmor", "MAC profiles + firewall + sysctl. Kernel needs lsm= on the cmdline." },
    { "SELinux",              "selinux",  "Full MAC. Real support only on Gentoo (policies + relabel); manual." },
};

static const option_t o_install[] = {
    { "Binary",  "binary", "Everything from binary repos. Fastest install." },
    { "Source",  "source", "Everything compiled locally. Hours of build time." },
    { "Hybrid",  "hybrid", "Binary base + local builds where needed. Recommended." },
};

const category_t categories[CAT_COUNT] = {
    [CAT_FIRMWARE]  = { "Firmware",          "firmware",        o_firmware, 2 },
    [CAT_LIBC]      = { "C library",         "libc",            o_libc,     3 },
    [CAT_CORE]      = { "Core userland",     "core",            o_core,     3 },
    [CAT_SHELL]     = { "Shell",             "shell",           o_shell,    5 },
    [CAT_INIT]      = { "Init system",       "init",            o_init,    11 },
    [CAT_PM]        = { "Package manager",   "package_manager", o_pm,       9 },
    [CAT_KERNEL]    = { "Kernel",            "kernel",          o_kernel,   6 },
    [CAT_KBUILD]    = { "Kernel build",      "kernel_build",    o_kbuild,   4 },
    [CAT_KCC]       = { "Kernel compiler",   "kernel_compiler", o_kcc,      2 },
    [CAT_KCOMPRESS] = { "Kernel compression","kernel_compress", o_kcompress,4 },
    [CAT_KMOD]      = { "Kernel modules",    "kernel_modules",  o_kmod,     2 },
    [CAT_KINITRD]   = { "Initramfs",         "initramfs",       o_kinitrd,  3 },
    [CAT_TC]        = { "Toolchain",         "toolchain",       o_tc,       3 },
    [CAT_TCPROFILE] = { "Build profile",     "build_profile",   o_tcprofile,5 },
    [CAT_GFX]       = { "Graphics",          "graphics",        o_gfx,      6 },
    [CAT_BOOT]      = { "Bootloader",        "bootloader",      o_boot,     6 },
    [CAT_SECBOOT]   = { "Secure Boot",       "secure_boot",     o_secboot,  2 },
    [CAT_FS]        = { "Root filesystem",   "filesystem",      o_fs,       2 },
    [CAT_CRYPT]     = { "Disk encryption",   "encryption",      o_crypt,    2 },
    [CAT_NET]       = { "Network",           "network",         o_net,      5 },
    [CAT_LOGIN]     = { "Login",             "login",           o_login,    2 },
    [CAT_PRIVESC]   = { "Privilege escalation","privesc",       o_privesc,  3 },
    [CAT_SECURITY]  = { "Security",          "security",        o_security, 5 },
    [CAT_INSTALL]   = { "Install method",    "install_method",  o_install,  3 },
};

void config_defaults(int sel[CAT_COUNT])
{
    sel[CAT_FIRMWARE]  = FW_UEFI;
    sel[CAT_LIBC]      = L_GLIBC;
    sel[CAT_CORE]      = C_GNU;
    sel[CAT_SHELL]     = SH_BASH;
    sel[CAT_INIT]      = I_DINIT;
    sel[CAT_PM]        = P_PACMAN;
    sel[CAT_KERNEL]    = K_LTS;
    sel[CAT_KBUILD]    = KB_PRECOMPILED;
    sel[CAT_KCC]       = KC_GCC;
    sel[CAT_KCOMPRESS] = KZ_ZSTD;
    sel[CAT_KMOD]      = KM_MODULES;
    sel[CAT_KINITRD]   = KI_SEPARATE;
    sel[CAT_TC]        = TC_GNU;
    sel[CAT_TCPROFILE] = TP_COMPAT;
    sel[CAT_GFX]       = G_NONE;
    sel[CAT_BOOT]      = B_GRUB;
    sel[CAT_SECBOOT]   = SB_OFF;
    sel[CAT_FS]        = F_EXT4;
    sel[CAT_CRYPT]     = E_NONE;
    sel[CAT_NET]       = N_DHCPCD;
    sel[CAT_LOGIN]     = LG_PASSWORD;
    sel[CAT_PRIVESC]   = PE_SUDO;
    sel[CAT_SECURITY]  = SEC_NONE;
    sel[CAT_INSTALL]   = IN_HYBRID;
}

void syscfg_defaults(syscfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->disk_mode = DM_ASK;
    strcpy(c->hostname, "lyw");
    strcpy(c->timezone, "Europe/Bucharest");
    strcpy(c->locale,   "en_US.UTF-8");
    strcpy(c->keymap,   "us");
    c->boot_timeout = 3;
    c->swap_gib = 4;
}
