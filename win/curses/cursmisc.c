#include "curses.h"
#include "hack.h"
#include "wincurs.h"
#include "cursmisc.h"
#include "func_tab.h"
#include "dlb.h"

#include <ctype.h>

/* Misc. curses interface functions */

/* Private declarations */

static int curs_x = -1;
static int curs_y = -1;

static int parse_escape_sequence(void);

/* Macros for Control and Alt keys */

/* Read a character of input from the user */

int curses_read_char() {
	int ch;

	ch = getch();
	ch = curses_convert_keys(ch);

	if (ch == 0) {
		ch = DOESCAPE; /* map NUL to ESC since nethack doesn't expect NUL */
	}
#if defined(ALT_0) && defined(ALT_9) /* PDCurses, maybe others */
	if ((ch >= ALT_0) && (ch <= ALT_9)) {
		int tmpch = (ch - ALT_0) + '0';
		ch = M(tmpch);
	}
#endif

#if defined(ALT_A) && defined(ALT_Z) /* PDCurses, maybe others */
	if ((ch >= ALT_A) && (ch <= ALT_Z)) {
		int tmpch = (ch - ALT_A) + 'a';
		ch = M(tmpch);
	}
#endif

#ifdef KEY_RESIZE
	/* Handle resize events via get_nh_event, not this code */
	if (ch == KEY_RESIZE) {
		ch = DOESCAPE; /* NetHack doesn't know what to do with KEY_RESIZE */
	}
#endif

	if (counting && !isdigit(ch)) { /* Dismiss count window if necissary */
		curses_count_window(NULL);
		curses_refresh_nethack_windows();
	}

	return ch;
}

/* Turn on or off the specified color and / or attribute */

void curses_toggle_color_attr(WINDOW *win, int color, int attr, int onoff) {
	int curses_color;

	/* color disabled */
	if (!iflags.wc_color) {
		return;
	}

	if (color == 0) { /* make black fg visible */
		if (can_change_color() && (COLORS > 16)) {
			/* colorpair for black is already darkgray */
		} else { /* Use bold for a bright black */

			wattron(win, A_BOLD);
		}
	}
	curses_color = color + 1;
	if (COLORS < 16) {
		if (curses_color > 8 && curses_color < 17)
			curses_color -= 8;
		else if (curses_color > (17 + 16))
			curses_color -= 16;
	}
	if (onoff == ON) { /* Turn on color/attributes */
		if (color != NONE) {
			if ((((color > BRIGHT) && (color < 17)) ||
			     (color > 17 + 17)) &&
			    (COLORS < 16)) {
				wattron(win, A_BOLD);
			}
			wattron(win, COLOR_PAIR(curses_color));
		}

		if (attr != NONE) {
			wattron(win, attr);
		}
	} else { /* Turn off color/attributes */

		if (color != NONE) {
			if ((color > BRIGHT) && (COLORS < 16)) {
				wattroff(win, A_BOLD);
			}
			if ((color == 0) && (!can_change_color() || (COLORS < 16))) {
				wattroff(win, A_BOLD);
			}
			wattroff(win, COLOR_PAIR(curses_color));
		}

		if (attr != NONE) {
			wattroff(win, attr);
		}
	}
}

/* clean up and quit - taken from tty port */

void curses_bail(const char *mesg) {
	clearlocks();
	curses_exit_nhwindows(mesg);
	terminate(EXIT_SUCCESS);
}

/* Return a winid for a new window of the given type */

winid curses_get_wid(int type) {
	winid ret;
	static winid menu_wid = 20; /* Always even */
	static winid text_wid = 21; /* Always odd */

	switch (type) {
		case NHW_MESSAGE:
			return MESSAGE_WIN;
		case NHW_MAP:
			return MAP_WIN;
		case NHW_STATUS:
			return STATUS_WIN;
		case NHW_MENU:
			ret = menu_wid;
			break;
		case NHW_TEXT:
			ret = text_wid;
			break;
		default:
			impossible("curses_get_wid: unsupported window type");
			ret = -1;
	}

	while (curses_window_exists(ret)) {
		ret += 2;
		if ((ret + 2) > 10000) { /* Avoid "wid2k" problem */
			ret -= 9900;
		}
	}

	if (type == NHW_MENU) {
		menu_wid += 2;
	} else {
		text_wid += 2;
	}

	return ret;
}

/*
 * Allocate a copy of the given string.  If null, return a string of
 * zero length.
 *
 * This is taken from copy_of() in tty/wintty.c.
 */

