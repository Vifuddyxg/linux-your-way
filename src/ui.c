#include <ncurses.h>
#include <string.h>
#include "lyw.h"

/* color pairs: 1..4 = status colors, 5 = header, 6 = highlight */
#define CP_STATUS(st) ((st) + 1)
#define CP_HEADER 5
#define CP_HILIT  6

void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);
    start_color();
    use_default_colors();
    init_pair(CP_STATUS(ST_OK),           COLOR_GREEN,  -1);
    init_pair(CP_STATUS(ST_EXPERIMENTAL), COLOR_CYAN,   -1);
    init_pair(CP_STATUS(ST_MANUAL),       COLOR_YELLOW, -1);
    init_pair(CP_STATUS(ST_INCOMPATIBLE), COLOR_RED,    -1);
    init_pair(CP_HEADER, COLOR_BLACK, COLOR_CYAN);
    init_pair(CP_HILIT,  COLOR_BLACK, COLOR_WHITE);
}

void ui_end(void)
{
    endwin();
}

static void draw_frame(const char *title, const char *sub)
{
    erase();
    attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    mvprintw(0, 2, " Linux Your Way — assemble your own Linux, layer by layer ");
    attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
    attron(A_BOLD);
    mvprintw(2, 2, "%s", title);
    attroff(A_BOLD);
    if (sub && *sub)
        mvprintw(3, 2, "%s", sub);
    attron(A_DIM);
    mvprintw(LINES - 1, 2, "↑↓ move   Enter select   B back   Q quit");
    attroff(A_DIM);
}

int ui_menu(const char *title, const char *sub, const item_t *items, int n, int cur)
{
    if (cur < 0 || cur >= n) cur = 0;
    int off = 0;
    for (;;) {
        draw_frame(title, sub);
        int top = 5, rows = LINES - 4 - top;
        if (rows < 1) rows = 1;
        if (cur < off) off = cur;
        if (cur >= off + rows) off = cur - rows + 1;
        for (int i = off; i < n && i - off < rows; i++) {
            int y = top + i - off;
            if (i == cur) attron(COLOR_PAIR(CP_HILIT));
            mvprintw(y, 4, "%-*s", COLS > 60 ? 44 : COLS - 12, items[i].label);
            if (i == cur) attroff(COLOR_PAIR(CP_HILIT));
            if (items[i].status >= 0) {
                attron(COLOR_PAIR(CP_STATUS(items[i].status)) | A_BOLD);
                mvprintw(y, 2, "%s", status_glyph(items[i].status));
                attroff(COLOR_PAIR(CP_STATUS(items[i].status)) | A_BOLD);
            }
        }
        if (items[cur].desc) {
            attron(A_DIM);
            mvprintw(LINES - 3, 2, "%.*s", COLS - 4, items[cur].desc);
            attroff(A_DIM);
        }
        refresh();

        int ch = getch();
        switch (ch) {
        case KEY_UP: case 'k':   cur = (cur + n - 1) % n; break;
        case KEY_DOWN: case 'j': cur = (cur + 1) % n; break;
        case '\n': case KEY_ENTER: case KEY_RIGHT: case 'l':
            return cur;
        case 'b': case 'B': case KEY_LEFT: case 'h': case 27:
            return UI_BACK;
        case 'q': case 'Q': case ERR:   /* ERR: lost the terminal */
            return UI_QUIT;
        }
    }
}

int ui_input(const char *title, const char *prompt, const char *def,
             char *out, int outsz, int hidden)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", def ? def : "");
    if ((int)sizeof buf > outsz) buf[outsz - 1] = '\0';
    int len = strlen(buf);

    curs_set(1);
    for (;;) {
        draw_frame(title, "Enter accept   Esc back   (edit the value below)");
        mvprintw(6, 4, "%s", prompt);
        attron(A_BOLD);
        if (hidden) {
            char stars[256];
            memset(stars, '*', len); stars[len] = '\0';
            mvprintw(8, 4, "> %s ", stars);
        } else {
            mvprintw(8, 4, "> %s ", buf);
        }
        attroff(A_BOLD);
        move(8, 6 + len);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) break;
        if (ch == 27) { curs_set(0); return UI_BACK; }
        if (ch == ERR) { curs_set(0); return UI_QUIT; }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) buf[--len] = '\0';
        } else if (ch == 21) {          /* ^U clear */
            len = 0; buf[0] = '\0';
        } else if (ch >= 32 && ch < 127 && len < outsz - 1 &&
                   len < (int)sizeof buf - 1) {
            buf[len++] = (char)ch; buf[len] = '\0';
        }
    }
    curs_set(0);
    snprintf(out, outsz, "%s", buf);
    return 0;
}

