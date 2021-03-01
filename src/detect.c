/*	SCCS Id: @(#)detect.c	3.4	2003/08/13	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Detection routines, including crystal ball, magic mapping, and search
 * command.
 */

#include "hack.h"
#include "artifact.h"

extern boolean known; /* from read.c */

static void do_dknown_of(struct obj *obj);
static bool check_map_spot(int x, int y, char oclass, uint material);
static bool clear_stale_map(char oclass, uint material);
static void sense_trap(struct trap *trap, xchar x, xchar y, int src_cursed);
static enum OTRAP detect_obj_traps(struct obj *objlist, bool show_them, int how);
static void show_map_spot(int x, int y);
static void findone(int zx, int zy, void *num);
static void openone(int zx, int zy, void *num);

/* Recursively search obj for an object in class oclass and return 1st found */
struct obj *o_in(struct obj *obj, char oclass) {
	struct obj *otmp;
	struct obj *temp;

	if (obj->oclass == oclass) return obj;

	/*
	 * Pills inside medical kits are specially handled (see apply.c).
	 * We don't want them to detect as food because then they will be
	 * shown as pink pills, which are something quite different. In
	 * practice the only other possible contents of medical kits are
	 * bandages and phials, neither of which is detectable by any
	 * means so we can simply avoid looking in medical kits.
	 */
	if (Has_contents(obj) && obj->otyp != MEDICAL_KIT) {
		for (otmp = obj->cobj; otmp; otmp = otmp->nobj)
			if (otmp->oclass == oclass)
				return otmp;
			else if (Has_contents(otmp) && otmp->otyp != MEDICAL_KIT &&
				 (temp = o_in(otmp, oclass)))
				return temp;
	}
	return NULL;
}

/* Recursively search obj for an object made of specified material and return 1st found */
struct obj *o_material(struct obj *obj, uint material) {
	struct obj *otmp;
	struct obj *temp;

	if (objects[obj->otyp].oc_material == material) return obj;

	if (Has_contents(obj)) {
		for (otmp = obj->cobj; otmp; otmp = otmp->nobj)
			if (objects[otmp->otyp].oc_material == material)
				return otmp;
			else if (Has_contents(otmp) && (temp = o_material(otmp, material)))
				return temp;
	}
	return NULL;
}

static void do_dknown_of(struct obj *obj) {
	struct obj *otmp;

	obj->dknown = 1;
	if (Has_contents(obj)) {
		for (otmp = obj->cobj; otmp; otmp = otmp->nobj)
			do_dknown_of(otmp);
	}
}

/* Check whether the location has an outdated object displayed on it. */
static bool check_map_spot(int x, int y, char oclass, uint material) {
	int glyph;
	struct obj *otmp;
	struct monst *mtmp;

	glyph = glyph_at(x, y);
	if (glyph_is_object(glyph)) {
		/* there's some object shown here */
		if (oclass == ALL_CLASSES) {
			return !(level.objects[x][y] || /* stale if nothing here */
				 ((mtmp = m_at(x, y)) != NULL && (mtmp->minvent)));
		} else {
			if (material && objects[glyph_to_obj(glyph)].oc_material == material) {
				/* the object shown here is of interest because material matches */
				for (otmp = level.objects[x][y]; otmp; otmp = otmp->nexthere)
					if (o_material(otmp, GOLD)) return false;
				/* didn't find it; perhaps a monster is carrying it */
				if ((mtmp = m_at(x, y)) != 0) {
					for (otmp = mtmp->minvent; otmp; otmp = otmp->nobj)
						if (o_material(otmp, GOLD)) return false;
				}
				/* detection indicates removal of this object from the map */
				return true;
			}
			if (oclass && objects[glyph_to_obj(glyph)].oc_class == oclass) {
				/* the object shown here is of interest because its class matches */
				for (otmp = level.objects[x][y]; otmp; otmp = otmp->nexthere)
					if (o_in(otmp, oclass)) return false;
				/* didn't find it; perhaps a monster is carrying it */
				if ((mtmp = m_at(x, y)) != 0) {
					for (otmp = mtmp->minvent; otmp; otmp = otmp->nobj)
						if (o_in(otmp, oclass)) return false;
				}
				/* detection indicates removal of this object from the map */
				return true;
			}
		}
	}
	return false;
}

/*
   When doing detection, remove stale data from the map display (corpses
   rotted away, objects carried away by monsters, etc) so that it won't
   reappear after the detection has completed.  Return true if noticeable
   change occurs.
 */
static bool clear_stale_map(char oclass, uint material) {
	int zx, zy;
	bool change_made = false;

	for (zx = 1; zx < COLNO; zx++)
		for (zy = 0; zy < ROWNO; zy++)
			if (check_map_spot(zx, zy, oclass, material)) {
				unmap_object(zx, zy);
				change_made = true;
			}

	return change_made;
}

