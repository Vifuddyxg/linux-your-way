#include <stdio.h>
#include "gen.h"

/* Install-target selection emitted into build.sh.
 *
 * Contract: after choose_target() the script has
 *   DISK        the target disk
 *   WIPE        1 = wipe + auto-partition the whole disk, 0 = surgical
 *   FORMAT_ESP  1 = mkfs.fat the ESP, 0 = reuse it untouched (dual-boot)
 *   ESP ROOTP   partitions to use ("" ESP on BIOS; + BOOTP with LUKS)
 *
 * The disk picked in the TUI is baked in below (still confirmed with YES).
 * DISK=/dev/sdX in the environment overrides everything and behaves like
 * the classic whole-disk wipe (automation path). */

static void emit_env_path(FILE *fp, const int s[CAT_COUNT])
{
    fputs("  if [ -n \"${DISK:-}\" ]; then\n"
          "    # non-interactive: DISK given in the environment — whole disk, no questions\n"
          "    case \"$DISK\" in *[0-9]) P=\"${DISK}p\";; *) P=\"$DISK\";; esac\n", fp);
    if (s[CAT_FIRMWARE] == FW_BIOS) {
        if (s[CAT_CRYPT] == E_LUKS)
            fputs("    ESP=\"\"; BOOTP=\"${P}2\"; ROOTP=\"${P}3\"\n", fp);
        else
            fputs("    ESP=\"\"; ROOTP=\"${P}2\"\n", fp);
    } else if (s[CAT_CRYPT] == E_LUKS)
        fputs("    ESP=\"${P}1\"; BOOTP=\"${P}2\"; ROOTP=\"${P}3\"\n", fp);
    else
        fputs("    ESP=\"${P}1\"; ROOTP=\"${P}2\"\n", fp);
    fputs("    confirm_target; return\n"
          "  fi\n", fp);
}

/* the disk/mode/partitions chosen in the TUI, baked into the script */
static void emit_preset(FILE *fp, const int s[CAT_COUNT])
{
    const syscfg_t *c = &g_syscfg;
    int luks = s[CAT_CRYPT] == E_LUKS;
    int bios = s[CAT_FIRMWARE] == FW_BIOS;

    fprintf(fp, "  # target chosen in the lyw TUI\n"
                "  DISK=%s\n"
                "  [ -b \"$DISK\" ] || { echo \"not a block device: $DISK\" >&2; exit 1; }\n"
                "  case \"$DISK\" in *[0-9]) P=\"${DISK}p\";; *) P=\"$DISK\";; esac\n",
            c->disk);
    switch (c->disk_mode) {
    case DM_WHOLE:
        if (bios)
            fputs(luks ? "  WIPE=1; ESP=\"\"; BOOTP=\"${P}2\"; ROOTP=\"${P}3\"\n"
                       : "  WIPE=1; ESP=\"\"; ROOTP=\"${P}2\"\n", fp);
        else
            fputs(luks ? "  WIPE=1; ESP=\"${P}1\"; BOOTP=\"${P}2\"; ROOTP=\"${P}3\"\n"
                       : "  WIPE=1; ESP=\"${P}1\"; ROOTP=\"${P}2\"\n", fp);
        break;
    case DM_FREE:
        fputs("  free_space\n", fp);
        break;
    case DM_PARTS:
        fputs("  WIPE=0; FORMAT_ESP=0\n", fp);
        if (!bios)
            fprintf(fp, "  ESP=%s\n"
                        "  [ -b \"$ESP\" ] || { echo \"not a partition: $ESP\" >&2; exit 1; }\n",
                    c->esp);
        fprintf(fp, "  ROOTP=%s\n"
                    "  [ -b \"$ROOTP\" ] || { echo \"not a partition: $ROOTP\" >&2; exit 1; }\n",
                c->rootp);
        if (c->bootp[0])
            fprintf(fp, "  BOOTP=%s\n"
                        "  [ -b \"$BOOTP\" ] || { echo \"not a partition: $BOOTP\" >&2; exit 1; }\n",
                    c->bootp);
        else if (luks)
            fputs("  printf '/boot partition (WILL be formatted ext4): '; read -r a\n"
                  "  BOOTP=$(devpath \"$a\"); [ -b \"$BOOTP\" ] || { echo \"not a partition: $BOOTP\" >&2; exit 1; }\n", fp);
        break;
    }
    fputs("  confirm_target\n", fp);
}

