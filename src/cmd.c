/*	SCCS Id: @(#)cmd.c	3.4	2003/02/06	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include <ctype.h>

#include "hack.h"
#include "defer.h"
#include "func_tab.h"
/* #define DEBUG */ /* uncomment for debugging */

/*
 * Some systems may have getchar() return EOF for various reasons, and
 * we should not quit before seeing at least NR_OF_EOFS consecutive EOFs.
 */
#if defined(SYSV) || defined(HPUX)
#define NR_OF_EOFS 20
#endif

#define CMD_TRAVEL	(char)0x90
#define CMD_CLICKLOOK	(char)0x8F

#ifdef DEBUG
/*
 * only one "wiz_debug_cmd" routine should be available (in whatever
 * module you are trying to debug) or things are going to get rather
 * hard to link :-)
 */
extern int wiz_debug_cmd(void);
#endif

static int (*timed_occ_fn)(void);

static int doprev_message(void);
static int timed_occupation(void);
static int doextcmd(void);
static int domonability(void);
static int dotravel(void);
static int playersteal(void);
static int wiz_wish(void);
static int wiz_identify(void);
static int wiz_map(void);
/* BEGIN TSANTH'S CODE */
static int wiz_gain_ac(void);
static int wiz_gain_level(void);
static int wiz_toggle_invulnerability(void);
/* END TSANTH'S CODE */
static int wiz_genesis(void);
static int wiz_where(void);
static int wiz_detect(void);
static int wiz_panic(void);
static int wiz_polyself(void);
static int wiz_level_tele(void);
static int wiz_level_change(void);
static int wiz_show_seenv(void);
static int wiz_show_vision(void);
static int wiz_smell(void);
static int wiz_mon_polycontrol(void);
static int wiz_show_wmodes(void);
static int wiz_showkills(void);
extern void list_vanquished(int, bool);
#ifdef DEBUG_MIGRATING_MONS
static int wiz_migrate_mons(void);
#endif
static usize size_monst(struct monst *mtmp);
static void count_obj(struct obj *, long *, long *, boolean, boolean);
static void obj_chain(winid, const char *, struct obj *, long *, long *);
static void mon_invent_chain(winid, const char *, struct monst *, long *, long *);
static void mon_chain(winid, const char *, struct monst *, long *, long *);
static void contained(winid, const char *, long *, long *);
static int wiz_show_stats(void);
static int wiz_show_display(void);
#ifdef PORT_DEBUG
static int wiz_port_debug(void);
#endif
static int enter_explore_mode(void);
static int doattributes(void);
static int doconduct(void); /**/
static boolean minimal_enlightenment(void);
static int makemenu(const char *, struct menu_list *);

static void bind_key(unsigned char, char *);
static void init_bind_list(void);
static void change_bind_list(void);
static void verify_key_list(void);
static void add_debug_extended_commands(void);
static void addchar(char);

static struct rm *maploc;

static void enlght_line(const char *, const char *, const char *);
static char *enlght_combatinc(const char *, int, int, char *);
#ifdef UNIX
static void end_of_input(void);
#endif

static const char *readchar_queue = "";
static coord clicklook_cc;

static char *parse(void);
static bool help_dir(char, const char *);

static int domenusystem(void); /* WAC the menus*/

static int doprev_message(void) {
	return nh_doprev_message();
}

/* Count down by decrementing multi */
static int timed_occupation(void) {
	(*timed_occ_fn)();
	if (multi > 0)
		multi--;
	return multi > 0;
}

/* If you have moved since initially setting some occupations, they
 * now shouldn't be able to restart.
 *
 * The basic rule is that if you are carrying it, you can continue
 * since it is with you.  If you are acting on something at a distance,
 * your orientation to it must have changed when you moved.
 *
 * The exception to this is taking off items, since they can be taken
 * off in a number of ways in the intervening time, screwing up ordering.
 *
 *	Currently:	Take off all armor.
 *			Picking Locks / Forcing Chests.
 *			Setting traps.
 */
void reset_occupations(void) {
	reset_remarm();
	reset_pick();
	reset_trapset();
}

/* If a time is given, use it to timeout this function, otherwise the
 * function times out by its own means.
 */
void set_occupation(int (*fn)(void), const char *txt, int xtime) {
	if (xtime) {
		occupation = timed_occupation;
		timed_occ_fn = fn;
	} else
		occupation = fn;
	occtxt = txt;
	occtime = 0;
	return;
}

static char popch(void);

/* Provide a means to redo the last command.  The flag `in_doagain' is set
 * to true while redoing the command.  This flag is tested in commands that
 * require additional input (like `throw' which requires a thing and a
 * direction), and the input prompt is not shown.  Also, while in_doagain is
 * true, no keystrokes can be saved into the saveq.
 */
#define BSIZE 20
static char pushq[BSIZE], saveq[BSIZE];
static int phead, ptail, shead, stail;

static char popch(void) {
	/* If occupied, return '\0', letting tgetch know a character should
	 * be read from the keyboard.  If the character read is not the
	 * ABORT character (as checked in pcmain.c), that character will be
	 * pushed back on the pushq.
	 */
	if (occupation) return '\0';
	if (in_doagain)
		return (shead != stail) ? saveq[stail++] : '\0';
	else
		return (phead != ptail) ? pushq[ptail++] : '\0';
}

char pgetchar(void) { /* curtesy of aeb@cwi.nl */
	if (iflags.debug_fuzzer) {
		return randomkey();
	}

	int ch;
	if ((ch = popch())) {
		return ch;
	} else {
		return nhgetch();
	}
}

/* A ch == 0 resets the pushq */
void pushch(char ch) {
	if (!ch)
		phead = ptail = 0;
	if (phead < BSIZE)
		pushq[phead++] = ch;
	return;
}

/* A ch == 0 resets the saveq.	Only save keystrokes when not
 * replaying a previous command.
 */
void savech(char ch) {
	if (!in_doagain) {
		if (!ch)
			phead = ptail = shead = stail = 0;
		else if (shead < BSIZE)
			saveq[shead++] = ch;
	}
	return;
}

/* mappings are held in a circular queue */
#define CQ_SIZE 100
static int cq_tail = 0; /* exit point; index for oldest element */
static int cq_head = 0; /* entry point; index for next element */
static char cq_list[CQ_SIZE];

/* This is intended to come "in front of" the old nhgetch so that if there's
 * a mapping/macro, it will insert the necessary keys instead.
 * However, most window-specific code doesn't call nhgetch, but instead calls
 * <win>_nhgetch.  I've tried to change all of the appropriate
 * <win>_nhgetch's to nhgetch's in win/<win>, but there's a good chance of
 * bugs here.
 */
char nhgetch(void) {
	int ch;

	if (cq_head != cq_tail) {
		ch = cq_list[cq_tail];
		cq_tail = (cq_tail + 1) % CQ_SIZE;
	} else {
		ch = nhwingetch(); /* old definition */
	}

	return ch;
}

static void addchar(char ch) {
	cq_list[cq_head] = ch;
	cq_head = (cq_head + 1) % CQ_SIZE;
	if (cq_head == cq_tail) {
		/* character dropped */
		cq_head = (cq_head + CQ_SIZE - 1) % CQ_SIZE;
	}
}

// here after # - now read a full-word command
static int doextcmd(void) {
	int idx, retval = 0;

	/* keep repeating until we don't run help or quit */
	do {
		idx = get_ext_cmd();
		if (idx < 0) return 0; /* quit */
		if (iflags.debug_fuzzer && extcmdlist[idx].disallowed_if_fuzzing) {
			continue;
		}

		retval = (*extcmdlist[idx].ef_funct)();
	} while (extcmdlist[idx].ef_funct == doextlist);

	return retval;
}

// here after #? - now list all full-word commands
int doextlist(void) {
	const struct ext_func_tab *efp;
	char buf[BUFSZ];
	winid datawin;

	datawin = create_nhwindow(NHW_TEXT);
	putstr(datawin, 0, "");
	putstr(datawin, 0, "            Extended Commands List");
	putstr(datawin, 0, "");
	putstr(datawin, 0, "    Press '#', then type:");
	putstr(datawin, 0, "");

	for (efp = extcmdlist; efp->ef_txt; efp++) {
		/* Show name and text for each command.  Autocompleted
		 * commands are marked with an asterisk ('*'). */
		sprintf(buf, "  %c %-15s - %s.",
			efp->autocomplete ? '*' : ' ',
			efp->ef_txt, efp->ef_desc);

		putstr(datawin, 0, buf);
	}
	putstr(datawin, 0, "");
	putstr(datawin, 0, "    Commands marked with a * will be autocompleted.");

	display_nhwindow(datawin, false);
	destroy_nhwindow(datawin);
	return 0;
}

int doremoveimarkers(void) {
	for (usize x = 1; x < COLNO; x++) {
		for (usize y = 0; y < ROWNO; y++) {
			levl[x][y].mem_invis = false;
		}
	}

	return 0;
}

#if defined(TTY_GRAPHICS) || defined(CURSES_GRAPHICS)
#define MAX_EXT_CMD 200 /* Change if we ever have more ext cmds */
/*
 * This is currently used only by the tty port and is
 * controlled via runtime option 'extmenu'
 */

// here after # - now show pick-list of possible commands
int extcmd_via_menu(void) {Deferral
	const struct ext_func_tab *efp;
	menu_item *pick_list = NULL;
	winid win;
	anything any;
	const struct ext_func_tab *choices[MAX_EXT_CMD];
	nhstr buf = new_nhs();
	nhstr cbuf = new_nhs(), prompt = new_nhs(), fmtstr = new_nhs();
	Defer({del_nhs(&buf); del_nhs(&cbuf); del_nhs(&prompt); del_nhs(&fmtstr);});
	int i, n, nchoices, acount;
	int ret, biggest;
	int accelerator, prevaccelerator;
	int matchlevel = 0;

	ret = 0;
	biggest = 0;
	while (!ret) {
		i = n = 0;
		accelerator = 0;
		any.a_void = 0;
		/* populate choices */
		for (efp = extcmdlist; efp->ef_txt; efp++) {
			if (!matchlevel || !strncmp(efp->ef_txt, nhs2cstr_tmp(cbuf), matchlevel)) {
				choices[i++] = efp;
				if ((int)strlen(efp->ef_desc) > biggest) {
					biggest = strlen(efp->ef_desc);
					nhscopyf(&fmtstr, "%%-%is", biggest + 15);
				}
#ifdef DEBUG
				if (i >= MAX_EXT_CMD - 2) {
					impossible("Exceeded %d extended commands in doextcmd() menu",
						   MAX_EXT_CMD - 2);
					Return 0;
				}
#endif
			}
		}
		choices[i] = NULL;
		nchoices = i;
		/* if we're down to one, we have our selection so get out of here */
		if (nchoices == 1) {
			for (i = 0; extcmdlist[i].ef_txt != NULL; i++)
				if (!strncmpi(extcmdlist[i].ef_txt, nhs2cstr_tmp(cbuf), matchlevel)) {
					ret = i;
					break;
				}
			break;
		}

		/* otherwise... */
		win = create_nhwindow(NHW_MENU);
		start_menu(win);
		prevaccelerator = 0;
		acount = 0;
		for (i = 0; choices[i]; ++i) {
			accelerator = choices[i]->ef_txt[matchlevel];
			if (accelerator != prevaccelerator || nchoices < (ROWNO - 3)) {
				if (acount) {
					/* flush the extended commands for that letter already in buf */
					nhscopyf(&buf, nhs2cstr_tmp(fmtstr), prompt);
					any.a_char = prevaccelerator;
					add_menu(win, NO_GLYPH, &any, any.a_char, 0,
						 ATR_NONE, nhs2cstr_tmp(buf), false);
					acount = 0;
				}
			}
			prevaccelerator = accelerator;
			if (!acount || nchoices < (ROWNO - 3)) {
				nhscopyf(&prompt, "%S [%S]", choices[i]->ef_txt,
					choices[i]->ef_desc);
			} else if (acount == 1) {
				nhscopyf(&prompt, "%S or %S", choices[i - 1]->ef_txt,
					choices[i]->ef_txt);
			} else {
				nhscatz(&prompt, " or ");
				nhscatz(&prompt, choices[i]->ef_txt);
			}
			++acount;
		}
		if (acount) {
			/* flush buf */
			nhscopyf(&buf, nhs2cstr_tmp(fmtstr), prompt);
			any.a_char = prevaccelerator;
			add_menu(win, NO_GLYPH, &any, any.a_char, 0, ATR_NONE, nhs2cstr_tmp(buf), false);
		}
		nhscopyf(&prompt, "Extended Command: %s", cbuf);
		end_menu(win, nhs2cstr_tmp(*nhstrim(&prompt, QBUFSZ-1)));
		n = select_menu(win, PICK_ONE, &pick_list);
		destroy_nhwindow(win);
		if (n == 1) {
			if (matchlevel > (QBUFSZ - 2)) {
				free(pick_list);
#ifdef DEBUG
				impossible("Too many characters (%d) entered in extcmd_via_menu()",
					   matchlevel);
#endif
				ret = -1;
			} else {
				matchlevel++;
				nhscatf(&cbuf, "%c", pick_list[0].item.a_char);
				free(pick_list);
			}
		} else {
			if (matchlevel) {
				ret = 0;
				matchlevel = 0;
			} else
				ret = -1;
		}

		// in fuzz mode, only allow to try one command
		if (iflags.debug_fuzzer) break;
	}
	Return ret;
}
#endif

// #monster command - use special monster ability while polymorphed
static int domonability(void) {
	if (can_breathe(youmonst.data))
		return dobreathe();
	else if (attacktype(youmonst.data, AT_SPIT))
		return dospit();
	else if (youmonst.data->mlet == S_NYMPH)
		return doremove();
	else if (attacktype(youmonst.data, AT_GAZE))
		return dogaze();
	else if (is_were(youmonst.data))
		return dosummon();
	else if (webmaker(youmonst.data))
		return dospinweb();
	else if (is_hider(youmonst.data))
		return dohide();
	else if (is_mind_flayer(youmonst.data))
		return domindblast();
	else if (u.umonnum == PM_GREMLIN) {
		if (IS_FOUNTAIN(levl[u.ux][u.uy].typ)) {
			if (split_mon(&youmonst, NULL))
				dryup(u.ux, u.uy, true);
		} else
			pline("There is no fountain here.");
	} else if (is_unicorn(youmonst.data)) {
		use_unicorn_horn(NULL);
		return 1;
	} else if (youmonst.data->msound == MS_SHRIEK) {
		pline("You shriek.");
		if (u.uburied)
			pline("Unfortunately sound does not carry well through rock.");
		else
			aggravate();
	} else if (is_vampire(youmonst.data) || is_vampshifter(&youmonst)) {
		return dopoly();
	} else if (Upolyd) {
		pline("Any special ability you may have is purely reflexive.");
	} else {
		pline("You don't have a special ability in your normal form!");
	}

	return 0;
}

static int enter_explore_mode(void) {
	if (!discover && !wizard) {
		pline("Beware!  From explore mode there will be no return to normal game.");
		if (yn("Do you want to enter explore mode?") == 'y') {
			clear_nhwindow(WIN_MESSAGE);
			pline("You are now in non-scoring explore mode.");
			discover = true;
		} else {
			clear_nhwindow(WIN_MESSAGE);
			pline("Resuming normal game.");
		}
	}
	return 0;
}

