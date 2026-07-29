#ifndef LYW_H
#define LYW_H

/* Linux Your Way — pick every layer of your Linux system. */

enum { CAT_FIRMWARE, CAT_LIBC, CAT_CORE, CAT_SHELL, CAT_INIT, CAT_PM,
       CAT_KERNEL, CAT_KBUILD, CAT_KCC, CAT_KCOMPRESS, CAT_KMOD, CAT_KINITRD,
       CAT_TC, CAT_TCPROFILE, CAT_GFX, CAT_BOOT, CAT_SECBOOT,
       CAT_FS, CAT_CRYPT, CAT_NET, CAT_LOGIN, CAT_PRIVESC, CAT_SECURITY,
       CAT_INSTALL, CAT_COUNT };

enum { FW_UEFI, FW_BIOS };
enum { L_GLIBC, L_MUSL, L_UCLIBC };
enum { C_GNU, C_BUSYBOX, C_TOYBOX };
enum { SH_BASH, SH_DASH, SH_ASH, SH_ZSH, SH_FISH };
enum { I_DINIT, I_RUNIT, I_OPENRC, I_S6, I_66, I_FINIT, I_SYSTEMD,
       I_SYSVINIT, I_BUSYBOX, I_SHEPHERD, I_CUSTOM };
enum { P_PACMAN, P_XBPS, P_APK, P_PORTAGE, P_NIX, P_GUIX, P_CRUX, P_NONE, P_CUSTOM };
enum { K_LTS, K_VANILLA, K_ZEN, K_HARDENED, K_RT, K_CUSTOM };
enum { KB_PRECOMPILED, KB_LOCAL, KB_MENUCONFIG, KB_OWNCONFIG };
enum { KC_GCC, KC_CLANG };
enum { KZ_ZSTD, KZ_GZIP, KZ_XZ, KZ_LZ4 };
enum { KM_MODULES, KM_MONO };
enum { KI_SEPARATE, KI_BUILTIN, KI_NONE };
enum { TC_GNU, TC_LLVM, TC_BOTH };
enum { TP_COMPAT, TP_SIZE, TP_PERF, TP_DEBUG, TP_HARDENED };
enum { G_NONE, G_OPENBOX, G_XFCE, G_SWAY, G_NIRI, G_NVWM };
enum { B_GRUB, B_LIMINE, B_SDBOOT, B_REFIND, B_SYSLINUX, B_EFISTUB };
enum { SB_OFF, SB_SBCTL };
enum { F_EXT4, F_BTRFS };
enum { E_NONE, E_LUKS };
enum { N_DHCPCD, N_NM, N_IWD, N_NETWORKD, N_NONE };
enum { LG_PASSWORD, LG_ROOTAUTO };
enum { PE_SUDO, PE_DOAS, PE_NONE };
enum { SEC_NONE, SEC_FIREWALL, SEC_HARDEN, SEC_APPARMOR, SEC_SELINUX };
enum { IN_BINARY, IN_SOURCE, IN_HYBRID };

/* install-target mode chosen in the TUI (before anything else) */
enum { DM_ASK, DM_WHOLE, DM_FREE, DM_PARTS, DM_ONEPART };

/* compatibility status, ordered from best to worst */
enum { ST_OK, ST_EXPERIMENTAL, ST_MANUAL, ST_INCOMPATIBLE };

typedef struct { const char *name, *val, *desc; } option_t;
typedef struct { const char *title, *key; const option_t *opts; int nopts; } category_t;
typedef struct { int status; unsigned mask; char msg[160]; } finding_t;

/* free-form settings that are not enum picks */
typedef struct {
    int  disk_mode;                     /* DM_* */
    char disk[64];                      /* /dev/... ("" with DM_ASK) */
    char esp[64], bootp[64], rootp[64]; /* DM_PARTS picks ("" = n/a) */
    char swapp[64];                     /* DM_PARTS: existing swap partition */
    char onepart[64];                   /* DM_ONEPART: partition to replace */
    int  swap_gib;                      /* swap size for auto layouts, 0 = none */
    char hostname[64];
    char timezone[64];
    char locale[40];
    char keymap[32];
    char rootpw[128];                   /* plaintext in RAM only; exported as SHA-512 hash */
    char username[32];                  /* "" = no user account */
    char userpw[128];
    char kver[40];                      /* git tag/branch for source kernel builds, "" = default */
    char kurl[160];                     /* custom kernel fork URL, "" = kernel.org stable */
    int  boot_timeout;                  /* bootloader menu timeout, seconds */
} syscfg_t;

extern const category_t categories[CAT_COUNT];
extern syscfg_t g_syscfg;

void config_defaults(int sel[CAT_COUNT]);
void syscfg_defaults(syscfg_t *c);

#define MAX_FINDINGS 48
int compat_eval(const int sel[CAT_COUNT], finding_t *out, int max);
int compat_overall(const int sel[CAT_COUNT]);
/* worst status among findings that involve `cat` if option `opt` were chosen */
int compat_option_status(const int sel[CAT_COUNT], int cat, int opt);
const char *status_glyph(int st);
const char *status_word(int st);

/* target.c: firmware + disk + partition stage (first wizard screen) */
int target_stage(int sel[CAT_COUNT], syscfg_t *c);

int export_yaml(const int sel[CAT_COUNT], const char *dir);
int genbuild_sh(const int sel[CAT_COUNT], const char *dir);
/* SHA-512 crypt hash of pw into out (outsz >= 128); 0 on success */
int pw_hash(const char *pw, char *out, int outsz);

/* ui */
#define UI_BACK  (-1)
#define UI_QUIT  (-2)
typedef struct { const char *label, *desc; int status; } item_t; /* status -1: no marker */
void ui_init(void);
void ui_end(void);
int ui_menu(const char *title, const char *sub, const item_t *items, int n, int cur);
/* line editor; def shown as initial text. 0 ok, UI_BACK (esc) or UI_QUIT */
int ui_input(const char *title, const char *prompt, const char *def,
             char *out, int outsz, int hidden);
int ui_summary(const int sel[CAT_COUNT]); /* returns 'i', 'e', 'b', 'm' or 'q' */
void ui_message(const char *title, const char *l1, const char *l2);

#endif
