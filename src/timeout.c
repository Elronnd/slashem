/*	SCCS Id: @(#)timeout.c	3.4	2002/12/17	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "lev.h" /* for checking save modes */

static void stoned_dialogue(void);
static void vomiting_dialogue(void);
static void choke_dialogue(void);
static void slime_dialogue(void);
static void slime_dialogue(void);
static void slip_or_trip(void);
static void see_lamp_flicker(struct obj *, const char *);
static void lantern_message(struct obj *);
static void accelerate_timer(short, anything, long);
static void cleanup_burn(void *, long);

/* He is being petrified - dialogue by inmet!tower */
static const char *const stoned_texts[] = {
	"You are slowing down.",	    /* 5 */
	"Your limbs are stiffening.",	    /* 4 */
	"Your limbs have turned to stone.", /* 3 */
	"You have turned to stone.",	    /* 2 */
	"You are a statue."		    /* 1 */
};

static void stoned_dialogue() {
	long i = (Stoned & TIMEOUT);

	if (i > 0L && i <= SIZE(stoned_texts))
		plines(stoned_texts[SIZE(stoned_texts) - i]);
	switch (i) {
		case 5: // slowing down
			HFast = 0L;
			if (multi > 0) nomul(0);
			break;
		case 4: // limbs stiffening
			/* just one move left to save oneself so quit fiddling around;
			   don't stop attempt to eat tin--might be lizard or acidic */
			if (!Popeye(STONED)) stop_occupation();
			if (multi > 0) nomul(0);
			break;
		case 3: // limbs turned to stone
			stop_occupation();
			nomul(-3);	// can't move anymore
			nomovemsg = 0;
			break;
		default:
			break;
	}
	exercise(A_DEX, false);
}

/* He is getting sicker and sicker prior to vomiting */
static const char *const vomiting_texts[] = {
	"are feeling mildly nauseated.", /* 14 */
	"feel slightly confused.",	 /* 11 */
	"can't seem to think straight.", /* 8 */
	"feel incredibly sick.",	 /* 5 */
	"suddenly vomit!"		 /* 2 */
};

static void vomiting_dialogue() {
	long i = (Vomiting & TIMEOUT) / 3L;

	if ((((Vomiting & TIMEOUT) % 3L) == 2) && (i >= 0) && (i < SIZE(vomiting_texts)))
		pline("You %s", vomiting_texts[SIZE(vomiting_texts) - i - 1]);

	switch ((int)i) {
		case 0:
			vomit();
			morehungry(20);
			stop_occupation();
			if (multi > 0) nomul(0);
			break;
		case 2:
			make_stunned(HStun + d(2, 4), false);
			if (!Popeye(VOMITING)) stop_occupation();

		fallthru;
		case 3:
			make_confused(HConfusion + d(2, 4), false);
			if (multi > 0) nomul(0);
			break;
	}
	exercise(A_CON, false);
}

static const char *const choke_texts[] = {
	"You find it hard to breathe.",
	"You're gasping for air.",
	"You can no longer breathe.",
	"You're turning %s.",
	"You suffocate."};

static const char *const choke_texts2[] = {
	"Your %s is becoming constricted.",
	"Your blood is having trouble reaching your brain.",
	"The pressure on your %s increases.",
	"Your consciousness is fading.",
	"You suffocate."};

static void choke_dialogue() {
	long i = (Strangled & TIMEOUT);

	if (i > 0 && i <= SIZE(choke_texts)) {
		if (Breathless || !rn2(50))
			pline(choke_texts2[SIZE(choke_texts2) - i], body_part(NECK));
		else {
			const char *str = choke_texts[SIZE(choke_texts) - i];

			if (index(str, '%'))
				pline(str, hcolor(NH_BLUE));
			else
				plines(str);
		}
	}
	exercise(A_STR, false);
}

static const char *const slime_texts[] = {
	"You are turning a little %s.",	  /* 5 */
	"Your limbs are getting oozy.",	  /* 4 */
	"Your skin begins to peel away.", /* 3 */
	"You are turning into %s.",	  /* 2 */
	"You have become %s."		  /* 1 */
};

static void slime_dialogue() {
	long i = (Slimed & TIMEOUT) / 2L;

	if (((Slimed & TIMEOUT) % 2L) && i >= 0L && i < SIZE(slime_texts)) {
		const char *str = slime_texts[SIZE(slime_texts) - i - 1L];

		if (index(str, '%')) {
			if (i == 4L) {	    /* "you are turning green" */
				if (!Blind) /* [what if you're already green?] */
					pline(str, hcolor(NH_GREEN));
			} else
				pline(str, an(Hallucination ? rndmonnam() : "green slime"));
		} else
			plines(str);
	}
	if (i == 3L) {	    /* limbs becoming oozy */
		HFast = 0L; /* lose intrinsic speed */
		if (!Popeye(SLIMED)) stop_occupation();
		if (multi > 0) nomul(0);
	}
	exercise(A_DEX, false);
}

void burn_away_slime(void) {
	if (Slimed) {
		make_slimed(0, "The slime that covers you is burned away!");
	}
}

