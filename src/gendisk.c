#include <stdio.h>
#include "gen.h"

/* Install-target selection emitted into build.sh.
 *
 * Contract: after choose_target() the script has
 *   DISK        the target disk
 *   WIPE        1 = wipe + auto-partition the whole disk, 0 = surgical
 *   FORMAT_ESP  1 = mkfs.fat the ESP, 0 = reuse it untouched (dual-boot)
 *   ESP ROOTP   partitions to use ("" ESP on BIOS; + BOOTP with LUKS,
 *   SWAPP       + SWAPP when a swap partition exists)
 *
 * The disk picked in the TUI is baked in below (still confirmed with YES).
 * DISK=/dev/sdX in the environment overrides everything and behaves like
 * the classic whole-disk wipe (automation path). */

static int has_swap(void) { return g_syscfg.swap_gib > 0; }

/* partition variables for the auto (wipe / DISK= env) layout, in disk order:
 * [bios-boot|ESP] [boot(luks)] [swap] root */
static void emit_layout_vars(FILE *fp, const int s[CAT_COUNT], const char *ind)
{
    int idx = 1;
    if (s[CAT_FIRMWARE] == FW_BIOS) { fprintf(fp, "%sESP=\"\"\n", ind); idx++; }
    else fprintf(fp, "%sESP=\"${P}%d\"\n", ind, idx++);
    if (s[CAT_CRYPT] == E_LUKS) fprintf(fp, "%sBOOTP=\"${P}%d\"\n", ind, idx++);
    if (has_swap()) fprintf(fp, "%sSWAPP=\"${P}%d\"\n", ind, idx++);
    fprintf(fp, "%sROOTP=\"${P}%d\"\n", ind, idx);
}

