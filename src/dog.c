/*	SCCS Id: @(#)dog.c	3.4	2002/09/08	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

static int pet_type(void);

void newedog(struct monst *mtmp) {
	if (!mtmp->mextra) mtmp->mextra = newmextra();
	if (!EDOG(mtmp)) {
		EDOG(mtmp) = new(struct edog);
	}
}

void free_edog(struct monst *mtmp) {
	if (mtmp->mextra && EDOG(mtmp)) {
		free(EDOG(mtmp));
		EDOG(mtmp) = NULL;
	}

	mtmp->mtame = 0;
}

void initedog(struct monst *mtmp) {
	mtmp->mtame = is_domestic(mtmp->data) ? 10 : 5;
	mtmp->mpeaceful = 1;
	mtmp->mavenge = 0;
	set_malign(mtmp); /* recalc alignment now that it's tamed */
	mtmp->mleashed = 0;
	mtmp->meating = 0;
	EDOG(mtmp)->droptime = 0;
	EDOG(mtmp)->dropdist = 10000;
	EDOG(mtmp)->apport = 10;
	EDOG(mtmp)->whistletime = 0;
	EDOG(mtmp)->hungrytime = 1000 + monstermoves;
	EDOG(mtmp)->ogoal.x = -1; /* force error if used before set */
	EDOG(mtmp)->ogoal.y = -1;
	EDOG(mtmp)->abuse = 0;
	EDOG(mtmp)->revivals = 0;
	EDOG(mtmp)->mhpmax_penalty = 0;
	EDOG(mtmp)->killed_by_u = 0;
}

static int pet_type(void) {
	if (urole.petnum != NON_PM)
		return urole.petnum;
	else if (preferred_pet == 'c')
		return PM_KITTEN;
	else if (preferred_pet == 'd')
		return PM_LITTLE_DOG;
	else
		return rn2(2) ? PM_KITTEN : PM_LITTLE_DOG;
}

struct monst *make_familiar(struct obj *otmp, xchar x, xchar y, boolean quietly) {
	struct permonst *pm;
	struct monst *mtmp = 0;
	int chance, trycnt = 100;

	do {
		if (otmp) { /* figurine; otherwise spell */
			int mndx = otmp->corpsenm;
			pm = &mons[mndx];
			/* activating a figurine provides one way to exceed the
			   maximum number of the target critter created--unless
			   it has a special limit (erinys, Nazgul) */
			if ((mvitals[mndx].mvflags & G_EXTINCT) &&
			    mbirth_limit(mndx) != MAXMONNO) {
				if (!quietly)
					/* have just been given "You <do something with>
					   the figurine and it transforms." message */
					pline("... into a pile of dust.");
				break; /* mtmp is null */
			}
		} else if (!rn2(3)) {
			pm = &mons[pet_type()];
		} else {
			pm = rndmonst();
			if (!pm) {
				if (!quietly)
					pline("There seems to be nothing available for a familiar.");
				break;
			}
		}

		mtmp = makemon(pm, x, y, MM_EDOG | MM_IGNOREWATER);
		if (otmp && !mtmp) { /* monster was genocided or square occupied */
			if (!quietly)
				pline("The figurine writhes and then shatters into pieces!");
			break;
		}
	} while (!mtmp && --trycnt > 0);

	if (!mtmp) return NULL;

	if (is_pool(mtmp->mx, mtmp->my) && minliquid(mtmp))
		return NULL;

	initedog(mtmp);
	mtmp->msleeping = 0;
	if (otmp) {		  /* figurine; resulting monster might not become a pet */
		chance = rn2(10); /* 0==tame, 1==peaceful, 2==hostile */
		if (chance > 2) chance = otmp->blessed ? 0 : !otmp->cursed ? 1 : 2;
		/* 0,1,2:  b=80%,10,10; nc=10%,80,10; c=10%,10,80 */
		if (chance > 0) {
			mtmp->mtame = 0;   /* not tame after all */
			if (chance == 2) { /* hostile (cursed figurine) */
				if (!quietly)
					pline("You get a bad feeling about this.");
				mtmp->mpeaceful = 0;
				set_malign(mtmp);
			}
		}
		/* if figurine has been named, give same name to the monster */
		if (otmp->onamelth)
			mtmp = christen_monst(mtmp, ONAME(otmp));
	}
	set_malign(mtmp); /* more alignment changes */
	newsym(mtmp->mx, mtmp->my);

	/* must wield weapon immediately since pets will otherwise drop it */
	if (mtmp->mtame && attacktype(mtmp->data, AT_WEAP)) {
		mtmp->weapon_check = NEED_HTH_WEAPON;
		mon_wield_item(mtmp);
	}
	return mtmp;
}

struct monst *make_helper(int mnum, xchar x, xchar y) {
	struct permonst *pm;
	struct monst *mtmp = 0;
	int trycnt = 100;

	do {
		pm = &mons[mnum];

		mtmp = makemon(pm, x, y, MM_EDOG);
	} while (!mtmp && --trycnt > 0);

