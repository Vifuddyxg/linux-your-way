#ifndef GEN_H
#define GEN_H
#include <stdio.h>
#include "lyw.h"

/* shared between the build.sh generator files:
 *   genbuild.c — orchestration, header vars, sysconfig, fstab
 *   genbase.c  — per-package-manager base install, pkg()/svc_enable() helpers
 *   genboot.c  — kernel build/install + bootloader + secure boot
 *   gensys.c   — toolchain, users/passwords/shell, locale/time/keymap
 *   gengfx.c   — graphics (WM/DE + greeter) and network sections
 *   gendisk.c  — target selection (TUI preset or interactive) + partitioning */

void emit_pkg_helper(FILE *fp, const int s[CAT_COUNT]);
void emit_svc_helper(FILE *fp, const int s[CAT_COUNT]);
void emit_base(FILE *fp, const int s[CAT_COUNT]);
void emit_altinit_call(FILE *fp, const int s[CAT_COUNT]);
void emit_graphics(FILE *fp, const int s[CAT_COUNT]);
void emit_network(FILE *fp, const int s[CAT_COUNT]);
void emit_skeletons(const int s[CAT_COUNT], const char *dir);
void emit_choose_target(FILE *fp, const int s[CAT_COUNT]);
void emit_partition(FILE *fp, const int s[CAT_COUNT]);
void emit_kernel(FILE *fp, const int s[CAT_COUNT]);
void emit_bootloader(FILE *fp, const int s[CAT_COUNT]);
void emit_secboot(FILE *fp, const int s[CAT_COUNT]);
void emit_toolchain(FILE *fp, const int s[CAT_COUNT]);
void emit_users(FILE *fp, const int s[CAT_COUNT]);
void emit_localetime(FILE *fp, const int s[CAT_COUNT]);
void emit_security(FILE *fp, const int s[CAT_COUNT]);
void emit_branding(FILE *fp, const int s[CAT_COUNT]);
const char *kernel_pkg(int pm, int kernel);
int kernel_from_source_cfg(const int s[CAT_COUNT]);

#endif