/* look for gold, on the floor or in monsters' possession */
int gold_detect(struct obj *sobj) {
	struct obj *obj;
	struct monst *mtmp;
	int uw = u.uinwater;
	struct obj *temp;
	boolean stale;

	known = stale = clear_stale_map(COIN_CLASS,
					(unsigned)(sobj->blessed ? GOLD : 0));

	/* look for gold carried by monsters (might be in a container) */
	for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
		if (DEADMONSTER(mtmp)) continue; /* probably not needed in this case but... */
		if (findgold(mtmp->minvent) || monsndx(mtmp->data) == PM_GOLD_GOLEM) {
			known = true;
			goto outgoldmap; /* skip further searching */
		} else
			for (obj = mtmp->minvent; obj; obj = obj->nobj)
				if (sobj->blessed && o_material(obj, GOLD)) {
					known = true;
					goto outgoldmap;
				} else if (o_in(obj, COIN_CLASS)) {
					known = true;
					goto outgoldmap; /* skip further searching */
				}
	}

	/* look for gold objects */
	for (obj = fobj; obj; obj = obj->nobj) {
		if (sobj->blessed && o_material(obj, GOLD)) {
			known = true;
			if (obj->ox != u.ux || obj->oy != u.uy) goto outgoldmap;
		} else if (o_in(obj, COIN_CLASS)) {
			known = true;
			if (obj->ox != u.ux || obj->oy != u.uy) goto outgoldmap;
		}
	}

	if (!known) {
		/* no gold found on floor or monster's inventory.
		   adjust message if you have gold in your inventory */
		if (sobj) {
			char buf[BUFSZ];
			if (youmonst.data == &mons[PM_GOLD_GOLEM]) {
				sprintf(buf, "You feel like a million %s!",
					currency(2L));
			} else if (hidden_gold() || money_cnt(invent))
				strcpy(buf,
				       "You feel worried about your future financial situation.");
			else
				strcpy(buf, "You feel materially poor.");
			strange_feeling(sobj, buf);
		}
		return 1;
	}
	/* only under me - no separate display required */
	if (stale) docrt();
	pline("You notice some gold between your %s.", makeplural(body_part(FOOT)));
	return 0;

outgoldmap:
	cls();

	u.uinwater = 0;
	/* Discover gold locations. */
	for (obj = fobj; obj; obj = obj->nobj) {
		if (sobj->blessed && (temp = o_material(obj, GOLD))) {
			if (temp != obj) {
				temp->ox = obj->ox;
				temp->oy = obj->oy;
			}
			map_object(temp, 1);
		} else if ((temp = o_in(obj, COIN_CLASS))) {
			if (temp != obj) {
				temp->ox = obj->ox;
				temp->oy = obj->oy;
			}
			map_object(temp, 1);
		}
	}
	for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
		if (DEADMONSTER(mtmp)) continue; /* probably overkill here */
		if (findgold(mtmp->minvent) || monsndx(mtmp->data) == PM_GOLD_GOLEM) {
			struct obj gold;

			gold.otyp = GOLD_PIECE;
			gold.ox = mtmp->mx;
			gold.oy = mtmp->my;
			map_object(&gold, 1);
		} else
			for (obj = mtmp->minvent; obj; obj = obj->nobj)
				if (sobj->blessed && (temp = o_material(obj, GOLD))) {
					temp->ox = mtmp->mx;
					temp->oy = mtmp->my;
					map_object(temp, 1);
					break;
				} else if ((temp = o_in(obj, COIN_CLASS))) {
					temp->ox = mtmp->mx;
					temp->oy = mtmp->my;
					map_object(temp, 1);
					break;
				}
	}

	newsym(u.ux, u.uy);
	pline("You feel very greedy, and sense gold!");
	exercise(A_WIS, true);
	display_nhwindow(WIN_MAP, true);
	docrt();
	u.uinwater = uw;
	if (Underwater) under_water(2);
	if (u.uburied) under_ground(2);
	return 0;
}

