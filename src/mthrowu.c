/*	SCCS Id: @(#)mthrowu.c	3.4	2003/05/09	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

static int drop_throw(struct monst *, struct obj *, boolean, int, int);

#define URETREATING(x, y) (distmin(u.ux, u.uy, x, y) > distmin(u.ux0, u.uy0, x, y))

#define POLE_LIM 5 /* How far monsters can use pole-weapons */

/*
 * Keep consistent with breath weapons in zap.c, and AD_* in monattk.h.
 */
const char *breathwep[] = {
	"fragments",
	"fire",
	"frost",
	"sleep gas",
	"a disintegration blast",
	"lightning",
	"poison gas",
	"acid",
	"strange breath #8",
	"strange breath #9"};

/* hero is hit by something other than a monster */
/* if name is null, then format `obj' */
int thitu(int tlev, int dam, struct obj *obj, const char *name) {
	const char *onm, *knm;
	boolean is_acid;
	int kprefix = KILLED_BY_AN;
	char onmbuf[BUFSZ], knmbuf[BUFSZ];

	if (!name) {
		if (!obj) panic("thitu: name & obj both null?");
		name = strcpy(onmbuf,
			      (obj->quan > 1L) ? doname(obj) : mshot_xname(obj));
		knm = strcpy(knmbuf, killer_xname(obj));
		kprefix = KILLED_BY; /* killer_name supplies "an" if warranted */
	} else {
		knm = name;
		/* [perhaps ought to check for plural here to] */
		if (!strncmpi(name, "the ", 4) ||
		    !strncmpi(name, "an ", 3) ||
		    !strncmpi(name, "a ", 2)) kprefix = KILLED_BY;
	}
	onm = (obj && obj_is_pname(obj)) ? the(name) :
					   (obj && obj->quan > 1L) ? name : an(name);
	is_acid = (obj && obj->otyp == ACID_VENOM);

	if (u.uac + tlev <= rnd(20)) {
		if (Blind || !flags.verbose)
			pline("It misses.");
		else
			pline("You are almost hit by %s.", onm);
		return 0;
	} else {
		if (Blind || !flags.verbose)
			pline("You are hit!");
		else
			pline("You are hit by %s%s", onm, exclam(dam));

		if (obj && objects[obj->otyp].oc_material == SILVER && mon_hates_silver(&youmonst)) {
			dam += rnd(20);
			pline("The silver sears your flesh!");
			exercise(A_CON, false);
		}
		if (is_acid && Acid_resistance)
			pline("It doesn't seem to hurt you.");
		else {
			if (is_acid) pline("It burns!");
			losehp(dam, knm, kprefix);
			exercise(A_STR, false);
		}
		return 1;
	}
}

/* Be sure this corresponds with what happens to player-thrown objects in
 * dothrow.c (for consistency). --KAA
 * Returns 0 if object still exists (not destroyed).
 */

