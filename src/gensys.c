#include <stdio.h>
#include "gen.h"

/* print s single-quoted for shell, escaping embedded quotes */
static void sq(FILE *fp, const char *s)
{
    fputc('\'', fp);
    for (; *s; s++) {
        if (*s == '\'') fputs("'\\''", fp);
        else fputc(*s, fp);
    }
    fputc('\'', fp);
}

/* ---------------- toolchain ---------------- */

static const char *tc_gnu_pkgs(int pm)
{
    switch (pm) {
    case P_PACMAN:  return "base-devel git";
    case P_XBPS:    return "gcc make binutils git";
    case P_APK:     return "build-base git";
    case P_PORTAGE: return NULL; /* the stage3 IS a GNU toolchain */
    default:        return NULL;
    }
}

static const char *tc_llvm_pkgs(int pm)
{
    switch (pm) {
    case P_PACMAN:  return "clang llvm lld";
    case P_XBPS:    return "clang lld llvm";
    case P_APK:     return "clang lld llvm";
    case P_PORTAGE: return "llvm-core/clang llvm-core/lld";
    default:        return NULL;
    }
}

static const char *profile_cflags(int p)
{
    switch (p) {
    case TP_SIZE:     return "-Os -pipe";
    case TP_PERF:     return "-O3 -pipe -march=native";
    case TP_DEBUG:    return "-Og -g -pipe";
    case TP_HARDENED: return "-O2 -pipe -D_FORTIFY_SOURCE=3 "
                             "-fstack-protector-strong -fstack-clash-protection";
    default:          return "-O2 -pipe";
    }
}

void emit_toolchain(FILE *fp, const int s[CAT_COUNT])
{
    const char *gnu  = tc_gnu_pkgs(s[CAT_PM]);
    const char *llvm = tc_llvm_pkgs(s[CAT_PM]);
    const char *cf   = profile_cflags(s[CAT_TCPROFILE]);

    fputs("toolchain_install() {\n", fp);
    fprintf(fp, "  msg 'Toolchain: %s (%s profile)'\n",
            categories[CAT_TC].opts[s[CAT_TC]].name,
            categories[CAT_TCPROFILE].opts[s[CAT_TCPROFILE]].val);
    if (s[CAT_TC] != TC_LLVM) {
        if (gnu) fprintf(fp, "  pkg %s\n", gnu);
        else if (s[CAT_PM] == P_PORTAGE)
            fputs("  # GCC + Binutils already come with the stage3\n", fp);
        else
            fputs("  # MANUAL: no package source for a GNU toolchain here\n", fp);
    }
    if (s[CAT_TC] != TC_GNU) {
        if (llvm) fprintf(fp, "  pkg %s\n", llvm);
        else fputs("  # MANUAL: install clang/llvm/lld yourself\n", fp);
    }
    /* default flags for things compiled on the installed system */
    fprintf(fp, "  printf 'CFLAGS=\"%s\"\\nCXXFLAGS=\"%s\"\\n' > \"$ROOT/etc/lyw-build.conf\"\n",
            cf, cf);
    if (s[CAT_PM] == P_PORTAGE)
        fprintf(fp, "  sed -i 's/^COMMON_FLAGS=.*/COMMON_FLAGS=\"%s\"/' "
                    "\"$ROOT/etc/portage/make.conf\"\n", cf);
    else if (s[CAT_TCPROFILE] != TP_COMPAT)
        fputs("  echo 'NOTE: binary packages keep their own flags; /etc/lyw-build.conf'\n"
              "  echo '      applies to what YOU compile on this system.'\n", fp);
    fputs("}\n\n", fp);
}

/* ---------------- shell + users + passwords ---------------- */