// returns 1 if nothing was detected
// returns 0 if something was detected
int food_detect(struct obj *sobj) {
	struct obj *obj;
	struct monst *mtmp;
	int ct = 0, ctu = 0;
	boolean confused = (Confusion || (sobj && sobj->cursed)), stale;
	char oclass = confused ? POTION_CLASS : FOOD_CLASS;
	const char *what = confused ? "something" : "food";
	int uw = u.uinwater;

	stale = clear_stale_map(oclass, 0);

	for (obj = fobj; obj; obj = obj->nobj)
		if (o_in(obj, oclass)) {
			if (obj->ox == u.ux && obj->oy == u.uy)
				ctu++;
			else
				ct++;
		}
	for (mtmp = fmon; mtmp && !ct; mtmp = mtmp->nmon) {
		/* no DEADMONSTER(mtmp) check needed since dmons never have inventory */
		for (obj = mtmp->minvent; obj; obj = obj->nobj)
			if (o_in(obj, oclass)) {
				ct++;
				break;
			}
	}

	if (!ct && !ctu) {
		known = stale && !confused;
		if (stale) {
			docrt();
			pline("You sense a lack of %s nearby.", what);
			if (sobj && sobj->blessed) {
				if (!u.uedibility) pline("Your %s starts to tingle.", body_part(NOSE));
				u.uedibility = 1;
			}
		} else if (sobj) {
			char buf[BUFSZ];
			sprintf(buf, "Your %s twitches%s.", body_part(NOSE),
				(sobj->blessed && !u.uedibility) ? " then starts to tingle" : "");
			if (sobj->blessed && !u.uedibility) {
				boolean savebeginner = flags.beginner; /* prevent non-delivery of */
				flags.beginner = false;		       /* 	message            */
				strange_feeling(sobj, buf);
				flags.beginner = savebeginner;
				u.uedibility = 1;
			} else
				strange_feeling(sobj, buf);
		}
		return !stale;
	} else if (!ct) {
		known = true;
		pline("You %s %s nearby.", sobj ? "smell" : "sense", what);
		if (sobj && sobj->blessed) {
			if (!u.uedibility) pline("Your %s starts to tingle.", body_part(NOSE));
			u.uedibility = 1;
		}
	} else {
		struct obj *temp;
		known = true;
		cls();
		u.uinwater = 0;
		for (obj = fobj; obj; obj = obj->nobj)
			if ((temp = o_in(obj, oclass)) != 0) {
				if (temp != obj) {
					temp->ox = obj->ox;
					temp->oy = obj->oy;
				}
				map_object(temp, 1);
			}
		for (mtmp = fmon; mtmp; mtmp = mtmp->nmon)
			/* no DEADMONSTER(mtmp) check needed since dmons never have inventory */
			for (obj = mtmp->minvent; obj; obj = obj->nobj)
				if ((temp = o_in(obj, oclass)) != 0) {
					temp->ox = mtmp->mx;
					temp->oy = mtmp->my;
					map_object(temp, 1);
					break; /* skip rest of this monster's inventory */
				}
		newsym(u.ux, u.uy);
		if (sobj) {
			if (sobj->blessed) {
				pline("Your %s %s to tingle and you smell %s.", body_part(NOSE),
				      u.uedibility ? "continues" : "starts", what);
				u.uedibility = 1;
			} else
				pline("Your %s tingles and you smell %s.", body_part(NOSE), what);
		} else
			pline("You sense %s.", what);
		display_nhwindow(WIN_MAP, true);
		exercise(A_WIS, true);
		docrt();
		u.uinwater = uw;
		if (Underwater) under_water(2);
		if (u.uburied) under_ground(2);
	}
	return 0;
}

/*
 * Used for scrolls, potions, spells, and crystal balls.  Returns:
 *
 *	1 - nothing was detected
 *	0 - something was detected
 */
int object_detect(
	struct obj *detector, /* object doing the detecting */
	int class /* an object class, 0 for all */) {
	int x, y;
	char stuff[BUFSZ];
	int is_cursed = (detector && detector->cursed);
	int do_dknown = (detector && (detector->oclass == POTION_CLASS || detector->oclass == SPBOOK_CLASS) &&
			 detector->blessed);
	int ct = 0, ctu = 0;
	struct obj *obj, *otmp = NULL;
	struct monst *mtmp;
	int uw = u.uinwater;
	int boulder = 0;

	if (class < 0 || class >= MAXOCLASSES) {
		impossible("object_detect:  illegal class %d", class);
		class = 0;
	}

	if (Hallucination || (Confusion && class == SCROLL_CLASS))
		strcpy(stuff, "something");
	else
		strcpy(stuff, class ? oclass_names[class] : "objects");
	if (boulder && class != ROCK_CLASS) strcat(stuff, " and/or large stones");

	if (do_dknown)
		for (obj = invent; obj; obj = obj->nobj)
			do_dknown_of(obj);

	for (obj = fobj; obj; obj = obj->nobj) {
		if ((!class && !boulder) || o_in(obj, class) || o_in(obj, boulder)) {
			if (obj->ox == u.ux && obj->oy == u.uy)
				ctu++;
			else
				ct++;
		}
		if (do_dknown) do_dknown_of(obj);
	}

	for (obj = level.buriedobjlist; obj; obj = obj->nobj) {
		if (!class || o_in(obj, class)) {
			if (obj->ox == u.ux && obj->oy == u.uy)
				ctu++;
			else
				ct++;
		}
		if (do_dknown) do_dknown_of(obj);
	}

	for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
		if (DEADMONSTER(mtmp)) continue;
		for (obj = mtmp->minvent; obj; obj = obj->nobj) {
			if ((!class && !boulder) || o_in(obj, class) || o_in(obj, boulder)) ct++;
			if (do_dknown) do_dknown_of(obj);
		}
		if ((is_cursed && mtmp->m_ap_type == M_AP_OBJECT &&
		     (!class || class == objects[mtmp->mappearance].oc_class)) ||
		    (findgold(mtmp->minvent) && (!class || class == COIN_CLASS))) {
			ct++;
			break;
		}
	}

	if (!clear_stale_map(!class ? ALL_CLASSES : class, 0) && !ct) {
		if (!ctu) {
			if (detector)
				strange_feeling(detector, "You feel a lack of something.");
			return 1;
		}

		pline("You sense %s nearby.", stuff);
		return 0;
	}

	cls();

	u.uinwater = 0;
	/*
	 *	Map all buried objects first.
	 */
	for (obj = level.buriedobjlist; obj; obj = obj->nobj)
		if (!class || (otmp = o_in(obj, class))) {
			if (class) {
				if (otmp != obj) {
					otmp->ox = obj->ox;
					otmp->oy = obj->oy;
				}
				map_object(otmp, 1);
			} else
				map_object(obj, 1);
		}
	/*
	 * If we are mapping all objects, map only the top object of a pile or
	 * the first object in a monster's inventory.  Otherwise, go looking
	 * for a matching object class and display the first one encountered
	 * at each location.
	 *
	 * Objects on the floor override buried objects.
	 */
	for (x = 1; x < COLNO; x++)
		for (y = 0; y < ROWNO; y++)
			for (obj = level.objects[x][y]; obj; obj = obj->nexthere)
				if ((!class && !boulder) ||
				    (otmp = o_in(obj, class)) || (otmp = o_in(obj, boulder))) {
					if (class || boulder) {
						if (otmp != obj) {
							otmp->ox = obj->ox;
							otmp->oy = obj->oy;
						}
						map_object(otmp, 1);
					} else
						map_object(obj, 1);
					break;
				}

	/* Objects in the monster's inventory override floor objects. */
	for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
		if (DEADMONSTER(mtmp)) continue;
		for (obj = mtmp->minvent; obj; obj = obj->nobj)
			if ((!class && !boulder) ||
			    (otmp = o_in(obj, class)) || (otmp = o_in(obj, boulder))) {
				if (!class && !boulder) otmp = obj;
				otmp->ox = mtmp->mx; /* at monster location */
				otmp->oy = mtmp->my;
				map_object(otmp, 1);
				break;
			}
		/* Allow a mimic to override the detected objects it is carrying. */
		if (is_cursed && mtmp->m_ap_type == M_AP_OBJECT &&
		    (!class || class == objects[mtmp->mappearance].oc_class)) {
			struct obj temp;

			temp.otyp = mtmp->mappearance; /* needed for obj_to_glyph() */
			temp.ox = mtmp->mx;
			temp.oy = mtmp->my;
			temp.corpsenm = PM_TENGU; /* if mimicing a corpse */
			map_object(&temp, 1);
		} else if (findgold(mtmp->minvent) && (!class || class == COIN_CLASS)) {
			struct obj gold;

			gold.otyp = GOLD_PIECE;
			gold.ox = mtmp->mx;
			gold.oy = mtmp->my;
			map_object(&gold, 1);
		}
	}

	newsym(u.ux, u.uy);
	pline("You detect the %s of %s.", ct ? "presence" : "absence", stuff);
	display_nhwindow(WIN_MAP, true);
	/*
	 * What are we going to do when the hero does an object detect while blind
	 * and the detected object covers a known pool?
	 */
	docrt(); /* this will correctly reset vision */

	u.uinwater = uw;
	if (Underwater) under_water(2);
	if (u.uburied) under_ground(2);
	return 0;
}