	if (!mtmp) return NULL; /* genocided */

	initedog(mtmp);
	mtmp->msleeping = 0;
	set_malign(mtmp); /* more alignment changes */
	newsym(mtmp->mx, mtmp->my);

	/* must wield weapon immediately since pets will otherwise drop it */
	if (mtmp->mtame && attacktype(mtmp->data, AT_WEAP)) {
		mtmp->weapon_check = NEED_HTH_WEAPON;
		mon_wield_item(mtmp);
	}
	return mtmp;
}

struct monst *makedog(void) {
	struct monst *mtmp;
	struct obj *otmp;
	const char *petname;
	int pettype, petsym;
	static int petname_used = 0;

	if (preferred_pet == 'n') return NULL;

	pettype = pet_type();
	petsym = mons[pettype].mlet;
	if (pettype == PM_WINTER_WOLF_CUB)
		petname = wolfname;
	else if (pettype == PM_GHOUL)
		petname = ghoulname;
	else if (pettype == PM_PONY)
		petname = horsename;
/*
	else if (petsym == S_BAT)
		petname = batname;
	else if (petsym == S_SNAKE)
		petname = snakename;
	else if (petsym == S_RODENT)
		petname = ratname;
	else if (pettype == PM_GIANT_BADGER)
		petname = badgername;
	else if (pettype == PM_BABY_RED_DRAGON)
		petname = reddragonname;
	else if (pettype == PM_BABY_WHITE_DRAGON)
		petname = whitedragonname;
*/
	else if (petsym == S_DOG)
		petname = dogname;
	else
		petname = catname;

	/* default pet names */
	if (!*petname && pettype == PM_LITTLE_DOG) {
		/* All of these names were for dogs. */
		if (Role_if(PM_CAVEMAN)) petname = "Slasher";  /* The Warrior */
		if (Role_if(PM_SAMURAI)) petname = "Hachi";    /* Shibuya Station */
		if (Role_if(PM_BARBARIAN)) petname = "Idefix"; /* Obelix */
		if (Role_if(PM_RANGER)) petname = "Sirius";    /* Orion's dog */
	}

	mtmp = makemon(&mons[pettype], u.ux, u.uy, MM_EDOG);

	if (!mtmp) return NULL; /* pets were genocided */

	/* Horses already wear a saddle */
	if (pettype == PM_PONY && !!(otmp = mksobj(SADDLE, true, false))) {
		if (mpickobj(mtmp, otmp))
			panic("merged saddle?");
		mtmp->misc_worn_check |= W_SADDLE;
		otmp->dknown = otmp->bknown = otmp->rknown = 1;
		otmp->owornmask = W_SADDLE;
		otmp->leashmon = mtmp->m_id;
		update_mon_intrinsics(mtmp, otmp, true, true);
	}

	if (!petname_used++ && *petname)
		mtmp = christen_monst(mtmp, petname);

	initedog(mtmp);
	return mtmp;
}

/* record `last move time' for all monsters prior to level save so that
   mon_arrive() can catch up for lost time when they're restored later */
void update_mlstmv(void) {
	struct monst *mon;

	/* monst->mlstmv used to be updated every time `monst' actually moved,
	   but that is no longer the case so we just do a blanket assignment */
	for (mon = fmon; mon; mon = mon->nmon)
		if (!DEADMONSTER(mon)) mon->mlstmv = monstermoves;
}

void losedogs(void) {
	struct monst *mtmp, *mtmp0 = 0, *mtmp2;

	while ((mtmp = mydogs) != 0) {
		mydogs = mtmp->nmon;
		mon_arrive(mtmp, true);
	}

	for (mtmp = migrating_mons; mtmp; mtmp = mtmp2) {
		mtmp2 = mtmp->nmon;
		if (mtmp->mux == u.uz.dnum && mtmp->muy == u.uz.dlevel) {
			if (mtmp == migrating_mons)
				migrating_mons = mtmp->nmon;
			else
				mtmp0->nmon = mtmp->nmon;
			mon_arrive(mtmp, false);
		} else
			mtmp0 = mtmp;
	}
}