static const char *shell_pkg(int pm, int sh)
{
    if (sh == SH_ASH)
        return pm == P_APK ? NULL /* busybox is the base */
             : pm == P_PORTAGE ? "sys-apps/busybox" : "busybox";
    if (pm == P_PORTAGE)
        switch (sh) {
        case SH_BASH: return "app-shells/bash";
        case SH_DASH: return "app-shells/dash";
        case SH_ZSH:  return "app-shells/zsh";
        case SH_FISH: return "app-shells/fish";
        }
    switch (sh) {
    case SH_BASH: return "bash";
    case SH_DASH: return "dash";
    case SH_ZSH:  return "zsh";
    case SH_FISH: return pm == P_XBPS ? "fish-shell" : "fish";
    }
    return NULL;
}

static const char *shell_cmd(int sh)
{
    switch (sh) {
    case SH_DASH: return "dash";
    case SH_ASH:  return "ash";
    case SH_ZSH:  return "zsh";
    case SH_FISH: return "fish";
    default:      return "bash";
    }
}

static int pm_has_pkgs(int pm)
{
    return pm == P_PACMAN || pm == P_XBPS || pm == P_APK ||
           pm == P_PORTAGE || pm == P_CUSTOM;
}

void emit_users(FILE *fp, const int s[CAT_COUNT])
{
    const char *pkg = shell_pkg(s[CAT_PM], s[CAT_SHELL]);
    const syscfg_t *c = &g_syscfg;
    char roothash[160] = "", userhash[160] = "";

    if (c->rootpw[0] && pw_hash(c->rootpw, roothash, sizeof roothash) != 0)
        roothash[0] = '\0';
    if (c->userpw[0] && pw_hash(c->userpw, userhash, sizeof userhash) != 0)
        userhash[0] = '\0';

    fputs("users_setup() {\n"
          "  msg 'Shell, users, passwords'\n", fp);

    /* login shell */
    if (pkg && pm_has_pkgs(s[CAT_PM]))
        fprintf(fp, "  pkg %s\n", pkg);
    if (s[CAT_SHELL] == SH_ASH)
        fputs("  BB=$($CHROOT \"$ROOT\" sh -c 'command -v busybox' || true)\n"
              "  [ -z \"$BB\" ] || $CHROOT \"$ROOT\" ln -sf \"$BB\" /bin/ash\n", fp);
    fprintf(fp, "  SH=$($CHROOT \"$ROOT\" sh -c 'command -v %s' || true)\n"
                "  if [ -n \"$SH\" ]; then\n"
                "    sed -i \"s#^root:\\\\(.*\\\\):[^:]*\\$#root:\\\\1:$SH#\" \"$ROOT/etc/passwd\"\n"
                "  else\n"
                "    echo 'NOTE: %s not found in target; root keeps /bin/sh' >&2\n"
                "  fi\n",
            shell_cmd(s[CAT_SHELL]), shell_cmd(s[CAT_SHELL]));

    /* root password (pre-hashed with SHA-512 crypt by lyw; never plaintext) */
    if (roothash[0]) {
        fputs("  printf 'root:%s\\n' ", fp);
        sq(fp, roothash);
        fputs(" | $CHROOT \"$ROOT\" chpasswd -e\n", fp);
    } else if (s[CAT_LOGIN] == LG_PASSWORD) {
        fputs("  echo 'Set a root password: '\"$CHROOT\"' \"$ROOT\" passwd'\n", fp);
    }

    /* user account */
    /* privilege escalation: sudo or OpenBSD-style doas */
    if (s[CAT_PRIVESC] == PE_SUDO) {
        if (pm_has_pkgs(s[CAT_PM]))
            fprintf(fp, "  pkg %s\n", s[CAT_PM] == P_PORTAGE ? "app-admin/sudo" : "sudo");
        fputs("  mkdir -p \"$ROOT/etc/sudoers.d\"\n"
              "  echo '%wheel ALL=(ALL:ALL) ALL' > \"$ROOT/etc/sudoers.d/10-wheel\"\n"
              "  chmod 440 \"$ROOT/etc/sudoers.d/10-wheel\"\n", fp);
    } else if (s[CAT_PRIVESC] == PE_DOAS) {
        if (pm_has_pkgs(s[CAT_PM]))
            fprintf(fp, "  pkg %s\n",
                    s[CAT_PM] == P_APK     ? "doas"
                  : s[CAT_PM] == P_PORTAGE ? "app-admin/doas" : "opendoas");
        fputs("  printf 'permit persist :wheel\\n' > \"$ROOT/etc/doas.conf\"\n"
              "  chmod 640 \"$ROOT/etc/doas.conf\"\n", fp);
    }

    if (c->username[0]) {
        fputs("  U=", fp); sq(fp, c->username); fputs("\n", fp);
        if (s[CAT_PM] == P_APK)
            fputs("  $CHROOT \"$ROOT\" adduser -D -s \"${SH:-/bin/sh}\" \"$U\" || true\n"
                  "  for g in wheel video audio input; do\n"
                  "    $CHROOT \"$ROOT\" addgroup \"$U\" \"$g\" 2>/dev/null || true\n"
                  "  done\n", fp);
        else
            fputs("  $CHROOT \"$ROOT\" useradd -m -s \"${SH:-/bin/sh}\" \"$U\" || true\n"
                  "  for g in wheel video audio input; do\n"
                  "    $CHROOT \"$ROOT\" usermod -aG \"$g\" \"$U\" 2>/dev/null || true\n"
                  "  done\n", fp);
        if (userhash[0]) {
            fputs("  printf '%s:%s\\n' \"$U\" ", fp);
            sq(fp, userhash);
            fputs(" | $CHROOT \"$ROOT\" chpasswd -e\n", fp);
        } else {
            fputs("  echo \"Set a password for $U: \"\"$CHROOT\"\" $ROOT passwd $U\"\n", fp);
        }
    }
    fputs("}\n\n", fp);
}