char *
curses_copy_of(const char *s) {
	if (!s)
		s = "";
	return strcpy(alloc(strlen(s) + 1), s);
}

/* Determine the number of lines needed for a string for a dialog window
of the given width */

int curses_num_lines(const char *str, int width) {
	int last_space, count;
	int curline = 1;
	char substr[BUFSZ];
	char tmpstr[BUFSZ];

	strncpy(substr, str, BUFSZ - 1);
	substr[BUFSZ - 1] = '\0';

	while (strlen(substr) > width) {
		last_space = 0;

		for (count = 0; count <= width; count++) {
			if (substr[count] == ' ')
				last_space = count;
		}
		if (last_space == 0) { /* No spaces found */
			last_space = count - 1;
		}
		for (count = (last_space + 1); count < strlen(substr); count++) {
			tmpstr[count - (last_space + 1)] = substr[count];
		}
		tmpstr[count - (last_space + 1)] = '\0';
		strcpy(substr, tmpstr);
		curline++;
	}

	return curline;
}

/* Break string into smaller lines to fit into a dialog window of the
given width */

char *
curses_break_str(const char *str, int width, int line_num) {
	int last_space, count;
	char *retstr;
	int curline = 0;
	int strsize = strlen(str) + 1;
	char substr[strsize];
	char curstr[strsize];
	char tmpstr[strsize];

	strcpy(substr, str);

	while (curline < line_num) {
		if (strlen(substr) == 0) {
			break;
		}
		curline++;
		last_space = 0;
		for (count = 0; count <= width; count++) {
			if (substr[count] == ' ') {
				last_space = count;
			} else if (substr[count] == '\0') {
				last_space = count;
				break;
			}
		}
		if (last_space == 0) { /* No spaces found */
			last_space = count - 1;
		}
		for (count = 0; count < last_space; count++) {
			curstr[count] = substr[count];
		}
		curstr[count] = '\0';
		if (substr[count] == '\0') {
			break;
		}
		for (count = (last_space + 1); count < strlen(substr); count++) {
			tmpstr[count - (last_space + 1)] = substr[count];
		}
		tmpstr[count - (last_space + 1)] = '\0';
		strcpy(substr, tmpstr);
	}

	if (curline < line_num) {
		return NULL;
	}

	retstr = curses_copy_of(curstr);

	return retstr;
}

/* Return the remaining portion of a string after hacking-off line_num lines */

char *
curses_str_remainder(const char *str, int width, int line_num) {
	int last_space, count;
	char *retstr;
	int curline = 0;
	int strsize = strlen(str) + 1;
	char substr[strsize];
	char tmpstr[strsize];

	strcpy(substr, str);

	while (curline < line_num) {
		if (strlen(substr) == 0) {
			break;
		}
		curline++;
		last_space = 0;
		for (count = 0; count <= width; count++) {
			if (substr[count] == ' ') {
				last_space = count;
			} else if (substr[count] == '\0') {
				last_space = count;
				break;
			}
		}
		if (last_space == 0) { /* No spaces found */
			last_space = count - 1;
		}
		if (substr[count] == '\0') {
			break;
		}
		for (count = (last_space + 1); count < strlen(substr); count++) {
			tmpstr[count - (last_space + 1)] = substr[count];
		}
		tmpstr[count - (last_space + 1)] = '\0';
		strcpy(substr, tmpstr);
	}

	if (curline < line_num) {
		return NULL;
	}

	retstr = curses_copy_of(substr);

	return retstr;
}

/* Determine if the given NetHack winid is a menu window */

bool curses_is_menu(winid wid) {
	if ((wid > 19) && !(wid % 2)) { /* Even number */
		return true;
	} else {
		return false;
	}
}

/* Determine if the given NetHack winid is a text window */

bool curses_is_text(winid wid) {
	if ((wid > 19) && (wid % 2)) { /* Odd number */
		return true;
	} else {
		return false;
	}
}