static void emit_interactive(FILE *fp, const int s[CAT_COUNT])
{
    int luks = s[CAT_CRYPT] == E_LUKS;
    int bios = s[CAT_FIRMWARE] == FW_BIOS;

    fputs("  msg 'Install target'\n"
          "  lsblk -d -e7 -o NAME,SIZE,MODEL,TRAN\n"
          "  printf 'Disk (e.g. sda, nvme0n1): '; read -r a; DISK=$(devpath \"$a\")\n"
          "  [ -b \"$DISK\" ] || { echo \"not a block device: $DISK\" >&2; exit 1; }\n"
          "  case \"$DISK\" in *[0-9]) P=\"${DISK}p\";; *) P=\"$DISK\";; esac\n"
          "  cat <<'MENU'\n"
          "How should this disk be used?\n"
          "  1) Whole disk      WIPE EVERYTHING and auto-partition (simplest)\n"
          "  2) Free space      keep existing OSes, install into unallocated space (dual-boot)\n"
          "  3) Existing parts  you already made partitions; pick them\n"
          "  4) Shrink first    shrink an ext4/NTFS partition to make room (dual-boot)\n"
          "  5) cfdisk          edit the partition table yourself, then pick partitions\n"
          "MENU\n"
          "  printf 'Choice [1-5]: '; read -r m\n"
          "  case \"$m\" in\n", fp);
    if (bios)
        fputs(luks ? "  1) WIPE=1; ESP=\"\"; BOOTP=\"${P}2\"; ROOTP=\"${P}3\" ;;\n"
                   : "  1) WIPE=1; ESP=\"\"; ROOTP=\"${P}2\" ;;\n", fp);
    else
        fputs(luks ? "  1) WIPE=1; ESP=\"${P}1\"; BOOTP=\"${P}2\"; ROOTP=\"${P}3\" ;;\n"
                   : "  1) WIPE=1; ESP=\"${P}1\"; ROOTP=\"${P}2\" ;;\n", fp);
    fputs("  2) free_space ;;\n"
          "  3) pick_parts ;;\n"
          "  4) shrink; free_space ;;\n"
          "  5) cfdisk \"$DISK\"; partprobe \"$DISK\"; sleep 1; pick_parts ;;\n"
          "  *) echo 'no such option' >&2; exit 1 ;;\n"
          "  esac\n"
          "  confirm_target\n", fp);
}

