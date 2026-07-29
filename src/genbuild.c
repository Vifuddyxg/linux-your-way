#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "gen.h"

static void emit_header_vars(FILE *fp, const int s[CAT_COUNT])
{
    const char *chroot_cmd =
        s[CAT_PM] == P_PACMAN ? (s[CAT_INIT] == I_SYSTEMD ? "arch-chroot" : "artix-chroot")
      : s[CAT_PM] == P_XBPS   ? "xchroot"
      :                         "chroot";
    fprintf(fp, "CHROOT=\"${CHROOT:-%s}\"\n", chroot_cmd);
    fputs("# fall back to plain chroot (+ prep_chroot mounts) if the helper is absent\n"
          "command -v \"$CHROOT\" >/dev/null 2>&1 || CHROOT=chroot\n", fp);
    if (s[CAT_PM] == P_XBPS)
        fprintf(fp, "REPO=%s\nXARCH=%s\n",
                s[CAT_LIBC] == L_MUSL ? "https://repo-default.voidlinux.org/current/musl"
                                      : "https://repo-default.voidlinux.org/current",
                s[CAT_LIBC] == L_MUSL ? "x86_64-musl" : "x86_64");
    if (s[CAT_PM] == P_APK)
        fputs("AMIRROR=${AMIRROR:-https://dl-cdn.alpinelinux.org/alpine/latest-stable}\n", fp);
    if (s[CAT_PM] == P_PACMAN && s[CAT_INIT] == I_SYSTEMD)
        fputs("ARCHMIRROR=${ARCHMIRROR:-https://geo.mirror.pkgbuild.com}\n", fp);
    if (s[CAT_CRYPT] == E_LUKS)
        fputs("ROOTDEV=/dev/mapper/lywroot\n", fp);
    fputs("\n", fp);

    /* AppArmor must be on the kernel cmdline; cmdline() feeds every loader */
    const char *lsm = s[CAT_SECURITY] == SEC_APPARMOR
        ? " lsm=landlock,lockdown,yama,integrity,apparmor,bpf" : "";
    fputs("cmdline() {\n", fp);
    if (s[CAT_CRYPT] != E_LUKS) {
        fprintf(fp, "  printf 'root=UUID=%%s rw%s' \"$(blkid -s UUID -o value \"$ROOTP\")\"\n", lsm);
    } else {
        switch (s[CAT_PM]) {
        case P_XBPS: case P_PORTAGE:  /* dracut */
            fprintf(fp, "  printf 'rd.luks.uuid=%%s root=/dev/mapper/lywroot rw%s' "
                  "\"$(blkid -s UUID -o value \"$ROOTP\")\"\n", lsm);
            break;
        case P_APK:                   /* mkinitfs */
            fprintf(fp, "  printf 'cryptroot=UUID=%%s cryptdm=lywroot root=/dev/mapper/lywroot rw%s' "
                  "\"$(blkid -s UUID -o value \"$ROOTP\")\"\n", lsm);
            break;
        default:                      /* mkinitcpio */
            fprintf(fp, "  printf 'cryptdevice=UUID=%%s:lywroot root=/dev/mapper/lywroot rw%s' "
                  "\"$(blkid -s UUID -o value \"$ROOTP\")\"\n", lsm);
            break;
        }
    }
    fputs("}\n\n", fp);
}

static void emit_netcheck(FILE *fp)
{
    fputs("check_net() {\n"
          "  msg 'Checking internet connectivity'\n"
          "  while ! curl -sm8 -o /dev/null https://www.google.com 2>/dev/null && \\\n"
          "        ! wget -q -T8 -O /dev/null https://www.google.com 2>/dev/null; do\n"
          "    echo 'No internet connection (packages must be downloaded).'\n"
          "    if command -v nmtui >/dev/null 2>&1; then\n"
          "      printf 'Open nmtui to connect now? [Y/n] '; read -r a\n"
          "      case \"$a\" in n|N) ;; *) nmtui ;; esac\n"
          "    else\n"
          "      echo 'Connect manually (ip link / dhcpcd / iwctl / wpa_supplicant),'\n"
          "      printf 'then press Enter to re-check... '; read -r a\n"
          "    fi\n"
          "  done\n"
          "}\n\n", fp);
}