static int playersteal(void) {
	int x, y;
	int chanch, base, dexadj, statbonus = 0;
	boolean no_steal = false;

	if (nohands(youmonst.data)) {
		pline("Could be hard without hands ...");
		no_steal = true;
	} else if (near_capacity() > SLT_ENCUMBER) {
		pline("Your load is too heavy to attempt to steal.");
		no_steal = true;
	}
	if (no_steal) {
		/* discard direction typeahead, if any */
		display_nhwindow(WIN_MESSAGE, true); /* --More-- */
		return 0;
	}

	if (!getdir(NULL)) return 0;
	if (!u.dx && !u.dy) return 0;

	x = u.ux + u.dx;
	y = u.uy + u.dy;

	if (u.uswallow) {
		pline("You search around but don't find anything.");
		return 1;
	}

	u_wipe_engr(2);

	maploc = &levl[x][y];

	if (MON_AT(x, y)) {
		struct monst *mdat = m_at(x, y);

		/* calculate chanch of sucess */
		base = 5;
		dexadj = 1;
		if (Role_if(PM_ROGUE)) {
			base = 5 + (u.ulevel * 2);
			dexadj = 3;
		}
		if (ACURR(A_DEX) < 10)
			statbonus = (ACURR(A_DEX) - 10) * dexadj;
		else if (ACURR(A_DEX) > 14)
			statbonus = (ACURR(A_DEX) - 14) * dexadj;

		chanch = base + statbonus;

		if (uarmg && uarmg->otyp != GAUNTLETS_OF_DEXTERITY)
			chanch -= 5;
		if (!uarmg) chanch += 5;
		if (uarms) chanch -= 10;
		if (uarm && uarm->owt < 75)
			chanch += 10;
		else if (uarm && uarm->owt < 125)
			chanch += 5;
		else if (uarm && uarm->owt < 175)
			chanch += 0;
		else if (uarm && uarm->owt < 225)
			chanch -= 5;
		else if (uarm && uarm->owt < 275)
			chanch -= 10;
		else if (uarm && uarm->owt < 325)
			chanch -= 15;
		else if (uarm && uarm->owt < 375)
			chanch -= 20;
		else if (uarm)
			chanch -= 25;
		if (chanch < 5) chanch = 5;
		if (chanch > 95) chanch = 95;
		if (rnd(100) < chanch || mdat->mtame) {
			/* [CWC] This will steal money from the monster from the
			 * first found goldobj - we could be really clever here and
			 * then move onwards to the next goldobj in invent if we
			 * still have coins left to steal, but lets leave that until
			 * we actually have other coin types to test it on.
			 */
			struct obj *gold = findgold(mdat->minvent);
			if (gold) {
				int mongold;
				int coinstolen;
				coinstolen = (u.ulevel * rn1(25, 25));
				mongold = (int)gold->quan;
				if (coinstolen > mongold) coinstolen = mongold;
				if (coinstolen > 0) {
					if (coinstolen != mongold)
						gold = splitobj(gold, coinstolen);
					obj_extract_self(gold);
					if (merge_choice(invent, gold) || inv_cnt() < 52) {
						addinv(gold);
						pline("You steal %s.", doname(gold));
					} else {
						pline("You grab %s, but find no room in your knapsack.", doname(gold));
						dropy(gold);
					}
				} else
					impossible("cmd.c:playersteal() stealing negative money");
			} else
				pline("You don't find anything to steal.");

			if (!mdat->mtame) exercise(A_DEX, true);
			return 1;
		} else {
			pline("You failed to steal anything.");
			setmangry(mdat);
			return 1;
		}
	} else {
		pline("I don't see anybody to rob there!");
		return 0;
	}

	return 0;
}

// ^W command - wish for something
// Unlimited wishes for debug mode by Paul Polderman
static int wiz_wish(void) {
	if (wizard) {
		boolean save_verbose = flags.verbose;

		flags.verbose = false;
		makewish();
		flags.verbose = save_verbose;
		encumber_msg();
	} else
		pline("Unavailable command '^W'.");
	return 0;
}

/* ^I command - identify hero's inventory */
static int wiz_identify(void) {
	if (wizard)
		identify_pack(0);
	else
		pline("Unavailable command '^I'.");
	return 0;
}

/* ^F command - reveal the level map and any traps on it */
static int wiz_map(void) {
	if (wizard) {
		struct trap *t;
		long save_Hconf = HConfusion,
		     save_Hhallu = HHallucination;

		HConfusion = HHallucination = 0L;
		for (t = ftrap; t != 0; t = t->ntrap) {
			t->tseen = 1;
			map_trap(t, true);
		}
		do_mapping();
		HConfusion = save_Hconf;
		HHallucination = save_Hhallu;
	} else
		pline("Unavailable command '^F'.");
	return 0;
}

/* ^G command - generate monster(s); a count prefix will be honored */
static int wiz_gain_level(void) {
	if (wizard)
		pluslvl(false);
	else
		pline("Unavailable command '^J'.");
	return 0;
}

/* BEGIN TSANTH'S CODE */
static int wiz_gain_ac(void) {
	if (wizard) {
		if (u.ublessed < 20) {
			pline("Intrinsic AC increased by 1.");
			HProtection |= FROMOUTSIDE;
			u.ublessed++;
			context.botl = 1;
		} else
			pline("Intrinsic AC is already maximized.");
	} else
		pline("Unavailable command '^C'.");
	return 0;
}

static int wiz_toggle_invulnerability(void) {
	if (wizard) {
		if ((Invulnerable == 0) && (u.uinvulnerable == false)) {
			pline("You will be invulnerable for 32000 turns.");
			Invulnerable = 32000;
			u.uinvulnerable = true;
		} else {
			pline("You are no longer invulnerable.");
			Invulnerable = 0;
			u.uinvulnerable = false;
		}
	} else
		pline("Unavailable command '^N'.");
	return 0;
}
/* END TSANTH'S CODE */

static int wiz_genesis(void) {
	if (wizard)
		create_particular();
	else
		pline("Unavailable command '^G'.");
	return 0;
}

/* ^O command - display dungeon layout */
static int wiz_where(void) {
	if (wizard)
		print_dungeon(false, NULL, NULL);
	else
		pline("Unavailable command '^O'.");
	return 0;
}

/* ^E command - detect unseen (secret doors, traps, hidden monsters) */
static int wiz_detect(void) {
	if (wizard)
		findit();
	else
		pline("Unavailable command '^E'.");
	return 0;
}

/* ^V command - level teleport */
static int wiz_level_tele(void) {
	if (wizard)
		level_tele();
	else
		pline("Unavailable command '^V'.");
	return 0;
}

/* #monpolycontrol command - choose new form for shapechangers, polymorphees */
static int wiz_mon_polycontrol(void) {
	iflags.mon_polycontrol = !iflags.mon_polycontrol;
	pline("Monster polymorph control is %s.",
	      iflags.mon_polycontrol ? "on" : "off");
	return 0;
}

/* #levelchange command - adjust hero's experience level */
static int wiz_level_change(void) {
	char buf[BUFSZ];
	int newlevel;
	int ret;

	getlin("To what experience level do you want to be set?", buf);
	mungspaces(buf);
	if (buf[0] == '\033' || buf[0] == '\0')
		ret = 0;
	else
		ret = sscanf(buf, "%d", &newlevel);

	if (ret != 1) {
		pline("%s", "Never mind.");
		return 0;
	}
	if (newlevel == u.ulevel) {
		pline("You are already that experienced.");
	} else if (newlevel < u.ulevel) {
		if (u.ulevel == 1) {
			pline("You are already as inexperienced as you can get.");
			return 0;
		}
		if (newlevel < 1) newlevel = 1;
		while (u.ulevel > newlevel)
			losexp("#levelchange", true);
	} else {
		if (u.ulevel >= MAXULEV) {
			pline("You are already as experienced as you can get.");
			return 0;
		}
		if (newlevel > MAXULEV) newlevel = MAXULEV;
		while (u.ulevel < newlevel)
			pluslvl(false);
	}
	u.ulevelmax = u.ulevel;
	return 0;
}

/* #panic command - test program's panic handling */
static int wiz_panic(void) {
	if (iflags.debug_fuzzer) {
		u.uhp = u.uhpmax = 1000;
		u.uen = u.uenmax = 1000;
		return 0;
	}

	if (yn("Do you want to call panic() and end your game?") == 'y') {
		panic("crash test.");
	}

	return 0;
}

/* #polyself command - change hero's form */
static int wiz_polyself(void) {
	polyself(1);
	return 0;
}

/* #seenv command */
static int wiz_show_seenv(void) {
	winid win;
	int x, y, v, startx, stopx, curx;
	char row[COLNO + 1];

	win = create_nhwindow(NHW_TEXT);
	/*
	 * Each seenv description takes up 2 characters, so center
	 * the seenv display around the hero.
	 */
	startx = max(1, u.ux - (COLNO / 4));
	stopx = min(startx + (COLNO / 2), COLNO);
	/* can't have a line exactly 80 chars long */
	if (stopx - startx == COLNO / 2) startx++;

	for (y = 0; y < ROWNO; y++) {
		for (x = startx, curx = 0; x < stopx; x++, curx += 2) {
			if (x == u.ux && y == u.uy) {
				row[curx] = row[curx + 1] = '@';
			} else {
				v = levl[x][y].seenv & 0xff;
				if (v == 0)
					row[curx] = row[curx + 1] = ' ';
				else
					sprintf(&row[curx], "%02x", v);
			}
		}
		/* remove trailing spaces */
		for (x = curx - 1; x >= 0; x--)
			if (row[x] != ' ') break;
		row[x + 1] = '\0';

		putstr(win, 0, row);
	}
	display_nhwindow(win, true);
	destroy_nhwindow(win);
	return 0;
}

/* #vision command */
static int wiz_show_vision(void) {
	winid win;
	int x, y, v;
	char row[COLNO + 1];

	win = create_nhwindow(NHW_TEXT);
	sprintf(row, "Flags: 0x%x could see, 0x%x in sight, 0x%x temp lit",
		COULD_SEE, IN_SIGHT, TEMP_LIT);
	putstr(win, 0, row);
	putstr(win, 0, "");
	for (y = 0; y < ROWNO; y++) {
		for (x = 1; x < COLNO; x++) {
			if (x == u.ux && y == u.uy)
				row[x] = '@';
			else {
				v = viz_array[y][x]; /* data access should be hidden */
				if (v == 0)
					row[x] = ' ';
				else
					row[x] = '0' + viz_array[y][x];
			}
		}
		/* remove trailing spaces */
		for (x = COLNO - 1; x >= 1; x--)
			if (row[x] != ' ') break;
		row[x + 1] = '\0';

		putstr(win, 0, &row[1]);
	}
	display_nhwindow(win, true);
	destroy_nhwindow(win);
	return 0;
}

// #wizsmell command - test usmellmon()
static int wiz_smell(void) {
	coord cc;  /* screen pos of unknown glyph */

	cc.x = u.ux;
	cc.y = u.uy;
	if (!olfaction(youmonst.data)) {
		pline("You are incapable of detecting odors in your present form.");
		return 0;
	}

	pline("You can move the cursor to a monster that you want to smell.");
	do {
		int mndx;  /* monster index */
		int glyph; /* glyph at selected position */

		/* Reset some variables. */

		pline("Pick a monster to smell.");
		int ans = getpos(&cc, true, "a monster");
		if (ans < 0 || cc.x < 0) {
			return 0; /* done */
		}
		/* Convert the glyph at the selected position to a mndxbol. */
		glyph = glyph_at(cc.x, cc.y);
		if (glyph_is_monster(glyph)) {
			mndx = glyph_to_mon(glyph);
		} else {
			mndx = 0;
		}
		/* Is it a monster? */
		if (mndx) {
			if (!usmellmon(&mons[mndx])) {
				pline("That monster seems to give off no smell.");
			}
		} else {
			pline("That is not a monster.");
		}
	} while (true);
	return 0;
}

/* #wmode command */
static int wiz_show_wmodes(void) {
	winid win;
	int x, y;
	char row[COLNO + 1];
	struct rm *lev;

	win = create_nhwindow(NHW_TEXT);
	for (y = 0; y < ROWNO; y++) {
		for (x = 0; x < COLNO; x++) {
			lev = &levl[x][y];
			if (x == u.ux && y == u.uy)
				row[x] = '@';
			else if (IS_WALL(lev->typ) || lev->typ == SDOOR)
				row[x] = '0' + (lev->wall_info & WM_MASK);
			else if (lev->typ == CORR)
				row[x] = '#';
			else if (IS_ROOM(lev->typ) || IS_DOOR(lev->typ))
				row[x] = '.';
			else
				row[x] = 'x';
		}
		row[COLNO] = '\0';
		putstr(win, 0, row);
	}
	display_nhwindow(win, true);
	destroy_nhwindow(win);
	return 0;
}

static int wiz_showkills(void) {
	list_vanquished('y', false);
	return 0;
}

/* -enlightenment and conduct- */
static winid en_win;
static const char
	You_[] = "You ",
	are[] = "are ", were[] = "were ",
	have[] = "have ", had[] = "had ",
	can[] = "can ", could[] = "could ";
static const char
	have_been[] = "have been ",
	have_never[] = "have never ", never[] = "never ";

#define enl_msg(prefix, present, past, suffix) \
	enlght_line(prefix, final ? past : present, suffix)
#define you_are(attr)		 enl_msg(You_, are, were, attr)
#define you_have(attr)		 enl_msg(You_, have, had, attr)
#define you_can(attr)		 enl_msg(You_, can, could, attr)
#define you_have_been(goodthing) enl_msg(You_, have_been, were, goodthing)
#define you_have_never(badthing) enl_msg(You_, have_never, never, badthing)
#define you_have_X(something)	 enl_msg(You_, have, "", something)

static void enlght_line(const char *start, const char *middle, const char *end) {
	nhstr buf = nhsfmt("%S%S%S.", start, middle, end);

	putstr(en_win, 0, nhs2cstr_tmp_destroy(&buf));
}

char *warned_of(int warntype, const char *aux) {
	static char buf[BUFSZ];
	buf[0] = '\0';

	/* [ALI] Add support for undead */
	struct {
		unsigned long mask;
		const char *str;
	} warntypes[] = {
		{M2_ORC, "orcs"},
		{M2_DEMON, "demons"},
		{M2_UNDEAD, "undead"},
		{M2_HUMAN, "humans"},
		{M2_ELF, "elves"},
	};

	if (warntype) {
		int n = 0;

		sprintf(buf, "aware of the presence of ");
		if (aux) {
			strcat(buf, aux);
			n++;
		}

		for (usize i = 0; i < SIZE(warntypes); i++) {
			if (warntype & warntypes[i].mask) {
				warntype &= ~warntypes[i].mask;
				if (n > 0) {
					if (warntype) {
						strcat(buf, ", ");
					} else {
						if (n == 1) strcat(buf, " and ");
						else strcat(buf, ", and ");
					}
				}

				strcat(buf, warntypes[i].str);
				n++;
			}
		}
		if (warntype) {
			impossible("Warned of monsters with flags %x???", warntype);
			if (n > 0)
				strcat(buf, " and ");
			strcat(buf, "certain monsters");
		}
	}

	return buf;
}