void nh_timeout(void) {
	struct prop *upp;
	struct kinfo *kptr;
	int sleeptime;
	int m_idx;
	int baseluck = (flags.moonphase == FULL_MOON) ? 1 : 0;

	if (flags.friday13) baseluck -= 1;

	if (u.uluck != baseluck &&
	    moves % (u.uhave.amulet || u.ugangr ? 300 : 600) == 0) {
		/* Cursed luckstones stop bad luck from timing out; blessed luckstones
		 * stop good luck from timing out; normal luckstones stop both;
		 * neither is stopped if you don't have a luckstone.
		 * Luck is based at 0 usually, +1 if a full moon and -1 on Friday 13th
		 */
		int time_luck = stone_luck(false);
		boolean nostone = !carrying(LUCKSTONE) && !stone_luck(true);

		if (u.uluck > baseluck && (nostone || time_luck < 0))
			u.uluck--;
		else if (u.uluck < baseluck && (nostone || time_luck > 0))
			u.uluck++;
	}

	/* WAC -- check for timeout of specials */
	tech_timeout();

	if (u.uinvulnerable) return; /* things past this point could kill you */
	if (Stoned) stoned_dialogue();
	if (Slimed) slime_dialogue();
	if (Vomiting) vomiting_dialogue();
	if (Strangled) choke_dialogue();
	if (u.mtimedone && !--u.mtimedone) {
		if (Unchanging)
			u.mtimedone = rnd(100 * youmonst.data->mlevel + 1);
		else
			rehumanize();
	}
	if (u.ucreamed) u.ucreamed--;

	/* Dissipate spell-based protection. */
	if (u.usptime) {
		if (--u.usptime == 0 && u.uspellprot) {
			u.usptime = u.uspmtime;
			u.uspellprot--;
			find_ac();
			if (!Blind)
				Norep("The %s haze around you %s.", hcolor(NH_GOLDEN),
				      u.uspellprot ? "becomes less dense" : "disappears");
		}
	}

	if (u.ugallop) {
		if (--u.ugallop == 0L && u.usteed)
			pline("%s stops galloping.", Monnam(u.usteed));
	}

	for (upp = u.uprops; upp < u.uprops + SIZE(u.uprops); upp++)
		if ((upp->intrinsic & TIMEOUT) && !(--upp->intrinsic & TIMEOUT)) {
			kptr = find_delayed_killer(upp - u.uprops);
			switch (upp - u.uprops) {
				case STONED:
					if (kptr && kptr->name.len) {
						killer.format = kptr->format;
						nhsmove(&killer.name, &kptr->name);
					} else {
						killer.format = NO_KILLER_PREFIX;
						nhscopyz(&killer.name, "killed by petrification");
					}
					dealloc_killer(kptr);
					done(STONING);
					break;
				case SLIMED:
					if (kptr && kptr->name.len) {
						killer.format = kptr->format;
						nhsmove(&killer.name, &kptr->name);
					} else {
						killer.format = NO_KILLER_PREFIX;
						nhscopyz(&killer.name, "turned into green slime");
					}
					u.uconduct.polyselfs++; // 'change form'
					dealloc_killer(kptr);
					done(TURNED_SLIME);
					break;
				case VOMITING:
					make_vomiting(0L, true);
					break;
				case SICK:
					pline("You die from your illness.");
					if (kptr && kptr->name.len) {
						killer.format = kptr->format;
						nhsmove(&killer.name, &kptr->name);
					} else {
						killer.format = KILLED_BY_AN;
						del_nhs(&killer.name);
					}
					dealloc_killer(kptr);

					if ((m_idx = name_to_mon(nhs2cstr_tmp(killer.name))) >= LOW_PM) {
						if (type_is_pname(&mons[m_idx])) {
							killer.format = KILLED_BY;
						} else if (mons[m_idx].geno & G_UNIQ) {
							killer.format = KILLED_BY;
							nhscopyz(&killer.name, the(nhs2cstr_tmp(killer.name)));
						}
					}
					u.usick_type = 0;
					done(POISONING);
					break;
				case FAST:
					if (!Very_fast)
						pline("You feel yourself slowing down%s.",
						      Fast ? " a bit" : "");
					break;
				case FIRE_RES:
					if (!Fire_resistance)
						pline("You feel a little warmer.");
					break;
				case COLD_RES:
					if (!Cold_resistance)
						pline("You feel a little cooler.");
					break;
				case SLEEP_RES:
					if (!Sleep_resistance)
						pline("You feel a little sleepy.");
					break;
				case SHOCK_RES:
					if (!Shock_resistance)
						pline("You feel a little static cling.");
					break;
				case POISON_RES:
					if (!Poison_resistance)
						pline("You feel a little less healthy.");
					break;
				case DISINT_RES:
					if (!Disint_resistance)
						pline("You feel a little less firm.");
					break;
				case TELEPORT:
					if (!Teleportation)
						pline("You feel a little less jumpy.");
					break;
				case TELEPORT_CONTROL:
					if (!Teleport_control)
						pline("You feel a little less in control of yourself.");
					break;
				case TELEPAT:
					if (!HTelepat)
						pline("You feel a little less mentally acute.");
					break;
				case FREE_ACTION:
					if (!Free_action)
						pline("You feel a little stiffer.");
					break;
				case PASSES_WALLS:
					if (!Passes_walls)
						pline("You feel a little more solid.");
					break;
				case INVULNERABLE:
					if (!Invulnerable)
						pline("You are no longer invulnerable.");
					break;
				case CONFUSION:
					HConfusion = 1; /* So make_confused works properly */
					make_confused(0L, true);
					stop_occupation();
					break;
				case STUNNED:
					HStun = 1;
					make_stunned(0L, true);
					stop_occupation();
					break;
				case BLINDED:
					Blinded = 1;
					make_blinded(0L, true);
					stop_occupation();
					break;
				case DEAF:
					if (!Deaf)
						pline("You can hear again.");
					stop_occupation();
					break;
				case INVIS:
					newsym(u.ux, u.uy);
					if (!Invis && !BInvis && !Blind) {
						pline(See_invisible ?
							      "You can no longer see through yourself." :
							      "You are no longer invisible.");
						stop_occupation();
					}
					break;
				case SEE_INVIS:
					set_mimic_blocking(); /* do special mimic handling */
					see_monsters();	      /* make invis mons appear */
					newsym(u.ux, u.uy);   /* make self appear */
					stop_occupation();
					break;
				case WOUNDED_LEGS:
					heal_legs();
					stop_occupation();
					break;
				case HALLUC:
					HHallucination = 1;
					make_hallucinated(0L, true, 0L);
					stop_occupation();
					break;
				case SLEEPING:
					if (unconscious() || Sleep_resistance)
						HSleeping += rnd(100);
					else if (Sleeping) {
						pline("You fall asleep.");
						sleeptime = rnd(20);
						fall_asleep(-sleeptime, true);
						HSleeping += sleeptime + rnd(100);
					}
					break;
				case LEVITATION:
					float_down(I_SPECIAL | TIMEOUT, 0L);
					break;
				case STRANGLED:
					killer.format = KILLED_BY;
					nhscopyz(&killer.name, (u.uburied) ? "suffocation" : "strangulation");
					done(DIED);
					break;
				case FUMBLING:
					/* call this only when a move took place.  */
					/* otherwise handle fumbling msgs locally. */
					if (u.umoved && !Levitation) {
						slip_or_trip();
						nomul(-2);
						nomovemsg = "";
						/* The more you are carrying the more likely you
					 * are to make noise when you fumble.  Adjustments
					 * to this number must be thoroughly play tested.
					 */
						if ((inv_weight() > -500)) {
							pline("You make a lot of noise!");
							wake_nearby();
						}
					}
					/* from outside means slippery ice; don't reset
				   counter if that's the only fumble reason */
					HFumbling &= ~FROMOUTSIDE;
					if (Fumbling)
						HFumbling += rnd(20);
					break;
				case DETECT_MONSTERS:
					see_monsters();
					break;
			}
		}

	run_timers();
}

void fall_asleep(int how_long, bool wakeup_msg) {
	stop_occupation();
	nomul(how_long);
	/* generally don't notice sounds while sleeping */
	if (wakeup_msg && multi == how_long) {
		/* caller can follow with a direct call to Hear_again() if
		   there's a need to override this when wakeup_msg is true */
		incr_itimeout(&HDeaf, how_long);
		afternmv = Hear_again; /* this won't give any messages */
	}
	/* early wakeup from combat won't be possible until next monster turn */
	u.usleep = monstermoves;
	nomovemsg = wakeup_msg ? "You wake up." : "You can move again.";
}

/* WAC polymorph an object
 * Unlike monsters,  this function is called after the polymorph
 */
void set_obj_poly(struct obj *obj, struct obj *old) {
	/* Same unpolytime (500,500) as for player */
	if (is_hazy(old))
		obj->oldtyp = old->oldtyp;
	else
		obj->oldtyp = old->otyp;
	if (obj->oldtyp == obj->otyp)
		obj->oldtyp = STRANGE_OBJECT;
	else
		start_timer(rn1(500, 500), TIMER_OBJECT, UNPOLY_OBJ, obj_to_any(obj));
	return;
}

/* timer callback routine: undo polymorph on an object */
void unpoly_obj(void *arg, long timeout) {
	struct obj *obj, *otmp, *otmp2;
	int oldobj, depthin;
	boolean silent = (timeout != monstermoves), /* unpoly'ed while away */
		explodes;

	obj = arg;
	if (!is_hazy(obj)) return;
	oldobj = obj->oldtyp;

	if (carried(obj) && !silent) /* silent == true is a strange case... */
		pline("Suddenly, your %s!", aobjnam(obj, "transmute"));

	stop_timer(UNPOLY_OBJ, obj_to_any(obj));

	obj = poly_obj(obj, oldobj);

	if (obj->otyp == WAN_CANCELLATION || Is_mbag(obj)) {
		otmp = obj;
		depthin = 0;
		explodes = false;

		while (otmp->where == OBJ_CONTAINED) {
			otmp = otmp->ocontainer;
			if (otmp->otyp == BAG_OF_HOLDING) {
				explodes = mbag_explodes(obj, depthin);
				break;
			}
			depthin++;
		}

		if (explodes) {
			otmp2 = otmp;
			while (otmp2->where == OBJ_CONTAINED) {
				otmp2 = otmp2->ocontainer;

				if (otmp2->otyp == BAG_OF_HOLDING)
					otmp = otmp2;
			}
			destroy_mbag(otmp, silent);
		}
	}
	return;
}

/*
 * Cleanup a hazy object if timer stopped.
 */
/*ARGSUSED*/
static void cleanup_unpoly(void *arg, long timeout) {
	struct obj *obj = (struct obj *)arg;
	obj->oldtyp = STRANGE_OBJECT;
	if (wizard && obj->where == OBJ_INVENT)
		update_inventory();
}

/* WAC polymorph a monster
 * returns 0 if no change, 1 if polymorphed and -1 if died.
 * This handles system shock for monsters so DON'T do system shock elsewhere
 * when polymorphing.
 * (except in unpolymorph code,  which is a special case)
 */
int mon_poly(struct monst *mtmp, boolean your_fault, const char *change_fmt) {
	if (change_fmt && canseemon(mtmp)) pline(change_fmt, Monnam(mtmp));
	return mon_spec_poly(mtmp, NULL, 0L,
			     false, canseemon(mtmp), true, your_fault);
}

/* WAC Muscle function - for more control over polying
 * returns 0 if no change, 1 if polymorphed and -1 if died.
 * cancels/sets up timers if polymorph is successful
 * lets receiver handle failures
 */

int mon_spec_poly(struct monst *mtmp, struct permonst *type, long when, boolean polyspot, boolean transform_msg, boolean system_shock, boolean your_fault) {
	int i;

	i = newcham(mtmp, type, polyspot, transform_msg);
	if (system_shock && (!i || !rn2(25))) {
		/* Uhoh.  !i == newcham wasn't able to make the polymorph...*/
		if (transform_msg) pline("%s shudders.", Monnam(mtmp));
		if (i) mtmp->mhp -= rnd(30);
		if (!i || (mtmp->mhp <= 0)) {
			if (your_fault)
				xkilled(mtmp, 3);
			else
				mondead(mtmp);
			i = -1;
		}
	}
	if (i > 0) {
		/* Stop any old timers.   */
		stop_timer(UNPOLY_MON, monst_to_any(mtmp));
		/* Lengthen unpolytime - was 500,500  for player */
		start_timer(when ? when : rn1(1000, 1000), TIMER_MONSTER, UNPOLY_MON, monst_to_any(mtmp));
	}
	return i;
}