/* called from resurrect() in addition to losedogs() */
void mon_arrive(struct monst *mtmp, boolean with_you) {
	struct trap *t;
	xchar xlocale, ylocale, xyloc, xyflags, wander;
	int num_segs;

	mtmp->nmon = fmon;
	fmon = mtmp;
	if (mtmp->isshk)
		set_residency(mtmp, false);

	num_segs = mtmp->wormno;
	/* baby long worms have no tail so don't use is_longworm() */
	if ((mtmp->data == &mons[PM_LONG_WORM]) &&
	    (mtmp->wormno = get_wormno()) != 0) {
		initworm(mtmp, num_segs);
		/* tail segs are not yet initialized or displayed */
	} else
		mtmp->wormno = 0;

	/* some monsters might need to do something special upon arrival
	   _after_ the current level has been fully set up; see dochug() */
	mtmp->mstrategy |= STRAT_ARRIVE;

	/* make sure mnexto(rloc_to(set_apparxy())) doesn't use stale data */
	mtmp->mux = u.ux, mtmp->muy = u.uy;
	xyloc = mtmp->mtrack[0].x;
	xyflags = mtmp->mtrack[0].y;
	xlocale = mtmp->mtrack[1].x;
	ylocale = mtmp->mtrack[1].y;
	mtmp->mtrack[0].x = mtmp->mtrack[0].y = 0;
	mtmp->mtrack[1].x = mtmp->mtrack[1].y = 0;

	if (mtmp == u.usteed)
		return; /* don't place steed on the map */

	if (with_you) {
		/* When a monster accompanies you, sometimes it will arrive
		   at your intended destination and you'll end up next to
		   that spot.  This code doesn't control the final outcome;
		   goto_level(do.c) decides who ends up at your target spot
		   when there is a monster there too. */
		if (!MON_AT(u.ux, u.uy) &&
		    !rn2(mtmp->mtame ? 10 : mtmp->mpeaceful ? 5 : 2))
			rloc_to(mtmp, u.ux, u.uy);
		else
			mnexto(mtmp);
		return;
	}
	/*
	 * The monster arrived on this level independently of the player.
	 * Its coordinate fields were overloaded for use as flags that
	 * specify its final destination.
	 */

	if (mtmp->mlstmv < monstermoves - 1L) {
		/* heal monster for time spent in limbo */
		long nmv = monstermoves - 1L - mtmp->mlstmv;

		mon_catchup_elapsed_time(mtmp, nmv);
		mtmp->mlstmv = monstermoves - 1L;

		/* let monster move a bit on new level (see placement code below) */
		wander = (xchar)min(nmv, 8);
	} else
		wander = 0;

	switch (xyloc) {
		case MIGR_APPROX_XY: /* {x,y}locale set above */
			break;
		case MIGR_EXACT_XY:
			wander = 0;
			break;
		case MIGR_NEAR_PLAYER:
			xlocale = u.ux, ylocale = u.uy;
			break;
		case MIGR_STAIRS_UP:
			xlocale = xupstair, ylocale = yupstair;
			break;
		case MIGR_STAIRS_DOWN:
			xlocale = xdnstair, ylocale = ydnstair;
			break;
		case MIGR_LADDER_UP:
			xlocale = xupladder, ylocale = yupladder;
			break;
		case MIGR_LADDER_DOWN:
			xlocale = xdnladder, ylocale = ydnladder;
			break;
		case MIGR_SSTAIRS:
			xlocale = sstairs.sx, ylocale = sstairs.sy;
			break;
		case MIGR_PORTAL:
			if (In_endgame(&u.uz)) {
				/* there is no arrival portal for endgame levels */
				/* BUG[?]: for simplicity, this code relies on the fact
			   that we know that the current endgame levels always
			   build upwards and never have any exclusion subregion
			   inside their TELEPORT_REGION settings. */
				xlocale = rn1(updest.hx - updest.lx + 1, updest.lx);
				ylocale = rn1(updest.hy - updest.ly + 1, updest.ly);
				break;
			}
			/* find the arrival portal */
			for (t = ftrap; t; t = t->ntrap)
				if (t->ttyp == MAGIC_PORTAL) break;
			if (t) {
				xlocale = t->tx, ylocale = t->ty;
				break;
			} else
				impossible("mon_arrive: no corresponding portal?");
		fallthru;
		default:
		case MIGR_RANDOM:
			xlocale = ylocale = 0;
			break;
	}

	if (xlocale && wander) {
		/* monster moved a bit; pick a nearby location */
		/* mnearto() deals w/stone, et al */
		char *r = in_rooms(xlocale, ylocale, 0);
		if (r && *r) {
			coord c;
			/* somexy() handles irregular rooms */
			if (somexy(&rooms[*r - ROOMOFFSET], &c))
				xlocale = c.x, ylocale = c.y;
			else
				xlocale = ylocale = 0;
		} else { /* not in a room */
			int i, j;
			i = max(1, xlocale - wander);
			j = min(COLNO - 1, xlocale + wander);
			xlocale = rn1(j - i, i);
			i = max(0, ylocale - wander);
			j = min(ROWNO - 1, ylocale + wander);
			ylocale = rn1(j - i, i);
		}
	} /* moved a bit */

	mtmp->mx = 0; /*(already is 0)*/
	mtmp->my = xyflags;
	if (xlocale) {
		mnearto(mtmp, xlocale, ylocale, false);
	} else {
		if (!rloc(mtmp, true)) {
			/*
			 * Failed to place migrating monster,
			 * probably because the level is full.
			 * Dump the monster's cargo and leave the monster dead.
			 */
			struct obj *obj;
			while ((obj = mtmp->minvent) != 0) {
				obj_extract_self(obj);
				obj_no_longer_held(obj);
				if (obj->owornmask & W_WEP)
					setmnotwielded(mtmp, obj);
				obj->owornmask = 0L;
				if (xlocale && ylocale) {
					place_object(obj, xlocale, ylocale);
				} else if (rloco(obj)) {
					get_obj_location(obj, &xlocale, &ylocale, 0);
				}
			}
			mkcorpstat(CORPSE, NULL, mtmp->data, xlocale, ylocale, CORPSTAT_NONE);
			mongone(mtmp);
		}
	}
}