static int drop_throw(struct monst *mon, struct obj *obj, boolean ohit, int x, int y) {
	struct obj *mwep = NULL;

	int retvalu = 1;
	int create;
	struct monst *mtmp;
	struct trap *t;

	if (mon) mwep = MON_WEP(mon);

	if (obj->otyp == CREAM_PIE || obj->oclass == VENOM_CLASS ||
	    /* WAC added Spoon throw code */
	    (obj->oartifact == ART_HOUCHOU) ||
	    /* WAC -- assume monsters don't throw without
	                	using the right propellor */
	    (is_bullet(obj)) ||
	    (ohit && obj->otyp == EGG))
		create = 0;
	else if (ohit && (is_multigen(obj) || obj->otyp == ROCK))
		create = !rn2(3);
	else
		create = 1;

	/* Detonate rockets */
	if (is_grenade(obj)) {
		if (!ohit) {
			create = 1; /* Don't destroy */
			arm_bomb(obj, false);
		} else {
			grenade_explode(obj, bhitpos.x, bhitpos.y, false, 0);
			obj = NULL;
		}
	} else if (objects[obj->otyp].oc_dir & EXPLOSION) {
		if (cansee(bhitpos.x, bhitpos.y))
			pline("%s explodes in a ball of fire!", Doname2(obj));
		else
			You_hear("an explosion");
		explode(bhitpos.x, bhitpos.y, -ZT_SPELL(ZT_FIRE), d(3, 8),
			WEAPON_CLASS, EXPL_FIERY);
	}

	/* D: Detonate crossbow bolts from Hellfire if they hit */
	if (ohit && mwep && mwep->oartifact == ART_HELLFIRE && is_ammo(obj) && ammo_and_launcher(obj, mwep)) {
		if (cansee(bhitpos.x, bhitpos.y))
			pline("%s explodes in a ball of fire!", Doname2(obj));
		else
			You_hear("an explosion");

		explode(bhitpos.x, bhitpos.y, -ZT_SPELL(ZT_FIRE),
			d(2, 6), WEAPON_CLASS, EXPL_FIERY);

		/* D: Exploding bolts will be destroyed */
		create = 0;
	}

	if (create && !((mtmp = m_at(x, y)) && (mtmp->mtrapped) &&
			(t = t_at(x, y)) && is_pitlike(t->ttyp))) {
		int objgone = 0;

		if (down_gate(x, y) != -1)
			objgone = ship_object(obj, x, y, false);
		if (!objgone) {
			if (!flooreffects(obj, x, y, "fall")) { /* don't double-dip on damage */
				place_object(obj, x, y);
				if (!mtmp && x == u.ux && y == u.uy)
					mtmp = &youmonst;
				if (mtmp && ohit)
					passive_obj(mtmp, obj, NULL);
				stackobj(obj);
				retvalu = 0;
			}
		}
	} else if (obj)
		obfree(obj, NULL);

	return retvalu;
}

/* an object launched by someone/thing other than player attacks a monster;
   return 1 if the object has stopped moving (hit or its range used up) */

/* mon => monster thrower (if applicable) */
/* mtmp => accidental target */
/* otml => missile, which might be destroyed by drop_throw */
/* range =>	how much farther will object travel if it misses */
/*		Use -1 to signify to keep going even after hit, */
/*		unless its gone (used for rolling_boulder_traps) */