void emit_choose_target(FILE *fp, const int s[CAT_COUNT])
{
    int luks = s[CAT_CRYPT] == E_LUKS;
    int bios = s[CAT_FIRMWARE] == FW_BIOS;

    fputs("devpath() { case \"$1\" in /dev/*) printf '%s\\n' \"$1\";; *) printf '/dev/%s\\n' \"$1\";; esac; }\n\n"
          "by_partlabel() { blkid -o device -t PARTLABEL=\"$1\" | grep -F \"$DISK\" | tail -1; }\n\n", fp);

    /* one honest confirmation, shared by every path */
    fputs("confirm_target() {\n"
          "  echo; echo 'Install target:'\n"
          "  if [ \"$WIPE\" = 1 ]; then\n"
          "    echo \"  the WHOLE disk $DISK will be WIPED\"\n"
          "  else\n"
          "    [ -z \"${ESP:-}\" ] || echo \"  ESP:  $ESP ($([ \"$FORMAT_ESP\" = 1 ] && echo formatted || echo 'kept as-is'))\"\n", fp);
    if (luks)
        fputs("    echo \"  boot: $BOOTP (formatted)\"\n", fp);
    fputs("    echo \"  root: $ROOTP (FORMATTED — everything on it is lost)\"\n"
          "  fi\n"
          "  printf 'Proceed? Type YES: '; read -r ok\n"
          "  [ \"$ok\" = YES ] || exit 1\n"
          "}\n\n", fp);

    /* pick existing partitions (interactive option 3 / after cfdisk) */
    fputs("pick_parts() {\n"
          "  WIPE=0; FORMAT_ESP=0\n"
          "  lsblk -o NAME,SIZE,FSTYPE,PARTTYPENAME,PARTLABEL,MOUNTPOINTS \"$DISK\"\n", fp);
    if (bios)
        fputs("  ESP=\"\"\n", fp);
    else
        fputs("  printf 'EFI system partition (kept as-is, e.g. %s1): ' \"$DISK\"; read -r a\n"
              "  ESP=$(devpath \"$a\"); [ -b \"$ESP\" ] || { echo \"not a partition: $ESP\" >&2; exit 1; }\n", fp);
    if (luks)
        fputs("  printf '/boot partition (WILL be formatted ext4): '; read -r a\n"
              "  BOOTP=$(devpath \"$a\"); [ -b \"$BOOTP\" ] || { echo \"not a partition: $BOOTP\" >&2; exit 1; }\n", fp);
    fputs("  printf 'Root partition (WILL be formatted): '; read -r a\n"
          "  ROOTP=$(devpath \"$a\"); [ -b \"$ROOTP\" ] || { echo \"not a partition: $ROOTP\" >&2; exit 1; }\n"
          "}\n\n", fp);

    /* create partitions in unallocated space; reuse an existing ESP if present */
    fputs("free_space() {\n"
          "  WIPE=0\n"
          "  [ \"$(blkid -s PTTYPE -o value \"$DISK\")\" = gpt ] || {\n"
          "    echo 'Free-space install needs a GPT disk. Use existing parts or whole-disk.' >&2; exit 1; }\n", fp);
    if (bios)
        fputs("  ESP=\"\"; FORMAT_ESP=0\n"
              "  # GRUB on GPT+BIOS needs a bios-boot (ef02) partition somewhere on the disk\n"
              "  if ! lsblk -nlo PARTTYPE \"$DISK\" | grep -qi 21686148-6449-6e6f-744e-656564454649; then\n"
              "    sgdisk -n0:0:+1M -t0:ef02 -c0:lyw-biosboot \"$DISK\"\n"
              "    partprobe \"$DISK\"; sleep 1\n"
              "  fi\n", fp);
    else
        fputs("  ESP=$(lsblk -nlo PATH,PARTTYPE \"$DISK\" \\\n"
              "    | awk '$2==\"c12a7328-f81f-11d2-ba4b-00a0c93ec93b\"{print $1; exit}')\n"
              "  if [ -n \"$ESP\" ]; then\n"
              "    FORMAT_ESP=0; echo \"Reusing existing EFI partition: $ESP\"\n"
              "  else\n"
              "    FORMAT_ESP=1\n"
              "    sgdisk -n0:0:+512M -t0:ef00 -c0:lyw-esp \"$DISK\"\n"
              "    partprobe \"$DISK\"; sleep 1\n"
              "    ESP=$(by_partlabel lyw-esp)\n"
              "  fi\n", fp);
    if (luks)
        fputs("  sgdisk -n0:0:+1G -t0:8300 -c0:lyw-boot \"$DISK\"\n"
              "  partprobe \"$DISK\"; sleep 1\n"
              "  BOOTP=$(by_partlabel lyw-boot)\n", fp);
    fputs("  sgdisk -n0:0:0 -t0:8300 -c0:lyw-root \"$DISK\"   # takes the largest free block\n"
          "  partprobe \"$DISK\"; sleep 1\n"
          "  ROOTP=$(by_partlabel lyw-root)\n"
          "  [ -n \"$ROOTP\" ] || { echo 'could not create a root partition — no free space?' >&2; exit 1; }\n"
          "}\n\n", fp);

    /* shrink an ext4/NTFS partition, then reuse the freed space */
    fputs("shrink() {\n"
          "  lsblk -o NAME,SIZE,FSTYPE,FSUSED,PARTLABEL \"$DISK\"\n"
          "  printf 'Partition to SHRINK (data is kept, but BACK IT UP first): '; read -r a\n"
          "  SP=$(devpath \"$a\"); [ -b \"$SP\" ] || { echo \"not a partition: $SP\" >&2; exit 1; }\n"
          "  T=$(blkid -s TYPE -o value \"$SP\")\n"
          "  printf 'New total size for %s in GiB (must exceed its used space): ' \"$SP\"; read -r G\n"
          "  printf 'Shrink %s (%s) to %s GiB? Type YES: ' \"$SP\" \"$T\" \"$G\"; read -r ok\n"
          "  [ \"$ok\" = YES ] || exit 1\n"
          "  case \"$T\" in\n"
          "  ext4) e2fsck -f \"$SP\"; resize2fs \"$SP\" \"${G}G\" ;;\n"
          "  ntfs) ntfsresize --no-action --size \"${G}G\" \"$SP\"   # dry run aborts if unsafe\n"
          "        ntfsresize --force --size \"${G}G\" \"$SP\" ;;\n"
          "  *) echo \"can only shrink ext4 or ntfs (found: ${T:-nothing})\" >&2; exit 1 ;;\n"
          "  esac\n"
          "  PN=$(cat \"/sys/class/block/$(basename \"$SP\")/partition\")\n"
          "  START=$(sgdisk -i \"$PN\" \"$DISK\" | awk '/First sector/{print $3}')\n"
          "  TGUID=$(sgdisk -i \"$PN\" \"$DISK\" | awk '/Partition GUID code:/{print $4}')\n"
          "  PGUID=$(sgdisk -i \"$PN\" \"$DISK\" | awk '/Partition unique GUID:/{print $4}')\n"
          "  # recreate the entry at the same start, smaller; GUIDs preserved so boot\n"
          "  # entries keep working (a Windows partition gets a chkdsk on next boot)\n"
          "  sgdisk -d \"$PN\" -n \"$PN:$START:+${G}G\" -t \"$PN:$TGUID\" -u \"$PN:$PGUID\" \"$DISK\"\n"
          "  partprobe \"$DISK\"; sleep 1\n"
          "}\n\n", fp);

    fputs("choose_target() {\n"
          "  FORMAT_ESP=1; WIPE=1\n", fp);
    if (s[CAT_FIRMWARE] == FW_BIOS)
        fputs("  FORMAT_ESP=0\n", fp);
    emit_env_path(fp, s);
    if (g_syscfg.disk_mode != DM_ASK && g_syscfg.disk[0])
        emit_preset(fp, s);
    else
        emit_interactive(fp, s);
    fputs("}\n\n", fp);
}