static void emit_prep_chroot(FILE *fp, const int s[CAT_COUNT])
{
    fputs("prep_chroot() {\n"
          "  mount --rbind /dev \"$ROOT/dev\";  mount --make-rslave \"$ROOT/dev\"\n"
          "  mount -t proc proc \"$ROOT/proc\"\n"
          "  mount --rbind /sys \"$ROOT/sys\";  mount --make-rslave \"$ROOT/sys\"\n", fp);
    if (s[CAT_PM] == P_PORTAGE)
        fputs("  $CHROOT \"$ROOT\" emerge-webrsync\n", fp);
    fputs("}\n\n", fp);
}

static void emit_autologin(FILE *fp, const int s[CAT_COUNT])
{
    fputs("  msg 'Login: root autologin, no password (testing/appliance mode)'\n"
          "  # empty password field in shadow = passwordless root on every tty\n"
          "  if [ -f \"$ROOT/etc/shadow\" ]; then\n"
          "    sed -i 's/^root:[^:]*:/root::/' \"$ROOT/etc/shadow\"\n"
          "  fi\n", fp);
    switch (s[CAT_INIT]) {
    case I_SYSTEMD:
        fputs("  mkdir -p \"$ROOT/etc/systemd/system/getty@tty1.service.d\"\n"
              "  {\n"
              "    printf '[Service]\\nExecStart=\\n'\n"
              "    printf 'ExecStart=-/sbin/agetty --autologin root --noclear %%I $TERM\\n'\n"
              "  } > \"$ROOT/etc/systemd/system/getty@tty1.service.d/autologin.conf\"\n", fp);
        break;
    case I_DINIT:
        fputs("  if [ -f \"$ROOT/etc/dinit.d/tty1\" ]; then\n"
              "    sed -i 's/agetty /agetty --autologin root /' \"$ROOT/etc/dinit.d/tty1\"\n"
              "  else\n"
              "    echo 'NOTE: add --autologin root to your tty1 dinit service' >&2\n"
              "  fi\n", fp);
        break;
    case I_RUNIT:
        fputs("  for f in \"$ROOT/etc/sv/agetty-tty1/conf\" \"$ROOT/etc/runit/sv/agetty-tty1/conf\"; do\n"
              "    [ -f \"$f\" ] && sed -i 's/GETTY_ARGS=\"/GETTY_ARGS=\"--autologin root /' \"$f\" || true\n"
              "  done\n", fp);
        break;
    case I_S6:
        fputs("  for f in \"$ROOT/etc/s6/sv/agetty-tty1/conf\" \"$ROOT/etc/s6/config/tty1.conf\"; do\n"
              "    [ -f \"$f\" ] && sed -i 's/GETTY_ARGS=\"/GETTY_ARGS=\"--autologin root /' \"$f\" || true\n"
              "  done\n"
              "  echo 'NOTE: verify the s6 agetty-tty1 service got --autologin root' >&2\n", fp);
        break;
    case I_OPENRC:
        if (s[CAT_PM] == P_APK)
            fputs("  # BusyBox getty: no prompt, straight to a root shell on tty1\n"
                  "  sed -i 's|^tty1::respawn:.*|tty1::respawn:/sbin/getty -n -l /bin/sh 38400 tty1|' \\\n"
                  "    \"$ROOT/etc/inittab\"\n", fp);
        else
            fputs("  sed -i '/^c1\\|^1:/s/agetty/agetty --autologin root/' \"$ROOT/etc/inittab\" || true\n", fp);
        break;
    case I_SYSVINIT:
        fputs("  sed -i 's/agetty /agetty --autologin root /' \"$ROOT/etc/inittab\"\n", fp);
        break;
    case I_BUSYBOX:
        fputs("  sed -i 's|getty 38400 tty1|getty -n -l /bin/sh 38400 tty1|' \"$ROOT/etc/inittab\"\n", fp);
        break;
    default: /* 66, finit, shepherd, custom */
        fputs("  # wire autologin into your own getty invocation for this init.\n", fp);
        break;
    }
}

