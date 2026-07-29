#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lyw.h"

static const char *g_outdir = "lyw-out";

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

static int pick_partition(const char *disk, const char *what, int optional,
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
static int target_stage(int sel[CAT_COUNT], syscfg_t *c)
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
        { "Whole disk",             "WIPE EVERYTHING on this disk and auto-partition it. Simplest.", -1 },
        { "Free space",             "Keep existing OSes; install into unallocated space (dual-boot).", -1 },
        { "Existing partitions",    "You already made partitions; pick them explicitly (only those are touched).", -1 },
        { "Edit partitions (cfdisk)","Open cfdisk on this disk NOW to create/resize/delete partitions, then pick them.", -1 },
        { "Decide at install",      "Keep the disk pick, answer the layout questions when build.sh runs.", -1 },
    };
    r = ui_menu("Install target — mode", sub, modes, 5, 0);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_disk;
    if (r == 4) { c->disk_mode = DM_ASK; return 0; }
    if (r == 3) {
        /* partition editor, then pick the partitions just made */
        ui_end();
        char cmd[96];
        snprintf(cmd, sizeof cmd, "cfdisk %s", c->disk);
        if (system(cmd) == -1) { /* cfdisk missing: picker below still works */ }
        ui_init();
        c->disk_mode = DM_PARTS;
    } else {
        c->disk_mode = r == 0 ? DM_WHOLE : r == 1 ? DM_FREE : DM_PARTS;
    }
    c->esp[0] = c->bootp[0] = c->rootp[0] = '\0';
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
    r = pick_partition(c->disk,
                       "/boot partition (optional; needed only with LUKS — formatted ext4)", 1,
                       c->bootp, sizeof c->bootp);
    if (r == UI_QUIT) return r;
    if (r == UI_BACK) goto step_mode;
    return 0;
}

/* ---------- category wizard (with conditional questions) ---------- */

static int skip_cat(const int sel[CAT_COUNT], int i)
{
    switch (i) {
    case CAT_FIRMWARE:                  /* asked in target_stage */
        return 1;
    case CAT_KCC: case CAT_KCOMPRESS: case CAT_KMOD: case CAT_KINITRD:
        return sel[CAT_KBUILD] == KB_PRECOMPILED;
    case CAT_SECBOOT:
        return sel[CAT_FIRMWARE] == FW_BIOS;
    }
    return 0;
}

static int wizard(int sel[CAT_COUNT])
{
    int i = 0;
    while (i < CAT_COUNT) {
        if (skip_cat(sel, i)) { i++; continue; }
        const category_t *c = &categories[i];
        item_t items[32];
        for (int o = 0; o < c->nopts && o < 32; o++) {
            items[o].label = c->opts[o].name;
            items[o].desc = c->opts[o].desc;
            items[o].status = compat_option_status(sel, i, o);
        }
        int step = 0, steps = 0;
        for (int k = 0; k < CAT_COUNT; k++) {
            if (skip_cat(sel, k)) continue;
            steps++;
            if (k < i) step++;
        }
        char title[64];
        snprintf(title, sizeof title, "%d/%d  %s", step + 1, steps, c->title);
        int r = ui_menu(title, "Markers show compatibility with your other choices.",
                        items, c->nopts, sel[i]);
        if (r == UI_QUIT) return UI_QUIT;
        if (r == UI_BACK) {
            do {
                if (i == 0) return UI_BACK;
                i--;
            } while (skip_cat(sel, i));
            continue;
        }
        sel[i] = r;
        i++;
    }
    return 0;
}

/* ---------- system settings (hostname, passwords, time, kernel extras) ---------- */

typedef struct {
    const char *title, *prompt;
    char *buf; int size;
    int hidden;
    int (*active)(const int sel[CAT_COUNT]);
} field_t;

static int f_user_set(const int sel[CAT_COUNT])
{ (void)sel; return g_syscfg.username[0] != '\0'; }
static int f_ksource(const int sel[CAT_COUNT])
{ return sel[CAT_KBUILD] != KB_PRECOMPILED || sel[CAT_KERNEL] == K_CUSTOM ||
         sel[CAT_KERNEL] == K_RT; }
static int f_kcustom(const int sel[CAT_COUNT])
{ return sel[CAT_KERNEL] == K_CUSTOM; }