/* verbose => give message(s) even when you can't see what happened */
int ohitmon(struct monst *mon, struct monst *mtmp, struct obj *otmp, int range, boolean verbose) {
	int damage, tmp;
	boolean vis, ismimic;
	int objgone = 1;

	ismimic = mtmp->m_ap_type && mtmp->m_ap_type != M_AP_MONSTER;
	vis = cansee(bhitpos.x, bhitpos.y);

	tmp = 5 + find_mac(mtmp) + omon_adj(mtmp, otmp, false);
	if (tmp < rnd(20)) {
		if (!ismimic) {
			if (vis)
				miss(distant_name(otmp, mshot_xname), mtmp);
			else if (verbose)
				pline("It is missed.");
		}
		if (!range) { /* Last position; object drops */
			drop_throw(mon, otmp, 0, mtmp->mx, mtmp->my);
			return 1;
		}
	} else if (otmp->oclass == POTION_CLASS) {
		if (ismimic) seemimic(mtmp);
		mtmp->msleeping = 0;
		if (vis) otmp->dknown = 1;
		potionhit(mtmp, otmp, false);
		if (mon && !DEADMONSTER(mon) && !DEADMONSTER(mtmp) &&
		    mtmp->movement >= NORMAL_SPEED && rn2(4)) {
			/* retaliate */
			mtmp->movement -= NORMAL_SPEED;
			mattackm(mtmp, mon);
		}
		return 1;
	} else {
		damage = dmgval(otmp, mtmp);
		if (otmp->otyp == SPOON) {
			pline("The spoon flashes brightly as it hits %s.",
			      the(mon_nam(mtmp)));
		}

		if (otmp->otyp == ACID_VENOM && resists_acid(mtmp))
			damage = 0;
		if (ismimic) seemimic(mtmp);
		mtmp->msleeping = 0;
		if (vis)
			hit(distant_name(otmp, mshot_xname), mtmp, exclam(damage));
		else if (verbose)
			pline("%s is hit%s", Monnam(mtmp), exclam(damage));

		if (otmp->opoisoned && is_poisonable(otmp)) {
			if (resists_poison(mtmp)) {
				if (vis) pline("The poison doesn't seem to affect %s.",
					       mon_nam(mtmp));
			} else {
				if (rn2(30)) {
					damage += rnd(6);
				} else {
					if (vis) pline("The poison was deadly...");
					damage = mtmp->mhp;
				}
			}
		}
		if (objects[otmp->otyp].oc_material == SILVER &&
		    mon_hates_silver(mtmp)) {
			if (vis)
				pline("The silver sears %s flesh!",
				      s_suffix(mon_nam(mtmp)));
			else if (verbose)
				pline("Its flesh is seared!");
		}
		if (otmp->otyp == ACID_VENOM && cansee(mtmp->mx, mtmp->my)) {
			if (resists_acid(mtmp)) {
				if (vis || verbose)
					pline("%s is unaffected.", Monnam(mtmp));
				damage = 0;
			} else {
				if (vis)
					pline("The acid burns %s!", mon_nam(mtmp));
				else if (verbose)
					pline("It is burned!");
			}
		}
		mtmp->mhp -= damage;
		if (mtmp->mhp < 1) {
			if (vis || verbose)
				pline("%s is %s!", Monnam(mtmp),
				      (nonliving(mtmp->data) || is_vampshifter(mtmp) || !canspotmon(mtmp)) ? "destroyed" : "killed");
			/* don't blame hero for unknown rolling boulder trap */
			if (!context.mon_moving &&
			    (otmp->otyp != BOULDER || range >= 0 || otmp->otrapped))
				xkilled(mtmp, 0);
			else
				mondied(mtmp);
		}

		if (can_blnd(NULL, mtmp,
			     (uchar)(otmp->otyp == BLINDING_VENOM ? AT_SPIT : AT_WEAP),
			     otmp)) {
			if (vis && mtmp->mcansee)
				pline("%s is blinded by %s.", Monnam(mtmp), the(xname(otmp)));
			mtmp->mcansee = 0;
			tmp = (int)mtmp->mblinded + rnd(25) + 20;
			if (tmp > 127) tmp = 127;
			mtmp->mblinded = tmp;
		}

		if (mon && !DEADMONSTER(mon) && !DEADMONSTER(mtmp) &&
		    mtmp->movement >= NORMAL_SPEED && rn2(4)) {
			/* retaliate */
			mtmp->movement -= NORMAL_SPEED;
			mattackm(mtmp, mon);
		}

		objgone = drop_throw(mon, otmp, 1, bhitpos.x, bhitpos.y);
		if (!objgone && range == -1) {	/* special case */
			obj_extract_self(otmp); /* free it for motion again */
			return 0;
		}
		return 1;
	}
	return 0;
}