static void emit_sysconfig(FILE *fp, const int s[CAT_COUNT])
{
    int bios = s[CAT_FIRMWARE] == FW_BIOS;

    fputs("system_config() {\n"
          "  msg 'System configuration'\n", fp);
    /* hostname comes from the TUI settings (validated chars only) */
    fprintf(fp, "  echo '%s' > \"$ROOT/etc/hostname\"\n"
                "  printf '127.0.0.1 localhost\\n127.0.1.1 %s\\n' >> \"$ROOT/etc/hosts\"\n",
            g_syscfg.hostname, g_syscfg.hostname);
    fputs("  {\n", fp);
    if (s[CAT_CRYPT] == E_LUKS) {
        fputs(s[CAT_FS] == F_BTRFS
              ? "    printf '/dev/mapper/lywroot / btrfs subvol=@,compress=zstd 0 1\\n'\n"
                "    printf '/dev/mapper/lywroot /home btrfs subvol=@home,compress=zstd 0 2\\n'\n"
              : "    printf '/dev/mapper/lywroot / ext4 defaults 0 1\\n'\n", fp);
        fputs("    printf 'UUID=%s /boot ext4 defaults 0 2\\n' "
              "\"$(blkid -s UUID -o value \"$BOOTP\")\"\n", fp);
    } else {
        fputs(s[CAT_FS] == F_BTRFS
              ? "    printf 'UUID=%s / btrfs subvol=@,compress=zstd 0 1\\n' "
                "\"$(blkid -s UUID -o value \"$ROOTP\")\"\n"
                "    printf 'UUID=%s /home btrfs subvol=@home,compress=zstd 0 2\\n' "
                "\"$(blkid -s UUID -o value \"$ROOTP\")\"\n"
              : "    printf 'UUID=%s / ext4 defaults 0 1\\n' "
                "\"$(blkid -s UUID -o value \"$ROOTP\")\"\n", fp);
    }
    if (!bios)
        fputs("    printf 'UUID=%s /boot/efi vfat defaults 0 2\\n' "
              "\"$(blkid -s UUID -o value \"$ESP\")\"\n", fp);
    fputs("    [ -z \"${SWAPP:-}\" ] || printf 'UUID=%s none swap defaults 0 0\\n' "
          "\"$(blkid -s UUID -o value \"$SWAPP\")\"\n", fp);
    fputs("  } >> \"$ROOT/etc/fstab\"\n", fp);

    if (s[CAT_CRYPT] == E_LUKS) {
        switch (s[CAT_PM]) {
        case P_PACMAN:
            fputs("  pkg cryptsetup\n"
                  "  sed -i 's/^HOOKS=.*/HOOKS=(base udev autodetect microcode modconf kms "
                  "keyboard keymap block encrypt filesystems fsck)/' \"$ROOT/etc/mkinitcpio.conf\"\n"
                  "  $CHROOT \"$ROOT\" mkinitcpio -P\n", fp);
            break;
        case P_XBPS:
            fputs("  pkg cryptsetup\n"
                  "  mkdir -p \"$ROOT/etc/dracut.conf.d\"\n"
                  "  echo 'add_dracutmodules+=\" crypt \"' > \"$ROOT/etc/dracut.conf.d/lyw.conf\"\n"
                  "  $CHROOT \"$ROOT\" xbps-reconfigure -fa\n", fp);
            break;
        case P_APK:
            fputs("  pkg cryptsetup\n"
                  "  sed -i 's/^features=\"/features=\"cryptsetup /' \"$ROOT/etc/mkinitfs/mkinitfs.conf\"\n"
                  "  $CHROOT \"$ROOT\" mkinitfs\n", fp);
            break;
        case P_PORTAGE:
            fputs("  pkg sys-fs/cryptsetup\n"
                  "  mkdir -p \"$ROOT/etc/dracut.conf.d\"\n"
                  "  echo 'add_dracutmodules+=\" crypt \"' > \"$ROOT/etc/dracut.conf.d/lyw.conf\"\n"
                  "  # regenerate the dist-kernel initramfs so it picks up the crypt module:\n"
                  "  $CHROOT \"$ROOT\" emerge --config sys-kernel/gentoo-kernel-bin || true\n", fp);
            break;
        default:
            fputs("  # MANUAL: add cryptsetup to your initramfs so the root can be unlocked.\n", fp);
            break;
        }
    }
    if (s[CAT_LOGIN] == LG_ROOTAUTO)
        emit_autologin(fp, s);
    fputs("}\n\n", fp);
}

