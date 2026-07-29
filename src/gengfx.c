#include <stdio.h>
#include <string.h>
#include "gen.h"

#define NVWM_GIT "https://github.com/Vifuddyxg/nvwm"

/* core session packages (WM/DE + terminal + dbus); the greeter is separate */
static const char *gfx_pkgs(int pm, int gfx)
{
    if (pm == P_PACMAN)
        switch (gfx) {
        case G_OPENBOX: return "xorg-server xorg-xinit openbox xterm dbus";
        case G_XFCE:    return "xorg-server xfce4 xfce4-goodies dbus";
        case G_SWAY:    return "sway foot swaybg wmenu polkit xdg-desktop-portal-wlr dbus";
        case G_NIRI:    return "niri alacritty fuzzel polkit xdg-desktop-portal-gtk dbus";
        case G_NVWM:    return "xorg-server xorg-xinit xterm dbus libx11 libxinerama "
                               "libxrandr libxcomposite libxrender git gcc make pkgconf";
        }
    if (pm == P_XBPS)
        switch (gfx) {
        case G_OPENBOX: return "xorg-minimal xinit openbox xterm dbus";
        case G_XFCE:    return "xorg-minimal xfce4 dbus";
        case G_SWAY:    return "sway foot dbus polkit";
        case G_NIRI:    return "niri alacritty fuzzel dbus polkit";
        case G_NVWM:    return "xorg-minimal xinit xterm dbus git gcc make pkg-config "
                               "libX11-devel libXinerama-devel libXrandr-devel "
                               "libXcomposite-devel libXrender-devel";
        }
    if (pm == P_APK)
        switch (gfx) {
        case G_OPENBOX: return "xorg-server xinit openbox xterm dbus";
        case G_XFCE:    return "xfce4 xfce4-terminal dbus";
        case G_SWAY:    return "sway foot dbus polkit";
        case G_NIRI:    return "niri alacritty fuzzel dbus polkit";
        case G_NVWM:    return "xorg-server xinit xterm dbus git gcc make musl-dev pkgconf "
                               "libx11-dev libxinerama-dev libxrandr-dev "
                               "libxcomposite-dev libxrender-dev";
        }
    if (pm == P_PORTAGE)
        switch (gfx) {
        case G_OPENBOX: return "x11-base/xorg-server x11-wm/openbox x11-terms/xterm";
        case G_XFCE:    return "xfce-base/xfce4-meta x11-misc/lightdm";
        case G_SWAY:    return "gui-wm/sway gui-apps/foot";
        case G_NIRI:    return "gui-wm/niri gui-apps/foot";
        case G_NVWM:    return "x11-base/xorg-server x11-terms/xterm dev-vcs/git "
                               "x11-libs/libX11 x11-libs/libXinerama x11-libs/libXrandr "
                               "x11-libs/libXcomposite x11-libs/libXrender";
        }
    return NULL;
}

static int gfx_is_x11(int gfx)
{
    return gfx == G_OPENBOX || gfx == G_XFCE || gfx == G_NVWM;
}

/* greeter stack: lightdm for X11 sessions, greetd (+tuigreet) for Wayland */
static const char *dm_pkgs(int pm, int gfx)
{
    if (gfx_is_x11(gfx))
        switch (pm) {
        case P_PACMAN:  return "lightdm lightdm-gtk-greeter";
        case P_XBPS:    return "lightdm lightdm-gtk3-greeter";
        case P_APK:     return "lightdm lightdm-gtk-greeter";
        case P_PORTAGE: return "x11-misc/lightdm";
        }
    else
        switch (pm) {
        case P_PACMAN:  return "greetd greetd-tuigreet";
        case P_XBPS:    return "greetd";
        case P_APK:     return "greetd greetd-tuigreet";
        case P_PORTAGE: return "gui-libs/greetd";
        }
    return NULL;
}

/* Artix ships service scripts per init as <pkg>-<init>; these inits have them */
static int artix_svc_init(int init)
{
    return init == I_DINIT || init == I_RUNIT || init == I_OPENRC || init == I_S6;
}

static void emit_nvwm_build(FILE *fp)
{
    fputs("  msg 'Building nvwm from source'\n"
          "  $CHROOT \"$ROOT\" sh -c 'rm -rf /tmp/nvwm && \\\n"
          "    git clone --depth 1 " NVWM_GIT " /tmp/nvwm && \\\n"
          "    make -C /tmp/nvwm && make -C /tmp/nvwm install PREFIX=/usr'\n"
          "  # session entry so the greeter can start it\n"
          "  mkdir -p \"$ROOT/usr/share/xsessions\"\n"
          "  {\n"
          "    printf '[Desktop Entry]\\nName=nvwm\\nComment=BSP tiling window manager\\n'\n"
          "    printf 'Exec=nvwm\\nType=XSession\\n'\n"
          "  } > \"$ROOT/usr/share/xsessions/nvwm.desktop\"\n", fp);
}

