/*	SCCS Id: @(#)topl.c	3.4	1996/10/24	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

#ifdef TTY_GRAPHICS

#include "tcap.h"
#include "wintty.h"
#include <ctype.h>

static void putnsyms(const nhstr str);
static void redotoplin(const char *);
static void redotopnlin(const nhstr);
static void topl_putsym(glyph_t);
static void remember_topl(void);
static void removetopl(int);

int tty_doprev_message(void) {
	/*WAC merged in NH340 prevwindow - add reverse ordering?*/

	struct WinDesc *cw = wins[WIN_MESSAGE];

	winid prevmsg_win;
	int i;
	if ((iflags.prevmsg_window != 's') && !ttyDisplay->inread) { /* not single */
		if (iflags.prevmsg_window == 'f') {		     /* full */
			prevmsg_win = create_nhwindow(NHW_MENU);
			putstr(prevmsg_win, 0, "Message History");
			putstr(prevmsg_win, 0, "");
			cw->maxcol = cw->maxrow;
			i = cw->maxcol;
			do {
				if (cw->data[i] && strcmp(cw->data[i], ""))
					putstr(prevmsg_win, 0, cw->data[i]);
				i = (i + 1) % cw->rows;
			} while (i != cw->maxcol);
			putnstr(prevmsg_win, 0, toplines);
			display_nhwindow(prevmsg_win, true);
			destroy_nhwindow(prevmsg_win);
		} else if (iflags.prevmsg_window == 'c') { /* combination */
			do {
				morc = 0;
				if (cw->maxcol == cw->maxrow) {
					ttyDisplay->dismiss_more = C('p'); /* <ctrl/P> allowed at --More-- */
					redotopnlin(toplines);
					cw->maxcol--;
					if (cw->maxcol < 0) cw->maxcol = cw->rows - 1;
					if (!cw->data[cw->maxcol])
						cw->maxcol = cw->maxrow;
				} else if (cw->maxcol == (cw->maxrow - 1)) {
					ttyDisplay->dismiss_more = C('p'); /* <ctrl/P> allowed at --More-- */
					redotoplin(cw->data[cw->maxcol]);
					cw->maxcol--;
					if (cw->maxcol < 0) cw->maxcol = cw->rows - 1;
					if (!cw->data[cw->maxcol])
						cw->maxcol = cw->maxrow;
				} else {
					prevmsg_win = create_nhwindow(NHW_MENU);
					putstr(prevmsg_win, 0, "Message History");
					putstr(prevmsg_win, 0, "");
					cw->maxcol = cw->maxrow;
					i = cw->maxcol;
					do {
						if (cw->data[i] && strcmp(cw->data[i], ""))
							putstr(prevmsg_win, 0, cw->data[i]);
						i = (i + 1) % cw->rows;
					} while (i != cw->maxcol);
					putnstr(prevmsg_win, 0, toplines);
					display_nhwindow(prevmsg_win, true);
					destroy_nhwindow(prevmsg_win);
				}

			} while (morc == C('p'));
			ttyDisplay->dismiss_more = 0;
		} else { /* reversed */
			morc = 0;
			prevmsg_win = create_nhwindow(NHW_MENU);
			putstr(prevmsg_win, 0, "Message History");
			putstr(prevmsg_win, 0, "");
			putnstr(prevmsg_win, 0, toplines);
			cw->maxcol = cw->maxrow - 1;
			if (cw->maxcol < 0) cw->maxcol = cw->rows - 1;
			do {
				putstr(prevmsg_win, 0, cw->data[cw->maxcol]);
				cw->maxcol--;
				if (cw->maxcol < 0) cw->maxcol = cw->rows - 1;
				if (!cw->data[cw->maxcol])
					cw->maxcol = cw->maxrow;
			} while (cw->maxcol != cw->maxrow);

			display_nhwindow(prevmsg_win, true);
			destroy_nhwindow(prevmsg_win);
			cw->maxcol = cw->maxrow;
			ttyDisplay->dismiss_more = 0;
		}
	} else if (iflags.prevmsg_window == 's') { /* single */
		ttyDisplay->dismiss_more = C('p'); /* <ctrl/P> allowed at --More-- */
		do {
			morc = 0;
			if (cw->maxcol == cw->maxrow)
				redotopnlin(toplines);
			else if (cw->data[cw->maxcol])
				redotoplin(cw->data[cw->maxcol]);
			cw->maxcol--;
			if (cw->maxcol < 0) cw->maxcol = cw->rows - 1;
			if (!cw->data[cw->maxcol])
				cw->maxcol = cw->maxrow;
		} while (morc == C('p'));
		ttyDisplay->dismiss_more = 0;
	}