void m_throw(
	struct monst *mon,
	int x,
	int y,
	int dx,
	int dy,
	int range, /* direction and range */
	struct obj *obj) {
	struct monst *mtmp;
	struct obj *singleobj, *mwep;
	char sym = obj->oclass;
	int hitu, blindinc = 0;

	bhitpos.x = x;
	bhitpos.y = y;

	if (obj->quan == 1L) {
		/*
		 * Remove object from minvent.  This cannot be done later on;
		 * what if the player dies before then, leaving the monster
		 * with 0 daggers?  (This caused the infamous 2^32-1 orcish
		 * dagger bug).
		 *
		 * VENOM is not in minvent - it should already be OBJ_FREE.
		 * The extract below does nothing.
		 */

		/* not possibly_unwield, which checks the object's */
		/* location, not its existence */
		if (MON_WEP(mon) == obj) {
			setmnotwielded(mon, obj);
			MON_NOWEP(mon);
		}
		obj_extract_self(obj);
		singleobj = obj;
		obj = NULL;
	} else {
		singleobj = splitobj(obj, 1L);
		obj_extract_self(singleobj);
	}

	singleobj->owornmask = 0; /* threw one of multiple weapons in hand? */

	if (mon)
		mwep = MON_WEP(mon);
	else
		mwep = NULL;

	/* D: Special launcher effects */
	if (mwep && is_ammo(singleobj) && ammo_and_launcher(singleobj, mwep)) {
		if (mwep->oartifact == ART_PLAGUE && is_poisonable(singleobj))
			singleobj->opoisoned = 1;

		/* D: Hellfire is handled in drop_throw */
	}

	if ((singleobj->cursed || singleobj->greased) && (dx || dy) && !rn2(7)) {
		if (canseemon(mon) && flags.verbose) {
			if (is_ammo(singleobj))
				pline("%s misfires!", Monnam(mon));
			else
				pline("%s as %s throws it!",
				      Tobjnam(singleobj, "slip"), mon_nam(mon));
		}
		dx = rn2(3) - 1;
		dy = rn2(3) - 1;
		/* check validity of new direction */
		if (!dx && !dy) {
			drop_throw(mon, singleobj, 0, bhitpos.x, bhitpos.y);
			return;
		}
	}

	/* pre-check for doors, walls and boundaries.
	  Also need to pre-check for bars regardless of direction;
	  the random chance for small objects hitting bars is
	  skipped when reaching them at point blank range */
	if (!isok(bhitpos.x + dx, bhitpos.y + dy) || IS_ROCK(levl[bhitpos.x + dx][bhitpos.y + dy].typ) || closed_door(bhitpos.x + dx, bhitpos.y + dy) || (levl[bhitpos.x + dx][bhitpos.y + dy].typ == IRONBARS && hits_bars(&singleobj, bhitpos.x, bhitpos.y, 0, 0))) {
		drop_throw(mon, singleobj, 0, bhitpos.x, bhitpos.y);
		return;
	}

	/* Note: drop_throw may destroy singleobj.  Since obj must be destroyed
	 * early to avoid the dagger bug, anyone who modifies this code should
	 * be careful not to use either one after it's been freed.
	 */
	if (sym) tmp_at(DISP_FLASH, obj_to_glyph(singleobj));
	while (range-- > 0) { /* Actually the loop is always exited by break */
		bhitpos.x += dx;
		bhitpos.y += dy;
		if ((mtmp = m_at(bhitpos.x, bhitpos.y)) != 0) {
			if (ohitmon(mon, mtmp, singleobj, range, true))
				break;
		} else if (bhitpos.x == u.ux && bhitpos.y == u.uy) {
			if (multi) nomul(0);

			if (singleobj->oclass == GEM_CLASS &&
			    singleobj->otyp <= LAST_GEM + 9 /* 9 glass colors */
			    && is_unicorn(youmonst.data)) {
				if (singleobj->otyp > LAST_GEM) {
					pline("You catch the %s.", xname(singleobj));
					pline("You are not interested in %s junk.",
					      s_suffix(mon_nam(mon)));
					makeknown(singleobj->otyp);
					dropy(singleobj);
				} else {
					pline("You accept %s gift in the spirit in which it was intended.",
					      s_suffix(mon_nam(mon)));
					hold_another_object(singleobj,
							    "You catch, but drop, %s.", xname(singleobj),
							    "You catch:");
				}
				break;
			}
			if (singleobj->oclass == POTION_CLASS) {
				if (!Blind) singleobj->dknown = 1;
				potionhit(&youmonst, singleobj, false);
				break;
			}
			switch (singleobj->otyp) {
				int dam, hitv;
				case EGG:
					if (!touch_petrifies(&mons[singleobj->corpsenm])) {
						impossible("monster throwing egg type %d",
							   singleobj->corpsenm);
						hitu = 0;
						break;
					}
				fallthru;
				case CREAM_PIE:
				case BLINDING_VENOM:
					hitu = thitu(8, 0, singleobj, NULL);
					break;
				default:
					dam = dmgval(singleobj, &youmonst);
					hitv = 3 - distmin(u.ux, u.uy, mon->mx, mon->my);
					if (hitv < -4) hitv = -4;
					if (is_elf(mon->data) &&
					    objects[singleobj->otyp].oc_skill == P_BOW) {
						hitv++;
						if (MON_WEP(mon) &&
						    MON_WEP(mon)->otyp == ELVEN_BOW)
							hitv++;
						if (singleobj->otyp == ELVEN_ARROW) dam++;
					}
					if (bigmonst(youmonst.data)) hitv++;
					hitv += 8 + singleobj->spe;
					if (dam < 1) dam = 1;
					hitu = thitu(hitv, dam, singleobj, NULL);
			}
			if (hitu && singleobj->opoisoned &&
			    is_poisonable(singleobj)) {
				char onmbuf[BUFSZ], knmbuf[BUFSZ];

				strcpy(onmbuf, xname(singleobj));
				strcpy(knmbuf, killer_xname(singleobj));
				poisoned(onmbuf, A_STR, knmbuf, -10);
			}
			if (hitu &&
			    can_blnd(NULL, &youmonst,
				     (uchar)(singleobj->otyp == BLINDING_VENOM ?
						     AT_SPIT :
						     AT_WEAP),
				     singleobj)) {
				blindinc = rnd(25);
				if (singleobj->otyp == CREAM_PIE) {
					if (!Blind)
						pline("Yecch!  You've been creamed.");
					else
						pline("There's something sticky all over your %s.",
						      body_part(FACE));
				} else if (singleobj->otyp == BLINDING_VENOM) {
					int num_eyes = eyecount(youmonst.data);
					/* venom in the eyes */
					if (!Blind)
						pline("The venom blinds you.");
					else
						pline("Your %s sting%s.",
						      (num_eyes == 1) ? body_part(EYE) :
									makeplural(body_part(EYE)),
						      (num_eyes == 1) ? "s" : "");
				}
			}
			if (hitu && singleobj->otyp == EGG) {
				if (!Stoned && !Stone_resistance && !(poly_when_stoned(youmonst.data) &&
							   polymon(PM_STONE_GOLEM))) {
					make_stoned(5, NULL, KILLED_BY, new_nhs());
				}
			}
			stop_occupation();
			if (hitu || !range) {
				drop_throw(mon, singleobj, hitu, u.ux, u.uy);
				break;
			}
		}
		if (!range /* reached end of path */
		    /* missile hits edge of screen */
		    || !isok(bhitpos.x + dx, bhitpos.y + dy)
		    /* missile hits the wall */
		    || IS_ROCK(levl[bhitpos.x + dx][bhitpos.y + dy].typ)
		    /* missile hit closed door */
		    || closed_door(bhitpos.x + dx, bhitpos.y + dy)
		    /* missile might hit bars */
		    || (levl[bhitpos.x + dx][bhitpos.y + dy].typ == IRONBARS &&
			hits_bars(&singleobj, bhitpos.x, bhitpos.y, !rn2(5), 0))
		    /* Thrown objects "sink" */
		    || IS_SINK(levl[bhitpos.x][bhitpos.y].typ)) {
			if (singleobj) /* hits_bars might have destroyed it */
				drop_throw(mon, singleobj, 0, bhitpos.x, bhitpos.y);
			break;
		}
		tmp_at(bhitpos.x, bhitpos.y);
		delay_output();
	}
	tmp_at(bhitpos.x, bhitpos.y);
	delay_output();
	tmp_at(DISP_END, 0);

	if (blindinc) {
		u.ucreamed += blindinc;
		make_blinded(Blinded + (long)blindinc, false);
		if (!Blind) pline("Your vision quickly clears.");
	}
}

