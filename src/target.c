#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lyw.h"

/* ---------- disk & partition picking (runs on this machine) ---------- */

#define MAXPICK 14

/* run cmd, one item per output line; returns count */
static int lines_to_items(const char *cmd, char store[][96], item_t *items, int max)
{
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    int n = 0;
    char line[96];
    while (n < max && fgets(line, sizeof line, p)) {
        line[strcspn(line, "\n")] = '\0';
        if (!line[0]) continue;
        snprintf(store[n], 96, "%s", line);
        items[n].label = store[n];
        items[n].desc = NULL;
        items[n].status = -1;
        n++;
    }
    pclose(p);
    return n;
}

static void first_word(const char *line, char *out, int outsz)
{
    int i = 0;
    while (line[i] && line[i] != ' ' && i < outsz - 1) { out[i] = line[i]; i++; }
    out[i] = '\0';
}

int pick_partition(const char *disk, const char *what, int optional,
                          char *out, int outsz)
{
    char cmd[256], store[MAXPICK][96], name[40];
    item_t items[MAXPICK + 1];
    snprintf(cmd, sizeof cmd,
             "lsblk -ln -o NAME,SIZE,FSTYPE,PARTLABEL %s 2>/dev/null | tail -n +2", disk);
    int n = lines_to_items(cmd, store, items, MAXPICK);
    if (optional) {
        items[n].label = "(skip)";
        items[n].desc = "Leave unset.";
        items[n].status = -1;
        n++;
    }
    if (n == 0) return UI_BACK;
    char title[96];
    snprintf(title, sizeof title, "Install target — %s", what);
    int r = ui_menu(title, "Partitions on the chosen disk.", items, n, 0);
    if (r < 0) return r;
    if (optional && r == n - 1) { out[0] = '\0'; return 0; }
    first_word(store[r], name, sizeof name);
    snprintf(out, outsz, "/dev/%s", name);
    return 0;
}

/* firmware + disk + mode + (partitions). 0 ok, UI_BACK, UI_QUIT */
int target_stage(int sel[CAT_COUNT], syscfg_t *c)
{
    /* firmware first: it decides which loaders/partitions make sense */
    struct stat st;
    int fwdef = stat("/sys/firmware/efi", &st) == 0 ? FW_UEFI : FW_BIOS;
    item_t fw[2];
    for (int o = 0; o < 2; o++) {
        fw[o].label = categories[CAT_FIRMWARE].opts[o].name;
        fw[o].desc  = categories[CAT_FIRMWARE].opts[o].desc;
        fw[o].status = -1;
    }
    fw[fwdef].desc = fwdef == FW_UEFI
        ? "Auto-detected: this machine booted with UEFI."
        : "Auto-detected: this machine booted in BIOS/legacy mode.";

step_fw:;
    int r = ui_menu("Install target — firmware", "How does this machine boot?",
                    fw, 2, sel[CAT_FIRMWARE] >= 0 ? sel[CAT_FIRMWARE] : fwdef);
    if (r < 0) return r;
    sel[CAT_FIRMWARE] = r;

step_disk:;
    char store[MAXPICK][96];
    item_t items[MAXPICK + 1];
    int nd = lines_to_items("lsblk -dn -e7 -o NAME,SIZE,MODEL,TRAN 2>/dev/null",
                            store, items, MAXPICK);
    items[nd].label = "Decide at install time";
    items[nd].desc = "Export a portable config; build.sh asks for the disk when it runs.";
    items[nd].status = -1;
    r = ui_menu("Install target — disk",
                "WHERE will this system be installed? Nothing is written before build.sh runs.",
                items, nd + 1, 0);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_fw;
    if (r == nd) {
        c->disk_mode = DM_ASK;
        c->disk[0] = c->esp[0] = c->bootp[0] = c->rootp[0] = '\0';
        return 0;
    }
    char name[40];
    first_word(store[r], name, sizeof name);
    snprintf(c->disk, sizeof c->disk, "/dev/%s", name);

step_mode:;
    char sub[128];
    snprintf(sub, sizeof sub, "How should %s be used?", c->disk);
    static const item_t modes[] = {
        { "Whole disk (automatic)", "WIPE EVERYTHING; boot, swap and root are created for you. Simplest.", -1 },
        { "One partition (automatic)","Pick ONE partition: it is deleted and boot/swap/root are auto-created in its place.", -1 },
        { "Free space",             "Keep existing OSes; install into unallocated space (dual-boot).", -1 },
        { "Existing partitions (manual)","Pick boot, swap and root yourself; only those are touched.", -1 },
        { "Edit partitions (cfdisk)","Open cfdisk on this disk NOW to create/resize/delete partitions, then pick them.", -1 },
        { "Decide at install",      "Keep the disk pick, answer the layout questions when build.sh runs.", -1 },
    };
    r = ui_menu("Install target — mode", sub, modes, 6, 0);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_disk;
    if (r == 5) { c->disk_mode = DM_ASK; return 0; }
    c->esp[0] = c->bootp[0] = c->rootp[0] = c->swapp[0] = c->onepart[0] = '\0';
    if (r == 1) {
        r = pick_partition(c->disk,
                           "partition to REPLACE (deleted; auto layout in its place)", 0,
                           c->onepart, sizeof c->onepart);
        if (r == UI_QUIT) return r;
        if (r == UI_BACK) goto step_mode;
        c->disk_mode = DM_ONEPART;
        return 0;
    }
    if (r == 4) {
        /* partition editor, then pick the partitions just made */
        ui_end();
        char cmd[96];
        snprintf(cmd, sizeof cmd, "cfdisk %s", c->disk);
        if (system(cmd) == -1) { /* cfdisk missing: picker below still works */ }
        ui_init();
        c->disk_mode = DM_PARTS;
    } else {
        c->disk_mode = r == 0 ? DM_WHOLE : r == 2 ? DM_FREE : DM_PARTS;
    }
    if (c->disk_mode != DM_PARTS) return 0;

    if (sel[CAT_FIRMWARE] == FW_UEFI) {
        r = pick_partition(c->disk, "EFI system partition (kept as-is)", 0,
                           c->esp, sizeof c->esp);
        if (r == UI_QUIT) return r;
        if (r == UI_BACK) goto step_mode;
    }
    r = pick_partition(c->disk, "root partition (WILL BE FORMATTED)", 0,
                       c->rootp, sizeof c->rootp);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_mode;
    r = pick_partition(c->disk, "swap partition (optional)", 1,
                       c->swapp, sizeof c->swapp);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_mode;
    r = pick_partition(c->disk,
                       "/boot partition (optional; needed only with LUKS — formatted ext4)", 1,
                       c->bootp, sizeof c->bootp);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_mode;
    return 0;
}