void emit_partition(FILE *fp, const int s[CAT_COUNT])
{
    int luks = s[CAT_CRYPT] == E_LUKS;
    int bios = s[CAT_FIRMWARE] == FW_BIOS;

    fputs("partition() {\n"
          "  if [ \"$WIPE\" = 1 ]; then\n", fp);
    if (bios) {
        /* 1M bios-boot keeps GRUB's core.img on GPT disks */
        if (luks)
            fputs("    msg \"Partitioning $DISK (GPT: 1M bios-boot + 1G /boot + LUKS2 root)\"\n"
                  "    sgdisk --zap-all \"$DISK\"\n"
                  "    sgdisk -n1:0:+1M -t1:ef02 -c1:lyw-biosboot \\\n"
                  "           -n2:0:+1G  -t2:8300 -c2:lyw-boot \\\n"
                  "           -n3:0:0    -t3:8309 -c3:lyw-luks \"$DISK\"\n"
                  "    partprobe \"$DISK\"; sleep 1\n", fp);
        else
            fputs("    msg \"Partitioning $DISK (GPT: 1M bios-boot + root)\"\n"
                  "    sgdisk --zap-all \"$DISK\"\n"
                  "    sgdisk -n1:0:+1M -t1:ef02 -c1:lyw-biosboot -n2:0:0 -t2:8300 -c2:lyw-root \"$DISK\"\n"
                  "    partprobe \"$DISK\"; sleep 1\n", fp);
    } else if (luks)
        fputs("    msg \"Partitioning $DISK (GPT: 512M EFI + 1G /boot + LUKS2 root)\"\n"
              "    sgdisk --zap-all \"$DISK\"\n"
              "    sgdisk -n1:0:+512M -t1:ef00 -c1:EFI \\\n"
              "           -n2:0:+1G  -t2:8300 -c2:lyw-boot \\\n"
              "           -n3:0:0    -t3:8309 -c3:lyw-luks \"$DISK\"\n"
              "    partprobe \"$DISK\"; sleep 1\n", fp);
    else
        fputs("    msg \"Partitioning $DISK (GPT: 512M EFI + root)\"\n"
              "    sgdisk --zap-all \"$DISK\"\n"
              "    sgdisk -n1:0:+512M -t1:ef00 -c1:EFI -n2:0:0 -t2:8300 -c2:lyw-root \"$DISK\"\n"
              "    partprobe \"$DISK\"; sleep 1\n", fp);
    fputs("  fi\n", fp);
    if (!luks)
        fputs("  ROOTDEV=\"$ROOTP\"\n", fp);
    if (!bios)
        fputs("  if [ \"$FORMAT_ESP\" = 1 ]; then mkfs.fat -F32 \"$ESP\"; fi\n", fp);
    if (luks)
        fputs("  mkfs.ext4 -F -L lyw-boot \"$BOOTP\"\n"
              "  msg 'Choose your LUKS passphrase'\n"
              "  cryptsetup luksFormat --type luks2 \"$ROOTP\"\n"
              "  cryptsetup open \"$ROOTP\" lywroot\n", fp);
    if (s[CAT_FS] == F_BTRFS)
        fputs("  mkfs.btrfs -f -L lyw-root \"$ROOTDEV\"\n"
              "  mount \"$ROOTDEV\" \"$ROOT\"\n"
              "  btrfs subvolume create \"$ROOT/@\"\n"
              "  btrfs subvolume create \"$ROOT/@home\"\n"
              "  umount \"$ROOT\"\n"
              "  mount -o subvol=@,compress=zstd \"$ROOTDEV\" \"$ROOT\"\n"
              "  mkdir -p \"$ROOT/home\"\n"
              "  mount -o subvol=@home,compress=zstd \"$ROOTDEV\" \"$ROOT/home\"\n", fp);
    else
        fputs("  mkfs.ext4 -F -L lyw-root \"$ROOTDEV\"\n"
              "  mount \"$ROOTDEV\" \"$ROOT\"\n", fp);
    if (luks)
        fputs("  mkdir -p \"$ROOT/boot\"\n"
              "  mount \"$BOOTP\" \"$ROOT/boot\"\n", fp);
    if (!bios)
        fputs("  mkdir -p \"$ROOT/boot/efi\"\n"
              "  mount \"$ESP\" \"$ROOT/boot/efi\"\n", fp);
    fputs("}\n\n", fp);
}
