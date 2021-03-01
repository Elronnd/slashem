/*	SCCS Id: @(#)potion.c	3.4	2002/10/02	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "onames.h"

/* KMH, intrinsics patch
 * There are many changes here to support >32-bit properties.
 * Also, blessed potions are once again permitted to convey
 * permanent intrinsics.
 */

boolean notonhead = false;

static int nothing, unkn;
static const char beverages[] = {POTION_CLASS, 0};

static long itimeout(long);
static long itimeout_incr(long, int);
static void ghost_from_bottle(void);
static short mixtype(struct obj *, struct obj *);

static void healup_mon(struct monst *, int, int, boolean, boolean);
/* For healing monsters - analogous to healup for players */

/* force `val' to be within valid range for intrinsic timeout value */
static long itimeout(long val) {
	if (val >= TIMEOUT)
		val = TIMEOUT;
	else if (val < 1)
		val = 0;

	return val;
}

/* increment `old' by `incr' and force result to be valid intrinsic timeout */
static long itimeout_incr(long old, int incr) {
	return itimeout((old & TIMEOUT) + (long)incr);
}

/* set the timeout field of intrinsic `which' */
void set_itimeout(long *which, long val) {
	*which &= ~TIMEOUT;
	*which |= itimeout(val);
}

/* increment the timeout field of intrinsic `which' */
void incr_itimeout(long *which, int incr) {
	set_itimeout(which, itimeout_incr(*which, incr));
}

void make_confused(long xtime, boolean talk) {
	long old = HConfusion;

	if (!xtime && old) {
		if (talk)
			pline("You feel less %s now.",
			      Hallucination ? "trippy" : "confused");
	}
	if ((xtime && !old) || (!xtime && old)) context.botl = true;

	set_itimeout(&HConfusion, xtime);
}

void make_stunned(long xtime, boolean talk) {
	long old = HStun;

	if (!xtime && old) {
		if (talk)
			pline("You feel %s now.",
			      Hallucination ? "less wobbly" : "a bit steadier");
	}
	if (xtime && !old) {
		if (talk) {
			if (u.usteed)
				pline("You wobble in the saddle.");
			else
				pline("You %s...", stagger(youmonst.data, "stagger"));
		}
	}
	if ((!xtime && old) || (xtime && !old)) context.botl = true;

	set_itimeout(&HStun, xtime);
}

void make_sick(long xtime, const char *cause, boolean talk, int type) {
	long old = Sick;

	if (xtime > 0L) {
		if (Sick_resistance) return;
		if (!old) {
			/* newly sick */
			pline("You feel deathly sick.");
		} else {
			/* already sick */
			if (talk) pline("You feel %s worse.",
					xtime <= Sick / 2L ? "much" : "even");
		}
		set_itimeout(&Sick, xtime);
		u.usick_type |= type;
		context.botl = true;
	} else if (old && (type & u.usick_type)) {
		/* was sick, now not */
		u.usick_type &= ~type;
		if (u.usick_type) { /* only partly cured */
			if (talk) pline("You feel somewhat better.");
			set_itimeout(&Sick, Sick * 2); /* approximation */
		} else {
			if (talk) pline("What a relief!");
			Sick = 0L; /* set_itimeout(&Sick, 0L) */
		}
		context.botl = true;
	}

	if (Sick) {
		exercise(A_CON, false);
		nhstr tmp = nhsdupz(cause);
		delayed_killer(SICK, KILLED_BY_AN, tmp);
		del_nhs(&tmp);
	} else {
		dealloc_killer(find_delayed_killer(SICK));
	}
}

void make_slimed(long xtime, const char *msg) {
	long old = Slimed;

	if ((!xtime && old) || (xtime && !old)) {
		if (msg) pline("%s", msg);
		context.botl = 1;
	}
	set_itimeout(&Slimed, xtime);
	if (!Slimed) dealloc_killer(find_delayed_killer(SLIMED));
}

// start or stop petrification
void make_stoned(long xtime, const char *msg, int killedby, const nhstr killername) {
	long old = Stoned;

	if ((!xtime && old) || (xtime && !old)) {
		if (msg) pline("%s", msg);
		//context.botl = 1;   // Stoned is not a status line item
	}

	set_itimeout(&Stoned, xtime);

	if (!Stoned) dealloc_killer(find_delayed_killer(STONED));
	else if (!old) delayed_killer(STONED, killedby, killername);
}

void make_vomiting(long xtime, boolean talk) {
	long old = Vomiting;

	if (!xtime && old)
		if (talk) pline("You feel much less nauseated now.");

	set_itimeout(&Vomiting, xtime);
}

void make_blinded(long xtime, boolean talk) {
	long old = Blinded;
	boolean u_could_see, can_see_now;
	int eyecnt;
	char buf[BUFSZ];

	/* we need to probe ahead in case the Eyes of the Overworld
	   are or will be overriding blindness */
	u_could_see = !Blind;
	Blinded = xtime ? 1L : 0L;
	can_see_now = !Blind;
	Blinded = old; /* restore */

	if (u.usleep) talk = false;

	if (can_see_now && !u_could_see) { /* regaining sight */
		if (talk) {
			if (Hallucination)
				pline("Far out!  Everything is all cosmic again!");
			else
				pline("You can see again.");
		}
	} else if (old && !xtime) {
		/* clearing temporary blindness without toggling blindness */
		if (talk) {
			if (!haseyes(youmonst.data)) {
				strange_feeling(NULL, NULL);
			} else if (Blindfolded) {
				strcpy(buf, body_part(EYE));
				eyecnt = eyecount(youmonst.data);
				pline("Your %s momentarily %s.", (eyecnt == 1) ? buf : makeplural(buf),
				      (eyecnt == 1) ? "itches" : "itch");
			} else { /* Eyes of the Overworld */
				pline("Your vision seems to brighten for a moment but is %s now.",
				      Hallucination ? "sadder" : "normal");
			}
		}
	}

	if (u_could_see && !can_see_now) { /* losing sight */
		if (talk) {
			if (Hallucination)
				pline("Oh, bummer!  Everything is dark!  Help!");
			else
				pline("A cloud of darkness falls upon you.");
		}
		/* Before the hero goes blind, set the ball&chain variables. */
		if (Punished) set_bc(0);
	} else if (!old && xtime) {
		/* setting temporary blindness without toggling blindness */
		if (talk) {
			if (!haseyes(youmonst.data)) {
				strange_feeling(NULL, NULL);
			} else if (Blindfolded) {
				strcpy(buf, body_part(EYE));
				eyecnt = eyecount(youmonst.data);
				pline("Your %s momentarily %s.", (eyecnt == 1) ? buf : makeplural(buf),
				      (eyecnt == 1) ? "twitches" : "twitch");
			} else { /* Eyes of the Overworld */
				pline("Your vision seems to dim for a moment but is %s now.",
				      Hallucination ? "happier" : "normal");
			}
		}
	}

	set_itimeout(&Blinded, xtime);

	if (u_could_see ^ can_see_now) { /* one or the other but not both */
		context.botl = 1;
		vision_full_recalc = 1; /* blindness just got toggled */
		vision_recalc(0);
		if (Blind_telepat || Infravision) see_monsters();
	}
}

/* xtime is nonzero if this is an attempt to turn on hallucination */
/* mask is nonzero if resistance status should change by mask */
boolean make_hallucinated(long xtime, boolean talk, long mask) {
	long old = HHallucination;
	boolean changed = 0;
	const char *message, *verb;

	message = (!xtime) ? "Everything %s SO boring now." :
			     "Oh wow!  Everything %s so cosmic!";
	verb = (!Blind) ? "looks" : "feels";

	if (mask) {
		if (HHallucination) changed = true;

		if (!xtime)
			EHalluc_resistance |= mask;
		else
			EHalluc_resistance &= ~mask;
	} else {
		if (!EHalluc_resistance && (!!HHallucination != !!xtime))
			changed = true;
		set_itimeout(&HHallucination, xtime);

		/* clearing temporary hallucination without toggling vision */
		if (!changed && !HHallucination && old && talk) {
			if (!haseyes(youmonst.data)) {
				strange_feeling(NULL, NULL);
			} else if (Blind) {
				char buf[BUFSZ];
				int eyecnt = eyecount(youmonst.data);

				strcpy(buf, body_part(EYE));
				pline("Your %s momentarily %s.", (eyecnt == 1) ? buf : makeplural(buf),
				      (eyecnt == 1) ? "itches" : "itch");
			} else { /* Grayswandir */
				pline("Your vision seems to flatten for a moment but is normal now.");
			}
		}
	}

	if (changed) {
		if (u.uswallow) {
			swallowed(0); /* redraw swallow display */
		} else {
			/* The see_* routines should be called *before* the pline. */
			see_monsters();
			see_objects();
			see_traps();
		}

		/* for perm_inv and anything similar
		(eg. Qt windowport's equipped items display) */
		update_inventory();

		context.botl = 1;
		if (talk) pline(message, verb);
	}
	return changed;
}

static void ghost_from_bottle() {
	struct monst *mtmp = makemon(&mons[PM_GHOST], u.ux, u.uy, NO_MM_FLAGS);

	if (!mtmp) {
		pline("This bottle turns out to be empty.");
		return;
	}
	if (Blind) {
		pline("As you open the bottle, something emerges.");
		return;
	}
	pline("As you open the bottle, an enormous %s emerges!",
	      Hallucination ? rndmonnam() : "ghost");
	if (flags.verbose)
		pline("You are frightened to death, and unable to move.");
	nomul(-3);
	nomovemsg = "You regain your composure.";
}

/* "Quaffing is like drinking, except you spill more."  -- Terry Pratchett
 */
int dodrink(void) {
	struct obj *otmp;
	const char *potion_descr;
	char quaffables[SIZE(beverages) + 2];
	char *qp = quaffables;

	if (Strangled) {
		pline("If you can't breathe air, how can you drink liquid?");
		return 0;
	}

	*qp++ = ALLOW_FLOOROBJ;
	if (!u.uswallow && (IS_FOUNTAIN(levl[u.ux][u.uy].typ) ||
			    IS_SINK(levl[u.ux][u.uy].typ) ||
			    IS_TOILET(levl[u.ux][u.uy].typ) ||
			    Underwater || IS_POOL(levl[u.ux][u.uy].typ)))
		*qp++ = ALLOW_THISPLACE;
	strcpy(qp, beverages);

	otmp = getobj(quaffables, "drink");
	if (otmp == &thisplace) {
		if (IS_FOUNTAIN(levl[u.ux][u.uy].typ)) {
			drinkfountain();
			return 1;
		} else if (IS_SINK(levl[u.ux][u.uy].typ)) {
			drinksink();
			return 1;
		} else if (IS_TOILET(levl[u.ux][u.uy].typ)) {
			drinktoilet();
			return 1;
		}
		pline("Do you know what lives in this water!");
		return 1;
	}
	if (!otmp) return 0;
	otmp->in_use = true; /* you've opened the stopper */

#define POTION_OCCUPANT_CHANCE(n) (13 + 2 * (n)) /* also in muse.c */

	potion_descr = OBJ_DESCR(objects[otmp->otyp]);
	if (potion_descr) {
		if (!strcmp(potion_descr, "milky") &&
		    !(mvitals[PM_GHOST].mvflags & G_GONE) &&
		    !rn2(POTION_OCCUPANT_CHANCE(mvitals[PM_GHOST].born))) {
			ghost_from_bottle();
			if (carried(otmp))
				useup(otmp);
			else
				useupf(otmp, 1L);
			return 1;
		} else if (!strcmp(potion_descr, "smoky") &&
			   !(mvitals[PM_DJINNI].mvflags & G_GONE) &&
			   !rn2(POTION_OCCUPANT_CHANCE(mvitals[PM_DJINNI].born))) {
			djinni_from_bottle(otmp);
			if (carried(otmp))
				useup(otmp);
			else
				useupf(otmp, 1L);
			return 1;
		}
	}
	return dopotion(otmp);
}

int dopotion(struct obj *otmp) {
	int retval;

	otmp->in_use = true;
	nothing = unkn = 0;

	if ((retval = peffects(otmp)) >= 0) return retval;

	if (nothing) {
		unkn++;
		pline("You have a %s feeling for a moment, then it passes.",
		      Hallucination ? "normal" : "peculiar");
	}
	if (otmp->dknown && !objects[otmp->otyp].oc_name_known) {
		if (!unkn) {
			makeknown(otmp->otyp);
			more_experienced(0, 10);
		} else if (!objects[otmp->otyp].oc_uname)
			docall(otmp);
	}
	if (carried(otmp))
		useup(otmp);
	else if (mcarried(otmp))
		m_useup(otmp->ocarry, otmp);
	else if (otmp->where == OBJ_FLOOR)
		useupf(otmp, 1L);
	else
		dealloc_obj(otmp); /* Dummy potion */
	return 1;
}

