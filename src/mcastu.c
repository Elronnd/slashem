/*	SCCS Id: @(#)mcastu.c	3.4	2003/01/08	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

/* monster mage spells */
#define MGC_PSI_BOLT	0
#define MGC_CURE_SELF	1
#define MGC_HASTE_SELF	2
#define MGC_STUN_YOU	3
#define MGC_DISAPPEAR	4
#define MGC_WEAKEN_YOU	5
#define MGC_DESTRY_ARMR 6
#define MGC_CURSE_ITEMS 7
#define MGC_AGGRAVATION 8
#define MGC_SUMMON_MONS 9
#define MGC_CLONE_WIZ	10
#define MGC_DEATH_TOUCH 11
#define MGC_CREATE_POOL 12
#define MGC_CALL_UNDEAD 13

/* monster cleric spells */
#define CLC_OPEN_WOUNDS 0
#define CLC_CURE_SELF	1
#define CLC_CONFUSE_YOU 2
#define CLC_PARALYZE	3
#define CLC_BLIND_YOU	4
#define CLC_INSECTS	5
#define CLC_CURSE_ITEMS 6
#define CLC_LIGHTNING	7
#define CLC_FIRE_PILLAR 8
#define CLC_GEYSER	9

static void cursetxt(struct monst *, boolean);
static int choose_magic_spell(int);
static int choose_clerical_spell(int);
static void cast_wizard_spell(struct monst *, int, int);
static void cast_cleric_spell(struct monst *, int, int);
static boolean is_undirected_spell(uint, int);
static boolean spell_would_be_useless(struct monst *, uint, int);

extern const char *const flash_types[]; /* from zap.c */

/* feedback when frustrated monster couldn't cast a spell */
static void cursetxt(struct monst *mtmp, boolean undirected) {
	if (canseemon(mtmp) && couldsee(mtmp->mx, mtmp->my)) {
		const char *point_msg; /* spellcasting monsters are impolite */

		if (undirected)
			point_msg = "all around, then curses";
		else if ((Invis && !perceives(mtmp->data) &&
			  (mtmp->mux != u.ux || mtmp->muy != u.uy)) ||
			 (youmonst.m_ap_type == M_AP_OBJECT &&
			  youmonst.mappearance == STRANGE_OBJECT) ||
			 u.uundetected)
			point_msg = "and curses in your general direction";
		else if (Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy))
			point_msg = "and curses at your displaced image";
		else
			point_msg = "at you, then curses";

		pline("%s points %s.", Monnam(mtmp), point_msg);
	} else if ((!(moves % 4) || !rn2(4))) {
		if (!Deaf) Norep("You hear a mumbled curse.");
	}
}

/* convert a level based random selection into a specific mage spell;
   inappropriate choices will be screened out by spell_would_be_useless() */
static int choose_magic_spell(int spellval) {
	switch (spellval) {
		case 22:
		case 21:
		case 20:
			return MGC_DEATH_TOUCH;
		case 19:
		case 18:
			return MGC_CLONE_WIZ;
		case 17:
		case 16:
		case 15:
			return MGC_SUMMON_MONS;
		case 14:
		case 13:
			return MGC_AGGRAVATION;
		case 12:
			return MGC_CREATE_POOL;
		case 11:
		case 10:
			return MGC_CURSE_ITEMS;
		case 9:
			return MGC_CALL_UNDEAD;
		case 8:
			return MGC_DESTRY_ARMR;
		case 7:
		case 6:
			return MGC_WEAKEN_YOU;
		case 5:
		case 4:
			return MGC_DISAPPEAR;
		case 3:
			return MGC_STUN_YOU;
		case 2:
			return MGC_HASTE_SELF;
		case 1:
			return MGC_CURE_SELF;
		case 0:
		default:
			return MGC_PSI_BOLT;
	}
}