static void emit_greeter(FILE *fp, const int s[CAT_COUNT])
{
    const char *dm  = dm_pkgs(s[CAT_PM], s[CAT_GFX]);
    const char *svc = gfx_is_x11(s[CAT_GFX]) ? "lightdm" : "greetd";
    const char *init = categories[CAT_INIT].opts[s[CAT_INIT]].val;

    if (!dm) {
        fputs("  # no packaged greeter for this manager: install/start one yourself\n", fp);
        return;
    }
    fprintf(fp, "  msg 'Greeter: %s'\n  pkg %s\n", svc, dm);

    /* Artix: the service script for the DM (and dbus) lives in its own package */
    if (s[CAT_PM] == P_PACMAN && artix_svc_init(s[CAT_INIT]))
        fprintf(fp, "  pkg %s-%s dbus-%s\n", svc, init, init);
    else if (s[CAT_PM] == P_PACMAN && s[CAT_INIT] == I_66)
        fprintf(fp, "  pkg %s-66 dbus-66 || true\n", svc);

    if (!gfx_is_x11(s[CAT_GFX])) {
        const char *sess = s[CAT_GFX] == G_SWAY ? "sway" : "niri-session";
        fprintf(fp,
            "  # greetd on vt7 so it never fights a getty on tty1\n"
            "  if $CHROOT \"$ROOT\" sh -c 'command -v tuigreet >/dev/null 2>&1'; then\n"
            "    GCMD=\"tuigreet --remember --cmd %s\"\n"
            "  else\n"
            "    GCMD=\"agreety --cmd %s\"\n"
            "  fi\n"
            "  mkdir -p \"$ROOT/etc/greetd\"\n"
            "  {\n"
            "    printf '[terminal]\\nvt = 7\\n\\n[default_session]\\n'\n"
            "    printf 'command = \"%%s\"\\nuser = \"greeter\"\\n' \"$GCMD\"\n"
            "  } > \"$ROOT/etc/greetd/config.toml\"\n", sess, sess);
    } else {
        fputs("  mkdir -p \"$ROOT/etc/lightdm/lightdm.conf.d\"\n"
              "  printf '[Seat:*]\\ngreeter-session=lightdm-gtk-greeter\\n' \\\n"
              "    > \"$ROOT/etc/lightdm/lightdm.conf.d/50-lyw.conf\"\n", fp);
    }
    fprintf(fp, "  svc_enable dbus %s\n", svc);
}

void emit_graphics(FILE *fp, const int s[CAT_COUNT])
{
    const char *pkgs = gfx_pkgs(s[CAT_PM], s[CAT_GFX]);
    fputs("graphics_install() {\n", fp);
    if (s[CAT_GFX] == G_NONE) {
        fputs("  msg 'Graphics: none (TTY only)'\n}\n\n", fp);
        return;
    }
    if (!pkgs && s[CAT_PM] != P_CUSTOM) {
        fputs("  msg 'Graphics: MANUAL'\n"
              "  # No automated graphics install path for this package manager.\n"
              "}\n\n", fp);
        return;
    }
    fprintf(fp, "  msg 'Graphics: %s'\n", categories[CAT_GFX].opts[s[CAT_GFX]].name);
    int wayland = (s[CAT_GFX] == G_SWAY || s[CAT_GFX] == G_NIRI);
    const char *seat = "";
    if (wayland && s[CAT_INIT] != I_SYSTEMD && s[CAT_PM] != P_PACMAN)
        seat = " seatd";  /* Artix bases carry elogind from basestrap already */
    fprintf(fp, "  pkg %s%s\n", pkgs ? pkgs : "'<your graphics stack>'", seat);
    if (*seat)
        fputs("  svc_enable seatd\n"
              "  # add your user to the 'seat' (or '_seatd') group after creating it\n", fp);
    if (s[CAT_GFX] == G_NVWM)
        emit_nvwm_build(fp);
    emit_greeter(fp, s);
    fputs("}\n\n", fp);
}

void emit_network(FILE *fp, const int s[CAT_COUNT])
{
    fputs("network_install() {\n", fp);
    if (s[CAT_NET] == N_NONE) {
        fputs("  msg 'Network: none'\n}\n\n", fp);
        return;
    }
    fprintf(fp, "  msg 'Network: %s'\n", categories[CAT_NET].opts[s[CAT_NET]].name);

    const char *init = categories[CAT_INIT].opts[s[CAT_INIT]].val;
    const char *pkgs =
        s[CAT_NET] == N_DHCPCD ? (s[CAT_PM] == P_PORTAGE ? "net-misc/dhcpcd" : "dhcpcd")
      : s[CAT_NET] == N_NM     ? (s[CAT_PM] == P_PORTAGE ? "net-misc/networkmanager"
                                                         : "networkmanager")
      : s[CAT_NET] == N_IWD    ? (s[CAT_PM] == P_PORTAGE ? "net-wireless/iwd net-misc/dhcpcd"
                                                         : "iwd dhcpcd")
      :                          NULL; /* networkd ships with systemd */
    if (s[CAT_PM] == P_XBPS && s[CAT_NET] == N_NM) pkgs = "NetworkManager";

    if (pkgs) {
        fprintf(fp, "  pkg %s\n", pkgs);
        /* Artix packages ship init scripts separately, per init */
        if (s[CAT_PM] == P_PACMAN &&
            (artix_svc_init(s[CAT_INIT]) || s[CAT_INIT] == I_66)) {
            const char *tail = s[CAT_INIT] == I_66 ? " || true" : "";
            if (s[CAT_NET] == N_DHCPCD)
                fprintf(fp, "  pkg dhcpcd-%s%s\n", init, tail);
            else if (s[CAT_NET] == N_NM)
                fprintf(fp, "  pkg networkmanager-%s%s\n", init, tail);
            else
                fprintf(fp, "  pkg iwd-%s dhcpcd-%s%s\n", init, init, tail);
        }
    }

    const char *svcs = s[CAT_NET] == N_DHCPCD ? "dhcpcd"
                     : s[CAT_NET] == N_NM     ? "NetworkManager"
                     : s[CAT_NET] == N_IWD    ? "iwd dhcpcd"
                     :                          "systemd-networkd";
    fprintf(fp, "  svc_enable %s\n}\n\n", svcs);
}
