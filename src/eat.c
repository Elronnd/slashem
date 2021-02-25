/*	SCCS Id: @(#)eat.c	3.4	2003/02/13	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "mextra.h"
/* #define DEBUG */ /* uncomment to enable new eat code debugging */

#ifdef DEBUG
#define debugpline \
	if (wizard) pline
#endif

static int eatmdone(void);
static int eatfood(void);
static void costly_tin(const char *verb);
static int opentin(void);
static int unfaint(void);

static const char *food_xname(struct obj *food, bool the_pfx);
static const char *Food_xname(struct obj *food, bool the_pfx);
static void choke(struct obj *food);
static void recalc_wt(void);
static struct obj *touchfood(struct obj *otmp);
static void do_reset_eat(void);
static void done_eating(bool message);
static void cprefx(int pm);
static int intrinsic_possible(int type, struct permonst *ptr);
static void givit(int type, struct permonst *ptr);
static void cpostfx(int pm);
static void start_tin(struct obj *otmp);
static int eatcorpse(struct obj *otmp);
static void start_eating(struct obj *otmp);
static void fprefx(struct obj *otmp);
static void fpostfx(struct obj *otmp);
static int bite(void);
static int edibility_prompts(struct obj *otmp);
static int rottenfood(struct obj *obj);
static void eatspecial(void);
static int bounded_increase(int old, int inc, int typ);
static void accessory_has_effect(struct obj *otmp);
static void eataccessory(struct obj *otmp);
static const char *foodword(struct obj *otmp);
static struct obj *floorfood(const char *verb);
static int tin_variety(struct obj *obj);

char msgbuf[BUFSZ];

/* hunger texts used on bottom line (each 8 chars long) */
#define SATIATED   0
#define NOT_HUNGRY 1
#define HUNGRY	   2
#define WEAK	   3
#define FAINTING   4
#define FAINTED	   5
#define STARVED	   6

/* also used to see if you're allowed to eat cats and dogs */
#define CANNIBAL_ALLOWED() (Role_if(PM_CAVEMAN) || Race_if(PM_ORC) || \
			    Race_if(PM_HUMAN_WEREWOLF) || Race_if(PM_VAMPIRE))

/* Gold must come first for getobj(). */
static const char allobj[] = {COIN_CLASS, ALLOW_FLOOROBJ,
			      WEAPON_CLASS, ARMOR_CLASS, POTION_CLASS, SCROLL_CLASS,
			      WAND_CLASS, RING_CLASS, AMULET_CLASS, FOOD_CLASS, TOOL_CLASS,
			      GEM_CLASS, ROCK_CLASS, BALL_CLASS, CHAIN_CLASS, SPBOOK_CLASS, 0};

static boolean force_save_hs = false;

/*
 * Decide whether a particular object can be eaten by the possibly
 * polymorphed character.  Not used for monster checks.
 */
bool is_edible(struct obj *obj) {
	/* protect invocation tools but not Rider corpses (handled elsewhere)*/
	/* if (obj->oclass != FOOD_CLASS && obj_resists(obj, 0, 0)) */
	if (evades_destruction(obj))
		return false;
	if (objects[obj->otyp].oc_unique)
		return false;
	/* above also prevents the Amulet from being eaten, so we must never
	   allow fake amulets to be eaten either [which is already the case] */

	if (metallivorous(youmonst.data) && is_metallic(obj) &&
	    (youmonst.data != &mons[PM_RUST_MONSTER] || is_rustprone(obj)))
		return true;
	/* KMH -- Taz likes organics, too! */
	if ((u.umonnum == PM_GELATINOUS_CUBE ||
	     u.umonnum == PM_TASMANIAN_DEVIL) &&
	    is_organic(obj) &&
	    /* [g.cubes can eat containers and retain all contents
	                    as engulfed items, but poly'd player can't do that] */
	    !Has_contents(obj))
		return true;

	/* Koalas only eat Eucalyptus leaves */
	if (u.umonnum == PM_KOALA)
		return obj->otyp == EUCALYPTUS_LEAF;

	/* Ghouls, ghasts only eat corpses */
	if (u.umonnum == PM_GHOUL || u.umonnum == PM_GHAST)
		return obj->otyp == CORPSE;
	/* Vampires drink the blood of meaty corpses */
	/* [ALI] (fully) drained food is not presented as an option,
	 * but partly eaten food is (even though you can't drain it).
	 */
	if (is_vampire(youmonst.data))
		return obj->otyp == CORPSE &&
		       has_blood(&mons[obj->corpsenm]) && (!obj->odrained || obj->oeaten > drainlevel(obj));

	/* return index(comestibles, obj->oclass); */
	return obj->oclass == FOOD_CLASS;
}

void init_uhunger(void) {
	u.uhunger = 900;
	u.uhs = NOT_HUNGRY;
}

static const struct {
	const char *txt;
	int nut;
} tintxts[] = {
	{"deep fried", 60},
	{"pickled", 40},
	{"soup made from", 20},
	{"pureed", 500},
	{"rotten", -50}, // rotten = 4
	{"homemade", 50}, // homemade = 5
	{"stir fried", 80},
	{"candied", 100},
	{"boiled", 50},
	{"dried", 55},
	{"szechuan", 70},
	{"french fried", 40}, // french-fried = 11
	{"sauteed", 95},
	{"broiled", 80},
	{"smoked", 50},
	/* [Tom] added a few new styles */
	{"stir fried", 80},
	{"candied", 100},
	{"boiled", 50},
	{"dried", 55},
	{"szechuan", 70},
	{"french fried", 40},
	{"sauteed", 95},
	{"broiled", 80},
	{"smoked", 50},
	{"", 0}};
#define TTSZ SIZE(tintxts)

static char *eatmbuf = 0; /* set by cpostfx() */

// called after mimicing is over
static int eatmdone(void) {
	/* release `eatmbuf' */
	if (eatmbuf) {
		if (nomovemsg == eatmbuf) nomovemsg = 0;
		free(eatmbuf), eatmbuf = 0;
	}
	/* update display */
	if (youmonst.m_ap_type) {
		youmonst.m_ap_type = M_AP_NOTHING;
		newsym(u.ux, u.uy);
	}
	return 0;
}

/* ``[the(] singular(food, xname) [)]'' with awareness of unique monsters */
static const char *food_xname(struct obj *food, bool the_pfx) {
	const char *result;
	int mnum = food->corpsenm;

	if (food->otyp == CORPSE && (mons[mnum].geno & G_UNIQ) && !Hallucination) {
		/* grab xname()'s modifiable return buffer for our own use */
		char *bufp = xname(food);

		sprintf(bufp, "%s%s corpse",
			(the_pfx && !type_is_pname(&mons[mnum])) ? "the " : "",
			s_suffix(mons[mnum].mname));
		result = bufp;
	} else {
		/* the ordinary case */
		result = singular(food, xname);
		if (the_pfx) result = the(result);
	}
	return result;
}

static const char *Food_xname(struct obj *food, bool the_pfx) {
	/* food_xname() uses a modifiable buffer, so we can use it too */
	char *buf = (char *)food_xname(food, the_pfx);

	*buf = highc(*buf);
	return buf;
}

/* Created by GAN 01/28/87
 * Amended by AKP 09/22/87: if not hard, don't choke, just vomit.
 * Amended by 3.  06/12/89: if not hard, sometimes choke anyway, to keep risk.
 *		  11/10/89: if hard, rarely vomit anyway, for slim chance.
 */
// To a full belly all food is bad. (It.)
static void choke(struct obj *food) {
	/* only happens if you were satiated */
	if (u.uhs != SATIATED) {
		if (!food || food->otyp != AMULET_OF_STRANGULATION)
			return;
	} else if (Role_if(PM_KNIGHT) && u.ualign.type == A_LAWFUL) {
		adjalign(-1); /* gluttony is unchivalrous */
		pline("You feel like a glutton!");
	}

	exercise(A_CON, false);

	if (Breathless || (!Strangled && !rn2(20))) {
		/* choking by eating AoS doesn't involve stuffing yourself */
		/* ALI - nor does other non-food nutrition (eg., life-blood) */
		if (!food || food->otyp == AMULET_OF_STRANGULATION) {
			nomovemsg = "You recover your composure.";
			pline("You choke over it.");
			nomul(-2);
			return;
		}
		pline("You stuff yourself and then vomit voluminously.");
		morehungry(1000); /* you just got *very* sick! */
		nomovemsg = 0;
		vomit();
	} else {
		killer.format = KILLED_BY_AN;
		/*
		 * Note all "killer"s below read "Choked on %s" on the
		 * high score list & tombstone.  So plan accordingly.
		 */
		if (food) {
			pline("You choke over your %s.", foodword(food));
			if (food->oclass == COIN_CLASS) {
				killer.name = nhsdupz("very rich meal");
			} else {
				killer.name = nhsdupz(food_xname(food, false));
				if (food->otyp == CORPSE && (mons[food->corpsenm].geno & G_UNIQ)) {
					if (!type_is_pname(&mons[food->corpsenm]))
						killer.name = nhsdupz(the(nhs2cstr(killer.name)));
					killer.format = KILLED_BY;
				} else if (obj_is_pname(food)) {
					killer.format = KILLED_BY;
					if (food->oartifact >= ART_ORB_OF_DETECTION)
						killer.name = nhsdupz(the(nhs2cstr(killer.name)));
				}
			}
		} else {
			pline("You choke over it.");
			killer.name = nhsdupz("quick snack");
		}
		pline("You die...");
		done(CHOKING);
	}
}

// modify object wt. depending on time spent consuming it
static void recalc_wt(void) {
	struct obj *piece = context.victual.piece;

#ifdef DEBUG
	debugpline("Old weight = %d", piece->owt);
	debugpline("Used time = %d, Req'd time = %d",
		   context.victual.usedtime, context.victual.reqtime);
#endif
	piece->owt = weight(piece);
#ifdef DEBUG
	debugpline("New weight = %d", piece->owt);
#endif
}

// called when eating interrupted by an event
void reset_eat(void) {
	/* we only set a flag here - the actual reset process is done after
	 * the round is spent eating.
	 */
	if (context.victual.eating && !context.victual.doreset) {
#ifdef DEBUG
		debugpline("reset_eat...");
#endif
		context.victual.doreset = true;
	}
	return;
}

static struct obj *touchfood(struct obj *otmp) {
	if (otmp->quan > 1L) {
		if (!carried(otmp))
			splitobj(otmp, otmp->quan - 1L);
		else
			otmp = splitobj(otmp, 1L);

#ifdef DEBUG
		debugpline("split object,");
#endif
	}

	if (!otmp->oeaten) {
		if (((!carried(otmp) && costly_spot(otmp->ox, otmp->oy) &&
		      !otmp->no_charge) ||
		     otmp->unpaid)) {
			/* create a dummy duplicate to put on bill */
			verbalize("You bit it, you bought it!");
			bill_dummy_object(otmp);
		}
		otmp->oeaten = (otmp->otyp == CORPSE ?
					mons[otmp->corpsenm].cnutrit :
					objects[otmp->otyp].oc_nutrition);
	}

	if (carried(otmp)) {
		freeinv(otmp);
		if (inv_cnt() >= 52) {
			sellobj_state(SELL_DONTSELL);
			dropy(otmp);
			sellobj_state(SELL_NORMAL);
		} else {
			otmp->oxlth++; /* hack to prevent merge */
			otmp = addinv(otmp);
			otmp->oxlth--;
		}
	}
	return otmp;
}

/* When food decays, in the middle of your meal, we don't want to dereference
 * any dangling pointers, so set it to null (which should still trigger
 * do_reset_eat() at the beginning of eatfood()) and check for null pointers
 * in do_reset_eat().
 */
void food_disappears(struct obj *obj) {
	if (obj == context.victual.piece) {
		context.victual.piece = NULL;
		context.victual.o_id = 0;
	}
	if (obj->timed) obj_stop_timers(obj);
}

/* renaming an object usually results in it having a different address;
   so the sequence start eating/opening, get interrupted, name the food,
   resume eating/opening would restart from scratch */
void food_substitution(struct obj *old_obj, struct obj *new_obj) {
	if (old_obj == context.victual.piece) {
		context.victual.piece = new_obj;
		context.victual.o_id = new_obj->o_id;
	}
	if (old_obj == context.tin.tin) {
		context.tin.tin = new_obj;
		context.tin.o_id = new_obj->o_id;
	}
}

static void do_reset_eat(void) {
#ifdef DEBUG
	debugpline("do_reset_eat...");
#endif
	if (context.victual.piece) {
		context.victual.o_id = 0;
		context.victual.piece = touchfood(context.victual.piece);
		if (context.victual.piece)
			context.victual.o_id = context.victual.piece->o_id;
		recalc_wt();
	}
	context.victual.fullwarn = context.victual.eating = context.victual.doreset = false;
	/* Do not set canchoke to false; if we continue eating the same object
	 * we need to know if canchoke was set when they started eating it the
	 * previous time.  And if we don't continue eating the same object
	 * canchoke always gets recalculated anyway.
	 */
	stop_occupation();
	newuhs(false);
}

// called each move during eating process
static int eatfood(void) {
	if (!context.victual.piece ||
	    (!carried(context.victual.piece) && !obj_here(context.victual.piece, u.ux, u.uy))) {
		/* maybe it was stolen? */
		do_reset_eat();
		return 0;
	}
	if (is_vampire(youmonst.data) != context.victual.piece->odrained) {
		/* Polymorphed while eating/draining */
		do_reset_eat();
		return 0;
	}
	if (!context.victual.eating) return 0;

	if (++context.victual.usedtime <= context.victual.reqtime) {
		if (bite()) return 0;
		return 1;			    /* still busy */
	} else {				    /* done */
		int crumbs = context.victual.piece->oeaten; /* The last crumbs */
		if (context.victual.piece->odrained) crumbs -= drainlevel(context.victual.piece);
		if (crumbs > 0) {
			lesshungry(crumbs);
			context.victual.piece->oeaten -= crumbs;
		}
		done_eating(true);
		return 0;
	}
}