#if 0
    ttyDisplay->dismiss_more = C('p');	/* <ctrl/P> allowed at --More-- */

    do {
	morc = 0;
        if (cw->maxcol == cw->maxrow) {
            redotopnlin(toplines);
            cw->maxcol--;
            if (cw->maxcol < 0) cw->maxcol = cw->rows-1;
            if (!cw->data[cw->maxcol])
                cw->maxcol = cw->maxrow;
        } else
        if (cw->data[cw->maxcol]) {
/*WAC Show all the history in a window*/
            tmpwin = create_nhwindow(NHW_MENU);
            putstr(tmpwin, ATR_BOLD, "Message History");
            putstr(tmpwin, 0, "");
            putnstr(tmpwin, 0, toplines);

            do {
                if (!cw->data[cw->maxcol]) break;
                putstr(tmpwin, 0, cw->data[cw->maxcol]);
                cw->maxcol--;
                if (cw->maxcol < 0) cw->maxcol = cw->rows-1;
                if (!cw->data[cw->maxcol])
                        cw->maxcol = cw->maxrow;
            } while (cw->maxcol != cw->maxrow);

            display_nhwindow(tmpwin, true);
            destroy_nhwindow(tmpwin);

            cw->maxcol = cw->maxrow;
        }

    } while (morc == C('p'));
    ttyDisplay->dismiss_more = 0;
#endif
	return 0;
}

static void redotopnlin(const nhstr str) {
	int otoplin = ttyDisplay->toplin;
	home();
	end_glyphout(); /* in case message printed during graphics output */
	putnsyms(str);
	cl_end();
	ttyDisplay->toplin = 1;
	if (ttyDisplay->cury && otoplin != 3)
		more();
}
static void redotoplin(const char *str) { redotopnlin(nhsdupz(str)); }

static void remember_topl(void) {
	struct WinDesc *cw = wins[WIN_MESSAGE];
	int idx = cw->maxrow;

	cw->data[idx] = nhs2cstr_trunc(toplines);
	cw->datlen[idx] = toplines.len + 1;

	cw->maxcol = cw->maxrow = (idx + 1) % cw->rows;
}

void addtopl(const nhstr s) {
	struct WinDesc *cw = wins[WIN_MESSAGE];

	tty_curs(BASE_WINDOW, cw->curx + 1, cw->cury);
	putnsyms(s);
	cl_end();
	ttyDisplay->toplin = 1;
}

void more(void) {
	struct WinDesc *cw = wins[WIN_MESSAGE];

	/* avoid recursion -- only happens from interrupts */
	if (ttyDisplay->inmore++) {
		return;
	}

	if (iflags.debug_fuzzer) {
		return;
	}

	if (ttyDisplay->toplin) {
		tty_curs(BASE_WINDOW, cw->curx + 1, cw->cury);
		if (cw->curx >= CO - 8) topl_putsym('\n');
	}

	if (flags.standout) {
		standoutbeg();
	}

	putsyms(defmorestr);

	if (flags.standout) {
		standoutend();
	}

	xwaitforspace("\033 ");

	if (morc == '\033')
		cw->flags |= WIN_STOP;

	if (ttyDisplay->toplin && cw->cury) {
		docorner(1, cw->cury + 1);
		cw->curx = cw->cury = 0;
		home();
	} else if (morc == '\033') {
		cw->curx = cw->cury = 0;
		home();
		cl_end();
	}
	ttyDisplay->toplin = 0;
	ttyDisplay->inmore = 0;
}