/* Remove an item from the monster's inventory and destroy it. */
void m_useup(struct monst *mon, struct obj *obj) {
	if (obj->quan > 1L) {
		obj->quan--;
		obj->owt = weight(obj);
	} else {
		obj_extract_self(obj);
		possibly_unwield(mon, false);
		if (obj->owornmask) {
			mon->misc_worn_check &= ~(obj->owornmask);
			update_mon_intrinsics(mon, obj, false, false);
		}
		obfree(obj, NULL);
	}
}

/* monster attempts ranged weapon attack against player */
void thrwmu(struct monst *mtmp) {
	struct obj *otmp, *mwep;
	xchar x, y;
	schar skill;
	int multishot;
	const char *onm;
	int chance;

	/* Rearranged beginning so monsters can use polearms not in a line */
	if (mtmp->weapon_check == NEED_WEAPON || !MON_WEP(mtmp)) {
		mtmp->weapon_check = NEED_RANGED_WEAPON;
		/* mon_wield_item resets weapon_check as appropriate */
		if (mon_wield_item(mtmp) != 0) return;
	}

	/* Pick a weapon */
	otmp = select_rwep(mtmp);
	if (!otmp) return;

	if ((MON_WEP(mtmp) == otmp) && is_pole(otmp)) {
		int dam, hitv;

		if (dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) > POLE_LIM ||
		    !couldsee(mtmp->mx, mtmp->my))
			return; /* Out of range, or intervening wall */

		if (canseemon(mtmp)) {
			onm = xname(otmp);
			pline("%s thrusts %s.", Monnam(mtmp),
			      obj_is_pname(otmp) ? the(onm) : an(onm));
		}

		dam = dmgval(otmp, &youmonst);
		hitv = 3 - distmin(u.ux, u.uy, mtmp->mx, mtmp->my);
		if (hitv < -4) hitv = -4;
		if (bigmonst(youmonst.data)) hitv++;
		hitv += 8 + otmp->spe;
		if (dam < 1) dam = 1;

		thitu(hitv, dam, otmp, NULL);
		stop_occupation();
		return;
	}

	x = mtmp->mx;
	y = mtmp->my;
	/* If you are coming toward the monster, the monster
	 * should try to soften you up with missiles.  If you are
	 * going away, you are probably hurt or running.  Give
	 * chase, but if you are getting too far away, throw.
	 */
	/* WAC Catch this since rn2(0) is illegal */
	chance = (BOLT_LIM - distmin(x, y, mtmp->mux, mtmp->muy) > 0) ?
			 BOLT_LIM - distmin(x, y, mtmp->mux, mtmp->muy) :
			 1;
	if (!lined_up(mtmp) || (URETREATING(x, y) && rn2(chance)))
		return;

	skill = objects[otmp->otyp].oc_skill;
	mwep = MON_WEP(mtmp); /* wielded weapon */

	if (ammo_and_launcher(otmp, mwep) && objects[mwep->otyp].oc_range &&
	    dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) >
		    objects[mwep->otyp].oc_range * objects[mwep->otyp].oc_range)
		return; /* Out of range */

	/* Multishot calculations */
	multishot = 1;
	if ((ammo_and_launcher(otmp, mwep) || skill == P_DAGGER ||
	     skill == -P_DART || skill == -P_SHURIKEN) &&
	    !mtmp->mconf) {
		/* Assumes lords are skilled, princes are expert */
		if (is_prince(mtmp->data))
			multishot += 2;
		else if (is_lord(mtmp->data))
			multishot++;

		/*  Elven Craftsmanship makes for light,  quick bows */
		if (otmp->otyp == ELVEN_ARROW && !otmp->cursed)
			multishot++;
		if (mwep && mwep->otyp == ELVEN_BOW && !mwep->cursed) multishot++;
		/* 1/3 of object enchantment */
		if (mwep && mwep->spe > 1)
			multishot += rounddiv(mwep->spe, 3);
		/* Some randomness */
		if (multishot > 1)
			multishot = rnd(multishot);
		if (mwep && objects[mwep->otyp].oc_rof && is_launcher(mwep))
			multishot += objects[mwep->otyp].oc_rof;

		switch (monsndx(mtmp->data)) {
			case PM_RANGER:
				multishot++;
				break;
			case PM_ROGUE:
				if (skill == P_DAGGER) multishot++;
				break;
			case PM_NINJA:
			case PM_SAMURAI:
				if (otmp->otyp == YA && mwep &&
				    mwep->otyp == YUMI) multishot++;
				break;
			default:
				break;
		}
		/* racial bonus */
		if ((is_elf(mtmp->data) &&
		     otmp->otyp == ELVEN_ARROW &&
		     mwep && mwep->otyp == ELVEN_BOW) ||
		    (is_orc(mtmp->data) &&
		     otmp->otyp == ORCISH_ARROW &&
		     mwep && mwep->otyp == ORCISH_BOW))
			multishot++;

		if ((long)multishot > otmp->quan) multishot = (int)otmp->quan;
		if (multishot < 1) multishot = 1;
		/* else multishot = rnd(multishot); */
	}

	if (canseemon(mtmp)) {
		char onmbuf[BUFSZ];

		if (multishot > 1) {
			/* "N arrows"; multishot > 1 implies otmp->quan > 1, so
			   xname()'s result will already be pluralized */
			sprintf(onmbuf, "%d %s", multishot, xname(otmp));
			onm = onmbuf;
		} else {
			/* "an arrow" */
			onm = singular(otmp, xname);
			onm = obj_is_pname(otmp) ? the(onm) : an(onm);
		}
		m_shot.s = ammo_and_launcher(otmp, mwep) ? true : false;
		pline("%s %s %s!", Monnam(mtmp),
		      m_shot.s ? is_bullet(otmp) ? "fires" : "shoots" : "throws",
		      onm);
		m_shot.o = otmp->otyp;
	} else {
		m_shot.o = STRANGE_OBJECT; /* don't give multishot feedback */
	}

	m_shot.n = multishot;
	for (m_shot.i = 1; m_shot.i <= m_shot.n; m_shot.i++)
		m_throw(mtmp, mtmp->mx, mtmp->my, sgn(tbx), sgn(tby),
			distmin(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy), otmp);
	m_shot.n = m_shot.i = 0;
	m_shot.o = STRANGE_OBJECT;
	m_shot.s = false;

	nomul(0);
}