static void done_eating(bool message) {
	context.victual.piece->in_use = true;
	occupation = 0; /* do this early, so newuhs() knows we're done */
	newuhs(false);
	if (nomovemsg) {
		if (message) pline("%s", nomovemsg);
		nomovemsg = 0;
	} else if (message)
		pline("You finish %s %s.", context.victual.piece->odrained ? "draining" : "eating", food_xname(context.victual.piece, true));

	if (context.victual.piece->otyp == CORPSE) {
		if (!context.victual.piece->odrained || (Race_if(PM_VAMPIRE) && !rn2(5))) {
			cpostfx(context.victual.piece->corpsenm);
		}
	} else {
		fpostfx(context.victual.piece);
	}

	if (context.victual.piece->odrained)
		context.victual.piece->in_use = false;
	else if (carried(context.victual.piece))
		useup(context.victual.piece);
	else
		useupf(context.victual.piece, 1L);
	context.victual.piece = NULL;
	context.victual.fullwarn = context.victual.eating = context.victual.doreset = false;
}

// handle side-effects of mind flayer's tentacle attack
// dmg_p: for dishing out extra damage in lieu of Int loss
int eat_brains(struct monst *magr, struct monst *mdef, bool visflag, int *dmg_p) {
	struct permonst *pd = mdef->data;
	bool give_nutrit = false;
	int result = MM_HIT, xtra_dmg = rnd(10);

	if (magr == &youmonst) {
		pline("You eat %s brain!", s_suffix(mon_nam(mdef)));
	} else if (mdef == &youmonst) {
		pline("Your brain is eaten!");
	} else {           /* monster against monster */
		if (visflag) pline("%s brain is eaten!", s_suffix(Monnam(mdef)));
	}

	if (flesh_petrifies(pd)) {
		/* mind flayer has attempted to eat the brains of a petrification
		   inducing critter (most likely Medusa; attacking a cockatrice via
		   tentacle-touch should have been caught before reaching this far) */
		if (magr == &youmonst) {
			if (!Stone_resistance && !Stoned) make_stoned(5, NULL, KILLED_BY_AN, nhsdupz(pd->mname));
		} else {
			/* no need to check for poly_when_stoned or Stone_resistance;
			   mind flayers don't have those capabilities */
			if (visflag) pline("%s turns to stone!", Monnam(magr));
			monstone(magr);
			if (magr->mhp > 0) {
				/* life-saved; don't continue eating the brains */
				return MM_MISS;
			} else {
				if (magr->mtame && !visflag)
					/* parallels mhitm.c's brief_feeling */
					pline("You have a sad thought for a moment, then is passes.");
				return MM_AGR_DIED;
			}
		}
	}

	if (magr == &youmonst) {
		 // player mind flayer is eating something's brain
		u.uconduct.food++;
		if (!vegan(pd))
			u.uconduct.unvegan++;
		if (!vegetarian(pd))
			violated_vegetarian();
		if (mindless(pd)) {             /* (cannibalism not possible here) */
			pline("%s doesn't notice.", Monnam(mdef));
			/* all done; no extra harm inflicted upon target */
			return MM_MISS;
		} else if (is_rider(pd)) {
			pline("Ingesting that is fatal.");
			killer.name = nhsfmt("unwisely ate the brain of %S", pd->mname);
			killer.format = NO_KILLER_PREFIX;
			done(DIED);
			/* life-saving needed to reach here */
			exercise(A_WIS, false);
			*dmg_p += xtra_dmg;         /* Rider takes extra damage */
		} else {
			morehungry(-rnd(30));       /* cannot choke */
			if (ABASE(A_INT) < AMAX(A_INT)) {
				/* recover lost Int; won't increase current max */
				ABASE(A_INT) += rnd(4);
				if (ABASE(A_INT) > AMAX(A_INT)) ABASE(A_INT) = AMAX(A_INT);
				context.botl = 1;
			}
			exercise(A_WIS, true);
			*dmg_p += xtra_dmg;
		}
		/* targetting another mind flayer or your own underlying species
		   is cannibalism */
		maybe_cannibal(monsndx(pd), true);
	} else if (mdef == &youmonst) {
		/*
		 * monster mind flayer is eating hero's brain
		 */
		/* no such thing as mindless players */
		if (ABASE(A_INT) <= ATTRMIN(A_INT)) {
			if (Lifesaved) {
				killer.name = nhsdupz("brainlessness");
				killer.format = KILLED_BY;
				done(DIED);
				/* amulet of life saving has now been used up */
				pline("Unfortunately your brain is still gone.");
			} else {
				pline("Your last thought fades away.");
			}
			killer.name = nhsdupz("brainlessness");
			killer.format = KILLED_BY;
			done(DIED);
			/* can only get here when in wizard or explore mode and user has
			   explicitly chosen not to die; arbitrarily boost intelligence */
			ABASE(A_INT) = ATTRMIN(A_INT) + 2;
			pline("You feel like a scarecrow.");
		}
		give_nutrit = true;     /* in case a conflicted pet is doing this */
		exercise(A_WIS, false);
		/* caller handles Int and memory loss */

	} else {           /* mhitm */
		/*
		 * monster mind flayer is eating another monster's brain
		 */
		if (mindless(pd)) {
			if (visflag) pline("%s doesn't notice.", Monnam(mdef));
			return MM_MISS;
		} else if (is_rider(pd)) {
			mondied(magr);
			if (magr->mhp <= 0) result = MM_AGR_DIED;
			/* Rider takes extra damage regardless of whether attacker dies */
			*dmg_p += xtra_dmg;
		} else {
			*dmg_p += xtra_dmg;
			give_nutrit = true;
			if (*dmg_p >= mdef->mhp && visflag)
				pline("%s last thought fades away...", s_suffix(Monnam(mdef)));
		}
	}

	if (give_nutrit && magr->mtame && !magr->isminion) {
		EDOG(magr)->hungrytime += rnd(60);
		magr->mconf = 0;
	}

	return result;
}


// eating a corpse or egg of one's own species is usually naughty
bool maybe_cannibal(int pm, bool allowmsg) {
	static long ate_brains = 0;
	struct permonst *fptr = &mons[pm];

       /* when poly'd into a mind flayer, multiple tentacle hits in one
          turn cause multiple digestion checks to occur; avoid giving
          multiple luck penalties for the same attack */
       if (moves == ate_brains) return false;
       ate_brains = moves;     // ate_anything, not just brains...

	if (your_race(fptr)
		/* non-cannibalistic heroes shouldn't eat own species ever
		   and also shouldn't eat current species when polymorphed
		   (even if having the form of something which doesn't care
		   about cannibalism--hero's innate traits aren't altered) */
	    || (Upolyd && same_race(youmonst.data, fptr))) {

		if (!CANNIBAL_ALLOWED()) {
			if (allowmsg) {
				if (Upolyd)
					pline("You have a bad feeling deep inside.");
				pline("You cannibal!  You will regret this!");
			}
			HAggravate_monster |= FROMOUTSIDE;
			change_luck(-rn1(4, 2)); /* -5..-2 */
		} else if (Role_if(PM_CAVEMAN)) {
			adjalign(sgn(u.ualign.type));
			pline("You honour the dead.");
		} else {
			adjalign(-sgn(u.ualign.type));
			pline("You feel evil and fiendish!");
		}
		return true;
	}
	return false;
}

static void cprefx(int pm) {
	maybe_cannibal(pm, true);
	if (flesh_petrifies(&mons[pm])) {
		if (!Stone_resistance &&
		    !(poly_when_stoned(youmonst.data) && polymon(PM_STONE_GOLEM))) {
			killer.format = KILLED_BY;
			killer.name = nhsfmt("tasting %S meat", mons[pm].mname);
			pline("You turn to stone.");
			done(STONING);
			if (context.victual.piece)
				context.victual.eating = false;
			return; /* lifesaved */
		}
	}

	switch (pm) {
		case PM_LITTLE_DOG:
		case PM_DOG:
		case PM_LARGE_DOG:
		case PM_KITTEN:
		case PM_HOUSECAT:
		case PM_LARGE_CAT:
			if (!CANNIBAL_ALLOWED()) {
				pline("You feel that %s the %s%s was a bad idea.",
				      context.victual.eating ? "eating" : "biting",
				      occupation == opentin ? "tinned " : "", mons[pm].mname);
				HAggravate_monster |= FROMOUTSIDE;
			}
			break;
		case PM_LIZARD:
			if (Stoned) fix_petrification();
			break;
		case PM_DEATH:
		case PM_PESTILENCE:
		case PM_FAMINE: {
			pline("Eating that is instantly fatal.");
			killer.format = NO_KILLER_PREFIX;
			killer.name = nhsfmt("unwisely ate the body of %S", mons[pm].mname);
			done(DIED);
			/* It so happens that since we know these monsters */
			/* cannot appear in tins, context.victual.piece will always */
			/* be what we want, which is not generally true. */
			if (revive_corpse(context.victual.piece, false)) {
				context.victual.piece = NULL;
				context.victual.o_id = 0;
			}
			return;
		}
		case PM_GREEN_SLIME:
			if (!Slimed && !Unchanging && !flaming(youmonst.data) &&
			    youmonst.data != &mons[PM_GREEN_SLIME]) {
				pline("You don't feel very well.");
				make_slimed(10, NULL);
				delayed_killer(SLIMED, KILLED_BY_AN, new_nhs());
			}
		fallthru;
		default:
			if (acidic(&mons[pm]) && Stoned)
				fix_petrification();
			break;
	}
}

/*
 * Called when a vampire bites a monster.
 * Returns true if hero died and was lifesaved.
 */

boolean bite_monster(struct monst *mon) {
	switch (monsndx(mon->data)) {
		case PM_LIZARD:
			if (Stoned) fix_petrification();
			break;
		case PM_DEATH:
		case PM_PESTILENCE:
		case PM_FAMINE:
			pline("Unfortunately, eating any of it is fatal.");
			done_in_by(mon);
			return true; /* lifesaved */

		case PM_GREEN_SLIME:
			if (!Unchanging && youmonst.data != &mons[PM_FIRE_VORTEX] &&
			    youmonst.data != &mons[PM_FIRE_ELEMENTAL] &&
			    youmonst.data != &mons[PM_GREEN_SLIME]) {
				pline("You don't feel very well.");
				Slimed = 10L;
			}
		fallthru;
		default:
			if (acidic(mon->data) && Stoned)
				fix_petrification();
			break;
	}
	return false;
}

void fix_petrification(void) {
	char buf[BUFSZ];

	if (Hallucination)
		sprintf(buf, "What a pity - you just ruined a future piece of %sart!", ACURR(A_CHA) > 15 ? "fine " : "");
	else
		strcpy(buf, "You feel limber!");

	make_stoned(0, buf, 0, new_nhs());
}

/*
 * If you add an intrinsic that can be gotten by eating a monster, add it
 * to intrinsic_possible() and givit().  (It must already be in prop.h to
 * be an intrinsic property.)
 * It would be very easy to make the intrinsics not try to give you one
 * that you already had by checking to see if you have it in
 * intrinsic_possible() instead of givit().
 */

/* intrinsic_possible() returns true if a monster can give an intrinsic. */
static int intrinsic_possible(int type, struct permonst *ptr) {
	switch (type) {
		case FIRE_RES:
#ifdef DEBUG
			if (ptr->mconveys & MR_FIRE) {
				debugpline("can get fire resistance");
				return true;
			} else
				return false;
#else
			return ptr->mconveys & MR_FIRE;
#endif
		case SLEEP_RES:
#ifdef DEBUG
			if (ptr->mconveys & MR_SLEEP) {
				debugpline("can get sleep resistance");
				return true;
			} else
				return false;
#else
			return ptr->mconveys & MR_SLEEP;
#endif
		case COLD_RES:
#ifdef DEBUG
			if (ptr->mconveys & MR_COLD) {
				debugpline("can get cold resistance");
				return true;
			} else
				return false;
#else
			return ptr->mconveys & MR_COLD;
#endif
		case DISINT_RES:
#ifdef DEBUG
			if (ptr->mconveys & MR_DISINT) {
				debugpline("can get disintegration resistance");
				return true;
			} else
				return false;
#else
			return ptr->mconveys & MR_DISINT;
#endif
		case SHOCK_RES: /* shock (electricity) resistance */
#ifdef DEBUG
			if (ptr->mconveys & MR_ELEC) {
				debugpline("can get shock resistance");
				return true;
			} else
				return false;
#else
			return ptr->mconveys & MR_ELEC;
#endif
		case POISON_RES:
#ifdef DEBUG
			if (ptr->mconveys & MR_POISON) {
				debugpline("can get poison resistance");
				return true;
			} else
				return false;
#else
			return ptr->mconveys & MR_POISON;
#endif
		case TELEPORT:
#ifdef DEBUG
			if (can_teleport(ptr)) {
				debugpline("can get teleport");
				return true;
			} else
				return false;
#else
			return can_teleport(ptr);
#endif
		case TELEPORT_CONTROL:
#ifdef DEBUG
			if (control_teleport(ptr)) {
				debugpline("can get teleport control");
				return true;
			} else
				return false;
#else
			return control_teleport(ptr);
#endif
		case TELEPAT:
#ifdef DEBUG
			if (telepathic(ptr)) {
				debugpline("can get telepathy");
				return true;
			} else
				return false;
#else
			return telepathic(ptr);
#endif
		default:
			return false;
	}
	/*NOTREACHED*/
}

/* givit() tries to give you an intrinsic based on the monster's level
 * and what type of intrinsic it is trying to give you.
 */