/* heal monster for time spent elsewhere */
void mon_catchup_elapsed_time(struct monst *mtmp, long nmv /* number of moves */) {
	int imv = 0; /* avoid zillions of casts and lint warnings */

#if defined(DEBUG) || defined(BETA)
	if (nmv < 0L) { /* crash likely... */
		panic("catchup from future time?");
		/*NOTREACHED*/
		return;
	} else if (nmv == 0L) { /* safe, but should'nt happen */
		impossible("catchup from now?");
	} else
#endif
		if (nmv >= LARGEST_INT) /* paranoia */
		imv = LARGEST_INT - 1;
	else
		imv = (int)nmv;

	/* might stop being afraid, blind or frozen */
	/* set to 1 and allow final decrement in movemon() */
	if (mtmp->mblinded) {
		if (imv >= (int)mtmp->mblinded)
			mtmp->mblinded = 1;
		else
			mtmp->mblinded -= imv;
	}
	if (mtmp->mfrozen) {
		if (imv >= mtmp->mfrozen)
			mtmp->mfrozen = 1;
		else
			mtmp->mfrozen -= imv;
	}
	if (mtmp->mfleetim) {
		if (imv >= (int)mtmp->mfleetim)
			mtmp->mfleetim = 1;
		else
			mtmp->mfleetim -= imv;
	}

	/* might recover from temporary trouble */
	if (mtmp->mtrapped && rn2(imv + 1) > 40 / 2) mtmp->mtrapped = 0;
	if (mtmp->mconf && rn2(imv + 1) > 50 / 2) mtmp->mconf = 0;
	if (mtmp->mstun && rn2(imv + 1) > 10 / 2) mtmp->mstun = 0;

	/* might finish eating or be able to use special ability again */
	if (imv > mtmp->meating)
		finish_meating(mtmp);
	else
		mtmp->meating -= imv;
	if (imv > mtmp->mspec_used)
		mtmp->mspec_used = 0;
	else
		mtmp->mspec_used -= imv;

	/*
	*      M1_MINDLESS __
	*      M2_UNDEAD     |
	*      M2_WERE       |-- These types will go ferral
	*      M2_DEMON      |
	*      M1_ANIMAL   --
	*/

	if (is_animal(mtmp->data) || mindless(mtmp->data) ||
	    is_demon(mtmp->data) || is_undead(mtmp->data) ||
	    is_were(mtmp->data)) {
		/* reduce tameness for every
		 * 150 moves you are away
		 */
		if (mtmp->mtame > nmv / 150)
			mtmp->mtame -= nmv / 150;
		else
			mtmp->mtame = 0;
	}
	/* check to see if it would have died as a pet; if so, go wild instead
	 * of dying the next time we call dog_move()
	 */
	if (mtmp->mtame && !mtmp->isminion &&
	    (carnivorous(mtmp->data) || herbivorous(mtmp->data))) {
		struct edog *edog = EDOG(mtmp);

		if ((monstermoves > edog->hungrytime + 500 && mtmp->mhp < 3) ||
		    (monstermoves > edog->hungrytime + 750))
			mtmp->mtame = mtmp->mpeaceful = 0;
	}

	if (!mtmp->mtame && mtmp->mleashed) {
		/* leashed monsters should always be with hero, consequently
		   never losing any time to be accounted for later */
		impossible("catching up for leashed monster?");
		m_unleash(mtmp, false);
	}

	/* recover lost hit points */
	if (!regenerates(mtmp->data)) imv /= 20;
	if (mtmp->mhp + imv >= mtmp->mhpmax)
		mtmp->mhp = mtmp->mhpmax;
	else
		mtmp->mhp += imv;
}