static int settings_stage(const int sel[CAT_COUNT], syscfg_t *c)
{
    char timeout[8];
    snprintf(timeout, sizeof timeout, "%d", c->boot_timeout);
    char rootpw2[128], userpw2[128];
    field_t fields[] = {
      { "Settings — hostname", "System hostname:", c->hostname, sizeof c->hostname, 0, NULL },
      { "Settings — timezone", "Timezone (from /usr/share/zoneinfo, e.g. Europe/Bucharest):",
        c->timezone, sizeof c->timezone, 0, NULL },
      { "Settings — locale", "Locale (e.g. en_US.UTF-8; ignored on musl):",
        c->locale, sizeof c->locale, 0, NULL },
      { "Settings — keymap", "Console keymap (e.g. us, ro, de):", c->keymap, sizeof c->keymap, 0, NULL },
      { "Settings — root password", "Root password (empty = set it manually after install):",
        c->rootpw, sizeof c->rootpw, 1, NULL },
      { "Settings — root password", "Repeat root password:", rootpw2, sizeof rootpw2, 1, NULL },
      { "Settings — user account", "User account to create (empty = none):",
        c->username, sizeof c->username, 0, NULL },
      { "Settings — user password", "Password for the user:", c->userpw, sizeof c->userpw, 1, f_user_set },
      { "Settings — user password", "Repeat user password:", userpw2, sizeof userpw2, 1, f_user_set },
      { "Settings — boot menu", "Bootloader menu timeout in seconds:", timeout, sizeof timeout, 0, NULL },
      { "Settings — kernel version", "Kernel git tag/branch (empty = default branch, e.g. v6.12):",
        c->kver, sizeof c->kver, 0, f_ksource },
      { "Settings — kernel fork", "Git URL of your kernel tree (empty = kernel.org stable):",
        c->kurl, sizeof c->kurl, 0, f_kcustom },
    };
    const int nf = (int)(sizeof fields / sizeof fields[0]);
    rootpw2[0] = userpw2[0] = '\0';

    int i = 0;
    while (i < nf) {
        field_t *fl = &fields[i];
        if (fl->active && !fl->active(sel)) { i++; continue; }
        int r = ui_input(fl->title, fl->prompt, fl->buf, fl->buf, fl->size, fl->hidden);
        if (r == UI_QUIT) return UI_QUIT;
        if (r == UI_BACK) {
            do {
                if (i == 0) return UI_BACK;
                i--;
            } while (fields[i].active && !fields[i].active(sel));
            continue;
        }
        /* password confirmations */
        if (fl->buf == rootpw2 && strcmp(rootpw2, c->rootpw) != 0) {
            ui_message("Passwords differ", "The two root passwords do not match.", "Try again.");
            i--; continue;
        }
        if (fl->buf == userpw2 && strcmp(userpw2, c->userpw) != 0) {
            ui_message("Passwords differ", "The two user passwords do not match.", "Try again.");
            i--; continue;
        }
        i++;
    }
    c->boot_timeout = atoi(timeout);
    if (c->boot_timeout < 0 || c->boot_timeout > 60) c->boot_timeout = 3;
    return 0;
}

/* ---------- main flow ---------- */

static int do_export(const int sel[CAT_COUNT])
{
    if (export_yaml(sel, g_outdir) != 0) return -1;
    if (genbuild_sh(sel, g_outdir) != 0) return -1;
    return 0;
}

/* leave curses, run the generated installer for real, come back */
static int run_install(void)
{
    ui_end();
    printf("\n==> Running %s/build.sh — this IS the install.\n"
           "==> It will still show the target and ask you to type YES.\n\n",
           g_outdir);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "sh '%s/build.sh'", g_outdir);
    int rc = system(cmd);
    printf("\n==> build.sh finished (exit %d). Press Enter to return to lyw... ",
           rc);
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
    ui_init();
    if (rc == 0)
        ui_message("Install finished",
                   "build.sh completed successfully.",
                   "Remove the USB stick and reboot into your new system.");
    else
        ui_message("Install did not finish",
                   "build.sh exited with an error — the messages above have the details.",
                   "Fix the issue (or change the config) and install again.");
    return rc;
}

/* drop out of curses, run a network TUI, come back */
static void network_setup(void)
{
    ui_end();
    int r = -1;
    if (system("command -v nmtui >/dev/null 2>&1") == 0)
        r = system("nmtui");
    else if (system("command -v iwctl >/dev/null 2>&1") == 0)
        r = system("iwctl");
    ui_init();
    if (r == -1)
        ui_message("Network setup",
                   "No network TUI found (nmtui/iwctl).",
                   "Connect manually: ip link, dhcpcd <iface>, wpa_supplicant.");
}

/* target → wizard → settings → summary; stages linked by Back */
static int configure(int sel[CAT_COUNT])
{
    int stage = 0;
    while (stage < 3) {
        int r = stage == 0 ? target_stage(sel, &g_syscfg)
              : stage == 1 ? wizard(sel)
              :              settings_stage(sel, &g_syscfg);
        if (r == UI_QUIT) return UI_QUIT;
        if (r == UI_BACK) {
            if (stage == 0) return UI_BACK;
            stage--;
            continue;
        }
        stage++;
    }
    return 0;
}