/* return -1 if potion is used up,  0 if error,  1 not used */
int peffects(struct obj *otmp) {
	int i, ii, lim;

	switch (otmp->otyp) {
		case POT_RESTORE_ABILITY:
		case SPE_RESTORE_ABILITY:
			unkn++;
			if (otmp->cursed) {
				pline("Ulch!  This makes you feel mediocre!");
				break;
			} else {
				pline("Wow!  This makes you feel %s!",
				      (otmp->blessed) ?
					      (unfixable_trouble_count(false) ? "better" : "great") :
					      "good");
				i = rn2(A_MAX); /* start at a random point */
				for (ii = 0; ii < A_MAX; ii++) {
					lim = AMAX(i);
					if (i == A_STR && u.uhs >= 3) --lim; /* WEAK */
					if (ABASE(i) < lim) {
						ABASE(i) = lim;
						context.botl = 1;
						/* only first found if not blessed */
						if (!otmp->blessed) break;
					}
					if (++i >= A_MAX) i = 0;
				}
			}
			break;
		case POT_HALLUCINATION:
			if (Hallucination || Halluc_resistance)
				nothing++;
			else
				makeknown(otmp->otyp);
			make_hallucinated(itimeout_incr(HHallucination, rn1(50, otmp->blessed ? 55 : otmp->cursed ? 155 : 105)), true, 0L);
			break;
		case POT_AMNESIA:
			pline(Hallucination ? "This tastes like champagne!" :
					      "This liquid bubbles and fizzes as you drink it.");
			forget((!otmp->blessed ? ALL_SPELLS : 0) | ALL_MAP);
			if (Hallucination)
				pline("Hakuna matata!");
			else
				pline("You feel your memories dissolve.");

			/* Blessed amnesia makes you forget lycanthropy, sickness */
			if (otmp->blessed) {
				if (u.ulycn >= LOW_PM && !Race_if(PM_HUMAN_WEREWOLF)) {
					pline("You forget your affinity to %s!",
					      makeplural(mons[u.ulycn].mname));
					if (youmonst.data == &mons[u.ulycn])
						you_unwere(false);
					u.ulycn = NON_PM; /* cure lycanthropy */
				}
				make_sick(0L, NULL, true, SICK_ALL);

				/* You feel refreshed */
				u.uhunger += 50 + rnd(50);
				newuhs(false);
			} else
				exercise(A_WIS, false);
			break;
		case POT_WATER:
			if (!otmp->blessed && !otmp->cursed) {
				pline("This tastes like water.");
				u.uhunger += rnd(10);
				newuhs(false);
				break;
			}
			unkn++;
			if (is_undead(youmonst.data) || is_demon(youmonst.data) ||
			    u.ualign.type == A_CHAOTIC) {
				if (otmp->blessed) {
					pline("This burns like acid!");
					exercise(A_CON, false);
					if (u.ulycn >= LOW_PM && !Race_if(PM_HUMAN_WEREWOLF)) {
						pline("Your affinity to %s disappears!",
						      makeplural(mons[u.ulycn].mname));
						if (youmonst.data == &mons[u.ulycn])
							you_unwere(false);
						u.ulycn = NON_PM; /* cure lycanthropy */
					}
					losehp(Maybe_Half_Phys(d(6, 6)), "potion of holy water", KILLED_BY_AN);
				} else if (otmp->cursed) {
					pline("You feel quite proud of yourself.");
					healup(d(6, 6), 0, 0, 0);
					if (u.ulycn >= LOW_PM && !Upolyd) you_were();
					exercise(A_CON, true);
				}
			} else {
				if (otmp->blessed) {
					pline("You feel full of awe.");
					if (u.ualign.type == A_LAWFUL) healup(d(6, 6), 0, 0, 0);
					make_sick(0L, NULL, true, SICK_ALL);
					exercise(A_WIS, true);
					exercise(A_CON, true);
					if (u.ulycn >= LOW_PM && !Race_if(PM_HUMAN_WEREWOLF)) {
						you_unwere(true); /* "Purified" */
					}
					/* make_confused(0L,true); */
				} else {
					if (u.ualign.type == A_LAWFUL) {
						pline("This burns like acid!");
						losehp(Maybe_Half_Phys(d(6, 6)), "potion of unholy water", KILLED_BY_AN);
					} else
						pline("You feel full of dread.");
					if (u.ulycn >= LOW_PM && !Upolyd) you_were();
					exercise(A_CON, false);
				}
			}
			break;
		case POT_BOOZE:
			unkn++;
			pline("Ooph!  This tastes like %s%s!",
			      otmp->odiluted ? "watered down " : "",
			      Hallucination ? "dandelion wine" : "liquid fire");
			if (!otmp->blessed)
				make_confused(itimeout_incr(HConfusion, d(3, 8)), false);
			/* the whiskey makes us feel better */
			if (!otmp->odiluted) healup(1, 0, false, false);
			u.uhunger += 10 * (2 + bcsign(otmp));
			newuhs(false);
			exercise(A_WIS, false);
			if (otmp->cursed) {
				pline("You pass out.");
				multi = -rnd(15);
				nomovemsg = "You awake with a headache.";
			}
			break;
		case POT_ENLIGHTENMENT:
			if (otmp->cursed) {
				unkn++;
				pline("You have an uneasy feeling...");
				exercise(A_WIS, false);
			} else {
				if (otmp->blessed) {
					adjattrib(A_INT, 1, false);
					adjattrib(A_WIS, 1, false);
				}
				pline("You feel self-knowledgeable...");
				display_nhwindow(WIN_MESSAGE, false);
				enlightenment(0);
				pline("The feeling subsides.");
				exercise(A_WIS, true);
			}
			break;
		case SPE_INVISIBILITY:
			/* spell cannot penetrate mummy wrapping */
			if (BInvis && uarmc->otyp == MUMMY_WRAPPING) {
				pline("You feel rather itchy under %s.", yname(uarmc));
				break;
			}
		fallthru;
		case POT_INVISIBILITY:
			if (Invis || Blind || BInvis) {
				nothing++;
			} else {
				self_invis_message();
			}
			if (otmp->blessed)
				HInvis |= FROMOUTSIDE;
			else
				incr_itimeout(&HInvis, rn1(15, 31));
			newsym(u.ux, u.uy); /* update position */
			if (otmp->cursed) {
				pline("For some reason, you feel your presence is known.");
				aggravate();
			}
			break;
		case POT_SEE_INVISIBLE:
		/* tastes like fruit juice in Rogue */
		case POT_FRUIT_JUICE: {
			int msg = Invisible && !Blind;

			unkn++;
			if (otmp->cursed)
				pline("Yecch!  This tastes %s.",
				      Hallucination ? "overripe" : "rotten");
			else
				pline(Hallucination ?
					      "This tastes like 10%% real %s%s all-natural beverage." :
					      "This tastes like %s%s.",
				      otmp->odiluted ? "reconstituted " : "",
				      fruitname(true));
			if (otmp->otyp == POT_FRUIT_JUICE) {
				u.uhunger += (otmp->odiluted ? 5 : 10) * (2 + bcsign(otmp));
				newuhs(false);
				break;
			}
			if (!otmp->cursed) {
				/* Tell them they can see again immediately, which
			 * will help them identify the potion...
			 */
				make_blinded(0L, true);
			}
			if (otmp->blessed)
				HSee_invisible |= FROMOUTSIDE;
			else
				incr_itimeout(&HSee_invisible, rn1(100, 750));
			set_mimic_blocking(); /* do special mimic handling */
			see_monsters();	      /* see invisible monsters */
			newsym(u.ux, u.uy);   /* see yourself! */
			if (msg && !Blind) {  /* Blind possible if polymorphed */
				pline("You can see through yourself, but you are visible!");
				unkn--;
			}
			break;
		}
		case POT_PARALYSIS:
			if (Free_action)
				pline("You stiffen momentarily.");
			else {
				if (Levitation || Is_airlevel(&u.uz) || Is_waterlevel(&u.uz))
					pline("You are motionlessly suspended.");
				else if (u.usteed)
					pline("You are frozen in place!");
				else
					pline("Your %s are frozen to the %s!",
					      makeplural(body_part(FOOT)), surface(u.ux, u.uy));
				nomul(-(rn1(10, 25 - 12 * bcsign(otmp))));
				nomovemsg = "You can move again.";
				exercise(A_DEX, false);
			}
			break;
		case POT_SLEEPING:
			if (Sleep_resistance || Free_action)
				pline("You yawn.");
			else {
				pline("You suddenly fall asleep!");
				fall_asleep(-rn1(10, 25 - 12 * bcsign(otmp)), true);
			}
			break;
		case POT_MONSTER_DETECTION:
		case SPE_DETECT_MONSTERS:
			if (otmp->blessed) {
				int x, y;

				if (Detect_monsters) nothing++;
				unkn++;
				/* after a while, repeated uses become less effective */
				if (HDetect_monsters >= 300L)
					i = 1;
				else
					i = rn1(40, 21);
				incr_itimeout(&HDetect_monsters, i);
				for (x = 1; x < COLNO; x++) {
					for (y = 0; y < ROWNO; y++) {
						if (memory_is_invisible(x, y)) {
							unmap_object(x, y);
							newsym(x, y);
						}
						if (MON_AT(x, y)) unkn = 0;
					}
				}
				see_monsters();
				if (unkn) pline("You feel lonely.");
				break;
			}
			if (monster_detect(otmp, 0))
				return 1; /* nothing detected */
			exercise(A_WIS, true);
			break;
		case POT_OBJECT_DETECTION:
		case SPE_DETECT_TREASURE:
			if (object_detect(otmp, 0))
				return 1; /* nothing detected */
			exercise(A_WIS, true);
			break;
		case POT_SICKNESS:
			pline("Yecch!  This stuff tastes like poison.");
			if (otmp->blessed) {
				pline("(But in fact it was mildly stale %s.)", fruitname(true));
				if (!Role_if(PM_HEALER) && !Poison_resistance) {
					/* NB: blessed otmp->fromsink is not possible */
					losehp(1, "mildly contaminated potion", KILLED_BY_AN);
				}
			} else {
				if (Poison_resistance)
					pline("(But in fact it was biologically contaminated %s.)",
						fruitname(true));
				if (Role_if(PM_HEALER)) {
					pline("Fortunately, you have been immunized.");
				} else {
					int typ = rn2(A_MAX);

					if (!Fixed_abil) {
						poisontell(typ);
						adjattrib(typ,
							  Poison_resistance ? -1 : -rn1(4, 3),
							  true);
					}
					if (!Poison_resistance) {
						if (otmp->fromsink)
							losehp(rnd(10) + 5 * !!(otmp->cursed),
							       "contaminated tap water", KILLED_BY);
						else
							losehp(rnd(10) + 5 * !!(otmp->cursed),
							       "contaminated potion", KILLED_BY_AN);
					}
					exercise(A_CON, false);
				}
			}
			if (Hallucination) {
				pline("You are shocked back to your senses!");
				make_hallucinated(0L, false, 0L);
			}
			break;
		case POT_CONFUSION:
			if (!Confusion) {
				if (Hallucination) {
					pline("What a trippy feeling!");
					unkn++;
				} else
					pline("Huh, What?  Where am I?");
			} else
				nothing++;
			make_confused(itimeout_incr(HConfusion,
						    rn1(7, 16 - 8 * bcsign(otmp))),
				      false);
			break;
		case POT_CLAIRVOYANCE:
			/* KMH -- handle cursed, blessed, blocked */
			if (otmp->cursed)
				nothing++;
			else if (!BClairvoyant) {
				if (Hallucination) pline("Dude! See-through walls!");
				do_vicinity_map();
			}
			if (otmp->blessed)
				incr_itimeout(&HClairvoyant, rn1(50, 100));
			break;
		case POT_ESP: {
			const char *mod;

			/* KMH -- handle cursed, blessed */
			if (otmp->cursed) {
				if (HTelepat)
					mod = "less ";
				else {
					unkn++;
					mod = NULL;
				}
				HTelepat = 0;
			} else if (otmp->blessed) {
				mod = "fully ";
				incr_itimeout(&HTelepat, rn1(100, 200));
				HTelepat |= FROMOUTSIDE;
			} else {
				mod = "more ";
				incr_itimeout(&HTelepat, rn1(50, 100));
			}
			if (mod)
				pline(Hallucination ?
					      "You feel %sin touch with the cosmos." :
					      "You feel %smentally acute.",
				      mod);
			see_monsters();
			break;
		}
		/* KMH, balance patch -- removed
	case POT_FIRE_RESISTANCE:
	       if(!(HFire_resistance & FROMOUTSIDE)) {
		if (Hallucination)
		   pline("You feel, like, totally cool!");
		   else pline("You feel cooler.");
		   HFire_resistance += rn1(100,50);
		   unkn++;
		   HFire_resistance |= FROMOUTSIDE;
		}
		break;*/
		case POT_INVULNERABILITY:
			incr_itimeout(&Invulnerable, rn1(4, 8 + 4 * bcsign(otmp)));
			pline(Hallucination ? "You feel like a super-duper hero!" : "You feel invulnerable!");
			break;
		case POT_GAIN_ABILITY:
			if (otmp->cursed) {
				pline("Ulch!  That potion tasted foul!");
				unkn++;
			} else if (Fixed_abil) {
				nothing++;
			} else {	  /* If blessed, increase all; if not, try up to */
				int itmp; /* 6 times to find one which can be increased. */
				i = -1;	  /* increment to 0 */
				for (ii = A_MAX; ii > 0; ii--) {
					i = (otmp->blessed ? i + 1 : rn2(A_MAX));
					/* only give "your X is already as high as it can get"
				   message on last attempt (except blessed potions) */
					itmp = (otmp->blessed || ii == 1) ? 0 : -1;
					if (adjattrib(i, 1, itmp) && !otmp->blessed)
						break;
				}
			}
			break;
		case POT_SPEED:
			// heal_legs() would heal steeds legs */
			if (Wounded_legs && !otmp->cursed && !u.usteed) {
				heal_legs();
				unkn++;
				break;
			}
		fallthru;
		case SPE_HASTE_SELF:
			if (!Very_fast)
				pline("You are suddenly moving %sfaster.",
				      Fast ? "" : "much ");
			else {
				pline("Your %s get new energy.",
				      makeplural(body_part(LEG)));
				unkn++;
			}
			exercise(A_DEX, true);
			incr_itimeout(&HFast, rn1(10, 100 + 60 * bcsign(otmp)));
			break;
		case POT_BLINDNESS:
			if (Blind) nothing++;
			make_blinded(itimeout_incr(Blinded,
						   rn1(200, 250 - 125 * bcsign(otmp))),
				     (boolean)!Blind);
			break;

		case POT_GAIN_LEVEL:
			if (otmp->cursed) {
				unkn++;
				/* they went up a level */
				if ((ledger_no(&u.uz) == 1 && u.uhave.amulet) ||
				    Can_rise_up(u.ux, u.uy, &u.uz)) {
					/* [ALI] Special handling for quaffing potions
				 * off the floor (otmp won't be valid after
				 * we change levels otherwise).
				 */
					if (otmp->where == OBJ_FLOOR) {
						if (otmp->quan > 1)
							splitobj(otmp, 1);
						/* Make sure you're charged if in shop */
						otmp->quan++;
						useupf(otmp, 1);
						obj_extract_self(otmp);
					}
					if (ledger_no(&u.uz) == 1) {
						pline("You rise up, through the %s!", ceiling(u.ux, u.uy));
						goto_level(&earth_level, false, false, false);
					} else {
						int newlev = depth(&u.uz) - 1;
						d_level newlevel;

						get_level(&newlevel, newlev);
						if (on_level(&newlevel, &u.uz)) {
							pline("It tasted bad.");
							break;
						} else
							pline("You rise up, through the %s!", ceiling(u.ux, u.uy));
						goto_level(&newlevel, false, false, false);
					}
				} else
					pline("You have an uneasy feeling.");
				break;
			}
			pluslvl(false);
			if (otmp->blessed)
				/* blessed potions place you at a random spot in the
			 * middle of the new level instead of the low point
			 */
				u.uexp = rndexp(true);
			break;
		case POT_HEALING:
			pline("You feel better.");
			healup(d(5, 6) + 5 * bcsign(otmp),
			       !otmp->cursed ? 1 : 0, 1 + 1 * !!otmp->blessed, !otmp->cursed);
			exercise(A_CON, true);
			break;
		case POT_EXTRA_HEALING:
			pline("You feel much better.");
			healup(d(6, 8) + 5 * bcsign(otmp),
			       otmp->blessed ? 5 : !otmp->cursed ? 2 : 0,
			       !otmp->cursed, true);
			make_hallucinated(0L, true, 0L);
			exercise(A_CON, true);
			exercise(A_STR, true);
			break;
		case POT_FULL_HEALING:
			pline("You feel completely healed.");
			healup(400, 4 + 4 * bcsign(otmp), !otmp->cursed, true);
			/* Restore one lost level if blessed */
			if (otmp->blessed && u.ulevel < u.ulevelmax) {
				/* when multiple levels have been lost, drinking
			   multiple potions will only get half of them back */
				u.ulevelmax -= 1;
				pluslvl(false);
			}
			make_hallucinated(0L, true, 0L);
			exercise(A_STR, true);
			exercise(A_CON, true);
			break;
		case POT_LEVITATION:
		case SPE_LEVITATION:
			if (otmp->cursed) HLevitation &= ~I_SPECIAL;
			if (!Levitation) {
				/* kludge to ensure proper operation of float_up() */
				HLevitation = 1;
				float_up();
				/* reverse kludge */
				HLevitation = 0;
				if (otmp->cursed && !Is_waterlevel(&u.uz)) {
					if ((u.ux != xupstair || u.uy != yupstair) && (u.ux != sstairs.sx || u.uy != sstairs.sy || !sstairs.up) && (!xupladder || u.ux != xupladder || u.uy != yupladder)) {
						pline("You hit your %s on the %s.",
						      body_part(HEAD),
						      ceiling(u.ux, u.uy));
						losehp(Maybe_Half_Phys(uarmh ? 1 : rnd(10)), "colliding with the ceiling", KILLED_BY);
					} else
						doup();
				}
			} else
				nothing++;
			if (otmp->blessed) {
				incr_itimeout(&HLevitation, rn1(50, 250));
				HLevitation |= I_SPECIAL;
			} else
				incr_itimeout(&HLevitation, rn1(140, 10));
			spoteffects(false); /* for sinks */
			break;
		case POT_GAIN_ENERGY: { /* M. Stephenson */
			int num, num2;
			if (otmp->cursed)
				pline("You feel lackluster.");
			else
				pline("Magical energies course through your body.");
			num = rnd(25) + 5 * otmp->blessed + 10;
			num2 = rnd(2) + 2 * otmp->blessed + 1;
			u.uenmax += (otmp->cursed) ? -num2 : num2;
			u.uen += (otmp->cursed) ? -num : num;
			if (u.uenmax <= 0) u.uenmax = 0;
			if (u.uen <= 0) u.uen = 0;
			if (u.uen > u.uenmax) {
				u.uenmax += ((u.uen - u.uenmax) / 2);
				u.uen = u.uenmax;
			}
			context.botl = 1;
			exercise(A_WIS, true);
		} break;
		case POT_OIL: { /* P. Winner */
			boolean good_for_you = false;

			if (otmp->lamplit) {
				if (likes_fire(youmonst.data)) {
					pline("Ahh, a refreshing drink.");
					good_for_you = true;
				} else {
					pline("You burn your %s.", body_part(FACE));
					losehp(d(Fire_resistance ? 1 : 3, 4), "burning potion of oil", KILLED_BY_AN);
				}
			} else if (otmp->cursed)
				pline("This tastes like castor oil.");
			else
				pline("That was smooth!");
			exercise(A_WIS, good_for_you);
		} break;
		case POT_ACID:
			if (Acid_resistance)
				/* Not necessarily a creature who _likes_ acid */
				pline("This tastes %s.", Hallucination ? "tangy" : "sour");
			else {
				pline("This burns%s!", otmp->blessed ? " a little" :
								       otmp->cursed ? " a lot" : " like acid");
				losehp(Maybe_Half_Phys(d(otmp->cursed ? 2 : 1, otmp->blessed ? 4 : 8)), "potion of acid", KILLED_BY_AN);
				exercise(A_CON, false);
			}
			if (Stoned) fix_petrification();
			unkn++; /* holy/unholy water can burn like acid too */
			break;
		case POT_POLYMORPH:
			pline("You feel a little %s.", Hallucination ? "normal" : "strange");
			if (!Unchanging) polyself(0);
			break;
		case POT_BLOOD:
		case POT_VAMPIRE_BLOOD:
			unkn++;
			u.uconduct.unvegan++;
			if (maybe_polyd(is_vampire(youmonst.data), Race_if(PM_VAMPIRE))) {
				violated_vegetarian();
				if (otmp->cursed)
					pline("Yecch!  This %s.", Hallucination ?
									  "liquid could do with a good stir" :
									  "blood has congealed");
				else
					pline(Hallucination ?
						      "The %s liquid stirs memories of home." :
						      "The %s blood tastes delicious.",
					      otmp->odiluted ? "watery" : "thick");
				if (!otmp->cursed)
					lesshungry((otmp->odiluted ? 1 : 2) *
						   (otmp->otyp == POT_VAMPIRE_BLOOD ? 400 :
										      otmp->blessed ? 15 : 10));
				if (otmp->otyp == POT_VAMPIRE_BLOOD && otmp->blessed) {
					int num = newhp();
					if (Upolyd) {
						u.mhmax += num;
						u.mh += num;
					} else {
						u.uhpmax += num;
						u.uhp += num;
					}
				}
			} else if (otmp->otyp == POT_VAMPIRE_BLOOD) {
				/* [CWC] fix conducts for potions of (vampire) blood -
			   doesn't use violated_vegetarian() to prevent
			   duplicated "you feel guilty" messages */
				u.uconduct.unvegetarian++;
				if (u.ualign.type == A_LAWFUL || Role_if(PM_MONK)) {
					pline("You feel %sguilty about drinking such a vile liquid.",
					      Role_if(PM_MONK) ? "especially " : "");
					u.ugangr++;
					adjalign(-15);
				} else if (u.ualign.type == A_NEUTRAL)
					adjalign(-3);
				exercise(A_CON, false);
				if (!Unchanging && polymon(PM_VAMPIRE))
					u.mtimedone = 0; /* "Permament" change */
			} else {
				violated_vegetarian();
				pline("Ugh.  That was vile.");
				make_vomiting(Vomiting + d(10, 8), true);
			}
			break;
		default:
			impossible("What a funny potion! (%u)", otmp->otyp);
			return 0;
	}
	return -1;
}