/* called when you move to another level */
void keepdogs(boolean pets_only /* true for ascension or final escape */) {
	struct monst *mtmp, *mtmp2;
	struct obj *obj;
	int num_segs;
	boolean stay_behind;
	extern d_level new_dlevel; /* in do.c */

	for (mtmp = fmon; mtmp; mtmp = mtmp2) {
		mtmp2 = mtmp->nmon;
		if (DEADMONSTER(mtmp)) continue;
		if (pets_only) {
			if (!mtmp->mtame) continue;     /* reject non-pets */
			/* don't block pets from accompanying hero's dungeon
			   escape or ascension simply due to mundane trifles;
			   unlike level change for steed, don't bother trying
			   to achieve a normal trap escape first */
			mtmp->mtrapped = 0;
			mtmp->meating = 0;
			mtmp->msleeping = 0;
			mtmp->mfrozen = 0;
			mtmp->mcanmove = 1;
		}

		if (((monnear(mtmp, u.ux, u.uy) && levl_follower(mtmp)) || (mtmp == u.usteed) ||
		     /* the wiz will level t-port from anywhere to chase
		                   the amulet; if you don't have it, will chase you
		                   only if in range. -3. */
		     (u.uhave.amulet && mtmp->iswiz)) &&
		    ((!mtmp->msleeping && mtmp->mcanmove)
		     /* eg if level teleport or new trap, steed has no control
		                       to avoid following */
		     || (mtmp == u.usteed))
		    /* monster won't follow if it hasn't noticed you yet */
		    && !(mtmp->mstrategy & STRAT_WAITFORU)) {
			stay_behind = false;
			if (mtmp == u.usteed) {
				/* make sure steed is eligible to accompany hero;
				 * start by having mintrap() give a chance to escape
				 * trap normally but if that fails, force the untrap
				 * (note: handle traps first because normal escape
				 * has the potential to set monster->meating) */
				if (mtmp->mtrapped && mintrap(mtmp))
					mtmp->mtrapped = 0;	// escape trap
				mtmp->meating = 0;		// terminate eating
				mdrop_special_objs(mtmp);	// drop Amulet
			} else if (mtmp->mtame && mtmp->meating) {
				if (canseemon(mtmp))
					pline("%s is still eating.", Monnam(mtmp));
				stay_behind = true;
			} else if (mtmp->mtame &&
				   (Is_blackmarket(&new_dlevel) || Is_blackmarket(&u.uz))) {
				pline("%s can't follow you %s.",
				      Monnam(mtmp), Is_blackmarket(&u.uz) ? "through the portal" : "into the Black Market");
				stay_behind = true;
			} else if (mon_has_amulet(mtmp)) {
				if (canseemon(mtmp))
					pline("%s seems very disoriented for a moment.",
					      Monnam(mtmp));
				stay_behind = true;
			} else if (mtmp->mtame && mtmp->mtrapped) {
				if (canseemon(mtmp))
					pline("%s is still trapped.", Monnam(mtmp));
				stay_behind = true;
			}

			if (stay_behind) {
				if (mtmp->mleashed) {
					pline("%s leash suddenly comes loose.",
					      humanoid(mtmp->data) ? (mtmp->female ? "Her" : "His") : "Its");
					m_unleash(mtmp, false);
				}
				if (mtmp == u.usteed) {
					// can't happen unless someone makes a change
					// which scrambles the stay_behind logic above
					impossible("steed left behind?");
					dismount_steed(DISMOUNT_GENERIC);
				}

				continue;
			}
			if (mtmp->isshk)
				set_residency(mtmp, true);

			if (mtmp->wormno) {
				int cnt;
				/* NOTE: worm is truncated to # segs = max wormno size */
				cnt = count_wsegs(mtmp);
				num_segs = min(cnt, MAX_NUM_WORMS - 1);
				wormgone(mtmp);
			} else
				num_segs = 0;

			/* set minvent's obj->no_charge to 0 */
			for (obj = mtmp->minvent; obj; obj = obj->nobj) {
				if (Has_contents(obj))
					picked_container(obj); /* does the right thing */
				obj->no_charge = 0;
			}

			relmon(mtmp);
			newsym(mtmp->mx, mtmp->my);
			mtmp->mx = mtmp->my = 0; /* avoid mnexto()/MON_AT() problem */
			mtmp->wormno = num_segs;
			mtmp->mlstmv = monstermoves;
			mtmp->nmon = mydogs;
			mydogs = mtmp;
		} else if (mtmp->iswiz) {
			/* we want to be able to find him when his next resurrection
			   chance comes up, but have him resume his present location
			   if player returns to this level before that time */
			migrate_to_level(mtmp, ledger_no(&u.uz),
					 MIGR_EXACT_XY, NULL);
		} else if (mtmp->mleashed) {
			/* this can happen if your quest leader ejects you from the
			   "home" level while a leashed pet isn't next to you */
			pline("%s leash goes slack.", s_suffix(Monnam(mtmp)));
			m_unleash(mtmp, false);
		}
	}
}