/* timer callback routine: undo polymorph on a monster */
void unpoly_mon(void *arg, long timeout) {
	struct monst *mtmp;
	int oldmon;
	char oldname[BUFSZ];			    /* DON'T use char * since this will change! */
	boolean silent = (timeout != monstermoves); /* unpoly'ed while away */

	mtmp = arg;
	oldmon = mtmp->oldmonnm;

	strcpy(oldname, Monnam(mtmp));

	stop_timer(UNPOLY_MON, monst_to_any(mtmp));

	if (!newcham(mtmp, &mons[oldmon], false, (canseemon(mtmp) && !silent))) {
		/* Wasn't able to unpolymorph */
		if (canseemon(mtmp) && !silent) pline("%s shudders.", oldname);
		mondead(mtmp);
		return;
	}

	/* Check if current form is genocided */
	if (mvitals[oldmon].mvflags & G_GENOD) {
		mtmp->mhp = 0;
		if (canseemon(mtmp) && !silent) pline("%s shudders.", oldname);
		/*  Since only player can read scrolls of genocide... */
		xkilled(mtmp, 3);
		return;
	}

#if 0
	if (canseemon(mtmp)) pline ("%s changes into %s!",
		                            oldname, an(mtmp->data->mname));
#endif
	return;
}

// Attach an explosion timeout to a given explosive device
void attach_bomb_blow_timeout(struct obj *bomb, int fuse, boolean yours) {
	long expiretime;

	if (bomb->cursed && !rn2(2)) return; /* doesn't arm if not armed */

	/* Now if you play with other people's property... */
	if (yours && (!carried(bomb) && costly_spot(bomb->ox, bomb->oy) &&
		      (!bomb->no_charge || bomb->unpaid))) {
		verbalize("You play with it, you pay for it!");
		bill_dummy_object(bomb);
	}

	expiretime = stop_timer(BOMB_BLOW, obj_to_any(bomb));
	if (expiretime > 0L) fuse = fuse - (expiretime - monstermoves);
	bomb->yours = yours;
	bomb->oarmed = true;

	start_timer(fuse, TIMER_OBJECT, BOMB_BLOW, obj_to_any(bomb));
}

// timer callback routine: detonate the explosives
void bomb_blow(void *arg, long timeout) {
	struct obj *bomb = arg;
	xchar x, y;
	boolean silent, underwater;
	struct monst *mtmp;

	silent = (timeout != monstermoves); /* exploded while away */

	if (get_obj_location(bomb, &x, &y, BURIED_TOO | CONTAINED_TOO)) {
		switch (bomb->where) {
			case OBJ_MINVENT:
				mtmp = bomb->ocarry;
				if (bomb == MON_WEP(mtmp)) {
					bomb->owornmask &= ~W_WEP;
					MON_NOWEP(mtmp);
				}
				if (!silent) {
					if (canseemon(mtmp))
						pline("You see %s engulfed in an explosion!", mon_nam(mtmp));
				}
				mtmp->mhp -= d(2, 5);
				if (mtmp->mhp < 1) {
					if (!bomb->yours)
						monkilled(mtmp,
							  (silent ? "" : "explosion"),
							  AD_PHYS);
					else
						xkilled(mtmp, !silent);
				}
				break;
			case OBJ_INVENT:
				/* This shouldn't be silent! */
				pline("Something explodes inside your knapsack!");
				if (bomb == uwep) {
					uwepgone();
					stop_occupation();
				} else if (bomb == uswapwep) {
					uswapwepgone();
					stop_occupation();
				} else if (bomb == uquiver) {
					uqwepgone();
					stop_occupation();
				}
				losehp(d(2, 5), "carrying live explosives", KILLED_BY);
				break;
			case OBJ_FLOOR:
				underwater = is_pool(x, y);
				if (!silent) {
					if (x == u.ux && y == u.uy) {
						if (underwater && (Flying || Levitation))
							pline("The water boils beneath you.");
						else if (underwater && Wwalking)
							pline("The water erupts around you.");
						else
							pline("A bomb explodes under your %s!",
							      makeplural(body_part(FOOT)));
					} else if (cansee(x, y))
						pline(underwater ?
							      "You see a plume of water shoot up." :
							      "You see a bomb explode.");
				}
				if (underwater && (Flying || Levitation || Wwalking)) {
					if (Wwalking && x == u.ux && y == u.uy) {
						struct trap trap;
						trap.ntrap = NULL;
						trap.tx = x;
						trap.ty = y;
						trap.launch.x = -1;
						trap.launch.y = -1;
						trap.ttyp = RUST_TRAP;
						trap.tseen = 0;
						trap.once = 0;
						trap.madeby_u = 0;
						trap.dst.dnum = -1;
						trap.dst.dlevel = -1;
						dotrap(&trap, 0);
					}
					goto free_bomb;
				}
				break;
			default: /* Buried, contained, etc. */
				if (!silent)
					You_hear("a muffled explosion.");
				goto free_bomb;
				break;
		}
		grenade_explode(bomb, x, y, bomb->yours, silent ? 2 : 0);
		return;
	} /* Migrating grenades "blow up in midair" */

free_bomb:
	obj_extract_self(bomb);
	obfree(bomb, NULL);
}

// Attach an egg hatch timeout to the given egg.
void attach_egg_hatch_timeout(struct obj *egg) {
	int i;

	/* stop previous timer, if any */
	stop_timer(HATCH_EGG, obj_to_any(egg));

	/*
	 * Decide if and when to hatch the egg.  The old hatch_it() code tried
	 * once a turn from age 151 to 200 (inclusive), hatching if it rolled
	 * a number x, 1<=x<=age, where x>150.  This yields a chance of
	 * hatching > 99.9993%.  Mimic that here.
	 */
	for (i = (MAX_EGG_HATCH_TIME - 50) + 1; i <= MAX_EGG_HATCH_TIME; i++)
		if (rnd(i) > 150) {
			/* egg will hatch */
			start_timer((long)i, TIMER_OBJECT, HATCH_EGG, obj_to_any(egg));
			break;
		}
}

/* prevent an egg from ever hatching */
void kill_egg(struct obj *egg) {
	/* stop previous timer, if any */
	stop_timer(HATCH_EGG, obj_to_any(egg));
}

/* timer callback routine: hatch the given egg */
void hatch_egg(void *arg, long timeout) {
	struct obj *egg = arg;
	struct monst *mon, *mon2;
	coord cc;
	xchar x, y;
	boolean yours, silent, knows_egg = false;
	boolean cansee_hatchspot = false;
	int i, mnum, hatchcount = 0;

	/* sterilized while waiting */
	if (egg->corpsenm == NON_PM) return;

	mon = mon2 = NULL;
	mnum = big_to_little(egg->corpsenm);
	/* The identity of one's father is learned, not innate */
	yours = (egg->spe || (!flags.female && carried(egg) && !rn2(2)));
	silent = (timeout != monstermoves); /* hatched while away */

	/* only can hatch when in INVENT, FLOOR, MINVENT */
	if (get_obj_location(egg, &x, &y, 0)) {
		hatchcount = rnd((int)egg->quan);
		cansee_hatchspot = cansee(x, y) && !silent;
		if (!(mons[mnum].geno & G_UNIQ) &&
		    !(mvitals[mnum].mvflags & (G_GENOD | G_EXTINCT))) {
			for (i = hatchcount; i > 0; i--) {
				if (!enexto(&cc, x, y, &mons[mnum]) ||
				    !(mon = makemon(&mons[mnum], cc.x, cc.y, NO_MINVENT)))
					break;
				/* tame if your own egg hatches while you're on the
				   same dungeon level, or any dragon egg which hatches
				   while it's in your inventory */
				if ((yours && !silent) ||
				    (carried(egg) && mon->data->mlet == S_DRAGON)) {
					if (tamedog(mon, NULL)) {
						if (carried(egg) && mon->data->mlet != S_DRAGON)
							mon->mtame = 20;
					}
				}
				if (mvitals[mnum].mvflags & G_EXTINCT)
					break; /* just made last one */
				mon2 = mon;    /* in case makemon() fails on 2nd egg */
			}
			if (!mon) mon = mon2;
			hatchcount -= i;
			egg->quan -= (long)hatchcount;
		}
	}
#if 0
	/*
	 * We could possibly hatch while migrating, but the code isn't
	 * set up for it...
	 */
	else if (obj->where == OBJ_MIGRATING) {
		/*
		We can do several things.  The first ones that come to
		mind are:

		+ Create the hatched monster then place it on the migrating
		  mons list.  This is tough because all makemon() is made
		  to place the monster as well.    Makemon() also doesn't
		  lend itself well to splitting off a "not yet placed"
		  subroutine.

		+ Mark the egg as hatched, then place the monster when we
		  place the migrating objects.

		+ Or just kill any egg which gets sent to another level.
		  Falling is the usual reason such transportation occurs.
		*/
		cansee_hatchspot = false;
		mon = ???
	}
#endif

	if (mon) {
		char monnambuf[BUFSZ], carriedby[BUFSZ];
		boolean siblings = (hatchcount > 1), redraw = false;

		if (cansee_hatchspot) {
			sprintf(monnambuf, "%s%s",
				siblings ? "some " : "",
				siblings ?
					makeplural(m_monnam(mon)) :
					an(m_monnam(mon)));
			/* we don't learn the egg type here because learning
			   an egg type requires either seeing the egg hatch
			   or being familiar with the egg already,
			   as well as being able to see the resulting
			   monster, checked below
			*/
		}
		switch (egg->where) {
			case OBJ_INVENT:
				knows_egg = true; /* true even if you are blind */
				if (!cansee_hatchspot)
					pline("You feel something %s from your pack!",
					      locomotion(mon->data, "drop"));
				else
					pline("You see %s %s out of your pack!",
					      monnambuf, locomotion(mon->data, "drop"));
				if (yours) {
					pline("%s cries sound like \"%s%s\"",
					      siblings ? "Their" : "Its",
					      flags.female ? "mommy" : "daddy",
					      egg->spe ? "." : "?");
				} else if (mon->data->mlet == S_DRAGON && !Deaf) {
					verbalize("Gleep!"); /* Mything eggs :-) */
				}
				break;

			case OBJ_FLOOR:
				if (cansee_hatchspot) {
					knows_egg = true;
					pline("You see %s hatch.", monnambuf);
					redraw = true; /* update egg's map location */
				}
				break;

			case OBJ_MINVENT:
				if (cansee_hatchspot) {
					/* egg carring monster might be invisible */
					if (canseemon(egg->ocarry)) {
						sprintf(carriedby, "%s pack",
							s_suffix(a_monnam(egg->ocarry)));
						knows_egg = true;
					} else if (is_pool(mon->mx, mon->my))
						strcpy(carriedby, "empty water");
					else
						strcpy(carriedby, "thin air");
					pline("You see %s %s out of %s!", monnambuf,
					      locomotion(mon->data, "drop"), carriedby);
				}
				break;
#if 0
		case OBJ_MIGRATING:
			break;
#endif
			default:
				impossible("egg hatched where? (%d)", (int)egg->where);
				break;
		}

		if (cansee_hatchspot && knows_egg)
			learn_egg_type(mnum);

		if (egg->quan > 0) {
			/* still some eggs left */
			attach_egg_hatch_timeout(egg);
			if (egg->timed) {
				/* replace ordinary egg timeout with a short one */
				stop_timer(HATCH_EGG, obj_to_any(egg));
				start_timer((long)rnd(12), TIMER_OBJECT, HATCH_EGG, obj_to_any(egg));
			}
		} else if (carried(egg)) {
			useup(egg);
		} else {
			/* free egg here because we use it above */
			obj_extract_self(egg);
			obfree(egg, NULL);
		}
		if (redraw) newsym(x, y);
	}
}