/* Move text cursor to specified coordinates in the given NetHack window */
void curses_move_cursor(winid wid, int x, int y) {
	int sx, sy, ex, ey;
	int xadj = 0;
	int yadj = 0;

#ifndef PDCURSES
	WINDOW *win = curses_get_nhwin(MAP_WIN);
#endif

	if (wid != MAP_WIN) {
		return;
	}
#ifdef PDCURSES
	/* PDCurses seems to not handle wmove correctly, so we use move and
	   physical screen coordinates instead */
	curses_get_window_xy(wid, &xadj, &yadj);
#endif
	curs_x = x + xadj;
	curs_y = y + yadj;
	curses_map_borders(&sx, &sy, &ex, &ey, x, y);

	if (curses_window_has_border(wid)) {
		curs_x++;
		curs_y++;
	}

	if ((x >= sx) && (x <= ex) && (y >= sy) && (y <= ey)) {
		curs_x -= sx;
		curs_y -= sy;
#ifdef PDCURSES
		move(curs_y, curs_x);
#else
		wmove(win, curs_y, curs_x);
#endif
	}
}

/* Perform actions that should be done every turn before nhgetch() */

void curses_prehousekeeping() {
#ifndef PDCURSES
	WINDOW *win = curses_get_nhwin(MAP_WIN);
#endif /* PDCURSES */

	if ((curs_x > -1) && (curs_y > -1)) {
		curs_set(1);
#ifdef PDCURSES
		/* PDCurses seems to not handle wmove correctly, so we use move
		   and physical screen coordinates instead */
		move(curs_y, curs_x);
#else
		wmove(win, curs_y, curs_x);
#endif /* PDCURSES */
		curses_refresh_nhwin(MAP_WIN);
	}
}

/* Perform actions that should be done every turn after nhgetch() */

void curses_posthousekeeping() {
	curs_set(0);
	curses_decrement_highlights(false);
	curses_clear_unhighlight_message_window();
}

void curses_view_file(const char *filename, bool must_exist) {
	winid wid;
	anything *identifier;
	char buf[BUFSZ];
	menu_item *selected = NULL;
	dlb *fp = dlb_fopen(filename, "r");

	if ((fp == NULL) && (must_exist)) {
		pline("Cannot open %s for reading!", filename);
	}

	if (fp == NULL) {
		return;
	}

	wid = curses_get_wid(NHW_MENU);
	curses_create_nhmenu(wid);
	identifier = alloc(sizeof(anything));
	identifier->a_void = NULL;

	while (dlb_fgets(buf, BUFSZ, fp) != NULL) {
		curses_add_menu(wid, NO_GLYPH, identifier, 0, 0, A_NORMAL, buf, false);
	}

	dlb_fclose(fp);
	curses_end_menu(wid, "");
	curses_select_menu(wid, PICK_NONE, &selected);
}

void curses_rtrim(char *str) {
	char *s;

	s = eos(str);
	while(s > str && isspace(*--s));
	if (s == str)
		*s = '\0';
	else
		*(++s) = '\0';
}

/* Read numbers until non-digit is encountered, and return number
in int form. */

int curses_get_count(int first_digit) {
	long current_count = first_digit;
	int current_char;

	current_char = curses_read_char();

	while (isdigit(current_char)) {
		current_count = (current_count * 10) + (current_char - '0');
		if (current_count > LARGEST_INT) {
			current_count = LARGEST_INT;
		}

		pline("Count: %ld", current_count);
		current_char = curses_read_char();
	}

	ungetch(current_char);

	if (current_char == DOESCAPE) { /* Cancelled with escape */
		current_count = -1;
	}

	return current_count;
}

/* Convert the given NetHack text attributes into the format curses
understands, and return that format mask. */

attr_t curses_convert_attr(int attr) {
	switch (attr) {
		case ATR_NONE:
			return A_NORMAL;
		case ATR_ULINE:
			return A_UNDERLINE;
		case ATR_BOLD:
			return A_BOLD;
		case ATR_BLINK:
			return A_BLINK;
		case ATR_INVERSE:
			return A_REVERSE;
		default:
			return A_NORMAL;
	}
}

attr_t curses_convert_attrs(int attr) {
	attr_t ret = A_NORMAL;
	if (attr & ATR_BOLD) ret |= A_BOLD;
	if (attr & ATR_ULINE) ret |= A_UNDERLINE;
	if (attr & ATR_BLINK) ret |= A_BLINK;
	if (attr & ATR_INVERSE) ret |= A_REVERSE;
	return ret;
}

/* Map letter attributes from a string to bitmask.  Return mask on
success, or 0 if not found */