/* hostname lands unquoted in the script; keep it to safe chars */
static void sanitize_hostname(void)
{
    char *h = g_syscfg.hostname;
    int j = 0;
    for (int i = 0; h[i]; i++) {
        char ch = h[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-')
            h[j++] = ch;
    }
    h[j] = '\0';
    if (!h[0]) strcpy(h, "lyw");
}

int genbuild_sh(const int s[CAT_COUNT], const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/build.sh", dir);
    mkdir(dir, 0755);
    emit_skeletons(s, dir);
    sanitize_hostname();

    FILE *fp = fopen(path, "w");
    if (!fp) return -1;

    fputs("#!/bin/sh\n"
          "# Generated by Linux Your Way (lyw) — REVIEW BEFORE RUNNING.\n"
          "# Build model: hybrid (binary base from real repos + local builds).\n", fp);
    if (g_syscfg.rootpw[0] || g_syscfg.userpw[0])
        fputs("# Passwords are embedded as SHA-512 crypt hashes (like /etc/shadow),\n"
              "# never in plaintext. Still: do not publish this file.\n", fp);
    fputs("# Config: ", fp);
    for (int c = 0; c < CAT_COUNT; c++)
        fprintf(fp, "%s%s", categories[c].opts[s[c]].val, c == CAT_COUNT - 1 ? "\n" : " + ");

    finding_t f[MAX_FINDINGS];
    int nf = compat_eval(s, f, MAX_FINDINGS);
    if (nf > MAX_FINDINGS) nf = MAX_FINDINGS;
    for (int i = 0; i < nf; i++)
        fprintf(fp, "# [%s] %s\n", status_word(f[i].status), f[i].msg);
    if (compat_overall(s) == ST_INCOMPATIBLE)
        fputs("echo 'INCOMPATIBLE combination — fix the config in lyw first.' >&2\n"
              "exit 1\n", fp);
    if (s[CAT_INSTALL] == IN_SOURCE)
        fputs("# source mode: v1 emits the hybrid script; treat package steps as\n"
              "# build-from-source TODOs.\n", fp);

    fputs("\nset -eu\n\n"
          "# Set DISK=/dev/sdX in the environment to skip all questions and\n"
          "# WIPE that whole disk (automation/testing path).\n"
          "ROOT=\"${ROOT:-/mnt/lyw}\"\n"
          "JOBS=\"${JOBS:-$(nproc)}\"\n"
          "SCRIPTDIR=$(cd \"$(dirname \"$0\")\" && pwd)\n"
          "mkdir -p \"$ROOT\"\n\n"
          "msg() { printf '\\n\\033[1;36m==> %s\\033[0m\\n' \"$*\"; }\n\n", fp);

    emit_header_vars(fp, s);
    emit_netcheck(fp);
    emit_pkg_helper(fp, s);
    emit_svc_helper(fp, s);
    emit_choose_target(fp, s);
    emit_partition(fp, s);
    emit_base(fp, s);
    emit_prep_chroot(fp, s);
    emit_altinit_call(fp, s);
    emit_kernel(fp, s);
    emit_graphics(fp, s);
    emit_network(fp, s);
    emit_bootloader(fp, s);
    emit_secboot(fp, s);
    emit_toolchain(fp, s);
    emit_security(fp, s);
    emit_localetime(fp, s);
    emit_users(fp, s);
    emit_branding(fp, s);
    emit_sysconfig(fp, s);

    fputs("check_net\n"
          "choose_target\n"
          "partition\n"
          "base_install\n"
          "if [ \"$CHROOT\" = chroot ]; then prep_chroot; fi\n"
          "cp -L /etc/resolv.conf \"$ROOT/etc/resolv.conf\" 2>/dev/null || true\n", fp);
    if (s[CAT_INIT] == I_FINIT || s[CAT_INIT] == I_SYSVINIT ||
        s[CAT_INIT] == I_BUSYBOX || s[CAT_INIT] == I_SHEPHERD)
        fputs("init_install\n", fp);
    fputs("kernel_install\n"
          "graphics_install\n"
          "network_install\n"
          "bootloader_install\n", fp);
    if (s[CAT_SECBOOT] == SB_SBCTL && s[CAT_FIRMWARE] == FW_UEFI)
        fputs("secboot_install\n", fp);
    fputs("toolchain_install\n"
          "security_install\n"
          "locale_time\n"
          "users_setup\n"
          "branding_install\n"
          "system_config\n"
          "msg 'Done. Review $ROOT, then reboot.'\n", fp);
    fclose(fp);
    chmod(path, (g_syscfg.rootpw[0] || g_syscfg.userpw[0]) ? 0700 : 0755);
    return 0;
}