/* Learn to recognize eggs of the given type. */
void learn_egg_type(int mnum) {
	/* baby monsters hatch from grown-up eggs */
	mnum = little_to_big(mnum);
	mvitals[mnum].mvflags |= MV_KNOWS_EGG;
	/* we might have just learned about other eggs being carried */
	update_inventory();
}

/* Attach a fig_transform timeout to the given figurine. */
void attach_fig_transform_timeout(struct obj *figurine) {
	int i;

	/* stop previous timer, if any */
	stop_timer(FIG_TRANSFORM, obj_to_any(figurine));

	/*
	 * Decide when to transform the figurine.
	 */
	i = rnd(9000) + 200;
	/* figurine will transform */
	start_timer((long)i, TIMER_OBJECT, FIG_TRANSFORM, obj_to_any(figurine));
}

/* give a fumble message */
static void slip_or_trip(void) {
	struct obj *otmp = vobj_at(u.ux, u.uy);
	const char *what, *pronoun;
	char buf[BUFSZ];
	boolean on_foot = true;
	if (u.usteed) on_foot = false;

	if (otmp && on_foot && !u.uinwater && is_pool(u.ux, u.uy)) otmp = 0;

	// trip over something in particular
	if (otmp && on_foot) {
		/*
		If there is only one item, it will have just been named
		during the move, so refer to by via pronoun; otherwise,
		if the top item has been or can be seen, refer to it by
		name; if not, look for rocks to trip over; trip over
		anonymous "something" if there aren't any rocks.
		 */
		pronoun = otmp->quan == 1L ? "it" : Hallucination ? "they" : "them";
		what = !otmp->nexthere ? pronoun :
					 (otmp->dknown || !Blind) ? doname(otmp) :
								    ((otmp = sobj_at(ROCK, u.ux, u.uy)) == 0 ? "something" :
													       (otmp->quan == 1L ? "a rock" : "some rocks"));
		if (Hallucination) {
			what = strcpy(buf, what);
			buf[0] = highc(buf[0]);
			pline("Egads!  %s bite%s your %s!",
			      what, (!otmp || otmp->quan == 1L) ? "s" : "",
			      body_part(FOOT));
		} else {
			pline("You trip over %s.", what);
		}

		if (!uarmf && otmp->otyp == CORPSE && touch_petrifies(&mons[otmp->corpsenm]) && !Stone_resistance) {
			nhscopyf(&killer.name, "tripping over %S corpse", an(mons[otmp->corpsenm].mname));
			instapetrify(nhs2cstr_tmp(killer.name));
		}
	} else if (rn2(3) && is_ice(u.ux, u.uy)) {
		pline("%s %s%s on the ice.",
		      u.usteed ? upstart(x_monnam(u.usteed,
						  has_name(u.usteed) ? ARTICLE_NONE : ARTICLE_THE,
						  NULL, SUPPRESS_SADDLE, false)) :
				 "You",
		      rn2(2) ? "slip" : "slide", on_foot ? "" : "s");
	} else {
		if (on_foot) {
			switch (rn2(4)) {
				case 1:
					pline("You trip over your own %s.", Hallucination ?
										    "elbow" :
										    makeplural(body_part(FOOT)));
					break;
				case 2:
					pline("You slip %s.", Hallucination ?
								      "on a banana peel" :
								      "and nearly fall");
					break;
				case 3:
					pline("You flounder.");
					break;
				default:
					pline("You stumble.");
					break;
			}
		} else {
			switch (rn2(4)) {
				case 1:
					pline("Your %s slip out of the stirrups.", makeplural(body_part(FOOT)));
					break;
				case 2:
					pline("You let go of the reins.");
					break;
				case 3:
					pline("You bang into the saddle-horn.");
					break;
				default:
					pline("You slide to one side of the saddle.");
					break;
			}
			dismount_steed(DISMOUNT_FELL);
		}
	}
}

/* Print a lamp flicker message with tailer. */
static void see_lamp_flicker(struct obj *obj, const char *tailer) {
	switch (obj->where) {
		case OBJ_INVENT:
		case OBJ_MINVENT:
			pline("%s flickers%s.", Yname2(obj), tailer);
			break;
		case OBJ_FLOOR:
			pline("You see %s flicker%s.", an(xname(obj)), tailer);
			break;
	}
}

/* Print a dimming message for brass lanterns. */
static void lantern_message(struct obj *obj) {
	/* from adventure */
	switch (obj->where) {
		case OBJ_INVENT:
			pline("Your lantern is getting dim.");
			if (Hallucination)
				pline("Batteries have not been invented yet.");
			break;
		case OBJ_FLOOR:
			pline("You see a lantern getting dim.");
			break;
		case OBJ_MINVENT:
			pline("%s lantern is getting dim.",
			      s_suffix(Monnam(obj->ocarry)));
			break;
	}
}

/*
 * Timeout callback for for objects that are burning. E.g. lamps, candles.
 * See begin_burn() for meanings of obj->age and obj->spe.
 */