/* KMH, balance patch -- eliminated temporary intrinsics from
 * corpses, and restored probabilities to NetHack levels.
 *
 * There were several ways to deal with this issue:
 * 1.  Let corpses convey permanent intrisics (as implemented in
 *     vanilla NetHack).  This is the easiest method for players
 *     to understand and has the least player frustration.
 * 2.  Provide a temporary intrinsic if you don't already have it,
 *     a give the permanent intrinsic if you do have it (Slash's
 *     method).  This is probably the most realistic solution,
 *     but players were extremely annoyed by it.
 * 3.  Let certain intrinsics be conveyed one way and the rest
 *     conveyed the other.  However, there would certainly be
 *     arguments about which should be which, and it would
 *     certainly become yet another FAQ.
 * 4.  Increase the timeouts.  This is limited by the number of
 *     bits reserved for the timeout.
 * 5.  Convey a permanent intrinsic if you have _ever_ been
 *     given the temporary intrinsic.  This is a nice solution,
 *     but it would use another bit, and probably isn't worth
 *     the effort.
 * 6.  Give the player better notice when the timeout expires,
 *     and/or some method to check on intrinsics that is not as
 *     revealing as enlightenment.
 * 7.  Some combination of the above.
 *
 * In the end, I decided that the simplest solution would be the
 * best solution.
 */
static void givit(int type, struct permonst *ptr) {
	int chance;

#ifdef DEBUG
	debugpline("Attempting to give intrinsic %d", type);
#endif
	/* some intrinsics are easier to get than others */
	switch (type) {
		case POISON_RES:
			if ((ptr == &mons[PM_KILLER_BEE] ||
			     ptr == &mons[PM_SCORPION]) &&
			    !rn2(4))
				chance = 1;
			else
				chance = 15;
			break;
		case TELEPORT:
			chance = 10;
			break;
		case TELEPORT_CONTROL:
			chance = 12;
			break;
		case TELEPAT:
			chance = 1;
			break;
		default:
			chance = 15;
			break;
	}

	if (ptr->mlevel <= rn2(chance))
		return; /* failed die roll */

	switch (type) {
		case FIRE_RES:
#ifdef DEBUG
			debugpline("Trying to give fire resistance");
#endif
			if (!(HFire_resistance & FROMOUTSIDE)) {
				pline(Hallucination ? "You be chillin'." :
						      "You feel a momentary chill.");
				HFire_resistance |= FROMOUTSIDE;
			}
			break;
		case SLEEP_RES:
#ifdef DEBUG
			debugpline("Trying to give sleep resistance");
#endif
			if (!(HSleep_resistance & FROMOUTSIDE)) {
				pline("You feel wide awake.");
				HSleep_resistance |= FROMOUTSIDE;
			}
			break;
		case COLD_RES:
#ifdef DEBUG
			debugpline("Trying to give cold resistance");
#endif
			if (!(HCold_resistance & FROMOUTSIDE)) {
				pline("You feel full of hot air.");
				HCold_resistance |= FROMOUTSIDE;
			}
			break;
		case DISINT_RES:
#ifdef DEBUG
			debugpline("Trying to give disintegration resistance");
#endif
			if (!(HDisint_resistance & FROMOUTSIDE)) {
				pline(Hallucination ?
					      "You feel totally together, man." :
					      "You feel very firm.");
				HDisint_resistance |= FROMOUTSIDE;
			}
			break;
		case SHOCK_RES: /* shock (electricity) resistance */
#ifdef DEBUG
			debugpline("Trying to give shock resistance");
#endif
			if (!(HShock_resistance & FROMOUTSIDE)) {
				if (Hallucination)
					pline("You feel grounded in reality.");
				else
					pline("Your health currently feels amplified!");
				HShock_resistance |= FROMOUTSIDE;
			}
			break;
		case POISON_RES:
#ifdef DEBUG
			debugpline("Trying to give poison resistance");
#endif
			if (!(HPoison_resistance & FROMOUTSIDE)) {
				pline(Poison_resistance ? "You feel especially healthy." : "You feel healthy.");
				HPoison_resistance |= FROMOUTSIDE;
			}
			break;
		case TELEPORT:
#ifdef DEBUG
			debugpline("Trying to give teleport");
#endif
			if (!(HTeleportation & FROMOUTSIDE)) {
				pline(Hallucination ? "You feel diffuse." : "You feel very jumpy.");
				HTeleportation |= FROMOUTSIDE;
			}
			break;
		case TELEPORT_CONTROL:
#ifdef DEBUG
			debugpline("Trying to give teleport control");
#endif
			if (!(HTeleport_control & FROMOUTSIDE)) {
				pline(Hallucination ? "You feel centered in your personal space." : "You feel in control of yourself.");
				HTeleport_control |= FROMOUTSIDE;
			}
			break;
		case TELEPAT:
#ifdef DEBUG
			debugpline("Trying to give telepathy");
#endif
			if (!(HTelepat & FROMOUTSIDE)) {
				pline(Hallucination ? "You feel in touch with the cosmos." : "You feel a strange mental acuity.");
				HTelepat |= FROMOUTSIDE;
				/* If blind, make sure monsters show up. */
				if (Blind) see_monsters();
			}
			break;
		default:
#ifdef DEBUG
			debugpline("Tried to give an impossible intrinsic");
#endif
			break;
	}
}

// called after completely consuming a corpse
static void cpostfx(int pm) {
	int tmp = 0;
	boolean catch_lycanthropy = false;

	/* in case `afternmv' didn't get called for previously mimicking
	   gold, clean up now to avoid `eatmbuf' memory leak */
	if (eatmbuf) eatmdone();

	switch (pm) {
		case PM_NEWT:
			/* MRKR: "eye of newt" may give small magical energy boost */
			if (rn2(3) || 3 * u.uen <= 2 * u.uenmax) {
				int old_uen = u.uen;
				u.uen += rnd(3);
				if (u.uen > u.uenmax) {
					if (!rn2(3)) u.uenmax++;
					u.uen = u.uenmax;
				}
				if (old_uen != u.uen) {
					pline("You feel a mild buzz.");
					context.botl = 1;
				}
			}
			break;
		case PM_WRAITH:
			switch (rnd(10)) {
				case 1:
					pline("You feel that was a bad idea.");
					losexp("eating a wraith corpse", false);
					break;
				case 2:
					pline("You don't feel so good ...");
					if (Upolyd) {
						u.mhmax -= 4;
						if (u.mhmax < 1) u.mhmax = 1;
					} else {
						u.uhpmax -= 4;
						if (u.uhpmax < 1) u.uhpmax = 1;
					}
					u.uenmax -= 8;
					if (u.uenmax < 1) u.uenmax = 1;
					u.uen -= 8;
					if (u.uen < 0) u.uen = 0;
					losehp(4, "eating a wraith corpse", KILLED_BY);
					break;
				case 3:
				case 4:
					pline("You feel something strange for a moment.");
					break;
				case 5:
					pline("You feel physically and mentally stronger!");
					if (Upolyd) {
						u.mhmax += 4;
						u.mh = u.mhmax;
					} else {
						u.uhpmax += 4;
						u.uhp = u.uhpmax;
					}
					u.uenmax += 8;
					u.uen = u.uenmax;
					break;
				case 6:
				case 7:
				case 8:
				case 9:
				case 10:
					pline("You feel that was a smart thing to do.");
					pluslvl(false);
					break;
				default:
					break;
			}
			context.botl = 1;
			break;
		case PM_HUMAN_WERERAT:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WERERAT;
			break;
		case PM_HUMAN_WEREJACKAL:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WEREJACKAL;
			break;
		case PM_HUMAN_WEREWOLF:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WEREWOLF;
			break;
		case PM_HUMAN_WEREPANTHER:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WEREPANTHER;
			break;
		case PM_HUMAN_WERETIGER:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WERETIGER;
			break;
		case PM_HUMAN_WERESNAKE:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WERESNAKE;
			break;
		case PM_HUMAN_WERESPIDER:
			catch_lycanthropy = true;
			if (!Race_if(PM_HUMAN_WEREWOLF)) u.ulycn = PM_WERESPIDER;
			break;
		case PM_NURSE:
			if (Upolyd)
				u.mh = u.mhmax;
			else
				u.uhp = u.uhpmax;
			context.botl = 1;
			break;

		case PM_STALKER:
			if (!Invis) {
				set_itimeout(&HInvis, (long)rn1(100, 50));
				if (!Blind && !BInvis) self_invis_message();
			} else {
				if (!(HInvis & INTRINSIC)) pline("You feel hidden!");
				HInvis |= FROMOUTSIDE;
				HSee_invisible |= FROMOUTSIDE;
			}
			newsym(u.ux, u.uy);
		fallthru;
		case PM_YELLOW_LIGHT:
		case PM_GIANT_BAT:
			make_stunned(HStun + 30, false);
		fallthru;
		case PM_BAT:
			make_stunned(HStun + 30, false);
			break;

		case PM_GIANT_MIMIC:
			tmp += 10;
		fallthru;
		case PM_LARGE_MIMIC:
			tmp += 20;
		fallthru;
		case PM_SMALL_MIMIC:
			tmp += 20;
			if (youmonst.data->mlet != S_MIMIC && !Unchanging) {
				u.uconduct.polyselfs++;	// 'change form'
				char buf[BUFSZ];
				pline("You can't resist the temptation to mimic %s.",
				      Hallucination ? "an orange" : "a pile of gold");

				/* A pile of gold can't ride. */
				if (u.usteed) dismount_steed(DISMOUNT_FELL);

				nomul(-tmp);
				sprintf(buf, Hallucination ? "You suddenly dread being peeled and mimic %s again!" : "You now prefer mimicking %s again.",
					an(Upolyd ? youmonst.data->mname : urace.noun));
				eatmbuf = strcpy(alloc(strlen(buf) + 1), buf);
				nomovemsg = eatmbuf;
				afternmv = eatmdone;
				/* ??? what if this was set before? */
				youmonst.m_ap_type = M_AP_OBJECT;
				youmonst.mappearance = Hallucination ? ORANGE : GOLD_PIECE;
				newsym(u.ux, u.uy);
				curs_on_u();
				/* make gold symbol show up now */
				display_nhwindow(WIN_MAP, true);
			}
			break;
		case PM_QUANTUM_MECHANIC:
			pline("Your velocity suddenly seems very uncertain!");
			if (HFast & INTRINSIC) {
				HFast &= ~INTRINSIC;
				pline("You seem slower.");
			} else {
				HFast |= FROMOUTSIDE;
				pline("You seem faster.");
			}
			break;
		case PM_LIZARD:
			if (HStun > 2) make_stunned(2L, false);
			if (HConfusion > 2) make_confused(2L, false);
			break;
		case PM_CHAMELEON:
		case PM_DOPPELGANGER:
//		case PM_SANDESTIN:
			if (!Unchanging) {
				pline("You feel a change coming over you.");
				polyself(0);
			}
			break;
		case PM_GENETIC_ENGINEER: /* Robin Johnson -- special msg */
			if (!Unchanging) {
				pline("You undergo a freakish metamorphosis!");
				polyself(0);
			}
			break;

		/* WAC all mind flayers as per mondata.h have to be here */
		case PM_MASTER_MIND_FLAYER:
		case PM_MIND_FLAYER:
			if (ABASE(A_INT) < ATTRMAX(A_INT)) {
				if (!rn2(2)) {
					pline("Yum! That was real brain food!");
					adjattrib(A_INT, 1, false);
					break; /* don't give them telepathy, too */
				}
			} else {
				pline("For some reason, that tasted bland.");
			}

		fallthru;
		default: {
			struct permonst *ptr = &mons[pm];
			int i, count;

			if (dmgtype(ptr, AD_STUN) || dmgtype(ptr, AD_HALU) ||
			    pm == PM_VIOLET_FUNGUS) {
				pline("Oh wow!  Great stuff!");
				make_hallucinated(HHallucination + 200, false, 0L);
			}
			if (is_giant(ptr) && !rn2(4)) gainstr(NULL, 0);

			/* Check the monster for all of the intrinsics.  If this
			 * monster can give more than one, pick one to try to give
			 * from among all it can give.
			 *
			 * If a monster can give 4 intrinsics then you have
			 * a 1/1 * 1/2 * 2/3 * 3/4 = 1/4 chance of getting the first,
			 * a 1/2 * 2/3 * 3/4 = 1/4 chance of getting the second,
			 * a 1/3 * 3/4 = 1/4 chance of getting the third,
			 * and a 1/4 chance of getting the fourth.
			 *
			 * And now a proof by induction:
			 * it works for 1 intrinsic (1 in 1 of getting it)
			 * for 2 you have a 1 in 2 chance of getting the second,
			 *	otherwise you keep the first
			 * for 3 you have a 1 in 3 chance of getting the third,
			 *	otherwise you keep the first or the second
			 * for n+1 you have a 1 in n+1 chance of getting the (n+1)st,
			 *	otherwise you keep the previous one.
			 * Elliott Kleinrock, October 5, 1990
			 */

			count = 0; /* number of possible intrinsics */
			tmp = 0;   /* which one we will try to give */
			for (i = 1; i <= LAST_PROP; i++) {
				if (intrinsic_possible(i, ptr)) {
					count++;
					/* a 1 in count chance of replacing the old
					 * one with this one, and a count-1 in count
					 * chance of keeping the old one.  (note
					 * that 1 in 1 and 0 in 1 are what we want
					 * for the first one
					 */
					if (!rn2(count)) {
#ifdef DEBUG
						debugpline("Intrinsic %d replacing %d",
							   i, tmp);
#endif
						tmp = i;
					}
				}
			}

			/* if any found try to give them one */
			if (count) givit(tmp, ptr);
		} break;
	}

	if (!Race_if(PM_HUMAN_WEREWOLF) &&
	    catch_lycanthropy && defends(AD_WERE, uwep)) {
		if (!touch_artifact(uwep, &youmonst)) {
			dropx(uwep);
			uwepgone();
		}
	}

	return;
}

