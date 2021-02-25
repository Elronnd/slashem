/*	SCCS Id: @(#)pline.c	3.4	1999/11/28	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

// keep the most recent DUMPLOG_MSG_COUNT messages
void dumplogmsg(nhstr s) {
	saved_plines[saved_pline_index++] = s;

	if (saved_pline_index >= DUMPLOG_MSG_COUNT) saved_pline_index = 0;
}
void dumplogfreemsgs(void) {
	for (usize i = 0; i < DUMPLOG_MSG_COUNT; i++) {
		free(saved_plines[i]);
	}
}

static bool no_repeat = false;

void msgpline_add(int typ, char *pattern) {
	regex_t regex;
	int errnum = tre_regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
	if (errnum != 0) {
		char errbuf[BUFSZ];
		tre_regerror(errnum, &regex, errbuf, sizeof(errbuf));
		raw_printf("Bad regex in MSGTYPE: \"%s\": \"%s\"", pattern, errbuf);
		return;
	}

	struct _plinemsg *tmp = new(struct _plinemsg);
	if (!tmp) return;

	tmp->msgtype = typ;
	tmp->pattern = regex;
	tmp->next = pline_msg;
	pline_msg = tmp;
}

static int msgpline_type(const char *msg) {
	struct _plinemsg *tmp = pline_msg;
	while (tmp) {
		if (tre_regexec(&tmp->pattern, msg, 0, NULL, 0) == 0) return tmp->msgtype;
		tmp = tmp->next;
	}
	return MSGTYP_NORMAL;
}

static void npline(nhstr line) {
	if (program_state.wizkit_wishing) return;

	if (!iflags.window_inited) {
		raw_print(nhs2cstr(line));
		return;
	}

#ifndef MAC
	if (no_repeat && nhseq(line, toplines))
		return;
#endif /* MAC */

	if (vision_full_recalc) vision_recalc(0);
	if (u.ux) flush_screen(1); /* %% */

	//todo make tre work on widechars (not as simple as #define wchar_t glyph_t; it uses wmemcmp and maybe also other annoying things), to avoid this copy
	int typ = msgpline_type(nhs2cstr(line));

	if (typ == MSGTYP_NOSHOW) return;

	if (typ == MSGTYP_NOREP && !nhseq(line, saved_plines[(saved_pline_index ? saved_pline_index : DUMPLOG_MSG_COUNT) - 1])) return;
	putnstr(WIN_MESSAGE, 0, line);
	if (typ == MSGTYP_STOP) display_nhwindow(WIN_MESSAGE, true); /* --more-- */

	if (typ != MSGTYP_NOSHOW) {
		dumplogmsg(line);
	}

}

static void vpline(const char *line, va_list the_args) {
	char pbuf[BUFSZ];
	if (!line || !*line) return;

	usize l;

	if (index(line, '%')) {
		l = vsnprintf(pbuf, sizeof(pbuf), line, the_args);
		line = pbuf;
	} else {
		l = strlen(line);
	}

	npline(nhsdupzn(line, l));
}

void pline(const char *line, ...) {
	VA_START(line);
	vpline(line, VA_ARGS);
	VA_END();
}

void spline(const char *line, ...) {
	VA_START(line);
	npline(nhsfmtc_v(nhstyle_default(), line, VA_ARGS));
	VA_END();
}

void Norep(const char *line, ...) {
	VA_START(line);
	no_repeat = true;
	vpline(line, VA_ARGS);
	no_repeat = false;
	VA_END();
	return;
}

void vhear(const char *fmt, va_list the_args) {
	if (Deaf || !flags.acoustics) return;

	vpline(fmt, the_args);
}

void hear(const char *fmt, ...) {
	VA_START(fmt);
	vhear(fmt, VA_ARGS);
	VA_END();
}

void You_hearf(const char *line, ...) {
	char buf[BUFSZ];

	VA_START(line);
	if (Underwater)
		strcpy(buf, "You barely hear ");
	else if (u.usleep)
		strcpy(buf, "You dream that you hear ");
	else
		strcpy(buf, "You hear ");

	vhear(strcat(buf, line), VA_ARGS);
	VA_END();
}

/* Print a message inside double-quotes.
 * The caller is responsible for checking deafness.
 * Gods can speak directly to you in spite of deafness.
 */
void verbalize(const char *line, ...) {
	char buf[BUFSZ];

	VA_START(line);

	sprintf(buf, "\"%s\"", line);
	vpline(buf, VA_ARGS);

	VA_END();
}