/* monster spits substance at you */
int spitmu(struct monst *mtmp, struct attack *mattk) {
	struct obj *otmp;

	if (mtmp->mcan) {
		if (!Deaf)
			pline("A dry rattle comes from %s throat.",
			      s_suffix(mon_nam(mtmp)));
		return 0;
	}
	if (lined_up(mtmp)) {
		switch (mattk->adtyp) {
			case AD_BLND:
			case AD_DRST:
				otmp = mksobj(BLINDING_VENOM, true, false);
				break;
			default:
				impossible("bad attack type in spitmu");
			fallthru;
			case AD_ACID:
				otmp = mksobj(ACID_VENOM, true, false);
				break;
		}
		if (!rn2(BOLT_LIM - distmin(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy))) {
			if (canseemon(mtmp))
				pline("%s spits venom!", Monnam(mtmp));
			m_throw(mtmp, mtmp->mx, mtmp->my, sgn(tbx), sgn(tby),
				distmin(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy), otmp);
			nomul(0);
			return 0;
		}
	}
	return 0;
}

/* monster breathes at you (ranged) */
int breamu(struct monst *mtmp, struct attack *mattk) {
	/* if new breath types are added, change AD_ACID to max type */
	int typ = (mattk->adtyp == AD_RBRE) ? rnd(AD_ACID) : mattk->adtyp;

	if (lined_up(mtmp)) {
		if (mtmp->mcan) {
			if (!Deaf) {
				if (canseemon(mtmp))
					pline("%s coughs.", Monnam(mtmp));
				else
					You_hear("a cough.");
			}
			return 0;
		}
		if (!mtmp->mspec_used && rn2(3)) {
			if ((typ >= AD_MAGM) && (typ <= AD_ACID)) {
				if (canseemon(mtmp))
					pline("%s breathes %s!", Monnam(mtmp),
					      breathwep[typ - 1]);
				buzz((int)(-20 - (typ - 1)), (int)mattk->damn,
				     mtmp->mx, mtmp->my, sgn(tbx), sgn(tby));
				nomul(0);
				/* breath runs out sometimes. Also, give monster some
				 * cunning; don't breath if the player fell asleep.
				 */
				if (!rn2(3))
					mtmp->mspec_used = 10 + rn2(20);
				if (typ == AD_SLEE && !Sleep_resistance)
					mtmp->mspec_used += rnd(20);
			} else
				impossible("Breath weapon %d used", typ - 1);
		}
	}
	return 1;
}