/* KMH, intrinsic patch -- several of these are updated */
/* 0 => still in progress; 1 => over, survived; 2 => dead */
void enlightenment(int final) {
	int ltmp;
	char buf[BUFSZ];

	en_win = create_nhwindow(NHW_MENU);
	putstr(en_win, 0, final ? "Final Attributes:" : "Current Attributes:");
	putstr(en_win, 0, "");

	if (u.uevent.uhand_of_elbereth) {
		static const char *const hofe_titles[3] = {
			"the Hand of Elbereth",
			"the Envoy of Balance",
			"the Glory of Arioch"};
		you_are(hofe_titles[u.uevent.uhand_of_elbereth - 1]);
	}

	/* note: piousness 20 matches MIN_QUEST_ALIGN (quest.h) */
	if (u.ualign.record >= 20)
		you_are("piously aligned");
	else if (u.ualign.record > 13)
		you_are("devoutly aligned");
	else if (u.ualign.record > 8)
		you_are("fervently aligned");
	else if (u.ualign.record > 3)
		you_are("stridently aligned");
	else if (u.ualign.record == 3)
		you_are("aligned");
	else if (u.ualign.record > 0)
		you_are("haltingly aligned");
	else if (u.ualign.record == 0)
		you_are("nominally aligned");
	else if (u.ualign.record >= -3)
		you_have("strayed");
	else if (u.ualign.record >= -8)
		you_have("sinned");
	else
		you_have("transgressed");
	if (wizard) {
		sprintf(buf, " %d", u.ualign.record);
		enl_msg("Your alignment ", "is", "was", buf);
	}

	/*** Resistances to troubles ***/
	if (Fire_resistance) you_are("fire resistant");
	if (Cold_resistance) you_are("cold resistant");
	if (Sleep_resistance) you_are("sleep resistant");
	if (Disint_resistance) you_are("disintegration-resistant");
	if (Shock_resistance) you_are("shock resistant");
	if (Poison_resistance) you_are("poison resistant");
	if (Drain_resistance) you_are("level-drain resistant");
	if (Sick_resistance) you_are("immune to sickness");
	if (Antimagic) you_are("magic-protected");
	if (Acid_resistance) you_are("acid resistant");
	if (Stone_resistance)
		you_are("petrification resistant");
	if (Invulnerable) you_are("invulnerable");
	if (u.uedibility) you_can("recognize detrimental food");

	/*** Troubles ***/
	if (Halluc_resistance)
		enl_msg("You resist", "", "ed", " hallucinations");
	if (final) {
		if (Hallucination) you_are("hallucinating");
		if (Stunned) you_are("stunned");
		if (Confusion) you_are("confused");
		if (Blinded) you_are("blinded");
		if (Deaf) you_are("deaf");
		if (Sick) {
			if (u.usick_type & SICK_VOMITABLE)
				you_are("sick from food poisoning");
			if (u.usick_type & SICK_NONVOMITABLE)
				you_are("sick from illness");
		}
	}
	if (Stoned) you_are("turning to stone");
	if (Slimed) you_are("turning into slime");
	if (Strangled) you_are((u.uburied) ? "buried" : "being strangled");
	if (Glib) {
		sprintf(buf, "slippery %s", makeplural(body_part(FINGER)));
		you_have(buf);
	}
	if (Fumbling) enl_msg("You fumble", "", "d", "");
	if (Wounded_legs && !u.usteed) {
		sprintf(buf, "wounded %s", makeplural(body_part(LEG)));
		you_have(buf);
	}
	if (Wounded_legs && u.usteed && wizard) {
		strcpy(buf, x_monnam(u.usteed, ARTICLE_YOUR, NULL,
				     SUPPRESS_SADDLE | SUPPRESS_HALLUCINATION, false));
		*buf = highc(*buf);
		enl_msg(buf, " has", " had", " wounded legs");
	}
	if (Sleeping) enl_msg("You ", "fall", "fell", " asleep");
	if (Hunger) enl_msg("You hunger", "", "ed", " rapidly");

	/*** Vision and senses ***/
	if (See_invisible) enl_msg(You_, "see", "saw", " invisible");
	if (Blind_telepat) you_are("telepathic");
	if (Warning) you_are("warned");
	if (Warn_of_mon) {
		unsigned long warn_flags = context.warntype.obj | context.warntype.polyd | context.warntype.intrins;

		if (warn_flags) you_are(warned_of(warn_flags, context.warntype.speciesidx ?
					makeplural(mons[context.warntype.speciesidx].mname) : NULL));
	}
	if (Searching) you_have("automatic searching");
	if (Clairvoyant) you_are("clairvoyant");
	if (Infravision) you_have("infravision");
	if (Detect_monsters) you_are("sensing the presence of monsters");
	if (u.umconf) you_are("going to confuse monsters");

	/*** Appearance and behavior ***/
	if (Adornment) {
		int adorn = 0;

		if (uleft && uleft->otyp == RIN_ADORNMENT) adorn += uleft->spe;
		if (uright && uright->otyp == RIN_ADORNMENT) adorn += uright->spe;
		if (adorn < 0)
			you_are("poorly adorned");
		else
			you_are("adorned");
	}
	if (Invisible)
		you_are("invisible");
	else if (Invis)
		you_are("invisible to others");
	/* ordinarily "visible" is redundant; this is a special case for
	   the situation when invisibility would be an expected attribute */
	else if ((HInvis || EInvis || pm_invisible(youmonst.data)) && BInvis)
		you_are("visible");
	if (Displaced) you_are("displaced");
	if (Stealth) you_are("stealthy");
	if (Aggravate_monster) enl_msg("You aggravate", "", "d", " monsters");
	if (Conflict) enl_msg("You cause", "", "d", " conflict");

	/*** Transportation ***/
	if (Jumping) you_can("jump");
	if (Teleportation) you_can("teleport");
	if (Teleport_control) you_have("teleport control");
	if (Lev_at_will)
		you_are("levitating, at will");
	else if (Levitation)
		you_are("levitating"); /* without control */
	else if (Flying)
		you_can("fly");
	if (Wwalking) you_can("walk on water");
	if (Swimming) you_can("swim");
	if (Breathless)
		you_can("survive without air");
	else if (Amphibious)
		you_can("breathe water");
	if (Passes_walls) you_can("walk through walls");

	/* If you die while dismounting, u.usteed is still set.  Since several
	 * places in the done() sequence depend on u.usteed, just detect this
	 * special case. */
	if (u.usteed && (final < 2 || strcmp(nhs2cstr_tmp(killer.name), "riding accident"))) {
		sprintf(buf, "riding %s", y_monnam(u.usteed));
		you_are(buf);
	}

	if (u.uswallow) {
		sprintf(buf, "swallowed by %s", a_monnam(u.ustuck));
		if (wizard) sprintf(eos(buf), " (%u)", u.uswldtim);
		you_are(buf);
	} else if (u.ustuck) {
		sprintf(buf, "%s %s",
			(Upolyd && sticks(youmonst.data)) ? "holding" : "held by",
			a_monnam(u.ustuck));
		you_are(buf);
	}

	/*** Physical attributes ***/
	if (u.uhitinc)
		you_have(enlght_combatinc("to hit", u.uhitinc, final, buf));
	if (u.udaminc)
		you_have(enlght_combatinc("damage", u.udaminc, final, buf));
	if (Slow_digestion) you_have("slower digestion");
	if (Regeneration) enl_msg("You regenerate", "", "d", "");
	if (u.uspellprot || Protection) {
		int prot = 0;

		if (uleft && uleft->otyp == RIN_PROTECTION) prot += uleft->spe;
		if (uright && uright->otyp == RIN_PROTECTION) prot += uright->spe;
		if (HProtection & INTRINSIC) prot += u.ublessed;
		prot += u.uspellprot;

		if (prot < 0)
			you_are("ineffectively protected");
		else
			you_are("protected");
	}
	if (Protection_from_shape_changers)
		you_are("protected from shape changers");
	if (Polymorph) you_are("polymorphing");
	if (Polymorph_control) you_have("polymorph control");
	if (u.ulycn >= LOW_PM) {
		strcpy(buf, an(mons[u.ulycn].mname));
		you_are(buf);
	}
	if (Upolyd) {
		if (u.umonnum == u.ulycn)
			strcpy(buf, "in beast form");
		else
			sprintf(buf, "polymorphed into %s", an(youmonst.data->mname));
		if (wizard) sprintf(eos(buf), " (%d)", u.mtimedone);
		you_are(buf);
	}
	if (Unchanging) you_can("not change from your current form");
	if (Fast) you_are(Very_fast ? "very fast" : "fast");
	if (Reflecting) you_have("reflection");
	if (Free_action) you_have("free action");
	if (Fixed_abil) you_have("fixed abilities");
	if (uamul && uamul->otyp == AMULET_VERSUS_STONE)
		enl_msg("You ", "will be", "would have been", " depetrified");
	if (Lifesaved)
		enl_msg("Your life ", "will be", "would have been", " saved");
	if (u.twoweap) {
		if (uwep && uswapwep)
			sprintf(buf, "wielding two weapons at once");
		else if (uwep || uswapwep)
			sprintf(buf, "fighting with a weapon and your %s %s",
				uwep ? "left" : "right", body_part(HAND));
		else
			sprintf(buf, "fighting with two %s",
				makeplural(body_part(HAND)));
		you_are(buf);
	}

	/*** Miscellany ***/
	if (Luck) {
		ltmp = abs((int)Luck);
		sprintf(buf, "%s%slucky",
			ltmp >= 10 ? "extremely " : ltmp >= 5 ? "very " : "",
			Luck < 0 ? "un" : "");
		if (wizard) sprintf(eos(buf), " (%d)", Luck);
		you_are(buf);
	} else if (wizard)
		enl_msg("Your luck ", "is", "was", " zero");
	if (u.moreluck > 0)
		you_have("extra luck");
	else if (u.moreluck < 0)
		you_have("reduced luck");
	if (carrying(LUCKSTONE) || stone_luck(true)) {
		ltmp = stone_luck(false);
		if (ltmp <= 0)
			enl_msg("Bad luck ", "does", "did", " not time out for you");
		if (ltmp >= 0)
			enl_msg("Good luck ", "does", "did", " not time out for you");
	}

	/* KMH, balance patch -- healthstones affect health */
	if (u.uhealbonus) {
		sprintf(buf, "%s health", u.uhealbonus > 0 ? "extra" : "reduced");
		if (wizard) sprintf(eos(buf), " (%ld)", u.uhealbonus);
		you_have(buf);
	} else if (wizard)
		enl_msg("Your health bonus ", "is", "was", " zero");

	if (u.ugangr) {
		sprintf(buf, " %sangry with you",
			u.ugangr > 6 ? "extremely " : u.ugangr > 3 ? "very " : "");
		if (wizard) sprintf(eos(buf), " (%d)", u.ugangr);
		enl_msg(u_gname(), " is", " was", buf);
	} else
		/*
		 * We need to suppress this when the game is over, because death
		 * can change the value calculated by can_pray(), potentially
		 * resulting in a false claim that you could have prayed safely.
		 */
		if (!final) {
#if 0
			/* "can [not] safely pray" vs "could [not] have safely prayed" */
			sprintf(buf, "%s%ssafely pray%s", can_pray(false) ? "" : "not ",
			        final ? "have " : "", final ? "ed" : "");
#else
			sprintf(buf, "%ssafely pray", can_pray(false) ? "" : "not ");
#endif
			if (wizard) sprintf(eos(buf), " (%d)", u.ublesscnt);
			you_can(buf);
		}

	{
		const char *p;

		buf[0] = '\0';
		if (final < 2) { /* still in progress, or quit/escaped/ascended */
			p = "survived after being killed ";
			switch (u.umortality) {
				case 0:
					p = !final ? NULL : "survived";
					break;
				case 1:
					strcpy(buf, "once");
					break;
				case 2:
					strcpy(buf, "twice");
					break;
				case 3:
					strcpy(buf, "thrice");
					break;
				default:
					sprintf(buf, "%d times", u.umortality);
					break;
			}
		} else { /* game ended in character's death */
			p = "are dead";
			switch (u.umortality) {
				case 0:
					impossible("dead without dying?");
				case 1:
					break; /* just "are dead" */
				default:
					sprintf(buf, " (%d%s time!)", u.umortality,
						ordin(u.umortality));
					break;
			}
		}
		if (p) enl_msg(You_, "have been killed ", p, buf);
	}

	display_nhwindow(en_win, true);
	destroy_nhwindow(en_win);
	return;
}

/*
 * Courtesy function for non-debug, non-explorer mode players
 * to help refresh them about who/what they are.
 * Returns false if menu cancelled (dismissed with ESC), true otherwise.
 */
static boolean minimal_enlightenment(void) {
	winid tmpwin;
	menu_item *selected;
	anything any;
	int genidx, n;
	char buf[BUFSZ], buf2[BUFSZ];
	static const char untabbed_fmtstr[] = "%-15s: %-12s";
	static const char untabbed_deity_fmtstr[] = "%-17s%s";
	static const char tabbed_fmtstr[] = "%s:\t%-12s";
	static const char tabbed_deity_fmtstr[] = "%s\t%s";
	static const char *fmtstr;
	static const char *deity_fmtstr;

	fmtstr = iflags.menu_tab_sep ? tabbed_fmtstr : untabbed_fmtstr;
	deity_fmtstr = iflags.menu_tab_sep ?
			       tabbed_deity_fmtstr :
			       untabbed_deity_fmtstr;
	any.a_void = 0;
	buf[0] = buf2[0] = '\0';
	tmpwin = create_nhwindow(NHW_MENU);
	start_menu(tmpwin);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, iflags.menu_headings, "Starting", false);

	/* Starting name, race, role, gender */
	sprintf(buf, fmtstr, "name", plname);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	sprintf(buf, fmtstr, "race", urace.noun);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	sprintf(buf, fmtstr, "role",
		(flags.initgend && urole.name.f) ? urole.name.f : urole.name.m);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	sprintf(buf, fmtstr, "gender", genders[flags.initgend].adj);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);

	/* Starting alignment */
	sprintf(buf, fmtstr, "alignment", align_str(u.ualignbase[A_ORIGINAL]));
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);

	/* Current name, race, role, gender */
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, "", false);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, iflags.menu_headings, "Current", false);
	sprintf(buf, fmtstr, "race", Upolyd ? youmonst.data->mname : urace.noun);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	if (Upolyd) {
		sprintf(buf, fmtstr, "role (base)",
			(u.mfemale && urole.name.f) ? urole.name.f : urole.name.m);
		add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	} else {
		sprintf(buf, fmtstr, "role",
			(flags.female && urole.name.f) ? urole.name.f : urole.name.m);
		add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	}
	/* don't want poly_gender() here; it forces `2' for non-humanoids */
	genidx = is_neuter(youmonst.data) ? 2 : flags.female;
	sprintf(buf, fmtstr, "gender", genders[genidx].adj);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	if (Upolyd && (int)u.mfemale != genidx) {
		sprintf(buf, fmtstr, "gender (base)", genders[u.mfemale].adj);
		add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);
	}

	/* Current alignment */
	sprintf(buf, fmtstr, "alignment", align_str(u.ualign.type));
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);

	/* Deity list */
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, "", false);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, iflags.menu_headings, "Deities", false);
	sprintf(buf2, deity_fmtstr, align_gname(A_CHAOTIC),
		(u.ualignbase[A_ORIGINAL] == u.ualign.type && u.ualign.type == A_CHAOTIC) ? " (s,c)" :
		(u.ualignbase[A_ORIGINAL] == A_CHAOTIC) ? " (s)" :
		(u.ualign.type == A_CHAOTIC) ? " (c)" : "");
	sprintf(buf, fmtstr, "Chaotic", buf2);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);

	sprintf(buf2, deity_fmtstr, align_gname(A_NEUTRAL),
		(u.ualignbase[A_ORIGINAL] == u.ualign.type && u.ualign.type == A_NEUTRAL) ? " (s,c)" :
		(u.ualignbase[A_ORIGINAL] == A_NEUTRAL) ? " (s)" :
		(u.ualign.type == A_NEUTRAL) ? " (c)" : "");
	sprintf(buf, fmtstr, "Neutral", buf2);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);

	sprintf(buf2, deity_fmtstr, align_gname(A_LAWFUL),
		(u.ualignbase[A_ORIGINAL] == u.ualign.type && u.ualign.type == A_LAWFUL) ?  " (s,c)" :
		(u.ualignbase[A_ORIGINAL] == A_LAWFUL) ? " (s)" :
		(u.ualign.type == A_LAWFUL) ? " (c)" : "");
	sprintf(buf, fmtstr, "Lawful", buf2);
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, false);

	end_menu(tmpwin, "Base Attributes");
	n = select_menu(tmpwin, PICK_NONE, &selected);
	destroy_nhwindow(tmpwin);
	return n != -1;
}

static int doattributes(void) {
	if (!minimal_enlightenment())
		return 0;
	if (wizard || discover)
		enlightenment(0);
	return 0;
}