/* ---------------- security: firewall, sysctl hardening, MAC ---------------- */

void emit_security(FILE *fp, const int s[CAT_COUNT])
{
    int sec = s[CAT_SECURITY];
    fputs("security_install() {\n", fp);
    fprintf(fp, "  msg 'Security: %s'\n", categories[CAT_SECURITY].opts[sec].name);
    if (sec == SEC_NONE) {
        fputs("}\n\n", fp);
        return;
    }
    if (sec == SEC_SELINUX) {
        fputs("  echo 'MANUAL: SELinux — switch the Gentoo profile, emerge policies, set'\n"
              "  echo 'SELINUX=permissive in /etc/selinux/config, relabel with rlpkg -a -r.'\n"
              "}\n\n", fp);
        return;
    }

    /* nftables firewall for firewall/harden/apparmor levels */
    if (pm_has_pkgs(s[CAT_PM]))
        fprintf(fp, "  pkg %s\n",
                s[CAT_PM] == P_PORTAGE ? "net-firewall/nftables" : "nftables");
    if (s[CAT_PM] == P_PACMAN && s[CAT_INIT] != I_SYSTEMD)
        fprintf(fp, "  pkg nftables-%s || true\n",
                categories[CAT_INIT].opts[s[CAT_INIT]].val);
    fputs("  cat > \"$ROOT/etc/nftables.conf\" <<'EOF'\n"
          "#!/usr/sbin/nft -f\n"
          "flush ruleset\n"
          "table inet filter {\n"
          "  chain input {\n"
          "    type filter hook input priority filter; policy drop;\n"
          "    ct state established,related accept\n"
          "    ct state invalid drop\n"
          "    iif \"lo\" accept\n"
          "    ip protocol icmp accept\n"
          "    meta l4proto ipv6-icmp accept\n"
          "  }\n"
          "  chain forward { type filter hook forward priority filter; policy drop; }\n"
          "  chain output  { type filter hook output priority filter; policy accept; }\n"
          "}\n"
          "EOF\n"
          "  svc_enable nftables\n", fp);

    if (sec >= SEC_HARDEN)
        fputs("  mkdir -p \"$ROOT/etc/sysctl.d\"\n"
              "  cat > \"$ROOT/etc/sysctl.d/90-lyw-hardening.conf\" <<'EOF'\n"
              "# Linux Your Way — BSD-flavoured kernel lockdown\n"
              "kernel.kptr_restrict = 2\n"
              "kernel.dmesg_restrict = 1\n"
              "kernel.yama.ptrace_scope = 1\n"
              "kernel.unprivileged_bpf_disabled = 1\n"
              "net.core.bpf_jit_harden = 2\n"
              "kernel.kexec_load_disabled = 1\n"
              "net.ipv4.conf.all.rp_filter = 1\n"
              "net.ipv4.tcp_syncookies = 1\n"
              "fs.protected_symlinks = 1\n"
              "fs.protected_hardlinks = 1\n"
              "EOF\n", fp);

    if (sec == SEC_APPARMOR) {
        if (s[CAT_PM] == P_PACMAN || s[CAT_PM] == P_XBPS)
            fputs("  pkg apparmor\n", fp);
        else if (s[CAT_PM] == P_PORTAGE)
            fputs("  pkg sys-apps/apparmor sys-apps/apparmor-utils sec-policy/apparmor-profiles\n", fp);
        else
            fputs("  # MANUAL: install apparmor + profiles yourself\n", fp);
        if (s[CAT_PM] == P_PACMAN && s[CAT_INIT] != I_SYSTEMD)
            fprintf(fp, "  pkg apparmor-%s || true\n",
                    categories[CAT_INIT].opts[s[CAT_INIT]].val);
        fputs("  svc_enable apparmor\n"
              "  # the kernel cmdline gets lsm=...apparmor via cmdline()/GRUB below\n", fp);
    }
    fputs("}\n\n", fp);
}