/* WAC for doorbusting ONLY (at this point in time) No checks */
boolean breamspot(struct monst *mtmp, struct attack *mattk, xchar ax, xchar ay) {
	/* if new breath types are added, change AD_ACID to max type */
	int typ = (mattk->adtyp == AD_RBRE) ? rnd(AD_ACID) : mattk->adtyp;

	if ((typ >= AD_MAGM) && (typ <= AD_ACID)) {
		if (canseemon(mtmp))
			pline("%s breathes %s!", Monnam(mtmp),
			      breathwep[typ - 1]);
		/* Do the door first - monster is ON TOP so call direct */
		zap_over_floor(mtmp->mx, mtmp->my, (int)(-20 - (typ - 1)), NULL);
		buzz((int)(-20 - (typ - 1)), (int)mattk->damn,
		     mtmp->mx, mtmp->my, ax, ay);
		nomul(0);
		/* breath runs out sometimes. */
		if (!rn2(3))
			mtmp->mspec_used = 10 + rn2(20);
	} else
		impossible("Breath weapon %d used", typ - 1);
	return true;
}

boolean linedup(xchar ax, xchar ay, xchar bx, xchar by) {
	tbx = ax - bx; /* These two values are set for use */
	tby = ay - by; /* after successful return.	    */

	/* sometimes displacement makes a monster think that you're at its
	   own location; prevent it from throwing and zapping in that case */
	if (!tbx && !tby) return false;

	if ((!tbx || !tby || abs(tbx) == abs(tby)) /* straight line or diagonal */
	    && distmin(tbx, tby, 0, 0) < BOLT_LIM) {
		if (ax == u.ux && ay == u.uy)
			return couldsee(bx, by);
		else if (clear_path(ax, ay, bx, by))
			return true;
	}
	return false;
}

