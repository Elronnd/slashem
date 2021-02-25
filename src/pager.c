/*	SCCS Id: @(#)pager.c	3.4	2003/08/13	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/* This file contains the command routines dowhatis() and dohelp() and */
/* a few other help related facilities */

#include "hack.h"
#include "dlb.h"

static boolean is_swallow_sym(int);
static int append_str(char *, const char *);
static struct permonst *lookat(int, int, char *, char *);
static void checkfile(char *, struct permonst *, bool, bool);
static boolean help_menu(int *);
#ifdef PORT_HELP
extern void port_help(void);
#endif

/* Returns "true" for characters that could represent a monster's stomach. */
static boolean is_swallow_sym(int c) {
	int i;
	for (i = S_sw_tl; i <= S_sw_br; i++)
		if ((int)showsyms[i] == c) return true;
	return false;
}

/*
 * Append new_str to the end of buf if new_str doesn't already exist as
 * a substring of buf.  Return 1 if the string was appended, 0 otherwise.
 * It is expected that buf is of size BUFSZ.
 */
static int append_str(char *buf, const char *new_str) {
	int space_left; /* space remaining in buf */

	if (strstri(buf, new_str)) return 0;

	space_left = BUFSZ - strlen(buf) - 1;
	strncat(buf, " or ", space_left);
	strncat(buf, new_str, space_left - 4);
	return 1;
}

/*
 * Return the name of the glyph found at (x,y).
 * If not hallucinating and the glyph is a monster, also monster data.
 */
static struct permonst *lookat(int x, int y, char *buf, char *monbuf) {
	struct monst *mtmp = NULL;
	struct permonst *pm = NULL;
	int glyph;