/* convert a level based random selection into a specific cleric spell */
static int choose_clerical_spell(int spellnum) {
	switch (spellnum) {
		case 13:
			return CLC_GEYSER;
		case 12:
			return CLC_FIRE_PILLAR;
		case 11:
			return CLC_LIGHTNING;
		case 10:
		case 9:
			return CLC_CURSE_ITEMS;
		case 8:
			return CLC_INSECTS;
		case 7:
		case 6:
			return CLC_BLIND_YOU;
		case 5:
		case 4:
			return CLC_PARALYZE;
		case 3:
		case 2:
			return CLC_CONFUSE_YOU;
		case 1:
			return CLC_CURE_SELF;
		case 0:
		default:
			return CLC_OPEN_WOUNDS;
	}
}

/* return values:
 * 1: successful spell
 * 0: unsuccessful spell
 */
int castmu(struct monst *mtmp, struct attack *mattk, boolean thinks_it_foundyou, boolean foundyou) {
	int dmg, ml = mtmp->m_lev;
	int ret;
	int spellnum = 0;
	int spellev, chance, difficulty, splcaster, learning;

	/* Three cases:
	 * -- monster is attacking you.  Search for a useful spell.
	 * -- monster thinks it's attacking you.  Search for a useful spell,
	 *    without checking for undirected.  If the spell found is directed,
	 *    it fails with cursetxt() and loss of mspec_used.
	 * -- monster isn't trying to attack.  Select a spell once.  Don't keep
	 *    searching; if that spell is not useful (or if it's directed),
	 *    return and do something else.
	 * Since most spells are directed, this means that a monster that isn't
	 * attacking casts spells only a small portion of the time that an
	 * attacking monster does.
	 */
	if ((mattk->adtyp == AD_SPEL || mattk->adtyp == AD_CLRC) && ml) {
		int cnt = 40;

		do {
			spellnum = rn2(ml);
			/* Casting level is limited by available energy */
			spellev = spellnum / 7 + 1;
			if (spellev > 10) spellev = 10;
			if (spellev * 5 > mtmp->m_en) {
				spellev = mtmp->m_en / 5;
				spellnum = (spellev - 1) * 7 + 1;
			}
			if (mattk->adtyp == AD_SPEL)
				spellnum = choose_magic_spell(spellnum);
			else
				spellnum = choose_clerical_spell(spellnum);
			/* not trying to attack?  don't allow directed spells */
			if (!thinks_it_foundyou) {
				if (!is_undirected_spell(mattk->adtyp, spellnum) ||
				    spell_would_be_useless(mtmp, mattk->adtyp, spellnum)) {
					if (foundyou)
						impossible("spellcasting monster found you and doesn't know it?");
					return 0;
				}
				break;
			}
		} while (--cnt > 0 &&
			 spell_would_be_useless(mtmp, mattk->adtyp, spellnum));
		if (cnt == 0) return 0;
	} else {
		/* Casting level is limited by available energy */
		spellev = ml / 7 + 1;
		if (spellev > 10) spellev = 10;
		if (spellev * 5 > mtmp->m_en) {
			spellev = mtmp->m_en / 5;
			ml = (spellev - 1) * 7 + 1;
		}
	}

	/* monster unable to cast spells? */
	if (mtmp->mcan || mtmp->m_en < 5 || mtmp->mspec_used || !ml) {
		cursetxt(mtmp, is_undirected_spell(mattk->adtyp, spellnum));
		return 0;
	}

	if (mattk->adtyp == AD_SPEL || mattk->adtyp == AD_CLRC) {
		mtmp->mspec_used = rn2(15) - mtmp->m_lev;
		if (mattk->adtyp == AD_SPEL)
			mtmp->mspec_used = mtmp->mspec_used > 0 ? 2 : 0;
		else if (mtmp->mspec_used < 2)
			mtmp->mspec_used = 2;
	}

	/* monster can cast spells, but is casting a directed spell at the
	   wrong place?  If so, give a message, and return.  Do this *after*
	   penalizing mspec_used. */
	if (!foundyou && thinks_it_foundyou &&
	    !is_undirected_spell(mattk->adtyp, spellnum)) {
		pline("%s casts a spell at %s!",
		      canseemon(mtmp) ? Monnam(mtmp) : "Something",
		      levl[mtmp->mux][mtmp->muy].typ == WATER ? "empty water" : "thin air");
		return 0;
	}

	nomul(0);

	mtmp->m_en -= spellev * 5; /* Use up the energy now */

	/* We should probably do similar checks to what is done for
	 * the player - armor, etc.
	 * Checks for armour and other intrinsic ability change splcaster
	 * Difficulty and experience affect chance
	 * Assume that monsters only cast spells that they know well
	 */
	splcaster = 15 - (mtmp->m_lev / 2); /* Base for a wizard is 5...*/

	if (splcaster < 5) splcaster = 5;
	if (splcaster > 20) splcaster = 20;

	chance = 11 * (mtmp->m_lev > 25 ? 18 : (12 + (mtmp->m_lev / 5)));
	chance++; /* Minimum chance of 1 */

	difficulty = (spellev - 1) * 4 - (mtmp->m_lev - 1);
	/* law of diminishing returns sets in quickly for
	 * low-level spells.  That is, higher levels quickly
	 * result in almost no gain
	 */
	learning = 15 * (-difficulty / spellev);
	chance += learning > 20 ? 20 : learning;

	/* clamp the chance */
	if (chance < 0) chance = 0;
	if (chance > 120) chance = 120;

	/* combine */
	chance = chance * (20 - splcaster) / 15 - splcaster;

	/* Clamp to percentile */
	if (chance > 100) chance = 100;
	if (chance < 0) chance = 0;

	if (mtmp->mconf || rnd(100) > chance) { /* fumbled attack */
		if (canseemon(mtmp) && !Deaf)
			pline("The air crackles around %s.", mon_nam(mtmp));
		return 0;
	}

	if (canspotmon(mtmp) || !is_undirected_spell(mattk->adtyp, spellnum)) {
		pline("%s casts a spell%s!",
				canspotmon(mtmp) ? Monnam(mtmp) : "Something",
				is_undirected_spell(mattk->adtyp, spellnum) ? "" :
				(Invisible && !perceives(mtmp->data) &&
				 (mtmp->mux != u.ux || mtmp->muy != u.uy)) ?
				" at a spot near you" :
				(Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy)) ?
				" at your displaced image" :
				" at you");
	}

	/*
	 *	As these are spells, the damage is related to the level
	 *	of the monster casting the spell.
	 */
	if (!foundyou) {
		dmg = 0;
		if (mattk->adtyp != AD_SPEL && mattk->adtyp != AD_CLRC) {
			impossible(
					"%s casting non-hand-to-hand version of hand-to-hand spell %d?",
					Monnam(mtmp), mattk->adtyp);
			return 0;
		}
	} else if (mattk->damd) {
		dmg = d((int)((ml / 2) + mattk->damn), (int)mattk->damd);
	} else {
		dmg = d((int)((ml / 2) + 1), 6);
	}
	if (Half_spell_damage) dmg = (dmg + 1) / 2;

	ret = 1;

	switch (mattk->adtyp) {
		case AD_FIRE:
			pline("You're enveloped in flames.");
			if (Fire_resistance) {
				shieldeff(u.ux, u.uy);
				pline("But you resist the effects.");
				dmg = 0;
			}
			if (Slimed) {
				pline("The slime is burned away!");
				Slimed = 0;
			}
			burn_away_slime();
			break;
		case AD_COLD:
			pline("You're covered in frost.");
			if (Cold_resistance) {
				shieldeff(u.ux, u.uy);
				pline("But you resist the effects.");
				dmg = 0;
			}
			break;
		case AD_MAGM:
			pline("You are hit by a shower of missiles!");
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				pline("The missiles bounce off!");
				dmg = 0;
			}
			break;
		case AD_SPEL:	/* wizard spell */
		case AD_CLRC: { /* clerical spell */
				      if (mattk->adtyp == AD_SPEL)
					      cast_wizard_spell(mtmp, dmg, spellnum);
				      else
					      cast_cleric_spell(mtmp, dmg, spellnum);
				      dmg = 0; /* done by the spell casting functions */
				      break;
			      }
	}
	if (dmg) mdamageu(mtmp, dmg);
	return ret;
}