void healup(int nhp, int nxtra, boolean curesick, boolean cureblind) {
	if (nhp) {
		if (Upolyd) {
			u.mh += nhp;
			if (u.mh > u.mhmax) u.mh = (u.mhmax += nxtra);
		} else {
			u.uhp += nhp;
			if (u.uhp > u.uhpmax) u.uhp = (u.uhpmax += nxtra);
		}
	}
	if (cureblind) make_blinded(0L, true);
	if (curesick) make_sick(0L, NULL, true, SICK_ALL);
	context.botl = 1;
	return;
}

void healup_mon(struct monst *mtmp, int nhp, int nxtra, boolean curesick, boolean cureblind) {
	if (nhp) {
		mtmp->mhp += nhp;
		if (mtmp->mhp > mtmp->mhpmax) mtmp->mhp = (mtmp->mhpmax += nxtra);
	}
	//if (cureblind) ; /* NOT DONE YET */
	//if (curesick)  ; /* NOT DONE YET */
	return;
}

void strange_feeling(struct obj *obj, const char *txt) {
	if (flags.beginner || !txt)
		pline("You have a %s feeling for a moment, then it passes.",
		      Hallucination ? "normal" : "strange");
	else
		plines(txt);

	if (!obj) /* e.g., crystal ball finds no traps */
		return;

	if (obj->dknown && !objects[obj->otyp].oc_name_known &&
	    !objects[obj->otyp].oc_uname)
		docall(obj);
	if (carried(obj))
		useup(obj);
	else
		useupf(obj, 1L);
}

const char *bottlenames[] = {
	"bottle", "phial", "flagon", "carafe", "flask", "jar", "vial"};

const char *bottlename(void) {
	return bottlenames[rn2(SIZE(bottlenames))];
}

