#include <stdio.h>
#include "gen.h"

/* ---------------- kernel ---------------- */

const char *kernel_pkg(int pm, int kernel)
{
    if (pm == P_PACMAN)
        switch (kernel) {
        case K_LTS:      return "linux-lts";
        case K_VANILLA:  return "linux";
        case K_ZEN:      return "linux-zen";
        case K_HARDENED: return "linux-hardened";
        }
    if (pm == P_XBPS)
        switch (kernel) {
        case K_LTS:     return "linux-lts";
        case K_VANILLA: return "linux";
        }
    if (pm == P_APK)
        switch (kernel) {
        case K_LTS:     return "linux-lts";
        case K_VANILLA: return "linux-edge";
        }
    if (pm == P_PORTAGE && (kernel == K_LTS || kernel == K_VANILLA))
        return "sys-kernel/gentoo-kernel-bin sys-kernel/linux-firmware";
    return NULL; /* no package: build from source */
}

int kernel_from_source_cfg(const int s[CAT_COUNT])
{
    return s[CAT_KBUILD] != KB_PRECOMPILED ||
           kernel_pkg(s[CAT_PM], s[CAT_KERNEL]) == NULL;
}

static const char *kernel_src_url(const int s[CAT_COUNT])
{
    if (s[CAT_KERNEL] == K_CUSTOM && g_syscfg.kurl[0])
        return g_syscfg.kurl;
    if (s[CAT_KERNEL] == K_ZEN)
        return "https://github.com/zen-kernel/zen-kernel.git";
    return "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git";
}

/* scripts/config tweaks applied after defconfig (skipped for own-config) */
static void emit_kconfig_tweaks(FILE *fp, const int s[CAT_COUNT])
{
    const char *kz =
        s[CAT_KCOMPRESS] == KZ_GZIP ? "GZIP"
      : s[CAT_KCOMPRESS] == KZ_XZ   ? "XZ"
      : s[CAT_KCOMPRESS] == KZ_LZ4  ? "LZ4" : "ZSTD";
    fprintf(fp, "  ./scripts/config -e KERNEL_%s\n", kz);
    if (s[CAT_KERNEL] == K_RT)
        fputs("  # PREEMPT_RT is mainline since 6.12; on older trees this knob does not exist\n"
              "  ./scripts/config -e EXPERT -e PREEMPT_RT\n", fp);
    if (s[CAT_KERNEL] == K_HARDENED && s[CAT_PM] != P_PACMAN)
        fputs("  # hardening flags — NOT the full linux-hardened patchset\n"
              "  ./scripts/config -e STACKPROTECTOR_STRONG -e FORTIFY_SOURCE \\\n"
              "    -e RANDOMIZE_BASE -e SECURITY_LOCKDOWN_LSM -d DEVMEM\n", fp);
    if (s[CAT_KMOD] == KM_MONO)
        fputs("  # monolithic: =m options silently drop to =n — review carefully!\n"
              "  ./scripts/config -d MODULES\n", fp);
    if (s[CAT_TCPROFILE] == TP_SIZE)
        fputs("  ./scripts/config -e CC_OPTIMIZE_FOR_SIZE\n", fp);
}

static void emit_kernel_initramfs(FILE *fp, const int s[CAT_COUNT])
{
    if (s[CAT_KINITRD] != KI_SEPARATE) {
        fputs("  # initramfs: none/built-in chosen — nothing generated here\n", fp);
        return;
    }
    if (s[CAT_KMOD] == KM_MONO) {
        fputs("  # monolithic kernel: no /lib/modules tree, so the usual generators\n"
              "  # cannot run — build your initramfs by hand if you need one\n", fp);
        return;
    }
    /* best effort: use whatever generator the target base ships */
    fputs("  if $CHROOT \"$ROOT\" sh -c 'command -v dracut' >/dev/null 2>&1; then\n"
          "    $CHROOT \"$ROOT\" dracut --force --kver \"$KVER\" /boot/initramfs-lyw.img\n"
          "  elif $CHROOT \"$ROOT\" sh -c 'command -v mkinitcpio' >/dev/null 2>&1; then\n"
          "    $CHROOT \"$ROOT\" mkinitcpio -k \"$KVER\" -g /boot/initramfs-lyw.img\n"
          "  else\n"
          "    echo 'NOTE: no dracut/mkinitcpio in the target — no initramfs generated' >&2\n"
          "  fi\n", fp);
}