static void vraw_printf(const char *, va_list);

void raw_printf(const char *line, ...) {
	VA_START(line);
	vraw_printf(line, VA_ARGS);
	VA_END();
}

static void vraw_printf(const char *line, va_list the_args) {
	if (!index(line, '%')) {
		raw_print(line);
	} else {
		char pbuf[BUFSZ];
		vsnprintf(pbuf, sizeof(pbuf), line, VA_ARGS);
		raw_print(pbuf);
	}
}

void _impossible(const char *file, int line, const char *s, ...) {
	VA_START(s);
	if (program_state.in_impossible)
		panic("impossible called impossible");
	program_state.in_impossible = 1;
	{
		char pbuf[BUFSZ];
		sprintf(pbuf, "%s:%d: ", file, line);
		vsnprintf(eos(pbuf), sizeof(pbuf) - strlen(pbuf), s, VA_ARGS);
		paniclog("impossible", pbuf);
		if (iflags.debug_fuzzer) {
			panic("%s", pbuf);
		}
	}
	pline("%s:%d: ", file, line);
	vpline(s, VA_ARGS);
	pline("Program in disorder!  Saving and reloading may fix the problem.");
	program_state.in_impossible = 0;
	VA_END();
}

const char *align_str(aligntyp alignment) {
	switch (alignment) {
		case A_CHAOTIC:
			return "chaotic";
		case A_NEUTRAL:
			return "neutral";
		case A_LAWFUL:
			return "lawful";
		case A_NONE:
			return "unaligned";
	}
	return "unknown";
}

void mstatusline(struct monst *mtmp) {
	aligntyp alignment = mon_aligntyp(mtmp);
	char info[BUFSZ], monnambuf[BUFSZ];

	info[0] = 0;
	if (mtmp->mtame) {
		strcat(info, ", tame");
		if (wizard) {
			sprintf(eos(info), " (%d", mtmp->mtame);
			if (!mtmp->isminion)
				sprintf(eos(info), "; hungry %ld; apport %d",
					EDOG(mtmp)->hungrytime, EDOG(mtmp)->apport);
			strcat(info, ")");
		}
	} else if (mtmp->mpeaceful)
		strcat(info, ", peaceful");
	else if (mtmp->mtraitor)
		strcat(info, ", traitor");
	if (mtmp->meating) strcat(info, ", eating");

	if (mtmp->meating && mtmp->cham == CHAM_ORDINARY && mtmp->mappearance && mtmp->m_ap_type) {
		sprintf(eos(info), ", mimicing %s",
				(mtmp->m_ap_type == M_AP_FURNITURE) ?
					an(sym_desc[mtmp->mappearance].explanation) :

				(mtmp->m_ap_type == M_AP_OBJECT && OBJ_DESCR(objects[mtmp->mappearance])) ?
					an(OBJ_DESCR(objects[mtmp->mappearance])) :

				(mtmp->m_ap_type == M_AP_OBJECT && OBJ_NAME(objects[mtmp->mappearance])) ?
					an(OBJ_NAME(objects[mtmp->mappearance])) :

				(mtmp->m_ap_type == M_AP_MONSTER) ?
					an(mons[mtmp->mappearance].mname) :

				"something");
	}

	if (mtmp->mcan) strcat(info, ", cancelled");
	if (mtmp->mconf) strcat(info, ", confused");
	if (mtmp->mblinded || !mtmp->mcansee)
		strcat(info, ", blind");
	if (mtmp->mstun) strcat(info, ", stunned");
	if (mtmp->msleeping) strcat(info, ", asleep");
#if 0 /* unfortunately mfrozen covers temporary sleep and being busy
	   (donning armor, for instance) as well as paralysis */
	else if (mtmp->mfrozen)	  strcat(info, ", paralyzed");
#else
	else if (mtmp->mfrozen || !mtmp->mcanmove)
		strcat(info, ", can't move");
#endif
	/* [arbitrary reason why it isn't moving] */
	else if (mtmp->mstrategy & STRAT_WAITMASK)
		strcat(info, ", meditating");
	else if (mtmp->mflee) {
		strcat(info, ", scared");
		if (wizard) sprintf(eos(info), " (%d)", mtmp->mfleetim);
	}
	if (mtmp->mtrapped) strcat(info, ", trapped");
	if (mtmp->mspeed) strcat(info,
				 mtmp->mspeed == MFAST ? ", fast" :
							 mtmp->mspeed == MSLOW ? ", slow" :
										 ", ???? speed");
	if (mtmp->mundetected) strcat(info, ", concealed");
	if (mtmp->minvis) strcat(info, ", invisible");
	if (mtmp == u.ustuck) strcat(info,
				     (sticks(youmonst.data)) ? ", held by you" :
							       u.uswallow ? (is_animal(u.ustuck->data) ?
										     ", swallowed you" :
										     ", engulfed you") :
									    ", holding you");
	if (mtmp == u.usteed) strcat(info, ", carrying you");

	/* avoid "Status of the invisible newt ..., invisible" */
	/* and unlike a normal mon_nam, use "saddled" even if it has a name */
	strcpy(monnambuf, x_monnam(mtmp, ARTICLE_THE, NULL,
				   (SUPPRESS_IT | SUPPRESS_INVISIBLE), false));

	pline("Status of %s (%s):  Level %d  HP %d(%d)  Pw %d(%d)  AC %d%s.",
	      monnambuf,
	      align_str(alignment),
	      mtmp->m_lev,
	      mtmp->mhp,
	      mtmp->mhpmax,
	      mtmp->m_en,
	      mtmp->m_enmax,
	      find_mac(mtmp),
	      info);
}