static const struct menu_tab game_menu[] = {
	{'O', true, doset, "Options"},
	{'r', true, doredraw, "Redraw Screen"},
	{'x', true, enter_explore_mode, "Enter Explore Mode"},
	{'S', true, dosave, "Save"},
	{'q', true, done2, "Quit [M-q]"},
	{0, 0, 0, 0},
};

static const struct menu_tab inv_menu[] = {
	{(char)0, true, NULL, "View Inventory"},
	{'i', true, ddoinv, "Inventory List"},
	{'I', true, dotypeinv, "Inventory List by Type"},
	{'*', true, doprinuse, "Items in use"},
	{(char)0, true, NULL, "Ready Items"},
	{'w', false, dowield, "Wield Weapon"},
	{'W', false, dowear, "Wear Protective Gear"},
	{'Q', false, dowieldquiver, "Prepare missile weapon (in Quiver)"},
	{'T', false, dotakeoff, "Take off Protective Gear"},
	{(char)0, true, NULL, "Manipulate Items"},
	{'a', false, doapply, "Apply an object"},
	{'d', false, dodip, "Dip an object [M-d]"},
	{'E', false, doengrave, "Engrave into the ground"},
	{'f', false, dofire, "Fire your prepared missile weapon"},
	{'i', true, doinvoke, "Invoke your weapon"},
	{'t', false, dothrow, "Throw an item"},
	{(char)0, true, NULL, "Drop Items"},
	{'d', false, dodrop, "Drop an object"},
	{'D', false, doddrop, "Multi-Drop"},
	{0, 0, 0, 0}};

static const struct menu_tab action_menu[] = {
	{'c', false, doclose, "Close a door"},
	{'e', false, doeat, "Eat some food"},
	{'f', false, doforce, "Force a lock [M-f]"},
	{'l', false, doloot, "Loot an object"},
	{'o', false, doopen, "Open a door"},
	{'q', true, dodrink, "Quaff a potion"},
	{'r', false, doread, "Read an object"},
	{'u', false, dountrap, "Untrap"},
	{'z', false, dozap, "Zap a wand"},
	{'Z', true, docast, "Cast a spell"},
	{0, 0, 0, 0}};

static const struct menu_tab player_menu[] = {
	{'b', false, playersteal, "Steal from Monsters [M-b]"},
	{'c', true, dotalk, "Chat with Monsters [M-c]"},
	{'d', false, dokick, "Do Kick"},
	/*        {'e', false, specialpower, "Use your Class Ability [M-e]"},*/
	{'e', true, enhance_weapon_skill, "Weapon Skills [M-k]"},
	{'m', true, domonability, "Use your Monster Ability [M-m]"},
	{'o', false, dosacrifice, "Offer a Sacrifice [M-o]"},
	{'p', false, dopay, "Pay the Shopkeeper"},
	{'s', false, dosit, "Sit down [M-s]"},
	{'t', true, dotele, "Controlled Teleport [C-t]"},
	/*	{'T', true, doturn, "Turn Undead [M-t]"},*/
	{'T', true, dotech, "Use Techniques [M-t]"},
	{'x', true, doattributes, "Show attributes"},
	{'y', true, polyatwill, "Self-Polymorph [M-y]"},
	{0, 0, 0, 0}};

static const struct menu_tab wizard_menu[] = {
	{'c', true, wiz_gain_ac, "Increase AC"},
	{'d', true, wiz_show_display, "Detail display layers"},
	{'e', true, wiz_detect, "Detect secret doors and traps"},
	{'f', true, wiz_map, "Do magic mapping"},
	{'g', true, wiz_genesis, "Create monster"},
	{'i', true, wiz_identify, "Identify items in pack"},
	{'j', true, wiz_gain_level, "Go up an experience level"},
	{'n', true, wiz_toggle_invulnerability, "Toggle invulnerability"},
	{'v', true, wiz_level_tele, "Do trans-level teleport"},
	{'w', true, wiz_wish, "Make wish"},
	{'L', true, wiz_light_sources, "show mobile light sources"},
	{'M', true, wiz_show_stats, "show memory statistics"},
	{'S', true, wiz_show_seenv, "show seen vectors"},
	{'T', true, wiz_timeout_queue, "look at timeout queue"},
	{'V', true, wiz_show_vision, "show vision array"},
	{'W', true, wiz_show_wmodes, "show wall modes"},
#ifdef DEBUG
	{'&', true, wiz_debug_cmd, "wizard debug command"},
#endif
	{0, 0, 0, 0, 0},
};

static const struct menu_tab help_menu[] = {
	{'?', true, dohelp, "Help Contents"},
	{'/', true, dowhatis, "Identify an object on the screen"},
	{'&', true, dowhatdoes, "Determine what a key does"},
	{0, 0, 0, 0, 0},
};

static const struct menu_tab main_menu[] = {
	{'g', true, NULL, "Game"},
	{'i', true, NULL, "Inventory"},
	{'a', true, NULL, "Action"},
	{'p', true, NULL, "Player"},
	{'d', true, NULL, "Discoveries"},
	{'w', true, NULL, "Wizard"},
	{'?', true, NULL, "Help"},
	{0, 0, 0, 0},
};

static const struct menu_tab discover_menu[] = {
	{'X', true, dovspell, "View known spells"},		    /* Mike Stephenson */
	{'d', true, dodiscovered, "Items already discovered [\\]"}, /* Robert Viduya */
	{'C', true, do_mname, "Name a monster"},
	{0, 0, 0, 0},
};

static struct menu_list main_menustruct[] = {
	{"Game", "Main Menu", game_menu},
	{"Inventory", "Main Menu", inv_menu},
	{"Action", "Main Menu", action_menu},
	{"Player", "Main Menu", player_menu},
	{"Discoveries", "Main Menu", discover_menu},
	{"Wizard", "Main Menu", wizard_menu},
	{"Help", "Main Menu", help_menu},
	{"Main Menu", NULL, main_menu},
	{0, 0, 0},
};

static int makemenu(const char *menuname, struct menu_list menu_struct[]) {
	winid win;
	anything any;
	menu_item *selected;
	int n, i, (*func)(void);
	const struct menu_tab *current_menu = NULL;

	any.a_void = 0;
	win = create_nhwindow(NHW_MENU);
	start_menu(win);

	for (i = 0; menu_struct[i].m_header; i++) {
		if (strcmp(menu_struct[i].m_header, menuname)) continue;
		current_menu = menu_struct[i].m_menu;
		for (n = 0; current_menu[n].m_item; n++) {
			if (u.uburied && !current_menu[n].can_if_buried) continue;
			if (!wizard && !current_menu[n].m_funct && !strcmp(current_menu[n].m_item, "Wizard")) continue;
			if (current_menu[n].m_char == (char)0) {
				any.a_int = 0;
				add_menu(win, NO_GLYPH, &any, 0, 0, ATR_BOLD,
					 current_menu[n].m_item, MENU_UNSELECTED);
				continue;
			}
			any.a_int = n + 1; /* non-zero */
			add_menu(win, NO_GLYPH, &any, current_menu[n].m_char,
				 0, ATR_NONE, current_menu[n].m_item, MENU_UNSELECTED);
		}
		break;
	}
	end_menu(win, menuname);
	n = select_menu(win, PICK_ONE, &selected);
	destroy_nhwindow(win);
	if (n > 0) {
		/* we discard 'const' because some compilers seem to have
		       trouble with the pointer passed to set_occupation() */
		i = selected[0].item.a_int - 1;
		func = current_menu[i].m_funct;
		if (current_menu[i].m_text && !occupation && multi)
			set_occupation(func, current_menu[i].m_text, multi);
		/*WAC catch void into makemenu */
		if (func == NULL)
			return makemenu(current_menu[i].m_item, menu_struct);
		else
			return (*func)(); /* perform the command */
	} else if (n < 0) {
		for (i = 0; menu_struct[i].m_header; i++) {
			if (menuname == menu_struct[i].m_header) {
				if (menu_struct[i].m_parent)
					return makemenu(menu_struct[i].m_parent, menu_struct);
				else
					return 0;
			}
		}
	}
	return 0;
}

static int domenusystem(void) {
	return makemenu("Main Menu", main_menustruct);
}

/* KMH, #conduct
 * (shares enlightenment's tense handling)
 */
static int doconduct(void) {
	show_conduct(0);
	return 0;
}

/* format increased damage or chance to hit */
static char *enlght_combatinc(const char *inctyp, int incamt, int final, char *outbuf) {
	char numbuf[24];
	const char *modif, *bonus;

	if (final || wizard) {
		sprintf(numbuf, "%s%d",
			(incamt > 0) ? "+" : "", incamt);
		modif = (const char *)numbuf;
	} else {
		int absamt = abs(incamt);

		if (absamt <= 3)
			modif = "small";
		else if (absamt <= 6)
			modif = "moderate";
		else if (absamt <= 12)
			modif = "large";
		else
			modif = "huge";
	}
	bonus = (incamt > 0) ? "bonus" : "penalty";
	/* "bonus to hit" vs "damage bonus" */
	if (!strcmp(inctyp, "damage")) {
		const char *ctmp = inctyp;
		inctyp = bonus;
		bonus = ctmp;
	}
	sprintf(outbuf, "%s %s %s", an(modif), bonus, inctyp);
	return outbuf;
}

void show_conduct(int final) {
	char buf[BUFSZ];
	int ngenocided;

	/* Create the conduct window */
	en_win = create_nhwindow(NHW_MENU);
	putstr(en_win, 0, "Voluntary challenges:");
	putstr(en_win, 0, "");

	if (!u.uconduct.food && !u.uconduct.unvegan)
		enl_msg(You_, "have gone", "went", " without food");
	/* But beverages are okay */
	else if (!u.uconduct.food)
		enl_msg(You_, "have gone", "went", " without eating");
	/* But quaffing animal products (eg., blood) is okay */
	else if (!u.uconduct.unvegan)
		you_have_X("followed a strict vegan diet");
	else if (!u.uconduct.unvegetarian)
		you_have_been("vegetarian");

	if (!u.uconduct.gnostic)
		you_have_been("an atheist");

	if (!u.uconduct.weaphit) {
		you_have_never("hit with a wielded weapon");
	} else if (wizard) {
		sprintf(buf, "used a wielded weapon %ld time%s",
			u.uconduct.weaphit, plur(u.uconduct.weaphit));
		you_have_X(buf);
	}
	if (!u.uconduct.killer)
		you_have_been("a pacifist");

	if (!u.uconduct.literate) {
		you_have_been("illiterate");
	} else if (wizard) {
		sprintf(buf, "read items or engraved %ld time%s",
			u.uconduct.literate, plur(u.uconduct.literate));
		you_have_X(buf);
	}

	ngenocided = num_genocides();
	if (ngenocided == 0) {
		you_have_never("genocided any monsters");
	} else {
		sprintf(buf, "genocided %d type%s of monster%s",
			ngenocided, plur(ngenocided), plur(ngenocided));
		you_have_X(buf);
	}

	if (!u.uconduct.polypiles) {
		you_have_never("polymorphed an object");
	} else if (wizard) {
		sprintf(buf, "polymorphed %ld item%s",
			u.uconduct.polypiles, plur(u.uconduct.polypiles));
		you_have_X(buf);
	}

	if (!u.uconduct.polyselfs) {
		you_have_never("changed form");
	} else if (wizard) {
		sprintf(buf, "changed form %ld time%s",
			u.uconduct.polyselfs, plur(u.uconduct.polyselfs));
		you_have_X(buf);
	}

	if (!u.uconduct.wishes)
		you_have_X("used no wishes");
	else {
		sprintf(buf, "used %ld wish%s",
			u.uconduct.wishes, (u.uconduct.wishes > 1L) ? "es" : "");
		you_have_X(buf);

		if (!u.uconduct.wisharti)
			enl_msg(You_, "have not wished", "did not wish",
				" for any artifacts");
	}

	if (!u.uconduct.celibacy) {
		you_have_X("remained celibate");
	} else if (wizard) {
		sprintf(buf, "your vow of celibacy %ld time%s",
			u.uconduct.celibacy, plur(u.uconduct.celibacy));
		enl_msg(You_, "have broken ", "broke ", buf);
	}

	/* Pop up the window and wait for a key */
	display_nhwindow(en_win, true);
	destroy_nhwindow(en_win);
}

// TODO make sure all these commands make it into the ext cmd list
#if 0
static const struct func_tab cmdlist[] = {
	/*	{C('s'), false, specialpower},*/
	/*WAC replace with dowear*/
	{'P', false, doputon},
	{M('p'), true, dopray},
	{'q', false, dodrink},
	{'Q', false, dowieldquiver},
	{M('q'), true, done2},
	{'u', false, dountrap}, /* if number_pad is on */
	{M('u'), false, dountrap},
	{'v', true, doversion},
	{'V', true, dohistory},
	/*replaced with dowear*/
	{'w', false, dowield},
	{'W', false, dowear},
	{M('w'), false, dowipe},
	{'x', false, doswapweapon},                    /* [Tom] */
	{'X', true, enter_explore_mode},
#if 0
	{M('x'), true, dovspell},                  /* Mike Stephenson */
#endif
	/*	'y', 'Y' : go nw */
	{'/', true, dowhatis},
	{'&', true, dowhatdoes},
	{M('?'), true, doextlist},
	/* WAC Angband style items in use, menusystem
		{'*', true, doinvinuse}, */
	{'`', true, domenusystem},
	{'~', true, domenusystem},
};
#endif

/* maps extended ascii codes for key presses to either
 *   - extended command entries in extcmdlist
 *   - other key press entries */
static struct key_tab cmdlist[256];

/* list built upon option loading; holds list of keys to be rebound later
 * see "crappy hack" below */
static struct binding_list_tab *bindinglist = NULL;

#define AUTOCOMPLETE true
#define IFBURIED     true