int curses_read_attrs(char *attrs) {
	int retattr = 0;

	if (strchr(attrs, 'b') || strchr(attrs, 'B')) {
		retattr = retattr | A_BOLD;
	}
	if (strchr(attrs, 'i') || strchr(attrs, 'I')) {
		retattr = retattr | A_REVERSE;
	}
	if (strchr(attrs, 'u') || strchr(attrs, 'U')) {
		retattr = retattr | A_UNDERLINE;
	}
	if (strchr(attrs, 'k') || strchr(attrs, 'K')) {
		retattr = retattr | A_BLINK;
	}
#ifdef A_ITALIC
	if (strchr(attrs, 't') || strchr(attrs, 'T')) {
		retattr = retattr | A_ITALIC;
	}
#endif
#ifdef A_RIGHTLINE
	if (strchr(attrs, 'r') || strchr(attrs, 'R')) {
		retattr = retattr | A_RIGHTLINE;
	}
#endif
#ifdef A_LEFTLINE
	if (strchr(attrs, 'l') || strchr(attrs, 'L')) {
		retattr = retattr | A_LEFTLINE;
	}
#endif

	return retattr;
}

/* Convert special keys into values that NetHack can understand.
Currently this is limited to arrow keys, but this may be expanded. */

int curses_convert_keys(int key) {
	int ret = key;

	if (ret == '\033') {
		ret = parse_escape_sequence();
	}

	/* Handle arrow keys */
	switch (key) {
		case KEY_LEFT:
			if (iflags.num_pad) {
				ret = '4';
			} else {
				ret = 'h';
			}
			break;
		case KEY_RIGHT:
			if (iflags.num_pad) {
				ret = '6';
			} else {
				ret = 'l';
			}
			break;
		case KEY_UP:
			if (iflags.num_pad) {
				ret = '8';
			} else {
				ret = 'k';
			}
			break;
		case KEY_DOWN:
			if (iflags.num_pad) {
				ret = '2';
			} else {
				ret = 'j';
			}
			break;
#ifdef KEY_A1
		case KEY_A1:
			if (iflags.num_pad) {
				ret = '7';
			} else {
				ret = 'y';
			}
			break;
#endif /* KEY_A1 */
#ifdef KEY_A3
		case KEY_A3:
			if (iflags.num_pad) {
				ret = '9';
			} else {
				ret = 'u';
			}
			break;
#endif /* KEY_A3 */
#ifdef KEY_C1
		case KEY_C1:
			if (iflags.num_pad) {
				ret = '1';
			} else {
				ret = 'b';
			}
			break;
#endif /* KEY_C1 */
#ifdef KEY_C3
		case KEY_C3:
			if (iflags.num_pad) {
				ret = '3';
			} else {
				ret = 'n';
			}
			break;
#endif /* KEY_C3 */
#ifdef KEY_B2
		case KEY_B2:
			if (iflags.num_pad) {
				ret = '5';
			} else {
				ret = 'g';
			}
			break;
#endif /* KEY_B2 */
	}

	return ret;
}

/* Process mouse events.  Mouse movement is processed until no further
mouse movement events are available.  Returns 0 for a mouse click
event, or the first non-mouse key event in the case of mouse
movement. */

int curses_get_mouse(int *mousex, int *mousey, int *mod) {
	int key = '\033';

#ifdef NCURSES_MOUSE_VERSION
	MEVENT event;

	if (getmouse(&event) == OK) { /* When the user clicks left mouse button */
		if (event.bstate & BUTTON1_CLICKED) {
			/* See if coords are in map window & convert coords */
			if (wmouse_trafo(mapwin, &event.y, &event.x, true)) {
				key = 0; /* Flag mouse click */
				*mousex = event.x;
				*mousey = event.y;

				if (curses_window_has_border(MAP_WIN)) {
					(*mousex)--;
					(*mousey)--;
				}

				*mod = CLICK_1;
			}
		}
	}
#endif /* NCURSES_MOUSE_VERSION */

	return key;
}

static int
parse_escape_sequence(void) {
#ifndef PDCURSES
	int ret;

	timeout(10);

	ret = getch();

	if (ret != ERR) { /* Likely an escape sequence */
		if (((ret >= 'a') && (ret <= 'z')) || ((ret >= '0') && (ret <= '9'))) {
			ret |= 0x80;	 /* Meta key support for most terminals */
		} else if (ret == 'O') { /* Numeric keypad */
			ret = getch();
			if ((ret != ERR) && (ret >= 112) && (ret <= 121)) {
				ret = ret - 112 + '0'; /* Convert to number */
			} else {
				ret = '\033'; /* Escape */
			}
		}
	} else {
		ret = '\033'; /* Just an escape character */
	}

	timeout(-1);

	return ret;
#else
	return '\033';
#endif /* !PDCURSES */
}