void ustatusline(void) {
	char info[BUFSZ];

	info[0] = '\0';
	if (Sick) {
		strcat(info, ", dying from");
		if (u.usick_type & SICK_VOMITABLE)
			strcat(info, " food poisoning");
		if (u.usick_type & SICK_NONVOMITABLE) {
			if (u.usick_type & SICK_VOMITABLE)
				strcat(info, " and");
			strcat(info, " illness");
		}
	}
	if (Stoned) strcat(info, ", solidifying");
	if (Slimed) strcat(info, ", becoming slimy");
	if (Strangled) strcat(info, ", being strangled");
	if (Vomiting) strcat(info, ", nauseated"); /* !"nauseous" */
	if (Confusion) strcat(info, ", confused");
	if (Blind) {
		strcat(info, ", blind");
		if (u.ucreamed) {
			if ((long)u.ucreamed < Blinded || Blindfolded || !haseyes(youmonst.data))
				strcat(info, ", cover");
			strcat(info, "ed by sticky goop");
		} /* note: "goop" == "glop"; variation is intentional */
	}
	if (Stunned) strcat(info, ", stunned");
	if (!u.usteed && Wounded_legs) {
		const char *what = body_part(LEG);
		if ((Wounded_legs & BOTH_SIDES) == BOTH_SIDES)
			what = makeplural(what);
		sprintf(eos(info), ", injured %s", what);
	}
	if (Glib) sprintf(eos(info), ", slippery %s",
			  makeplural(body_part(HAND)));
	if (u.utrap) strcat(info, ", trapped");
	if (Fast) strcat(info, Very_fast ?
				       ", very fast" :
				       ", fast");
	if (u.uundetected) strcat(info, ", concealed");
	if (Invis) strcat(info, ", invisible");
	if (u.ustuck) {
		if (sticks(youmonst.data))
			strcat(info, ", holding ");
		else
			strcat(info, ", held by ");
		strcat(info, mon_nam(u.ustuck));
	}

	pline("Status of %s (%s%s):  Level %d  HP %d(%d)  Pw %d(%d)  AC %d%s.",
	      plname,
	      (u.ualign.record >= 20) ? "piously " :
	      (u.ualign.record >  13) ? "devoutly " :
	      (u.ualign.record >   8) ? "fervently " :
	      (u.ualign.record >   3) ? "stridently " :
	      (u.ualign.record ==  3) ? "" :
	      (u.ualign.record >=  1) ? "haltingly " :
	      (u.ualign.record ==  0) ? "nominally " : "insufficiently ",
	      align_str(u.ualign.type),
	      Upolyd ? mons[u.umonnum].mlevel : u.ulevel,
	      Upolyd ? u.mh : u.uhp,
	      Upolyd ? u.mhmax : u.uhpmax,
	      u.uen,
	      u.uenmax,
	      u.uac,
	      info);
}

void self_invis_message(void) {
	pline("%s %s.",
	      Hallucination ? "Far out, man!  You" : "Gee!  All of a sudden, you",
	      See_invisible ? "can see right through yourself" :
			      "can't see yourself");
}
/*pline.c*/
