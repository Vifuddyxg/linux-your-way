# LYW live session: on the first console (tty1) auto-launch the configurator.
# On any other tty (or after lyw exits) you get a normal shell.
# tty1 autologins as ROOT (see /etc/runit/sv/agetty-tty1/conf) so the
# disk tools and bootstrap never depend on sudo; as_root() keeps every other
# login (lyw user on tty2+, ssh) working too.
as_root() {
    if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi
}
case "$(tty)" in
/dev/tty1)
    clear
    cat <<'BANNER'

  Welcome to Linux Your Way (live).

  Launching the configurator...  (press Ctrl-C to drop to a shell instead)
  No network? Run 'nmtui' to connect first, then 'lyw'.

BANNER
    # only auto-start once, and only if the configurator is present
    if [ -z "${LYW_NOAUTO:-}" ] && command -v lyw >/dev/null 2>&1; then
        export LYW_NOAUTO=1
        # Keep kernel/daemon console messages off this tty: they scribble
        # over the curses TUI.
        as_root dmesg -n 1 2>/dev/null || true
        as_root setterm --msg off 2>/dev/null || true
        cd /root
        as_root lyw || true
        cat <<'DONE'

  Configurator exited. Your config is in ./lyw-out/ (if you exported).
  Build the system:     sh lyw-out/build.sh
                        (asks which disk/partition to use; can resize for
                         dual-boot. DISK=/dev/sdX wipes that disk, no questions)
  Re-run the TUI:       lyw
  Connect to network:   nmtui   (also available from the TUI main menu)

DONE
    fi
    ;;
*)
    command -v lyw >/dev/null 2>&1 && \
        echo "LYW live — run 'lyw' to configure your system (self-elevates as needed)."
    ;;
esac