void emit_kernel(FILE *fp, const int s[CAT_COUNT])
{
    const char *pkg = s[CAT_KBUILD] == KB_PRECOMPILED
                      ? kernel_pkg(s[CAT_PM], s[CAT_KERNEL]) : NULL;
    fputs("kernel_install() {\n", fp);
    if (pkg) {
        fprintf(fp, "  msg 'Kernel: %s (precompiled)'\n  pkg %s\n", pkg, pkg);
        fputs("}\n\n", fp);
        return;
    }

    int clang = s[CAT_KCC] == KC_CLANG;
    fputs("  msg 'Kernel: building from source on this machine'\n", fp);
    fprintf(fp, "  for t in git make %s bc bison flex perl; do\n"
                "    command -v \"$t\" >/dev/null 2>&1 || { echo \"kernel build needs $t on this host\" >&2; exit 1; }\n"
                "  done\n",
            clang ? "clang ld.lld llvm-objcopy" : "gcc");
    fprintf(fp, "  git clone --depth 1%s%s%s %s /tmp/lyw-kernel\n"
                "  cd /tmp/lyw-kernel\n",
            g_syscfg.kver[0] ? " --branch '" : "",
            g_syscfg.kver[0] ? g_syscfg.kver : "",
            g_syscfg.kver[0] ? "'" : "",
            kernel_src_url(s));
    fputs("  # apply your patches, if any (drop *.patch into lyw-out/patches/)\n"
          "  for p in \"$SCRIPTDIR\"/patches/*.patch; do\n"
          "    [ -e \"$p\" ] || break\n"
          "    msg \"Applying $(basename \"$p\")\"\n"
          "    patch -p1 < \"$p\"\n"
          "  done\n", fp);

    const char *mk = clang ? "make LLVM=1" : "make";
    switch (s[CAT_KBUILD]) {
    case KB_OWNCONFIG:
        fprintf(fp, "  cp \"$SCRIPTDIR/.config\" .config && %s olddefconfig\n", mk);
        break;
    case KB_MENUCONFIG:
        fprintf(fp, "  %s defconfig\n", mk);
        emit_kconfig_tweaks(fp, s);
        fprintf(fp, "  %s olddefconfig && %s menuconfig\n", mk, mk);
        break;
    default: /* KB_LOCAL, and forced source builds with precompiled selected */
        fprintf(fp, "  %s defconfig\n", mk);
        emit_kconfig_tweaks(fp, s);
        fprintf(fp, "  %s olddefconfig\n", mk);
        break;
    }

    const char *kcflags =
        s[CAT_TCPROFILE] == TP_PERF ? " KCFLAGS='-march=native'" : "";
    fprintf(fp, "  %s -j\"$JOBS\"%s\n", mk, kcflags);
    if (s[CAT_KMOD] != KM_MONO)
        fprintf(fp, "  %s -j\"$JOBS\" modules_install INSTALL_MOD_PATH=\"$ROOT\"\n", mk);
    fputs("  KVER=$(make -s kernelrelease)\n"
          "  cp arch/x86/boot/bzImage \"$ROOT/boot/vmlinuz-lyw\"\n"
          "  cp .config \"$ROOT/boot/config-$KVER\"\n"
          "  cd - >/dev/null\n", fp);
    if (kernel_pkg(s[CAT_PM], K_LTS)) /* base has a repo: pull firmware blobs */
        fprintf(fp, "  pkg %s\n",
                s[CAT_PM] == P_PORTAGE ? "sys-kernel/linux-firmware" : "linux-firmware");
    emit_kernel_initramfs(fp, s);
    fputs("}\n\n", fp);
}

/* ---------------- bootloaders ---------------- */