void migrate_to_level(
	struct monst *mtmp,
	xchar tolev, /* destination level */
	xchar xyloc, /* MIGR_xxx destination xy location: */
	coord *cc /* optional destination coordinates */) {
	struct obj *obj;
	d_level new_lev;
	xchar xyflags;
	int num_segs = 0; /* count of worm segments */

	if (mtmp->isshk)
		set_residency(mtmp, true);

	if (mtmp->wormno) {
		int cnt;
		/* **** NOTE: worm is truncated to # segs = max wormno size **** */
		cnt = count_wsegs(mtmp);
		num_segs = min(cnt, MAX_NUM_WORMS - 1);
		wormgone(mtmp);
	}

	/* set minvent's obj->no_charge to 0 */
	for (obj = mtmp->minvent; obj; obj = obj->nobj) {
		if (Has_contents(obj))
			picked_container(obj); /* does the right thing */
		obj->no_charge = 0;
	}

	if (mtmp->mleashed) {
		mtmp->mtame--;
		m_unleash(mtmp, true);
	}
	relmon(mtmp);
	mtmp->nmon = migrating_mons;
	migrating_mons = mtmp;
	newsym(mtmp->mx, mtmp->my);

	new_lev.dnum = ledger_to_dnum((xchar)tolev);
	new_lev.dlevel = ledger_to_dlev((xchar)tolev);
	/* overload mtmp->[mx,my], mtmp->[mux,muy], and mtmp->mtrack[] as */
	/* destination codes (setup flag bits before altering mx or my) */
	xyflags = (depth(&new_lev) < depth(&u.uz)); /* 1 => up */
	if (In_W_tower(mtmp->mx, mtmp->my, &u.uz)) xyflags |= 2;
	mtmp->wormno = num_segs;
	mtmp->mlstmv = monstermoves;
	mtmp->mtrack[1].x = cc ? cc->x : mtmp->mx;
	mtmp->mtrack[1].y = cc ? cc->y : mtmp->my;
	mtmp->mtrack[0].x = xyloc;
	mtmp->mtrack[0].y = xyflags;
	mtmp->mux = new_lev.dnum;
	mtmp->muy = new_lev.dlevel;
	mtmp->mx = mtmp->my = 0; /* this implies migration */
}

/* return quality of food; the lower the better */
/* fungi will eat even tainted food */
int dogfood(struct monst *mon, struct obj *obj) {
	struct permonst *mptr = mon->data, *fptr = &mons[obj->corpsenm];
	bool carni = carnivorous(mon->data),
	     herbi = herbivorous(mon->data),
	     starving;

	if (is_quest_artifact(obj) || obj_resists(obj, 0, 95))
		return obj->cursed ? TABU : APPORT;

	/* KMH -- Koalas can only eat eucalyptus */
	if (mon->data == &mons[PM_KOALA])
		return obj->otyp == EUCALYPTUS_LEAF ? DOGFOOD : APPORT;

	switch (obj->oclass) {
		case FOOD_CLASS:
			if (obj->otyp == CORPSE &&
			    ((touch_petrifies(fptr) && !resists_ston(mon)) || is_rider(fptr)))
				return TABU;
			/* Ghouls only eat old corpses... yum! */
			if (mon->data == &mons[PM_GHOUL] || mon->data == &mons[PM_GHAST]) {
				return (obj->otyp == CORPSE && obj->corpsenm != PM_ACID_BLOB &&
					peek_at_iced_corpse_age(obj) + 5 * rn1(20, 10) <= monstermoves) ?
					       DOGFOOD :
					       TABU;
			}
			/* vampires only "eat" very fresh corpses ...
		 * Assume meat -> blood
		 */
			if (is_vampire(mon->data)) {
				return (obj->otyp == CORPSE &&
					has_blood(fptr) && !obj->oeaten &&
					peek_at_iced_corpse_age(obj) + 5 >= monstermoves) ?
					       DOGFOOD :
					       TABU;
			}

			if (!carni && !herbi)
				return obj->cursed ? UNDEF : APPORT;

			/* a starving pet will eat almost anything */
			starving = (mon->mtame && !mon->isminion &&
				    EDOG(mon)->mhpmax_penalty);

			switch (obj->otyp) {
				case TRIPE_RATION:
				case MEATBALL:
				case MEAT_RING:
				case MEAT_STICK:
				case HUGE_CHUNK_OF_MEAT:
					return carni ? DOGFOOD : MANFOOD;
				case EGG:
					if (touch_petrifies(fptr) && !resists_ston(mon))
						return POISON;
					return carni ? CADAVER : MANFOOD;
				case CORPSE:
					if ((peek_at_iced_corpse_age(obj) + 50L <= monstermoves
					     && obj->corpsenm != PM_LIZARD
					     && obj->corpsenm != PM_LICHEN
					     && mptr->mlet != S_FUNGUS)
					    || (acidic(fptr) && !resists_acid(mon))
					    || (poisonous(fptr) && !resists_poison(mon)))
						return POISON;
					else if (vegan(fptr))
						return herbi ? CADAVER : MANFOOD;
					// most humanoids will avoid cannibalism unless starving;
					// arbitrary: elves won't eat other elves even then
					else if (humanoid(mptr) && same_race(mptr, fptr) &&
						 (!is_undead(mptr) && fptr->mlet != S_KOBOLD &&
						  fptr->mlet != S_ORC && fptr->mlet != S_OGRE))
						return (starving && carni && !is_elf(mptr)) ?  ACCFOOD : TABU;
					else
						return carni ? CADAVER : MANFOOD;

				case CLOVE_OF_GARLIC:
					return ((is_undead(mptr) || is_vampshifter(mon)) ? TABU :
								       ((herbi || starving) ? ACCFOOD : MANFOOD));
				case TIN:
					return metallivorous(mptr) ? ACCFOOD : MANFOOD;
				case APPLE:
				case CARROT:
					return herbi ? DOGFOOD : starving ? ACCFOOD : MANFOOD;
				case BANANA:
					return ((mptr->mlet == S_YETI) ? DOGFOOD :
									      ((herbi || starving) ? ACCFOOD : MANFOOD));
				default:
					if (starving) return ACCFOOD;
					return (obj->otyp > SLIME_MOLD ?
							(carni ? ACCFOOD : MANFOOD) :
							(herbi ? ACCFOOD : MANFOOD));
			}
		default:
			if (obj->otyp == AMULET_OF_STRANGULATION ||
			    obj->otyp == RIN_SLOW_DIGESTION)
				return TABU;
			if (mon_hates_silver(mon) &&
			    objects[obj->otyp].oc_material == SILVER)
				return TABU;
			/* KMH -- Taz likes organics, too! */
			if ((mptr == &mons[PM_GELATINOUS_CUBE] ||
			     mptr == &mons[PM_SHOGGOTH] ||
			     mptr == &mons[PM_GIANT_SHOGGOTH] ||
			     mptr == &mons[PM_TASMANIAN_DEVIL]) &&
			    is_organic(obj))
				return ACCFOOD;
			if (metallivorous(mptr) && is_metallic(obj) && (is_rustprone(obj) || mptr != &mons[PM_RUST_MONSTER])) {
				/* Non-rustproofed ferrous based metals are preferred. */
				return ((is_rustprone(obj) && !obj->oerodeproof) ? DOGFOOD :
										   ACCFOOD);
			}
			if (!obj->cursed && obj->oclass != BALL_CLASS &&
			    obj->oclass != CHAIN_CLASS)
				return APPORT;

			return UNDEF;

		case ROCK_CLASS:
			return UNDEF;
	}
}