static void tui_run(void)
{
    static const item_t actions[] = {
        { "Configure your system", "Every layer is yours to pick: target disk, libc, init, kernel, graphics...", -1 },
        { "Network setup",         "Connect to a network first (needed to download packages).", -1 },
        { "Quit",                  NULL, -1 },
    };
    ui_init();
    int cur = 0;
    for (;;) {
        int a = ui_menu("Linux Your Way", "Total control — there is no easy mode.", actions, 3, cur);
        if (a == UI_QUIT || a == UI_BACK || a == 2) break;
        cur = a;
        if (a == 1) { network_setup(); continue; }

        int sel[CAT_COUNT];
        config_defaults(sel);
        syscfg_defaults(&g_syscfg);
        int r = configure(sel);
        if (r == UI_QUIT) break;
        if (r == UI_BACK) continue;

        int done = 0;
        while (!done) {
            char l1[256];
            switch (ui_summary(sel)) {
            case 'i':
                if (do_export(sel) != 0) {
                    snprintf(l1, sizeof l1, "Could not write to %s/", g_outdir);
                    ui_message("Export failed", l1, NULL);
                    break;
                }
                run_install();
                break;
            case 'e':
                if (do_export(sel) == 0) {
                    snprintf(l1, sizeof l1, "Wrote %s/lyw.yaml and %s/build.sh",
                             g_outdir, g_outdir);
                    ui_message("Exported", l1,
                               "Pick [I] to install, or run build.sh yourself later.");
                } else {
                    snprintf(l1, sizeof l1, "Could not write to %s/", g_outdir);
                    ui_message("Export failed", l1, NULL);
                }
                break;
            case 'b':
                if (configure(sel) == UI_QUIT) { ui_end(); return; }
                break;
            case 'm':
                done = 1;
                break;
            case 'q':
                ui_end();
                return;
            }
        }
    }
    ui_end();
}

/* ---------- headless mode ---------- */

static int find_opt(const category_t *c, const char *val)
{
    for (int o = 0; o < c->nopts; o++)
        if (!strcmp(c->opts[o].val, val)) return o;
    return -1;
}

static int apply_set(int sel[CAT_COUNT], const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq) return -1;
    for (int c = 0; c < CAT_COUNT; c++) {
        if (strlen(categories[c].key) == (size_t)(eq - kv) &&
            !strncmp(categories[c].key, kv, eq - kv)) {
            int o = find_opt(&categories[c], eq + 1);
            if (o < 0) return -1;
            sel[c] = o;
            return 0;
        }
    }
    return -1;
}

static void print_config(const int sel[CAT_COUNT])
{
    for (int c = 0; c < CAT_COUNT; c++)
        printf("  %-16s %s\n", categories[c].key, categories[c].opts[sel[c]].val);
    finding_t f[MAX_FINDINGS];
    int nf = compat_eval(sel, f, MAX_FINDINGS);
    if (nf > MAX_FINDINGS) nf = MAX_FINDINGS;
    printf("compatibility: %s %s\n",
           status_glyph(compat_overall(sel)), status_word(compat_overall(sel)));
    for (int i = 0; i < nf; i++)
        printf("  %s %s\n", status_glyph(f[i].status), f[i].msg);
}

static int usage(void)
{
    fprintf(stderr,
        "usage: lyw                            interactive TUI\n"
        "       lyw --set k=v [--set k=v]... [--out DIR]\n"
        "           headless: export lyw.yaml + build.sh\n"
        "keys for --set: ");
    for (int c = 0; c < CAT_COUNT; c++)
        fprintf(stderr, "%s%s", categories[c].key, c == CAT_COUNT - 1 ? "\n" : ", ");
    return 2;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    int headless = 0;
    int sel[CAT_COUNT];
    config_defaults(sel);
    syscfg_defaults(&g_syscfg);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            if (apply_set(sel, argv[++i]) != 0) {
                fprintf(stderr, "lyw: bad --set '%s'\n", argv[i]);
                return 2;
            }
            headless = 1;
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            g_outdir = argv[++i];
        } else {
            return usage();
        }
    }

    if (!headless) {
        tui_run();
        return 0;
    }

    print_config(sel);
    if (do_export(sel) != 0) {
        fprintf(stderr, "lyw: could not write to %s/\n", g_outdir);
        return 1;
    }
    printf("wrote %s/lyw.yaml and %s/build.sh\n", g_outdir, g_outdir);
    return 0;
}