void burn_object(void *arg, long timeout) {
	struct obj *obj = (struct obj *)arg;
	boolean canseeit, many, menorah, need_newsym;
	xchar x, y;
	char whose[BUFSZ];

	menorah = obj->otyp == CANDELABRUM_OF_INVOCATION;
	many = menorah ? obj->spe > 1 : obj->quan > 1L;

	/* timeout while away */
	if (timeout != monstermoves) {
		long how_long = monstermoves - timeout;

		if (how_long >= obj->age) {
			obj->age = 0;
			end_burn(obj, false);

			if (menorah) {
				obj->spe = 0; /* no more candles */
			} else if (Is_candle(obj) || obj->otyp == POT_OIL) {
				/* get rid of candles and burning oil potions */
				obj_extract_self(obj);
				obfree(obj, NULL);
				obj = NULL;
			} else if (obj->otyp == STICK_OF_DYNAMITE) {
				bomb_blow(obj, timeout);
				return;
			}

		} else {
			obj->age -= how_long;
			begin_burn(obj, true);
		}
		return;
	}

	/* only interested in INVENT, FLOOR, and MINVENT */
	if (get_obj_location(obj, &x, &y, 0)) {
		canseeit = !Blind && cansee(x, y);
		/* set `whose[]' to be "Your" or "Fred's" or "The goblin's" */
		Shk_Your(whose, obj);
	} else {
		canseeit = false;
	}
	need_newsym = false;

	/* obj->age is the age remaining at this point.  */
	switch (obj->otyp) {
		case POT_OIL:
			/* this should only be called when we run out */
			if (canseeit) {
				switch (obj->where) {
					case OBJ_INVENT:
					case OBJ_MINVENT:
						pline("%s potion of oil has burnt away.",
						      whose);
						break;
					case OBJ_FLOOR:
						pline("You see a burning potion of oil go out.");
						need_newsym = true;
						break;
				}
			}
			end_burn(obj, false); /* turn off light source */
			obj_extract_self(obj);
			obfree(obj, NULL);
			obj = NULL;
			break;

		case TORCH:
		case BRASS_LANTERN:
		case OIL_LAMP:
			switch ((int)obj->age) {
				case 150:
				case 100:
				case 50:
					if (canseeit) {
						if (obj->otyp == BRASS_LANTERN)
							lantern_message(obj);
						else
							see_lamp_flicker(obj,
									 obj->age == 50L ? " considerably" : "");
					}
					break;

				case 25:
					if (canseeit) {
						if (obj->otyp == BRASS_LANTERN)
							lantern_message(obj);
						else {
							switch (obj->where) {
								case OBJ_INVENT:
								case OBJ_MINVENT:
									pline("%s seems about to go out.", Yname2(obj));
									break;
								case OBJ_FLOOR:
									pline("You see %s about to go out.",
									      an(xname(obj)));
									break;
							}
						}
					}
					break;

				case 0:
					/* even if blind you'll know if holding it */
					if (canseeit || obj->where == OBJ_INVENT) {
						switch (obj->where) {
							case OBJ_INVENT:
							case OBJ_MINVENT:
								if (obj->otyp == BRASS_LANTERN)
									pline("%s lantern has run out of power.",
									      whose);
								else
									pline("%s has gone out.", Yname2(obj));
								break;
							case OBJ_FLOOR:
								if (obj->otyp == BRASS_LANTERN)
									pline("You see a lantern run out of power.");
								else
									pline("You see %s go out.",
									      an(xname(obj)));
								break;
						}
					}

					/* MRKR: Burnt out torches are considered worthless */

					if (obj->otyp == TORCH) {
						if (obj->unpaid && costly_spot(u.ux, u.uy)) {
							const char *ithem = obj->quan > 1L ? "them" : "it";
							verbalize("You burn %s, you bought %s!", ithem, ithem);
							bill_dummy_object(obj);
						}
					}
					end_burn(obj, false);
					break;

				default:
					/*
			 * Someone added fuel to the lamp while it was
			 * lit.  Just fall through and let begin burn
			 * handle the new age.
			 */
					break;
			}

			if (obj->age)
				begin_burn(obj, true);

			break;

		case CANDELABRUM_OF_INVOCATION:
		case TALLOW_CANDLE:
		case WAX_CANDLE:
			switch (obj->age) {
				case 75:
					if (canseeit)
						switch (obj->where) {
							case OBJ_INVENT:
							case OBJ_MINVENT:
								pline("%s %scandle%s getting short.",
								      whose,
								      menorah ? "candelabrum's " : "",
								      many ? "s are" : " is");
								break;
							case OBJ_FLOOR:
								pline("You see %scandle%s getting short.",
								      menorah ? "a candelabrum's " :
										many ? "some " : "a ",
								      many ? "s" : "");
								break;
						}
					break;

				case 15:
					if (canseeit)
						switch (obj->where) {
							case OBJ_INVENT:
							case OBJ_MINVENT:
								pline("%s%scandle%s flame%s flicker%s low!",
									whose,
									menorah ? "candelabrum's " : "",
									many ? "s'" : "'s",
									many ? "s" : "",
									many ? "" : "s");
								break;
							case OBJ_FLOOR:
								pline("You see %scandle%s flame%s flicker low!",
								      menorah ? "a candelabrum's " :
										many ? "some " : "a ",
								      many ? "s'" : "'s",
								      many ? "s" : "");
								break;
						}
					break;

				case 0:
					/* we know even if blind and in our inventory */
					if (canseeit || obj->where == OBJ_INVENT) {
						if (menorah) {
							switch (obj->where) {
								case OBJ_INVENT:
								case OBJ_MINVENT:
									pline("%scandelabrum's flame%s.",
									      whose, many ? "s die" : " dies");
									break;
								case OBJ_FLOOR:
									pline("You see a candelabrum's flame%s die.",
									      many ? "s" : "");
									break;
							}
						} else {
							switch (obj->where) {
								case OBJ_INVENT:
								case OBJ_MINVENT:
									pline("%s %s consumed!",
									      Yname2(obj),
									      many ? "are" : "is");
									break;
								case OBJ_FLOOR:
									/*
						You see some wax candles consumed!
						You see a wax candle consumed!
						*/
									pline("You see %s%s consumed!",
									      many ? "some " : "",
									      many ? xname(obj) : an(xname(obj)));
									need_newsym = true;
									break;
							}

							/* post message */
							pline(Hallucination ?
								      (many ? "They shriek!" :
									      "It shrieks!") :
								      Blind ? "" :
									      (many ? "Their flames die." :
										      "Its flame dies."));
						}
					}
					end_burn(obj, false);

					if (menorah) {
						obj->spe = 0;
					} else {
						obj_extract_self(obj);
						obfree(obj, NULL);
						obj = NULL;
					}
					break;

				default:
					/*
			 * Someone added fuel (candles) to the menorah while
			 * it was lit.  Just fall through and let begin burn
			 * handle the new age.
			 */
					break;
			}

			if (obj && obj->age)
				begin_burn(obj, true);

			break;

		case RED_DOUBLE_LIGHTSABER:
			if (obj->altmode && obj->cursed && !rn2(25)) {
				obj->altmode = false;
				pline("%s %s reverts to single blade mode!",
				      whose, xname(obj));
			}
		fallthru;
		case GREEN_LIGHTSABER:
		case BLUE_LIGHTSABER:
		case RED_LIGHTSABER:
			/* Callback is checked every 5 turns -
			lightsaber automatically deactivates if not wielded */
			if ((obj->cursed && !rn2(50)) ||
			    (obj->where == OBJ_FLOOR) ||
			    (obj->where == OBJ_MINVENT &&
			     (!MON_WEP(obj->ocarry) || MON_WEP(obj->ocarry) != obj)) ||
			    (obj->where == OBJ_INVENT &&
			     ((!uwep || uwep != obj) &&
			      (!u.twoweap || !uswapwep || obj != uswapwep))))
				lightsaber_deactivate(obj, false);
			switch (obj->age) {
				case 100:
					/* Single warning time */
					if (canseeit) {
						switch (obj->where) {
							case OBJ_INVENT:
							case OBJ_MINVENT:
								pline("%s %s dims!", whose, xname(obj));
								break;
							case OBJ_FLOOR:
								pline("You see %s dim!", an(xname(obj)));
								break;
						}
					} else {
						pline("You hear the hum of %s change!", an(xname(obj)));
					}
					break;
				case 0:
					lightsaber_deactivate(obj, false);
					break;

				default:
					/*
			 * Someone added fuel to the lightsaber while it was
			 * lit.  Just fall through and let begin burn
			 * handle the new age.
			 */
					break;
			}
			if (obj && obj->age && obj->lamplit) /* might be deactivated */
				begin_burn(obj, true);
			break;

		case STICK_OF_DYNAMITE:
			end_burn(obj, false);
			bomb_blow(obj, timeout);
			return;

		default:
			impossible("burn_object: unexpeced obj %s", xname(obj));
			break;
	}
	if (need_newsym) newsym(x, y);
}

/* lightsabers deactivate when they hit the ground/not wielded */
/* assumes caller checks for correct conditions */
void lightsaber_deactivate(struct obj *obj, boolean timer_attached) {
	xchar x, y;
	char whose[BUFSZ];

	Shk_Your(whose, obj);

	if (get_obj_location(obj, &x, &y, 0)) {
		if (cansee(x, y)) {
			switch (obj->where) {
				case OBJ_INVENT:
				case OBJ_MINVENT:
					pline("%s %s deactivates.", whose, xname(obj));
					break;
				case OBJ_FLOOR:
					pline("You see %s deactivate.", an(xname(obj)));
					break;
			}
		} else {
			pline("You hear a lightsaber deactivate.");
		}
	}
	if (obj->otyp == RED_DOUBLE_LIGHTSABER) obj->altmode = false;
	if ((obj == uwep) || (u.twoweap && obj != uswapwep)) unweapon = true;
	end_burn(obj, timer_attached);
}