/* WAC -- monsters can throw potions around too! */
void potionhit(struct monst *mon, struct obj *obj, boolean your_fault) {
	const char *botlnam = bottlename();
	boolean isyou = (mon == &youmonst);
	int distance;

	if (isyou) {
		distance = 0;
		pline("The %s crashes on your %s and breaks into shards.",
		      botlnam, body_part(HEAD));
		losehp(Maybe_Half_Phys(rnd(2)), "thrown potion", KILLED_BY_AN);
	} else {
		distance = distu(mon->mx, mon->my);
		if (!cansee(mon->mx, mon->my))
			pline("Crash!");
		else {
			char *mnam = mon_nam(mon);
			char buf[BUFSZ];

			if (has_head(mon->data)) {
				sprintf(buf, "%s %s",
					s_suffix(mnam),
					(notonhead ? "body" : "head"));
			} else {
				strcpy(buf, mnam);
			}
			pline("The %s crashes on %s and breaks into shards.",
			      botlnam, buf);
		}
		if (rn2(5) && mon->mhp > 1)
			mon->mhp--;
	}

	/* oil doesn't instantly evaporate */
	if (obj->otyp != POT_OIL && cansee(mon->mx, mon->my))
		pline("%s.", Tobjnam(obj, "evaporate"));

	if (isyou) {
		switch (obj->otyp) {
			case POT_OIL:
				if (obj->lamplit)
					splatter_burning_oil(u.ux, u.uy);
				break;
			case POT_POLYMORPH:
				pline("You feel a little %s.", Hallucination ? "normal" : "strange");
				if (!Unchanging && !Antimagic) polyself(0);
				break;
			case POT_ACID:
				if (!Acid_resistance) {
					pline("This burns%s!", obj->blessed ? " a little" :
							       obj->cursed ? " a lot" : "");
					losehp(Maybe_Half_Phys(d(obj->cursed ? 2 : 1, obj->blessed ? 4 : 8)), "potion of acid", KILLED_BY_AN);
				}
				break;
			case POT_AMNESIA:
				/* Uh-oh! */
				if (uarmh && is_helmet(uarmh) &&
				    rn2(10 - (uarmh->cursed ? 8 : 0)))
					get_wet(uarmh, true);
				break;
		}
	} else {
		boolean angermon = true;

		if (!your_fault) angermon = false;
		switch (obj->otyp) {
			case POT_HEALING:
do_healing:
				if (mon->data == &mons[PM_PESTILENCE]) goto do_illness;
				angermon = false;
				if (canseemon(mon))
					pline("%s looks better.", Monnam(mon));
				healup_mon(mon, d(5, 6) + 5 * bcsign(obj),
					   !obj->cursed ? 1 : 0, 1 + 1 * !!obj->blessed, !obj->cursed);
				break;
			case POT_EXTRA_HEALING:
				if (mon->data == &mons[PM_PESTILENCE]) goto do_illness;
				angermon = false;
				if (canseemon(mon))
					pline("%s looks much better.", Monnam(mon));
				healup_mon(mon, d(6, 8) + 5 * bcsign(obj),
					   obj->blessed ? 5 : !obj->cursed ? 2 : 0,
					   !obj->cursed, true);
				break;
			case POT_FULL_HEALING:
				if (mon->data == &mons[PM_PESTILENCE]) goto do_illness;
			fallthru;
			case POT_RESTORE_ABILITY:
			case POT_GAIN_ABILITY:
				angermon = false;
				if (canseemon(mon))
					pline("%s looks sound and hale again.", Monnam(mon));
				healup_mon(mon, 400, 5 + 5 * !!(obj->blessed), !(obj->cursed), 1);
				break;
			case POT_SICKNESS:
				if (mon->data == &mons[PM_PESTILENCE]) goto do_healing;
				if (dmgtype(mon->data, AD_DISE) ||
				    dmgtype(mon->data, AD_PEST) || /* won't happen, see prior goto */
				    resists_poison(mon)) {
					if (canseemon(mon))
						pline("%s looks unharmed.", Monnam(mon));
					break;
				}
do_illness:
				if ((mon->mhpmax > 3) && !resist(mon, POTION_CLASS, 0, NOTELL))
					mon->mhpmax /= 2;
				if ((mon->mhp > 2) && !resist(mon, POTION_CLASS, 0, NOTELL))
					mon->mhp /= 2;
				if (mon->mhp > mon->mhpmax) mon->mhp = mon->mhpmax;
				if (canseemon(mon))
					pline("%s looks rather ill.", Monnam(mon));
				break;
			case POT_CONFUSION:
			case POT_BOOZE:
				if (!resist(mon, POTION_CLASS, 0, NOTELL)) mon->mconf = true;
				break;
			case POT_POLYMORPH:
				/* [Tom] polymorph potion thrown
			 * [max] added poor victim a chance to resist
			 * magic resistance protects from polymorph traps, so make
			 * it guard against involuntary polymorph attacks too...
			 */
				if (resists_magm(mon)) {
					shieldeff(mon->mx, mon->my);
				} else if (!resist(mon, POTION_CLASS, 0, NOTELL)) {
					mon_poly(mon, your_fault, "%s changes!");
					if (!Hallucination && canspotmon(mon))
						makeknown(POT_POLYMORPH);
				}
				break;
			case POT_INVISIBILITY:
				angermon = false;
				mon_set_minvis(mon);
				break;
			case POT_SLEEPING:
				/* wakeup() doesn't rouse victims of temporary sleep */
				if (sleep_monst(mon, rnd(12), POTION_CLASS)) {
					pline("%s falls asleep.", Monnam(mon));
					slept_monst(mon);
				}
				break;
			case POT_PARALYSIS:
				if (mon->mcanmove) {
					/* really should be rnd(5) for consistency with players
					 * breathing potions, but...
					 */
					paralyze_monst(mon, rnd(25));
				}
				break;
			case POT_SPEED:
				angermon = false;
				mon_adjust_speed(mon, 1, obj);
				break;
			case POT_BLINDNESS:
				if (haseyes(mon->data)) {
					int btmp = 64 + rn2(32) +
						   rn2(32) * !resist(mon, POTION_CLASS, 0, NOTELL);
					btmp += mon->mblinded;
					mon->mblinded = min(btmp, 127);
					mon->mcansee = 0;
				}
				break;
			case POT_WATER:
				if (is_undead(mon->data) || is_demon(mon->data) ||
				    is_were(mon->data) || is_vampshifter(mon)) {
					if (obj->blessed) {
						if (is_silent(mon->data)) {
							pline("%s writhes in pain!", Monnam(mon));
						} else {
							pline("%s shrieks in pain!", Monnam(mon));
							wake_nearto(mon->mx, mon->my, mon->data->mlevel * 10);
						}
						mon->mhp -= d(2, 6);
						if (mon->mhp < 1) {
							if (your_fault)
								killed(mon);
							else
								monkilled(mon, "", AD_ACID);
						} else if (is_were(mon->data) && !is_human(mon->data))
							new_were(mon); /* revert to human */
					} else if (obj->cursed) {
						angermon = false;
						if (canseemon(mon))
							pline("%s looks healthier.", Monnam(mon));
						mon->mhp += d(2, 6);
						if (mon->mhp > mon->mhpmax) mon->mhp = mon->mhpmax;
						if (is_were(mon->data) && is_human(mon->data) &&
						    !Protection_from_shape_changers)
							new_were(mon); /* transform into beast */
					}
				} else if (mon->data == &mons[PM_GREMLIN]) {
					angermon = false;
					split_mon(mon, NULL);
				} else if (mon->data == &mons[PM_FLAMING_SPHERE] ||
					   mon->data == &mons[PM_IRON_GOLEM]) {
					if (canseemon(mon))
						pline("%s %s.", Monnam(mon),
						      mon->data == &mons[PM_IRON_GOLEM] ?
							      "rusts" :
							      "flickers");
					mon->mhp -= d(1, 6);
					if (mon->mhp < 1) {
						if (your_fault)
							killed(mon);
						else
							monkilled(mon, "", AD_ACID);
					}
				}
				break;
			case POT_AMNESIA:
				switch (monsndx(mon->data)) {
					case PM_GREMLIN:
						/* Gremlins multiply... */
						mon->mtame = false;
						split_mon(mon, NULL);
						break;
					case PM_FLAMING_SPHERE:
					case PM_IRON_GOLEM:
						if (canseemon(mon)) pline("%s %s.", Monnam(mon),
									  monsndx(mon->data) == PM_IRON_GOLEM ?
										  "rusts" :
										  "flickers");
						mon->mhp -= d(1, 6);
						if (mon->mhp < 1)
							if (your_fault)
								killed(mon);
							else
								monkilled(mon, "", AD_ACID);
						else
							mon->mtame = false;
						break;
					case PM_WIZARD_OF_YENDOR:
						if (your_fault) {
							if (canseemon(mon))
								pline("%s laughs at you!", Monnam(mon));
							forget(1);
						}
						break;
					case PM_MEDUSA:
						if (canseemon(mon))
							pline("%s looks like %s's having a bad hair day!",
							      Monnam(mon), mhe(mon));
						break;
					case PM_CROESUS:
						if (canseemon(mon))
							pline("%s says: 'My gold! I must count my gold!'",
							      Monnam(mon));
						break;
					case PM_DEATH:
						if (canseemon(mon))
							pline("%s pauses, then looks at you thoughtfully!",
							      Monnam(mon));
						break;
					case PM_FAMINE:
						if (canseemon(mon))
							pline("%s looks unusually hungry!", Monnam(mon));
						break;
					case PM_PESTILENCE:
						if (canseemon(mon))
							pline("%s looks unusually well!", Monnam(mon));
						break;
					default:
						if (mon->data->msound == MS_NEMESIS && canseemon(mon) && your_fault)
							pline("%s curses your ancestors!", Monnam(mon));
						else if (mon->isshk) {
							angermon = false;
							if (canseemon(mon))
								pline("%s looks at you curiously!",
								      Monnam(mon));
							make_happy_shk(mon, false);
						} else if (!is_covetous(mon->data) && !rn2(4) &&
							   !resist(mon, POTION_CLASS, 0, 0)) {
							angermon = false;
							if (canseemon(mon)) {
								if (mon->msleeping) {
									wakeup(mon);
									pline("%s wakes up looking bewildered!",
									      Monnam(mon));
								} else
									pline("%s looks bewildered!", Monnam(mon));
								mon->mpeaceful = true;
								mon->mtame = false;
							}
						}
						break;
				}
				break;
			case POT_OIL:
				if (obj->lamplit)
					splatter_burning_oil(mon->mx, mon->my);
				break;
			/*
			case POT_GAIN_LEVEL:
			case POT_LEVITATION:
			case POT_FRUIT_JUICE:
			case POT_MONSTER_DETECTION:
			case POT_OBJECT_DETECTION:
				break;
		*/
			/* KMH, balance patch -- added */
			case POT_ACID:
				if (!resists_acid(mon) && !resist(mon, POTION_CLASS, 0, NOTELL)) {
					if (is_silent(mon->data)) {
						pline("%s writhes in pain!", Monnam(mon));
					} else {
						pline("%s shrieks in pain!", Monnam(mon));
						wake_nearto(mon->mx, mon->my, mon->data->mlevel * 10);
					}
					mon->mhp -= d(obj->cursed ? 2 : 1, obj->blessed ? 4 : 8);
					if (mon->mhp < 1) {
						if (your_fault)
							killed(mon);
						else
							monkilled(mon, "", AD_ACID);
					}
				}
				break;
		}
		if (angermon)
			wakeup(mon);
		else
			mon->msleeping = 0;
	}

	/* Note: potionbreathe() does its own docall() */
	if ((distance == 0 || ((distance < 3) && rn2(5))) &&
	    (!breathless(youmonst.data) || haseyes(youmonst.data)))
		potionbreathe(obj);
	else if (obj->dknown && !objects[obj->otyp].oc_name_known &&
		 !objects[obj->otyp].oc_uname && cansee(mon->mx, mon->my))
		docall(obj);
	if (*u.ushops && obj->unpaid) {
		struct monst *shkp =
			shop_keeper(*in_rooms(u.ux, u.uy, SHOPBASE));

		if (!shkp)
			obj->unpaid = 0;
		else {
			stolen_value(obj, u.ux, u.uy,
				     (boolean)shkp->mpeaceful, false, true);
			subfrombill(obj, shkp);
		}
	}
	obfree(obj, NULL);
}

/* vapors are inhaled or get in your eyes */
void potionbreathe(struct obj *obj) {
	int i, ii, isdone, kn = 0;

	switch (obj->otyp) {
		case POT_RESTORE_ABILITY:
		case POT_GAIN_ABILITY:
			if (obj->cursed) {
				if (!breathless(youmonst.data))
					pline("Ulch!  That potion smells terrible!");
				else if (haseyes(youmonst.data)) {
					int numeyes = eyecount(youmonst.data);
					pline("Your %s sting%s!",
					      (numeyes == 1) ? body_part(EYE) : makeplural(body_part(EYE)),
					      (numeyes == 1) ? "s" : "");
				}
				break;
			} else {
				i = rn2(A_MAX); /* start at a random point */
				for (isdone = ii = 0; !isdone && ii < A_MAX; ii++) {
					if (ABASE(i) < AMAX(i)) {
						ABASE(i)
						++;
						/* only first found if not blessed */
						isdone = !(obj->blessed);
						context.botl = 1;
					}
					if (++i >= A_MAX) i = 0;
				}
			}
			break;
		case POT_FULL_HEALING:
			if (Upolyd && u.mh < u.mhmax) u.mh++, context.botl = 1;
			if (u.uhp < u.uhpmax) u.uhp++, context.botl = 1;
		fallthru;
		case POT_EXTRA_HEALING:
			if (Upolyd && u.mh < u.mhmax) u.mh++, context.botl = 1;
			if (u.uhp < u.uhpmax) u.uhp++, context.botl = 1;
		fallthru;
		case POT_HEALING:
			if (Upolyd && u.mh < u.mhmax) u.mh++, context.botl = 1;
			if (u.uhp < u.uhpmax) u.uhp++, context.botl = 1;
			exercise(A_CON, true);
			break;
		case POT_SICKNESS:
			if (!Role_if(PM_HEALER)) {
				if (Upolyd) {
					if (u.mh <= 5)
						u.mh = 1;
					else
						u.mh -= 5;
				} else {
					if (u.uhp <= 5)
						u.uhp = 1;
					else
						u.uhp -= 5;
				}
				context.botl = 1;
				exercise(A_CON, false);
			}
			break;
		case POT_HALLUCINATION:
			pline("You have a momentary vision.");
			break;
		case POT_CONFUSION:
		case POT_BOOZE:
			if (!Confusion)
				pline("You feel somewhat dizzy.");
			make_confused(itimeout_incr(HConfusion, rnd(5)), false);
			break;
		case POT_INVISIBILITY:
			if (!Blind && !Invis) {
				kn++;
				pline("For an instant you %s!",
				      See_invisible ? "could see right through yourself" : "couldn't see yourself");
			}
			break;
		case POT_PARALYSIS:
			kn++;
			if (!Free_action) {
				pline("Something seems to be holding you.");
				nomul(-rnd(5));
				nomovemsg = "You can move again.";
				exercise(A_DEX, false);
			} else
				pline("You stiffen momentarily.");
			break;
		case POT_SLEEPING:
			kn++;
			if (!Free_action && !Sleep_resistance) {
				pline("You feel rather tired.");
				nomul(-rnd(5));
				nomovemsg = "You can move again.";
				exercise(A_DEX, false);
			} else
				pline("You yawn.");
			break;
		case POT_SPEED:
			if (!Fast) pline("Your knees seem more flexible now.");
			incr_itimeout(&HFast, rnd(5));
			exercise(A_DEX, true);
			break;
		case POT_BLINDNESS:
			if (!Blind && !u.usleep) {
				kn++;
				pline("It suddenly gets dark.");
			}
			make_blinded(itimeout_incr(Blinded, rnd(5)), false);
			if (!Blind && !u.usleep) pline("Your vision quickly clears.");
			break;
		case POT_WATER:
			if (u.umonnum == PM_GREMLIN) {
				split_mon(&youmonst, NULL);
			} else if (u.ulycn >= LOW_PM) {
				/* vapor from [un]holy water will trigger
				   transformation but won't cure lycanthropy */
				if (obj->blessed && youmonst.data == &mons[u.ulycn])
					you_unwere(false);
				else if (obj->cursed && !Upolyd)
					you_were();
			}
			break;
		case POT_AMNESIA:
			if (u.umonnum == PM_GREMLIN)
				split_mon(&youmonst, NULL);
			else if (u.umonnum == PM_FLAMING_SPHERE) {
				pline("You flicker!");
				losehp(d(1, 6), "potion of amnesia", KILLED_BY_AN);
			} else if (u.umonnum == PM_IRON_GOLEM) {
				pline("You rust!");
				losehp(d(1, 6), "potion of amnesia", KILLED_BY_AN);
			}
			pline("You feel dizzy!");
			forget(1 + rn2(5));
			break;
		case POT_ACID:
		case POT_POLYMORPH:
			exercise(A_CON, false);
			break;
		case POT_BLOOD:
		case POT_VAMPIRE_BLOOD:
			if (maybe_polyd(is_vampire(youmonst.data), Race_if(PM_VAMPIRE))) {
				exercise(A_WIS, false);
				pline("You feel a %ssense of loss.",
				      obj->otyp == POT_VAMPIRE_BLOOD ? "terrible " : "");
			} else
				exercise(A_CON, false);
			break;
			/*
			case POT_GAIN_LEVEL:
			case POT_LEVITATION:
			case POT_FRUIT_JUICE:
			case POT_MONSTER_DETECTION:
			case POT_OBJECT_DETECTION:
			case POT_OIL:
				break;
		*/
	}
	/* note: no obfree() */
	if (obj->dknown) {
		if (kn)
			makeknown(obj->otyp);
		else if (!objects[obj->otyp].oc_name_known &&
			 !objects[obj->otyp].oc_uname && !Blind)
			docall(obj);
	}
}