/*
 * Used by: crystal balls, potions, fountains
 *
 * Returns 1 if nothing was detected.
 * Returns 0 if something was detected.
 */
int monster_detect(
	struct obj *otmp, /* detecting object (if any) */
	int mclass /* monster class, 0 for all */) {
	struct monst *mtmp;
	int mcnt = 0;

	/* Note: This used to just check fmon for a non-zero value
	 * but in versions since 3.3.0 fmon can test true due to the
	 * presence of dmons, so we have to find at least one
	 * with positive hit-points to know for sure.
	 */
	for (mtmp = fmon; mtmp; mtmp = mtmp->nmon)
		if (!DEADMONSTER(mtmp)) {
			mcnt++;
			break;
		}

	if (!mcnt) {
		if (otmp)
			strange_feeling(otmp, Hallucination ?
						      "You get the heebie jeebies." :
						      "You feel threatened.");
		return 1;
	} else {
		boolean woken = false;

		cls();
		for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
			if (DEADMONSTER(mtmp)) continue;
			if (!mclass || mtmp->data->mlet == mclass ||
			    (mtmp->data == &mons[PM_LONG_WORM] && mclass == S_WORM_TAIL))
				if (mtmp->mx > 0) {
					if (mclass && def_monsyms[mclass] == ' ')
						show_glyph(mtmp->mx, mtmp->my,
							   detected_mon_to_glyph(mtmp));
					else
						show_glyph(mtmp->mx, mtmp->my, mon_to_glyph(mtmp));
					/* don't be stingy - display entire worm */
					if (mtmp->data == &mons[PM_LONG_WORM]) detect_wsegs(mtmp, 0);
				}
			if (otmp && otmp->cursed &&
			    (mtmp->msleeping || !mtmp->mcanmove)) {
				mtmp->msleeping = false;
				mtmp->mfrozen = 0;
				mtmp->mcanmove = 1;
				woken = true;
			}
		}
		display_self();
		pline("You sense the presence of monsters.");
		if (woken)
			pline("Monsters sense the presence of you.");
		display_nhwindow(WIN_MAP, true);
		docrt();
		if (Underwater) under_water(2);
		if (u.uburied) under_ground(2);
	}
	return 0;
}

static void sense_trap(struct trap *trap, xchar x, xchar y, int src_cursed) {
	if (Hallucination || src_cursed) {
		struct obj obj; /* fake object */
		if (trap) {
			obj.ox = trap->tx;
			obj.oy = trap->ty;
		} else {
			obj.ox = x;
			obj.oy = y;
		}
		obj.otyp = (src_cursed) ? GOLD_PIECE : random_object();
		obj.corpsenm = random_monster(); /* if otyp == CORPSE */
		map_object(&obj, 1);
	} else if (trap) {
		map_trap(trap, 1);
		trap->tseen = 1;
	} else {
		struct trap temp_trap; /* fake trap */
		temp_trap.tx = x;
		temp_trap.ty = y;
		temp_trap.ttyp = BEAR_TRAP; /* some kind of trap */
		map_trap(&temp_trap, 1);
	}
}