void violated_vegetarian(void) {
	u.uconduct.unvegetarian++;
	if (Role_if(PM_MONK)) {
		pline("You feel guilty.");
		adjalign(-1);
	}
	return;
}

/* common code to check and possibly charge for 1 context.context.tin.tin,
 * will split() context.tin.tin if necessary */
static void costly_tin(const char *verb /* if 0, the verb is "open" */) {
	if (((!carried(context.tin.tin) &&
	      costly_spot(context.tin.tin->ox, context.tin.tin->oy) &&
	      !context.tin.tin->no_charge) ||
	     context.tin.tin->unpaid)) {
		verbalize("You %s it, you bought it!", verb ? verb : "open");
		if (context.tin.tin->quan > 1L) {
			context.tin.tin = splitobj(context.tin.tin, 1L);
			if (context.tin.tin)
				context.tin.o_id = context.tin.tin->o_id;
		}
		bill_dummy_object(context.tin.tin);
	}
}

int tin_variety_txt(char *s, int *tinvariety) {
	usize k, l;
	if (s && tinvariety) {
		*tinvariety = -1;
		for (k = 0; k < TTSZ-1; ++k) {
			l = strlen(tintxts[k].txt);
			if (!strncmpi(s, tintxts[k].txt, l) && (strlen(s) > l) && s[l] == ' ') {
				*tinvariety = k;
				return (l + 1);
			}
		}
	}
	return 0;
}

void set_tin_variety(struct obj *obj, int forcetype) {
	int r;

	if (forcetype == SPINACH_TIN) {
		obj->spe = 1;
		return;
	} else if (forcetype >= 0 && forcetype < TTSZ-1) {
		r = forcetype;
	} else { // RANDOM_TIN
		r = rn2(TTSZ-1);		// take your pick
		if (r == ROTTEN_TIN && (obj->corpsenm == PM_LIZARD || obj->corpsenm == PM_LICHEN))
			r = HOMEMADE_TIN;	// lizards don't rot
	}
	obj->spe = -(r+1); // offset by 1 to allow index 0
}

static int tin_variety(struct obj *obj) {
	int r;

	if (obj->spe != 1 && obj->cursed) {
		r = ROTTEN_TIN;                 // always rotten if cursed
	} else if (obj->spe < 0) {
		r = -(obj->spe);
		--r; // get rid of the offset
	} else
		r = rn2(TTSZ-1);

	if (r == HOMEMADE_TIN &&
			!obj->blessed && !rn2(7))
		r = ROTTEN_TIN;                 // some homemade tins go bad

	if (r == ROTTEN_TIN && (obj->corpsenm == PM_LIZARD ||
				obj->corpsenm == PM_LICHEN))
		r = HOMEMADE_TIN;               // lizards don't rot
	return r;
}



// called during each move whilst opening a tin
static int opentin(void) {
	int r;
	const char *what;
	int which;

	if (!carried(context.tin.tin) && !obj_here(context.tin.tin, u.ux, u.uy))
		/* perhaps it was stolen? */
		return 0; /* %% probably we should use tinoid */
	if (context.tin.usedtime++ >= 50) {
		pline("You give up your attempt to open the tin.");
		return 0;
	}
	if (context.tin.usedtime < context.tin.reqtime)
		return 1; /* still busy */
	if (context.tin.tin->otrapped ||
	    (context.tin.tin->cursed && context.tin.tin->spe != -1 && !rn2(8))) {
		b_trapped("tin", 0);
		costly_tin("destroyed");
		goto use_me;
	}
	pline("You succeed in opening the tin.");
	if (context.tin.tin->spe != 1) {
		if (context.tin.tin->corpsenm == NON_PM) {
			pline("It turns out to be empty.");
			context.tin.tin->dknown = context.tin.tin->known = true;
			costly_tin(NULL);
			goto use_me;
		}
		r = tin_variety(context.tin.tin);
		which = 0;		/* 0=>plural, 1=>as-is, 2=>"the" prefix */
		if (Hallucination) {
			what = rndmonnam();
		} else {
			what = mons[context.tin.tin->corpsenm].mname;
			if (mons[context.tin.tin->corpsenm].geno & G_UNIQ)
				which = type_is_pname(&mons[context.tin.tin->corpsenm]) ? 1 : 2;
		}
		if (which == 0) what = makeplural(what);
		/* ALI - you already know the type of the tinned meat */
		if (context.tin.tin->known && mvitals[context.tin.tin->corpsenm].eaten < 255)
			mvitals[context.tin.tin->corpsenm].eaten++;
		/* WAC - you only recognize if you've eaten this before */
		if (!mvitals[context.tin.tin->corpsenm].eaten && !Hallucination) {
			if (rn2(2))
				pline("It smells kind of like %s.",
				      monexplain[mons[context.tin.tin->corpsenm].mlet]);
			else
				pline("The smell is unfamiliar.");
		} else
			pline("It smells like %s%s.", (which == 2) ? "the " : "", what);

		if (yn("Eat it?") == 'n') {
			/* ALI - you know the tin iff you recognized the contents */
			if (mvitals[context.tin.tin->corpsenm].eaten)
				if (!Hallucination) context.tin.tin->dknown = context.tin.tin->known = true;
			if (flags.verbose) pline("You discard the open tin.");
			costly_tin(NULL);
			goto use_me;
		}
		/* in case stop_occupation() was called on previous meal */
		context.victual.piece = NULL;
		context.victual.o_id = 0;
		context.victual.fullwarn = context.victual.eating = context.victual.doreset = false;

		/* WAC - you only recognize if you've eaten this before */
		pline("You consume %s %s.", tintxts[r].txt,
		      mvitals[context.tin.tin->corpsenm].eaten ?
			      mons[context.tin.tin->corpsenm].mname :
			      "food");

		/* KMH, conduct */
		u.uconduct.food++;
		if (!vegan(&mons[context.tin.tin->corpsenm]))
			u.uconduct.unvegan++;
		if (!vegetarian(&mons[context.tin.tin->corpsenm]))
			violated_vegetarian();

		if (mvitals[context.tin.tin->corpsenm].eaten)
			context.tin.tin->dknown = context.tin.tin->known = true;
		cprefx(context.tin.tin->corpsenm);
		cpostfx(context.tin.tin->corpsenm);

		/* charge for one at pre-eating cost */
		costly_tin(NULL);

		/* check for vomiting added by GAN 01/16/87 */
		if (tintxts[r].nut < 0)
			make_vomiting((long)rn1(15, 10), false);
		else
			lesshungry(tintxts[r].nut);

		if (r == 0 || r == FRENCH_FRIED_TIN) {
			/* Assume !Glib, because you can't open tins when Glib. */
			incr_itimeout(&Glib, rnd(15));
			pline("Eating deep fried food made your %s very slippery.",
			      makeplural(body_part(FINGER)));
		}
	} else {
		if (context.tin.tin->cursed)
			pline("It contains some decaying%s%s substance.",
			      Blind ? "" : " ", Blind ? "" : hcolor(NH_GREEN));
		else
			pline("It contains spinach.");

		if (yn("Eat it?") == 'n') {
			if (!Hallucination && !context.tin.tin->cursed)
				context.tin.tin->dknown = context.tin.tin->known = true;
			if (flags.verbose)
				pline("You discard the open tin.");
			costly_tin(NULL);
			goto use_me;
		}

		context.tin.tin->dknown = context.tin.tin->known = true;
		costly_tin(NULL);

		if (!context.tin.tin->cursed)
			pline("This makes you feel like %s!",
			      Hallucination ? "Swee'pea" : "Popeye");
		lesshungry(600);
		gainstr(context.tin.tin, 0);
		u.uconduct.food++;
	}
use_me:
	if (carried(context.tin.tin)) useup(context.tin.tin);
	else useupf(context.tin.tin, 1L);
	context.tin.tin = NULL;
	context.tin.o_id = 0;
	return 0;
}

// called when starting to open a tin
static void start_tin(struct obj *otmp) {
	int tmp;

	if (metallivorous(youmonst.data)) {
		pline("You bite right into the metal tin...");
		tmp = 1;
	} else if (nolimbs(youmonst.data)) {
		pline("You cannot handle the tin properly to open it.");
		return;
	} else if (otmp->blessed) {
		pline("The tin opens like magic!");
		tmp = 1;
	} else if (uwep) {
		switch (uwep->otyp) {
			case TIN_OPENER:
				tmp = 1;
				break;
			case DAGGER:
			case SILVER_DAGGER:
			case ELVEN_DAGGER:
			case ORCISH_DAGGER:
			case ATHAME:
			case CRYSKNIFE:
			case DARK_ELVEN_DAGGER:
			case GREAT_DAGGER:
				tmp = 3;
				break;
			case PICK_AXE:
			case AXE:
				tmp = 6;
				break;
			default:
				goto no_opener;
		}
		pline("Using %s you try to open the tin.", yobjnam(uwep, NULL));
	} else {
no_opener:
		pline("It is not so easy to open this tin.");
		if (Glib) {
			pline("The tin slips from your %s.",
			      makeplural(body_part(FINGER)));
			if (otmp->quan > 1L) {
				otmp = splitobj(otmp, 1L);
			}
			if (carried(otmp))
				dropx(otmp);
			else
				stackobj(otmp);
			return;
		}
		tmp = rn1(1 + 500 / ((int)(ACURR(A_DEX) + ACURRSTR)), 10);
	}
	context.tin.reqtime = tmp;
	context.tin.usedtime = 0;
	context.tin.tin = otmp;
	if (context.tin.tin) context.tin.o_id = context.tin.tin->o_id;
	set_occupation(opentin, "opening the tin", 0);
	return;
}

// called when waking up after fainting
int Hear_again(void) {
	// Chance of deafness going away while fainted/sleeping/etc.
	if (!rn2(2)) {
		set_itimeout(&HDeaf, 0);
	}

	return 0;
}

// called on the "first bite" of rotten food
static int rottenfood(struct obj *obj) {
	pline("Blecch!  Rotten %s!", foodword(obj));
	if (!rn2(4)) {
		if (Hallucination)
			pline("You feel rather trippy.");
		else
			pline("You feel rather %s.", body_part(LIGHT_HEADED));
		make_confused(HConfusion + d(2, 4), false);
	} else if (!rn2(4) && !Blind) {
		pline("Everything suddenly goes dark.");
		make_blinded((long)d(2, 10), false);
		if (!Blind) pline("Your %s", "vision quickly clears.");
	} else if (!rn2(3)) {
		const char *what, *where;
		int duration = rnd(10);

		if (!Blind) {
			what = "goes";
			where = "dark";
		} else if (Levitation || Is_airlevel(&u.uz) || Is_waterlevel(&u.uz)) {
			what = "you lose control of";
			where = "yourself";
		} else {
			what = "you slap against the";
			where = u.usteed ? "saddle" : surface(u.ux, u.uy);
		}
		pline("The world spins and %s %s.", what, where);
		incr_itimeout(&HDeaf, duration);
		nomul(-duration);
		nomovemsg = "You are conscious again.";
		afternmv = Hear_again;
		return 1;
	}
	return 0;
}

/* [ALI] Return codes:
 *
 *	0 - Ready to start eating
 *	1 - Corpse partly eaten, but don't start occupation
 *	2 - Corpse completely consumed, context.victual.piece left dangling
 *	3 - Corpse was inedible
 */