struct ext_func_tab extcmdlist[] = {
	{"2weapon", "toggle two-weapon combat", dotwoweapon, !IFBURIED, AUTOCOMPLETE},
	{"apply", "apply (use) a tool (pick-axe, key, lamp...)", doapply, !IFBURIED},
	{"attributes", "show your attributes (intrinsic ones included in debug or explore mode)", doattributes, IFBURIED},
	{"borrow", "steal from monsters", playersteal, !IFBURIED}, /* jla */
	{"close", "close a door", doclose, !IFBURIED},
	{"cast", "zap (cast) a spell", docast, IFBURIED},
	{"discoveries", "show what object types have been discovered", dodiscovered, IFBURIED},
	{"down", "go down a staircase", dodown, !IFBURIED},
	{"drop", "drop an item", dodrop, !IFBURIED},
	{"dropall", "drop specific item types", doddrop, !IFBURIED},
	{"takeoffall", "remove all armor", doddoremarm, !IFBURIED},
	{"inventory", "show your inventory", ddoinv, IFBURIED},
	{"quaff", "quaff (drink) something", dodrink, !IFBURIED},
	{"#", "perform an extended command", doextcmd, IFBURIED},
	{"travel", "Travel to a specific location", dotravel, !IFBURIED},
	{"eat", "eat something", doeat, !IFBURIED},
	{"engrave", "engrave writing on the floor", doengrave, !IFBURIED},
	{"fire", "fire ammunition from quiver", dofire, !IFBURIED},
	{"history", "show long version and game history", dohistory, IFBURIED},
	{"help", "give a help message", dohelp, IFBURIED},
	{"seetrap", "show the type of a trap", doidtrap, IFBURIED},
	{"kick", "kick something", dokick, !IFBURIED},
	{"look", "loot a box on the floor", dolook, IFBURIED},
	{"call", "call (name) a particular monster", do_mname, IFBURIED},
	{"wait", "rest one move while doing nothing", donull, IFBURIED},
	{"previous", "toggle through previously displayed game messages", doprev_message, IFBURIED},
	{"open", "open a door", doopen, !IFBURIED},
	{"pickup", "pick up things at the current location", dopickup, !IFBURIED},
	{"pay", "pay your shopping bill", dopay, !IFBURIED},
	{"puton", "put on an accessory (ring amulet, etc)", doputon, !IFBURIED},
	{"seeweapon", "show the weapon currently wielded", doprwep, IFBURIED},
	{"seearmor", "show the armor currently worn", doprarm, IFBURIED},
	{"seerings", "show the ring(s) currently worn", doprring, IFBURIED},
	{"seeamulet", "show the amulet currently worn", dopramulet, IFBURIED},
	{"seetools", "show the tools currently in use", doprtool, IFBURIED},
	{"seeall", "show all equipment in use (generally, ),[,=,\",( commands", doprinuse, IFBURIED},
	{"seegold", "count your gold", doprgold, IFBURIED},
	{"glance", "show what type of thing a map symbol on the level corresponds to", doquickwhatis, IFBURIED},
	{"remove", "remove an accessory (ring, amulet, etc)", doremring, !IFBURIED},
	{"read", "read a scroll or spellbook", doread, !IFBURIED},
	{"redraw", "redraw screen", doredraw, IFBURIED},
#ifdef SUSPEND
	{"suspend", "suspend game (only if defined)", dosuspend, IFBURIED, .disallowed_if_fuzzing = true},
#endif /* SUSPEND */
	{"setoptions", "show option settings, possibly change them", doset, IFBURIED},
	{"search", "search for traps and secret doors", dosearch, IFBURIED, !AUTOCOMPLETE, "searching"},
	{"save", "save the game", dosave, IFBURIED, .disallowed_if_fuzzing = true},
	{"swap", "swap wielded and secondary weapons", doswapweapon, !IFBURIED},
	{"throw", "throw something", dothrow, !IFBURIED},
	{"takeoff", "take off one piece of armor", dotakeoff, !IFBURIED},
	{"teleport", "teleport around level", dotele, IFBURIED},
	{"inventoryall", "inventory specific item types", dotypeinv, IFBURIED},
	{"autopickup", "toggle the pickup option on/off", dotogglepickup, IFBURIED},
	{"up", "go up a staircase", doup, !IFBURIED},
	{"version", "show version", doversion, IFBURIED},
	{"seespells", "list known spells", dovspell, IFBURIED},
	{"quiver", "select ammunition for quiver", dowieldquiver, !IFBURIED},
	{"whatis", "show what type of thing a symbol corresponds to", dowhatis, IFBURIED},
	{"whatdoes", "tell what a command does", dowhatdoes, IFBURIED},
	{"wield", "wield (put in use) a weapon", dowield, !IFBURIED},
	{"wear", "wear a piece of armor", dowear, !IFBURIED},
	{"zap", "zap a wand", dozap, !IFBURIED},
	{"explore_mode", "enter explore (discovery) mode (only if defined)", enter_explore_mode, IFBURIED},

	{"adjust", "adjust inventory letters", doorganize, IFBURIED, AUTOCOMPLETE},
	{"annotate", "name current level", donamelevel, IFBURIED, AUTOCOMPLETE},
	{"chat", "talk to someone", dotalk, IFBURIED, AUTOCOMPLETE}, /* converse? */
	{"conduct", "list which challenges you have adhered to", doconduct, IFBURIED, AUTOCOMPLETE},
	{"dip", "dip an object into something", dodip, !IFBURIED, AUTOCOMPLETE},
	{"enhance", "advance or check weapons skills", enhance_weapon_skill, IFBURIED, AUTOCOMPLETE},
	{"force", "force a lock", doforce, !IFBURIED, AUTOCOMPLETE},
	{"invoke", "invoke an object's powers", doinvoke, IFBURIED, AUTOCOMPLETE},
	{"jump", "jump to a location", dojump, !IFBURIED, AUTOCOMPLETE},
	{"loot", "loot a box on the floor", doloot, !IFBURIED, AUTOCOMPLETE},
	{"monster", "use a monster's special ability", domonability, IFBURIED, AUTOCOMPLETE},
	{"name", "name an item or type of object", ddocall, IFBURIED, AUTOCOMPLETE},
	{"offer", "offer a sacrifice to the gods", dosacrifice, !IFBURIED, AUTOCOMPLETE},
	{"overview", "show an overview of the dungeon", dooverview, IFBURIED, AUTOCOMPLETE},
	{"pray", "pray to the gods for help", dopray, IFBURIED, AUTOCOMPLETE},
	{"quit", "exit without saving current game", done2, IFBURIED, AUTOCOMPLETE, .disallowed_if_fuzzing = true},
	{"removeimarkers", "remove markers of remembered invisible monsters", doremoveimarkers, IFBURIED, !AUTOCOMPLETE},
	{"ride", "ride (or stop riding) a monster", doride, !IFBURIED, AUTOCOMPLETE},
	{"rub", "rub a lamp", dorub, !IFBURIED, AUTOCOMPLETE},
	{"sit", "sit down", dosit, !IFBURIED, AUTOCOMPLETE},
	{"technique", "perform a technique", dotech, IFBURIED, AUTOCOMPLETE},
	{"tip", "tip objects out of a container", dotip, IFBURIED, AUTOCOMPLETE},
	{"turn", "turn undead", doturn, IFBURIED, AUTOCOMPLETE},
	{"twoweapon", "toggle two-weapon combat", dotwoweapon, !IFBURIED, AUTOCOMPLETE},
	{"untrap", "untrap something", dountrap, !IFBURIED, AUTOCOMPLETE},
	{"vanquished", "list vanquished monsters", dolistvanq, true},
	{"wipe", "wipe off your face", dowipe, !IFBURIED, AUTOCOMPLETE},
	{"youpoly", "polymorph at will", polyatwill, !IFBURIED}, /* jla */
	{"?", "get this list of extended commands", doextlist, IFBURIED, AUTOCOMPLETE},
#if 0
	{"ethics", "list which challenges you have adhered to", doethics, IFBURIED},
#endif
#ifdef SHOUT
	{"shout", "say something loud", doyell, IFBURIED}, /* jrn */
#endif

	/*
	 * There must be a blank entry here for every entry in the table
	 * below.
	 */

	{NULL, NULL, donull, true},  // #display
	{NULL, NULL, donull, true},  // #levelchange
	{NULL, NULL, donull, true},  // #lightsources
#ifdef DEBUG_MIGRATING_MONS
	{NULL, NULL, donull, true},  // #migratemons
#endif
	{NULL, NULL, donull, true},  // #monpolycontrol
	{NULL, NULL, donull, true},  // #panic
	{NULL, NULL, donull, true},  // #polyself
#ifdef PORT_DEBUG
	{NULL, NULL, donull, true},  // #portdebug
#endif
	{NULL, NULL, donull, true},  // #seenv
	{NULL, NULL, donull, true},  // #stats
	{NULL, NULL, donull, true},  // #timeout
	{NULL, NULL, donull, true},  // #vision
	{NULL, NULL, donull, true},  // #wizsmell
#ifdef DEBUG
	{NULL, NULL, donull, true},  // #wizdebug
#endif
	{NULL, NULL, donull, true},  // #wmode
	{NULL, NULL, donull, true},  // #showkills
	{NULL, NULL, donull, true},  // #detect
	{NULL, NULL, donull, true},  // #map
	{NULL, NULL, donull, true},  // #genesis
	{NULL, NULL, donull, true},  // #identify
	{NULL, NULL, donull, true},  // #levelport
	{NULL, NULL, donull, true},  // #wish
	{NULL, NULL, donull, true},  // #where
	{NULL, NULL, donull, true},  // terminator
};

static struct ext_func_tab debug_extcmdlist[] = {
	{"display", "detail display layers", wiz_show_display, IFBURIED, !AUTOCOMPLETE},
	{"levelchange", "change experience level", wiz_level_change, IFBURIED, AUTOCOMPLETE},
	{"lightsources", "show mobile light sources", wiz_light_sources, IFBURIED, AUTOCOMPLETE},
#ifdef DEBUG_MIGRATING_MONS
	{"migratemons", "migrate n random monsters", wiz_migrate_mons, IFBURIED, AUTOCOMPLETE},
#endif
	{"monpolycontrol", "control monster polymorphs", wiz_mon_polycontrol, IFBURIED, AUTOCOMPLETE},
	{"panic", "test panic routine (fatal to game)", wiz_panic, IFBURIED, AUTOCOMPLETE},
	{"polyself", "polymorph self", wiz_polyself, IFBURIED, AUTOCOMPLETE},
#ifdef PORT_DEBUG
	{"portdebug", "wizard port debug command", wiz_port_debug, IFBURIED, AUTOCOMPLETE},
#endif
	{"seenv", "show seen vectors", wiz_show_seenv, IFBURIED, AUTOCOMPLETE},
	{"stats", "show memory statistics", wiz_show_stats, IFBURIED, AUTOCOMPLETE},
	{"timeout", "look at timeout queue", wiz_timeout_queue, IFBURIED, AUTOCOMPLETE},
	{"vision", "show vision array", wiz_show_vision, IFBURIED, AUTOCOMPLETE},
	{"wizsmell", "smell monster", wiz_smell, IFBURIED, AUTOCOMPLETE},
#ifdef DEBUG
	{"wizdebug", "wizard debug command", wiz_debug_cmd, IFBURIED, AUTOCOMPLETE},
#endif
	{"wmode", "show wall modes", wiz_show_wmodes, IFBURIED, AUTOCOMPLETE},
	{"showkills", "show numbers of monsters killed", wiz_showkills, IFBURIED, AUTOCOMPLETE},
	{"detect", "detect secret doors and traps", wiz_detect, IFBURIED},
	{"map", "do magic mapping", wiz_map, IFBURIED},
	{"genesis", "create monster", wiz_genesis, IFBURIED},
	{"identify", "identify items in pack", wiz_identify, IFBURIED},
	{"levelport", "to trans-level teleport", wiz_level_tele, IFBURIED},
	{"wish", "make wish", wiz_wish, IFBURIED},
	{"where", "tell locations of special levels", wiz_where, IFBURIED},
	{NULL, NULL, donull, IFBURIED}, // terminator
};

static void bind_key(unsigned char key, char *command) {
	struct ext_func_tab *extcmd;

	/* special case: "nothing" is reserved for unbinding */
	if (!strcmp(command, "nothing")) {
		cmdlist[key].bind_cmd = NULL;
		return;
	}

	for (extcmd = extcmdlist; extcmd->ef_txt; extcmd++) {
		if (strcmp(command, extcmd->ef_txt)) continue;
		cmdlist[key].bind_cmd = extcmd;

		return;
	}

	pline("Bad command %s matched with key %c (ASCII %i). "
	      "Ignoring command.\n",
	      command, key, key);
}

static void init_bind_list(void) {
	bind_key(C('d'), "kick"); /* "D" is for door!...?  Msg is in dokick.c */
	bind_key(C('i'), "removeimarkers"); // do this first so it can be overridden in wizmode
	if (wizard) {
		//bind_key(C('c'), "gainac");
		bind_key(C('e'), "detect");
		bind_key(C('f'), "map");
		bind_key(C('g'), "genesis");
		bind_key(C('i'), "identify");
		//bind_key(C('j'), "gainlevel");
		//bind_key(C('n'), "toggleinvulnerability");
		bind_key(C('o'), "where");
		bind_key(C('v'), "levelport");
		bind_key(C('w'), "wish");
	}
	bind_key(C('l'), "redraw"); /* if number_pad is set */
	bind_key(C('p'), "previous");
	bind_key(C('r'), "redraw");
	bind_key(C('t'), "teleport");
	bind_key(C('x'), "attributes");
	bind_key('a', "apply");
	bind_key('A', "takeoffall");
	bind_key(M('a'), "adjust");
	/*       'b', 'B' : go sw */
	bind_key(C('b'), "borrow");
	bind_key('c', "close");
	bind_key('C', "call");
	bind_key(M('c'), "chat");
	bind_key('d', "drop");
	bind_key('D', "dropall");
	bind_key(M('d'), "dip");
	bind_key('e', "eat");
	bind_key('E', "engrave");
	bind_key(M('e'), "enhance");
	bind_key('f', "fire");
	/*       'F' : fight (one time) */
	bind_key(M('f'), "force");
	/*       'g', 'G' : multiple go */
	/*       'h', 'H' : go west */
	bind_key('h', "help"); /* if number_pad is set */
	bind_key('i', "inventory");
	bind_key('I', "inventoryall"); /* Robert Viduya */
	bind_key(M('i'), "invoke");
	bind_key('j', "jump");
	/*       'j', 'J', 'k', 'K', 'l', 'L', 'm', 'M', 'n', 'N' : move commands */
	bind_key(M('j'), "jump"); /* if number_pad is on */
	bind_key('k', "kick");	  /* if number_pad is on */
	bind_key('K', "vanquished");/*if number_pad is on */
	//bind_key(M('k'), "enhance"); /*is this really necessary?*/
	bind_key('l', "loot");	  /* if number_pad is on */
	bind_key(M('l'), "loot");
	bind_key(M('m'), "monster");
	bind_key('N', "name");
	/*       'n' prefixes a count if number_pad is on */
	bind_key(M('n'), "name");
	bind_key(C('n'), "annotate");
	bind_key('o', "open");
	bind_key('O', "setoptions");
	bind_key(C('o'), "overview");  //overwrites #where
	bind_key(M('o'), "offer");
	bind_key('p', "pay");
	bind_key('P', "puton");
	bind_key(M('p'), "pray");
	bind_key('q', "quaff");
	bind_key('Q', "quiver");
	bind_key(M('q'), "quit");
	bind_key('r', "read");
	bind_key('R', "remove");
	bind_key(M('r'), "rub");
	bind_key('s', "search");
	bind_key('S', "save");
	bind_key(M('s'), "sit");
	bind_key('t', "throw");
	bind_key('T', "takeoff");
	//bind_key(M('t'), "turn");
	bind_key(M('t'), "technique");
	/*        'u', 'U' : go ne */
	bind_key('u', "untrap"); /* if number_pad is on */
	bind_key(M('u'), "untrap");
	bind_key('v', "version");
	bind_key('V', "history");
	bind_key('w', "wield");
	bind_key('W', "wear");
	bind_key(M('w'), "wipe");
	bind_key('x', "swap");
	bind_key('X', "explore_mode");
#if 0
	{M('x'), true, dovspell},                  /* Mike Stephenson */
#endif
	/*        'y', 'Y' : go nw */
	bind_key(M('y'), "youpoly");
	bind_key('z', "zap");
	bind_key('Z', "cast");
#ifdef SUSPEND
	bind_key(C('z'), "suspend");
#endif
	bind_key('<', "up");
	bind_key('>', "down");
	bind_key('/', "whatis");
	bind_key('&', "whatdoes");
	bind_key('?', "help");
	bind_key(M('?'), "?");
	bind_key('.', "wait");
	bind_key(' ', "wait");
	bind_key(',', "pickup");
	bind_key(':', "look");
	bind_key(';', "glance");
	bind_key('^', "seetrap");
	bind_key('\\', "discoveries"); /* Robert Viduya */
	bind_key('@', "autopickup");
	bind_key(M('2'), "twoweapon");
	bind_key(WEAPON_SYM, "seeweapon");
	bind_key(ARMOR_SYM, "seearmor");
	bind_key(RING_SYM, "seerings");
	bind_key(AMULET_SYM, "seeamulet");
	bind_key(TOOL_SYM, "seetools");
	bind_key('*', "seeall"); /* inventory of all equipment in use */
	bind_key(GOLD_SYM, "seegold");
	bind_key(SPBOOK_SYM, "seespells"); /* Mike Stephenson */
	bind_key('#', "#");
	bind_key('_', "travel");
}

/* takes the list of bindings loaded from the options file, and changes cmdlist
 * to match it */