/*
 * Start a burn timeout on the given object. If not "already lit" then
 * create a light source for the vision system.  There had better not
 * be a burn already running on the object.
 *
 * Magic lamps stay lit as long as there's a genie inside, so don't start
 * a timer.
 *
 * Burn rules:
 *      torches
 *		age = # of turns of fuel left
 *		spe = <weapon plus of torch, not used here>
 *
 *	potions of oil, lamps & candles:
 *		age = # of turns of fuel left
 *		spe = <unused>
 *
 *	magic lamps:
 *		age = <unused>
 *		spe = 0 not lightable, 1 lightable forever
 *
 *	candelabrum:
 *		age = # of turns of fuel left
 *		spe = # of candles
 *
 * Once the burn begins, the age will be set to the amount of fuel
 * remaining _once_the_burn_finishes_.  If the burn is terminated
 * early then fuel is added back.
 *
 * This use of age differs from the use of age for corpses and eggs.
 * For the latter items, age is when the object was created, so we
 * know when it becomes "bad".
 *
 * This is a "silent" routine - it should not print anything out.
 */
void begin_burn(struct obj *obj, boolean already_lit) {
	int radius = 3;
	long turns = 0;
	boolean do_timer = true;

	if (obj->age == 0 && obj->otyp != MAGIC_LAMP &&
	    obj->otyp != MAGIC_CANDLE && !artifact_light(obj))
		return;

	switch (obj->otyp) {
		case MAGIC_LAMP:
		case MAGIC_CANDLE:
			obj->lamplit = 1;
			do_timer = false;
			if (obj->otyp == MAGIC_CANDLE) obj->age = 300L;
			break;
		case RED_DOUBLE_LIGHTSABER:
			if (obj->altmode && obj->age > 1)
				obj->age--; /* Double power usage */
		fallthru;
		case RED_LIGHTSABER:
		case BLUE_LIGHTSABER:
		case GREEN_LIGHTSABER:
			turns = 1;
			radius = 1;
			break;
		case POT_OIL:
			turns = obj->age;
			radius = 1; /* very dim light */
			break;
		case STICK_OF_DYNAMITE:
			turns = obj->age;
			radius = 1; /* very dim light */
			break;

		case BRASS_LANTERN:
		case OIL_LAMP:
		case TORCH:
			/* magic times are 150, 100, 50, 25, and 0 */
			if (obj->age > 150L)
				turns = obj->age - 150L;
			else if (obj->age > 100L)
				turns = obj->age - 100L;
			else if (obj->age > 50L)
				turns = obj->age - 50L;
			else if (obj->age > 25L)
				turns = obj->age - 25L;
			else
				turns = obj->age;
			break;

		case CANDELABRUM_OF_INVOCATION:
		case TALLOW_CANDLE:
		case WAX_CANDLE:
			/* magic times are 75, 15, and 0 */
			if (obj->age > 75L)
				turns = obj->age - 75L;
			else if (obj->age > 15L)
				turns = obj->age - 15L;
			else
				turns = obj->age;
			radius = candle_light_range(obj);
			break;

		default:
			/* [ALI] Support artifact light sources */
			if (obj->oartifact && artifact_light(obj)) {
				obj->lamplit = 1;
				do_timer = false;
				radius = 2;
			} else {
				impossible("begin burn: unexpected %s", xname(obj));
				turns = obj->age;
			}
			break;
	}

	if (do_timer) {
		if (start_timer(turns, TIMER_OBJECT, BURN_OBJECT, obj_to_any(obj))) {
			obj->lamplit = 1;
			obj->age -= turns;
			if (carried(obj) && !already_lit)
				update_inventory();
		} else {
			obj->lamplit = 0;
		}
	} else {
		if (carried(obj) && !already_lit)
			update_inventory();
	}

	if (obj->lamplit && !already_lit) {
		xchar x, y;

		if (get_obj_location(obj, &x, &y, CONTAINED_TOO | BURIED_TOO))
			new_light_source(x, y, radius, LS_OBJECT, obj_to_any(obj));
		else
			impossible("begin_burn: can't get obj position");
	}
}

/*
 * Stop a burn timeout on the given object if timer attached.  Darken
 * light source.
 */
void end_burn(struct obj *obj, boolean timer_attached) {
	if (!obj->lamplit) {
		impossible("end_burn: obj %s not lit", xname(obj));
		return;
	}

	if (obj->otyp == MAGIC_LAMP || obj->otyp == MAGIC_CANDLE ||
	    artifact_light(obj))
		timer_attached = false;

	if (!timer_attached) {
		/* [DS] Cleanup explicitly, since timer cleanup won't happen */
		del_light_source(LS_OBJECT, obj_to_any(obj));
		obj->lamplit = 0;
		if (obj->where == OBJ_INVENT)
			update_inventory();
	} else if (!stop_timer(BURN_OBJECT, obj_to_any(obj)))
		impossible("end_burn: obj %s not timed!", xname(obj));
}

/*
 * Cleanup a burning object if timer stopped.
 */
static void cleanup_burn(void *arg, long expire_time) {
	struct obj *obj = arg;
	if (!obj->lamplit) {
		impossible("cleanup_burn: obj %s not lit", xname(obj));
		return;
	}

	del_light_source(LS_OBJECT, obj_to_any(obj));

	/* restore unused time */
	obj->age += expire_time - monstermoves;

	obj->lamplit = 0;

	if (obj->where == OBJ_INVENT)
		update_inventory();
}

/*
 * MRKR: Use up some fuel quickly, eg: when hitting a monster with
 *       a torch.
 */

void burn_faster(struct obj *obj, long adj) {
	if (!obj->lamplit) {
		impossible("burn_faster: obj %s not lit", xname(obj));
		return;
	}

	accelerate_timer(BURN_OBJECT, obj_to_any(obj), adj);
}

void do_storms(void) {
	int nstrike;
	int x, y;
	int dirx, diry;
	int count;

	/* no lightning if not the air level or too often, even then */
	if (!Is_airlevel(&u.uz) || rn2(8))
		return;

	/* the number of strikes is 8-log2(nstrike) */
	for (nstrike = rnd(64); nstrike <= 64; nstrike *= 2) {
		count = 0;
		do {
			x = rnd(COLNO - 1);
			y = rn2(ROWNO);
		} while (++count < 100 && levl[x][y].typ != CLOUD);

		if (count < 100) {
			dirx = rn2(3) - 1;
			diry = rn2(3) - 1;
			if (dirx != 0 || diry != 0)
				buzz(-15, /* "monster" LIGHTNING spell */
				     8, x, y, dirx, diry);
		}
	}

	if (levl[u.ux][u.uy].typ == CLOUD) {
		/* inside a cloud during a thunder storm is deafening */
		/* Even if deaf, we sense the thunder's vibrations */
		pline("Kaboom!!!  Boom!!  Boom!!");
		incr_itimeout(&HDeaf, rn1(20, 30));
		if (!u.uinvulnerable) {
			stop_occupation();
			nomul(-3);
			nomovemsg = 0;
		}
	} else
		You_hear("a rumbling noise.");
}

/* ------------------------------------------------------------------------- */
/*
 * Generic Timeout Functions.
 *
 * Interface:
 *
 * General:
 *	boolean start_timer(long timeout, short kind, short func_index, anything arg)
 *		Start a timer of kind 'kind' that will expire at time
 *		monstermoves+'timeout'.  Call the function at 'func_index'
 *		in the timeout table using argument 'arg'.  Return true if
 *		a timer was started.  This places the timer on a list ordered
 *		"sooner" to "later".  If an object, increment the object's
 *		timer count.
 *
 *	long stop_timer(short func_index, anything arg)
 *		Stop a timer specified by the (func_index, arg) pair.  This
 *		assumes that such a pair is unique.  Return the time the
 *		timer would have gone off.  If no timer is found, return 0.
 *		If an object, decrement the object's timer count.
 *
 *	long peek_timer(short func_index, anything arg)
 *		Return time specified timer will go off (0 if no such timer).
 *
 *
 *	void run_timers(void)
 *		Call timers that have timed out.
 *
 *
 * Save/Restore:
 *	void save_timers(int fd, int mode, int range)
 *		Save all timers of range 'range'.  Range is either global
 *		or local.  Global timers follow game play, local timers
 *		are saved with a level.  Object and monster timers are
 *		saved using their respective id's instead of pointers.
 *
 *	void restore_timers(int fd, int range, boolean ghostly, long adjust)
 *		Restore timers of range 'range'.  If from a ghost pile,
 *		adjust the timeout by 'adjust'.  The object and monster
 *		ids are not restored until later.
 *
 *	void relink_timers(boolean ghostly)
 *		Relink all object and monster timers that had been saved
 *		using their object's or monster's id number.
 *
 * Object Specific:
 *	void obj_move_timers(struct obj *src, struct obj *dest)
 *		Reassign all timers from src to dest.
 *
 *	void obj_split_timers(struct obj *src, struct obj *dest)
 *		Duplicate all timers assigned to src and attach them to dest.
 *
 *	void obj_stop_timers(struct obj *obj)
 *		Stop all timers attached to obj.
 *
 *	bool obj_has_timer(struct obj *object, short timer_type)
 *		Check whether object has a timer of type timer_type.
 *
 * Monster Specific:
 *	void mon_stop_timers(struct monst *mon)
 *		Stop all timers attached to mon.
 */