enum OTRAP {
	OTRAP_NONE = 0,       /* nothing found */
	OTRAP_HERE = 1,       /* found at hero's location */
	OTRAP_THERE = 2,      /* found at any other location */
};

/* check a list of objects for chest traps; return HERE if found at <ux,uy>,
   THERE if found at some other spot, HERE|THERE if both, NONE otherwise; optionally
   update the map to show where such traps were found */
// how=1 for misleading map feedback
static enum OTRAP detect_obj_traps(struct obj *objlist, bool show_them, int how) {
	struct obj *otmp;
	xchar x, y;
	enum OTRAP result = OTRAP_NONE;

	for (otmp = objlist; otmp; otmp = otmp->nobj) {
		if (Is_box(otmp) && otmp->otrapped &&
				get_obj_location(otmp, &x, &y, BURIED_TOO|CONTAINED_TOO)) {
			result |= (x == u.ux && y == u.uy) ? OTRAP_HERE : OTRAP_THERE;
			if (show_them) sense_trap(NULL, x, y, how);
		}
		if (Has_contents(otmp))
			result |= detect_obj_traps(otmp->cobj, show_them, how);
	}
	return result;
}


// the detections are pulled out so they can
// also be used in the crystal ball routine
// returns 1 if nothing was detected
// returns 0 if something was detected
// sobj is null if crystal ball, *scroll if gold detection scroll
int trap_detect(struct obj *sobj) {
	struct trap *ttmp;
	struct obj *obj;
	struct monst *mon;
	int door, glyph, tr;
	int uw = u.uinwater, cursed_src = sobj && sobj->cursed;
	boolean found = false;
	int x, y;

	// floor/ceiling traps
	for (ttmp = ftrap; ttmp; ttmp = ttmp->ntrap) {
		if (ttmp->tx != u.ux || ttmp->ty != u.uy)
			goto outtrapmap;
		else
			found = true;
	}
	    /* chest traps (might be buried or carried) */
	if ((tr = detect_obj_traps(fobj, false, 0)) != OTRAP_NONE) {
		if (tr & OTRAP_THERE) goto outtrapmap;
		else found = true;
	}
	if ((tr = detect_obj_traps(level.buriedobjlist, false, 0)) != OTRAP_NONE) {
		if (tr & OTRAP_THERE) goto outtrapmap;
		else found = true;
	}
	for (mon = fmon; mon; mon = mon->nmon) {
		if (DEADMONSTER(mon)) continue;
		if ((tr = detect_obj_traps(mon->minvent, false, 0)) != OTRAP_NONE) {
			if (tr & OTRAP_THERE) goto outtrapmap;
			else found = true;
		}
	}
	if (detect_obj_traps(invent, false, 0) != OTRAP_NONE) found = true;
	// door traps
	for (door = 0; door < doorindex; door++) {
		x = doors[door].x;
		y = doors[door].y;
		if (levl[x][y].doormask & D_TRAPPED) {
			if (x != u.ux || y != u.uy)
				goto outtrapmap;
			else
				found = true;
		}
	}
	if (!found) {
		char buf[42];
		sprintf(buf, "Your %s stop itching.", makeplural(body_part(TOE)));
		strange_feeling(sobj, buf);
		return 1;
	}
	/* traps exist, but only under me - no separate display required */
	pline("Your %s itch.", makeplural(body_part(TOE)));
	return 0;
outtrapmap:
	cls();

	u.uinwater = 0;
	for (ttmp = ftrap; ttmp; ttmp = ttmp->ntrap)
		sense_trap(ttmp, 0, 0, cursed_src);

	for (obj = fobj; obj; obj = obj->nobj)
		if (Is_box(obj) && obj->otrapped)
			sense_trap(NULL, obj->ox, obj->oy, cursed_src);

	for (door = 0; door < doorindex; door++) {
		x = doors[door].x;
		y = doors[door].y;
		if (levl[x][y].doormask & D_TRAPPED)
			sense_trap(NULL, x, y, cursed_src);
	}

	// redisplay hero unless sense_trap() revealed something at <ux,uy>
	glyph = glyph_at(u.ux, u.uy);
	if (!(glyph_is_trap(glyph) || glyph_is_object(glyph)))
		newsym(u.ux, u.uy);

	pline("You feel %s.", cursed_src ? "very greedy" : "entrapped");

	// wait for user to respond, then reset map display to normal
	display_nhwindow(WIN_MAP, true);
	docrt();
	u.uinwater = uw;
	if (Underwater) under_water(2);
	if (u.uburied) under_ground(2);
	return 0;
}