static void change_bind_list(void) {
	struct binding_list_tab *binding;

	/* TODO: they must be loaded forward,
	 * not backward as they are now */
	while ((binding = bindinglist)) {
		bindinglist = bindinglist->next;
		bind_key(binding->key, binding->extcmd);
		free(binding->extcmd);
		free(binding);
	}
}

/* mapping a key to itself should cause an infinite loop... */
static void verify_key_list(void) {
	// TODO
}

/*
 * Insert debug commands into the extended command list.  This function
 * assumes that the last entry will be the help entry.
 *
 * You must add entries in ext_func_tab every time you add one to the
 * debug_extcmdlist().
 */
static void add_debug_extended_commands(void) {
	int n;

	/* count the # of help entries */
	for (n = 0; extcmdlist[n].ef_txt; n++);

	if (SIZE(extcmdlist) - n != SIZE(debug_extcmdlist)) {
		impossible("extcmdlist has %d-%d=%d space left for debug entries, should be %d", SIZE(extcmdlist), n, SIZE(extcmdlist) - n, SIZE(debug_extcmdlist));
	}

	memcpy(&extcmdlist[n], debug_extcmdlist, sizeof(extcmdlist[0]) * (SIZE(debug_extcmdlist)-1));
}

/* list all keys and their bindings, like dat/hh but dynamic */
void dokeylist(void) {
	char buf[BUFSZ], buf2[BUFSZ];
	uchar key;
	boolean keys_used[256] = {0};
	const char *dir_keys;
	winid datawin;
	int i;
	char *dir_desc[10] = {"move west",
			      "move northwest",
			      "move north",
			      "move northeast",
			      "move east",
			      "move southeast",
			      "move south",
			      "move southwest",
			      "move downward",
			      "move upward"};
	char *misc_desc[MISC_CMD_COUNT] = {
		"rush until something interesting is seen",
		"run until something extremely interesting is seen",
		"fight even if you don't see a monster",
		"move without picking up objects/fighting",
		"run without picking up objects/fighting",
		"escape from the current query/action",
		"redo the previous command"};

	datawin = create_nhwindow(NHW_TEXT);
	putstr(datawin, 0, "");
	putstr(datawin, 0, "            Full Current Key Bindings List");
	putstr(datawin, 0, "");

	/* directional keys */
	if (iflags.num_pad)
		dir_keys = ndir;
	else
		dir_keys = sdir;
	putstr(datawin, 0, "Directional keys:");
	for (i = 0; i < 10; i++) {
		key = dir_keys[i];
		keys_used[key] = true;
		if (!iflags.num_pad) {
			keys_used[toupper(key)] = true;
			keys_used[C(key)] = true;
		}
		sprintf(buf, "%c\t%s", key, dir_desc[i]);
		putstr(datawin, 0, buf);
	}
	if (!iflags.num_pad) {
		putstr(datawin, 0, "Shift-<direction> will move in specified direction until you hit");
		putstr(datawin, 0, "        a wall or run into something.");
		putstr(datawin, 0, "Ctrl-<direction> will run in specified direction until something");
		putstr(datawin, 0, "        very interesting is seen.");
	}
	putstr(datawin, 0, "");

	/* special keys -- theoretically modifiable but many are still hard-coded*/
	putstr(datawin, 0, "Miscellaneous keys:");
	for (i = 0; i < MISC_CMD_COUNT; i++) {
		key = misc_cmds[i];
		keys_used[key] = true;
		sprintf(buf, "%s\t%s", key2txt(key, buf2), misc_desc[i]);
		putstr(datawin, 0, buf);
	}
	putstr(datawin, 0, "");

	/* more special keys -- all hard-coded */
#ifndef NO_SIGNAL
	putstr(datawin, 0, "^c\tbreak out of nethack (SIGINT)");
	keys_used[C('c')] = true;
	if (!iflags.num_pad) putstr(datawin, 0, "");
#endif
	if (iflags.num_pad) {
		putstr(datawin, 0, "-\tforce fight (same as above)");
		putstr(datawin, 0, "5\trun (same as above)");
		putstr(datawin, 0, "0\tinventory (as #inventory)");
		keys_used['-'] = keys_used['5'] = keys_used['0'] = true;
		putstr(datawin, 0, "");
	}

	/* command keys - can be rebound or remapped*/
	putstr(datawin, 0, "Command keys:");
	for (i = 0; i <= 255; i++) {
		struct ext_func_tab *extcmd;
		char *mapping;
		key = i;
		/* JDS: not the most efficient way, perhaps */
		if (keys_used[i]) continue;
		if (key == ' ' && !flags.rest_on_space) continue;
		if ((extcmd = cmdlist[i].bind_cmd)) {
			sprintf(buf, "%s\t%s", key2txt(key, buf2),
				extcmd->ef_desc);
			putstr(datawin, 0, buf);
		} else if ((mapping = cmdlist[i].map_list)) {
			sprintf(buf, "%s\tmapped to ",
				key2txt(key, buf2));
			str2txt(mapping, eos(buf));
			putstr(datawin, 0, buf);
		}
	}
	putstr(datawin, 0, "");

	display_nhwindow(datawin, false);
	destroy_nhwindow(datawin);
}

bool is_redraw_cmd(char c) {
	return c == C('r') || (iflags.num_pad && c == C('l'));
}

static const char template[] = "%-18s %4ld  %6ld";
static const char count_str[] = "                   count  bytes";
static const char separator[] = "------------------ -----  ------";

static usize size_monst(struct monst *mtmp) {
	usize sz = sizeof(struct monst);

	if (mtmp->mextra) {
		if (MNAME(mtmp)) sz += strlen(MNAME(mtmp))+1;
		if (EGD(mtmp)) sz += sizeof(struct egd);
		if (EPRI(mtmp)) sz += sizeof(struct epri);
		if (ESHK(mtmp)) sz += sizeof(struct eshk);
		if (EMIN(mtmp)) sz += sizeof(struct emin);
		if (EDOG(mtmp)) sz += sizeof(struct edog);
		if (EGYP(mtmp)) sz += sizeof(struct egyp);
	}
	return sz;
}


static void count_obj(struct obj *chain, long *total_count, long *total_size, boolean top, boolean recurse) {
	long count, size;
	struct obj *obj;

	for (count = size = 0, obj = chain; obj; obj = obj->nobj) {
		if (top) {
			count++;
			size += sizeof(struct obj) + obj->oxlth + obj->onamelth;
		}
		if (recurse && obj->cobj)
			count_obj(obj->cobj, total_count, total_size, true, true);
	}
	*total_count += count;
	*total_size += size;
}

static void obj_chain(winid win, const char *src, struct obj *chain, long *total_count, long *total_size) {
	char buf[BUFSZ];
	long count = 0, size = 0;

	count_obj(chain, &count, &size, true, false);
	*total_count += count;
	*total_size += size;
	sprintf(buf, template, src, count, size);
	putstr(win, 0, buf);
}

static void mon_invent_chain(winid win, const char *src, struct monst *chain, long *total_count, long *total_size) {
	char buf[BUFSZ];
	long count = 0, size = 0;
	struct monst *mon;

	for (mon = chain; mon; mon = mon->nmon)
		count_obj(mon->minvent, &count, &size, true, false);
	*total_count += count;
	*total_size += size;
	sprintf(buf, template, src, count, size);
	putstr(win, 0, buf);
}

static void contained(winid win, const char *src, long *total_count, long *total_size) {
	char buf[BUFSZ];
	long count = 0, size = 0;
	struct monst *mon;

	count_obj(invent, &count, &size, false, true);
	count_obj(fobj, &count, &size, false, true);
	count_obj(level.buriedobjlist, &count, &size, false, true);
	count_obj(migrating_objs, &count, &size, false, true);
	/* DEADMONSTER check not required in this loop since they have no inventory */
	for (mon = fmon; mon; mon = mon->nmon)
		count_obj(mon->minvent, &count, &size, false, true);
	for (mon = migrating_mons; mon; mon = mon->nmon)
		count_obj(mon->minvent, &count, &size, false, true);

	*total_count += count;
	*total_size += size;

	sprintf(buf, template, src, count, size);
	putstr(win, 0, buf);
}

static void mon_chain(winid win, const char *src, struct monst *chain, long *total_count, long *total_size) {
	char buf[BUFSZ];
	long count, size;
	struct monst *mon;

	for (count = size = 0, mon = chain; mon; mon = mon->nmon) {
		count++;
		size += size_monst(mon);
	}
	*total_count += count;
	*total_size += size;
	sprintf(buf, template, src, count, size);
	putstr(win, 0, buf);
}

/*
 * Display memory usage of all monsters and objects on the level.
 */
static int wiz_show_stats(void) {
	char buf[BUFSZ];
	winid win;
	long total_obj_size = 0, total_obj_count = 0;
	long total_mon_size = 0, total_mon_count = 0;

	win = create_nhwindow(NHW_TEXT);
	putstr(win, 0, "Current memory statistics:");
	putstr(win, 0, "");
	sprintf(buf, "Objects, size %d", (int)sizeof(struct obj));
	putstr(win, 0, buf);
	putstr(win, 0, "");
	putstr(win, 0, count_str);

	obj_chain(win, "invent", invent, &total_obj_count, &total_obj_size);
	obj_chain(win, "fobj", fobj, &total_obj_count, &total_obj_size);
	obj_chain(win, "buried", level.buriedobjlist,
		  &total_obj_count, &total_obj_size);
	obj_chain(win, "migrating obj", migrating_objs,
		  &total_obj_count, &total_obj_size);
	mon_invent_chain(win, "minvent", fmon,
			 &total_obj_count, &total_obj_size);
	mon_invent_chain(win, "migrating minvent", migrating_mons,
			 &total_obj_count, &total_obj_size);

	contained(win, "contained",
		  &total_obj_count, &total_obj_size);

	putstr(win, 0, separator);
	sprintf(buf, template, "Total", total_obj_count, total_obj_size);
	putstr(win, 0, buf);

	putstr(win, 0, "");
	putstr(win, 0, "");
	sprintf(buf, "Monsters, size %d", (int)sizeof(struct monst));
	putstr(win, 0, buf);
	putstr(win, 0, "");

	mon_chain(win, "fmon", fmon,
		  &total_mon_count, &total_mon_size);
	mon_chain(win, "migrating", migrating_mons,
		  &total_mon_count, &total_mon_size);

	putstr(win, 0, separator);
	sprintf(buf, template, "Total", total_mon_count, total_mon_size);
	putstr(win, 0, buf);

	display_nhwindow(win, false);
	destroy_nhwindow(win);
	return 0;
}

void sanity_check(void) {
	obj_sanity_check();
	timer_sanity_check();
}

/*
 * Detail contents of each display layer at specified location(s).
 */
static int wiz_show_display(void) {
	int ans, glyph;
	coord cc;
	winid win;
	char buf[BUFSZ];
	struct rm *lev;

	cc.x = u.ux;
	cc.y = u.uy;
	pline("Pick a location.");
	ans = getpos(&cc, false, "a location of interest");
	if (ans < 0 || cc.x < 0)
		return 0; /* done */
	lev = &levl[cc.x][cc.y];
	win = create_nhwindow(NHW_MENU);
	sprintf(buf, "Contents of hero's memory at (%d, %d):", cc.x, cc.y);
	putstr(win, 0, buf);
	putstr(win, 0, "");
	sprintf(buf, "Invisible monster: %s",
		lev->mem_invis ? "present" : "none");
	putstr(win, 0, buf);
	if (lev->mem_obj && lev->mem_corpse)
		if (mons[lev->mem_obj - 1].geno & G_UNIQ)
			sprintf(buf, "Object: %s%s corpse",
				type_is_pname(&mons[lev->mem_obj - 1]) ? "" : "the ",
				s_suffix(mons[lev->mem_obj - 1].mname));
		else
			sprintf(buf, "Object: %s corpse", mons[lev->mem_obj - 1].mname);
	else
		sprintf(buf, "Object: %s", lev->mem_obj ? obj_typename(lev->mem_obj - 1) : "none");
	putstr(win, 0, buf);
	sprintf(buf, "Trap: %s", lev->mem_trap ? sym_desc[trap_to_defsym(lev->mem_trap)].explanation : "none");
	putstr(win, 0, buf);
	sprintf(buf, "Backgroud: %s", sym_desc[lev->mem_bg].explanation);
	putstr(win, 0, buf);
	putstr(win, 0, "");
	glyph = glyph_at(cc.x, cc.y);
	sprintf(buf, "Buffered (3rd screen): ");
	if (glyph_is_monster(glyph)) {
		strcat(buf, mons[glyph_to_mon(glyph)].mname);
		if (glyph_is_pet(glyph))
			strcat(buf, " (tame)");
		if (glyph_is_ridden_monster(glyph))
			strcat(buf, " (ridden)");
		if (glyph_is_detected_monster(glyph))
			strcat(buf, " (detected)");
	} else if (glyph_is_object(glyph)) {
		if (glyph_is_body(glyph)) {
			int corpse = glyph_to_body(glyph);
			if (mons[corpse].geno & G_UNIQ)
				sprintf(eos(buf), "%s%s corpse",
					type_is_pname(&mons[corpse]) ? "" : "the ",
					s_suffix(mons[corpse].mname));
			else
				sprintf(eos(buf), "%s corpse", mons[corpse].mname);
		} else
			strcat(buf, obj_typename(glyph_to_obj(glyph)));
	} else if (glyph_is_invisible(glyph))
		strcat(buf, "invisible monster");
	else if (glyph_is_cmap(glyph))
		strcat(buf, sym_desc[glyph_to_cmap(glyph)].explanation);
	else
		sprintf(eos(buf), "[%d]", glyph);
	putstr(win, 0, buf);
	display_nhwindow(win, false);
	destroy_nhwindow(win);
	return 0;
}

#ifdef DEBUG_MIGRATING_MONS
static int wiz_migrate_mons(void) {
	int mcount = 0;
	char inbuf[BUFSZ];
	struct permonst *ptr;
	struct monst *mtmp;
	d_level tolevel;
	getlin("How many random monsters to migrate? [0]", inbuf);
	if (*inbuf == '\033') return 0;
	mcount = atoi(inbuf);
	if (mcount < 0 || mcount > (COLNO * ROWNO) || Is_botlevel(&u.uz))
		return 0;
	while (mcount > 0) {
		if (Is_stronghold(&u.uz))
			assign_level(&tolevel, &valley_level);
		else
			get_level(&tolevel, depth(&u.uz) + 1);
		ptr = rndmonst();
		mtmp = makemon(ptr, 0, 0, NO_MM_FLAGS);
		if (mtmp) migrate_to_level(mtmp, ledger_no(&tolevel),
					   MIGR_RANDOM, NULL);
		mcount--;
	}
	return 0;
}
#endif

/* a wrapper function for strcmp.  Can this be done more simply? */
static int compare_commands(const void *_cmd1, const void *_cmd2) {
	const struct ext_func_tab *cmd1 = _cmd1, *cmd2 = _cmd2;

	return strcmp(cmd1->ef_txt, cmd2->ef_txt);
}

void commands_init(void) {
	int count = 0;

	while (extcmdlist[count].ef_txt)
		count++;

	qsort(extcmdlist, count, sizeof(struct ext_func_tab), &compare_commands);

	if (wizard) add_debug_extended_commands();
	init_bind_list();   /* initialize all keyboard commands */
	change_bind_list(); /* change keyboard commands based on options */
	verify_key_list();
}

/* returns a one-byte character from the text (it may massacre the txt
 * buffer) */