/* is mtmp in position to use ranged attack? */
boolean lined_up(struct monst *mtmp) {
	return linedup(mtmp->mux, mtmp->muy, mtmp->mx, mtmp->my);
}

/* Check if a monster is carrying a particular item.
 */
struct obj *m_carrying(struct monst *mtmp, int type) {
	struct obj *otmp;

	for (otmp = mtmp->minvent; otmp; otmp = otmp->nobj)
		if (otmp->otyp == type)
			return otmp;
	return NULL;
}

/* true iff thrown/kicked/rolled object doesn't pass through iron bars */

/* *obj_p will be set to NULL if object breaks */
/* whodidit: 1==hero, 0=other, -1==just check whether it'll pass thru */

boolean hits_bars(struct obj **obj_p, int x, int y, int always_hit, int whodidit) {
	struct obj *otmp = *obj_p;
	int obj_type = otmp->otyp;
	boolean hits = always_hit;

	if (!hits)
		switch (otmp->oclass) {
			case WEAPON_CLASS: {
				int oskill = objects[obj_type].oc_skill;

				hits = (oskill != -P_BOW && oskill != -P_CROSSBOW &&
					oskill != -P_DART && oskill != -P_SHURIKEN &&
					(oskill != -P_FIREARM || obj_type == ROCKET) &&
					oskill != P_SPEAR && oskill != P_JAVELIN &&
					oskill != P_KNIFE); /* but not dagger */
				break;
			}
			case ARMOR_CLASS:
				hits = (objects[obj_type].oc_armcat != ARM_GLOVES);
				break;
			case TOOL_CLASS:
				hits = (obj_type != SKELETON_KEY &&
					obj_type != LOCK_PICK &&
					obj_type != CREDIT_CARD &&
					obj_type != TALLOW_CANDLE &&
					obj_type != WAX_CANDLE &&
					obj_type != LENSES &&
					obj_type != TIN_WHISTLE &&
					obj_type != MAGIC_WHISTLE);
				break;
			case ROCK_CLASS: /* includes boulder */
				if (obj_type != STATUE ||
				    mons[otmp->corpsenm].msize > MZ_TINY) hits = true;
				break;
			case FOOD_CLASS:
				if (obj_type == CORPSE &&
				    mons[otmp->corpsenm].msize > MZ_TINY)
					hits = true;
				else
					hits = (obj_type == MEAT_STICK ||
						obj_type == HUGE_CHUNK_OF_MEAT);
				break;
			case SPBOOK_CLASS:
			case WAND_CLASS:
			case BALL_CLASS:
			case CHAIN_CLASS:
				hits = true;
				break;
			default:
				break;
		}

	if (hits && whodidit != -1) {
		if (whodidit ? hero_breaks(otmp, x, y, false) : breaks(otmp, x, y))
			*obj_p = otmp = 0; /* object is now gone */
		/* breakage makes its own noises */
		else if (obj_type == BOULDER || obj_type == HEAVY_IRON_BALL)
			pline("Whang!");
		else if (otmp->oclass == COIN_CLASS ||
			 objects[obj_type].oc_material == GOLD ||
			 objects[obj_type].oc_material == SILVER)
			pline("Clink!");
		else
			pline("Clonk!");
	}

	return hits;
}

/*mthrowu.c*/