static const char *kind_name(short);
static void print_queue(winid, timer_element *);
static void insert_timer(timer_element *);
static timer_element *remove_timer(timer_element **, short, anything);
static void write_timer(int, timer_element *);
static boolean mon_is_local(struct monst *);
static boolean timer_is_local(timer_element *);
static int maybe_write_timer(int, int, boolean);
static void write_timer(int, timer_element *); /* Damn typedef write_timer is in the middle */

/* ordered timer list */
static timer_element *timer_base; /* "active" */
static unsigned long timer_id = 1;

typedef struct {
	timeout_proc f, cleanup;
	const char *name;
} ttable;

/* table of timeout functions */
static const ttable timeout_funcs[NUM_TIME_FUNCS] = {
	{rot_organic, NULL, "rot_organic"},
	{rot_corpse, NULL, "rot_corpse"},
	{moldy_corpse, NULL, "moldy_corpse"},
	{revive_mon, NULL, "revive_mon"},
	{burn_object, cleanup_burn, "burn_object"},
	{hatch_egg, NULL, "hatch_egg"},
	{fig_transform, NULL, "fig_transform"},
	{unpoly_mon, NULL, "unpoly_mon"},
	{bomb_blow, NULL, "bomb_blow"},
	{unpoly_obj, cleanup_unpoly, "unpoly_obj"},
	{melt_ice_away, NULL, "melt_ice_away"},
};

static const char *kind_name(short kind) {
	switch (kind) {
		case TIMER_LEVEL:
			return "level";
		case TIMER_GLOBAL:
			return "global";
		case TIMER_OBJECT:
			return "object";
		case TIMER_MONSTER:
			return "monster";
	}
	return "unknown";
}

static void print_queue(winid win, timer_element *base) {
	timer_element *curr;
	char buf[BUFSZ], arg_address[20];

	if (!base) {
		putstr(win, 0, "<empty>");
	} else {
		putstr(win, 0, "timeout  id   kind   call");
		for (curr = base; curr; curr = curr->next) {
			sprintf(buf, " %4ld   %4ld  %-6s %s(%s)",
				curr->timeout, curr->tid, kind_name(curr->kind),
				timeout_funcs[curr->func_index].name,
				fmt_ptr(curr->arg.a_void, arg_address));
			putstr(win, 0, buf);
		}
	}
}

int wiz_timeout_queue(void) {
	winid win;
	char buf[BUFSZ];

	win = create_nhwindow(NHW_MENU); /* corner text window */
	if (win == WIN_ERR) return 0;

	sprintf(buf, "Current time = %ld.", monstermoves);
	putstr(win, 0, buf);
	putstr(win, 0, "");
	putstr(win, 0, "Active timeout queue:");
	putstr(win, 0, "");
	print_queue(win, timer_base);

	display_nhwindow(win, false);
	destroy_nhwindow(win);

	return 0;
}

void timer_sanity_check(void) {
	timer_element *curr;
	char obj_address[20];

	/* this should be much more complete */
	for (curr = timer_base; curr; curr = curr->next)
		if (curr->kind == TIMER_OBJECT) {
			struct obj *obj = curr->arg.a_obj;
			if (obj->timed == 0) {
				pline("timer sanity: untimed obj %s, timer %ld",
				      fmt_ptr((void *)obj, obj_address), curr->tid);
			}
		}
}

/*
 * Pick off timeout elements from the global queue and call their functions.
 * Do this until their time is less than or equal to the move count.
 */
void run_timers(void) {
	timer_element *curr;

	/*
	 * Always use the first element.  Elements may be added or deleted at
	 * any time.  The list is ordered, we are done when the first element
	 * is in the future.
	 */
	while (timer_base && timer_base->timeout <= monstermoves) {
		curr = timer_base;
		timer_base = curr->next;

		if (curr->kind == TIMER_OBJECT) curr->arg.a_obj->timed--;
		(*timeout_funcs[curr->func_index].f)(curr->arg.a_void, curr->timeout);
		free(curr);
	}
}

/*
 * Start a timer.  Return true if successful.
 */
boolean start_timer(long when, short kind, short func_index, anything arg) {
	timer_element *gnu;

	if (func_index < 0 || func_index >= NUM_TIME_FUNCS)
		panic("start_timer");

	gnu = alloc(sizeof(timer_element));
	gnu->next = 0;
	gnu->tid = timer_id++;
	gnu->timeout = monstermoves + when;
	gnu->kind = kind;
	gnu->needs_fixup = 0;
	gnu->func_index = func_index;
	gnu->arg = arg;
	insert_timer(gnu);

	if (kind == TIMER_OBJECT) /* increment object's timed count */
		arg.a_obj->timed++;

	/* should check for duplicates and fail if any */
	return true;
}

/*
 * Remove the timer from the current list and free it up.  Return the time
 * it would have gone off, 0 if not found.
 */
long stop_timer(short func_index, anything arg) {
	timer_element *doomed;
	long timeout;

	doomed = remove_timer(&timer_base, func_index, arg);

	if (doomed) {
		timeout = doomed->timeout;
		if (doomed->kind == TIMER_OBJECT)
			arg.a_obj->timed--;
		if (timeout_funcs[doomed->func_index].cleanup)
			(*timeout_funcs[doomed->func_index].cleanup)(arg.a_void, timeout);
		free(doomed);
		return timeout;
	}
	return 0;
}

// Find the timeout of specified timer; return 0 if none.
long peek_timer(short type, anything arg) {
	timer_element *curr;

	for (curr = timer_base; curr; curr = curr->next) {
		if (curr->func_index == type && curr->arg.a_void == arg.a_void)
			return curr->timeout;
	}

	return 0;
}


// Move all object timers from src to dest, leaving src untimed.
void obj_move_timers(struct obj *src, struct obj *dest) {
	int count;
	timer_element *curr;

	for (count = 0, curr = timer_base; curr; curr = curr->next)
		if (curr->kind == TIMER_OBJECT && curr->arg.a_obj == src) {
			curr->arg.a_obj = dest;
			dest->timed++;
			count++;
		}
	if (count != src->timed)
		panic("obj_move_timers");
	src->timed = 0;
}

/*
 * Find all object timers and duplicate them for the new object "dest".
 */
void obj_split_timers(struct obj *src, struct obj *dest) {
	timer_element *curr, *next_timer = 0;

	for (curr = timer_base; curr; curr = next_timer) {
		next_timer = curr->next; /* things may be inserted */
		if (curr->kind == TIMER_OBJECT && curr->arg.a_obj == src) {
			start_timer(curr->timeout - monstermoves, TIMER_OBJECT, curr->func_index, obj_to_any(dest));
		}
	}
}

/*
 * Stop all timers attached to this object.  We can get away with this because
 * all object pointers are unique.
 */
void obj_stop_timers(struct obj *obj) {
	timer_element *curr, *prev, *next_timer = 0;

	for (prev = 0, curr = timer_base; curr; curr = next_timer) {
		next_timer = curr->next;
		if (curr->kind == TIMER_OBJECT && curr->arg.a_obj == obj) {
			if (prev)
				prev->next = curr->next;
			else
				timer_base = curr->next;
			if (timeout_funcs[curr->func_index].cleanup)
				(*timeout_funcs[curr->func_index].cleanup)(curr->arg.a_void,
									   curr->timeout);
			free(curr);
		} else {
			prev = curr;
		}
	}
	obj->timed = 0;
}

/*
 * Stop all timers attached to this monster.  We can get away with this because
 * all monster pointers are unique.
 */
void mon_stop_timers(struct monst *mon) {
	timer_element *curr, *prev, *next_timer = 0;

	for (prev = 0, curr = timer_base; curr; curr = next_timer) {
		next_timer = curr->next;
		if (curr->kind == TIMER_MONSTER && curr->arg.a_monst == mon) {
			if (prev)
				prev->next = curr->next;
			else
				timer_base = curr->next;
			if (timeout_funcs[curr->func_index].cleanup)
				(*timeout_funcs[curr->func_index].cleanup)(curr->arg.a_void,
									   curr->timeout);
			free(curr);
		} else {
			prev = curr;
		}
	}
}

// Check whether object has a timer of type timer_type.
bool obj_has_timer(struct obj *object, short timer_type) {
	long timeout = peek_timer(timer_type, obj_to_any(object));

	return timeout != 0;
}


/*
 * Stop all timers of index func_index at this spot.
 *
 */
void spot_stop_timers(xchar x, xchar y, short func_index) {
	timer_element *curr, *prev, *next_timer=0;
	long where = ((long)x << 16) | ((long)y);

	for (prev = 0, curr = timer_base; curr; curr = next_timer) {
		next_timer = curr->next;
		if (curr->kind == TIMER_LEVEL && curr->func_index == func_index && curr->arg.a_long == where) {
			if (prev) {
				prev->next = curr->next;
			} else {
				timer_base = curr->next;
			}

			if (timeout_funcs[curr->func_index].cleanup) {
				(*timeout_funcs[curr->func_index].cleanup)(curr->arg.a_void, curr->timeout);
			}

			free(curr);
		} else {
			prev = curr;
		}
	}
}