	buf[0] = monbuf[0] = 0;
	glyph = glyph_at(x, y);
	if (u.ux == x && u.uy == y && canspotself()) {
		char race[QBUFSZ];

		/* if not polymorphed, show both the role and the race */
		race[0] = 0;
		if (!Upolyd) {
			sprintf(race, "%s ", urace.adj);
		}

		sprintf(buf, "%s%s%s called %s",
			Invis ? "invisible " : "",
			race,
			mons[u.umonnum].mname,
			plname);
		/* file lookup can't distinguish between "gnomish wizard" monster
		   and correspondingly named player character, always picking the
		   former; force it to find the general "wizard" entry instead */
		if (Role_if(PM_WIZARD) && Race_if(PM_GNOME) && !Upolyd)
			pm = &mons[PM_WIZARD];

#if 0
		char race[QBUFSZ], role[QBUFSZ];

		/* if not polymorphed, show both the role and the race */
		role[0] = 0;
		race[0] = 0;

		if (!Upolyd) {
			sprintf(race, "%s ", urace.adj);
			sprintf(role, "%s ", urole.name);
		} else sprintf(race, "%s ", mons[u.umonnum].mname);

		sprintf(buf, "%s%s%s called %s",
		        Invis ? "invisible " : "",
		        race,
		        role,
		        plname);
#endif

		if (u.usteed) {
			char steedbuf[BUFSZ];

			sprintf(steedbuf, ", mounted on %s", y_monnam(u.usteed));
			/* assert((sizeof buf >= strlen(buf)+strlen(steedbuf)+1); */
			strcat(buf, steedbuf);
		}

		/* When you see yourself normally, no explanation is appended
		   (even if you could also see yourself via other means).
		   Sensing self while blind or swallowed is treated as if it
		   were by normal vision (cf canseeself()). */
		if ((Invisible || u.uundetected) && !Blind && !u.uswallow) {
			unsigned how = 0;

			if (Infravision) how |= 1;
			if (Unblind_telepat) how |= 2;
			if (Detect_monsters) how |= 4;

			if (how)
				sprintf(eos(buf), " [seen: %s%s%s%s%s]",
					(how & 1) ? "infravision" : "",
					/* add comma if telep and infrav */
					((how & 3) > 2) ? ", " : "",
					(how & 2) ? "telepathy" : "",
					/* add comma if detect and (infrav or telep or both) */
					((how & 7) > 4) ? ", " : "",
					(how & 4) ? "monster detection" : "");
		}
	} else if (u.uswallow) {
		/* all locations when swallowed other than the hero are the monster */
		sprintf(buf, "interior of %s",
			Blind ? "a monster" : a_monnam(u.ustuck));
		pm = u.ustuck->data;
	} else if (glyph_is_monster(glyph)) {
		bhitpos.x = x;
		bhitpos.y = y;
		mtmp = m_at(x, y);
		if (mtmp != NULL) {
			char *name, monnambuf[BUFSZ];
			boolean accurate = !Hallucination;

			if (mtmp->data == &mons[PM_COYOTE] && accurate)
				name = coyotename(mtmp, monnambuf);
			else
				name = distant_monnam(mtmp, ARTICLE_NONE, monnambuf);

			pm = mtmp->data;
			sprintf(buf, "%s%s%s",
				(mtmp->mx != x || mtmp->my != y) ?
					((mtmp->isshk && accurate) ? "tail of " : "tail of a ") :
					"",
				(mtmp->mtame && accurate) ? "tame " :
							    (mtmp->mpeaceful && accurate) ? "peaceful " : "",
				name);
			if (u.ustuck == mtmp)
				strcat(buf, (Upolyd && sticks(youmonst.data)) ?
						    ", being held" :
						    ", holding you");
			if (mtmp->mleashed)
				strcat(buf, ", leashed to you");

			if (mtmp->mtrapped && cansee(mtmp->mx, mtmp->my)) {
				struct trap *t = t_at(mtmp->mx, mtmp->my);
				int tt = t ? t->ttyp : NO_TRAP;

				/* newsym lets you know of the trap, so mention it here */
				if (tt == BEAR_TRAP || tt == PIT ||
				    tt == SPIKED_PIT || tt == WEB)
					sprintf(eos(buf), ", trapped in %s",
						an(sym_desc[trap_to_defsym(tt)].explanation));
			}

			{
				int ways_seen = 0, normal = 0, xraydist;
				bool useemon = canseemon(mtmp);

				xraydist = (u.xray_range < 0) ? -1 : u.xray_range * u.xray_range;
				   /* normal vision
				    * cansee is true for both normal and astral vision,
				    * but couldsee it not true for astral vision */
				if ((mtmp->wormno ? worm_known(mtmp) :
						    (cansee(mtmp->mx, mtmp->my) && couldsee(mtmp->mx, mtmp->my)))
				    && mon_visible(mtmp) && !mtmp->minvis) {
					ways_seen++;
					normal++;
				}
				/* see invisible */
				if (useemon && mtmp->minvis)
					ways_seen++;
				/* infravision */
				if ((!mtmp->minvis || See_invisible) && see_with_infrared(mtmp))
					ways_seen++;
				/* telepathy */
				if (tp_sensemon(mtmp))
					ways_seen++;
				/* xray */
				if (useemon && xraydist > 0 &&
				    distu(mtmp->mx, mtmp->my) <= xraydist)
					ways_seen++;
				if (Detect_monsters)
					ways_seen++;
				if (MATCH_WARN_OF_MON(mtmp))
					ways_seen++;

				if (ways_seen > 1 || !normal) {
					if (normal) {
						strcat(monbuf, "normal vision");
						/* can't actually be 1 yet here */
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
					if (useemon && mtmp->minvis) {
						strcat(monbuf, "see invisible");
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
					if ((!mtmp->minvis || See_invisible) &&
					    see_with_infrared(mtmp)) {
						strcat(monbuf, "infravision");
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
					if (tp_sensemon(mtmp)) {
						strcat(monbuf, "telepathy");
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
					if (useemon && xraydist > 0 &&
					    distu(mtmp->mx, mtmp->my) <= xraydist) {
						/* Eyes of the Overworld */
						strcat(monbuf, "astral vision");
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
					if (Detect_monsters) {
						strcat(monbuf, "monster detection");
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
					if (MATCH_WARN_OF_MON(mtmp)) {
						char wbuf[BUFSZ];
						if (Hallucination)
							strcat(monbuf, "paranoid delusion");
						else {
							sprintf(wbuf, "warned of %s",
								makeplural(mtmp->data->mname));
							strcat(monbuf, wbuf);
						}
						if (ways_seen-- > 1) strcat(monbuf, ", ");
					}
				}
			}
		}
	} else if (glyph_is_object(glyph)) {
		struct obj *otmp = vobj_at(x, y);

		if (!otmp || otmp->otyp != glyph_to_obj(glyph)) {
			if (glyph_to_obj(glyph) != STRANGE_OBJECT) {
				otmp = mksobj(glyph_to_obj(glyph), false, false);
				if (otmp->oclass == COIN_CLASS)
					otmp->quan = 2L; /* to force pluralization */
				else if (otmp->otyp == SLIME_MOLD)
					otmp->spe = current_fruit; /* give the fruit a type */
				strcpy(buf, distant_name(otmp, xname));
				dealloc_obj(otmp);
			}
		} else
			strcpy(buf, distant_name(otmp, xname));

		if (levl[x][y].typ == STONE || levl[x][y].typ == SCORR)
			strcat(buf, " embedded in stone");
		else if (IS_WALL(levl[x][y].typ) || levl[x][y].typ == SDOOR)
			strcat(buf, " embedded in a wall");
		else if (closed_door(x, y))
			strcat(buf, " embedded in a door");
		else if (is_pool(x, y))
			strcat(buf, " in water");
		else if (is_lava(x, y))
			strcat(buf, " in molten lava"); /* [can this ever happen?] */
	} else if (glyph_is_trap(glyph)) {
		int tnum = what_trap(glyph_to_trap(glyph));
		strcpy(buf, sym_desc[trap_to_defsym(tnum)].explanation);
	} else if (!glyph_is_cmap(glyph)) {
		strcpy(buf, "unexplored area");
	} else
		switch (glyph_to_cmap(glyph)) {
			case S_altar:
				if (!In_endgame(&u.uz))
					sprintf(buf, "%s altar",
						align_str(Amask2align(levl[x][y].altarmask & ~AM_SHRINE)));
				else
					sprintf(buf, "aligned altar");
				break;
			case S_ndoor:
				if (is_drawbridge_wall(x, y) >= 0)
					strcpy(buf, "open drawbridge portcullis");
				else if ((levl[x][y].doormask & ~D_TRAPPED) == D_BROKEN)
					strcpy(buf, "broken door");
				else
					strcpy(buf, "doorway");
				break;
			case S_cloud:
				strcpy(buf, Is_airlevel(&u.uz) ? "cloudy area" : "fog/vapor cloud");
				break;
			case S_water:
			case S_pool:
				strcpy(buf, level.flags.lethe ? "sparkling water" : "water");
				break;
			default:
				strcpy(buf, sym_desc[glyph_to_cmap(glyph)].explanation);
				break;
		}

	return (pm && !Hallucination) ? pm : NULL;
}

/*
 * Look in the "data" file for more info.  Called if the user typed in the
 * whole name (user_typed_name == true), or we've found a possible match
 * with a character/glyph and flags.help is true.
 *
 * NOTE: when (user_typed_name == false), inp is considered read-only and
 *	 must not be changed directly, e.g. via lcase(). We want to force
 *	 lcase() for data.base lookup so that we can have a clean key.
 *	 Therefore, we create a copy of inp _just_ for data.base lookup.
 */
static void checkfile(char *inp, struct permonst *pm, bool user_typed_name, bool without_asking) {
	dlb *fp;
	char buf[BUFSZ], newstr[BUFSZ];
	char *ep, *dbase_str;
	bool found_in_file = false;

	fp = dlb_fopen(NH_DATAFILE, "r");
	if (!fp) {
		pline("Cannot open data file!");
		return;
	}

	/* To prevent the need for entries in data.base like *ngel to account
	 * for Angel and angel, make the lookup string the same for both
	 * user_typed_name and picked name.
	 */
	if (pm != NULL && !user_typed_name) {
		dbase_str = strcpy(newstr, pm->mname);
	} else {
		dbase_str = strcpy(newstr, inp);
	}

	if (!strncmp(dbase_str, "interior of ", 12))
		dbase_str += 12;
	if (!strncmp(dbase_str, "a ", 2))
		dbase_str += 2;
	else if (!strncmp(dbase_str, "an ", 3))
		dbase_str += 3;
	else if (!strncmp(dbase_str, "the ", 4))
		dbase_str += 4;
	if (!strncmp(dbase_str, "tame ", 5))
		dbase_str += 5;
	else if (!strncmp(dbase_str, "peaceful ", 9))
		dbase_str += 9;
	if (!strncmp(dbase_str, "invisible ", 10))
		dbase_str += 10;

	if (!strncmp(dbase_str, "statue of ", 10))
		dbase_str[6] = '\0';
	else if (!strncmp(dbase_str, "figurine of ", 12))
		dbase_str[8] = '\0';

	/* adjust the input to remove "named " and convert to lower case */
	char *alt = NULL; /* alternate description */

	if ((ep = strstri(dbase_str, " named ")) != 0)
		alt = ep + 7;
	else
		ep = strstri(dbase_str, " called ");
	if (!ep) ep = strstri(dbase_str, ", ");
	if (ep && ep > dbase_str) *ep = '\0';

	/*
	 * If the object is named, then the name is the alternate description;
	 * otherwise, the result of makesingular() applied to the name is. This
	 * isn't strictly optimal, but named objects of interest to the user
	 * will usually be found under their name, rather than under their
	 * object type, so looking for a singular form is pointless.
	 */
	if (!alt) {
		alt = makesingular(dbase_str);
	}

	bool in_exclusion = false;
	usize entry_location;

	// look for the appropriate entry
	while (dlb_fgets(buf, BUFSZ, fp)) {
		// comment line
		if (buf[0] == '#') continue;

		// part of an entry's text
		if (buf[0] == '\t') {
			// reached the end of a list of entries, so the
			// exclusion no longer applies
			in_exclusion = false;
			continue;
		}

		if (in_exclusion) {
			continue;
		}

		if (!index(buf, '\n')) goto bad_data_file;
		*index(buf, '\n') = 0;	// strip newline

		if (*buf == '~') {
			if (regmatch(buf + 1, dbase_str, true) || (alt && regmatch(buf + 1, alt, true))) {
				in_exclusion = true;
				continue;
			}
		}

		// matched the entry name
		if (regmatch(buf, dbase_str, true) || (alt && regmatch(buf, alt, true))) {
			// skip to the part containing the description
			do {
				entry_location = dlb_ftell(fp);
				if (!dlb_fgets(buf, BUFSZ, fp)) {
					goto bad_data_file;
				}
			} while (buf[0] != '\t');

			found_in_file = true;
			break;
		}
	}

	if (found_in_file) {
		if (user_typed_name || without_asking || yn("More info?") == 'y') {
			winid datawin;

			if (dlb_fseek(fp, entry_location, SEEK_SET) < 0) {
				goto bad_data_file;
			}

			datawin = create_nhwindow(NHW_MENU);
			while (true) {
				if (!dlb_fgets(buf, BUFSZ, fp)) {
					// EOF
					break;
				}

				if (buf[0] != '\t') {
					// end of entry--start of the title of a new one
					break;
				}

				putstr(datawin, 0, buf + 1);
			}
			display_nhwindow(datawin, false);
			destroy_nhwindow(datawin);
		}
	} else if (user_typed_name) {
		pline("I don't have any information on those things.");
	}

	dlb_fclose(fp);
	return;

bad_data_file:
	impossible("Bad data file");
	dlb_fclose(fp);
}

/* getpos() return values */
#define LOOK_TRADITIONAL 0 /* '.' -- ask about "more info?" */
#define LOOK_QUICK	 1 /* ',' -- skip "more info?" */
#define LOOK_ONCE	 2 /* ';' -- skip and stop looping */
#define LOOK_VERBOSE	 3 /* ':' -- show more info w/o asking */

/* also used by getpos hack in do_name.c */
const char what_is_an_unknown_object[] = "an unknown object";

int do_look(int mode, coord *click_cc) {
	bool quick = mode == 1; // use cursor and don't search for 'more info'
	bool clicklook = mode == 2; // right mouse-click method

	char out_str[BUFSZ], look_buf[BUFSZ];
	const char *x_str, *firstmatch = 0;
	struct permonst *pm = 0;
	int i, ans = 0;
	glyph_t sym;	      /* typed symbol or converted glyph */
	int found;	      /* count of matching syms found */
	coord cc;	      /* screen pos of unknown glyph */
	boolean save_verbose; /* saved value of flags.verbose */
	boolean from_screen;  /* question from the screen */
	boolean need_to_look; /* need to get explan. from glyph */
	boolean hit_trap;     /* true if found trap explanation */
	int skipped_venom;    /* non-zero if we ignored "splash of venom" */
	static const char *mon_interior = "the interior of a monster";

	if (!clicklook) {
		if (quick) {
			from_screen = true; /* yes, we want to use the cursor */
		} else {
			i = ynq("Specify unknown object by cursor?");
			if (i == 'q') return 0;
			from_screen = (i == 'y');
		}

		if (from_screen) {
			cc.x = u.ux;
			cc.y = u.uy;
			sym = 0; /* gcc -Wall lint */
		} else {
			getlin("Specify what? (type the word)", out_str);
			if (out_str[0] == '\0' || out_str[0] == '\033')
				return 0;

			if (out_str[1]) { /* user typed in a complete string */
				checkfile(out_str, pm, true, true);
				return 0;
			}
			sym = out_str[0];
		}
	} else { // clicklook
		cc.x = click_cc->x;
		cc.y = click_cc->y;
		sym = 0;
		from_screen = false;
	}

	/* Save the verbose flag, we change it later. */
	save_verbose = flags.verbose;
	flags.verbose = flags.verbose && !quick;
	/*
	 * The user typed one letter, or we're identifying from the screen.
	 */
	do {
		/* Reset some variables. */
		need_to_look = false;
		pm = NULL;
		skipped_venom = 0;
		found = 0;
		out_str[0] = '\0';

		if (from_screen || clicklook) {
			int glyph; /* glyph at selected position */

			if (from_screen) {
				if (flags.verbose)
					pline("Please move the cursor to %s.",
							what_is_an_unknown_object);
				else
					pline("Pick an object.");

				ans = getpos(&cc, quick, what_is_an_unknown_object);
				if (ans < 0 || cc.x < 0) {
					flags.verbose = save_verbose;
					return 0; /* done */
				}
				flags.verbose = false; /* only print long question once */
			}

			/* Convert the glyph at the selected position to a symbol. */
			glyph = glyph_at(cc.x, cc.y);
			if (glyph_is_cmap(glyph)) {
				sym = showsyms[glyph_to_cmap(glyph)];
			} else if (glyph_is_trap(glyph)) {
				sym = showsyms[trap_to_defsym(glyph_to_trap(glyph))];
			} else if (glyph_is_object(glyph)) {
				sym = oc_syms[(int)objects[glyph_to_obj(glyph)].oc_class];
			} else if (glyph_is_monster(glyph)) {
				/* takes care of pets, detected, ridden, and regular mons */
				sym = monsyms[(int)mons[glyph_to_mon(glyph)].mlet];
			} else if (glyph_is_swallow(glyph)) {
				sym = showsyms[glyph_to_swallow(glyph) + S_sw_tl];
			} else if (glyph_is_invisible(glyph)) {
				sym = DEF_INVISIBLE;
			} else if (glyph_is_warning(glyph)) {
				sym = showsyms[S_warn0 + glyph - GLYPH_WARNING_OFF];
			} else {
				impossible("do_look:  bad glyph %d at (%d,%d)",
					   glyph, (int)cc.x, (int)cc.y);
				sym = ' ';
			}
		}

		/*
		 * Check all the possibilities, saving all explanations in a buffer.
		 * When all have been checked then the string is printed.
		 */

		/* Check for monsters */
		for (i = 0; i < MAXMCLASSES; i++) {
			if (sym == ((from_screen || clicklook) ? monsyms[i] : def_monsyms[i]) &&
			    monexplain[i]) {
				need_to_look = true;
				if (!found) {
					sprintf(out_str, "%s       %s", utf8_tmpstr(sym), an(monexplain[i]));
					firstmatch = monexplain[i];
					found++;
				} else {
					found += append_str(out_str, an(monexplain[i]));
				}
			}
		}
		/* handle '@' as a special case if it refers to you and you're
		   playing a character which isn't normally displayed by that
		   symbol; firstmatch is assumed to already be set for '@' */
		if (((from_screen || clicklook) ?
			     (sym == monsyms[S_HUMAN] && cc.x == u.ux && cc.y == u.uy) :
			     (sym == def_monsyms[S_HUMAN] && !iflags.showrace)) &&
		    !(Race_if(PM_HUMAN) || Race_if(PM_ELF)) && !Upolyd)
			found += append_str(out_str, "you"); /* tack on "or you" */

		/*
		 * Special case: if identifying from the screen, and we're swallowed,
		 * and looking at something other than our own symbol, then just say
		 * "the interior of a monster".
		 */
		if (u.uswallow && (from_screen || clicklook) && is_swallow_sym(sym)) {
			if (!found) {
				sprintf(out_str, "%s       %s", utf8_tmpstr(sym), mon_interior);
				firstmatch = mon_interior;
			} else {
				found += append_str(out_str, mon_interior);
			}
			need_to_look = true;
		}

		/* Now check for objects */
		for (i = 1; i < MAXOCLASSES; i++) {
			if (sym == ((from_screen || clicklook) ? oc_syms[i] : def_oc_syms[i])) {
				need_to_look = true;
				if ((from_screen || clicklook) && i == VENOM_CLASS) {
					skipped_venom++;
					continue;
				}
				if (!found) {
					sprintf(out_str, "%s       %s", utf8_tmpstr(sym), an(objexplain[i]));
					firstmatch = objexplain[i];
					found++;
				} else {
					found += append_str(out_str, an(objexplain[i]));
				}
			}
		}

		if (sym == DEF_INVISIBLE) {
			if (!found) {
				sprintf(out_str, "%s       %s", utf8_tmpstr(sym), an(invisexplain));
				firstmatch = invisexplain;
				found++;
			} else {
				found += append_str(out_str, an(invisexplain));
			}
		}

#define is_cmap_trap(i)	      ((i) >= S_arrow_trap && (i) <= S_polymorph_trap)
#define is_cmap_drawbridge(i) ((i) >= S_vodbridge && (i) <= S_hcdbridge)

		/* Now check for graphics symbols */
		for (hit_trap = false, i = 0; i < MAXPCHARS; i++) {
			x_str = sym_desc[i].explanation;
			if (sym == ((from_screen || clicklook) ? showsyms[i] : ascii_graphics[i]) && *x_str) {
				/* avoid "an air", "a water", "a floor of a room", or "a dark part of a room" */
				int article = ((i == S_room) || (i == S_darkroom)) ? 2 :  // 2=>"the"
						      !(strcmp(x_str, "air") == 0 ||	  /* 1=>"an"  */
							strcmp(x_str, "water") == 0);	  /* 0=>(none)*/

				if (!found) {
					if (is_cmap_trap(i)) {
						sprintf(out_str, "%s       a trap", utf8_tmpstr(sym));
						hit_trap = true;
					} else if (level.flags.lethe && !strcmp(x_str, "water")) {
						sprintf(out_str, "%s       sparkling water", utf8_tmpstr(sym));
					} else {
						sprintf(out_str, "%s       %s", utf8_tmpstr(sym),
							article == 2 ? the(x_str) :
								       article == 1 ? an(x_str) : x_str);
					}
					firstmatch = x_str;
					found++;
				} else if (!u.uswallow && !(hit_trap && is_cmap_trap(i)) &&
					   !(found >= 3 && is_cmap_drawbridge(i))) {
					if (level.flags.lethe && !strcmp(x_str, "water"))
						found += append_str(out_str, "sparkling water");
					else
						found += append_str(out_str,
								    article == 2 ? the(x_str) :
										   article == 1 ? an(x_str) : x_str);
					if (is_cmap_trap(i)) hit_trap = true;
				}

				if (i == S_altar || is_cmap_trap(i))
					need_to_look = true;
			}
		}

		/* Now check for warning symbols */
		for (i = 1; i < MAXWARNINGS; i++) {
			x_str = sym_desc[S_warn0 + i].explanation;
			if (sym == ((from_screen || clicklook) ? showsyms : ascii_graphics)[S_warn0 + i]) {
				if (!found) {
					sprintf(out_str, "%s       %s", utf8_tmpstr(sym), sym_desc[S_warn0 + i].explanation);
					firstmatch = sym_desc[S_warn0 + i].explanation;
					found++;
				} else {
					found += append_str(out_str, sym_desc[S_warn0 + i].explanation);
				}
				/* Kludge: warning trumps boulders on the display.
				   Reveal the boulder too or player can get confused */
				if ((from_screen || clicklook) && sobj_at(BOULDER, cc.x, cc.y))
					strcat(out_str, " co-located with a boulder");
				break; /* out of for loop*/
			}
		}

		/* if we ignored venom and list turned out to be short, put it back */
		if (skipped_venom && found < 2) {
			x_str = objexplain[VENOM_CLASS];
			if (!found) {
				sprintf(out_str, "%s       %s", utf8_tmpstr(sym), an(x_str));
				firstmatch = x_str;
				found++;
			} else {
				found += append_str(out_str, an(x_str));
			}
		}

		/*
		 * If we are looking at the screen, follow multiple possibilities or
		 * an ambiguous explanation by something more detailed.
		 */
		if (from_screen || clicklook) {
			if (found > 1 || need_to_look) {
				char monbuf[QBUFSZ];
				nhstr temp_buf = new_nhs();

				pm = lookat(cc.x, cc.y, look_buf, monbuf);
				firstmatch = look_buf;
				if (*firstmatch) {
					temp_buf = nhsfmt(" (%S)", firstmatch);
					strncat(out_str, nhs2cstr(temp_buf), BUFSZ - strlen(out_str) - 1);
					found = 1; /* we have something to look up */
				}
				if (monbuf[0]) {
					temp_buf = nhsfmt(" [seen: %S]", monbuf);
					strncat(out_str, nhs2cstr(temp_buf), BUFSZ - strlen(out_str) - 1);
				}
				if (wizard && pm) {
					struct monst *mtmp = m_at(cc.x, cc.y);
					if (mtmp && mtmp->oldmonnm != monsndx(pm)) {
						temp_buf = nhsfmt(" [polymorphed from a %S]",
							mons[mtmp->oldmonnm].mname);
						strncat(out_str, nhs2cstr(temp_buf), BUFSZ - strlen(out_str) - 1);
					}
				}
				del_nhs(&temp_buf);
			}
		}

		/* Finally, print out our explanation. */
		if (found) {
			pline("%s", out_str);
			/* check the data file for information about this thing */
			if (found == 1 && ans != LOOK_QUICK && ans != LOOK_ONCE &&
			    (ans == LOOK_VERBOSE || (flags.help && !quick)) && !clicklook) {
				char temp_buf[BUFSZ];
				strcpy(temp_buf, level.flags.lethe && !strcmp(firstmatch, "water") ?
							 "lethe" :
							 firstmatch);
				checkfile(temp_buf, pm, false, ans == LOOK_VERBOSE);
			}
		} else {
			pline("I've never heard of such things.");
		}

	} while (from_screen && !quick && ans != LOOK_ONCE && !clicklook);

	flags.verbose = save_verbose;
	return 0;
}

int dowhatis(void) {
	return do_look(0, NULL);
}

int doquickwhatis(void) {
	return do_look(1, NULL);
}

int doidtrap(void) {
	struct trap *trap;
	int x, y, tt;

	if (!getdir("^")) return 0;
	x = u.ux + u.dx;
	y = u.uy + u.dy;
	for (trap = ftrap; trap; trap = trap->ntrap)
		if (trap->tx == x && trap->ty == y) {
			if (!trap->tseen) break;
			tt = trap->ttyp;
			if (u.dz) {
				if (u.dz < 0 ? is_holelike(tt) :
					       tt == ROCKTRAP) break;
			}
			tt = what_trap(tt);
			pline("That is %s%s%s.",
			      an(sym_desc[trap_to_defsym(tt)].explanation),
			      !trap->madeby_u ? "" : (tt == WEB) ? " woven" :
			   /* trap doors & spiked pits can't be made by
			         player, and should be considered at least
			         as much "set" as "dug" anyway */
							     (tt == HOLE || tt == PIT) ? " dug" : " set",
			      !trap->madeby_u ? "" : " by you");
			return 0;
		}
	pline("I can't see a trap there.");
	return 0;
}

char *
dowhatdoes_core(char q, char *cbuf) {
	dlb *fp;
	char bufr[BUFSZ];
	char *buf = &bufr[6], *ep, ctrl, meta;

	fp = dlb_fopen(NH_CMDHELPFILE, "r");
	if (!fp) {
		pline("Cannot open data file!");
		return 0;
	}

	ctrl = ((q <= '\033') ? (q - 1 + 'A') : 0);
	meta = ((0x80 & q) ? (0x7f & q) : 0);
	while (dlb_fgets(buf, BUFSZ - 6, fp)) {
		if ((ctrl && *buf == '^' && *(buf + 1) == ctrl) ||
		    (meta && *buf == 'M' && *(buf + 1) == '-' && *(buf + 2) == meta) ||
		    *buf == q) {
			if ((ep = index(buf, '\n')) != 0) *ep = 0;
			if (ctrl && buf[2] == '\t') {
				buf = bufr + 1;
				memcpy(buf, "^?      ", 8);
				buf[1] = ctrl;
			} else if (meta && buf[3] == '\t') {
				buf = bufr + 2;
				memcpy(buf, "M-?     ", 8);
				buf[2] = meta;
			} else if (buf[1] == '\t') {
				buf = bufr;
				buf[0] = q;
				memcpy(buf + 1, "       ", 7);
			}
			dlb_fclose(fp);
			strcpy(cbuf, buf);
			return cbuf;
		}
	}
	dlb_fclose(fp);
	return NULL;
}

int dowhatdoes(void) {
	char bufr[BUFSZ];
	char q, *reslt;

#ifdef UNIX
	introff();
#endif
	q = yn_function("What command?", NULL, '\0');
#ifdef UNIX
	intron();
#endif
	reslt = dowhatdoes_core(q, bufr);
	if (reslt)
		pline("%s", reslt);
	else
		pline("I've never heard of such commands.");
	return 0;
}

/* data for help_menu() */
static const char *help_menu_items[] = {
	/* 0*/ "Long description of the game and commands.",
	/* 1*/ "List of game commands.",
	/* 2*/ "Concise history of Slash'EM.",
	/* 3*/ "Info on a character in the game display.",
	/* 4*/ "Info on what a given key does.",
	/* 5*/ "List of game options.",
	/* 6*/ "Longer explanation of game options.",
	/* 7*/ "Full list of keyboard commands.",
	/* 8*/ "List of extended commands.",
	/* 9*/ "The NetHack license.",

#ifndef MAC
	/*WAC Add access to txt guidebook*/
	/* 10*/ "The Slash'EM Guidebook.",
#endif
#ifdef PORT_HELP
	"%s-specific help and commands.",
#endif
	"List of wizard-mode commands.",
	"",
	NULL};

enum {
	LICENSE_SLOT = 9,
#ifndef MAC
	GUIDEBOOK_SLOT,
#endif
#ifdef PORT_HELP
	PORT_HELP_ID,
#endif
	WIZHLP_SLOT,
	NULL_SLOT
};

static boolean help_menu(int *sel) {
	winid tmpwin = create_nhwindow(NHW_MENU);
#ifdef PORT_HELP
	char helpbuf[QBUFSZ];
#endif
	int i, n;
	menu_item *selected;
	anything any;

	any.a_void = 0; /* zero all bits */
	start_menu(tmpwin);
	if (!wizard) help_menu_items[WIZHLP_SLOT] = "",
		     help_menu_items[WIZHLP_SLOT + 1] = NULL;
	for (i = 0; help_menu_items[i]; i++)
#ifdef PORT_HELP
		/* port-specific line has a %s in it for the PORT_ID */
		if (help_menu_items[i][0] == '%') {
			sprintf(helpbuf, help_menu_items[i], PORT_ID);
			any.a_int = PORT_HELP_ID + 1;
			add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE,
				 helpbuf, MENU_UNSELECTED);
		} else
#endif
		{
			any.a_int = (*help_menu_items[i]) ? i + 1 : 0;
			add_menu(tmpwin, NO_GLYPH, &any, 0, 0,
				 ATR_NONE, help_menu_items[i], MENU_UNSELECTED);
		}
	end_menu(tmpwin, "Select one item:");
	n = select_menu(tmpwin, PICK_ONE, &selected);
	destroy_nhwindow(tmpwin);
	if (n > 0) {
		*sel = selected[0].item.a_int - 1;
		free(selected);
		return true;
	}
	return false;
}

int dohelp(void) {
	int sel = 0;

	if (help_menu(&sel)) {
		switch (sel) {
			case 0:
				display_file(NH_HELP, true);
				break;
			case 1:
				display_file(NH_SHELP, true);
				break;
			case 2:
				dohistory();
				break;
			case 3:
				dowhatis();
				break;
			case 4:
				dowhatdoes();
				break;
			case 5:
				option_help();
				break;
			case 6:
				display_file(NH_OPTIONFILE, true);
				break;
			case 7:
				dokeylist();
				break;
			case 8:
				doextlist();
				break;
			case 9:
				display_file(NH_LICENSE, true);
				break;

			case GUIDEBOOK_SLOT:
				display_file(NH_GUIDEBOOK, true);
				break;
#ifdef PORT_HELP
			case PORT_HELP_ID:
				port_help();
				break;
#endif
			case WIZHLP_SLOT:
				display_file(NH_DEBUGHELP, true);
				break;
		}
	}
	return 0;
}

int dohistory(void) {
	display_file(NH_HISTORY, true);
	return 0;
}

/*pager.c*/