char txt2key(char *txt) {
	txt = stripspace(txt);
	if (!*txt) return 0;

	/* simple character */
	if (!txt[1]) return txt[0];

	/* a few special entries */
	if (!strcmp(txt, "<enter>")) return '\n';
	if (!strcmp(txt, "<space>")) return ' ';
	if (!strcmp(txt, "<esc>")) return '\033';

	/* control and meta keys */
	switch (*txt) {
		case 'm': /* can be mx, Mx, m-x, M-x */
		case 'M':
			txt++;
			if (*txt == '-' && txt[1]) txt++;
			if (txt[1]) return 0;
			return M(*txt);
		case 'c': /* can be cx, Cx, ^x, c-x, C-x, ^-x */
		case 'C':
		case '^':
			txt++;
			if (*txt == '-' && txt[1]) txt++;
			if (txt[1]) return 0;
			return C(*txt);
	}

	/* ascii codes: must be three-digit decimal */
	if (*txt >= '0' && *txt <= '9') {
		uchar key = 0;
		int i;
		for (i = 0; i < 3; i++) {
			if (txt[i] < '0' || txt[i] > '9') return 0;
			key = 10 * key + txt[i] - '0';
		}
		return key;
	}

	return 0;
}

/* returns the text for a one-byte encoding
 * must be shorter than a tab for proper formatting */
char *key2txt(char c, char *txt /* sufficiently long buffer */) {
	if (c == ' ')
		sprintf(txt, "<space>");
	else if (c == '\033')
		sprintf(txt, "<esc>");
	else if (c == '\n')
		sprintf(txt, "<enter>");
	else if (ISCTRL(c))
		sprintf(txt, "^%c", UNCTRL(c));
	else if (ISMETA(c))
		sprintf(txt, "M-%c", UNMETA(c));
	else if (c >= 33 && c <= 126)
		sprintf(txt, "%c", c); /* regular keys: ! through ~ */
	else
		sprintf(txt, "A-%i", c); /* arbitrary ascii combinations */
	return txt;
}

/* returns the text for a string of one-byte encodings */
char *str2txt(char *s, char *txt) {
	char *buf = txt;

	while (*s) {
		key2txt(*s, buf);
		buf = eos(buf);
		*buf = ' ';
		buf++;
		*buf = 0;
		s++;
	}
	return txt;
}

/* strips leading and trailing whitespace */
char *stripspace(char *txt) {
	char *end;
	while (isspace(*txt))
		txt++;
	end = eos(txt);
	while (--end >= txt && isspace(*end))
		*end = 0;
	return txt;
}

/* closely follows parseoptions in options.c */
void parsebindings(char *bindings) {
	char *bind;
	char key;
	struct binding_list_tab *newbinding = NULL;

	/* break off first binding from the rest; parse the rest */
	if ((bind = index(bindings, ',')) != 0) {
		*bind++ = 0;
		parsebindings(bind);
	}

	/* parse a single binding: first split around : */
	if (!(bind = index(bindings, ':'))) return; /* it's not a binding */
	*bind++ = 0;

	/* read the key to be bound */
	key = txt2key(bindings);
	if (!key) {
		raw_printf("Bad binding %s.", bindings);
		wait_synch();
		return;
	}

	/* JDS: crappy hack because wizard mode information
	 * isn't read until _after_ key bindings are read,
	 * and to change this would cause numerous side effects.
	 * instead, I save a list of rebindings, which are later
	 * bound. */
	bind = stripspace(bind);
	newbinding = alloc(sizeof(*newbinding));
	newbinding->key = key;
	newbinding->extcmd = alloc(strlen(bind) + 1);
	strcpy(newbinding->extcmd, bind);
	newbinding->next = bindinglist;
	bindinglist = newbinding;
}

// closesly follows parsebindings and parseoptions
void parseautocomplete(char *autocomplete, boolean condition) {
	char *autoc;
	int i;

	/* break off first autocomplete from the rest; parse the rest */
	if ((autoc = index(autocomplete, ',')) || (autoc = index(autocomplete, ':'))) {
		*autoc++ = 0;
		parseautocomplete(autoc, condition);
	}

	/* strip leading and trailing white space */
	autocomplete = stripspace(autocomplete);

	if (!*autocomplete) return;

	/* take off negations */
	while (*autocomplete == '!') {
		/* unlike most options, a leading "no" might actually be a part of
		 * the extended command.  Thus you have to use ! */
		autocomplete++;
		condition = !condition;
	}

	/* find and modify the extended command */
	/* JDS: could be much faster [O(log n) vs O(n)] if done differently */
	for (i = 0; extcmdlist[i].ef_txt && i < SIZE(extcmdlist); i++) {
		if (strcmp(autocomplete, extcmdlist[i].ef_txt)) continue;
		extcmdlist[i].autocomplete = condition;
		return;
	}

	/* do the exact same thing with the wizmode list */
	/* this is a hack because wizard-mode commands haven't been loaded yet when
	 * this code is run.  See "crappy hack" elsewhere. */
	for (i = 0; debug_extcmdlist[i].ef_txt && i < SIZE(debug_extcmdlist); i++) {
		if (strcmp(autocomplete, debug_extcmdlist[i].ef_txt)) continue;
		debug_extcmdlist[i].autocomplete = condition;
		return;
	}

	/* not a real extended command */
	raw_printf("Bad autocomplete: invalid extended command '%s'.", autocomplete);
	wait_synch();
}
char randomkey(void) {
	switch (rn2(12)) {
		default:
			return '\033';
		case 0:
			return '\n';
		case 1:
		case 2:
		case 3:
		case 4:
			return ' ' + rn2('~' - ' ');
		case 5:
			return '\t';
		case 6:
			return 'a' + rn2('z' - 'a');
		case 7:
			return 'A' + rn2('Z' - 'A');
		case 8: {
			// bindinglist is a *temporary* list that's used to
			// construct cmdlist; if it's still around, then
			// cmdlist isn't so try again.
			if (bindinglist) return randomkey();

			while (true) {
				// randomly select a cmd; if it's prayer, accept it only with 1% probability
				uchar k;
				if (cmdlist[k = rn2(255)].bind_cmd) {
					// prayer tends to summon a bunch of couatls which get in the way
					// bot tends to get stuck in extcmd lists and those don't matter particularly
					// options just don't particularly matter
					if (cmdlist[k].bind_cmd->ef_funct == dopray || cmdlist[k].bind_cmd->ef_funct == extcmd_via_menu || cmdlist[k].bind_cmd->ef_funct == doset) {
						if (!rn2(800)) continue;
					}

					// just not that useful, likely to be cancelled anyway, and bot has a tendency to turn on extcmd
					if (cmdlist[k].bind_cmd->ef_funct == get_ext_cmd) {
						if (!rn2(100)) continue;
					}

					return k;
				}
			}
		}
		case 9:
			return '#';
	}
}

// closely follows parsebindings and parseoptions
void parsemappings(char *mapping) {
	char *map;
	unsigned char key;
	unsigned char map_to[BUFSZ] = "";
	int cnt;
	struct key_tab *keytab;

	/* break off first mapping from the rest; parse the rest */
	if ((map = index(mapping, ','))) {
		*map++ = 0;
		parsemappings(map);
	}

	/* parse a single mapping: first split around : */
	if (!(map = index(mapping, ':'))) return; /* it's not a mapping */
	*map++ = 0;

	/* read the key to be mapped */
	key = txt2key(mapping);
	if (!key) {
		raw_printf("Bad mapping for key %s.", mapping);
		wait_synch();
		return;
	}

	/* read the mapping list */
	cnt = get_uchar_list(map, map_to, BUFSZ - 1);
	if (cnt < 0) {
		cnt = -cnt - 1;
		raw_printf("Bad mapping for key %s - %i at %s",
			   mapping, cnt, map);
		wait_synch();
		return;
	}
	if (cnt == 0) {
		raw_printf("You can't map %s to nothing (%s).",
			   mapping, map);
		wait_synch();
		return;
	}
	map_to[cnt] = 0;

	/* store in cmdlist */
	keytab = &(cmdlist[key]);
	if (keytab->map_list != NULL) free(keytab->map_list);
	keytab->map_list = alloc(cnt + 1);
	strcpy(keytab->map_list, (char *)map_to);
}

void rhack(char *cmd) {
	bool do_walk, do_rush, prefix_seen, bad_command,
	     firsttime = (cmd == 0);

	iflags.menu_requested = false;
	if (firsttime) {
		context.nopick = 0;
		cmd = parse();
	}
	if (*cmd == DOESCAPE) { /* <esc> key - user might be panicking */
		/* Bring up the menu */
		if (multi || !flags.menu_on_esc || !(domenusystem())) {
			context.move = false;
			multi = 0;
		}
		return;
	}
	if (*cmd == DOAGAIN && !in_doagain && saveq[0]) {
		in_doagain = true;
		stail = 0;
		rhack(NULL); /* read and execute command */
		in_doagain = false;
		return;
	}
	/* Special case of *cmd == ' ' handled better below */
	if (!*cmd || *cmd == (char)0377) {
		nhbell();
		context.move = false;
		return; /* probably we just had an interrupt */
	}
	if (iflags.num_pad && iflags.num_pad_mode == 1) {
		/* This handles very old inconsistent DOS/Windows behaviour
		 * in a new way: earlier, the keyboard handler mapped these,
		 * which caused counts to be strange when entered from the
		 * number pad. Now do not map them until here.
		 */
		switch (*cmd) {
			case '5': *cmd = 'g'; break;
			case M('5'): *cmd = 'G'; break;
			case M('0'): *cmd = 'I'; break;
		}
	}
	/* handle most movement commands */
	do_walk = do_rush = prefix_seen = false;
	context.travel = context.travel1 = 0;

	switch (*cmd) {
		case DORUSH:
			if (movecmd(cmd[1])) {
				context.run = 2;
				do_rush = true;
			} else {
				prefix_seen = true;
			}
			break;

		case '5': if (!iflags.num_pad) break; // else fallthru
		case DORUN:
			if (movecmd(lowc(cmd[1]))) {
				    context.run = 3;
				    do_rush = true;
			} else {
				prefix_seen = true;
			}
			break;

		case '-': if (!iflags.num_pad) break; // else fallthru
		/* Effects of movement commands and invisible monsters:
		 * m: always move onto space (even if 'I' remembered)
		 * F: always attack space (even if 'I' not remembered)
		 * normal movement: attack if 'I', move otherwise
		 */
		case DOFORCEFIGHT:
			if (movecmd(cmd[1])) {
				context.forcefight = 1;
				do_walk = true;
			} else {
				prefix_seen = true;
			}
			break;

		case DONOPICKUP:
			if (movecmd(cmd[1]) || u.dz) {
				context.run = 0;
				context.nopick = 1;
				if (!u.dz) {
					do_walk = true;
				} else {
					cmd[0] = cmd[1]; /* "m<" or "m>" */
				}
			} else {
				prefix_seen = true;
			}
			break;

		case DORUN_NOPICKUP:
			if (movecmd(lowc(cmd[1]))) {
				context.run = 1;
				context.nopick = 1;
				do_rush = true;
			} else {
				prefix_seen = true;
			}
			break;

		case '0':
			if (!iflags.num_pad) break;
			ddoinv(); /* a convenience borrowed from the PC */
			context.move = false;
			multi = 0;
			break;

		case CMD_CLICKLOOK:
			if (iflags.clicklook) {
				context.move = false;
				do_look(2, &clicklook_cc);
			}
			return;

		case CMD_TRAVEL:
			context.travel = 1;
			context.travel1 = 1;
			context.run = 8;
			context.nopick = 1;
			do_rush = true;
			break;
		default:
			/* ordinary movement */
			if (movecmd(*cmd)) {
				do_walk = true;
			} else if (movecmd(iflags.num_pad ?  UNMETA(*cmd) : lowc(*cmd))) {
				context.run = 1;
				do_rush = true;
			} else if (movecmd(UNCTRL(*cmd))) {
				context.run = 3;
				do_rush = true;
			}
	}

	/* some special prefix handling */
	/* overload 'm' prefix for ',' to mean "request a menu" */
	if (prefix_seen && cmd[1] == ',') {
		iflags.menu_requested = true;
		++cmd;
	}

	if (do_walk) {
		if (multi) context.mv = true;
		domove();
		context.forcefight = 0;
		return;
	} else if (do_rush) {
		if (firsttime) {
			if (!multi) multi = max(COLNO, ROWNO);
			u.last_str_turn = 0;
		}
		context.mv = true;
		domove();
		return;
	} else if (prefix_seen && cmd[1] == DOESCAPE) { /* <prefix><escape> */
		/* don't report "unknown command" for change of heart... */
		bad_command = false;
	} else if (*cmd == ' ' && !flags.rest_on_space) {
		bad_command = true; /* skip cmdlist[] loop */
				    /* handle bound/mapped commands */
	} else {
		const struct key_tab *keytab = &cmdlist[(unsigned char)*cmd];

		if (keytab->bind_cmd != NULL) {
			struct ext_func_tab *extcmd = keytab->bind_cmd;
			int res;
			int (*func)(void);

			if (iflags.debug_fuzzer && extcmd->disallowed_if_fuzzing) {
				return;
			}

			if (u.uburied && !extcmd->can_if_buried) {
				pline("You can't do that while you are buried!");
				res = 0;
			} else {
				func = extcmd->ef_funct;
				if (extcmd->f_text && !occupation && multi)
					set_occupation(func, extcmd->f_text, multi);

				res = (*func)(); /* perform the command */
			}
			if (!res) {
				context.move = false;
				multi = 0;
			}
			return;
		}
		if (keytab->map_list != NULL) {
			char buf[BUFSZ];
			char *mapping = keytab->map_list;
			pline("Mapping char %s.", key2txt(*cmd, buf));
			while (*mapping) {
				pline("Mapping to %s.", key2txt(*mapping, buf));
				addchar(*mapping);
				mapping++;
			}
			context.move = false;
			return;
		}

		/* if we reach here, cmd wasn't found in cmdlist[] */
		bad_command = true;
	}
	if (bad_command) {
		char expcmd[10];
		char *cp = expcmd;

		while (*cmd && (int)(cp - expcmd) < (int)(sizeof expcmd - 3)) {
			if (*cmd >= 040 && *cmd < 0177) {
				*cp++ = *cmd++;
			} else if (*cmd & 0200) {
				*cp++ = 'M';
				*cp++ = '-';
				*cp++ = *cmd++ &= ~0200;
			} else {
				*cp++ = '^';
				*cp++ = *cmd++ ^ 0100;
			}
		}
		*cp = '\0';
		if (!prefix_seen || !iflags.cmdassist ||
		    !help_dir(0, "Invalid direction key!"))
			Norep("Unknown command '%s'.", expcmd);
	}
	/* didn't move */
	context.move = false;
	multi = 0;
	return;
}

// convert an x,y pair into a direction code
int xytod(schar x, schar y) {
	int dd;

	for (dd = 0; dd < 8; dd++)
		if (x == xdir[dd] && y == ydir[dd]) return dd;

	return -1;
}

// convert a direction code into an x,y pair */
void dtoxy(coord *cc, int dd) {
	cc->x = xdir[dd];
	cc->y = ydir[dd];
	return;
}

// also sets u.dz, but returns false for <> */
int movecmd(char sym) {
	const char *dp;
	const char *sdp;
	if (iflags.num_pad)
		sdp = ndir;
	else
		sdp = sdir; /* DICE workaround */

	u.dz = 0;
	if (!(dp = index(sdp, sym))) return 0;
	u.dx = xdir[dp - sdp];
	u.dy = ydir[dp - sdp];
	u.dz = zdir[dp - sdp];
	if (u.dx && u.dy && u.umonnum == PM_GRID_BUG) {
		u.dx = u.dy = 0;
		return 0;
	}
	return !u.dz;
}

/*
 * uses getdir() but unlike getdir() it specifically
 * produces coordinates using the direction from getdir()
 * and verifies that those coordinates are ok.
 *
 * If the call to getdir() returns 0, "Never mind." is displayed.
 * If the resulting coordinates are not okay, emsg is displayed.
 *
 * Returns non-zero if coordinates in cc are valid.
 */