// called when a corpse is selected as food
static int eatcorpse(struct obj *otmp) {
	int tp = 0, mnum = otmp->corpsenm;
	long rotted = 0L;
	boolean uniq = !!(mons[mnum].geno & G_UNIQ);
	int retcode = 0;
	boolean stoneable = (flesh_petrifies(&mons[mnum]) && !Stone_resistance &&
			     !poly_when_stoned(youmonst.data));

	/* KMH, conduct */
	if (!vegan(&mons[mnum])) u.uconduct.unvegan++;
	if (!vegetarian(&mons[mnum])) violated_vegetarian();

	if (mnum != PM_LIZARD && mnum != PM_LICHEN) {
		long age = peek_at_iced_corpse_age(otmp);

		rotted = (monstermoves - age) / (10L + rn2(20));
		if (otmp->cursed)
			rotted += 2L;
		else if (otmp->blessed)
			rotted -= 2L;
	}

	/* Vampires only drink the blood of very young, meaty corpses
	 * is_edible only allows meaty corpses here
	 * Blood is assumed to be 1/5 of the nutrition
	 * Thus happens before the conduct checks intentionally - should it be after?
	 * Blood is assumed to be meat and flesh.
	 */
	if (is_vampire(youmonst.data)) {
		/* oeaten is set up by touchfood */
		if (otmp->odrained ? otmp->oeaten <= drainlevel(otmp) :
				     otmp->oeaten < mons[otmp->corpsenm].cnutrit) {
			pline("There is no blood left in this corpse!");
			return 3;
		} else if (rotted <= 0 &&
			   (peek_at_iced_corpse_age(otmp) + 5) >= monstermoves) {
			char buf[BUFSZ];

			/* Generate the name for the corpse */
			if (!uniq || Hallucination)
				sprintf(buf, "%s", the(corpse_xname(otmp, true)));
			else
				sprintf(buf, "%s%s corpse",
					!type_is_pname(&mons[mnum]) ? "the " : "",
					s_suffix(mons[mnum].mname));

			pline("You drain the blood from %s.", buf);
			otmp->odrained = 1;
		} else {
			pline("The blood in this corpse has coagulated!");
			return 3;
		}
	} else
		otmp->odrained = 0;

	/* Very rotten corpse will make you sick unless you are a ghoul or a ghast */
	if (mnum != PM_ACID_BLOB && !stoneable && rotted > 5L) {
		boolean cannibal = maybe_cannibal(mnum, false);
		if (u.umonnum == PM_GHOUL || u.umonnum == PM_GHAST) {
			pline("Yum - that %s was well aged%s!",
			      mons[mnum].mlet == S_FUNGUS ? "fungoid vegetation" :
							    !vegetarian(&mons[mnum]) ? "meat" : "protoplasm",
			      cannibal ? ", cannibal" : "");
		} else {
			pline("Ulch - that %s was tainted%s!",
			      mons[mnum].mlet == S_FUNGUS ? "fungoid vegetation" :
							    !vegetarian(&mons[mnum]) ? "meat" : "protoplasm",
			      cannibal ? ", you cannibal" : "");
			if (Sick_resistance) {
				pline("It doesn't seem at all sickening, though...");
			} else {
				char buf[BUFSZ];
				long sick_time;

				sick_time = (long)rn1(10, 10);
				/* make sure new ill doesn't result in improvement */
				if (Sick && (sick_time > Sick))
					sick_time = (Sick > 1L) ? Sick - 1L : 1L;
				if (!uniq || Hallucination)
					sprintf(buf, "rotted %s", corpse_xname(otmp, true));
				else
					sprintf(buf, "%s%s rotted corpse",
						!type_is_pname(&mons[mnum]) ? "the " : "",
						s_suffix(mons[mnum].mname));
				make_sick(sick_time, buf, true, SICK_VOMITABLE);
			}
			if (carried(otmp))
				useup(otmp);
			else
				useupf(otmp, 1L);
			return 2;
		}
	} else if (youmonst.data == &mons[PM_GHOUL] ||
		   youmonst.data == &mons[PM_GHAST]) {
		pline("This corpse is too fresh!");
		return 3;
	} else if (acidic(&mons[mnum]) && !Acid_resistance) {
		tp++;
		pline("You have a very bad case of stomach acid."); /* not body_part() */
		losehp(rnd(15), "acidic corpse", KILLED_BY_AN);
	} else if (poisonous(&mons[mnum]) && rn2(5)) {
		tp++;
		pline("Ecch - that must have been poisonous!");
		if (!Poison_resistance) {
			losestr(rnd(4));
			losehp(rnd(15), "poisonous corpse", KILLED_BY_AN);
		} else
			pline("You seem unaffected by the poison.");
		/* now any corpse left too long will make you mildly ill */
	} else if ((rotted > 5L || (rotted > 3L && rn2(5))) && !Sick_resistance) {
		tp++;
		pline("You feel %ssick.", (Sick) ? "very " : "");
		losehp(rnd(8), "cadaver", KILLED_BY_AN);
	}

	/* delay is weight dependent */
	context.victual.reqtime = 3 + (mons[mnum].cwt >> 6);
	if (otmp->odrained) context.victual.reqtime = rounddiv(context.victual.reqtime, 5);

	if (!tp && mnum != PM_LIZARD && mnum != PM_LICHEN &&
	    (otmp->orotten || !rn2(7))) {
		if (rottenfood(otmp)) {
			otmp->orotten = true;
			touchfood(otmp);
			retcode = 1;
		}

		if (!mons[otmp->corpsenm].cnutrit) {
			/* no nutrution: rots away, no message if you passed out */
			if (!retcode) pline("The corpse rots away completely.");
			if (carried(otmp))
				useup(otmp);
			else
				useupf(otmp, 1L);
			retcode = 2;
		}

		if (!retcode) consume_oeaten(otmp, 2); /* oeaten >>= 2 */
		if (retcode < 2 && otmp->odrained && otmp->oeaten < drainlevel(otmp))
			otmp->oeaten = drainlevel(otmp);
	} else if (!is_vampire(youmonst.data)) {
		boolean pname = type_is_pname(&mons[mnum]);
		pline("%s%s %s!",
		      !uniq ? "This " : !pname ? "The " : "",
		      uniq && pname ?
			      Food_xname(otmp, false) :
			      food_xname(otmp, false),
		      (vegan(&mons[mnum]) ?
			       (!carnivorous(youmonst.data) && herbivorous(youmonst.data)) :
			       (carnivorous(youmonst.data) && !herbivorous(youmonst.data))) ?
			      "is delicious" :
			      "tastes terrible");
	}

	/* WAC Track food types eaten */
	if (mvitals[mnum].eaten < 255) mvitals[mnum].eaten++;

	return retcode;
}

// called as you start to eat
static void start_eating(struct obj *otmp) {
#ifdef DEBUG
	debugpline("start_eating: %lx (victual = %lx)", otmp, context.victual.piece);
	debugpline("reqtime = %d", context.victual.reqtime);
	debugpline("(original reqtime = %d)", objects[otmp->otyp].oc_delay);
	debugpline("nmod = %d", context.victual.nmod);
	debugpline("oeaten = %d", otmp->oeaten);
#endif
	context.victual.fullwarn = context.victual.doreset = false;
	context.victual.eating = true;

	if (otmp->otyp == CORPSE) {
		cprefx(context.victual.piece->corpsenm);
		if (!context.victual.piece || !context.victual.eating) {
			/* rider revived, or died and lifesaved */
			return;
		}
	}

	if (bite()) return;

	if (++context.victual.usedtime >= context.victual.reqtime) {
		/* print "finish eating" message if they just resumed -dlc */
		done_eating(context.victual.reqtime > 1 ? true : false);
		return;
	}

	sprintf(msgbuf, "%s %s", otmp->odrained ? "draining" : "eating",
		food_xname(otmp, true));
	set_occupation(eatfood, msgbuf, 0);
}

/*
 * called on "first bite" of (non-corpse) food.
 * used for non-rotten non-tin non-corpse food
 */
static void fprefx(struct obj *otmp) {
	switch (otmp->otyp) {
		case FOOD_RATION:
			if (u.uhunger <= 200)
				pline(Hallucination ? "Oh wow, like, superior, man!" :
						      "That food really hit the spot!");
			else if (u.uhunger <= 700)
				pline("That satiated your %s!",
				      body_part(STOMACH));
			break;
		case TRIPE_RATION:
			if ((carnivorous(youmonst.data) && (!humanoid(youmonst.data))) ||
			    (u.ulycn != NON_PM && carnivorous(&mons[u.ulycn]) &&
			     !humanoid(&mons[u.ulycn])))
				/* Symptom of lycanthropy is starting to like your
			 * alternative form's food!
			 */
				pline("That tripe ration was surprisingly good!");
			else if (maybe_polyd(is_orc(youmonst.data), Race_if(PM_ORC)))
				pline(Hallucination ? "Tastes great! Less filling!" :
						      "Mmm, tripe... not bad!");
			else {
				pline("Yak - dog food!");
				more_experienced(1, 0);
				newexplevel();
				/* not cannibalism, but we use similar criteria
			   for deciding whether to be sickened by this meal */
				if (rn2(2) && !CANNIBAL_ALLOWED())
					make_vomiting((long)rn1(context.victual.reqtime, 14), false);
			}
			break;
		case PILL:
			pline("You swallow the little pink pill.");
			switch (rn2(7)) {
				case 0:
					/* [Tom] wishing pills are from the Land of Oz */
					pline("The pink sugar coating hid a silver wishing pill!");
					makewish();
					break;
				case 1:
					if (!Poison_resistance) {
						pline("You feel your stomach twinge.");
						losestr(rnd(4));
						losehp(rnd(15), "poisonous pill", KILLED_BY_AN);
					} else
						pline("You seem unaffected by the poison.");
					break;
				case 2:
					pline("Everything begins to get blurry.");
					make_stunned(HStun + 30, false);
					break;
				case 3:
					pline("Oh wow!  Look at the lights!");
					make_hallucinated(HHallucination + 150, false, 0L);
					break;
				case 4:
					pline("That tasted like vitamins...");
					lesshungry(600);
					break;
				case 5:
					if (Sleep_resistance) {
						pline("Hmm. Nothing happens.");
					} else {
						pline("You feel drowsy...");
						nomul(-rn2(50));
						u.usleep = 1;
						nomovemsg = "You wake up.";
					}
					break;
				case 6:
					pline("Wow... everything is moving in slow motion...");
					/* KMH, balance patch -- Use incr_itimeout() instead of += */
					incr_itimeout(&HFast, rn1(10, 200));
					break;
			}
			break;
		case MUSHROOM:
			pline("This %s is %s", singular(otmp, xname),
			      otmp->cursed ? (Hallucination ? "far-out!" : "terrible!") :
					     Hallucination ? "groovy!" : "delicious!");
			switch (rn2(10)) {
				case 0:
				case 1:
					if (!Poison_resistance) {
						pline("You feel rather ill....");
						losestr(rnd(4));
						losehp(rnd(15), "poisonous mushroom", KILLED_BY_AN);
					} else
						pline("You burp loudly.");
					break;
				case 2:
					pline("That mushroom tasted a little funny.");
					make_stunned(HStun + 30, false);
					break;
				case 3:
					pline("Whoa! Everything looks groovy!");
					make_hallucinated(HHallucination + 150, false, 0L);
					break;
				case 4:
					gainstr(otmp, 1);
					pline("You feel stronger!");
					break;
				case 5:
				case 6:
				case 7:
				case 8:
				case 9:
					break;
			}
			break;
		case MEATBALL:
		case MEAT_STICK:
		case HUGE_CHUNK_OF_MEAT:
		case MEAT_RING:
			goto give_feedback;

		case CLOVE_OF_GARLIC:
			if (is_undead(youmonst.data)) {
				make_vomiting((long)rn1(context.victual.reqtime, 5), false);
				break;
			}
		fallthru;
		default:
			if (otmp->otyp == SLIME_MOLD && !otmp->cursed && otmp->spe == current_fruit)
				pline("My, that was a %s %s!",
				      Hallucination ? "primo" : "yummy",
				      singular(otmp, xname));
			else
#ifdef UNIX
				if (otmp->otyp == APPLE || otmp->otyp == PEAR) {
				if (!Hallucination) {
					pline("Core dumped.");
				} else {
					// This is based on an old Usenet joke, a fake a.out manual page
					int x = rnd(100);
					if (x <= 75)
						pline("Segmentation fault -- core dumped.");
					else if (x <= 98)
						pline("Bus error -- core dumped.");
					else
						pline("Yo' mama -- core dumped.");
				}
			} else
#elif defined(MAC)	// KMH -- Why should Unix have all the fun?
				if (otmp->otyp == APPLE) {
				pline("This Macintosh is wonderful!");
			} else
#endif
				if (otmp->otyp == EGG && stale_egg(otmp)) {
				pline("Ugh.  Rotten egg."); /* perhaps others like it */
				make_vomiting(Vomiting + d(10, 4), true);
			} else {
				boolean bad_for_you;
			give_feedback:
				bad_for_you = otmp->cursed ||
					      (Race_if(PM_HUMAN_WEREWOLF) &&
					       otmp->otyp == SPRIG_OF_WOLFSBANE);
				pline("This %s is %s", singular(otmp, xname),
				      bad_for_you ? (Hallucination ? "grody!" : "terrible!") :
						    (otmp->otyp == CRAM_RATION || otmp->otyp == K_RATION || otmp->otyp == C_RATION) ? "bland." :
																      Hallucination ? "gnarly!" : "delicious!");
			}
			break;
	}
}

/* increment a combat intrinsic with limits on its growth */
static int bounded_increase(int old, int inc, int typ) {
	int absold, absinc, sgnold, sgninc;

	/* don't include any amount coming from worn rings */
	if (uright && uright->otyp == typ) old -= uright->spe;
	if (uleft && uleft->otyp == typ) old -= uleft->spe;
	absold = abs(old), absinc = abs(inc);
	sgnold = sgn(old), sgninc = sgn(inc);

	if (absinc == 0 || sgnold != sgninc || absold + absinc < 10) {
		;       /* use inc as-is */
	} else if (absold + absinc < 20) {
		absinc = rnd(absinc);   /* 1..n */
		if (absold + absinc < 10) absinc = 10 - absold;
		inc = sgninc * absinc;
	} else if (absold + absinc < 40) {
		absinc = rn2(absinc) ? 1 : 0;
		if (absold + absinc < 20) absinc = rnd(20 - absold);
		inc = sgninc * absinc;
	} else {
		inc = 0;        /* no further increase allowed via this method */
	}
	return old + inc;
}


static void accessory_has_effect(struct obj *otmp) {
	pline("Magic spreads through your body as you digest the %s.",
	      otmp->oclass == RING_CLASS ? "ring" : "amulet");
}