static void emit_env_path(FILE *fp, const int s[CAT_COUNT])
{
    fputs("  if [ -n \"${DISK:-}\" ]; then\n"
          "    # non-interactive: DISK given in the environment — whole disk, no questions\n"
          "    case \"$DISK\" in *[0-9]) P=\"${DISK}p\";; *) P=\"$DISK\";; esac\n"
          "    WIPE=1\n", fp);
    emit_layout_vars(fp, s, "    ");
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
        fputs("  WIPE=1\n", fp);
        emit_layout_vars(fp, s, "  ");
        break;
    case DM_FREE:
        fputs("  free_space\n", fp);
        break;
    case DM_ONEPART:
        fprintf(fp, "  onepart %s\n", c->onepart);
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
        if (c->swapp[0])
            fprintf(fp, "  SWAPP=%s\n"
                        "  [ -b \"$SWAPP\" ] || { echo \"not a partition: $SWAPP\" >&2; exit 1; }\n",
                    c->swapp);
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
    fputs("  msg 'Install target'\n"
          "  lsblk -d -e7 -o NAME,SIZE,MODEL,TRAN\n"
          "  printf 'Disk (e.g. sda, nvme0n1): '; read -r a; DISK=$(devpath \"$a\")\n"
          "  [ -b \"$DISK\" ] || { echo \"not a block device: $DISK\" >&2; exit 1; }\n"
          "  case \"$DISK\" in *[0-9]) P=\"${DISK}p\";; *) P=\"$DISK\";; esac\n"
          "  cat <<'MENU'\n"
          "How should this disk be used?\n"
          "  1) Whole disk      WIPE EVERYTHING and auto-partition (boot/swap/root made for you)\n"
          "  2) Free space      keep existing OSes, install into unallocated space (dual-boot)\n"
          "  3) Existing parts  you already made partitions; pick them\n"
          "  4) One partition   DELETE one partition, auto-create the layout in its place\n"
          "  5) Shrink first    shrink an ext4/NTFS partition to make room (dual-boot)\n"
          "  6) cfdisk          edit the partition table yourself, then pick partitions\n"
          "MENU\n"
          "  printf 'Choice [1-6]: '; read -r m\n"
          "  case \"$m\" in\n"
          "  1) WIPE=1\n", fp);
    emit_layout_vars(fp, s, "     ");
    fputs("  ;;\n"
          "  2) free_space ;;\n"
          "  3) pick_parts ;;\n"
          "  4) lsblk -o NAME,SIZE,FSTYPE,PARTLABEL \"$DISK\"\n"
          "     printf 'Partition to REPLACE with the auto layout: '; read -r a\n"
          "     onepart \"$(devpath \"$a\")\" ;;\n"
          "  5) shrink; free_space ;;\n"
          "  6) cfdisk \"$DISK\"; partprobe \"$DISK\"; sleep 1; pick_parts ;;\n"
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
    fputs("    [ -z \"${SWAPP:-}\" ] || echo \"  swap: $SWAPP (formatted as swap)\"\n"
          "    echo \"  root: $ROOTP (FORMATTED — everything on it is lost)\"\n"
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
    fputs("  printf 'Swap partition (Enter = none): '; read -r a\n"
          "  if [ -n \"$a\" ]; then\n"
          "    SWAPP=$(devpath \"$a\"); [ -b \"$SWAPP\" ] || { echo \"not a partition: $SWAPP\" >&2; exit 1; }\n"
          "  fi\n"
          "  printf 'Root partition (WILL be formatted): '; read -r a\n"
          "  ROOTP=$(devpath \"$a\"); [ -b \"$ROOTP\" ] || { echo \"not a partition: $ROOTP\" >&2; exit 1; }\n"
          "}\n\n", fp);

    /* create partitions in unallocated space; reuse an existing ESP if present */
    fputs("free_space() {\n"
          "  WIPE=0\n"
          "  [ \"$(blkid -s PTTYPE -o value \"$DISK\")\" = gpt ] || {\n"
          "    echo 'This mode needs a GPT disk. Use existing parts or whole-disk.' >&2; exit 1; }\n", fp);
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
    if (has_swap())
        fprintf(fp,
              "  sgdisk -n0:0:+%dG -t0:8200 -c0:lyw-swap \"$DISK\"\n"
              "  partprobe \"$DISK\"; sleep 1\n"
              "  SWAPP=$(by_partlabel lyw-swap)\n", g_syscfg.swap_gib);
    fputs("  sgdisk -n0:0:0 -t0:8300 -c0:lyw-root \"$DISK\"   # takes the largest free block\n"
          "  partprobe \"$DISK\"; sleep 1\n"
          "  ROOTP=$(by_partlabel lyw-root)\n"
          "  [ -n \"$ROOTP\" ] || { echo 'could not create a root partition — no free space?' >&2; exit 1; }\n"
          "}\n\n", fp);

    /* delete one partition, then build the auto layout in the freed space */
    fputs("onepart() {\n"
          "  TP=$1\n"
          "  [ -b \"$TP\" ] || { echo \"not a partition: $TP\" >&2; exit 1; }\n"
          "  lsblk -o NAME,SIZE,FSTYPE,PARTLABEL \"$TP\"\n"
          "  echo \"$TP will be DELETED and replaced by an automatic boot/swap/root layout.\"\n"
          "  printf 'Type YES to delete it: '; read -r ok\n"
          "  [ \"$ok\" = YES ] || exit 1\n"
          "  PN=$(cat \"/sys/class/block/$(basename \"$TP\")/partition\")\n"
          "  sgdisk -d \"$PN\" \"$DISK\"\n"
          "  partprobe \"$DISK\"; sleep 1\n"
          "  free_space\n"
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
    int idx = 1;

    fputs("partition() {\n"
          "  if [ \"$WIPE\" = 1 ]; then\n"
          "    msg \"Partitioning $DISK (GPT auto layout)\"\n"
          "    sgdisk --zap-all \"$DISK\"\n"
          "    sgdisk \\\n", fp);
    if (bios)
        fprintf(fp, "      -n%d:0:+1M -t%d:ef02 -c%d:lyw-biosboot \\\n", idx, idx, idx), idx++;
    else
        fprintf(fp, "      -n%d:0:+512M -t%d:ef00 -c%d:EFI \\\n", idx, idx, idx), idx++;
    if (luks)
        fprintf(fp, "      -n%d:0:+1G -t%d:8300 -c%d:lyw-boot \\\n", idx, idx, idx), idx++;
    if (has_swap())
        fprintf(fp, "      -n%d:0:+%dG -t%d:8200 -c%d:lyw-swap \\\n",
                idx, g_syscfg.swap_gib, idx, idx), idx++;
    fprintf(fp, "      -n%d:0:0 -t%d:%s -c%d:lyw-root \"$DISK\"\n",
            idx, idx, luks ? "8309" : "8300", idx);
    fputs("    partprobe \"$DISK\"; sleep 1\n"
          "  fi\n", fp);
    if (!luks)
        fputs("  ROOTDEV=\"$ROOTP\"\n", fp);
    if (!bios)
        fputs("  if [ \"$FORMAT_ESP\" = 1 ]; then mkfs.fat -F32 \"$ESP\"; fi\n", fp);
    fputs("  if [ -n \"${SWAPP:-}\" ]; then mkswap -f \"$SWAPP\"; swapon \"$SWAPP\" 2>/dev/null || true; fi\n", fp);
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