/* monster wizard and cleric spellcasting functions
 *
 * If dmg is zero, then the monster is not casting at you.
 * If the monster is intentionally not casting at you, we have previously
 * called spell_would_be_useless() and spellnum should always be a valid
 * undirected spell.
 * If you modify either of these, be sure to change is_undirected_spell()
 * and spell_would_be_useless().
 */
static void cast_wizard_spell(struct monst *mtmp, int dmg, int spellnum) {
	if (dmg == 0 && !is_undirected_spell(AD_SPEL, spellnum)) {
		impossible("cast directed wizard spell (%d) with dmg=0?", spellnum);
		return;
	}

	switch (spellnum) {
		case MGC_DEATH_TOUCH:
			pline("Oh no, %s's using the touch of death!", mhe(mtmp));
			if (nonliving(youmonst.data) || is_demon(youmonst.data)) {
				pline("You seem no deader than before.");
			} else if (!Antimagic && rn2(mtmp->m_lev) > 12) {
				if (Hallucination) {
					pline("You have an out of body experience.");
				} else {
					killer.format = KILLED_BY_AN;
					killer.name = nhsdupz("touch of death");
					done(DIED);
				}
			} else {
				if (Antimagic) shieldeff(u.ux, u.uy);
				pline("Lucky for you, it didn't work!");
			}
			dmg = 0;
			break;
		case MGC_CREATE_POOL:
			if (levl[u.ux][u.uy].typ == ROOM || levl[u.ux][u.uy].typ == CORR) {
				pline("A pool appears beneath you!");
				levl[u.ux][u.uy].typ = POOL;
				del_engr_at(u.ux, u.uy);
				water_damage(level.objects[u.ux][u.uy], false, true);
				spoteffects(false); /* possibly drown, notice objects */
			} else
				impossible("bad pool creation?");
			dmg = 0;
			break;
		case MGC_CLONE_WIZ:
			if (mtmp->iswiz && context.no_of_wizards == 1) {
				pline("Double Trouble...");
				clonewiz();
				dmg = 0;
			} else
				impossible("bad wizard cloning?");
			break;
		case MGC_SUMMON_MONS: {
			int count;

			count = nasty(mtmp); /* summon something nasty */
			if (mtmp->iswiz)
				verbalize("Destroy the thief, my pet%s!", plur(count));
			else {
				const char *mappear =
					(count == 1) ? "A monster appears" : "Monsters appear";

				/* messages not quite right if plural monsters created but
			   only a single monster is seen */
				if (Invisible && !perceives(mtmp->data) &&
				    (mtmp->mux != u.ux || mtmp->muy != u.uy))
					pline("%s around a spot near you!", mappear);
				else if (Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy))
					pline("%s around your displaced image!", mappear);
				else
					pline("%s from nowhere!", mappear);
			}
			dmg = 0;
			break;
		}
		case MGC_CALL_UNDEAD: {
			coord mm;
			mm.x = u.ux;
			mm.y = u.uy;
			pline("Undead creatures are called forth from the grave!");
			mkundead(&mm, false, NO_MINVENT);
		}
			dmg = 0;
			break;
		case MGC_AGGRAVATION:
			pline("You feel that monsters are aware of your presence.");
			aggravate();
			dmg = 0;
			break;
		case MGC_CURSE_ITEMS:
			pline("You feel as if you need some help.");
			rndcurse();
			dmg = 0;
			break;
		case MGC_DESTRY_ARMR:
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				pline("A field of force surrounds you!");
			} else if (!destroy_arm(some_armor(&youmonst))) {
				pline("Your skin itches.");
			}
			dmg = 0;
			break;
		case MGC_WEAKEN_YOU: /* drain strength */
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				pline("You feel momentarily weakened.");
			} else {
				pline("You suddenly feel weaker!");
				dmg = mtmp->m_lev - 6;
				if (Half_spell_damage) dmg = (dmg + 1) / 2;
				losestr(rnd(dmg));
				if (u.uhp < 1)
					done_in_by(mtmp);
			}
			dmg = 0;
			break;
		case MGC_DISAPPEAR: /* makes self invisible */
			if (!mtmp->minvis && !mtmp->invis_blkd) {
				if (canseemon(mtmp))
					pline("%s suddenly %s!", Monnam(mtmp),
					      !See_invisible ? "disappears" : "becomes transparent");
				mon_set_minvis(mtmp);
				dmg = 0;
			} else
				impossible("no reason for monster to cast disappear spell?");
			break;
		case MGC_STUN_YOU:
			if (Antimagic || Free_action) {
				shieldeff(u.ux, u.uy);
				if (!Stunned)
					pline("You feel momentarily disoriented.");
				make_stunned(1L, false);
			} else {
				pline(Stunned ? "You struggle to keep your balance." : "You reel...");
				dmg = d(ACURR(A_DEX) < 12 ? 6 : 4, 4);
				if (Half_spell_damage) dmg = (dmg + 1) / 2;
				make_stunned(HStun + dmg, false);
			}
			dmg = 0;
			break;
		case MGC_HASTE_SELF:
			mon_adjust_speed(mtmp, 1, NULL);
			dmg = 0;
			break;
		case MGC_CURE_SELF:
			if (mtmp->mhp < mtmp->mhpmax) {
				if (canseemon(mtmp))
					pline("%s looks better.", Monnam(mtmp));
				/* note: player healing does 6d4; this used to do 1d8 */
				if ((mtmp->mhp += d(3, 6)) > mtmp->mhpmax)
					mtmp->mhp = mtmp->mhpmax;
				dmg = 0;
			}
			break;
		case MGC_PSI_BOLT:
			/* prior to 3.4.0 Antimagic was setting the damage to 1--this
		   made the spell virtually harmless to players with magic res. */
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				dmg = (dmg + 1) / 2;
			}
			if (dmg <= 5)
				pline("You get a slight %sache.", body_part(HEAD));
			else if (dmg <= 10)
				pline("Your brain is on fire!");
			else if (dmg <= 20)
				pline("Your %s suddenly aches painfully!", body_part(HEAD));
			else
				pline("Your %s suddenly aches very painfully!", body_part(HEAD));
			break;
		default:
			impossible("mcastu: invalid magic spell (%d)", spellnum);
			dmg = 0;
			break;
	}

	if (dmg) mdamageu(mtmp, dmg);
}

