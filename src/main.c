#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lyw.h"

static const char *g_outdir = "lyw-out";

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
        /* picking a ✗ option: say immediately WHICH other layer it fights */
        if (compat_option_status(sel, i, r) == ST_INCOMPATIBLE) {
            finding_t f[MAX_FINDINGS];
            int nf = compat_eval(sel, f, MAX_FINDINGS);
            if (nf > MAX_FINDINGS) nf = MAX_FINDINGS;
            const char *why = "";
            for (int k = 0; k < nf; k++)
                if (f[k].status == ST_INCOMPATIBLE && (f[k].mask & (1u << i))) {
                    why = f[k].msg;
                    break;
                }
            ui_message("This combination cannot work", why,
                       "Keep it and change the OTHER layer later, or go Back and pick differently.");
        }
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
    char timeout[8], swapg[8];
    snprintf(timeout, sizeof timeout, "%d", c->boot_timeout);
    snprintf(swapg, sizeof swapg, "%d", c->swap_gib);
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
      { "Settings — swap", "Swap size in GiB for automatic layouts (0 = no swap):",
        swapg, sizeof swapg, 0, NULL },
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
    c->swap_gib = atoi(swapg);
    if (c->swap_gib < 0 || c->swap_gib > 256) c->swap_gib = 4;
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
        int a = ui_menu("Linux Your Way", "Pick every layer of your system — the wizard guides you through.", actions, 3, cur);
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
                if (compat_overall(sel) == ST_INCOMPATIBLE) {
                    finding_t f[MAX_FINDINGS];
                    int nf = compat_eval(sel, f, MAX_FINDINGS);
                    if (nf > MAX_FINDINGS) nf = MAX_FINDINGS;
                    const char *why = "";
                    for (int k = 0; k < nf; k++)
                        if (f[k].status == ST_INCOMPATIBLE) { why = f[k].msg; break; }
                    ui_message("Cannot install: incompatible combination", why,
                               "Press [B] and change one of the two layers marked ✗.");
                    break;
                }
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