void update_topl(const nhstr s) {
	int notdied = 1;
	struct WinDesc *cw = wins[WIN_MESSAGE];

	/* If there is room on the line, print message on same line */
	/* But messages like "You die..." deserve their own line */
	if ((ttyDisplay->toplin == 1 || (cw->flags & WIN_STOP)) &&
	    cw->cury == 0 &&
	    s.len + toplines.len + 3 < CO - 8 && /* room for --More-- */
	    (notdied = !nhseq(nhstrim(s, 7), nhsdupz("You die")))) {
		toplines = nhscatf(toplines, "  %s", s);
		cw->curx += 2;
		if (!(cw->flags & WIN_STOP))
			addtopl(s);
		return;
	} else if (!(cw->flags & WIN_STOP)) {
		if (ttyDisplay->toplin == 1) {
			more();
		} else if (cw->cury) {		   /* for when flags.toplin == 2 && cury > 1 */
			docorner(1, cw->cury + 1); /* reset cury = 0 if redraw screen */
			cw->curx = cw->cury = 0;   /* from home--cls() & docorner(1,n) */
		}
	}
	remember_topl();
	toplines = nhsdup(s);

	nhstr ntopl = new_nhs();
	nhstr tl = toplines;
	while (tl.len >= CO) {
		usize i = CO;
		while (i && !isspace(tl.str[--i])) {}
		if (!i) {
			/* Eek!  A huge token.  Try splitting after it. */
			isize j = nhsindex(tl, ' ');

			if (j < 0) break; /* No choice but to spit it out whole. */

			i = j;
		}
		ntopl = nhscatf(ntopl, "%s\n", nhstrim(tl, i));
		tl = nhslice(tl, i+1);
	}
	toplines = nhscat(ntopl, tl);

	if (!notdied) cw->flags &= ~WIN_STOP;
	if (!(cw->flags & WIN_STOP)) redotopnlin(toplines);
}

static void topl_putsym(glyph_t c) {
	struct WinDesc *cw = wins[WIN_MESSAGE];

	if (cw == NULL) panic("Putsym window MESSAGE nonexistant");

	switch (c) {
		case '\b':
			if (ttyDisplay->curx == 0 && ttyDisplay->cury > 0)
				tty_curs(BASE_WINDOW, CO, (int)ttyDisplay->cury - 1);
			backsp();
			ttyDisplay->curx--;
			cw->curx = ttyDisplay->curx;
			return;
		case '\n':
			cl_end();
			ttyDisplay->curx = 0;
			ttyDisplay->cury++;
			cw->cury = ttyDisplay->cury;
			break;
		default:
			if (ttyDisplay->curx == CO - 1)
				topl_putsym('\n'); /* 1 <= curx <= CO; avoid CO */
			ttyDisplay->curx++;
	}
	cw->curx = ttyDisplay->curx;
	if (cw->curx == 0) cl_end();
	pututf8char(c);
}

void putsyms(const char *str) {
	while (*str)
		topl_putsym(*str++);
}

//todo style
static void putnsyms(const nhstr s) {
	nhstyle old = nhstyle_default();
	for (usize i = 0; i < s.len; i++) {
		if (!nhstyle_eq(s.style[i], old)) {
			term_end_color();
			tty_style_start(s.style[i]);
			old = s.style[i];
		}
		topl_putsym(s.str[i]);
	}
	if (!nhstyle_eq(old, nhstyle_default())) term_end_color();
}

static void removetopl(int n) {
	/* assume addtopl() has been done, so ttyDisplay->toplin is already set */
	while (n-- > 0)
		putsyms("\b \b");
}

extern char erase_char; /* from xxxtty.c; don't need kill_char */

/*
 *   Generic yes/no function. 'def' is the default (returned by space or
 *   return; 'esc' returns 'q', or 'n', or the default, depending on
 *   what's in the string. The 'query' string is printed before the user
 *   is asked about the string.
 *   If resp is NULL, any single character is accepted and returned.
 *   If not-NULL, only characters in it are allowed (exceptions:  the
 *   quitchars are always allowed, and if it contains '#' then digits
 *   are allowed); if it includes an <esc>, anything beyond that won't
 *   be shown in the prompt to the user but will be acceptable as input.
 */