static void eataccessory(struct obj *otmp) {
	int typ = otmp->otyp;
	long oldprop;

	/* Note: rings are not so common that this is unbalancing. */
	/* (How often do you even _find_ 3 rings of polymorph in a game?) */
	/* KMH, intrinsic patch -- several changes below */
	oldprop = u.uprops[objects[typ].oc_oprop].intrinsic;
	if (otmp == uleft || otmp == uright) {
		Ring_gone(otmp);
		if (u.uhp <= 0) return; /* died from sink fall */
	}
	otmp->known = otmp->dknown = 1; /* by taste */
	if (!rn2(otmp->oclass == RING_CLASS ? 3 : 5)) {
		switch (otmp->otyp) {
			default:
				if (!objects[typ].oc_oprop) break; /* should never happen */

				if (!(u.uprops[objects[typ].oc_oprop].intrinsic & FROMOUTSIDE))
					accessory_has_effect(otmp);

				u.uprops[objects[typ].oc_oprop].intrinsic |= FROMOUTSIDE;

				switch (typ) {
					case RIN_SEE_INVISIBLE:
						set_mimic_blocking();
						see_monsters();
						if (Invis && !oldprop && !ESee_invisible &&
						    !perceives(youmonst.data) && !Blind) {
							newsym(u.ux, u.uy);
							pline("Suddenly you can see yourself.");
							makeknown(typ);
						}
						break;
					case RIN_INVISIBILITY:
						if (!oldprop && !EInvis && !BInvis &&
						    !See_invisible && !Blind) {
							newsym(u.ux, u.uy);
							pline("Your body takes on a %s transparency...",
							      Hallucination ? "normal" : "strange");
							makeknown(typ);
						}
						break;
					case RIN_PROTECTION_FROM_SHAPE_CHANGERS:
						rescham();
						break;
					case RIN_LEVITATION:
						/* undo the `.intrinsic |= FROMOUTSIDE' done above */
						u.uprops[LEVITATION].intrinsic = oldprop;
						if (!Levitation) {
							float_up();
							incr_itimeout(&HLevitation, d(10, 20));
							makeknown(typ);
						}
						break;
				}
				break;
			case RIN_ADORNMENT:
				accessory_has_effect(otmp);
				if (adjattrib(A_CHA, otmp->spe, -1))
					makeknown(typ);
				break;
			case RIN_GAIN_STRENGTH:
				accessory_has_effect(otmp);
				if (adjattrib(A_STR, otmp->spe, -1))
					makeknown(typ);
				break;
			case RIN_GAIN_CONSTITUTION:
				accessory_has_effect(otmp);
				if (adjattrib(A_CON, otmp->spe, -1))
					makeknown(typ);
				break;
			case RIN_GAIN_INTELLIGENCE:
				accessory_has_effect(otmp);
				if (adjattrib(A_INT, otmp->spe, -1))
					makeknown(typ);
				break;
			case RIN_GAIN_WISDOM:
				accessory_has_effect(otmp);
				if (adjattrib(A_WIS, otmp->spe, -1))
					makeknown(typ);
				break;
			case RIN_GAIN_DEXTERITY:
				accessory_has_effect(otmp);
				if (adjattrib(A_DEX, otmp->spe, -1))
					makeknown(typ);
				break;
			case RIN_INCREASE_ACCURACY:
				accessory_has_effect(otmp);
				u.uhitinc = bounded_increase(u.uhitinc, otmp->spe, otmp->otyp);
				break;
			case RIN_INCREASE_DAMAGE:
				accessory_has_effect(otmp);
				u.udaminc = bounded_increase(u.udaminc, otmp->spe, otmp->otyp);
				break;
			case RIN_PROTECTION:
				accessory_has_effect(otmp);
				HProtection |= FROMOUTSIDE;
				u.ublessed = bounded_increase(u.ublessed, otmp->spe, otmp->otyp);
				context.botl = 1;
				break;
			case RIN_FREE_ACTION:
				/* Give sleep resistance instead */
				if (!(HSleep_resistance & FROMOUTSIDE))
					accessory_has_effect(otmp);
				if (!Sleep_resistance)
					pline("You feel wide awake.");
				HSleep_resistance |= FROMOUTSIDE;
				break;
			case AMULET_OF_CHANGE:
				accessory_has_effect(otmp);
				makeknown(typ);
				change_sex();
				pline("You are suddenly very %s!",
				      flags.female ? "feminine" : "masculine");
				context.botl = 1;
				break;
			case AMULET_OF_UNCHANGING:
				/* un-change: it's a pun */
				if (!Unchanging && Upolyd) {
					accessory_has_effect(otmp);
					makeknown(typ);
					rehumanize();
				}
				break;
			case AMULET_OF_STRANGULATION: /* bad idea! */
				/* no message--this gives no permanent effect */
				choke(otmp);
				break;
			case AMULET_OF_RESTFUL_SLEEP: /* another bad idea! */
			case RIN_SLEEPING:
				if (!(HSleeping & FROMOUTSIDE))
					accessory_has_effect(otmp);
				HSleeping = FROMOUTSIDE | rnd(100);
				break;
			case AMULET_VERSUS_STONE:
				/* no message--this gives no permanent effect */
				uunstone();
				break;
			case RIN_SUSTAIN_ABILITY:
			case AMULET_OF_FLYING: /* Intrinsic flying not supported --ALI */
			case AMULET_OF_LIFE_SAVING:
			case AMULET_OF_REFLECTION: /* nice try */
			case AMULET_OF_DRAIN_RESISTANCE:
				/* can't eat Amulet of Yendor or fakes,
			 * and no oc_prop even if you could -3.
			 */
				break;
		}
	}
}

// called after eating non-food
static void eatspecial(void) {
	struct obj *otmp = context.victual.piece;

	/* lesshungry wants an occupation to handle choke messages correctly */
	set_occupation(eatfood, "eating non-food", 0);
	lesshungry(context.victual.nmod);
	occupation = 0;
	context.victual.piece = NULL;
	context.victual.o_id = 0;
	context.victual.eating = 0;
	if (otmp->oclass == COIN_CLASS) {
		if (carried(otmp))
			useupall(otmp);
		else
			useupf(otmp, otmp->quan);
		return;
	}
	if (otmp->oclass == POTION_CLASS) {
		otmp->quan++; /* dopotion() does a useup() */
		dopotion(otmp);
	}
	if (otmp->oclass == RING_CLASS || otmp->oclass == AMULET_CLASS)
		eataccessory(otmp);
	else if (otmp->otyp == LEASH && otmp->leashmon)
		o_unleash(otmp);

	/* KMH -- idea by "Tommy the Terrorist" */
	if ((otmp->otyp == TRIDENT) && !otmp->cursed) {
		pline(Hallucination ? "Four out of five dentists agree." :
				      "That was pure chewing satisfaction!");
		exercise(A_WIS, true);
	}
	if ((otmp->otyp == FLINT) && !otmp->cursed) {
		pline("Yabba-dabba delicious!");
		exercise(A_CON, true);
	}

	if (otmp == uwep && otmp->quan == 1L) uwepgone();
	if (otmp == uquiver && otmp->quan == 1L) uqwepgone();
	if (otmp == uswapwep && otmp->quan == 1L) uswapwepgone();

	if (otmp == uball) unpunish();
	if (otmp == uchain)
		unpunish(); /* but no useup() */
	else if (carried(otmp))
		useup(otmp);
	else
		useupf(otmp, 1L);
}

/* NOTE: the order of these words exactly corresponds to the
   order of oc_material values #define'd in objclass.h. */
static const char *foodwords[] = {
	"meal", "liquid", "wax", "food", "meat",
	"paper", "cloth", "leather", "wood", "bone", "scale",
	"metal", "metal", "metal", "silver", "gold", "platinum", "mithril",
	"plastic", "glass", "rich food", "stone"};

static const char *foodword(struct obj *otmp) {
	if (otmp->oclass == FOOD_CLASS) return "food";
	if (otmp->oclass == GEM_CLASS &&
	    objects[otmp->otyp].oc_material == GLASS &&
	    otmp->dknown)
		makeknown(otmp->otyp);
	return foodwords[objects[otmp->otyp].oc_material];
}

// called after consuming (non-corpse) food
static void fpostfx(struct obj *otmp) {
	switch (otmp->otyp) {
		case SPRIG_OF_WOLFSBANE:
			if (u.ulycn >= LOW_PM || is_were(youmonst.data) || Race_if(PM_HUMAN_WEREWOLF))
				you_unwere(true);
			break;
		case HOLY_WAFER:
			if (u.ualign.type == A_LAWFUL) {
				if (u.uhp < u.uhpmax) {
					pline("You feel warm inside.");
					u.uhp += rn1(20, 20);
					if (u.uhp > u.uhpmax) u.uhp = u.uhpmax;
				}
			}
			if (Sick) make_sick(0L, NULL, true, SICK_ALL);
			if (u.ulycn != -1) {
				you_unwere(true);
			}
			if (u.ualign.type == A_CHAOTIC) {
				pline("You feel a burning inside!");
				u.uhp -= rn1(10, 10);
				/* KMH, balance patch 2 -- should not have 0 hp */
				if (u.uhp < 1) u.uhp = 1;
			}
			break;
		case CARROT:
			if (!u.uswallow || !attacktype_fordmg(u.ustuck->data, AT_ENGL, AD_BLND))
				make_blinded(u.ucreamed, true);
			break;
		/* body parts -- now checks for artifact and name*/
		case EYEBALL:
			if (!otmp->oartifact) break;
			pline("You feel a burning inside!");
			u.uhp -= rn1(50, 150);
			if (u.uhp <= 0) {
				killer.format = KILLED_BY;
				killer.name = nhsdupz(food_xname(otmp, true));
				done(CHOKING);
			}
			break;
		case SEVERED_HAND:
			if (!otmp->oartifact) break;
			pline("You feel the hand scrabbling around inside of you!");
			u.uhp -= rn1(50, 150);
			if (u.uhp <= 0) {
				killer.format = KILLED_BY;
				killer.name = nhsdupz(food_xname(otmp, true));
				done(CHOKING);
			}
			break;
		case FORTUNE_COOKIE:
			if (yn("Read the fortune?") == 'y') {
				outrumor(bcsign(otmp), BY_COOKIE);
				if (!Blind) u.uconduct.literate++;
			}
			break;
		/* STEPHEN WHITE'S NEW CODE */
		case LUMP_OF_ROYAL_JELLY:
			/* This stuff seems to be VERY healthy! */
			gainstr(otmp, 1);
			if (Upolyd) {
				u.mh += otmp->cursed ? -rnd(20) : rnd(20);
				if (u.mh > u.mhmax) {
					if (!rn2(17)) u.mhmax++;
					u.mh = u.mhmax;
				} else if (u.mh <= 0) {
					rehumanize();
				}
			} else {
				u.uhp += otmp->cursed ? -rnd(20) : rnd(20);
				if (u.uhp > u.uhpmax) {
					if (!rn2(17)) u.uhpmax++;
					u.uhp = u.uhpmax;
				} else if (u.uhp <= 0) {
					killer.format = KILLED_BY_AN;
					killer.name = nhsdupz("rotten lump of royal jelly");
					done(POISONING);
				}
			}
			if (!otmp->cursed) heal_legs();
			break;
		case EGG:
			if (flesh_petrifies(&mons[otmp->corpsenm])) {
				if (!Stone_resistance &&
				    !(poly_when_stoned(youmonst.data) && polymon(PM_STONE_GOLEM))) {
					if (!Stoned) {
						killer.name = nhsfmt("%S egg", mons[otmp->corpsenm].mname);
						make_stoned(5, NULL, KILLED_BY_AN, killer.name);
					}
				}
			}
			break;
		case EUCALYPTUS_LEAF:
			if (Sick && !otmp->cursed)
				make_sick(0L, NULL, true, SICK_ALL);
			if (Vomiting && !otmp->cursed)
				make_vomiting(0L, true);
			break;
	}

	return;
}
/*
 * return 0 if the food was not dangerous.
 * return 1 if the food was dangerous and you chose to stop.
 * return 2 if the food was dangerous and you chose to eat it anyway.
 */