/* returns the potion type when o1 is dipped in o2 */
static short mixtype(struct obj *o1, struct obj *o2) {
	/* cut down on the number of cases below */
	if (o1->oclass == POTION_CLASS &&
	    (o2->otyp == POT_GAIN_LEVEL ||
	     o2->otyp == POT_GAIN_ENERGY ||
	     o2->otyp == POT_HEALING ||
	     o2->otyp == POT_EXTRA_HEALING ||
	     o2->otyp == POT_FULL_HEALING ||
	     o2->otyp == POT_ENLIGHTENMENT ||
	     o2->otyp == POT_FRUIT_JUICE)) {
		struct obj *swp;

		swp = o1;
		o1 = o2;
		o2 = swp;
	}

	switch (o1->otyp) {
		case POT_HEALING:
			switch (o2->otyp) {
				case POT_SPEED:
				case POT_GAIN_LEVEL:
				case POT_GAIN_ENERGY:
					return POT_EXTRA_HEALING;
			}
			break;
		case POT_EXTRA_HEALING:
			switch (o2->otyp) {
				case POT_GAIN_LEVEL:
				case POT_GAIN_ENERGY:
					return POT_FULL_HEALING;
			}
			break;
		case POT_FULL_HEALING:
			switch (o2->otyp) {
				case POT_GAIN_LEVEL:
				case POT_GAIN_ENERGY:
					return POT_GAIN_ABILITY;
			}
			break;
		case UNICORN_HORN:
			switch (o2->otyp) {
				case POT_SICKNESS:
					return POT_FRUIT_JUICE;
				case POT_HALLUCINATION:
				case POT_BLINDNESS:
				case POT_CONFUSION:
				case POT_BLOOD:
				case POT_VAMPIRE_BLOOD:
					return POT_WATER;
			}
			break;
		case AMETHYST: /* "a-methyst" == "not intoxicated" */
			if (o2->otyp == POT_BOOZE)
				return POT_FRUIT_JUICE;
			break;
		case POT_GAIN_LEVEL:
		case POT_GAIN_ENERGY:
			switch (o2->otyp) {
				case POT_CONFUSION:
					return rn2(3) ? POT_BOOZE : POT_ENLIGHTENMENT;
				case POT_HEALING:
					return POT_EXTRA_HEALING;
				case POT_EXTRA_HEALING:
					return POT_FULL_HEALING;
				case POT_FULL_HEALING:
					return POT_GAIN_ABILITY;
				case POT_FRUIT_JUICE:
					return POT_SEE_INVISIBLE;
				case POT_BOOZE:
					return POT_HALLUCINATION;
			}
			break;
		case POT_FRUIT_JUICE:
			switch (o2->otyp) {
				case POT_SICKNESS:
					return POT_SICKNESS;
				case POT_BLOOD:
					return POT_BLOOD;
				case POT_VAMPIRE_BLOOD:
					return POT_VAMPIRE_BLOOD;
				case POT_SPEED:
					return POT_BOOZE;
				case POT_GAIN_LEVEL:
				case POT_GAIN_ENERGY:
					return POT_SEE_INVISIBLE;
			}
			break;
		case POT_ENLIGHTENMENT:
			switch (o2->otyp) {
				case POT_LEVITATION:
					if (rn2(3)) return POT_GAIN_LEVEL;
					break;
				case POT_FRUIT_JUICE:
					return POT_BOOZE;
				case POT_BOOZE:
					return POT_CONFUSION;
			}
			break;
	}
	/* MRKR: Extra alchemical effects. */

	if (o2->otyp == POT_ACID && o1->oclass == GEM_CLASS) {
		const char *potion_descr;

		/* Note: you can't create smoky, milky or clear potions */

		switch (o1->otyp) {
				/* white */

			case DILITHIUM_CRYSTAL:
				/* explodes - special treatment in dodip */
				/* here we just want to return something non-zero */
				return POT_WATER;
				break;
			case DIAMOND:
				/* won't dissolve */
				potion_descr = NULL;
				break;
			case OPAL:
				potion_descr = "cloudy";
				break;

				/* red */

			case RUBY:
				potion_descr = "ruby";
				break;
			case GARNET:
				potion_descr = "pink";
				break;
			case JASPER:
				potion_descr = "purple-red";
				break;

				/* orange */

			case JACINTH:
				potion_descr = "orange";
				break;
			case AGATE:
				potion_descr = "swirly";
				break;

				/* yellow */

			case CITRINE:
				potion_descr = "yellow";
				break;
			case CHRYSOBERYL:
				potion_descr = "golden";
				break;

				/* yellowish brown */

			case AMBER:
				potion_descr = "brown";
				break;
			case TOPAZ:
				potion_descr = "murky";
				break;

				/* green */

			case EMERALD:
				potion_descr = "emerald";
				break;
			case TURQUOISE:
				potion_descr = "sky blue";
				break;
			case AQUAMARINE:
				potion_descr = "cyan";
				break;
			case JADE:
				potion_descr = "dark green";
				break;

				/* blue */

			case SAPPHIRE:
				potion_descr = "brilliant blue";
				break;

				/* violet */

			case AMETHYST:
				potion_descr = "magenta";
				break;
			case FLUORITE:
				potion_descr = "white";
				break;

				/* black */

			case BLACK_OPAL:
				potion_descr = "black";
				break;
			case JET:
				potion_descr = "dark";
				break;
			case OBSIDIAN:
				potion_descr = "effervescent";
				break;
			default:
				potion_descr = NULL;
		}

		if (potion_descr) {
			int typ;

			/* find a potion that matches the description */

			for (typ = bases[POTION_CLASS];
			     objects[typ].oc_class == POTION_CLASS;
			     typ++) {
				if (strcmp(potion_descr, OBJ_DESCR(objects[typ])) == 0) {
					return typ;
				}
			}
		}
	}

	return 0;
}

/* Bills an object that's about to be downgraded, assuming that's not already
 * been done */
static void pre_downgrade_obj(struct obj *obj, boolean *used) {
	boolean dummy = false;

	if (!used) used = &dummy;
	if (!*used) pline("Your %s for a moment.", aobjnam(obj, "sparkle"));
	if (obj->unpaid && costly_spot(u.ux, u.uy) && !*used) {
		pline("You damage it, you pay for it.");
		bill_dummy_object(obj);
	}
	*used = true;
}

/* Implements the downgrading effect of potions of amnesia and Lethe water */
/* nomagic = The non-magical object to downgrade to */
static void downgrade_obj(struct obj *obj, int nomagic, boolean *used) {
	pre_downgrade_obj(obj, used);
	obj->otyp = nomagic;
	obj->spe = 0;
	obj->owt = weight(obj);
	context.botl = true;
}

/* returns true if something happened (potion should be used up) */
boolean get_wet(struct obj *obj, boolean amnesia) {
	boolean used = false;

	if (snuff_lit(obj)) return true;

	if (obj->greased) {
		grease_protect(obj, NULL, &youmonst);
		return false;
	}
	/* (Rusting shop goods ought to be charged for.) */
	switch (obj->oclass) {
		case POTION_CLASS:
			if (obj->otyp == POT_WATER) {
				if (amnesia) {
					pline("Your %s to sparkle.", aobjnam(obj, "start"));
					obj->odiluted = 0;
					obj->otyp = POT_AMNESIA;
					used = true;
					break;
				}
				return false;
			}

			/* Diluting a !ofAmnesia just gives water... */
			if (obj->otyp == POT_AMNESIA) {
				pline("Your %s flat.", aobjnam(obj, "become"));
				obj->odiluted = 0;
				obj->otyp = POT_WATER;
				used = true;
				break;
			}

			/* KMH -- Water into acid causes an explosion */
			if (obj->otyp == POT_ACID) {
				pline("It boils vigorously!");
				pline("You are caught in the explosion!");
				losehp(Acid_resistance ? rnd(5) : rnd(10), "elementary chemistry", KILLED_BY);
				if (amnesia) {
					pline("You feel a momentary lapse of reason!");
					forget(2 + rn2(3));
				}
				makeknown(obj->otyp);
				used = true;
				break;
			}
			if (amnesia)
				pline("%s completely.", Yobjnam2(obj, "dilute"));
			else
				pline("%s%s.", Yobjnam2(obj, "dilute"),
				      obj->odiluted ? " further" : "");
			if (obj->unpaid && costly_spot(u.ux, u.uy)) {
				pline("You dilute it, you pay for it.");
				bill_dummy_object(obj);
			}
			if (obj->odiluted || amnesia) {
				obj->odiluted = 0;
				obj->blessed = obj->cursed = false;
				obj->otyp = POT_WATER;
			} else
				obj->odiluted++;
			used = true;
			break;
		case SCROLL_CLASS:
			if (obj->otyp != SCR_BLANK_PAPER
#ifdef MAIL
			    && obj->otyp != SCR_MAIL
#endif
			) {
				if (!Blind) {
					boolean oq1 = obj->quan == 1L;
					pline("The scroll%s %s.",
					      oq1 ? "" : "s", otense(obj, "fade"));
				}
				if (obj->unpaid && costly_spot(u.ux, u.uy)) {
					pline("You erase it, you pay for it.");
					bill_dummy_object(obj);
				}
				obj->otyp = SCR_BLANK_PAPER;
				obj->spe = 0;
				used = true;
			}
			break;
		case SPBOOK_CLASS:
			if (obj->otyp != SPE_BLANK_PAPER) {
				if (obj->otyp == SPE_BOOK_OF_THE_DEAD) {
					pline("%s suddenly heats up; steam rises and it remains dry.",
					      The(xname(obj)));
				} else {
					if (!Blind) {
						boolean oq1 = obj->quan == 1L;
						pline("The spellbook%s %s.",
						      oq1 ? "" : "s", otense(obj, "fade"));
					}
					if (obj->unpaid) {
						subfrombill(obj, shop_keeper(*u.ushops));
						pline("You erase it, you pay for it.");
						bill_dummy_object(obj);
					}
					obj->otyp = SPE_BLANK_PAPER;
				}
				used = true;
			}
			break;
		case GEM_CLASS:
			if (amnesia && (obj->otyp == LUCKSTONE ||
					obj->otyp == LOADSTONE || obj->otyp == HEALTHSTONE ||
					obj->otyp == TOUCHSTONE))
				downgrade_obj(obj, FLINT, &used);
			break;
		case TOOL_CLASS:
			/* Artifacts aren't downgraded by amnesia */
			if (amnesia && !obj->oartifact) {
				switch (obj->otyp) {
					case MAGIC_LAMP:
						/* Magic lamps forget their djinn... */
						downgrade_obj(obj, OIL_LAMP, &used);
						break;
					case MAGIC_CANDLE:
						downgrade_obj(obj,
							      rn2(2) ? WAX_CANDLE : TALLOW_CANDLE,
							      &used);
						break;
					case DRUM_OF_EARTHQUAKE:
						downgrade_obj(obj, LEATHER_DRUM, &used);
						break;
					case MAGIC_WHISTLE:
						/* Magic whistles lose their powers... */
						downgrade_obj(obj, TIN_WHISTLE, &used);
						break;
					case MAGIC_FLUTE:
						/* Magic flutes sound normal again... */
						downgrade_obj(obj, WOODEN_FLUTE, &used);
						break;
					case MAGIC_HARP:
						/* Magic harps sound normal again... */
						downgrade_obj(obj, WOODEN_HARP, &used);
						break;
					case FIRE_HORN:
					case FROST_HORN:
					case HORN_OF_PLENTY:
						downgrade_obj(obj, TOOLED_HORN, &used);
						break;
					case MAGIC_MARKER:
						/* Magic markers run... */
						if (obj->spe > 0) {
							pre_downgrade_obj(obj, &used);
							if ((obj->spe -= (3 + rn2(10))) < 0)
								obj->spe = 0;
						}
						break;
				}
			}

			/* The only other tools that can be affected are pick axes and
		 * unicorn horns... */
			if (!is_weptool(obj)) break;
		/* Drop through for disenchantment and rusting... */
		fallthru;
		case ARMOR_CLASS:
		case WEAPON_CLASS:
		case WAND_CLASS:
		case RING_CLASS:
		/* Just "fall through" to generic rustprone check for now. */
		fallthru;
		default:
			switch (artifact_wet(obj, false)) {
				case -1:
					break;
				default:
					return true;
			}
			/* !ofAmnesia acts as a disenchanter... */
			if (amnesia && obj->spe > 0) {
				pre_downgrade_obj(obj, &used);
				drain_item(obj);
			}
			if (!obj->oerodeproof && is_rustprone(obj) &&
			    (obj->oeroded < MAX_ERODE) && !rn2(2)) {
				pline("%s some%s.", Yobjnam2(obj, "rust"), obj->oeroded ? " more" : "what");
				obj->oeroded++;
				if (obj->unpaid && costly_spot(u.ux, u.uy) && !used) {
					pline("You damage it, you pay for it.");
					bill_dummy_object(obj);
				}
				used = true;
			}
			break;
	}
	/* !ofAmnesia might strip away fooproofing... */
	if (amnesia && obj->oerodeproof && !rn2(13)) {
		pre_downgrade_obj(obj, &used);
		obj->oerodeproof = false;
	}

	/* !ofAmnesia also strips blessed/cursed status... */

	if (amnesia && (obj->cursed || obj->blessed)) {
		/* Blessed objects are valuable, cursed objects aren't, unless
		 * they're water.
		 */
		if (obj->blessed || obj->otyp == POT_WATER)
			pre_downgrade_obj(obj, &used);
		else if (!used) {
			pline("Your %s for a moment.", aobjnam(obj, "sparkle"));
			used = true;
		}
		uncurse(obj);
		unbless(obj);
	}

	if (used)
		update_inventory();
	else
		pline("%s wet.", Yobjnam2(obj, "get"));

	return used;
}