/*
 * When is the spot timer of type func_index going to expire?
 * Returns 0L if no such timer.
 */
long spot_time_expires(xchar x, xchar y, short func_index) {
	timer_element *curr;
	long where = (((long)x << 16) | ((long)y));

	for (curr = timer_base; curr; curr = curr->next) {
		if (curr->kind == TIMER_LEVEL &&
				curr->func_index == func_index && curr->arg.a_long == where)
			return curr->timeout;
	}
	return 0L;
}

long spot_time_left(xchar x, xchar y, short func_index) {
    long expires = spot_time_expires(x,y,func_index);
    return (expires > 0L) ? expires - monstermoves : 0L;
}

/* Insert timer into the global queue */
static void insert_timer(timer_element *gnu) {
	timer_element *curr, *prev;

	for (prev = 0, curr = timer_base; curr; prev = curr, curr = curr->next)
		if (curr->timeout >= gnu->timeout) break;

	gnu->next = curr;
	if (prev)
		prev->next = gnu;
	else
		timer_base = gnu;
}

static timer_element *remove_timer(timer_element **base, short func_index, anything arg) {
	timer_element *prev, *curr;

	for (prev = 0, curr = *base; curr; prev = curr, curr = curr->next)
		if (curr->func_index == func_index && curr->arg.a_void == arg.a_void) break;

	if (curr) {
		if (prev)
			prev->next = curr->next;
		else
			*base = curr->next;
	}

	return curr;
}

static void write_timer(int fd, timer_element *timer) {
	anything arg_save;

	switch (timer->kind) {
		case TIMER_GLOBAL:
		case TIMER_LEVEL:
			/* assume no pointers in arg */
			bwrite(fd, timer, sizeof(timer_element));
			break;

		case TIMER_OBJECT:
			if (timer->needs_fixup)
				bwrite(fd, timer, sizeof(timer_element));
			else {
				/* replace object pointer with id */
				arg_save = timer->arg;
				timer->arg = uint_to_any(timer->arg.a_obj->o_id);
				timer->needs_fixup = 1;
				bwrite(fd, timer, sizeof(timer_element));
				timer->arg = arg_save;
				timer->needs_fixup = 0;
			}
			break;

		case TIMER_MONSTER:
			if (timer->needs_fixup)
				bwrite(fd, timer, sizeof(timer_element));
			else {
				/* replace monster pointer with id */
				arg_save = timer->arg;
				timer->arg = uint_to_any(timer->arg.a_monst->m_id);
				timer->needs_fixup = 1;
				bwrite(fd, timer, sizeof(timer_element));
				timer->arg = arg_save;
				timer->needs_fixup = 0;
			}
			break;

		default:
			panic("write_timer");
			break;
	}
}

/*
 * MRKR: Run one particular timer faster for a number of steps
 *       Needed for burn_faster above.
 */

static void accelerate_timer(short func_index, anything arg, long adj) {
	timer_element *timer;

	/* This will effect the ordering, so we remove it from the list */
	/* and add it back in afterwards (if warranted) */

	timer = remove_timer(&timer_base, func_index, arg);

	for (; adj > 0; adj--) {
		timer->timeout--;

		if (timer->timeout <= monstermoves) {
			if (timer->kind == TIMER_OBJECT) (arg.a_obj)->timed--;
			(*timeout_funcs[func_index].f)(arg.a_void, timer->timeout);
			free(timer);
			break;
		}
	}

	if (adj == 0)
		insert_timer(timer);
}

/*
 * Return true if the object will stay on the level when the level is
 * saved.
 */
boolean obj_is_local(struct obj *obj) {
	switch (obj->where) {
		case OBJ_INVENT:
		case OBJ_MIGRATING:
			return false;
		case OBJ_FLOOR:
		case OBJ_BURIED:
			return true;
		case OBJ_CONTAINED:
			return obj_is_local(obj->ocontainer);
		case OBJ_MINVENT:
			return mon_is_local(obj->ocarry);
	}
	panic("obj_is_local");
	return false;
}

/*
 * Return true if the given monster will stay on the level when the
 * level is saved.
 */
static boolean mon_is_local(struct monst *mon) {
	struct monst *curr;

	for (curr = migrating_mons; curr; curr = curr->nmon)
		if (curr == mon) return false;
	/* `mydogs' is used during level changes, never saved and restored */
	for (curr = mydogs; curr; curr = curr->nmon)
		if (curr == mon) return false;
	return true;
}

/*
 * Return true if the timer is attached to something that will stay on the
 * level when the level is saved.
 */
static boolean timer_is_local(timer_element *timer) {
	switch (timer->kind) {
		case TIMER_LEVEL:
			return true;
		case TIMER_GLOBAL:
			return false;
		case TIMER_OBJECT:
			return obj_is_local(timer->arg.a_obj);
		case TIMER_MONSTER:
			return mon_is_local(timer->arg.a_monst);
	}
	panic("timer_is_local");
	return false;
}

/*
 * Part of the save routine.  Count up the number of timers that would
 * be written.  If write_it is true, actually write the timer.
 */
static int maybe_write_timer(int fd, int range, boolean write_it) {
	int count = 0;
	timer_element *curr;

	for (curr = timer_base; curr; curr = curr->next) {
		if (range == RANGE_GLOBAL) {
			/* global timers */

			if (!timer_is_local(curr)) {
				count++;
				if (write_it) write_timer(fd, curr);
			}

		} else {
			/* local timers */

			if (timer_is_local(curr)) {
				count++;
				if (write_it) write_timer(fd, curr);
			}
		}
	}

	return count;
}

/*
 * Save part of the timer list.  The parameter 'range' specifies either
 * global or level timers to save.  The timer ID is saved with the global
 * timers.
 *
 * Global range:
 *		+ timeouts that follow the hero (global)
 *		+ timeouts that follow obj & monst that are migrating
 *
 * Level range:
 *		+ timeouts that are level specific (e.g. storms)
 *		+ timeouts that stay with the level (obj & monst)
 */
void save_timers(int fd, int mode, int range) {
	timer_element *curr, *prev, *next_timer = 0;
	int count;

	if (perform_bwrite(mode)) {
		if (range == RANGE_GLOBAL)
			bwrite(fd, (void *)&timer_id, sizeof(timer_id));

		count = maybe_write_timer(fd, range, false);
		bwrite(fd, (void *)&count, sizeof count);
		maybe_write_timer(fd, range, true);
	}

	if (release_data(mode)) {
		for (prev = 0, curr = timer_base; curr; curr = next_timer) {
			next_timer = curr->next; /* in case curr is removed */

			if (!(!!(range == RANGE_LEVEL) ^ !!timer_is_local(curr))) {
				if (prev)
					prev->next = curr->next;
				else
					timer_base = curr->next;
				free(curr);
				/* prev stays the same */
			} else {
				prev = curr;
			}
		}
	}
}

/*
 * Pull in the structures from disk, but don't recalculate the object and
 * monster pointers.
 */
void restore_timers(int fd, int range, boolean from_ghost_level, long timeout_adjustment) {
	int count;
	timer_element *curr;

	if (range == RANGE_GLOBAL)
		mread(fd, (void *)&timer_id, sizeof timer_id);

	/* restore elements */
	mread(fd, (void *)&count, sizeof count);
	while (count-- > 0) {
		curr = alloc(sizeof(timer_element));
		mread(fd, (void *)curr, sizeof(timer_element));
		if (from_ghost_level)
			curr->timeout += timeout_adjustment;
		insert_timer(curr);
	}
}

/* reset all timers that are marked for reseting */
void relink_timers(boolean ghostly) {
	timer_element *curr;
	unsigned nid;

	for (curr = timer_base; curr; curr = curr->next) {
		if (curr->needs_fixup) {
			if (curr->kind == TIMER_OBJECT) {
				if (ghostly) {
					if (!lookup_id_mapping(curr->arg.a_uint, &nid))
						panic("relink_timers 1");
				} else
					nid = curr->arg.a_uint;

				curr->arg.a_obj = find_oid(nid);
				if (!curr->arg.a_obj) panic("cant find o_id %d", nid);
				curr->needs_fixup = 0;
			} else if (curr->kind == TIMER_MONSTER) {
				/*                panic("relink_timers: no monster timer implemented");*/
				/* WAC attempt to relink monster timers based on above
				 * and light source code
				 */
				if (ghostly) {
					if (!lookup_id_mapping(curr->arg.a_uint, &nid))
						panic("relink_timers 1b");
				} else
					nid = curr->arg.a_uint;
				curr->arg.a_monst = find_mid(nid, FM_EVERYWHERE);
				if (!curr->arg.a_monst) panic("cant find m_id %d", nid);
				curr->needs_fixup = 0;
			} else
				panic("relink_timers 2");
		}
	}
}
/*timeout.c*/