// returns true if taming succeeded
bool tamedog(struct monst *mtmp, struct obj *obj) {
	/* The Wiz, Medusa and the quest nemeses aren't even made peaceful. */
	if (mtmp->iswiz || mtmp->data == &mons[PM_MEDUSA] || (mtmp->data->mflags3 & M3_WANTSARTI))
		return false;

	/* worst case, at least it'll be peaceful. */
	mtmp->mpeaceful = 1;
	mtmp->mtraitor = 0; /* No longer a traitor */
	set_malign(mtmp);
	if (flags.moonphase == FULL_MOON && night() && rn2(6) && obj && mtmp->data->mlet == S_DOG)
		return false;

	/* If we cannot tame it, at least it's no longer afraid. */
	mtmp->mflee = 0;
	mtmp->mfleetim = 0;

	/* make grabber let go now, whether it becomes tame or not */
	if (mtmp == u.ustuck) {
		if (u.uswallow)
			expels(mtmp, mtmp->data, true);
		else if (!(Upolyd && sticks(youmonst.data)))
			unstuck(mtmp);
	}

	/* feeding it treats makes it tamer */
	if (mtmp->mtame && obj) {
		int tasty;

		if (mtmp->mcanmove && !mtmp->mconf && !mtmp->meating &&
		    ((tasty = dogfood(mtmp, obj)) == DOGFOOD ||
		     (tasty <= ACCFOOD && EDOG(mtmp)->hungrytime <= monstermoves))) {
			/* pet will "catch" and eat this thrown food */
			if (canseemon(mtmp)) {
				boolean big_corpse = (obj->otyp == CORPSE &&
						      obj->corpsenm >= LOW_PM &&
						      mons[obj->corpsenm].msize > mtmp->data->msize);
				pline("%s catches %s%s",
				      Monnam(mtmp), the(xname(obj)),
				      !big_corpse ? "." : ", or vice versa!");
			} else if (cansee(mtmp->mx, mtmp->my))
				pline("%s.", Tobjnam(obj, "stop"));
			/* dog_eat expects a floor object */
			place_object(obj, mtmp->mx, mtmp->my);
			dog_eat(mtmp, obj, mtmp->mx, mtmp->my, false);
			/* eating might have killed it, but that doesn't matter here;
			   a non-null result suppresses "miss" message for thrown
			   food and also implies that the object has been deleted */
			return true;
		} else
			return false;
	}

	if (mtmp->mtame || !mtmp->mcanmove ||
	    /* monsters with conflicting structures cannot be tamed */
	    mtmp->isshk || mtmp->isgd || mtmp->ispriest || mtmp->isminion ||
	    /* KMH -- Added gypsy */
	    mtmp->isgyp ||
	    is_covetous(mtmp->data) || is_human(mtmp->data) ||
	    (is_demon(mtmp->data) && !is_demon(youmonst.data)) ||
	    /* Mik -- New flag to indicate which things cannot be tamed... */
	    cannot_be_tamed(mtmp->data) ||
	    (obj && dogfood(mtmp, obj) >= MANFOOD)) return false;

	if (mtmp->m_id == quest_status.leader_m_id)
		return false;

	/* add the pet extension */
	newedog(mtmp);
	initedog(mtmp);

	if (obj) { /* thrown food */
		/* defer eating until the edog extension has been set up */
		place_object(obj, mtmp->mx, mtmp->my); /* put on floor */
		/* devour the food (might grow into larger, genocided monster) */
		if (dog_eat(mtmp, obj, mtmp->mx, mtmp->my, true) == 2)
			return true; /* oops, it died... */
				      /* `obj' is now obsolete */
	}

	newsym(mtmp->mx, mtmp->my);
	if (attacktype(mtmp->data, AT_WEAP)) {
		mtmp->weapon_check = NEED_HTH_WEAPON;
		mon_wield_item(mtmp);
	}

	return true;
}