static void cast_cleric_spell(struct monst *mtmp, int dmg, int spellnum) {
	if (dmg == 0 && !is_undirected_spell(AD_CLRC, spellnum)) {
		impossible("cast directed cleric spell (%d) with dmg=0?", spellnum);
		return;
	}

	switch (spellnum) {
		case CLC_GEYSER:
			/* this is physical damage, not magical damage */
			pline("A sudden geyser slams into you from nowhere!");
			dmg = d(8, 6);
			if (Half_physical_damage) dmg = (dmg + 1) / 2;
			break;
		case CLC_FIRE_PILLAR:
			pline("A pillar of fire strikes all around you!");
			if (Fire_resistance) {
				shieldeff(u.ux, u.uy);
				dmg = 0;
			} else
				dmg = d(8, 6);
			if (Half_spell_damage) dmg = (dmg + 1) / 2;
			burn_away_slime();
			burnarmor(&youmonst);
			destroy_item(SCROLL_CLASS, AD_FIRE);
			destroy_item(POTION_CLASS, AD_FIRE);
			destroy_item(SPBOOK_CLASS, AD_FIRE);
			burn_floor_paper(u.ux, u.uy, true, false);
			break;
		case CLC_LIGHTNING: {
			boolean reflects;

			/* WAC add lightning strike effect */
			zap_strike_fx(u.ux, u.uy, AD_ELEC - 1);
			pline("A bolt of lightning strikes down at you from above!");
			reflects = ureflects("It bounces off your %s%s.", "");
			if (reflects || Shock_resistance) {
				shieldeff(u.ux, u.uy);
				dmg = 0;
				if (reflects)
					break;
			} else
				dmg = d(8, 6);
			if (Half_spell_damage) dmg = (dmg + 1) / 2;
			destroy_item(WAND_CLASS, AD_ELEC);
			destroy_item(RING_CLASS, AD_ELEC);
			flashburn(Half_spell_damage ? 10 : 20);
			break;
		}
		case CLC_CURSE_ITEMS:
			pline("You feel as if you need some help.");
			rndcurse();
			dmg = 0;
			break;
		case CLC_INSECTS: {
			/* Try for insects, and if there are none
		   left, go for (sticks to) snakes.  -3. */
			struct permonst *pm = mkclass(S_ANT, 0);
			struct monst *mtmp2 = NULL;
			char let = (pm ? S_ANT : S_SNAKE);
			boolean success;
			int i;
			coord bypos;
			int quan;

			quan = (mtmp->m_lev < 2) ? 1 : rnd((int)mtmp->m_lev / 2);
			if (quan < 3) quan = 3;
			success = pm ? true : false;
			for (i = 0; i <= quan; i++) {
				if (!enexto(&bypos, mtmp->mux, mtmp->muy, mtmp->data))
					break;
				if ((pm = mkclass(let, 0)) != 0 &&
				    (mtmp2 = makemon(pm, bypos.x, bypos.y, NO_MM_FLAGS)) != 0) {
					success = true;
					mtmp2->msleeping = mtmp2->mpeaceful = false;
					mtmp2->mtame = 0;
					set_malign(mtmp2);
				}
			}
			/* Not quite right:
		 * -- message doesn't always make sense for unseen caster (particularly
		 *    the first message)
		 * -- message assumes plural monsters summoned (non-plural should be
		 *    very rare, unlike in nasty())
		 * -- message assumes plural monsters seen
		 */
			if (!success)
				pline("%s casts at a clump of sticks, but nothing happens.",
				      Monnam(mtmp));
			else if (let == S_SNAKE)
				pline("%s transforms a clump of sticks into snakes!",
				      Monnam(mtmp));
			else if (Invisible && !perceives(mtmp->data) &&
				 (mtmp->mux != u.ux || mtmp->muy != u.uy))
				pline("%s summons insects around a spot near you!",
				      Monnam(mtmp));
			else if (Displaced && (mtmp->mux != u.ux || mtmp->muy != u.uy))
				pline("%s summons insects around your displaced image!",
				      Monnam(mtmp));
			else
				pline("%s summons insects!", Monnam(mtmp));
			dmg = 0;
			break;
		}
		case CLC_BLIND_YOU:
			/* note: resists_blnd() doesn't apply here */
			if (!Blinded) {
				int num_eyes = eyecount(youmonst.data);
				pline("Scales cover your %s!",
				      (num_eyes == 1) ?
					      body_part(EYE) :
					      makeplural(body_part(EYE)));
				make_blinded(Half_spell_damage ? 100L : 200L, false);
				if (!Blind) pline("Your %s", "vision quickly clears.");
				dmg = 0;
			} else
				impossible("no reason for monster to cast blindness spell?");
			break;
		case CLC_PARALYZE:
			if (Antimagic || Free_action) {
				shieldeff(u.ux, u.uy);
				if (multi >= 0)
					pline("You stiffen briefly.");
				nomul(-1);
			} else {
				if (multi >= 0)
					pline("You are frozen in place!");
				dmg = 4 + (int)mtmp->m_lev;
				if (Half_spell_damage) dmg = (dmg + 1) / 2;
				nomul(-dmg);
			}
			nomovemsg = 0;
			dmg = 0;
			break;
		case CLC_CONFUSE_YOU:
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				pline("You feel momentarily dizzy.");
			} else {
				boolean oldprop = !!Confusion;

				dmg = (int)mtmp->m_lev;
				if (Half_spell_damage) dmg = (dmg + 1) / 2;
				make_confused(HConfusion + dmg, true);
				if (Hallucination)
					pline("You feel %s!", oldprop ? "trippier" : "trippy");
				else
					pline("You feel %sconfused!", oldprop ? "more " : "");
			}
			dmg = 0;
			break;
		case CLC_CURE_SELF:
			if (mtmp->mhp < mtmp->mhpmax) {
				if (canseemon(mtmp))
					pline("%s looks better.", Monnam(mtmp));
				/* note: player healing does 6d4; this used to do 1d8 */
				if ((mtmp->mhp += d(3, 6)) > mtmp->mhpmax)
					mtmp->mhp = mtmp->mhpmax;
				dmg = 0;
			}
			break;
		case CLC_OPEN_WOUNDS:
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				dmg = (dmg + 1) / 2;
			}
			if (dmg <= 5)
				pline("Your skin itches badly for a moment.");
			else if (dmg <= 10)
				pline("Wounds appear on your body!");
			else if (dmg <= 20)
				pline("Severe wounds appear on your body!");
			else
				pline("Your body is covered with painful wounds!");
			break;
		default:
			impossible("mcastu: invalid clerical spell (%d)", spellnum);
			dmg = 0;
			break;
	}

	if (dmg) mdamageu(mtmp, dmg);
}