/* KMH, balance patch -- idea by Dylan O'Donnell <dylanw@demon.net>
 * The poor hacker's polypile.  This includes weapons, armor, and tools.
 * To maintain balance, magical categories (amulets, scrolls, spellbooks,
 * potions, rings, and wands) should NOT be supported.
 * Polearms are not currently implemented.
 */

/* returns 1 if something happened (potion should be used up)
 * returns 0 if nothing happened
 * returns -1 if object exploded (potion should be used up)
 */
int upgrade_obj(struct obj *obj) {
	int chg, otyp = obj->otyp;
	short otyp2;
	xchar ox, oy;
	long owornmask;
	struct obj *otmp;
	boolean explodes;
	char buf[BUFSZ];

	/* Check to see if object is valid */
	if (!obj)
		return 0;
	snuff_lit(obj);
	if (obj->oartifact)
		/* WAC -- Could have some funky fx */
		return 0;

	switch (obj->otyp) {
		/* weapons */
		case ORCISH_DAGGER:
			obj->otyp = DAGGER;
			break;
		case GREAT_DAGGER:
		case DAGGER:
			if (!rn2(2))
				obj->otyp = ELVEN_DAGGER;
			else
				obj->otyp = DARK_ELVEN_DAGGER;
			break;
		case ELVEN_DAGGER:
		case DARK_ELVEN_DAGGER:
			obj->otyp = GREAT_DAGGER;
			break;
		case KNIFE:
			obj->otyp = STILETTO;
			break;
		case STILETTO:
			obj->otyp = KNIFE;
			break;
		case AXE:
			obj->otyp = BATTLE_AXE;
			break;
		case BATTLE_AXE:
			obj->otyp = AXE;
			break;
		case PICK_AXE:
			obj->otyp = DWARVISH_MATTOCK;
			break;
		case DWARVISH_MATTOCK:
			obj->otyp = PICK_AXE;
			break;
		case ORCISH_SHORT_SWORD:
			obj->otyp = SHORT_SWORD;
			break;
		case ELVEN_SHORT_SWORD:
		case DARK_ELVEN_SHORT_SWORD:
		case SHORT_SWORD:
			obj->otyp = DWARVISH_SHORT_SWORD;
			break;
		case DWARVISH_SHORT_SWORD:
			if (!rn2(2))
				obj->otyp = ELVEN_SHORT_SWORD;
			else
				obj->otyp = DARK_ELVEN_SHORT_SWORD;
			break;
		case BROADSWORD:
			obj->otyp = ELVEN_BROADSWORD;
			break;
		case ELVEN_BROADSWORD:
			obj->otyp = BROADSWORD;
			break;
		case CLUB:
			obj->otyp = AKLYS;
			break;
		case AKLYS:
			obj->otyp = CLUB;
			break;
		case WAR_HAMMER:
			obj->otyp = HEAVY_HAMMER;
			break;
		case HEAVY_HAMMER:
			obj->otyp = WAR_HAMMER;
			break;
		case ELVEN_BOW:
		case DARK_ELVEN_BOW:
		case YUMI:
		case ORCISH_BOW:
			obj->otyp = BOW;
			break;
		case BOW:
			switch (rn2(3)) {
				case 0:
					obj->otyp = ELVEN_BOW;
					break;
				case 1:
					obj->otyp = DARK_ELVEN_BOW;
					break;
				case 2:
					obj->otyp = YUMI;
					break;
			}
			break;
		case ELVEN_ARROW:
		case DARK_ELVEN_ARROW:
		case YA:
		case ORCISH_ARROW:
			obj->otyp = ARROW;
			break;
		case ARROW:
			switch (rn2(3)) {
				case 0:
					obj->otyp = ELVEN_ARROW;
					break;
				case 1:
					obj->otyp = DARK_ELVEN_ARROW;
					break;
				case 2:
					obj->otyp = YA;
					break;
			}
			break;
		/* armour */
		case ELVEN_MITHRIL_COAT:
			obj->otyp = DARK_ELVEN_MITHRIL_COAT;
			break;
		case DARK_ELVEN_MITHRIL_COAT:
			obj->otyp = ELVEN_MITHRIL_COAT;
			break;
		case ORCISH_CHAIN_MAIL:
			obj->otyp = CHAIN_MAIL;
			break;
		case CHAIN_MAIL:
			obj->otyp = ORCISH_CHAIN_MAIL;
			break;
		case STUDDED_LEATHER_ARMOR:
		case LEATHER_JACKET:
			obj->otyp = LEATHER_ARMOR;
			break;
		case LEATHER_ARMOR:
			obj->otyp = STUDDED_LEATHER_ARMOR;
			break;
		/* robes */
		case ROBE:
			if (!rn2(2))
				obj->otyp = ROBE_OF_PROTECTION;
			else
				obj->otyp = ROBE_OF_POWER;
			break;
		case ROBE_OF_PROTECTION:
		case ROBE_OF_POWER:
			obj->otyp = ROBE;
			break;
		/* cloaks */
		case CLOAK_OF_PROTECTION:
		case CLOAK_OF_INVISIBILITY:
		case CLOAK_OF_MAGIC_RESISTANCE:
		case CLOAK_OF_DISPLACEMENT:
		case DWARVISH_CLOAK:
		case ORCISH_CLOAK:
			if (!rn2(2))
				obj->otyp = OILSKIN_CLOAK;
			else
				obj->otyp = ELVEN_CLOAK;
			break;
		case OILSKIN_CLOAK:
		case ELVEN_CLOAK:
			switch (rn2(4)) {
				case 0:
					obj->otyp = CLOAK_OF_PROTECTION;
					break;
				case 1:
					obj->otyp = CLOAK_OF_INVISIBILITY;
					break;
				case 2:
					obj->otyp = CLOAK_OF_MAGIC_RESISTANCE;
					break;
				case 3:
					obj->otyp = CLOAK_OF_DISPLACEMENT;
					break;
			}
			break;
		/* helms */
		case FEDORA:
			obj->otyp = ELVEN_LEATHER_HELM;
			break;
		case ELVEN_LEATHER_HELM:
			obj->otyp = FEDORA;
			break;
		case DENTED_POT:
			obj->otyp = ORCISH_HELM;
			break;
		case ORCISH_HELM:
		case HELM_OF_BRILLIANCE:
		case HELM_OF_TELEPATHY:
			obj->otyp = DWARVISH_IRON_HELM;
			break;
		case DWARVISH_IRON_HELM:
			if (!rn2(2))
				obj->otyp = HELM_OF_BRILLIANCE;
			else
				obj->otyp = HELM_OF_TELEPATHY;
			break;
		case CORNUTHAUM:
			obj->otyp = DUNCE_CAP;
			break;
		case DUNCE_CAP:
			obj->otyp = CORNUTHAUM;
			break;
		/* gloves */
		case LEATHER_GLOVES:
			if (!rn2(2))
				obj->otyp = GAUNTLETS_OF_SWIMMING;
			else
				obj->otyp = GAUNTLETS_OF_DEXTERITY;
			break;
		case GAUNTLETS_OF_SWIMMING:
		case GAUNTLETS_OF_DEXTERITY:
			obj->otyp = LEATHER_GLOVES;
			break;
		/* shields */
		case ELVEN_SHIELD:
			if (!rn2(2))
				obj->otyp = URUK_HAI_SHIELD;
			else
				obj->otyp = ORCISH_SHIELD;
			break;
		case URUK_HAI_SHIELD:
		case ORCISH_SHIELD:
			obj->otyp = ELVEN_SHIELD;
			break;
		case DWARVISH_ROUNDSHIELD:
			obj->otyp = LARGE_SHIELD;
			break;
		case LARGE_SHIELD:
			obj->otyp = DWARVISH_ROUNDSHIELD;
			break;
		/* boots */
		case LOW_BOOTS:
			obj->otyp = HIGH_BOOTS;
			break;
		case HIGH_BOOTS:
			obj->otyp = LOW_BOOTS;
			break;
		/* NOTE:  Supposedly,  HIGH_BOOTS should upgrade to any of the
		other magic leather boots (except for fumble).  IRON_SHOES
		should upgrade to the iron magic boots,  unless
		the iron magic boots are fumble */
		/* rings,  amulets */
		case LARGE_BOX:
		case ICE_BOX:
			obj->otyp = CHEST;
			break;
		case CHEST:
			obj->otyp = ICE_BOX;
			break;
		case SACK:
			obj->otyp = rn2(5) ? OILSKIN_SACK : BAG_OF_HOLDING;
			break;
		case OILSKIN_SACK:
			obj->otyp = BAG_OF_HOLDING;
			break;
		case BAG_OF_HOLDING:
			obj->otyp = OILSKIN_SACK;
			break;
		case TOWEL:
			obj->otyp = BLINDFOLD;
			break;
		case BLINDFOLD:
			obj->otyp = TOWEL;
			break;
		case CREDIT_CARD:
		case LOCK_PICK:
			obj->otyp = SKELETON_KEY;
			break;
		case SKELETON_KEY:
			obj->otyp = LOCK_PICK;
			break;
		case TALLOW_CANDLE:
			obj->otyp = WAX_CANDLE;
			break;
		case WAX_CANDLE:
			obj->otyp = TALLOW_CANDLE;
			break;
		case OIL_LAMP:
			obj->otyp = BRASS_LANTERN;
			break;
		case BRASS_LANTERN:
			obj->otyp = OIL_LAMP;
			break;
		case TIN_WHISTLE:
			obj->otyp = MAGIC_WHISTLE;
			break;
		case MAGIC_WHISTLE:
			obj->otyp = TIN_WHISTLE;
			break;
		case WOODEN_FLUTE:
			obj->otyp = MAGIC_FLUTE;
			obj->spe = rn1(5, 10);
			break;
		case MAGIC_FLUTE:
			obj->otyp = WOODEN_FLUTE;
			break;
		case TOOLED_HORN:
			obj->otyp = rn1(HORN_OF_PLENTY - TOOLED_HORN, FROST_HORN);
			obj->spe = rn1(5, 10);
			obj->known = 0;
			break;
		case HORN_OF_PLENTY:
		case FIRE_HORN:
		case FROST_HORN:
			obj->otyp = TOOLED_HORN;
			break;
		case WOODEN_HARP:
			obj->otyp = MAGIC_HARP;
			obj->spe = rn1(5, 10);
			obj->known = 0;
			break;
		case MAGIC_HARP:
			obj->otyp = WOODEN_HARP;
			break;
		case LEASH:
			obj->otyp = SADDLE;
			break;
		case SADDLE:
			obj->otyp = LEASH;
			break;
		case TIN_OPENER:
			obj->otyp = TINNING_KIT;
			obj->spe = rn1(30, 70);
			obj->known = 0;
			break;
		case TINNING_KIT:
			obj->otyp = TIN_OPENER;
			break;
		case CRYSTAL_BALL:
			/* "ball-point pen" */
			obj->otyp = MAGIC_MARKER;
			/* Keep the charges (crystal ball usually less than marker) */
			break;
		case MAGIC_MARKER:
			obj->otyp = CRYSTAL_BALL;
			chg = rn1(10, 3);
			if (obj->spe > chg)
				obj->spe = chg;
			obj->known = 0;
			break;
		case K_RATION:
		case C_RATION:
		case LEMBAS_WAFER:
			if (!rn2(2))
				obj->otyp = CRAM_RATION;
			else
				obj->otyp = FOOD_RATION;
			break;
		case FOOD_RATION:
		case CRAM_RATION:
			obj->otyp = LEMBAS_WAFER;
			break;
		case LOADSTONE:
			obj->otyp = FLINT;
			break;
		case FLINT:
			if (!rn2(2))
				obj->otyp = LUCKSTONE;
			else
				obj->otyp = HEALTHSTONE;
			break;
		default:
			/* This object is not upgradable */
			return 0;
	}

	if (artifact_name(ONAME(obj), &otyp2) && otyp2 == obj->otyp) {
		int n;
		char c1, c2;

		strcpy(buf, ONAME(obj));
		n = rn2((int)strlen(buf));
		c1 = lowc(buf[n]);
		do
			c2 = 'a' + rn2('z' - 'a');
		while (c1 == c2);
		buf[n] = (buf[n] == c1) ? c2 : highc(c2); /* keep same case */
		if (oname(obj, buf) != obj)
			panic("upgrade_obj: unhandled realloc");
	}

	if ((!carried(obj) || obj->unpaid) && !is_hazy(obj) &&
	    get_obj_location(obj, &ox, &oy, BURIED_TOO | CONTAINED_TOO) &&
	    costly_spot(ox, oy)) {
		char objroom = *in_rooms(ox, oy, SHOPBASE);
		struct monst *shkp = shop_keeper(objroom);

		if ((!obj->no_charge ||
		     (Has_contents(obj) &&
		      (contained_cost(obj, shkp, 0L, false, false) != 0L))) &&
		    inhishop(shkp)) {
			if (shkp->mpeaceful) {
				if (*u.ushops && *in_rooms(u.ux, u.uy, 0) == *in_rooms(shkp->mx, shkp->my, 0) &&
				    !costly_spot(u.ux, u.uy))
					make_angry_shk(shkp, ox, oy);
				else {
					pline("%s gets angry!", Monnam(shkp));
					hot_pursuit(shkp);
				}
			} else
				Norep("%s is furious!", Monnam(shkp));
			otyp2 = obj->otyp;
			obj->otyp = otyp;
			/*
			 * [ALI] When unpaid containers are upgraded, the
			 * old container is billed as a dummy object, but
			 * it's contents are unaffected and will remain
			 * either unpaid or not as appropriate.
			 */
			otmp = obj->cobj;
			obj->cobj = NULL;
			if (costly_spot(u.ux, u.uy) && objroom == *u.ushops)
				bill_dummy_object(obj);
			else
				stolen_value(obj, ox, oy, false, false, false);
			obj->otyp = otyp2;
			obj->cobj = otmp;
		}
	}

	/* The object was transformed */
	obj->owt = weight(obj);
	obj->oclass = objects[obj->otyp].oc_class;
	if (!objects[obj->otyp].oc_uses_known)
		obj->known = 1;

	if (carried(obj)) {
		if (obj == uskin) rehumanize();
		/* Quietly remove worn item if no longer compatible --ALI */
		owornmask = obj->owornmask;
		if (owornmask & W_ARM && !is_suit(obj))
			owornmask &= ~W_ARM;
		if (owornmask & W_ARMC && !is_cloak(obj))
			owornmask &= ~W_ARMC;
		if (owornmask & W_ARMH && !is_helmet(obj))
			owornmask &= ~W_ARMH;
		if (owornmask & W_ARMS && !is_shield(obj))
			owornmask &= ~W_ARMS;
		if (owornmask & W_ARMG && !is_gloves(obj))
			owornmask &= ~W_ARMG;
		if (owornmask & W_ARMF && !is_boots(obj))
			owornmask &= ~W_ARMF;
		if (owornmask & W_ARMU && !is_shirt(obj))
			owornmask &= ~W_ARMU;
		if (owornmask & W_TOOL && obj->otyp != BLINDFOLD &&
		    obj->otyp != TOWEL && obj->otyp != LENSES)
			owornmask &= ~W_TOOL;
		otyp2 = obj->otyp;
		obj->otyp = otyp;
		if (obj->otyp == LEASH && obj->leashmon) o_unleash(obj);
		remove_worn_item(obj, true);
		obj->otyp = otyp2;
		obj->owornmask = owornmask;
		setworn(obj, obj->owornmask);
		puton_worn_item(obj);
	}

	if (obj->otyp == BAG_OF_HOLDING && Has_contents(obj)) {
		explodes = false;

		for (otmp = obj->cobj; otmp; otmp = otmp->nobj)
			if (mbag_explodes(otmp, 0)) {
				explodes = true;
				break;
			}

		if (explodes) {
			pline("As you upgrade your bag, you are blasted by a magical explosion!");
			delete_contents(obj);
			if (carried(obj))
				useup(obj);
			else
				useupf(obj, obj->quan);
			losehp(d(6, 6), "magical explosion", KILLED_BY_AN);
			return -1;
		}
	}
	return 1;
}