/* ---------------- OS branding: Linux Your Way ---------------- */

static const char *id_like(const int s[CAT_COUNT])
{
    switch (s[CAT_PM]) {
    case P_PACMAN:  return s[CAT_INIT] == I_SYSTEMD ? "arch" : "artix arch";
    case P_XBPS:    return "void";
    case P_APK:     return "alpine";
    case P_PORTAGE: return "gentoo";
    default:        return NULL;
    }
}

void emit_branding(FILE *fp, const int s[CAT_COUNT])
{
    const char *like = id_like(s);
    fputs("branding_install() {\n"
          "  msg 'Branding: Linux Your Way'\n"
          "  # /etc/os-release as a regular file wins over /usr/lib/os-release\n"
          "  rm -f \"$ROOT/etc/os-release\"\n"
          "  {\n"
          "    printf 'NAME=\"Linux Your Way\"\\n'\n"
          "    printf 'PRETTY_NAME=\"Linux Your Way\"\\n'\n"
          "    printf 'ID=lyw\\n'\n", fp);
    if (like)
        fprintf(fp, "    printf 'ID_LIKE=\"%s\"\\n'\n", like);
    fputs("    printf 'BUILD_ID=rolling\\n'\n"
          "    printf 'ANSI_COLOR=\"1;33\"\\n'\n"
          "    printf 'HOME_URL=\"https://github.com/Vifuddyxg/linux-your-way\"\\n'\n"
          "    printf 'LOGO=lyw\\n'\n"
          "  } > \"$ROOT/etc/os-release\"\n"
          "  printf 'Linux Your Way \\\\r (\\\\l)\\n\\n' > \"$ROOT/etc/issue\"\n"
          "  mkdir -p \"$ROOT/usr/share/lyw\" \"$ROOT/etc/fastfetch\"\n"
          "  # the letter-art logo ships on the LYW live ISO; copy it into the target\n"
          "  if [ -f /usr/share/lyw/logo.txt ]; then\n"
          "    cp /usr/share/lyw/logo.txt \"$ROOT/usr/share/lyw/logo.txt\"\n"
          "    cat > \"$ROOT/etc/fastfetch/config.jsonc\" <<'EOF'\n"
          "// Linux Your Way — letter-art logo (see github.com/Vifuddyxg/linux-your-way)\n"
          "{\n"
          "    \"logo\": {\n"
          "        \"type\": \"file-raw\",\n"
          "        \"source\": \"/usr/share/lyw/logo.txt\",\n"
          "        \"padding\": { \"right\": 3 }\n"
          "    }\n"
          "}\n"
          "EOF\n"
          "  else\n"
          "    echo 'NOTE: /usr/share/lyw/logo.txt not on this host; fastfetch logo skipped' >&2\n"
          "  fi\n"
          "}\n\n", fp);
}