static void emit_grub(FILE *fp, const int s[CAT_COUNT])
{
    int bios = s[CAT_FIRMWARE] == FW_BIOS;
    switch (s[CAT_PM]) {
    case P_PACMAN:  fputs(bios ? "  pkg grub os-prober\n"
                               : "  pkg grub efibootmgr os-prober\n", fp); break;
    case P_XBPS:    fputs(bios ? "  pkg grub os-prober\n"
                               : "  pkg grub-x86_64-efi os-prober\n", fp); break;
    case P_APK:     fputs(bios ? "  pkg grub grub-bios\n"
                               : "  pkg grub grub-efi efibootmgr\n", fp);
                    fputs("  pkg os-prober || true\n", fp); break;
    case P_PORTAGE: fputs("  pkg sys-boot/grub sys-boot/os-prober\n", fp); break;
    default:        fputs("  # install GRUB into the rootfs manually\n", fp); break;
    }
    fputs("  # dual-boot: let grub-mkconfig find other installed systems\n"
          "  mkdir -p \"$ROOT/etc/default\"\n"
          "  echo 'GRUB_DISABLE_OS_PROBER=false' >> \"$ROOT/etc/default/grub\"\n", fp);
    fprintf(fp, "  echo 'GRUB_TIMEOUT=%d' >> \"$ROOT/etc/default/grub\"\n",
            g_syscfg.boot_timeout);
    if (s[CAT_SECURITY] == SEC_APPARMOR)
        fputs("  echo 'GRUB_CMDLINE_LINUX_DEFAULT=\"loglevel=3 quiet "
              "lsm=landlock,lockdown,yama,integrity,apparmor,bpf\"' >> \"$ROOT/etc/default/grub\"\n", fp);
    if (s[CAT_CRYPT] == E_LUKS)
        fputs("  printf 'GRUB_CMDLINE_LINUX=\"%s\"\\n' \"$(cmdline)\" >> \"$ROOT/etc/default/grub\"\n", fp);
    if (bios)
        fputs("  $CHROOT \"$ROOT\" grub-install --target=i386-pc \"$DISK\"\n", fp);
    else
        fputs("  $CHROOT \"$ROOT\" grub-install --target=x86_64-efi \\\n"
              "    --efi-directory=/boot/efi --bootloader-id=LYW\n", fp);
    fputs("  $CHROOT \"$ROOT\" grub-mkconfig -o /boot/grub/grub.cfg\n", fp);
}

static void emit_limine(FILE *fp, const int s[CAT_COUNT])
{
    int bios = s[CAT_FIRMWARE] == FW_BIOS;
    switch (s[CAT_PM]) {
    case P_PACMAN: case P_XBPS: case P_APK:
        fputs("  pkg limine\n", fp); break;
    case P_PORTAGE:
        fputs("  pkg sys-boot/limine\n", fp); break;
    default:
        fputs("  # place limine's files under /usr/share/limine yourself\n", fp); break;
    }
    if (bios) {
        /* Limine v8+ only reads FAT — on BIOS that FAT partition is on you */
        fputs("  msg 'Limine on BIOS: MANUAL FAT /boot required'\n"
              "  echo 'Limine v8+ reads only FAT partitions. Make a FAT32 /boot, copy the'\n"
              "  echo 'kernel + limine.conf + limine-bios.sys there, then run:'\n"
              "  echo \"  $CHROOT $ROOT limine bios-install $DISK\"\n", fp);
        return;
    }
    fputs("  mkdir -p \"$ROOT/boot/efi/EFI/BOOT\"\n"
          "  cp \"$ROOT\"/usr/share/limine/BOOTX64.EFI \"$ROOT/boot/efi/EFI/BOOT/\"\n"
          "  KPATH=$(cd \"$ROOT/boot\" && ls vmlinuz* | head -1)\n"
          "  IPATH=$(cd \"$ROOT/boot\" && ls initramfs* initrd* 2>/dev/null | head -1)\n"
          "  # Limine reads limine.conf from the partition it boots from: keep\n"
          "  # kernel + initramfs + conf together on the ESP (re-copy on kernel updates).\n"
          "  cp \"$ROOT/boot/$KPATH\" \"$ROOT/boot/efi/\"\n"
          "  [ -z \"$IPATH\" ] || cp \"$ROOT/boot/$IPATH\" \"$ROOT/boot/efi/\"\n"
          "  {\n", fp);
    fprintf(fp, "    printf 'timeout: %d\\n\\n/Linux Your Way\\n'\n", g_syscfg.boot_timeout);
    fputs("    printf '    protocol: linux\\n'\n"
          "    printf '    path: boot():/%s\\n' \"$KPATH\"\n"
          "    [ -z \"$IPATH\" ] || printf '    module_path: boot():/%s\\n' \"$IPATH\"\n"
          "    printf '    cmdline: %s\\n' \"$(cmdline)\"\n"
          "  } > \"$ROOT/boot/efi/limine.conf\"\n"
          "  # dual-boot: add entries for other OSes to limine.conf yourself\n"
          "  # (e.g. 'protocol: efi_chainload' + path to their EFI binary).\n", fp);
}