char tty_yn_function(const char *query, const char *resp, char def) {
	char q;
	bool digit_ok, allow_num;
	struct WinDesc *cw = wins[WIN_MESSAGE];
	bool doprev = false;
	nhstr prompt;

	if (ttyDisplay->toplin == 1 && !(cw->flags & WIN_STOP)) more();
	cw->flags &= ~WIN_STOP;
	ttyDisplay->toplin = 3; /* special prompt state */
	ttyDisplay->inread++;
	if (resp) {
		char *rb, respbuf[QBUFSZ];

		allow_num = index(resp, '#') != 0;
		strcpy(respbuf, resp);
		/* any acceptable responses that follow <esc> aren't displayed */
		if ((rb = index(respbuf, '\033')) != 0) *rb = '\0';
		prompt = nhsfmt("%S [%S] ", query, respbuf);
		if (def) prompt = nhscatf(prompt, "(%c) ", def);
		spline("%s", prompt);
	} else {
		pline("%s ", query);
		q = readchar();
		goto clean_up;
	}

	do { /* loop until we get valid input */
		q = lowc(readchar());
		if (q == '\020') { /* ctrl-P */
			if (iflags.prevmsg_window != 's') {
				int sav = ttyDisplay->inread;
				ttyDisplay->inread = 0;
				tty_doprev_message();
				ttyDisplay->inread = sav;
				tty_clear_nhwindow(WIN_MESSAGE);
				cw->maxcol = cw->maxrow;
				addtopl(prompt);
			} else {
				if (!doprev)
					tty_doprev_message(); /* need two initially */
				tty_doprev_message();
				doprev = true;
			}
			q = '\0'; /* force another loop iteration */
			continue;
		} else if (doprev) {
			/* BUG[?]: this probably ought to check whether the
			   character which has just been read is an acceptable
			   response; if so, skip the reprompt and use it. */
			tty_clear_nhwindow(WIN_MESSAGE);
			cw->maxcol = cw->maxrow;
			doprev = false;
			addtopl(prompt);
			q = '\0'; /* force another loop iteration */
			continue;
		}
		digit_ok = allow_num && digit(q);
		if (q == '\033') {
			if (index(resp, 'q'))
				q = 'q';
			else if (index(resp, 'n'))
				q = 'n';
			else
				q = def;
			break;
		} else if (index(quitchars, q)) {
			q = def;
			break;
		}
		if (!index(resp, q) && !digit_ok) {
			tty_nhbell();
			q = (char)0;
		} else if (q == '#' || digit_ok) {
			char z, digit_string[2];
			int n_len = 0;
			long value = 0;
			addtopl(nhsdupz("#")), n_len++;
			digit_string[1] = '\0';
			if (q != '#') {
				digit_string[0] = q;
				addtopl(nhsdupz(digit_string)), n_len++;
				value = q - '0';
				q = '#';
			}
			do { /* loop until we get a non-digit */
				z = lowc(readchar());
				if (digit(z)) {
					value = (10 * value) + (z - '0');
					if (value < 0) break; /* overflow: try again */
					digit_string[0] = z;
					addtopl(nhsdupz(digit_string)), n_len++;
				} else if (z == 'y' || index(quitchars, z)) {
					if (z == '\033') value = -1; /* abort */
					z = '\n';		     /* break */
				} else if (z == erase_char || z == '\b') {
					if (n_len <= 1) {
						value = -1;
						break;
					} else {
						value /= 10;
						removetopl(1), n_len--;
					}
				} else {
					value = -1; /* abort */
					tty_nhbell();
					break;
				}
			} while (z != '\n');
			if (value > 0)
				yn_number = value;
			else if (value == 0)
				q = 'n'; /* 0 => "no" */
			else {		 /* remove number from top line, then try again */
				removetopl(n_len), n_len = 0;
				q = '\0';
			}
		}
	} while (!q);

	if (q != '#') {
		addtopl(nhsfmt("%c", q));
	}
clean_up:
	ttyDisplay->inread--;
	ttyDisplay->toplin = 2;
	if (ttyDisplay->intr) ttyDisplay->intr--;
	if (wins[WIN_MESSAGE]->cury)
		tty_clear_nhwindow(WIN_MESSAGE);

	return q;
}

#endif /* TTY_GRAPHICS */

/*topl.c*/