/* ---------------- timezone, clock, locale, keymap ---------------- */

void emit_localetime(FILE *fp, const int s[CAT_COUNT])
{
    const syscfg_t *c = &g_syscfg;

    fputs("locale_time() {\n"
          "  msg 'Timezone, clock, locale, keymap'\n", fp);

    fputs("  TZSEL=", fp); sq(fp, c->timezone); fputs("\n", fp);
    fputs("  if [ -e \"$ROOT/usr/share/zoneinfo/$TZSEL\" ]; then\n"
          "    ln -sf \"../usr/share/zoneinfo/$TZSEL\" \"$ROOT/etc/localtime\"\n"
          "  else\n"
          "    echo \"NOTE: timezone $TZSEL not found in the target; set /etc/localtime yourself\" >&2\n"
          "  fi\n"
          "  $CHROOT \"$ROOT\" hwclock --systohc 2>/dev/null || true\n", fp);
    if (s[CAT_INIT] == I_SYSTEMD)
        fputs("  svc_enable systemd-timesyncd || true\n", fp);

    /* console keymap: vconsole.conf is read by systemd and the Artix/Arch rc;
     * Void reads rc.conf, Gentoo OpenRC reads conf.d/keymaps */
    fputs("  KM=", fp); sq(fp, c->keymap); fputs("\n", fp);
    fputs("  printf 'KEYMAP=%s\\n' \"$KM\" > \"$ROOT/etc/vconsole.conf\"\n", fp);
    if (s[CAT_PM] == P_XBPS)
        fputs("  sed -i \"s/^#\\\\?KEYMAP=.*/KEYMAP=$KM/\" \"$ROOT/etc/rc.conf\" || true\n", fp);
    if (s[CAT_PM] == P_PORTAGE && s[CAT_INIT] == I_OPENRC)
        fputs("  sed -i \"s/^keymap=.*/keymap=\\\"$KM\\\"/\" \"$ROOT/etc/conf.d/keymaps\" || true\n", fp);

    /* locale (glibc only; musl has a built-in C.UTF-8-ish locale) */
    if (s[CAT_LIBC] == L_GLIBC) {
        fputs("  LOC=", fp); sq(fp, c->locale); fputs("\n", fp);
        fputs("  printf 'LANG=%s\\n' \"$LOC\" > \"$ROOT/etc/locale.conf\"\n", fp);
        switch (s[CAT_PM]) {
        case P_PACMAN:
            fputs("  sed -i \"s/^#$LOC/$LOC/\" \"$ROOT/etc/locale.gen\" || true\n"
                  "  $CHROOT \"$ROOT\" locale-gen || true\n", fp);
            break;
        case P_PORTAGE:
            fputs("  echo \"$LOC UTF-8\" >> \"$ROOT/etc/locale.gen\"\n"
                  "  $CHROOT \"$ROOT\" locale-gen || true\n", fp);
            break;
        case P_XBPS:
            fputs("  echo \"$LOC UTF-8\" >> \"$ROOT/etc/default/libc-locales\"\n"
                  "  $CHROOT \"$ROOT\" xbps-reconfigure -f glibc-locales || true\n", fp);
            break;
        default:
            fputs("  echo 'NOTE: generate the locale with your base tooling' >&2\n", fp);
            break;
        }
    } else {
        fputs("  # musl/uClibc: no locale generation; LANG left to the user\n", fp);
    }
    fputs("}\n\n", fp);
}