static void emit_sdboot(FILE *fp)
{
    fputs("  $CHROOT \"$ROOT\" bootctl --esp-path=/boot/efi install\n"
          "  KPATH=$(cd \"$ROOT/boot\" && ls vmlinuz* | head -1)\n"
          "  IPATH=$(cd \"$ROOT/boot\" && ls initramfs* initrd* 2>/dev/null | head -1)\n"
          "  cp \"$ROOT/boot/$KPATH\" \"$ROOT/boot/efi/\"\n"
          "  [ -z \"$IPATH\" ] || cp \"$ROOT/boot/$IPATH\" \"$ROOT/boot/efi/\"\n"
          "  mkdir -p \"$ROOT/boot/efi/loader/entries\"\n", fp);
    fprintf(fp, "  printf 'timeout %d\\n' > \"$ROOT/boot/efi/loader/loader.conf\"\n",
            g_syscfg.boot_timeout);
    fputs("  {\n"
          "    printf 'title Linux Your Way\\n'\n"
          "    printf 'linux /%s\\n' \"$KPATH\"\n"
          "    [ -z \"$IPATH\" ] || printf 'initrd /%s\\n' \"$IPATH\"\n"
          "    printf 'options %s\\n' \"$(cmdline)\"\n"
          "  } > \"$ROOT/boot/efi/loader/entries/lyw.conf\"\n"
          "  # dual-boot: systemd-boot auto-detects Windows on the same ESP;\n"
          "  # other Linux installs need their own loader entries.\n", fp);
}

static void emit_refind(FILE *fp, const int s[CAT_COUNT])
{
    switch (s[CAT_PM]) {
    case P_PACMAN: case P_XBPS: case P_APK:
        fputs("  pkg refind\n", fp); break;
    case P_PORTAGE:
        fputs("  pkg sys-boot/refind\n", fp); break;
    default:
        fputs("  # install rEFInd into the target yourself, then run refind-install\n", fp); break;
    }
    fputs("  $CHROOT \"$ROOT\" refind-install\n"
          "  # rEFInd auto-detects kernels in /boot; give it the right cmdline:\n"
          "  printf '\"Boot with standard options\" \"%s\"\\n' \"$(cmdline)\" \\\n"
          "    > \"$ROOT/boot/refind_linux.conf\"\n", fp);
    fprintf(fp, "  sed -i 's/^timeout .*/timeout %d/' \"$ROOT/boot/efi/EFI/refind/refind.conf\" || true\n",
            g_syscfg.boot_timeout);
}

static void emit_syslinux(FILE *fp, const int s[CAT_COUNT])
{
    switch (s[CAT_PM]) {
    case P_PACMAN: case P_XBPS: case P_APK:
        fputs("  pkg syslinux\n", fp); break;
    case P_PORTAGE:
        fputs("  pkg sys-boot/syslinux\n", fp); break;
    default:
        fputs("  # install syslinux into the target yourself\n", fp); break;
    }
    fputs("  mkdir -p \"$ROOT/boot/syslinux\"\n"
          "  $CHROOT \"$ROOT\" extlinux --install /boot/syslinux\n"
          "  KPATH=$(cd \"$ROOT/boot\" && ls vmlinuz* | head -1)\n"
          "  IPATH=$(cd \"$ROOT/boot\" && ls initramfs* initrd* 2>/dev/null | head -1)\n"
          "  {\n"
          "    printf 'DEFAULT lyw\\nPROMPT 1\\n'\n", fp);
    fprintf(fp, "    printf 'TIMEOUT %d\\n'\n", g_syscfg.boot_timeout * 10);
    fputs("    printf 'LABEL lyw\\n  LINUX ../%s\\n' \"$KPATH\"\n"
          "    [ -z \"$IPATH\" ] || printf '  INITRD ../%s\\n' \"$IPATH\"\n"
          "    printf '  APPEND %s\\n' \"$(cmdline)\"\n"
          "  } > \"$ROOT/boot/syslinux/syslinux.cfg\"\n"
          "  # write the MBR boot code and mark the boot partition\n"
          "  MBRBIN=$(ls \"$ROOT\"/usr/lib/syslinux/bios/gptmbr.bin \\\n"
          "             \"$ROOT\"/usr/share/syslinux/gptmbr.bin 2>/dev/null | head -1)\n"
          "  PTT=$(blkid -s PTTYPE -o value \"$DISK\")\n"
          "  if [ \"$PTT\" = gpt ]; then\n"
          "    dd bs=440 count=1 conv=notrunc if=\"$MBRBIN\" of=\"$DISK\"\n", fp);
    fputs(s[CAT_CRYPT] == E_LUKS
          ? "    PN=$(cat \"/sys/class/block/$(basename \"$BOOTP\")/partition\")\n"
          : "    PN=$(cat \"/sys/class/block/$(basename \"$ROOTP\")/partition\")\n", fp);
    fputs("    sgdisk -A \"$PN:set:2\" \"$DISK\"   # legacy-boot attribute\n"
          "  else\n"
          "    MBRBIN=$(ls \"$ROOT\"/usr/lib/syslinux/bios/mbr.bin \\\n"
          "               \"$ROOT\"/usr/share/syslinux/mbr.bin 2>/dev/null | head -1)\n"
          "    dd bs=440 count=1 conv=notrunc if=\"$MBRBIN\" of=\"$DISK\"\n"
          "    echo 'NOTE: mark the boot partition active (fdisk, a) if it is not' >&2\n"
          "  fi\n", fp);
}