static int edibility_prompts(struct obj *otmp) {
	/* blessed food detection granted you a one-use
	   ability to detect food that is unfit for consumption
	   or dangerous and avoid it. */

	char buf[4 * BUFSZ], foodsmell[BUFSZ],
		it_or_they[QBUFSZ], eat_it_anyway[QBUFSZ];
	boolean cadaver = (otmp->otyp == CORPSE),
		stoneorslime = false;
	int material = objects[otmp->otyp].oc_material,
	    mnum = otmp->corpsenm;
	long rotted = 0L;

	strcpy(foodsmell, Tobjnam(otmp, "smell"));
	strcpy(it_or_they, (otmp->quan == 1L) ? "it" : "they");
	sprintf(eat_it_anyway, "Eat %s anyway?",
		(otmp->quan == 1L) ? "it" : "one");

	if (cadaver || otmp->otyp == EGG || otmp->otyp == TIN) {
		/* These checks must match those in eatcorpse() */
		stoneorslime = (flesh_petrifies(&mons[mnum]) &&
				!Stone_resistance &&
				!poly_when_stoned(youmonst.data));

		if (mnum == PM_GREEN_SLIME)
			stoneorslime = (!Unchanging && !flaming(youmonst.data) &&
					youmonst.data != &mons[PM_GREEN_SLIME]);

		if (cadaver && mnum != PM_LIZARD && mnum != PM_LICHEN) {
			long age = peek_at_iced_corpse_age(otmp);
			/* worst case rather than random
			   in this calculation to force prompt */
			rotted = (monstermoves - age) / (10L + 0 /* was rn2(20) */);
			if (otmp->cursed)
				rotted += 2L;
			else if (otmp->blessed)
				rotted -= 2L;
		}
	}

	/*
	 * These problems with food should be checked in
	 * order from most detrimental to least detrimental.
	 */

	if (cadaver && mnum != PM_ACID_BLOB && rotted > 5L && !Sick_resistance) {
		/* Tainted meat */
		sprintf(buf, "%s like %s could be tainted! %s",
			foodsmell, it_or_they, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (stoneorslime) {
		sprintf(buf, "%s like %s could be something very dangerous! %s",
			foodsmell, it_or_they, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (otmp->orotten || (cadaver && rotted > 3L)) {
		/* Rotten */
		sprintf(buf, "%s like %s could be rotten! %s",
			foodsmell, it_or_they, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (cadaver && poisonous(&mons[mnum]) && !Poison_resistance) {
		/* poisonous */
		sprintf(buf, "%s like %s might be poisonous! %s",
			foodsmell, it_or_they, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (cadaver && !vegetarian(&mons[mnum]) &&
	    !u.uconduct.unvegetarian && Role_if(PM_MONK)) {
		sprintf(buf, "%s unhealthy. %s",
			foodsmell, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (cadaver && acidic(&mons[mnum]) && !Acid_resistance) {
		sprintf(buf, "%s rather acidic. %s",
			foodsmell, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (Upolyd && u.umonnum == PM_RUST_MONSTER &&
	    is_metallic(otmp) && otmp->oerodeproof) {
		sprintf(buf, "%s disgusting to you right now. %s",
			foodsmell, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}

	/*
	 * Breaks conduct, but otherwise safe.
	 */
	if (!u.uconduct.unvegan &&
	    ((material == LEATHER || material == BONE ||
	      material == EYEBALL || material == SEVERED_HAND ||
	      material == DRAGON_HIDE || material == WAX) ||
	     (cadaver && !vegan(&mons[mnum])))) {
		sprintf(buf, "%s foul and unfamiliar to you. %s",
			foodsmell, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	if (!u.uconduct.unvegetarian &&
	    ((material == LEATHER || material == BONE ||
	      material == EYEBALL || material == SEVERED_HAND ||
	      material == DRAGON_HIDE) ||
	     (cadaver && !vegetarian(&mons[mnum])))) {
		sprintf(buf, "%s unfamiliar to you. %s",
			foodsmell, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}

	if (cadaver && mnum != PM_ACID_BLOB && rotted > 5L && Sick_resistance) {
		/* Tainted meat with Sick_resistance */
		sprintf(buf, "%s like %s could be tainted! %s",
			foodsmell, it_or_they, eat_it_anyway);
		if (yn_function(buf, ynchars, 'n') == 'n')
			return 1;
		else
			return 2;
	}
	return 0;
}

// generic "eat" command funtion (see cmd.c)
int doeat(void) {
	struct obj *otmp;
	int basenutrit;	 // nutrition of full item
	int nutrit;	 // nutrition available
	char qbuf[QBUFSZ];
	char c;

	boolean dont_start = false;
	if (Strangled) {
		pline("If you can't breathe air, how can you consume solids?");
		return 0;
	}
	if (!(otmp = floorfood("eat"))) return 0;
	if (check_capacity(NULL)) return 0;

	if (u.uedibility) {
		int res = edibility_prompts(otmp);
		if (res) {
			pline("Your %s stops tingling and your sense of smell returns to normal.",
			      body_part(NOSE));
			u.uedibility = 0;
			if (res == 1) return 0;
		}
	}

	/* We have to make non-foods take 1 move to eat, unless we want to
	 * do ridiculous amounts of coding to deal with partly eaten plate
	 * mails, players who polymorph back to human in the middle of their
	 * metallic meal, etc....
	 */
	if (!is_edible(otmp)) {
		pline("You cannot eat that!");
		return 0;
	} else if ((otmp->owornmask & (W_ARMOR | W_TOOL | W_AMUL | W_SADDLE)) != 0) {
		/* let them eat rings */
		pline("You can't eat something you're wearing.");
		return 0;
	}
	if (is_metallic(otmp) &&
	    u.umonnum == PM_RUST_MONSTER && otmp->oerodeproof) {
		otmp->rknown = true;
		if (otmp->quan > 1L) {
			if (!carried(otmp))
				splitobj(otmp, otmp->quan - 1L);
			else
				otmp = splitobj(otmp, 1L);
		}
		pline("Ulch - that %s was rustproofed!", xname(otmp));
		/* The regurgitated object's rustproofing is gone now */
		otmp->oerodeproof = 0;
		make_stunned(HStun + rn2(10), true);
		pline("You spit %s out onto the %s.", the(xname(otmp)),
		      surface(u.ux, u.uy));
		if (carried(otmp)) {
			freeinv(otmp);
			dropy(otmp);
		}
		stackobj(otmp);
		return 1;
	}
	if (otmp->otyp == EYEBALL || otmp->otyp == SEVERED_HAND) {
		strcpy(qbuf, "Are you sure you want to eat that?");
		if ((c = yn_function(qbuf, ynqchars, 'n')) != 'y') return 0;
	}

	/* KMH -- Slow digestion is... indigestible */
	if (otmp->otyp == RIN_SLOW_DIGESTION) {
		pline("This ring is indigestible!");
		rottenfood(otmp);
		if (otmp->dknown && !objects[otmp->otyp].oc_name_known && !objects[otmp->otyp].oc_uname)
			docall(otmp);
		return 1;
	}

	if (otmp->oclass != FOOD_CLASS) {
		int material;
		context.victual.reqtime = 1;
		context.victual.piece = otmp;
		context.victual.o_id = otmp->o_id;
		/* Don't split it, we don't need to if it's 1 move */
		context.victual.usedtime = 0;
		context.victual.canchoke = (u.uhs == SATIATED);
		/* Note: gold weighs 1 pt. for each 1000 pieces (see */
		/* pickup.c) so gold and non-gold is consistent. */
		if (otmp->oclass == COIN_CLASS)
			basenutrit = ((otmp->quan > 200000L) ? 2000 : (int)(otmp->quan / 100L));
		else if (otmp->oclass == BALL_CLASS || otmp->oclass == CHAIN_CLASS)
			basenutrit = weight(otmp);
		/* oc_nutrition is usually weight anyway */
		else
			basenutrit = objects[otmp->otyp].oc_nutrition;
		context.victual.nmod = basenutrit;
		context.victual.eating = true; /* needed for lesshungry() */

		material = objects[otmp->otyp].oc_material;
		if (material == LEATHER ||
		    material == EYEBALL || material == SEVERED_HAND ||
		    material == BONE || material == DRAGON_HIDE) {
			u.uconduct.unvegan++;
			violated_vegetarian();
		} else if (material == WAX)
			u.uconduct.unvegan++;
		u.uconduct.food++;

		if (otmp->cursed)
			rottenfood(otmp);

		if (otmp->oclass == WEAPON_CLASS && otmp->opoisoned) {
			pline("Ecch - that must have been poisonous!");
			if (!Poison_resistance) {
				losestr(rnd(4));
				losehp(rnd(15), xname(otmp), KILLED_BY_AN);
			} else {
				pline("You seem unaffected by the poison.");
			}
		} else if (!otmp->cursed)
			pline("%s%s is delicious!", (obj_is_pname(otmp) && otmp->oartifact < ART_ORB_OF_DETECTION) ? "" :
					obj_is_pname(otmp) ? "The " : "This ",
			      otmp->oclass == COIN_CLASS ? foodword(otmp) : singular(otmp, xname));

		eatspecial();
		return 1;
	}

	/* [ALI] Hero polymorphed in the meantime.
	 */
	if (otmp == context.victual.piece &&
	    is_vampire(youmonst.data) != otmp->odrained)
		context.victual.piece = NULL; /* Can't resume */

	/* [ALI] Blood can coagulate during the interruption
	 *       but not during the draining process.
	 */
	if (otmp == context.victual.piece && otmp->odrained &&
	    (peek_at_iced_corpse_age(otmp) + context.victual.usedtime + 5) < monstermoves)
		context.victual.piece = NULL; /* Can't resume */

	if (otmp == context.victual.piece) {
		/* If they weren't able to choke, they don't suddenly become able to
		 * choke just because they were interrupted.  On the other hand, if
		 * they were able to choke before, if they lost food it's possible
		 * they shouldn't be able to choke now.
		 */
		if (u.uhs != SATIATED) context.victual.canchoke = false;
		context.victual.o_id = 0;
		context.victual.piece = touchfood(otmp);
		if (context.victual.piece) context.victual.o_id = context.victual.piece->o_id;
		pline("You resume your meal.");
		start_eating(context.victual.piece);
		return 1;
	}

	/* nothing in progress - so try to find something. */
	/* tins are a special case */
	/* tins must also check conduct separately in case they're discarded */
	if (otmp->otyp == TIN) {
		start_tin(otmp);
		return 1;
	}

	/* KMH, conduct */
	u.uconduct.food++;

	context.victual.o_id = 0;
	context.victual.piece = otmp = touchfood(otmp);
	if (context.victual.piece) context.victual.o_id = context.victual.piece->o_id;
	context.victual.usedtime = 0;

	/* Now we need to calculate delay and nutritional info.
	 * The base nutrition calculated here and in eatcorpse() accounts
	 * for normal vs. rotten food.  The reqtime and nutrit values are
	 * then adjusted in accordance with the amount of food left.
	 */
	if (otmp->otyp == CORPSE) {
		int tmp = eatcorpse(otmp);
		if (tmp == 3) {
			/* inedible */
			context.victual.piece = NULL;
			/*
			 * The combination of odrained == true and oeaten == cnutrit
			 * represents the case of starting to drain a corpse but not
			 * getting any further (eg., loosing consciousness due to
			 * rotten food). We must preserve this case to avoid corpses
			 * changing appearance after a failed attempt to eat.
			 */
			if (!otmp->odrained &&
			    otmp->oeaten == mons[otmp->corpsenm].cnutrit)
				otmp->oeaten = 0;
			/* ALI, conduct: didn't eat it after all */
			u.uconduct.food--;
			return 0;
		} else if (tmp == 2) {
			/* used up */
			context.victual.piece = NULL;
			context.victual.o_id = 0;
			return 1;
		} else if (tmp)
			dont_start = true;
		/* if not used up, eatcorpse sets up reqtime and may modify
		 * oeaten */
	} else {
		/* No checks for WAX, LEATHER, BONE, DRAGON_HIDE.  These are
		 * all handled in the != FOOD_CLASS case, above */
		switch (objects[otmp->otyp].oc_material) {
			case FLESH:
				u.uconduct.unvegan++;
				if (otmp->otyp != EGG && otmp->otyp != CHEESE) {
					violated_vegetarian();
				}
				break;

			default:
				if (otmp->otyp == PANCAKE ||
				    otmp->otyp == FORTUNE_COOKIE || /* eggs */
				    otmp->otyp == CREAM_PIE ||
				    otmp->otyp == CANDY_BAR || /* milk */
				    otmp->otyp == LUMP_OF_ROYAL_JELLY)
					u.uconduct.unvegan++;
				break;
		}

		context.victual.reqtime = objects[otmp->otyp].oc_delay;
		if (otmp->otyp != FORTUNE_COOKIE &&
		    (otmp->cursed ||
		     (((monstermoves - otmp->age) > (int)(otmp->blessed ? 50 : 30)) &&
		      (otmp->orotten || !rn2(7))))) {
			if (rottenfood(otmp)) {
				otmp->orotten = true;
				dont_start = true;
			}
			if (otmp->oeaten < 2) {
				context.victual.piece = NULL;
				if (carried(otmp))
					useup(otmp);
				else
					useupf(otmp, 1L);
				return 1;
			} else
				consume_oeaten(otmp, 1); /* oeaten >>= 1 */
		} else
			fprefx(otmp);
	}

	/* re-calc the nutrition */
	if (otmp->otyp == CORPSE)
		basenutrit = mons[otmp->corpsenm].cnutrit;
	else
		basenutrit = objects[otmp->otyp].oc_nutrition;
	nutrit = otmp->oeaten;
	if (otmp->otyp == CORPSE && otmp->odrained) {
		nutrit -= drainlevel(otmp);
		basenutrit -= drainlevel(otmp);
	}

#ifdef DEBUG
	debugpline("before rounddiv: context.victual.reqtime == %d", context.victual.reqtime);
	debugpline("oeaten == %d, basenutrit == %d", otmp->oeaten, basenutrit);
	debugpline("nutrit == %d, cnutrit == %d", nutrit, otmp->otyp == CORPSE ? mons[otmp->corpsenm].cnutrit : objects[otmp->otyp].oc_nutrition);
#endif
	context.victual.reqtime = (basenutrit == 0 ? 0 :
					     rounddiv(context.victual.reqtime * (long)nutrit, basenutrit));
#ifdef DEBUG
	debugpline("after rounddiv: context.victual.reqtime == %d", context.victual.reqtime);
#endif
	/* calculate the modulo value (nutrit. units per round eating)
	 * [ALI] Note: although this is not exact, the remainder is
	 *       now dealt with in done_eating().
	 */
	if (context.victual.reqtime == 0 || nutrit == 0)
		/* possible if most has been eaten before */
		context.victual.nmod = 0;
	else if (nutrit >= context.victual.reqtime)
		context.victual.nmod = -(nutrit / context.victual.reqtime);
	else
		context.victual.nmod = context.victual.reqtime % nutrit;
	context.victual.canchoke = (u.uhs == SATIATED);

	if (!dont_start) start_eating(otmp);
	return 1;
}

/* Take a single bite from a piece of food, checking for choking and
 * modifying usedtime.  Returns 1 if they choked and survived, 0 otherwise.
 */
static int bite(void) {
	if (context.victual.canchoke && u.uhunger >= 2000) {
		choke(context.victual.piece);
		return 1;
	}
	if (context.victual.doreset) {
		do_reset_eat();
		return 0;
	}
	force_save_hs = true;
	if (context.victual.nmod < 0) {
		lesshungry(-context.victual.nmod);
		consume_oeaten(context.victual.piece, context.victual.nmod); /* -= -nmod */
	} else if (context.victual.nmod > 0 && (context.victual.usedtime % context.victual.nmod)) {
		lesshungry(1);
		consume_oeaten(context.victual.piece, -1); /* -= 1 */
	}
	force_save_hs = false;
	recalc_wt();
	return 0;
}

// as time goes by - called by moveloop() and domove()
void gethungry(void) {
	if (u.uinvulnerable) return; /* you don't feel hungrier */

	if ((!u.usleep || !rn2(10)) /* slow metabolic rate while asleep */
	    && (carnivorous(youmonst.data) || herbivorous(youmonst.data)) && !Slow_digestion)
		u.uhunger--; /* ordinary food consumption */

	if (moves % 2) { /* odd turns */
		/* Regeneration uses up food, unless due to an artifact */
		if (HRegeneration || ((ERegeneration & (~W_ART)) &&
				      (ERegeneration != W_WEP || !uwep->oartifact)))
			u.uhunger--;
		if (near_capacity() > SLT_ENCUMBER) u.uhunger--;
	} else { /* even turns */
		if (Hunger) u.uhunger--;
		/* Conflict uses up food too */
		if (HConflict || (EConflict & (~W_ARTI))) u.uhunger--;
		/* +0 charged rings don't do anything, so don't affect hunger */
		/* Slow digestion still uses ring hunger */
		switch ((int)(moves % 20)) { /* note: use even cases only */
			case 4:
				if (uleft &&
				    (uleft->spe || !objects[uleft->otyp].oc_charged))
					u.uhunger--;
				break;
			case 8:
				if (uamul) u.uhunger--;
				break;
			case 12:
				if (uright &&
				    (uright->spe || !objects[uright->otyp].oc_charged))
					u.uhunger--;
				break;
			case 16:
				if (u.uhave.amulet) u.uhunger--;
				break;
			default:
				break;
		}
	}
	newuhs(true);
}

// called after vomiting and after performing feats of magic
void morehungry(int num) {
	u.uhunger -= num;
	newuhs(true);
}

// called after eating (and after drinking fruit juice)
void lesshungry(int num) {
	/* See comments in newuhs() for discussion on force_save_hs */
	boolean iseating = occupation == eatfood || force_save_hs;
#ifdef DEBUG
	debugpline("lesshungry(%d)", num);
#endif
	u.uhunger += num;
	if (u.uhunger >= 2000) {
		if (!iseating || context.victual.canchoke) {
			if (iseating) {
				choke(context.victual.piece);
				reset_eat();
			} else
				choke(occupation == opentin ? context.tin.tin : NULL);
			/* no reset_eat() */
		}
	} else {
		/* Have lesshungry() report when you're nearly full so all eating
		 * warns when you're about to choke.
		 */
		if (u.uhunger >= 1500) {
			if (!context.victual.eating || (context.victual.eating && !context.victual.fullwarn)) {
				pline("You're having a hard time getting all of it down.");
				nomovemsg = "You're finally finished.";
				if (!context.victual.eating)
					multi = -2;
				else {
					context.victual.fullwarn = true;
					if (context.victual.canchoke && context.victual.reqtime > 1) {
						/* a one-gulp food will not survive a stop */
						if (paranoid_yn("Continue eating?") == 'n') {
							reset_eat();
							nomovemsg = NULL;
						}
					}
				}
			}
		}
	}
	newuhs(false);
}

static int unfaint(void) {
	Hear_again();
	if (u.uhs > FAINTING)
		u.uhs = FAINTING;
	stop_occupation();
	context.botl = 1;
	return 0;
}

bool is_fainted(void) {
	return u.uhs == FAINTED;
}

// call when a faint must be prematurely terminated
void reset_faint(void) {
	if (afternmv == unfaint) unmul("You revive.");
}

// compute and comment on your (new?) hunger status
void newuhs(boolean incr) {
	unsigned newhs;
	static unsigned save_hs;
	static boolean saved_hs = false;
	int h = u.uhunger;

	newhs = (h > 1000) ? SATIATED :
		(h > 150) ? NOT_HUNGRY :
		(h > 50) ? HUNGRY :
		(h > 0) ? WEAK : FAINTING;

	/* While you're eating, you may pass from WEAK to HUNGRY to NOT_HUNGRY.
	 * This should not produce the message "you only feel hungry now";
	 * that message should only appear if HUNGRY is an endpoint.  Therefore
	 * we check to see if we're in the middle of eating.  If so, we save
	 * the first hunger status, and at the end of eating we decide what
	 * message to print based on the _entire_ meal, not on each little bit.
	 */
	/* It is normally possible to check if you are in the middle of a meal
	 * by checking occupation == eatfood, but there is one special case:
	 * start_eating() can call bite() for your first bite before it
	 * sets the occupation.
	 * Anyone who wants to get that case to work _without_ an ugly static
	 * force_save_hs variable, feel free.
	 */
	/* Note: If you become a certain hunger status in the middle of the
	 * meal, and still have that same status at the end of the meal,
	 * this will incorrectly print the associated message at the end of
	 * the meal instead of the middle.  Such a case is currently
	 * impossible, but could become possible if a message for SATIATED
	 * were added or if HUNGRY and WEAK were separated by a big enough
	 * gap to fit two bites.
	 */
	if (occupation == eatfood || force_save_hs) {
		if (!saved_hs) {
			save_hs = u.uhs;
			saved_hs = true;
		}
		u.uhs = newhs;
		return;
	} else {
		if (saved_hs) {
			u.uhs = save_hs;
			saved_hs = false;
		}
	}

	if (newhs == FAINTING) {
		if (is_fainted()) newhs = FAINTED;
		if (u.uhs <= WEAK || rn2(20 - u.uhunger / 10) >= 19) {
			if (!is_fainted() && multi >= 0 /* %% */) {
				int duration = 10 - (u.uhunger / 10);

				/* stop what you're doing, then faint */
				stop_occupation();
				pline("You faint from lack of food.");
				incr_itimeout(&HDeaf, duration);
				nomul(-duration);
				nomovemsg = "You regain consciousness.";
				afternmv = unfaint;
				newhs = FAINTED;
			}
		} else if (u.uhunger < -(int)(200 + 20 * ACURR(A_CON))) {
			u.uhs = STARVED;
			context.botl = 1;
			bot();
			pline("You die from starvation.");
			killer.format = KILLED_BY;
			killer.name = nhsdupz("starvation");
			done(STARVING);
			/* if we return, we lifesaved, and that calls newuhs */
			return;
		}
	}

	if (newhs != u.uhs) {
		if (newhs >= WEAK && u.uhs < WEAK)
			losestr(1); /* this may kill you -- see below */
		else if (newhs < WEAK && u.uhs >= WEAK)
			losestr(-1);
		switch (newhs) {
			case HUNGRY:
				if (Hallucination) {
					pline(incr ?
						      "You are getting the munchies." :
						      "You now have a lesser case of the munchies.");
				} else {
					pline((!incr) ? "You only feel hungry now." :
							(u.uhunger < 145) ? "You feel hungry." :
									    "You are beginning to feel hungry.");
				}
				if (incr && occupation &&
				    (occupation != eatfood && occupation != opentin))
					stop_occupation();
				break;
			case WEAK:
				if (Hallucination)
					pline((!incr) ?
						      "You still have the munchies." :
						      "The munchies are interfering with your motor capabilities.");
				else if (incr &&
					 (Role_if(PM_WIZARD) || Race_if(PM_ELF) ||
					  Role_if(PM_VALKYRIE)))
					pline("%s needs food, badly!",
					      (Role_if(PM_WIZARD) || Role_if(PM_VALKYRIE)) ?
						      urole.name.m :
						      "Elf");
				else
					pline((!incr) ? "You feel weak now." :
							(u.uhunger < 45) ? "You feel weak." :
									   "You are beginning to feel weak.");
				if (incr && occupation &&
				    (occupation != eatfood && occupation != opentin))
					stop_occupation();
				break;
		}
		u.uhs = newhs;
		context.botl = 1;
		bot();
		if ((Upolyd ? u.mh : u.uhp) < 1) {
			pline("You die from hunger and exhaustion.");
			killer.format = KILLED_BY;
			killer.name = nhsdupz("exhaustion");
			done(STARVING);
			return;
		}
	}
}

/* Can you reach an object on the floor (to eat it, etc)? */
bool can_reach_floorobj(void) {
	// You need to be able to reach the floor in the first place; this covers
	// things like levitation, unskilled rider, etc.
	return can_reach_floor()
	       // If the floor is water or lava, you need to be immersed in it --
	       // not waterwalking or ceiling-clinging. If flying, you need to be
	       // unbreathing as well.
	       && !((is_pool(u.ux, u.uy) || is_lava(u.ux, u.uy))
	           && (Wwalking || is_clinger(youmonst.data) || (Flying && !Breathless)))
	       // And you can't be "teetering at the edge" of a pit, since then
	       // things on the floor are assumed to be at the bottom of the pit.
	       && !uteetering_at_seen_pit();
}

/* Returns an object representing food.  Object may be either on floor or
 * in inventory.
 */
// get food from floor or pack
static struct obj *floorfood(const char *verb) {
	struct obj *otmp;
	/* We cannot use ALL_CLASSES since that causes getobj() to skip its
	 * "ugly checks" and we need to check for inedible items.
	 */
	const char *edibles = (const char *)allobj;
	char qbuf[QBUFSZ];
	char c;

	if (u.usteed) /* can't eat off floor while riding */
		edibles++;
	else if (metallivorous(youmonst.data)) {
		struct trap *ttmp = t_at(u.ux, u.uy);

		if (ttmp && ttmp->tseen && ttmp->ttyp == BEAR_TRAP) {
			/* If not already stuck in the trap, perhaps there should
			   be a chance to becoming trapped?  Probably not, because
			   then the trap would just get eaten on the _next_ turn... */
			sprintf(qbuf, "There is a bear trap here (%s); eat it?",
				(u.utrap && u.utraptype == TT_BEARTRAP) ?
					"holding you" :
					"armed");
			if ((c = yn_function(qbuf, ynqchars, 'n')) == 'y') {
				u.utrap = u.utraptype = 0;
				deltrap(ttmp);
				return mksobj(BEARTRAP, true, false);
			} else if (c == 'q') {
				return NULL;
			}
		}
	}

	otmp = getobj(edibles, verb);
	if (otmp && otmp->oclass == COIN_CLASS)
		obj_extract_self(otmp);
	return otmp;
}

/* Side effects of vomiting */
/* added nomul (MRS) - it makes sense, you're too busy being sick! */
// A good idea from David Neves
void vomit(void) {
	make_sick(0L, NULL, true, SICK_VOMITABLE);
	nomul(-2);
	nomovemsg = 0;
}

int eaten_stat(int base, struct obj *obj) {
	long uneaten_amt, full_amount;

	uneaten_amt = (long)obj->oeaten;
	full_amount = (obj->otyp == CORPSE) ? (long)mons[obj->corpsenm].cnutrit : (long)objects[obj->otyp].oc_nutrition;
	if (uneaten_amt > full_amount) {
		impossible(
			"partly eaten food (%ld) more nutritious than untouched food (%ld)",
			uneaten_amt, full_amount);
		uneaten_amt = full_amount;
	}

	base = (int)(full_amount ? (long)base * uneaten_amt / full_amount : 0L);
	return (base < 1) ? 1 : base;
}

/* reduce obj's oeaten field, making sure it never hits or passes 0 */
void consume_oeaten(struct obj *obj, int amt) {
	/*
	 * This is a hack to try to squelch several long standing mystery
	 * food bugs.  A better solution would be to rewrite the entire
	 * victual handling mechanism from scratch using a less complex
	 * model.  Alternatively, this routine could call done_eating()
	 * or food_disappears() but its callers would need revisions to
	 * cope with context.victual.piece unexpectedly going away.
	 *
	 * Multi-turn eating operates by setting the food's oeaten field
	 * to its full nutritional value and then running a counter which
	 * independently keeps track of whether there is any food left.
	 * The oeaten field can reach exactly zero on the last turn, and
	 * the object isn't removed from inventory until the next turn
	 * when the "you finish eating" message gets delivered, so the
	 * food would be restored to the status of untouched during that
	 * interval.  This resulted in unexpected encumbrance messages
	 * at the end of a meal (if near enough to a threshold) and would
	 * yield full food if there was an interruption on the critical
	 * turn.  Also, there have been reports over the years of food
	 * becoming massively heavy or producing unlimited satiation;
	 * this would occur if reducing oeaten via subtraction attempted
	 * to drop it below 0 since its unsigned type would produce a
	 * huge positive value instead.  So far, no one has figured out
	 * _why_ that inappropriate subtraction might sometimes happen.
	 */

	if (amt > 0) {
		/* bit shift to divide the remaining amount of food */
		obj->oeaten >>= amt;
	} else {
		/* simple decrement; value is negative so we actually add it */
		if ((int)obj->oeaten > -amt)
			obj->oeaten += amt;
		else
			obj->oeaten = 0;
	}

	if (obj->oeaten == 0) {
		if (obj == context.victual.piece)		    /* always true unless wishing... */
			context.victual.reqtime = context.victual.usedtime; /* no bites left */
		obj->oeaten = 1;			    /* smallest possible positive value */
	}
}

/* called when eatfood occupation has been interrupted,
   or in the case of theft, is about to be interrupted */
boolean maybe_finished_meal(boolean stopping) {
	/* in case consume_oeaten() has decided that the food is all gone */
	if (occupation == eatfood && context.victual.usedtime >= context.victual.reqtime) {
		if (stopping) occupation = 0; /* for do_reset_eat */
		eatfood();		      /* calls done_eating() to use up context.victual.piece */
		return true;
	}
	return false;
}

/* Tin of <something> to the rescue?  Decide whether current occupation
   is an attempt to eat a tin of something capable of saving hero's life.
   We don't care about consumption of non-tinned food here because special
   effects there take place on first bite rather than at end of occupation.
   [Popeye the Sailor gets out of trouble by eating tins of spinach. :-] */
bool Popeye(int threat) {
	struct obj *otin;
	int mndx;

	if (occupation != opentin) return false;
	otin = context.tin.tin;
	/* make sure hero still has access to tin */
	if (!carried(otin) && !obj_here(otin, u.ux, u.uy)) return false;
	/* unknown tin is assumed to be helpful */
	if (!otin->known) return true;
	/* known tin is helpful if it will stop life-threatening problem */
	mndx = otin->corpsenm;
	switch (threat) {
		/* note: not used; hunger code bypasses stop_occupation() when eating */
		case HUNGER:
			return (mndx != NON_PM || otin->spe == 1);
			/* flesh from lizards and acidic critters stops petrification */
		case STONED:
			return (mndx >= LOW_PM && (mndx == PM_LIZARD || acidic(&mons[mndx])));
			/* no tins can cure these (yet?) */
		case SLIMED:
		case SICK:
		case VOMITING:
			break;
		default:
			break;
	}
	return false;
}

/*eat.c*/