int get_adjacent_loc(const char *prompt, const char *emsg, xchar x, xchar y, coord *cc) {
	xchar new_x, new_y;
	if (!getdir(prompt)) {
		pline("%s", "Never mind.");
		return 0;
	}
	new_x = x + u.dx;
	new_y = y + u.dy;
	if (cc && isok(new_x, new_y)) {
		cc->x = new_x;
		cc->y = new_y;
	} else {
		if (emsg) pline("%s", emsg);
		return 0;
	}
	return 1;
}

int getdir(const char *s) {
	char dirsym;
	/* WAC add dirsymbols to generic prompt */
	char buf[BUFSZ];

	sprintf(buf, "In what direction? [%s]",
		(iflags.num_pad ? ndir : sdir));

retry:
	if (in_doagain || *readchar_queue) {
		dirsym = readchar();
	} else {
		dirsym = yn_function((s && *s != '^') ? s : buf, NULL, '\0');
	}

	// remove the prompt string so caller won't have to
	clear_nhwindow(WIN_MESSAGE);

	if (is_redraw_cmd(dirsym)) {
		docrt();
		goto retry;
	}

	savech(dirsym);

	if (dirsym == '.' || dirsym == 's') {
		u.dx = u.dy = u.dz = 0;
	} else if (!movecmd(dirsym) && !u.dz) {
		if (!index(quitchars, dirsym)) {
			bool did_help = false, help_requested = (dirsym == '?');
			if (help_requested || iflags.cmdassist) {
				did_help = help_dir((s && *s == '^') ? dirsym : 0, help_requested ? NULL : "Invalid direction key!");
			}
			if (!did_help) pline("What a strange direction!");
			if (help_requested) goto retry;
		}
		return 0;
	}
	if (!u.dz && (Stunned || (Confusion && !rn2(5)))) confdir();
	return 1;
}

static bool help_dir(char sym, const char *msg) {
	char ctrl;
	winid win;
	static const char wiz_only_list[] = "EFGIOVW";
	char buf[BUFSZ], buf2[BUFSZ], *expln;

	win = create_nhwindow(NHW_TEXT);
	if (!win) return false;
	if (msg) {
		sprintf(buf, "cmdassist: %s", msg);
		putstr(win, 0, buf);
		putstr(win, 0, "");
	}
	if (letter(sym)) {
		sym = highc(sym);
		ctrl = (sym - 'A') + 1;
		if ((expln = dowhatdoes_core(ctrl, buf2)) && (!index(wiz_only_list, sym) || wizard)) {
			sprintf(buf, "Are you trying to use ^%c%s?", sym,
				index(wiz_only_list, sym) ? "" :
							    " as specified in the Guidebook");
			putstr(win, 0, buf);
			putstr(win, 0, "");
			putstr(win, 0, expln);
			putstr(win, 0, "");
			putstr(win, 0, "To use that command, you press");
			sprintf(buf,
				"the <Ctrl> key, and the <%c> key at the same time.", sym);
			putstr(win, 0, buf);
			putstr(win, 0, "");
		}
	}
	if (iflags.num_pad && u.umonnum == PM_GRID_BUG) {
		putstr(win, 0, "Valid direction keys in your current form (with number_pad on) are:");
		putstr(win, 0, "             8   ");
		putstr(win, 0, "             |   ");
		putstr(win, 0, "          4- . -6");
		putstr(win, 0, "             |   ");
		putstr(win, 0, "             2   ");
	} else if (u.umonnum == PM_GRID_BUG) {
		putstr(win, 0, "Valid direction keys in your current form are:");
		putstr(win, 0, "             k   ");
		putstr(win, 0, "             |   ");
		putstr(win, 0, "          h- . -l");
		putstr(win, 0, "             |   ");
		putstr(win, 0, "             j   ");
	} else if (iflags.num_pad) {
		putstr(win, 0, "Valid direction keys (with number_pad on) are:");
		putstr(win, 0, "          7  8  9");
		putstr(win, 0, "           \\ | / ");
		putstr(win, 0, "          4- . -6");
		putstr(win, 0, "           / | \\ ");
		putstr(win, 0, "          1  2  3");
	} else {
		putstr(win, 0, "Valid direction keys are:");
		putstr(win, 0, "          y  k  u");
		putstr(win, 0, "           \\ | / ");
		putstr(win, 0, "          h- . -l");
		putstr(win, 0, "           / | \\ ");
		putstr(win, 0, "          b  j  n");
	};
	putstr(win, 0, "");
	putstr(win, 0, "          <  up");
	putstr(win, 0, "          >  down");
	putstr(win, 0, "          .  direct at yourself");

	// null message means this was an explicit user request, so they don't
	// need to be told how to disable it.
	if (msg) {
		putstr(win, 0, "");
		putstr(win, 0, "(Suppress this message with !cmdassist in config file.)");
	}
	display_nhwindow(win, false);
	destroy_nhwindow(win);
	return true;
}

void confdir(void) {
	int x = (u.umonnum == PM_GRID_BUG) ? 2 * rn2(4) : rn2(8);
	u.dx = xdir[x];
	u.dy = ydir[x];
	return;
}

bool isok(int x, int y) {
	/* x corresponds to curx, so x==1 is the first column. Ach. %% */
	return x >= 1 && x <= COLNO - 1 && y >= 0 && y <= ROWNO - 1;
}

static int last_multi;

/*
 * convert a MAP window position into a movecmd
 */
const char *click_to_cmd(int x, int y, int mod) {
	int dir;
	static char cmd[4];
	cmd[1] = 0;

	if (iflags.clicklook && (mod == CLICK_2)) {
		clicklook_cc.x = x;
		clicklook_cc.y = y;
		cmd[0] = CMD_CLICKLOOK;
		return cmd;
	}

	x -= u.ux;
	y -= u.uy;

	if (abs(x) <= 1 && abs(y) <= 1) {
		x = sgn(x), y = sgn(y);
	} else {
		u.tx = u.ux + x;
		u.ty = u.uy + y;
		cmd[0] = CMD_TRAVEL;
		return cmd;
	}

	if (x == 0 && y == 0) {
		/* here */
		if (IS_FOUNTAIN(levl[u.ux][u.uy].typ) || IS_SINK(levl[u.ux][u.uy].typ)) {
			cmd[0] = mod == CLICK_1 ? 'q' : M('d');
			return cmd;
		} else if (IS_THRONE(levl[u.ux][u.uy].typ)) {
			cmd[0] = M('s');
			return cmd;
		} else if ((u.ux == xupstair && u.uy == yupstair) || (u.ux == sstairs.sx && u.uy == sstairs.sy && sstairs.up) || (u.ux == xupladder && u.uy == yupladder)) {
			return "<";
		} else if ((u.ux == xdnstair && u.uy == ydnstair) || (u.ux == sstairs.sx && u.uy == sstairs.sy && !sstairs.up) || (u.ux == xdnladder && u.uy == ydnladder)) {
			return ">";
		} else if (OBJ_AT(u.ux, u.uy)) {
			cmd[0] = Is_container(level.objects[u.ux][u.uy]) ? M('l') : ',';
			return cmd;
		} else {
			return "."; /* just rest */
		}
	}

	/* directional commands */

	dir = xytod(x, y);

	if (!m_at(u.ux + x, u.uy + y) && !test_move(u.ux, u.uy, x, y, TEST_MOVE)) {
		cmd[1] = (iflags.num_pad ? ndir[dir] : sdir[dir]);
		cmd[2] = 0;
		if (IS_DOOR(levl[u.ux + x][u.uy + y].typ)) {
			/* slight assistance to the player: choose kick/open for them */
			if (levl[u.ux + x][u.uy + y].doormask & D_LOCKED) {
				cmd[0] = C('d');
				return cmd;
			}
			if (levl[u.ux + x][u.uy + y].doormask & D_CLOSED) {
				cmd[0] = 'o';
				return cmd;
			}
		}
		if (levl[u.ux + x][u.uy + y].typ <= SCORR) {
			cmd[0] = 's';
			cmd[1] = 0;
			return cmd;
		}
	}

	/* move, attack, etc. */
	cmd[1] = 0;
	if (mod == CLICK_1) {
		cmd[0] = (iflags.num_pad ? ndir[dir] : sdir[dir]);
	} else {
		cmd[0] = (iflags.num_pad ? M(ndir[dir]) :
					   (sdir[dir] - 'a' + 'A')); /* run command */
	}

	return cmd;
}

static char *parse(void) {
	static char in_line[COLNO];
	int foo;
	static char repeat_char;
	boolean prezero = false;

	multi = 0;
	context.move = 1;
	flush_screen(1); /* Flush screen buffer. Put the cursor on the hero. */

	/* [Tom] for those who occasionally go insane... */
	if (repeat_hit) {
		/* Sanity checks for repeat_hit */
		if (repeat_hit < 0)
			repeat_hit = 0;
		else {
			/* Don't want things to get too out of hand */
			if (repeat_hit > 10) repeat_hit = 10;

			repeat_hit--;
			in_line[0] = repeat_char;
			in_line[1] = 0;
			return in_line;
		}
	}

	if (!iflags.num_pad || (foo = readchar()) == 'n')
		for (;;) {
			foo = readchar();
			if (foo >= '0' && foo <= '9') {
				multi = 10 * multi + foo - '0';
				if (multi < 0 || multi >= LARGEST_INT) multi = LARGEST_INT;
				if (multi > 9) {
					clear_nhwindow(WIN_MESSAGE);
					sprintf(in_line, "Count: %d", multi);
					pline("%s", in_line);
					mark_synch();
				}
				last_multi = multi;
				if (!multi && foo == '0') prezero = true;
			} else
				break; /* not a digit */
		}

	if (foo == DOESCAPE) { /* esc cancels count (TH) */
		clear_nhwindow(WIN_MESSAGE);
		/* multi = */ last_multi = 0; /* WAC multi is cleared later in rhack */
	} else if (foo == DOAGAIN || in_doagain) {
		multi = last_multi;
	} else {
		last_multi = multi;
		savech(0); /* reset input queue */
		savech((char)foo);
	}

	if (multi) {
		multi--;
		save_cm = in_line;
	} else {
		save_cm = NULL;
	}
	in_line[0] = foo;
	in_line[1] = '\0';

	if (foo == DORUSH || foo == DORUN || foo == DOFORCEFIGHT || foo == DONOPICKUP || foo == DORUN_NOPICKUP || (iflags.num_pad && (foo == '5' || foo == '-'))) {
		foo = readchar();

		savech((char)foo);

		in_line[1] = foo;
		in_line[2] = 0;
	}
	clear_nhwindow(WIN_MESSAGE);

	if (prezero) in_line[0] = DOESCAPE;
	repeat_char = in_line[0];

	return in_line;
}

#ifdef UNIX
static void end_of_input(void) {
#ifndef NOSAVEONHANGUP
	if (!program_state.done_hup++ && program_state.something_worth_saving)
		dosave0();
#endif
	exit_nhwindows(NULL);
	clearlocks();
	terminate(EXIT_SUCCESS);
}
#endif

char readchar(void) {
	int sym;
	int x = u.ux, y = u.uy, mod = 0;

	if (iflags.debug_fuzzer) {
		return randomkey();
	}

	if (*readchar_queue) {
		sym = *readchar_queue++;
	} else {
		sym = in_doagain ? Getchar() : nh_poskey(&x, &y, &mod);
	}

#ifdef UNIX
#ifdef NR_OF_EOFS
	if (sym == EOF) {
		int cnt = NR_OF_EOFS;
		/*
		 * Some SYSV systems seem to return EOFs for various reasons
		 * (?like when one hits break or for interrupted systemcalls?),
		 * and we must see several before we quit.
		 */
		do {
			clearerr(stdin); /* omit if clearerr is undefined */
			sym = Getchar();
		} while (--cnt && sym == EOF);
	}
#endif /* NR_OF_EOFS */
	if (sym == EOF)
		end_of_input();
#endif /* UNIX */

	if (sym == 0) {
		/* click event */
		readchar_queue = click_to_cmd(x, y, mod);
		sym = *readchar_queue++;
	}
	return sym;
}

static int dotravel(void) {
	/* Keyboard travel command */
	static char cmd[2];
	coord cc;

	cmd[1] = 0;
	cc.x = iflags.travelcc.x;
	cc.y = iflags.travelcc.y;
	if (cc.x == -1 && cc.y == -1) {
		/* No cached destination, start attempt from current position */
		cc.x = u.ux;
		cc.y = u.uy;
	}
	pline("Where do you want to travel to?");
	if (getpos(&cc, true, "the desired destination") < 0) {
		/* user pressed ESC */
		return 0;
	}
	iflags.travelcc.x = u.tx = cc.x;
	iflags.travelcc.y = u.ty = cc.y;
	cmd[0] = CMD_TRAVEL;
	readchar_queue = cmd;
	return 0;
}

#ifdef PORT_DEBUG

int wiz_port_debug(void) {
	int n, k;
	winid win;
	anything any;
	int item = 'a';
	int num_menu_selections;
	struct menu_selection_struct {
		char *menutext;
		char *portname;
		void (*fn)(void);
	} menu_selections[] = {
		{NULL, NULL, NULL} /* array terminator */
	};

	num_menu_selections = SIZE(menu_selections) - 1;
	for (k = n = 0; k < num_menu_selections; ++k)
		if (!strcmp(menu_selections[k].portname, windowprocs.name))
			n++;
	if (n > 0) {
		menu_item *pick_list;
		win = create_nhwindow(NHW_MENU);
		start_menu(win);
		for (k = 0; k < num_menu_selections; ++k) {
			if (strcmp(menu_selections[k].portname,
				   windowprocs.name))
				continue;
			any.a_int = k + 1;
			add_menu(win, NO_GLYPH, &any, item++, 0, ATR_NONE,
				 menu_selections[k].menutext, MENU_UNSELECTED);
		}
		end_menu(win, "Which port debugging feature?");
		n = select_menu(win, PICK_ONE, &pick_list);
		destroy_nhwindow(win);
		if (n > 0) {
			n = pick_list[0].item.a_int - 1;
			free(pick_list);
			/* execute the function */
			(*menu_selections[n].fn)();
		}
	} else
		pline("No port-specific debug capability defined.");
	return 0;
}
#endif /*PORT_DEBUG*/

/*
 *   Parameter validator for generic yes/no function to prevent
 *   the core from sending too long a prompt string to the
 *   window port causing a buffer overflow there.
 */
char yn_function(const char *query, const char *resp, char def) {
	char qbuf[QBUFSZ];
	unsigned truncspot, reduction = sizeof(" [N]  ?") + 1;

	if (resp) reduction += strlen(resp) + sizeof(" () ");
	if (strlen(query) < (QBUFSZ - reduction))
		return windowprocs.win_yn_function(query, resp, def);
	paniclog("Query truncated: ", query);
	reduction += sizeof("...");
	truncspot = QBUFSZ - reduction;
	strncpy(qbuf, query, (int)truncspot);
	qbuf[truncspot] = '\0';
	strcat(qbuf, "...");
	return windowprocs.win_yn_function(qbuf, resp, def);
}

bool yesno_helper(char *input_so_far) { return !strcmpi(input_so_far, "yes") || !strcmpi(input_so_far, "no"); }

// 'paranoid' alternative to yn(), an answer of 'yes' must be entered completely
char yesno(const char *query) {
	const char yesnochars[] = " [yes/no] (n)";
	char qbuf[QBUFSZ];
	strncpy(qbuf, query, sizeof(qbuf) - 1 - sizeof(yesnochars));
	strcat(qbuf, yesnochars);

	char resp[BUFSZ];

	while (true) {
		instant_getlin(qbuf, resp, yesno_helper);

		if (!strcmpi(resp, "yes")) return 'y';
		else if (resp[0] == '\033' || !strcmpi(resp, "no") || !strcmpi(resp, "n")) return 'n';
	}
}

/*cmd.c*/