int dodip(void) {
	struct obj *potion, *obj, *singlepotion;
	const char *tmp;
	uchar here;
	char allowall[2], qbuf[QBUFSZ];
	short mixture;
	int res;

	allowall[0] = ALL_CLASSES;
	allowall[1] = '\0';
	if (!(obj = getobj(allowall, "dip")))
		return 0;

	here = levl[u.ux][u.uy].typ;
	/* Is there a fountain to dip into here? */
	if (IS_FOUNTAIN(here)) {
		if (yn("Dip it into the fountain?") == 'y') {
			dipfountain(obj);
			return 1;
		}
	} else if (IS_TOILET(here)) {
		if (yn("Dip it into the toilet?") == 'y') {
			diptoilet(obj);
			return 1;
		}
	} else if (is_pool(u.ux, u.uy)) {
		tmp = waterbody_name(u.ux, u.uy);
		sprintf(qbuf, "Dip it into the %s?", tmp);
		if (yn(qbuf) == 'y') {
			if (Levitation) {
				floating_above(tmp);
			} else if (u.usteed && !is_swimmer(u.usteed->data) &&
				   P_SKILL(P_RIDING) < P_BASIC) {
				rider_cant_reach(); /* not skilled enough to reach */
			} else {
				get_wet(obj, level.flags.lethe);
				if (obj->otyp == POT_ACID) useup(obj);
			}
			return 1;
		}
	}

	if (!(potion = getobj(beverages, "dip into")))
		return 0;
	if (potion == obj && potion->quan == 1L) {
		pline("That is a potion bottle, not a Klein bottle!");
		return 0;
	}

	if (potion->otyp != POT_WATER && obj->otyp == POT_WATER) {
		/* swap roles, to ensure symmetry */
		struct obj *otmp = potion;
		potion = obj;
		obj = otmp;
	}
	potion->in_use = true; /* assume it will be used up */
	if (potion->otyp == POT_WATER) {
		boolean useeit = !Blind || (obj == ublindf && Blindfolded_only);
		if (potion->blessed) {
			if (obj->cursed) {
				if (useeit)
					pline("%s %s.", Yobjnam2(obj, "softly glow"), hcolor(NH_AMBER));
				uncurse(obj);
				obj->bknown = 1;
poof:
				if (!(objects[potion->otyp].oc_name_known) &&
				    !(objects[potion->otyp].oc_uname))
					docall(potion);
				useup(potion);
				return 1;
			} else if (!obj->blessed) {
				if (useeit) {
					tmp = hcolor(NH_LIGHT_BLUE);
					pline("%s with a%s %s aura.",
					      Yobjnam2(obj, "softly glow"),
					      index(vowels, *tmp) ? "n" : "", tmp);
				}
				bless(obj);
				obj->bknown = 1;
				goto poof;
			}
		} else if (potion->cursed) {
			if (obj->blessed) {
				if (useeit)
					pline("%s %s.", Yobjnam2(obj, "glow"), hcolor("brown"));
				unbless(obj);
				obj->bknown = 1;
				goto poof;
			} else if (!obj->cursed) {
				if (useeit) {
					tmp = hcolor(NH_BLACK);
					pline("%s with a%s %s aura.", Yobjnam2(obj, "glow"), index(vowels, *tmp) ? "n" : "", tmp);
				}
				curse(obj);
				obj->bknown = 1;
				goto poof;
			}
		} else {
			switch (artifact_wet(obj, true)) {
				/* Assume ZT_xxx is AD_xxx-1 */
				case -1:
					break;
				default:
					zap_over_floor(u.ux, u.uy,
						       (artifact_wet(obj, true) - 1), NULL);
					break;
			}
			if (get_wet(obj, false))
				goto poof;
		}
	} else if (potion->otyp == POT_AMNESIA) {
		if (potion == obj) {
			obj->in_use = false;
			potion = splitobj(obj, 1L);
			potion->in_use = true;
		}
		if (get_wet(obj, true)) goto poof;
	}
	/* WAC - Finn Theoderson - make polymorph and gain level msgs similar
	 * 	 Give out name of new object and allow user to name the potion
	 */
	else if (obj->otyp == POT_POLYMORPH ||
		 potion->otyp == POT_POLYMORPH) {
		/* some objects can't be polymorphed */
		if (obj->otyp == potion->otyp || /* both POT_POLY */
		    obj->otyp == WAN_POLYMORPH ||
		    obj->otyp == SPE_POLYMORPH ||
		    obj == uball || obj == uskin ||
		    obj_resists(obj->otyp == POT_POLYMORPH ?
					potion :
					obj,
				5, 95)) {
			pline("Nothing happens.");
		} else {
			boolean was_wep = false, was_swapwep = false, was_quiver = false;
			short save_otyp = obj->otyp;
			/* KMH, conduct */
			u.uconduct.polypiles++;

			if (obj == uwep)
				was_wep = true;
			else if (obj == uswapwep)
				was_swapwep = true;
			else if (obj == uquiver)
				was_quiver = true;

			obj = poly_obj(obj, STRANGE_OBJECT);

			if (was_wep)
				setuwep(obj, true);
			else if (was_swapwep)
				setuswapwep(obj, true);
			else if (was_quiver)
				setuqwep(obj);

			if (obj->otyp != save_otyp) {
				makeknown(POT_POLYMORPH);
				useup(potion);
				prinv(NULL, obj, 0L);
				return 1;
			} else {
				pline("Nothing seems to happen.");
				goto poof;
			}
		}
		potion->in_use = false; /* didn't go poof */
		return 1;
	} else if (potion->otyp == POT_RESTORE_ABILITY && is_hazy(obj)) {
		/* KMH -- Restore ability will stop unpolymorphing */
		stop_timer(UNPOLY_OBJ, obj_to_any(obj));
		obj->oldtyp = STRANGE_OBJECT;
		if (!Blind)
			pline("%s seems less hazy.", Yname2(obj));
		useup(potion);
		return 1;
	} else if (obj->oclass == POTION_CLASS && obj->otyp != potion->otyp) {
		/* Mixing potions is dangerous... */
		pline("The potions mix...");
		/* KMH, balance patch -- acid is particularly unstable */
		if (obj->cursed || obj->otyp == POT_ACID ||
		    potion->cursed || potion->otyp == POT_ACID || !rn2(10)) {
			pline("BOOM!  They explode!");
			exercise(A_STR, false);
			if (!breathless(youmonst.data) || haseyes(youmonst.data))
				potionbreathe(obj);
			useup(obj);
			useup(potion);
			/* MRKR: an alchemy smock ought to be */
			/* some protection against this: */
			// Done -MC
			if (uarmc->otyp == LAB_COAT) {
				pline("Fortunately, %s protects you.", yobjnam(uarmc, NULL));
				makeknown(LAB_COAT);
			} else {
				losehp(Acid_resistance ? rnd(5) : rnd(10), "alchemic blast", KILLED_BY_AN);
			}
			return 1;
		}

		obj->blessed = obj->cursed = obj->bknown = 0;
		if (Blind || Hallucination) obj->dknown = 0;

		if ((mixture = mixtype(obj, potion)) != 0) {
			obj->otyp = mixture;
		} else {
			switch (obj->odiluted ? 1 : rnd(8)) {
				case 1:
					obj->otyp = POT_WATER;
					break;
				case 2:
				case 3:
					obj->otyp = POT_SICKNESS;
					break;
				case 4: {
					struct obj *otmp;
					otmp = mkobj(POTION_CLASS, false);
					obj->otyp = otmp->otyp;
					obfree(otmp, NULL);
				} break;
				default:
					if (!Blind)
						pline("The mixture glows brightly and evaporates.");
					useup(obj);
					useup(potion);
					return 1;
			}
		}

		obj->odiluted = (obj->otyp != POT_WATER);

		if (obj->otyp == POT_WATER && !Hallucination) {
			pline("The mixture bubbles%s.",
			      Blind ? "" : ", then clears");
		} else if (!Blind) {
			pline("The mixture looks %s.",
			      hcolor(OBJ_DESCR(objects[obj->otyp])));
		}

		useup(potion);
		return 1;
	}

	if (!always_visible(obj)) {
		if (potion->otyp == POT_INVISIBILITY && !obj->oinvis) {
			obj_set_oinvis(obj, true, true);
			goto poof;
		} else if (potion->otyp == POT_SEE_INVISIBLE && obj->oinvis) {
			obj_set_oinvis(obj, false, true);
			goto poof;
		}
	}

	if (is_poisonable(obj)) {
		if (potion->otyp == POT_SICKNESS && !obj->opoisoned) {
			char buf[BUFSZ];
			if (potion->quan > 1L)
				sprintf(buf, "One of %s", the(xname(potion)));
			else
				strcpy(buf, The(xname(potion)));
			pline("%s forms a coating on %s.",
			      buf, the(xname(obj)));
			obj->opoisoned = true;
			goto poof;
		} else if (obj->opoisoned &&
			   (potion->otyp == POT_HEALING ||
			    potion->otyp == POT_EXTRA_HEALING ||
			    potion->otyp == POT_FULL_HEALING)) {
			pline("A coating wears off %s.", the(xname(obj)));
			obj->opoisoned = 0;
			goto poof;
		}
	}

	if (potion->otyp == POT_ACID) {
		if (erode_obj(obj, true, false, true))
			goto poof;
	}

	if (potion->otyp == POT_OIL) {
		boolean wisx = false;
		if (potion->lamplit) { /* burning */
			int omat = objects[obj->otyp].oc_material;
			/* the code here should be merged with fire_damage */
			if (catch_lit(obj)) {
				/* catch_lit does all the work if true */
			} else if (obj->oerodeproof || obj_resists(obj, 5, 95) ||
				   !is_flammable(obj) || obj->oclass == FOOD_CLASS) {
				pline("%s %s to burn for a moment.",
				      Yname2(obj), otense(obj, "seem"));
			} else {
				if ((omat == PLASTIC || omat == PAPER) && !obj->oartifact)
					obj->oeroded = MAX_ERODE;
				pline("The burning oil %s %s%c",
				      obj->oeroded == MAX_ERODE ? "destroys" : "damages",
				      yname(obj),
				      obj->oeroded == MAX_ERODE ? '!' : '.');
				if (obj->oeroded == MAX_ERODE) {
					if (obj->owornmask) remove_worn_item(obj, true);
					obj_extract_self(obj);
					obfree(obj, NULL);
					obj = NULL;
				} else {
					/* we know it's carried */
					if (obj->unpaid) {
						/* create a dummy duplicate to put on bill */
						verbalize("You burnt it, you bought it!");
						bill_dummy_object(obj);
					}
					obj->oeroded++;
				}
			}
		} else if (potion->cursed) {
			pline("The potion spills and covers your %s with oil.",
			      makeplural(body_part(FINGER)));
			incr_itimeout(&Glib, d(2, 10));
		} else if (obj->oclass != WEAPON_CLASS && !is_weptool(obj)) {
			/* the following cases apply only to weapons */
			goto more_dips;
			/* Oil removes rust and corrosion, but doesn't unburn.
			 * Arrows, etc are classed as metallic due to arrowhead
			 * material, but dipping in oil shouldn't repair them.
			 */
		} else if ((!is_rustprone(obj) && !is_corrodeable(obj)) ||
			   is_ammo(obj) || (!obj->oeroded && !obj->oeroded2)) {
			/* uses up potion, doesn't set obj->greased */
			pline("%s %s with an oily sheen.",
			      Yname2(obj), otense(obj, "gleam"));
		} else {
			pline("%s %s less %s.",
			      Yname2(obj), otense(obj, "are"),
			      (obj->oeroded && obj->oeroded2) ? "corroded and rusty" :
								obj->oeroded ? "rusty" : "corroded");
			if (obj->oeroded > 0) obj->oeroded--;
			if (obj->oeroded2 > 0) obj->oeroded2--;
			wisx = true;
		}
		exercise(A_WIS, wisx);
		makeknown(potion->otyp);
		useup(potion);
		return 1;
	} else if (potion->otyp == POT_GAIN_LEVEL) {
		res = upgrade_obj(obj);
		if (res != 0) {
			if (res == 1) {
				/* The object was upgraded */
				pline("Hmm!  You don't recall dipping that into the potion.");
				prinv(NULL, obj, 0L);
			} /* else potion exploded */
			if (!objects[potion->otyp].oc_name_known &&
			    !objects[potion->otyp].oc_uname)
				docall(potion);
			useup(potion);
			update_inventory();
			exercise(A_WIS, true);
			return 1;
		}
		/* no return here, go for Interesting... message */
	}

	/* KMH, balance patch -- acid affects damage(proofing) */
	if (potion->otyp == POT_ACID && (obj->oclass == ARMOR_CLASS ||
					 obj->oclass == WEAPON_CLASS || is_weptool(obj))) {
		if (!potion->blessed && obj->oerodeproof) {
			pline("%s %s golden shield.", Yname2(obj),
			      (obj->quan > 1L) ? "lose their" : "loses its");
			obj->oerodeproof = 0;
			makeknown(potion->otyp);
		} else {
			pline("%s looks a little dull.", Yname2(obj));
			if (!objects[potion->otyp].oc_name_known &&
			    !objects[potion->otyp].oc_uname)
				docall(potion);
		}
		exercise(A_WIS, false);
		useup(potion);
		return 1;
	}
more_dips:

	/* Allow filling of MAGIC_LAMPs to prevent identification by player */
	if ((obj->otyp == OIL_LAMP || obj->otyp == MAGIC_LAMP) &&
	    (potion->otyp == POT_OIL)) {
		/* Turn off engine before fueling, turn off fuel too :-)  */
		if (obj->lamplit || potion->lamplit) {
			useup(potion);
			explode(u.ux, u.uy, ZT_SPELL(ZT_FIRE), d(6, 6), 0, EXPL_FIERY);
			exercise(A_WIS, false);
			return 1;
		}
		/* Adding oil to an empty magic lamp renders it into an oil lamp */
		if ((obj->otyp == MAGIC_LAMP) && obj->spe == 0) {
			obj->otyp = OIL_LAMP;
			obj->age = 0;
		}
		if (obj->age > 1000L) {
			pline("%s %s full.", Yname2(obj), otense(obj, "are"));
			potion->in_use = false; /* didn't go poof */
		} else {
			pline("You fill your %s with oil.", yname(obj));
			check_unpaid(potion);	     /* Yendorian Fuel Tax */
			obj->age += 2 * potion->age; /* burns more efficiently */
			if (obj->age > 1500L) obj->age = 1500L;
			useup(potion);
			exercise(A_WIS, true);
		}
		makeknown(POT_OIL);
		obj->spe = 1;
		update_inventory();
		return 1;
	}

	potion->in_use = false; /* didn't go poof */
	if ((obj->otyp == UNICORN_HORN || obj->oclass == GEM_CLASS) &&
	    (mixture = mixtype(obj, potion)) != 0) {
		char oldbuf[BUFSZ], newbuf[BUFSZ];
		short old_otyp = potion->otyp;
		boolean old_dknown = false;
		boolean more_than_one = potion->quan > 1;

		oldbuf[0] = '\0';
		if (potion->dknown) {
			old_dknown = true;
			sprintf(oldbuf, "%s ",
				hcolor(OBJ_DESCR(objects[potion->otyp])));
		}
		/* with multiple merged potions, split off one and
		   just clear it */
		if (potion->quan > 1L) {
			singlepotion = splitobj(potion, 1L);
		} else
			singlepotion = potion;

		/* MRKR: Gems dissolve in acid to produce new potions */

		if (obj->oclass == GEM_CLASS && potion->otyp == POT_ACID) {
			struct obj *singlegem = (obj->quan > 1L ?
							 splitobj(obj, 1L) :
							 obj);

			singlegem->in_use = true;
			if (potion->otyp == POT_ACID &&
			    (obj->otyp == DILITHIUM_CRYSTAL ||
			     potion->cursed || !rn2(10))) {
				/* Just to keep them on their toes */

				singlepotion->in_use = true;
				if (Hallucination && obj->otyp == DILITHIUM_CRYSTAL) {
					/* Thanks to Robin Johnson */
					pline("Warning, Captain!  The warp core has been breached!");
				}
				pline("BOOM!  %s explodes!", The(xname(singlegem)));
				exercise(A_STR, false);
				if (!breathless(youmonst.data) || haseyes(youmonst.data))
					potionbreathe(singlepotion);
				useup(singlegem);
				useup(singlepotion);
				/* MRKR: an alchemy smock ought to be */
				/* some protection against this: */
				losehp(Acid_resistance ? rnd(5) : rnd(10),
				       "alchemic blast", KILLED_BY_AN);
				return 1;
			}

			pline("%s dissolves in %s.", The(xname(singlegem)),
			      the(xname(singlepotion)));
			makeknown(POT_ACID);
			useup(singlegem);
		}

		if (singlepotion->unpaid && costly_spot(u.ux, u.uy)) {
			pline("You use it, you pay for it.");
			bill_dummy_object(singlepotion);
		}

		if (singlepotion->otyp == mixture) {
			/* no change - merge it back in */
			if (more_than_one && !merged(&potion, &singlepotion)) {
				/* should never happen */
				impossible("singlepotion won't merge with parent potion.");
			}
		} else {
			singlepotion->otyp = mixture;
			singlepotion->blessed = 0;
			if (mixture == POT_WATER) {
				singlepotion->cursed = false;
				singlepotion->odiluted = 0;
			} else {
				singlepotion->cursed = obj->cursed; /* odiluted left as-is */
			}
			singlepotion->bknown = false;
			if (Blind) {
				singlepotion->dknown = false;
			} else {
				singlepotion->dknown = !Hallucination;
				if (mixture == POT_WATER && singlepotion->dknown)
					sprintf(newbuf, "clears");
				else
					sprintf(newbuf, "turns %s",
						hcolor(OBJ_DESCR(objects[mixture])));
				pline("The %spotion%s %s.", oldbuf,
				      more_than_one ? " that you dipped into" : "",
				      newbuf);
				if (!objects[old_otyp].oc_uname &&
				    !objects[old_otyp].oc_name_known && old_dknown) {
					struct obj fakeobj;
					fakeobj = zeroobj;
					fakeobj.dknown = 1;
					fakeobj.otyp = old_otyp;
					fakeobj.oclass = POTION_CLASS;
					docall(&fakeobj);
				}
			}
			obj_extract_self(singlepotion);
			singlepotion = hold_another_object(singlepotion,
							   "You juggle and drop %s!",
							   doname(singlepotion), NULL);
			update_inventory();
		}

		return 1;
	}

	pline("Interesting...");
	return 1;
}