static void emit_efistub(FILE *fp, const int s[CAT_COUNT])
{
    switch (s[CAT_PM]) {
    case P_PACMAN: case P_XBPS: case P_APK:
        fputs("  pkg efibootmgr\n", fp); break;
    case P_PORTAGE:
        fputs("  pkg sys-boot/efibootmgr\n", fp); break;
    default:
        fputs("  # efibootmgr must exist in the target\n", fp); break;
    }
    fputs("  KPATH=$(cd \"$ROOT/boot\" && ls vmlinuz* | head -1)\n"
          "  IPATH=$(cd \"$ROOT/boot\" && ls initramfs* initrd* 2>/dev/null | head -1)\n"
          "  cp \"$ROOT/boot/$KPATH\" \"$ROOT/boot/efi/\"\n"
          "  [ -z \"$IPATH\" ] || cp \"$ROOT/boot/$IPATH\" \"$ROOT/boot/efi/\"\n"
          "  PN=$(cat \"/sys/class/block/$(basename \"$ESP\")/partition\")\n"
          "  EXTRA=\"\"\n"
          "  [ -z \"$IPATH\" ] || EXTRA=\" initrd=\\\\$IPATH\"\n"
          "  $CHROOT \"$ROOT\" efibootmgr --create --disk \"$DISK\" --part \"$PN\" \\\n"
          "    --label 'Linux Your Way' --loader \"\\\\$KPATH\" \\\n"
          "    --unicode \"$(cmdline)$EXTRA\"\n"
          "  # kernel updates: re-copy vmlinuz/initramfs to the ESP yourself.\n", fp);
}

void emit_bootloader(FILE *fp, const int s[CAT_COUNT])
{
    fputs("bootloader_install() {\n", fp);
    fprintf(fp, "  msg 'Bootloader: %s'\n", categories[CAT_BOOT].opts[s[CAT_BOOT]].name);
    switch (s[CAT_BOOT]) {
    case B_GRUB:     emit_grub(fp, s);     break;
    case B_LIMINE:   emit_limine(fp, s);   break;
    case B_SDBOOT:   emit_sdboot(fp);      break;
    case B_REFIND:   emit_refind(fp, s);   break;
    case B_SYSLINUX: emit_syslinux(fp, s); break;
    case B_EFISTUB:  emit_efistub(fp, s);  break;
    }
    fputs("}\n\n", fp);
}

void emit_secboot(FILE *fp, const int s[CAT_COUNT])
{
    if (s[CAT_SECBOOT] != SB_SBCTL || s[CAT_FIRMWARE] == FW_BIOS)
        return;
    fputs("secboot_install() {\n"
          "  msg 'Secure Boot: sbctl (own keys)'\n", fp);
    if (s[CAT_PM] != P_PACMAN) {
        fputs("  echo 'MANUAL: install sbctl (or use your distro tooling), create + enroll'\n"
              "  echo 'keys with the firmware in setup mode, then sign the loader and kernel.'\n"
              "}\n\n", fp);
        return;
    }
    fputs("  pkg sbctl\n"
          "  printf 'Enroll YOUR OWN Secure Boot keys now? Firmware must be in setup mode. [y/N] '\n"
          "  read -r a\n"
          "  case \"$a\" in y|Y) ;; *) echo 'skipped — run sbctl inside the system later'; return ;; esac\n"
          "  $CHROOT \"$ROOT\" sbctl create-keys\n"
          "  $CHROOT \"$ROOT\" sbctl enroll-keys -m   # -m keeps Microsoft keys (safer)\n"
          "  # sign everything the firmware or loader will execute\n"
          "  for f in $($CHROOT \"$ROOT\" sbctl verify 2>/dev/null | awk '/is not signed/{print $2}'); do\n"
          "    $CHROOT \"$ROOT\" sbctl sign -s \"$f\" || true\n"
          "  done\n"
          "}\n\n", fp);
}