static void summary_target(int y)
{
    const syscfg_t *c = &g_syscfg;
    attron(A_BOLD);
    switch (c->disk_mode) {
    case DM_WHOLE:
        attron(COLOR_PAIR(CP_STATUS(ST_INCOMPATIBLE)));
        mvprintw(y, 2, "Target: %s — the WHOLE disk will be WIPED", c->disk);
        attroff(COLOR_PAIR(CP_STATUS(ST_INCOMPATIBLE)));
        break;
    case DM_FREE:
        mvprintw(y, 2, "Target: %s — install into free space (existing OSes kept)", c->disk);
        break;
    case DM_PARTS:
        mvprintw(y, 2, "Target: %s  root=%s (formatted)  ESP=%s%s%s%s%s",
                 c->disk, c->rootp, c->esp[0] ? c->esp : "-",
                 c->swapp[0] ? "  swap=" : "", c->swapp,
                 c->bootp[0] ? "  boot=" : "", c->bootp);
        break;
    case DM_ONEPART:
        attron(COLOR_PAIR(CP_STATUS(ST_MANUAL)));
        mvprintw(y, 2, "Target: %s — %s is DELETED, boot/swap/root auto-created in its place",
                 c->disk, c->onepart);
        attroff(COLOR_PAIR(CP_STATUS(ST_MANUAL)));
        break;
    default:
        mvprintw(y, 2, "Target: chosen interactively when build.sh runs");
        break;
    }
    attroff(A_BOLD);
}

int ui_summary(const int sel[CAT_COUNT])
{
    finding_t f[MAX_FINDINGS];
    int nf = compat_eval(sel, f, MAX_FINDINGS);
    if (nf > MAX_FINDINGS) nf = MAX_FINDINGS;
    int overall = compat_overall(sel);
    const syscfg_t *c = &g_syscfg;

    for (;;) {
        draw_frame("Summary", NULL);
        summary_target(4);
        int half = (CAT_COUNT + 1) / 2, y0 = 6;
        for (int i = 0; i < CAT_COUNT; i++) {
            int col = i < half ? 2 : COLS / 2;
            int y = y0 + (i < half ? i : i - half);
            attron(A_DIM);
            mvprintw(y, col, "%-18s", categories[i].title);
            attroff(A_DIM);
            mvprintw(y, col + 19, "%.*s", COLS / 2 - 21,
                     categories[i].opts[sel[i]].name);
        }
        int y = y0 + half;
        attron(A_DIM);
        mvprintw(y, 2, "host %s  tz %s  locale %s  keymap %s  user %s  rootpw %s",
                 c->hostname, c->timezone, c->locale, c->keymap,
                 c->username[0] ? c->username : "-",
                 c->rootpw[0] ? "set" : "unset");
        attroff(A_DIM);
        y += 2;
        attron(COLOR_PAIR(CP_STATUS(overall)) | A_BOLD);
        mvprintw(y++, 2, "%s Compatibility: %s", status_glyph(overall), status_word(overall));
        attroff(COLOR_PAIR(CP_STATUS(overall)) | A_BOLD);
        int shown = 0;
        for (int i = 0; i < nf && y < LINES - 3; i++, y++, shown++) {
            attron(COLOR_PAIR(CP_STATUS(f[i].status)));
            mvprintw(y, 4, "%s %.*s", status_glyph(f[i].status), COLS - 8, f[i].msg);
            attroff(COLOR_PAIR(CP_STATUS(f[i].status)));
        }
        if (shown < nf)
            mvprintw(LINES - 3, 4, "(+%d more findings — all listed in lyw.yaml)", nf - shown);
        attron(A_BOLD);
        mvprintw(LINES - 2, 2,
                 "[I]/Enter Install now   [E] Export only   [B] Back   [M] Main menu   [Q] Quit");
        attroff(A_BOLD);
        refresh();

        int ch = getch();
        switch (ch) {
        case 'i': case 'I': case '\n': return 'i';
        case 'e': case 'E': return 'e';
        case 'b': case 'B': case KEY_LEFT: case 27: return 'b';
        case 'm': case 'M': return 'm';
        case 'q': case 'Q': case ERR: return 'q';
        }
    }
}

void ui_message(const char *title, const char *l1, const char *l2)
{
    draw_frame(title, NULL);
    if (l1) mvprintw(6, 4, "%.*s", COLS - 6, l1);
    if (l2) mvprintw(7, 4, "%.*s", COLS - 6, l2);
    attron(A_DIM);
    mvprintw(9, 4, "Press any key...");
    attroff(A_DIM);
    refresh();
    getch();
}