const char *level_distance(d_level *where) {
	schar ll = depth(&u.uz) - depth(where);
	boolean indun = (u.uz.dnum == where->dnum);

	if (ll < 0) {
		if (ll < (-8 - rn2(3))) {
			if (!indun)
				return "far away";
			else
				return "far below";
		} else if (ll < -1) {
			if (!indun)
				return "away below you";
			else
				return "below you";
		} else if (!indun)
			return "in the distance";
		else
			return "just below";
	} else if (ll > 0) {
		if (ll > (8 + rn2(3))) {
			if (!indun)
				return "far away";
			else
				return "far above";
		} else if (ll > 1) {
			if (!indun)
				return "away above you";
			else
				return "above you";
		} else if (!indun)
			return "in the distance";
		else
			return "just above";
	} else if (!indun)
		return "in the distance";
	else
		return "near you";
}

static const struct {
	const char *what;
	d_level *where;
} level_detects[] = {
	{"Delphi", &oracle_level},
	{"Medusa's lair", &medusa_level},
	{"a castle", &stronghold_level},
	{"the Wizard of Yendor's tower", &wiz1_level},
};

void use_crystal_ball(struct obj *obj) {
	char ch;
	int oops;

	if (Blind) {
		pline("Too bad you can't see %s.", the(xname(obj)));
		return;
	}
	oops = (rnd(20) > ACURR(A_INT) || obj->cursed);
	if (oops && (obj->spe > 0)) {
		switch (rnd(obj->oartifact ? 4 : 5)) {
			case 1:
				pline("%s too much to comprehend!", Tobjnam(obj, "are"));
				break;
			case 2:
				pline("%s you!", Tobjnam(obj, "confuse"));
				make_confused(HConfusion + rnd(100), false);
				break;
			case 3:
				if (!resists_blnd(&youmonst)) {
					pline("%s your vision!", Tobjnam(obj, "damage"));
					make_blinded(Blinded + rnd(100), false);
					if (!Blind) pline("Your %s", "vision quickly clears.");
				} else {
					pline("%s your vision.", Tobjnam(obj, "assault"));
					pline("You are unaffected!");
				}
				break;
			case 4:
				pline("%s your mind!", Tobjnam(obj, "zap"));
				make_hallucinated(HHallucination + rnd(100), false, 0L);
				break;
			case 5:
				pline("%s!", Tobjnam(obj, "explode"));
				useup(obj);
				obj = 0; /* it's gone */
				losehp(Maybe_Half_Phys(rnd(30)), "exploding crystal ball", KILLED_BY_AN);
				break;
		}
		if (obj) consume_obj_charge(obj, true);
		return;
	}

	if (Hallucination) {
		if (!obj->spe) {
			pline("All you see is funky %s haze.", hcolor(NULL));
		} else {
			switch (rnd(6)) {
				case 1:
					pline("You grok some groovy globs of incandescent lava.");
					break;
				case 2:
					pline("Whoa!  Psychedelic colors, %s!",
					      poly_gender() == 1 ? "babe" : "dude");
					break;
				case 3:
					pline("The crystal pulses with sinister %s light!",
					      hcolor(NULL));
					break;
				case 4:
					pline("You see goldfish swimming above fluorescent rocks.");
					break;
				case 5:
					pline("You see tiny snowflakes spinning around a miniature farmhouse.");
					break;
				default:
					pline("Oh wow... like a kaleidoscope!");
					break;
			}
			consume_obj_charge(obj, true);
		}
		return;
	}

	/* read a single character */
	if (flags.verbose) pline("You may look for an object or monster symbol.");
	ch = yn_function("What do you look for?", NULL, '\0');
	/* Don't filter out ' ' here; it has a use */
	if ((ch != def_monsyms[S_GHOST]) && index(quitchars, ch)) {
		if (flags.verbose) pline("%s", "Never mind.");
		return;
	}
	pline("You peer into %s...", the(xname(obj)));
	nomul(-rnd(10));
	nomovemsg = "";
	if (obj->spe <= 0)
		pline("The vision is unclear.");
	else {
		int class;
		int ret = 0;

		makeknown(CRYSTAL_BALL);
		consume_obj_charge(obj, true);

		/* special case: accept ']' as synonym for mimic
		 * we have to do this before the def_char_to_objclass check
		 */
		if (ch == DEF_MIMIC_DEF) ch = DEF_MIMIC;

		if ((class = def_char_to_objclass(ch)) != MAXOCLASSES)
			ret = object_detect(NULL, class);
		else if ((class = def_char_to_monclass(ch)) != MAXMCLASSES)
			ret = monster_detect(NULL, class);
		else
			switch (ch) {
				case '^':
					ret = trap_detect(NULL);
					break;
				default: {
					int i = rn2(SIZE(level_detects));
					pline("You see %s, %s.",
					      level_detects[i].what,
					      level_distance(level_detects[i].where));
				}
					ret = 0;
					break;
			}

		if (ret) {
			if (!rn2(100)) /* make them nervous */
				pline("You see the Wizard of Yendor gazing out at you.");
			else
				pline("The vision is unclear.");
		}
	}
	return;
}

static void show_map_spot(int x, int y) {
	struct rm *lev;

	if (Confusion && rn2(7)) return;
	lev = &levl[x][y];

	lev->seenv = SVALL;

	/* Secret corridors are found, but not secret doors. */
	if (lev->typ == SCORR) {
		lev->typ = CORR;
		unblock_point(x, y);
	}

	/* if we don't remember an object or trap there, map it */
	if (!lev->mem_obj && !lev->mem_trap) {
		if (level.flags.hero_memory) {
			magic_map_background(x, y, 0);
			newsym(x, y); /* show it, if not blocked */
		} else {
			magic_map_background(x, y, 1); /* display it */
		}
	}
}