void djinni_from_bottle(struct obj *obj) {
	struct monst *mtmp;
	int chance;

	if (!(mtmp = makemon(&mons[PM_DJINNI], u.ux, u.uy, NO_MM_FLAGS))) {
		pline("It turns out to be empty.");
		return;
	}

	if (!Blind) {
		pline("In a cloud of smoke, %s emerges!", a_monnam(mtmp));
		pline("%s speaks.", Monnam(mtmp));
	} else {
		pline("You smell acrid fumes.");
		pline("Something speaks.");
	}

	chance = rn2(5);
	if (obj->blessed)
		chance = (chance == 4) ? rnd(4) : 0;
	else if (obj->cursed)
		chance = (chance == 0) ? rn2(4) : 4;
	/* 0,1,2,3,4:  b=80%,5,5,5,5; nc=20%,20,20,20,20; c=5%,5,5,5,80 */

	switch (chance) {
		case 0:
			verbalize("I am in your debt.  I will grant one wish!");
			makewish();
			mongone(mtmp);
			break;
		case 1:
			verbalize("Thank you for freeing me!");
			tamedog(mtmp, NULL);
			break;
		case 2:
			verbalize("You freed me!");
			mtmp->mpeaceful = true;
			set_malign(mtmp);
			break;
		case 3:
			verbalize("It is about time!");
			if (canspotmon(mtmp)) pline("%s vanishes.", Monnam(mtmp));
			mongone(mtmp);
			break;
		default:
			verbalize("You disturbed me, fool!");
			mtmp->mpeaceful = false;
			set_malign(mtmp);
			break;
	}
}

/* clone a gremlin or mold (2nd arg non-null implies heat as the trigger);
   hit points are cut in half (odd HP stays with original) */
struct monst *split_mon(struct monst *mon, struct monst *mtmp) {
	struct monst *mtmp2;
	char reason[BUFSZ];

	reason[0] = '\0';
	if (mtmp) sprintf(reason, " from %s heat",
			  (mtmp == &youmonst) ? "your" :
						s_suffix(mon_nam(mtmp)));

	if (mon == &youmonst) {
		mtmp2 = cloneu();
		if (mtmp2) {
			mtmp2->mhpmax = u.mhmax / 2;
			u.mhmax -= mtmp2->mhpmax;
			context.botl = 1;
			pline("You multiply%s!", reason);
		}
	} else {
		mtmp2 = clone_mon(mon, 0, 0);
		if (mtmp2) {
			mtmp2->mhpmax = mon->mhpmax / 2;
			mon->mhpmax -= mtmp2->mhpmax;
			if (canspotmon(mon))
				pline("%s multiplies%s!", Monnam(mon), reason);
		}
	}
	return mtmp2;
}
/*potion.c*/