static boolean is_undirected_spell(uint adtyp, int spellnum) {
	if (adtyp == AD_SPEL) {
		switch (spellnum) {
			case MGC_CLONE_WIZ:
			case MGC_SUMMON_MONS:
			case MGC_AGGRAVATION:
			case MGC_DISAPPEAR:
			case MGC_HASTE_SELF:
			case MGC_CURE_SELF:
			case MGC_CALL_UNDEAD:
				return true;
			default:
				break;
		}
	} else if (adtyp == AD_CLRC) {
		switch (spellnum) {
			case CLC_INSECTS:
			case CLC_CURE_SELF:
				return true;
			default:
				break;
		}
	}
	return false;
}

// Some spells are useless under some circumstances.
static boolean spell_would_be_useless(struct monst *mtmp, uint adtyp, int spellnum) {
	/* Some spells don't require the player to really be there and can be cast
	 * by the monster when you're invisible, yet still shouldn't be cast when
	 * the monster doesn't even think you're there.
	 * This check isn't quite right because it always uses your real position.
	 * We really want something like "if the monster could see mux, muy".
	 */
	boolean mcouldseeu = couldsee(mtmp->mx, mtmp->my);

	if (adtyp == AD_SPEL) {
		/* aggravate monsters, etc. won't be cast by peaceful monsters */
		if (mtmp->mpeaceful && (spellnum == MGC_AGGRAVATION ||
					spellnum == MGC_SUMMON_MONS || spellnum == MGC_CLONE_WIZ ||
					spellnum == MGC_CALL_UNDEAD))
			return true;
		/* haste self when already fast */
		if (mtmp->permspeed == MFAST && spellnum == MGC_HASTE_SELF)
			return true;
		/* invisibility when already invisible */
		if ((mtmp->minvis || mtmp->invis_blkd) && spellnum == MGC_DISAPPEAR)
			return true;
		/* peaceful monster won't cast invisibility if you can't see invisible,
		   same as when monsters drink potions of invisibility.  This doesn't
		   really make a lot of sense, but lets the player avoid hitting
		   peaceful monsters by mistake */
		if (mtmp->mpeaceful && !See_invisible && spellnum == MGC_DISAPPEAR)
			return true;
		/* healing when already healed */
		if (mtmp->mhp == mtmp->mhpmax && spellnum == MGC_CURE_SELF)
			return true;
		/* don't summon monsters if it doesn't think you're around */
		if (!mcouldseeu && (spellnum == MGC_SUMMON_MONS ||
				    spellnum == MGC_CALL_UNDEAD ||
				    (!mtmp->iswiz && spellnum == MGC_CLONE_WIZ)))
			return true;
		/* only lichs can cast call undead */
		if (mtmp->data->mlet != S_LICH && spellnum == MGC_CALL_UNDEAD)
			return true;
		/* pools can only be created in certain locations and then only
		 * rarely unless you're carrying the amulet.
		 */
		if (((levl[u.ux][u.uy].typ != ROOM && levl[u.ux][u.uy].typ != CORR) || (!u.uhave.amulet && rn2(10))) && spellnum == MGC_CREATE_POOL)
			return true;
		if ((!mtmp->iswiz || context.no_of_wizards > 1) && spellnum == MGC_CLONE_WIZ)
			return true;

		// aggravation (global wakeup) when everyone is already active
		if (spellnum == MGC_AGGRAVATION) {
			struct monst *nxtmon;

			for (nxtmon = fmon; nxtmon; nxtmon = nxtmon->nmon) {
				if (DEADMONSTER(nxtmon)) continue;
				if ((nxtmon->mstrategy & STRAT_WAITFORU) != 0 ||
						nxtmon->msleeping || !nxtmon->mcanmove) break;
			}
			/* if nothing needs to be awakened then this spell is useless
			   but caster might not realize that [chance to pick it then
			   must be very small otherwise caller's many retry attempts
			   will eventually end up picking it too often] */
			if (!nxtmon) return rn2(100) ? true : false;
		}
	} else if (adtyp == AD_CLRC) {
		/* summon insects/sticks to snakes won't be cast by peaceful monsters */
		if (mtmp->mpeaceful && spellnum == CLC_INSECTS)
			return true;
		/* healing when already healed */
		if (mtmp->mhp == mtmp->mhpmax && spellnum == CLC_CURE_SELF)
			return true;
		/* don't summon insects if it doesn't think you're around */
		if (!mcouldseeu && spellnum == CLC_INSECTS)
			return true;
		/* blindness spell on blinded player */
		if (Blinded && spellnum == CLC_BLIND_YOU)
			return true;
	}
	return false;
}

/* convert 1..10 to 0..9; add 10 for second group (spell casting) */
#define ad_to_typ(k) (10 + k - 1)

// monster uses spell (ranged)
bool buzzmu(struct monst *mtmp, struct attack *mattk) {
	/* don't print constant stream of curse messages for 'normal'
	   spellcasting monsters at range */
	if (mattk->adtyp > AD_SPC2)
		return false;

	if (mtmp->mcan) {
		cursetxt(mtmp, false);
		return false;
	}
	if (lined_up(mtmp) && rn2(3)) {
		nomul(0);
		if (mattk->adtyp && (mattk->adtyp < 11)) { /* no cf unsigned >0 */
			if (canseemon(mtmp))
				pline("%s zaps you with a %s!", Monnam(mtmp),
				      flash_types[ad_to_typ(mattk->adtyp)]);
			buzz(-ad_to_typ(mattk->adtyp), (int)mattk->damn,
			     mtmp->mx, mtmp->my, sgn(tbx), sgn(tby));
		} else
			impossible("Monster spell %d cast", mattk->adtyp - 1);
	}
	return true;
}

/*mcastu.c*/