void do_mapping(void) {
	int zx, zy;
	int uw = u.uinwater;

	u.uinwater = 0;
	for (zx = 1; zx < COLNO; zx++)
		for (zy = 0; zy < ROWNO; zy++)
			show_map_spot(zx, zy);
	exercise(A_WIS, true);
	u.uinwater = uw;
	if (!level.flags.hero_memory || Underwater) {
		flush_screen(1);		 /* flush temp screen */
		display_nhwindow(WIN_MAP, true); /* wait */
		docrt();
	}
}

void do_vicinity_map(void) {
	int zx, zy;
	int lo_y = (u.uy - 5 < 0 ? 0 : u.uy - 5),
	    hi_y = (u.uy + 6 > ROWNO ? ROWNO : u.uy + 6),
	    lo_x = (u.ux - 9 < 1 ? 1 : u.ux - 9), /* avoid column 0 */
		hi_x = (u.ux + 10 > COLNO ? COLNO : u.ux + 10);

	for (zx = lo_x; zx < hi_x; zx++)
		for (zy = lo_y; zy < hi_y; zy++)
			show_map_spot(zx, zy);

	if (!level.flags.hero_memory || Underwater) {
		flush_screen(1);		 /* flush temp screen */
		display_nhwindow(WIN_MAP, true); /* wait */
		docrt();
	}
}

/* convert a secret door into a normal door */
void cvt_sdoor_to_door(struct rm *lev) {
	int newmask = lev->doormask & ~WM_MASK;

	if (Is_rogue_level(&u.uz))
		/* rogue didn't have doors, only doorways */
		newmask = D_NODOOR;
	else
		/* newly exposed door is closed */
		if (!(newmask & D_LOCKED))
		newmask |= D_CLOSED;

	lev->typ = DOOR;
	lev->doormask = newmask;
}

static void findone(int zx, int zy, void *num) {
	struct trap *ttmp;
	struct monst *mtmp;

	if (levl[zx][zy].typ == SDOOR) {
		cvt_sdoor_to_door(&levl[zx][zy]); /* .typ = DOOR */
		magic_map_background(zx, zy, 0);
		newsym(zx, zy);
		(*(int *)num)++;
	} else if (levl[zx][zy].typ == SCORR) {
		levl[zx][zy].typ = CORR;
		unblock_point(zx, zy);
		magic_map_background(zx, zy, 0);
		newsym(zx, zy);
		(*(int *)num)++;
	} else if ((ttmp = t_at(zx, zy)) != 0) {
		if (!ttmp->tseen && ttmp->ttyp != STATUE_TRAP) {
			ttmp->tseen = 1;
			newsym(zx, zy);
			(*(int *)num)++;
		}
	} else if ((mtmp = m_at(zx, zy)) != 0) {
		if (mtmp->m_ap_type) {
			seemimic(mtmp);
			(*(int *)num)++;
		}
		if (mtmp->mundetected &&
		    (is_hider(mtmp->data) || mtmp->data->mlet == S_EEL)) {
			mtmp->mundetected = 0;
			newsym(zx, zy);
			(*(int *)num)++;
		}
		if (!canspotmon(mtmp) &&
		    !memory_is_invisible(zx, zy))
			map_invisible(zx, zy);
	} else if (memory_is_invisible(zx, zy)) {
		unmap_object(zx, zy);
		newsym(zx, zy);
		(*(int *)num)++;
	}
}

static void openone(int zx, int zy, void *num) {
	struct trap *ttmp;
	struct obj *otmp;

	if (OBJ_AT(zx, zy)) {
		for (otmp = level.objects[zx][zy];
		     otmp;
		     otmp = otmp->nexthere) {
			if (Is_box(otmp) && otmp->olocked) {
				otmp->olocked = 0;
				(*(int *)num)++;
			}
		}
		/* let it fall to the next cases. could be on trap. */
	}
	if (levl[zx][zy].typ == SDOOR || (levl[zx][zy].typ == DOOR &&
					  (levl[zx][zy].doormask & (D_CLOSED | D_LOCKED)))) {
		if (levl[zx][zy].typ == SDOOR)
			cvt_sdoor_to_door(&levl[zx][zy]); /* .typ = DOOR */
		if (levl[zx][zy].doormask & D_TRAPPED) {
			if (distu(zx, zy) < 3)
				b_trapped("door", 0);
			else
				Norep("You %s an explosion!",
				      cansee(zx, zy) ? "see" :
				      Deaf ? "feel the shock of" :
				      "hear");

			wake_nearto(zx, zy, 11 * 11);
			levl[zx][zy].doormask = D_NODOOR;
		} else
			levl[zx][zy].doormask = D_ISOPEN;
		unblock_point(zx, zy);
		newsym(zx, zy);
		(*(int *)num)++;
	} else if (levl[zx][zy].typ == SCORR) {
		levl[zx][zy].typ = CORR;
		unblock_point(zx, zy);
		newsym(zx, zy);
		(*(int *)num)++;
	} else if ((ttmp = t_at(zx, zy)) != 0) {
		if (!ttmp->tseen && ttmp->ttyp != STATUE_TRAP) {
			ttmp->tseen = 1;
			newsym(zx, zy);
			(*(int *)num)++;
		}
	} else if (find_drawbridge(&zx, &zy)) {
		/* make sure it isn't an open drawbridge */
		open_drawbridge(zx, zy);
		(*(int *)num)++;
	}
}

