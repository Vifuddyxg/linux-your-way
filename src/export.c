#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <sys/stat.h>
#include <crypt.h>
#include "lyw.h"

/* SHA-512 crypt ($6$) with a random salt; plaintext never leaves RAM */
int pw_hash(const char *pw, char *out, int outsz)
{
    static const char alpha[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    unsigned char rnd[16];
    FILE *ur = fopen("/dev/urandom", "r");
    if (!ur || fread(rnd, 1, sizeof rnd, ur) != sizeof rnd) {
        if (ur) fclose(ur);
        return -1;
    }
    fclose(ur);

    char setting[24] = "$6$";
    for (int i = 0; i < 16; i++)
        setting[3 + i] = alpha[rnd[i] & 63];
    setting[19] = '$';
    setting[20] = '\0';

    char *h = crypt(pw, setting);
    if (!h || h[0] == '*') return -1;
    snprintf(out, outsz, "%s", h);
    return 0;
}

static const char *dm_word(int m)
{
    switch (m) {
    case DM_WHOLE: return "whole-disk";
    case DM_FREE:  return "free-space";
    case DM_PARTS: return "existing-partitions";
    default:       return "ask-at-install";
    }
}

int export_yaml(const int sel[CAT_COUNT], const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/lyw.yaml", dir);
    mkdir(dir, 0755);

    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    const syscfg_t *c = &g_syscfg;

    fprintf(fp, "# Linux Your Way — system configuration\n");
    fprintf(fp, "lyw:\n");
    fprintf(fp, "  version: 2\n");
    fprintf(fp, "  architecture: x86_64\n");
    for (int i = 0; i < CAT_COUNT; i++)
        fprintf(fp, "  %s: %s\n", categories[i].key, categories[i].opts[sel[i]].val);

    fprintf(fp, "  target:\n");
    fprintf(fp, "    mode: %s\n", dm_word(c->disk_mode));
    if (c->disk[0])  fprintf(fp, "    disk: %s\n", c->disk);
    if (c->esp[0])   fprintf(fp, "    esp: %s\n", c->esp);
    if (c->bootp[0]) fprintf(fp, "    boot: %s\n", c->bootp);
    if (c->rootp[0]) fprintf(fp, "    root: %s\n", c->rootp);

    fprintf(fp, "  system:\n");
    fprintf(fp, "    hostname: \"%s\"\n", c->hostname);
    fprintf(fp, "    timezone: \"%s\"\n", c->timezone);
    fprintf(fp, "    locale: \"%s\"\n", c->locale);
    fprintf(fp, "    keymap: \"%s\"\n", c->keymap);
    fprintf(fp, "    user: \"%s\"\n", c->username);
    fprintf(fp, "    boot_timeout: %d\n", c->boot_timeout);
    /* passwords are NOT stored here; build.sh carries SHA-512 hashes only */
    fprintf(fp, "    root_password: %s\n", c->rootpw[0] ? "set-in-build.sh" : "unset");
    if (c->kver[0]) fprintf(fp, "    kernel_version: \"%s\"\n", c->kver);
    if (c->kurl[0]) fprintf(fp, "    kernel_url: \"%s\"\n", c->kurl);

    finding_t f[MAX_FINDINGS];
    int nf = compat_eval(sel, f, MAX_FINDINGS);
    if (nf > MAX_FINDINGS) nf = MAX_FINDINGS;
    fprintf(fp, "  compatibility:\n");
    fprintf(fp, "    overall: %s\n", status_word(compat_overall(sel)));
    if (nf == 0) {
        fprintf(fp, "    findings: []\n");
    } else {
        fprintf(fp, "    findings:\n");
        for (int i = 0; i < nf; i++) {
            fprintf(fp, "      - status: %s\n", status_word(f[i].status));
            fprintf(fp, "        note: \"%s\"\n", f[i].msg);
        }
    }
    fclose(fp);
    return 0;
}