int make_pet_minion(int mnum, aligntyp alignment) {
	struct monst *mon;
	mon = makemon(&mons[mnum], u.ux, u.uy, MM_EDOG | (mnum == PM_ANGEL ? MM_EPRI : MM_EMIN));
	if (!mon) return 0;

	initedog(mon);
	newsym(mon->mx, mon->my);
	mon->mpeaceful = 1;
	set_malign(mon);
	mon->mtame = 10;

	/* this section names the creature "of ______" */
	if (mnum == PM_ANGEL) {
		mon->isminion = true;
		EPRI(mon)->shralign = alignment;
	} else {
		mon->isminion = true;
		EMIN(mon)->min_align = alignment;
	}

	return 1;
}

/*
 * Called during pet revival or pet life-saving.
 * If you killed the pet, it revives wild.
 * If you abused the pet a lot while alive, it revives wild.
 * If you abused the pet at all while alive, it revives untame.
 * If the pet wasn't abused and was very tame, it might revive tame.
 */
void wary_dog(struct monst *mtmp, boolean was_dead) {
	struct edog *edog;
	boolean quietly = was_dead;

	finish_meating(mtmp);

	if (!mtmp->mtame) return;
	edog = !mtmp->isminion ? EDOG(mtmp) : 0;

	/* if monster was starving when it died, undo that now */
	if (edog && edog->mhpmax_penalty) {
		mtmp->mhpmax += edog->mhpmax_penalty;
		mtmp->mhp += edog->mhpmax_penalty; /* heal it */
		edog->mhpmax_penalty = 0;
	}

	if (edog && (edog->killed_by_u == 1 || edog->abuse > 2)) {
		mtmp->mpeaceful = false;
		mtmp->mtame = 0;
		if (edog->abuse >= 0 && edog->abuse < 10)
			if (!rn2(edog->abuse + 1)) mtmp->mpeaceful = 1;
		if (!quietly && cansee(mtmp->mx, mtmp->my)) {
			if (haseyes(youmonst.data)) {
				if (haseyes(mtmp->data))
					pline("%s %s to look you in the %s.",
					      Monnam(mtmp),
					      mtmp->mpeaceful ? "seems unable" :
								"refuses",
					      body_part(EYE));
				else
					pline("%s avoids your gaze.",
					      Monnam(mtmp));
			}
		}
	} else {
		/* chance it goes wild anyway - Pet Semetary */
		if (!rn2(mtmp->mtame)) {
			mtmp->mpeaceful = false;
			mtmp->mtame = 0;
		}
	}
	if (!mtmp->mtame) {
		newsym(mtmp->mx, mtmp->my);
		/* a life-saved monster might be leashed;
		   don't leave it that way if it's no longer tame */
		if (mtmp->mleashed) m_unleash(mtmp, true);
	}

	/* if its still a pet, start a clean pet-slate now */
	if (edog && mtmp->mtame) {
		edog->revivals++;
		edog->killed_by_u = 0;
		edog->abuse = 0;
		edog->ogoal.x = edog->ogoal.y = -1;
		if (was_dead || edog->hungrytime < monstermoves + 500L)
			edog->hungrytime = monstermoves + 500L;
		if (was_dead) {
			edog->droptime = 0L;
			edog->dropdist = 10000;
			edog->whistletime = 0L;
			edog->apport = 5;
		} /* else lifesaved, so retain current values */
	}
}

void abuse_dog(struct monst *mtmp) {
	if (!mtmp->mtame) return;

	if (Aggravate_monster || Conflict)
		mtmp->mtame /= 2;
	else
		mtmp->mtame--;

	if (mtmp->mtame && !mtmp->isminion)
		EDOG(mtmp)->abuse++;

	if (!mtmp->mtame && mtmp->mleashed)
		m_unleash(mtmp, true);

	/* don't make a sound if pet is in the middle of leaving the level */
	/* newsym isn't necessary in this case either */
	if (mtmp->mx != 0) {
		if (mtmp->mtame && rn2(mtmp->mtame))
			yelp(mtmp);
		else
			growl(mtmp); /* give them a moment's worry */

		/* Give monster a chance to betray you now */
		if (mtmp->mtame) betrayed(mtmp);

		if (!mtmp->mtame) newsym(mtmp->mx, mtmp->my);
	}
}

/*dog.c*/