// returns number of things found
int findit(void) {
	int num = 0;

	if (u.uswallow) return 0;
	do_clear_area(u.ux, u.uy, BOLT_LIM, findone, (void *)&num);
	return num;
}

// returns number of things found and opened
int openit(void) {
	int num = 0;

	if (u.uswallow) {
		if (is_animal(u.ustuck->data)) {
			if (Blind)
				pline("Its mouth opens!");
			else
				pline("%s opens its mouth!", Monnam(u.ustuck));
		}
		expels(u.ustuck, u.ustuck->data, true);
		return -1;
	}

	do_clear_area(u.ux, u.uy, BOLT_LIM, openone, (void *)&num);
	return num;
}

void find_trap(struct trap *trap) {
	int tt = what_trap(trap->ttyp);
	boolean cleared = false;

	trap->tseen = 1;
	exercise(A_WIS, true);
	if (Blind)
		feel_location(trap->tx, trap->ty);
	else
		newsym(trap->tx, trap->ty);

	if (levl[trap->tx][trap->ty].mem_obj || memory_is_invisible(trap->tx, trap->ty)) {
		/* There's too much clutter to see your find otherwise */
		cls();
		map_trap(trap, 1);
		display_self();
		cleared = true;
	}

	pline("You find %s.", an(sym_desc[trap_to_defsym(tt)].explanation));

	if (cleared) {
		display_nhwindow(WIN_MAP, true); /* wait */
		docrt();
	}
}

void dosearch0(bool aflag) {
	if (u.uswallow) {
		if (!aflag)
			pline("What are you looking for?  The exit?");
	} else {
		for (xchar x = u.ux - 1; x < u.ux + 2; x++)
			for (xchar y = u.uy - 1; y < u.uy + 2; y++) {
				if (!isok(x, y)) continue;
				if (x != u.ux || y != u.uy) {
					if (Blind && !aflag) feel_location(x, y);
					if (levl[x][y].typ == SDOOR) {
						cvt_sdoor_to_door(&levl[x][y]); /* .typ = DOOR */
						exercise(A_WIS, true);
						nomul(0);
						if (Blind && !aflag)
							feel_location(x, y); /* make sure it shows up */
						else
							newsym(x, y);
					} else if (levl[x][y].typ == SCORR) {
						levl[x][y].typ = CORR;
						unblock_point(x, y); /* vision */
						exercise(A_WIS, true);
						nomul(0);
						newsym(x, y);
					} else {
						struct monst *mtmp;

						/* Be careful not to find anything in an SCORR or SDOOR */
						if ((mtmp = m_at(x, y)) && !aflag) {
							if (mtmp->m_ap_type) {
								seemimic(mtmp);
find:
								exercise(A_WIS, true);
								if (!canspotmon(mtmp)) {
									if (memory_is_invisible(x, y)) {
										/* found invisible monster in a square
										 * which already has an 'I' in it.
										 * Logically, this should still take
										 * time, but if we did that the player would
										 * keep finding the same monster every turn.
										 */
										continue;
									} else {
										pline("You feel an unseen monster!");
										map_invisible(x, y);
									}
								} else if (!sensemon(mtmp))
									pline("You find %s.", a_monnam(mtmp));
								return;
							}
							if (!canspotmon(mtmp)) {
								if (mtmp->mundetected &&
								    (is_hider(mtmp->data) || mtmp->data->mlet == S_EEL))
									mtmp->mundetected = 0;
								newsym(x, y);
								goto find;
							}
						}

						/* see if an invisible monster has moved--if Blind,
						 * feel_location() already did it
						 */
						if (!aflag && !mtmp && !Blind &&
						    memory_is_invisible(x, y)) {
							unmap_object(x, y);
							newsym(x, y);
						}

						struct trap *trap;
						if ((trap = t_at(x, y)) && !trap->tseen) {
							nomul(0);

							if (trap->ttyp == STATUE_TRAP) {
								mtmp = activate_statue_trap(trap, x, y, false);
								if (mtmp != NULL)
									exercise(A_WIS, true);
								return;
							} else {
								find_trap(trap);
							}
						}
					}
				}
			}
	}
}

/* Pre-map the sokoban levels */
void sokoban_detect(void) {
	int x, y;
	struct trap *ttmp;
	struct obj *obj;

	/* Map the background and boulders */
	for (x = 1; x < COLNO; x++)
		for (y = 0; y < ROWNO; y++) {
			levl[x][y].seenv = SVALL;
			levl[x][y].waslit = true;
			map_background(x, y, 1);
			for (obj = level.objects[x][y]; obj; obj = obj->nexthere)
				if (obj->otyp == BOULDER)
					map_object(obj, 1);
		}

	/* Map the traps */
	for (ttmp = ftrap; ttmp; ttmp = ttmp->ntrap) {
		ttmp->tseen = 1;
		map_trap(ttmp, 1);
	}
}

int dosearch(void) {
	dosearch0(false);
	return 1;
}

/*detect.c*/
