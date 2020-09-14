/*	SCCS Id: @(#)objnam.c	3.4	2003/12/04	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "onames.h"

/* "an uncursed greased partly eaten guardian naga hatchling [corpse]" */
#define PREFIX	  80 /* (56) */
#define SCHAR_LIM 127
#define NUMOBUF	  12

static char *strprepend(char *, const char *);
static boolean wishymatch(const char *, const char *, boolean);
static char *nextobuf(void);
static void add_erosion_words(struct obj *, char *);

static char *xname2(struct obj *obj, bool keen);

struct Jitem {
	int item;
	const char *name;
};

/* true for gems/rocks that should have " stone" appended to their names */
#define GemStone(typ) (typ == FLINT ||                              \
		       (objects[typ].oc_material == GEMSTONE &&     \
			(typ != DILITHIUM_CRYSTAL && typ != RUBY && \
			 typ != DIAMOND && typ != SAPPHIRE &&       \
			 typ != BLACK_OPAL &&                       \
			 typ != EMERALD && typ != OPAL)))

static struct Jitem Japanese_items[] = {
	{SHORT_SWORD, "wakizashi"},
	{BROADSWORD, "ninja-to"},
	{FLAIL, "nunchaku"},
	{GLAIVE, "naginata"},
	{LOCK_PICK, "osaku"},
	{WOODEN_HARP, "koto"},
	{KNIFE, "shito"},
	{PLATE_MAIL, "tanko"},
	{HELMET, "kabuto"},
	{LEATHER_GLOVES, "yugake"},
	{FOOD_RATION, "gunyoki"},
	{POT_BOOZE, "sake"},
	{0, ""}};

static const char *Japanese_item_name(int i);

static char *strprepend(char *s, const char *pref) {
	int i = (int)strlen(pref);

	if (strlen(pref) > PREFIX) {
		impossible("PREFIX too short (for %d).", i);
		return s;
	}

	s -= i;
	memcpy(s, pref, i); /* do not copy trailing 0 */
	return s;
}

/* manage a pool of BUFSZ buffers, so callers don't have to */
static char *nextobuf(void) {
	static char bufs[NUMOBUF][BUFSZ];
	static int bufidx = 0;

	bufidx = (bufidx + 1) % NUMOBUF;
	return bufs[bufidx];
}

char *obj_typename(int otyp) {
	char *buf = nextobuf();
	struct objclass *ocl = &objects[otyp];
	const char *actualn = OBJ_NAME(*ocl);
	const char *dn = OBJ_DESCR(*ocl);
	const char *un = ocl->oc_uname;
	int nn = ocl->oc_name_known;

	if (Role_if(PM_SAMURAI) && Japanese_item_name(otyp))
		actualn = Japanese_item_name(otyp);
	switch (ocl->oc_class) {
		case COIN_CLASS:
			strcpy(buf, "coin");
			break;
		case POTION_CLASS:
			strcpy(buf, "potion");
			break;
		case SCROLL_CLASS:
			strcpy(buf, "scroll");
			break;
		case WAND_CLASS:
			strcpy(buf, "wand");
			break;
		case SPBOOK_CLASS:
			strcpy(buf, "spellbook");
			break;
		case RING_CLASS:
			strcpy(buf, "ring");
			break;
		case AMULET_CLASS:
			if (nn)
				strcpy(buf, actualn);
			else
				strcpy(buf, "amulet");
			if (un)
				sprintf(eos(buf), " called %s", un);
			if (dn)
				sprintf(eos(buf), " (%s)", dn);
			return buf;
		default:
			if (nn) {
				strcpy(buf, actualn);
				if (GemStone(otyp))
					strcat(buf, " stone");
				if (un)
					sprintf(eos(buf), " called %s", un);
				if (dn)
					sprintf(eos(buf), " (%s)", dn);
			} else {
				strcpy(buf, dn ? dn : actualn);
				if (ocl->oc_class == GEM_CLASS)
					strcat(buf, (ocl->oc_material == MINERAL) ?
							    " stone" :
							    " gem");
				if (un)
					sprintf(eos(buf), " called %s", un);
			}
			return buf;
	}
	/* here for ring/scroll/potion/wand */
	if (nn) {
		if (ocl->oc_unique)
			strcpy(buf, actualn); /* avoid spellbook of Book of the Dead */
		/* KMH -- "mood ring" instead of "ring of mood" */
		else if (otyp == RIN_MOOD)
			sprintf(buf, "%s ring", actualn);
		else
			sprintf(eos(buf), " of %s", actualn);
	}
	if (un)
		sprintf(eos(buf), " called %s", un);
	if (dn)
		sprintf(eos(buf), " (%s)", dn);
	return buf;
}

/* less verbose result than obj_typename(); either the actual name
   or the description (but not both); user-assigned name is ignored */
char *simple_typename(int otyp) {
	char *bufp, *pp, *save_uname = objects[otyp].oc_uname;

	objects[otyp].oc_uname = 0; /* suppress any name given by user */
	bufp = obj_typename(otyp);
	objects[otyp].oc_uname = save_uname;
	if ((pp = strstri(bufp, " (")) != 0)
		*pp = '\0'; /* strip the appended description */
	return bufp;
}

boolean obj_is_pname(struct obj *obj) {
	return ((boolean)(obj->dknown && obj->known && obj->onamelth &&
			  /* Since there aren't any objects which are both
	                    artifacts and unique, the last check is redundant. */
			  obj->oartifact && !objects[obj->otyp].oc_unique));
}

/* Give the name of an object seen at a distance.  Unlike xname/doname,
 * we don't want to set dknown if it's not set already.  The kludge used is
 * to temporarily set Blind so that xname() skips the dknown setting.  This
 * assumes that we don't want to do this too often; if this function becomes
 * frequently used, it'd probably be better to pass a parameter to xname()
 * or doname() instead.
 */
char *distant_name(struct obj *obj, char *(*func)(struct obj *)) {
	char *str;

	long save_Blinded = Blinded;
	Blinded = 1;
	str = (*func)(obj);
	Blinded = save_Blinded;
	return str;
}

/* convert player specified fruit name into corresponding fruit juice name
   ("slice of pizza" -> "pizza juice" rather than "slice of pizza juice") */
// juice => whether or not to append " juice" to the name
char *fruitname(boolean juice) {
	char *buf = nextobuf();
	const char *fruit_nam = strstri(pl_fruit, " of ");

	if (fruit_nam)
		fruit_nam += 4; /* skip past " of " */
	else
		fruit_nam = pl_fruit; /* use it as is */

	sprintf(buf, "%s%s", makesingular(fruit_nam), juice ? " juice" : "");
	return buf;
}

char *xname2(struct obj *obj, bool keen) {
	/* Hallu */
	char *buf;
	int typ = obj->otyp;
	struct objclass *ocl = &objects[typ];
	int nn = ocl->oc_name_known;
	const char *actualn = OBJ_NAME(*ocl);
	const char *dn = OBJ_DESCR(*ocl);
	const char *un = ocl->oc_uname;
	bool pluralize = obj->quan != 1;

	buf = nextobuf() + PREFIX; /* leave room for "17 -3 " */
	if (Role_if(PM_SAMURAI) && Japanese_item_name(typ))
		actualn = Japanese_item_name(typ);

	if (!dn && restoring) dn = "???";
	buf[0] = '\0';
	/*
	 * clean up known when it's tied to oc_name_known, eg after AD_DRIN
	 * This is only required for unique objects since the article
	 * printed for the object is tied to the combination of the two
	 * and printing the wrong article gives away information.
	 */
	if (!nn && ocl->oc_uses_known && ocl->oc_unique) obj->known = 0;
	if (!Blind && (!obj->oinvis || See_invisible)) obj->dknown = true;
	if (Role_if(PM_PRIEST) || Role_if(PM_NECROMANCER)) obj->bknown = true;

	/* We could put a switch(obj->oclass) here but currently only this one case exists */
	if (obj->oclass == WEAPON_CLASS && is_poisonable(obj) && obj->opoisoned)
		strcpy(buf, "poisoned ");

	if (obj_is_pname(obj))
		goto nameit;
	switch (obj->oclass) {
		case AMULET_CLASS:
			if (!obj->dknown)
				strcpy(buf, "amulet");
			else if (typ == AMULET_OF_YENDOR ||
				 typ == FAKE_AMULET_OF_YENDOR)
				/* each must be identified individually */
				strcpy(buf, obj->known ? actualn : dn);
			else if (nn)
				strcpy(buf, actualn);
			else if (un)
				sprintf(buf, "amulet called %s", un);
			else
				sprintf(buf, "%s amulet", dn);
			break;
		case WEAPON_CLASS:
		case VENOM_CLASS:
		case TOOL_CLASS:
			if (typ == LENSES)
				strcpy(buf, "pair of ");

			if (!obj->dknown)
				strcat(buf, dn ? dn : actualn);
			else if (nn)
				strcat(buf, actualn);
			else if (un) {
				strcat(buf, dn ? dn : actualn);
				strcat(buf, " called ");
				strcat(buf, un);
			} else
				strcat(buf, dn ? dn : actualn);
			/* If we use an() here we'd have to remember never to use */
			/* it whenever calling doname() or xname(). */
			if (typ == FIGURINE)
				sprintf(eos(buf), " of a%s %s",
					index(vowels, *(mons[obj->corpsenm].mname)) ? "n" : "",
					mons[obj->corpsenm].mname);
			break;
		case ARMOR_CLASS:
			/* depends on order of the dragon scales objects */
			if (typ >= GRAY_DRAGON_SCALES && typ <= YELLOW_DRAGON_SCALES) {
				sprintf(buf, "set of %s", actualn);
				break;
			}
			if (is_boots(obj) || is_gloves(obj)) strcpy(buf, "pair of ");

			if (obj->otyp >= ELVEN_SHIELD && obj->otyp <= ORCISH_SHIELD && !obj->dknown) {
				strcpy(buf, "shield");
				break;
			}
			if (obj->otyp == SHIELD_OF_REFLECTION && !obj->dknown) {
				strcpy(buf, "smooth shield");
				break;
			}

			if (nn)
				strcat(buf, actualn);
			else if (un) {
				if (is_boots(obj))
					strcat(buf, "boots");
				else if (is_gloves(obj))
					strcat(buf, "gloves");
				else if (is_cloak(obj))
					strcpy(buf, "cloak");
				else if (is_helmet(obj))
					strcpy(buf, "helmet");
				else if (is_shield(obj))
					strcpy(buf, "shield");
				else
					strcpy(buf, "armor");
				strcat(buf, " called ");
				strcat(buf, un);
			} else
				strcat(buf, dn);
			break;
		case FOOD_CLASS:
			if (typ == SLIME_MOLD) {
				struct fruit *f;

				for (f = ffruit; f; f = f->nextf) {
					if (f->fid == obj->spe) {
						strcpy(buf, f->fname);
						break;
					}
				}
				if (!f) {
					impossible("Bad fruit #%d?", obj->spe);
					strcpy(buf, "slime mold");
				} else if (pluralize) {
					/* ick; already pluralized fruit names
					   are allowed--we want to try to avoid
					   adding a redundant plural suffix */
					strcpy(buf, makeplural(makesingular(buf)));
					pluralize = false;
				}
				break;
			}

			strcpy(buf, actualn);
			if (typ == TIN && obj->known) {
				if (obj->spe > 0)
					strcat(buf, " of spinach");
				else if (obj->corpsenm == NON_PM)
					strcpy(buf, "empty tin");
				else if (vegetarian(&mons[obj->corpsenm]))
					sprintf(eos(buf), " of %s", mons[obj->corpsenm].mname);
				else
					sprintf(eos(buf), " of %s meat", mons[obj->corpsenm].mname);
			}
			break;
		case COIN_CLASS:
		case CHAIN_CLASS:
			strcpy(buf, actualn);
			break;
		case ROCK_CLASS:
			if (typ == STATUE)
				sprintf(buf, "%s%s of %s%s",
					(Role_if(PM_ARCHEOLOGIST) && (obj->spe & STATUE_HISTORIC)) ? "historic " : "",
					actualn,
					type_is_pname(&mons[obj->corpsenm]) ? "" :
					(mons[obj->corpsenm].geno & G_UNIQ) ? "the " :
					(index(vowels, *(mons[obj->corpsenm].mname)) ?  "an " :
					"a "),
					mons[obj->corpsenm].mname);
			else
				strcpy(buf, actualn);
			break;
		case BALL_CLASS:
			sprintf(buf, "%sheavy iron ball",
				(obj->owt > ocl->oc_weight) ? "very " : "");
			break;
		case POTION_CLASS:
			if (obj->dknown && obj->odiluted)
				strcpy(buf, "diluted ");
			if (nn || un || !obj->dknown) {
				strcat(buf, "potion");
				if (!obj->dknown) break;
				if (nn) {
					strcat(buf, " of ");
					if (typ == POT_WATER &&
					    obj->bknown && (obj->blessed || obj->cursed)) {
						strcat(buf, obj->blessed ? "holy " : "unholy ");
					}
					strcat(buf, actualn);
				} else {
					strcat(buf, " called ");
					strcat(buf, un);
				}
			} else {
				strcat(buf, dn);
				strcat(buf, " potion");
			}
			break;
		case SCROLL_CLASS:
			strcpy(buf, "scroll");
			if (!obj->dknown) break;
			if (nn) {
				strcat(buf, " of ");
				strcat(buf, actualn);
			} else if (un) {
				strcat(buf, " called ");
				strcat(buf, un);
			} else if (ocl->oc_magic) {
				strcat(buf, " labeled ");
				strcat(buf, dn);
			} else {
				strcpy(buf, dn);
				strcat(buf, " scroll");
			}
			break;
		case WAND_CLASS:
			if (!obj->dknown)
				strcpy(buf, "wand");
			else if (nn)
				sprintf(buf, "wand of %s", actualn);
			else if (un)
				sprintf(buf, "wand called %s", un);
			else
				sprintf(buf, "%s wand", dn);
			break;
		case SPBOOK_CLASS:
			if (!obj->dknown) {
				strcpy(buf, "spellbook");
			} else if (nn) {
				if (typ != SPE_BOOK_OF_THE_DEAD)
					strcpy(buf, "spellbook of ");
				strcat(buf, actualn);
			} else if (un) {
				sprintf(buf, "spellbook called %s", un);
			} else
				sprintf(buf, "%s spellbook", dn);
			break;
		case RING_CLASS:
			if (!obj->dknown)
				strcpy(buf, "ring");
			else if (nn) {
				/* KMH -- "mood ring" instead of "ring of mood" */
				if (typ == RIN_MOOD)
					sprintf(buf, "%s ring", actualn);
				else
					sprintf(buf, "ring of %s", actualn);
			} else if (un)
				sprintf(buf, "ring called %s", un);
			else
				sprintf(buf, "%s ring", dn);
			break;
		case GEM_CLASS: {
			const char *rock =
				(ocl->oc_material == MINERAL) ? "stone" : "gem";
			if (!obj->dknown) {
				strcpy(buf, rock);
			} else if (!nn) {
				if (un)
					sprintf(buf, "%s called %s", rock, un);
				else
					sprintf(buf, "%s %s", dn, rock);
			} else {
				strcpy(buf, actualn);
				if (GemStone(typ)) strcat(buf, " stone");
			}
			break;
		}
		default:
			sprintf(buf, "glorkum %d %d %d", obj->oclass, typ, obj->spe);
	}
	if (pluralize) strcpy(buf, makeplural(buf));

	if (obj->onamelth && obj->dknown) {
		strcat(buf, " named ");
nameit:
		strcat(buf, ONAME(obj));
	}

	if (!strncmpi(buf, "the ", 4)) buf += 4;
	return buf;
} /* end Hallu */

/* WAC calls the above xname2 */
char *xname(struct obj *obj) {
	/* WAC moved hallucination here */
	struct obj *hobj;
	static char bufr[BUFSZ];
	char *buf = &(bufr[PREFIX]); /* leave room for "17 -3 " */

	if (Hallucination && !program_state.gameover) {
		hobj = mkobj(obj->oclass, 0);
		hobj->quan = obj->quan;
		/* WAC clean up */
		buf = xname2(hobj, false);

		if (Has_contents(hobj))
			delete_contents(hobj);

		obj_extract_self(hobj);
		dealloc_obj(hobj);

		return buf;
	} else {
		return xname2(obj, false);
	}
}

/* xname() output augmented for multishot missile feedback */
char *mshot_xname(struct obj *obj) {
	char tmpbuf[BUFSZ];
	char *onm = xname(obj);

	if (m_shot.n > 1 && m_shot.o == obj->otyp) {
		/* copy xname's result so that we can reuse its return buffer */
		strcpy(tmpbuf, onm);
		/* "the Nth arrow"; value will eventually be passed to an() or
		   The(), both of which correctly handle this "the " prefix */
		sprintf(onm, "the %d%s %s", m_shot.i, ordin(m_shot.i), tmpbuf);
	}

	return onm;
}

/* used for naming "the unique_item" instead of "a unique_item" */
boolean the_unique_obj(struct obj *obj) {
	if (!obj->dknown)
		return false;
	else if (obj->otyp == FAKE_AMULET_OF_YENDOR && !obj->known)
		return true; /* lie */
	else
		return (boolean)(objects[obj->otyp].oc_unique &&
				 (obj->known || obj->otyp == AMULET_OF_YENDOR));
}

static void add_erosion_words(struct obj *obj, char *prefix) {
	boolean iscrys = (obj->otyp == CRYSKNIFE);

	if ((!is_damageable(obj) && !iscrys) || Hallucination) return;

	/* The only cases where any of these bits do double duty are for
	 * rotted food and diluted potions, which are all not is_damageable().
	 */
	if (obj->oeroded && !iscrys) {
		switch (obj->oeroded) {
			case 2:
				strcat(prefix, "very ");
				break;
			case 3:
				strcat(prefix, "thoroughly ");
				break;
		}
		strcat(prefix, is_rustprone(obj) ? "rusty " : "burnt ");
	}
	if (obj->oeroded2 && !iscrys) {
		switch (obj->oeroded2) {
			case 2:
				strcat(prefix, "very ");
				break;
			case 3:
				strcat(prefix, "thoroughly ");
				break;
		}
		strcat(prefix, is_corrodeable(obj) ? "corroded " :
						     "rotted ");
	}
	if (obj->rknown && obj->oerodeproof)
		strcat(prefix,
		       iscrys ? "fixed " :
		       is_rustprone(obj) ? "rustproof " :
		       is_corrodeable(obj) ? "corrodeproof " : /* "stainless"? */
		       is_flammable(obj) ? "fireproof " : "");
}

char *doname_ext(struct obj *obj, bool keen) {
	boolean ispoisoned = false;
	char prefix[PREFIX];
	char tmpbuf[PREFIX + 1];
	/* when we have to add something at the start of prefix instead of the
	 * end (strcat is used on the end)
	 */
	char *bp = xname(obj);

	/* When using xname, we want "poisoned arrow", and when using
	 * doname, we want "poisoned +0 arrow".  This kludge is about the only
	 * way to do it, at least until someone overhauls xname() and doname(),
	 * combining both into one function taking a parameter.
	 */
	/* must check opoisoned--someone can have a weirdly-named fruit */
	if (!strncmp(bp, "poisoned ", 9) && obj->opoisoned) {
		bp += 9;
		ispoisoned = true;
	}

	if (obj->quan != 1L)
		sprintf(prefix, "%ld ", obj->quan);
	else if (!Hallucination && (obj_is_pname(obj) || the_unique_obj(obj))) {
		if (!strncmpi(bp, "the ", 4))
			bp += 4;
		strcpy(prefix, "the ");
	} else
		strcpy(prefix, "a ");

	if (obj->oinvis) strcat(prefix, "invisible ");

	// "empty" goes at the beginning, but item count goes at the end
	/* bag of tricks: include "empty" prefix if it's known to
	 * be empty but its precise number of charges isn't known
	 * (when that is known, or identification is keen, a suffix of
	 * "(n:0)" will be appended, making the prefix be redundant; note
	 * that 'known' flag isn't set when emptiness gets discovered because
	 * then charging magic would yield known number of new charges)
	 */
	if ((obj->otyp == BAG_OF_TRICKS) ? (!keen && obj->spe == 0 && !obj->known) :
	    (Is_container(obj) || obj->otyp == STATUE) && !Has_contents(obj)) {
		if (obj->cknown) strcat(prefix, "empty ");
		else if (keen) strcat(prefix, "[empty] ");
	}


	if ((wizard || keen) && is_hazy(obj)) strcat(prefix, "[hazy] ");

	if ((!Hallucination || Role_if(PM_PRIEST) || Role_if(PM_NECROMANCER)) &&
	    (obj->bknown || keen) &&
	    obj->oclass != COIN_CLASS &&
	    (obj->otyp != POT_WATER || !objects[POT_WATER].oc_name_known || (!obj->cursed && !obj->blessed) || Hallucination)) {
		/* allow 'blessed clear potion' if we don't know it's holy water;
		 * always allow "uncursed potion of water"
		 */
		if (Hallucination ? !rn2(10) : obj->cursed)
			strcat(prefix, obj->bknown ? "cursed " : "[cursed] ");
		else if (Hallucination ? !rn2(10) : obj->blessed)
			strcat(prefix, obj->bknown ? "blessed " : "[blessed] ");
		else if ((!obj->known || !objects[obj->otyp].oc_charged ||
			  (obj->oclass == ARMOR_CLASS ||
			   obj->oclass == RING_CLASS))
				/* For most items with charges or +/-, if you know how many
		                 * charges are left or what the +/- is, then you must have
		                 * totally identified the item, so "uncursed" is unneccesary,
		                 * because an identified object not described as "blessed" or
		                 * "cursed" must be uncursed.
		                 *
		                 * If the charges or +/- is not known, "uncursed" must be
		                 * printed to avoid ambiguity between an item whose curse
		                 * status is unknown, and an item known to be uncursed.
		                 */
#ifdef MAIL
			 && obj->otyp != SCR_MAIL
#endif
			 && obj->otyp != FAKE_AMULET_OF_YENDOR && obj->otyp != AMULET_OF_YENDOR && !Role_if(PM_PRIEST) && !Role_if(PM_NECROMANCER))
			strcat(prefix, obj->bknown ? "uncursed " : "[uncursed] ");
	}

	if ((obj->lknown || keen) && Is_box(obj)) {
		if (!obj->lknown) strcat(prefix, "[");

		if (obj->obroken) strcat(prefix, "unlockable");
		else if (obj->olocked) strcat(prefix, "locked");
		else strcat(prefix, "unlocked");

		if (!obj->lknown) strcat(prefix, "]");

		strcat(prefix, " ");
	}

	if (Hallucination ? !rn2(100) : obj->greased) strcat(prefix, "greased ");

	if ((obj->cknown || keen) && Has_contents(obj)) {
		struct obj *curr;
		long itemcount = 0L;

		// Count the number of contained objects
		for (curr = obj->cobj; curr; curr = curr->nobj)
			itemcount += curr->quan;

		sprintf(eos(bp), " %scontaining %ld item%s%s",
				obj->cknown ? "" : "[", itemcount, plur(itemcount), obj->cknown ? "" : "]");
	}



	switch (obj->oclass) {
		case AMULET_CLASS:
			add_erosion_words(obj, prefix);
			if (obj->owornmask & W_AMUL)
				strcat(bp, " (being worn)");
			break;
		case WEAPON_CLASS:
			if (ispoisoned)
				strcat(prefix, "poisoned ");
			else if (keen && obj->opoisoned)
				strcat(prefix, "[poisoned] ");
plus:
			add_erosion_words(obj, prefix);
			if (Hallucination)
				break;
			if (obj->known) {
				strcat(prefix, sitoa(obj->spe));
				strcat(prefix, " ");
			}
			if (is_lightsaber(obj) ||
			    obj->otyp == STICK_OF_DYNAMITE) {
				if (obj->lamplit) strcat(bp, " (lit)");
#ifdef DEBUG
				sprintf(eos(bp), " (%d)", obj->age);
#endif
			} else if (is_grenade(obj)) {
				if (obj->oarmed) strcat(bp, " (armed)");
			}
			break;
		case ARMOR_CLASS:
			if (obj->owornmask & W_ARMOR)
				strcat(bp, (obj == uskin) ? " (embedded in your skin)" :
							    " (being worn)");
			goto plus;
		case TOOL_CLASS:
			/* weptools already get this done when we go to the +n code */
			if (!is_weptool(obj))
				add_erosion_words(obj, prefix);
			if (Hallucination)
				break;
			// tool => blindfold
			if (obj->owornmask & (W_TOOL | W_SADDLE)) {
				strcat(bp, " (being worn)");
				break;
			}
			if (obj->otyp == LEASH && obj->leashmon != 0) {
				strcat(bp, " (in use)");
				break;
			}
			if (obj->otyp == CANDELABRUM_OF_INVOCATION) {
				if (!obj->spe)
					strcpy(tmpbuf, "no");
				else
					sprintf(tmpbuf, "%d", obj->spe);
				sprintf(eos(bp), " (%s candle%s%s)",
					tmpbuf, plur(obj->spe),
					!obj->lamplit ? " attached" : ", lit");
				break;
			} else if (obj->otyp == OIL_LAMP || obj->otyp == MAGIC_LAMP ||
				   obj->otyp == BRASS_LANTERN || obj->otyp == TORCH ||
				   Is_candle(obj)) {
				if (Is_candle(obj) &&
				    /* WAC - magic candles are never "partly used" */
				    obj->otyp != MAGIC_CANDLE &&
				    obj->age < 20L * (long)objects[obj->otyp].oc_cost)
					strcat(prefix, "partly used ");
				if (obj->lamplit)
					strcat(bp, " (lit)");
			}
			if (is_weptool(obj))
				goto plus;
			if (objects[obj->otyp].oc_charged)
				goto charges;
			break;
		case SPBOOK_CLASS: /* WAC spellbooks have charges now */
			if (wizard) {
				if (Hallucination)
					break;
				if (obj->known)
					sprintf(eos(bp), " (%d:%d,%d)",
						(int)obj->recharged, obj->spe, obj->spestudied);
				break;
			} else {
				goto charges;
			}
		case WAND_CLASS:
			add_erosion_words(obj, prefix);
		charges:
			if (Hallucination)
				break;
			if (obj->known)
				sprintf(eos(bp), " (%d:%d)", (int)obj->recharged, obj->spe);
			break;
		case POTION_CLASS:
			if (Hallucination)
				break;
			if (obj->otyp == POT_OIL && obj->lamplit)
				strcat(bp, " (lit)");
			break;
		case RING_CLASS:
			add_erosion_words(obj, prefix);
		ring:
			if (obj->owornmask & W_RINGR) strcat(bp, " (on right ");
			if (obj->owornmask & W_RINGL) strcat(bp, " (on left ");
			if (obj->owornmask & W_RING) {
				strcat(bp, body_part(HAND));
				strcat(bp, ")");
			}
			if (Hallucination)
				break;
			if (obj->known && objects[obj->otyp].oc_charged) {
				strcat(prefix, sitoa(obj->spe));
				strcat(prefix, " ");
			}
			break;
		case FOOD_CLASS:
			if (obj->otyp == CORPSE && obj->odrained) {
				if (wizard && obj->oeaten < drainlevel(obj))
					strcpy(tmpbuf, "over-drained ");
				else
					sprintf(tmpbuf, "%sdrained ",
						(obj->oeaten > drainlevel(obj)) ? "partly " : "");
			} else if (obj->oeaten) {
				strcpy(tmpbuf, "partly eaten ");
			} else {
				tmpbuf[0] = '\0';
			}
			strcat(prefix, tmpbuf);
			if (obj->otyp == CORPSE && !Hallucination) {
				if (mons[obj->corpsenm].geno & G_UNIQ) {
					sprintf(prefix, "%s%s ",
						(type_is_pname(&mons[obj->corpsenm]) ?
							 "" :
							 "the "),
						s_suffix(mons[obj->corpsenm].mname));
					strcat(prefix, tmpbuf);
				} else {
					strcat(prefix, mons[obj->corpsenm].mname);
					strcat(prefix, " ");
				}
			} else if (obj->otyp == EGG) {
#if 0 /* corpses don't tell if they're stale either */
			if (obj->known && stale_egg(obj))
				strcat(prefix, "stale ");
#endif
				if (obj->corpsenm >= LOW_PM &&
				    (obj->known ||
				     mvitals[obj->corpsenm].mvflags & MV_KNOWS_EGG)) {
					strcat(prefix, mons[obj->corpsenm].mname);
					strcat(prefix, " ");
					if (obj->spe == 2)
						strcat(bp, " (with your markings)");
					else if (obj->spe)
						strcat(bp, " (laid by you)");
				}
			}
			if (obj->otyp == MEAT_RING) goto ring;
			break;
		case BALL_CLASS:
		case CHAIN_CLASS:
			add_erosion_words(obj, prefix);
			if (obj->owornmask & W_BALL)
				strcat(bp, " (chained to you)");
			break;
	}
	if ((obj->owornmask & W_WEP) && !mrg_to_wielded) {
		if (obj->quan != 1L) {
			strcat(bp, " (wielded)");
		} else {
			const char *hand_s = body_part(HAND);

			if (bimanual(obj)) hand_s = makeplural(hand_s);
			sprintf(eos(bp), " (weapon in %s)", hand_s);
		}
	}
	if (obj->owornmask & W_SWAPWEP) {
		if (u.twoweap)
			sprintf(eos(bp), " (wielded in other %s)",
				body_part(HAND));
		else
			strcat(bp, " (alternate weapon; not wielded)");
	}
	if (obj->owornmask & W_QUIVER) strcat(bp, " (in quiver)");
	if (!Hallucination && obj->unpaid) {
		xchar ox, oy;
		long quotedprice = unpaid_cost(obj);
		struct monst *shkp = NULL;

		if (Has_contents(obj) &&
		    get_obj_location(obj, &ox, &oy, BURIED_TOO | CONTAINED_TOO) &&
		    costly_spot(ox, oy) &&
		    (shkp = shop_keeper(*in_rooms(ox, oy, SHOPBASE))))
			quotedprice += contained_cost(obj, shkp, 0L, false, true);
		sprintf(eos(bp), " (unpaid, %ld %s)",
			quotedprice, currency(quotedprice));
	}
	if (wizard && obj->in_use)	    /* Can't use "(in use)", see leashes */
		strcat(bp, " (finishing)"); /* always a bug */
	if (!strncmp(prefix, "a ", 2) &&
	    index(vowels, *(prefix + 2) ? *(prefix + 2) : *bp) && (*(prefix + 2) || (strncmp(bp, "uranium", 7) && strncmp(bp, "unicorn", 7) && strncmp(bp, "eucalyptus", 10)))) {
		strcpy(tmpbuf, prefix);
		strcpy(prefix, "an ");
		strcpy(prefix + 3, tmpbuf + 2);
	}

	/* [max] weight inventory */
	if ((obj->otyp != BOULDER) || !throws_rocks(youmonst.data))
		if ((obj->otyp < LUCKSTONE) && (obj->otyp != CHEST) && (obj->otyp != LARGE_BOX) &&
		    (obj->otyp != ICE_BOX) && (!Hallucination && flags.invweight))
			sprintf(eos(bp), " {%d}", obj->owt);

	bp = strprepend(bp, prefix);
	return bp;
}

char *doname(struct obj *obj) { return doname_ext(obj, false); }

/* used from invent.c */
bool not_fully_identified(struct obj *otmp) {
	/* gold doesn't have any interesting attributes [yet?] */
	if (otmp->oclass == COIN_CLASS) return false; /* always fully ID'd */

	/* check fundamental ID hallmarks first */
	if (!otmp->known || !otmp->dknown ||
#ifdef MAIL
	    (!otmp->bknown && otmp->otyp != SCR_MAIL) ||
#else
	    !otmp->bknown ||
#endif
	    !objects[otmp->otyp].oc_name_known) /* ?redundant? */
		return true;

	if ((!otmp->cknown && (Is_container(otmp) || otmp->otyp == STATUE)) ||
	    (!otmp->lknown && Is_box(otmp)))
		return true;

	if (otmp->oartifact && undiscovered_artifact(otmp->oartifact))
		return true;
	/* otmp->rknown is the only item of interest if we reach here */
	/*
	*  Note:  if a revision ever allows scrolls to become fireproof or
	*  rings to become shockproof, this checking will need to be revised.
	*  `rknown' ID only matters if xname() will provide the info about it.
	*/
	if (otmp->rknown || (otmp->oclass != ARMOR_CLASS &&
			     otmp->oclass != WEAPON_CLASS &&
			     !is_weptool(otmp) &&	  /* (redunant) */
			     otmp->oclass != BALL_CLASS)) /* (useless) */
		return false;
	else /* lack of `rknown' only matters for vulnerable objects */
		return is_rustprone(otmp) ||
		       is_corrodeable(otmp) ||
		       is_flammable(otmp);
}

char *corpse_xname(struct obj *otmp, boolean force_singular) {
	char *nambuf = nextobuf();

	int mndx = Hallucination ? rndmonnum() : otmp->corpsenm;
	const char *mname = (mndx != NON_PM) ? mons[mndx].mname :
                               "thing";  /* shouldn't happen */

	if (type_is_pname(&mons[mndx]) || mndx == PM_ORACLE) mname = s_suffix(mname);
	sprintf(nambuf, "%s corpse", mname);

	if (force_singular || otmp->quan < 2)
		return nambuf;
	else
		return makeplural(nambuf);
}

/* xname, unless it's a corpse, then corpse_xname(obj, false) */
char *cxname(struct obj *obj) {
	if (obj->otyp == CORPSE)
		return corpse_xname(obj, false);
	return xname(obj);
}

/* treat an object as fully ID'd when it might be used as reason for death */
char *killer_xname(struct obj *obj) {
	struct obj save_obj;
	unsigned save_ocknown;
	char *buf, *save_ocuname;

	/* remember original settings for core of the object;
	   oname and oattached extensions don't matter here--since they
	   aren't modified they don't need to be saved and restored */
	save_obj = *obj;
	/* killer name should be more specific than general xname; however, exact
	   info like blessed/cursed and rustproof makes things be too verbose */
	obj->known = obj->dknown = 1;
	obj->bknown = obj->rknown = obj->greased = 0;
	/* if character is a priest[ess], bknown will get toggled back on */
	obj->blessed = obj->cursed = 0;
	/* "killed by poisoned <obj>" would be misleading when poison is
	   not the cause of death and "poisoned by poisoned <obj>" would
	   be redundant when it is, so suppress "poisoned" prefix */
	obj->opoisoned = 0;
	/* strip user-supplied name; artifacts keep theirs */
	if (!obj->oartifact) obj->onamelth = 0;
	/* temporarily identify the type of object */
	save_ocknown = objects[obj->otyp].oc_name_known;
	objects[obj->otyp].oc_name_known = 1;
	save_ocuname = objects[obj->otyp].oc_uname;
	objects[obj->otyp].oc_uname = 0; /* avoid "foo called bar" */

	buf = xname2(obj, false);
	if (obj->quan == 1L) buf = obj_is_pname(obj) ? the(buf) : an(buf);

	objects[obj->otyp].oc_name_known = save_ocknown;
	objects[obj->otyp].oc_uname = save_ocuname;
	*obj = save_obj; /* restore object's core settings */

	return buf;
}

char *killer_cxname(struct obj *obj, boolean force_singular) {
	char *buf;
	if (obj->otyp == CORPSE) {
		buf = nextobuf();

		sprintf(buf, "%s%s corpse",
			Hallucination ? "hallucinogen-distorted " : "",
			mons[obj->corpsenm].mname);

		if (!force_singular && obj->quan >= 2)
			buf = makeplural(buf);
	} else
		buf = killer_xname(obj);
	return buf;
}

/*
 * Used if only one of a collection of objects is named (e.g. in eat.c).
 */
const char *singular(struct obj *otmp, char *(*func)(struct obj *)) {
	long savequan;
	unsigned saveowt;
	char *nam;

	/* Note: using xname for corpses will not give the monster type */
	if (otmp->otyp == CORPSE && func == xname && !Hallucination)
		return corpse_xname(otmp, true);

	savequan = otmp->quan;
	otmp->quan = 1L;

	saveowt = otmp->owt;
	otmp->owt = weight(otmp);

	nam = (*func)(otmp);
	otmp->quan = savequan;

	otmp->owt = saveowt;

	return nam;
}

char *an(const char *str) {
	char *buf = nextobuf();

	buf[0] = '\0';

	if (strncmpi(str, "the ", 4) &&
	    strcmp(str, "molten lava") &&
	    strcmp(str, "iron bars") &&
	    strcmp(str, "ice")) {
		if (index(vowels, *str) &&
		    strncmp(str, "one-", 4) &&
		    strncmp(str, "useful", 6) &&
		    strncmp(str, "unicorn", 7) &&
		    strncmp(str, "uranium", 7) &&
		    strncmp(str, "eucalyptus", 10))
			strcpy(buf, "an ");
		else
			strcpy(buf, "a ");
	}

	strcat(buf, str);
	return buf;
}

char *An(const char *str) {
	char *tmp = an(str);
	*tmp = highc(*tmp);
	return tmp;
}

/*
 * Prepend "the" if necessary; assumes str is a subject derived from xname.
 * Use type_is_pname() for monster names, not the().  the() is idempotent.
 */
char *the(const char *str) {
	char *buf = nextobuf();
	boolean insert_the = false;

	if (!strncmpi(str, "the ", 4)) {
		buf[0] = lowc(*str);
		strcpy(&buf[1], str + 1);
		return buf;
	} else if (*str < 'A' || *str > 'Z') {
		/* not a proper name, needs an article */
		insert_the = true;
	} else {
		/* Probably a proper name, might not need an article */
		char *tmp, *named, *called;
		int l;

		/* some objects have capitalized adjectives in their names */
		if (((tmp = rindex(str, ' ')) || (tmp = rindex(str, '-'))) &&
		    (tmp[1] < 'A' || tmp[1] > 'Z'))
			insert_the = true;
		else if (tmp && index(str, ' ') < tmp) { /* has spaces */
			/* it needs an article if the name contains "of" */
			tmp = strstri(str, " of ");
			named = strstri(str, " named ");
			called = strstri(str, " called ");
			if (called && (!named || called < named)) named = called;

			if (tmp && (!named || tmp < named)) /* found an "of" */
				insert_the = true;
			/* stupid special case: lacks "of" but needs "the" */
			else if (!named && (l = strlen(str)) >= 31 &&
				 !strcmp(&str[l - 31], "Platinum Yendorian Express Card"))
				insert_the = true;
		}
	}
	if (insert_the)
		strcpy(buf, "the ");
	else
		buf[0] = '\0';
	strcat(buf, str);

	return buf;
}

char *The(const char *str) {
	char *tmp = the(str);
	*tmp = highc(*tmp);
	return tmp;
}

/* returns "count cxname(otmp)" or just cxname(otmp) if count == 1 */
char *aobjnam(struct obj *otmp, const char *verb) {
	char *bp = cxname(otmp);
	char prefix[PREFIX];

	if (otmp->quan != 1L) {
		sprintf(prefix, "%ld ", otmp->quan);
		bp = strprepend(bp, prefix);
	}

	if (verb) {
		strcat(bp, " ");
		strcat(bp, otense(otmp, verb));
	}
	return bp;
}

/* combine yname and aobjnam eg "your count cxname(otmp)" */
char *yobjnam(struct obj *obj, const char *verb) {
	char *s = aobjnam(obj, verb);

	/* leave off "your" for most of your artifacts, but prepend
	 * "your" for unique objects and "foo of bar" quest artifacts */
	if (!carried(obj) || !obj_is_pname(obj) || obj->oartifact >= ART_ORB_OF_DETECTION) {
		char *outbuf = shk_your(nextobuf(), obj);
		int space_left = BUFSZ - 1 - strlen(outbuf);

		s = strncat(outbuf, s, space_left);
	}

	return s;
}

/* combine Yname2 and aobjnam eg "Your count cxname(otmp)" */
char *Yobjnam2(struct obj *obj, const char *verb) {
	char *s = yobjnam(obj,verb);

	*s = highc(*s);
	return s;
}


/* like aobjnam, but prepend "The", not count, and use xname */
char *Tobjnam(struct obj *otmp, const char *verb) {
	char *bp = The(xname(otmp));

	if (verb) {
		strcat(bp, " ");
		strcat(bp, otense(otmp, verb));
	}
	return bp;
}

/* return form of the verb (input plural) if xname(otmp) were the subject */
char *otense(struct obj *otmp, const char *verb) {
	char *buf;

	/*
	 * verb is given in plural (without trailing s).  Return as input
	 * if the result of xname(otmp) would be plural.  Don't bother
	 * recomputing xname(otmp) at this time.
	 */
	if (!is_plural(otmp))
		return vtense(NULL, verb);

	buf = nextobuf();
	strcpy(buf, verb);
	return buf;
}

/* various singular words that vtense would otherwise categorize as plural */
static const char *const special_subjs[] = {
	"erinys",
	"manes", /* this one is ambiguous */
	"Cyclops",
	"Hippocrates",
	"Pelias",
	"aklys",
	"amnesia",
	"paralysis",
	0};

/* return form of the verb (input plural) for present tense 3rd person subj */
char *
vtense(const char *subj, const char *verb) {
	char *buf = nextobuf();
	int len, ltmp;
	const char *sp, *spot;
	const char *const *spec;

	/*
	 * verb is given in plural (without trailing s).  Return as input
	 * if subj appears to be plural.  Add special cases as necessary.
	 * Many hard cases can already be handled by using otense() instead.
	 * If this gets much bigger, consider decomposing makeplural.
	 * Note: monster names are not expected here (except before corpse).
	 *
	 * special case: allow null sobj to get the singular 3rd person
	 * present tense form so we don't duplicate this code elsewhere.
	 */
	if (subj) {
		if (!strncmpi(subj, "a ", 2) || !strncmpi(subj, "an ", 3))
			goto sing;
		spot = NULL;
		for (sp = subj; (sp = index(sp, ' ')) != 0; ++sp) {
			if (!strncmp(sp, " of ", 4) ||
			    !strncmp(sp, " from ", 6) ||
			    !strncmp(sp, " called ", 8) ||
			    !strncmp(sp, " named ", 7) ||
			    !strncmp(sp, " labeled ", 9)) {
				if (sp != subj) spot = sp - 1;
				break;
			}
		}
		len = (int)strlen(subj);
		if (!spot) spot = subj + len - 1;

		/*
		 * plural: anything that ends in 's', but not '*us' or '*ss'.
		 * Guess at a few other special cases that makeplural creates.
		 */
		if ((*spot == 's' && spot != subj &&
		     (*(spot - 1) != 'u' && *(spot - 1) != 's')) ||
		    ((spot - subj) >= 4 && !strncmp(spot - 3, "eeth", 4)) ||
		    ((spot - subj) >= 3 && !strncmp(spot - 3, "feet", 4)) ||
		    ((spot - subj) >= 2 && !strncmp(spot - 1, "ia", 2)) ||
		    ((spot - subj) >= 2 && !strncmp(spot - 1, "ae", 2))) {
			/* check for special cases to avoid false matches */
			len = (int)(spot - subj) + 1;
			for (spec = special_subjs; *spec; spec++) {
				ltmp = strlen(*spec);
				if (len == ltmp && !strncmpi(*spec, subj, len)) goto sing;
				/* also check for <prefix><space><special_subj>
				   to catch things like "the invisible erinys" */
				if (len > ltmp && *(spot - ltmp) == ' ' &&
				    !strncmpi(*spec, spot - ltmp + 1, ltmp)) goto sing;
			}

			return strcpy(buf, verb);
		}
		/*
		 * 3rd person plural doesn't end in telltale 's';
		 * 2nd person singular behaves as if plural.
		 */
		if (!strcmpi(subj, "they") || !strcmpi(subj, "you"))
			return strcpy(buf, verb);
	}

sing:
	len = strlen(verb);
	spot = verb + len - 1;

	if (!strcmp(verb, "are"))
		strcpy(buf, "is");
	else if (!strcmp(verb, "have"))
		strcpy(buf, "has");
	else if (index("zxs", *spot) ||
		 (len >= 2 && *spot == 'h' && index("cs", *(spot - 1))) ||
		 (len == 2 && *spot == 'o')) {
		/* Ends in z, x, s, ch, sh; add an "es" */
		strcpy(buf, verb);
		strcat(buf, "es");
	} else if (*spot == 'y' && (!index(vowels, *(spot - 1)))) {
		/* like "y" case in makeplural */
		strcpy(buf, verb);
		strcpy(buf + len - 1, "ies");
	} else {
		strcpy(buf, verb);
		strcat(buf, "s");
	}

	return buf;
}

/* capitalized variant of doname() */
char *Doname2(struct obj *obj) {
	char *s = doname(obj);

	*s = highc(*s);
	return s;
}

/* returns "[your ]xname(obj)" or "Foobar's xname(obj)" or "the xname(obj)" */
char *yname(struct obj *obj) {
	char *s = cxname(obj);

	/* leave off "your" for most of your artifacts, but prepend
	 * "your" for unique objects and "foo of bar" quest artifacts */
	if (!carried(obj) || !obj_is_pname(obj) || obj->oartifact >= ART_ORB_OF_DETECTION) {
		char *outbuf = shk_your(nextobuf(), obj);
		int space_left = BUFSZ - 1 - strlen(outbuf);
		s = strncat(outbuf, s, space_left);
	}

	return s;
}

/* capitalized variant of yname() */
char *Yname2(struct obj *obj) {
	char *s = yname(obj);

	*s = highc(*s);
	return s;
}

/* returns "your simple_typename(obj->otyp)"
 * or "Foobar's simple_typename(obj->otyp)"
 * or "the simple_typename(obj-otyp)"
 */
char *ysimple_name(struct obj *obj) {
	char *outbuf = nextobuf();
	char *s = shk_your(outbuf, obj); /* assert( s == outbuf ); */
	int space_left = BUFSZ - 1 - strlen(s);

	return strncat(s, simple_typename(obj->otyp), space_left);
}

/* capitalized variant of ysimple_name() */
char *Ysimple_name2(struct obj *obj) {
	char *s = ysimple_name(obj);

	*s = highc(*s);
	return s;
}

static const char *wrp[] = {
	"wand", "ring", "potion", "scroll", "gem",
	"amulet", "spellbook", "spell book",
	/* for non-specific wishes */
	"weapon", "armor", "tool", "food", "comestible",
};
static const char wrpsym[] = {
	WAND_CLASS, RING_CLASS, POTION_CLASS, SCROLL_CLASS, GEM_CLASS,
	AMULET_CLASS, SPBOOK_CLASS, SPBOOK_CLASS,
	WEAPON_CLASS, ARMOR_CLASS, TOOL_CLASS, FOOD_CLASS,
	FOOD_CLASS};

/* Plural routine; chiefly used for user-defined fruits.  We have to try to
 * account for everything reasonable the player has; something unreasonable
 * can still break the code.  However, it's still a lot more accurate than
 * "just add an s at the end", which Rogue uses...
 *
 * Also used for plural monster names ("Wiped out all homunculi.")
 * and body parts.
 *
 * Also misused by muse.c to convert 1st person present verbs to 2nd person.
 */
char *makeplural(const char *oldstr) {
	/* Note: cannot use strcmpi here -- it'd give MATZot, CAVEMeN,... */
	char *spot;
	char *str = nextobuf();
	const char *excess = NULL;
	int len;

	while (*oldstr == ' ')
		oldstr++;
	if (!oldstr || !*oldstr) {
		impossible("plural of null?");
		strcpy(str, "s");
		return str;
	}
	strcpy(str, oldstr);

	/*
	 * Skip changing "pair of" to "pairs of".  According to Webster, usual
	 * English usage is use pairs for humans, e.g. 3 pairs of dancers,
	 * and pair for objects and non-humans, e.g. 3 pair of boots.  We don't
	 * refer to pairs of humans in this game so just skip to the bottom.
	 */
	if (!strncmp(str, "pair of ", 8))
		goto bottom;

	/* Search for common compounds, ex. lump of royal jelly */
	for (spot = str; *spot; spot++) {
		if (!strncmp(spot, " of ", 4) ||
		    !strncmp(spot, " labeled ", 9) ||
		    !strncmp(spot, " called ", 8) ||
		    !strncmp(spot, " named ", 7) ||
		    !strcmp(spot, " above") || // lurkers above
		    !strncmp(spot, " versus ", 8) ||
		    !strncmp(spot, " from ", 6) ||
		    !strncmp(spot, " in ", 4) ||
		    !strncmp(spot, " on ", 4) ||
		    !strncmp(spot, " a la ", 6) ||
		    !strncmp(spot, " with", 5) || // " with "?
		    !strncmp(spot, " de ", 4) ||
		    !strncmp(spot, " d'", 3) ||
		    !strncmp(spot, " du ", 4) ||
		    !strncmp(spot, "-in-", 4) ||
		    !strncmp(spot, "-at-", 4)) {
			excess = oldstr + (int)(spot - str);
			*spot = 0;
			break;
		}
	}
	spot--;
	while (*spot == ' ')
		spot--; /* Strip blanks from end */
	*(spot + 1) = 0;
	/* Now spot is the last character of the string */

	len = strlen(str);

	/* Single letters */
	if (len == 1 || !letter(*spot)) {
		strcpy(spot + 1, "'s");
		goto bottom;
	}

	/* Same singular and plural; mostly Japanese words except for "manes" */
	if ((len == 2 && !strcmp(str, "ya")) ||
	    (len >= 2 && !strcmp(spot - 1, "ai")) || /* samurai, Uruk-hai */
	    (len >= 3 && !strcmp(spot - 2, " ya")) ||
	    (len >= 4 &&
	     (!strcmp(spot - 3, "fish") || !strcmp(spot - 3, "tuna") ||
	      !strcmp(spot - 3, "deer") || !strcmp(spot - 3, "yaki") ||
	      !strcmp(spot - 3, "drow"))) ||
	    (len >= 5 && (!strcmp(spot - 4, "sheep") ||
			  !strcmp(spot - 4, "ninja") ||
			  !strcmp(spot - 4, "shito") ||
			  !strcmp(spot - 4, "tengu") ||
			  !strcmp(spot - 4, "manes"))) ||
	    (len >= 6 && (!strcmp(spot - 5, "ki-rin") ||
			  !strcmp(spot - 5, "Nazgul"))) ||
	    (len >= 7 && !strcmp(spot - 6, "gunyoki")) ||
	    (len >= 8 && !strcmp(spot - 7, "shuriken")))
		goto bottom;

	/* man/men ("Wiped out all cavemen.") */
	if (len >= 3 && !strcmp(spot - 2, "man") &&
	    (len < 6 || strcmp(spot - 5, "shaman")) &&
	    (len < 5 || strcmp(spot - 4, "human"))) {
		*(spot - 1) = 'e';
		goto bottom;
	}

	/* tooth/teeth */
	if (len >= 5 && !strcmp(spot - 4, "tooth")) {
		strcpy(spot - 3, "eeth");
		goto bottom;
	}

	/* knife/knives, etc... */
	if (!strcmp(spot - 1, "fe")) {
		strcpy(spot - 1, "ves");
		goto bottom;
	} else if (*spot == 'f') {
		if (index("lr", *(spot - 1)) || index(vowels, *(spot - 1))) {
			strcpy(spot, "ves");
			goto bottom;
		} else if (len >= 5 && !strncmp(spot - 4, "staf", 4)) {
			strcpy(spot - 1, "ves");
			goto bottom;
		}
	}

	/* foot/feet (body part) */
	if (len >= 4 && !strcmp(spot - 3, "foot")) {
		strcpy(spot - 2, "eet");
		goto bottom;
	}

	/* ium/ia (mycelia, baluchitheria) */
	if (len >= 3 && !strcmp(spot - 2, "ium")) {
		*(spot--) = (char)0;
		*spot = 'a';
		goto bottom;
	}

	/* algae, larvae, hyphae (another fungus part) */
	if ((len >= 4 && !strcmp(spot - 3, "alga")) ||
	    (len >= 5 &&
	     (!strcmp(spot - 4, "hypha") || !strcmp(spot - 4, "larva")))) {
		strcpy(spot, "ae");
		goto bottom;
	}

	/* fungus/fungi, homunculus/homunculi, but buses, lotuses, wumpuses */
	if (len > 3 && !strcmp(spot - 1, "us") &&
	    (len < 5 || (strcmp(spot - 4, "lotus") &&
			 (len < 6 || strcmp(spot - 5, "wumpus"))))) {
		*(spot--) = (char)0;
		*spot = 'i';
		goto bottom;
	}

	/* vortex/vortices */
	if (len >= 6 && !strcmp(spot - 3, "rtex")) {
		strcpy(spot - 1, "ices");
		goto bottom;
	}

	/* djinni/djinn (note: also efreeti/efreet) */
	if (len >= 6 && !strcmp(spot - 5, "djinni")) {
		*spot = (char)0;
		goto bottom;
	}

	/* mumak/mumakil */
	if (len >= 5 && !strcmp(spot - 4, "mumak")) {
		strcpy(spot + 1, "il");
		goto bottom;
	}

	/* sis/ses (nemesis) */
	if (len >= 3 && !strcmp(spot - 2, "sis")) {
		*(spot - 1) = 'e';
		goto bottom;
	}

	/* erinys/erinyes */
	if (len >= 6 && !strcmp(spot - 5, "erinys")) {
		strcpy(spot, "es");
		goto bottom;
	}

	/* mouse/mice,louse/lice (not a monster, but possible in food names) */
	if (len >= 5 && !strcmp(spot - 3, "ouse") && index("MmLl", *(spot - 4))) {
		strcpy(spot - 3, "ice");
		goto bottom;
	}

	/* matzoh/matzot, possible food name */
	if (len >= 6 && (!strcmp(spot - 5, "matzoh") || !strcmp(spot - 5, "matzah"))) {
		strcpy(spot - 1, "ot");
		goto bottom;
	}
	if (len >= 5 && (!strcmp(spot - 4, "matzo") || !strcmp(spot - 4, "matza"))) {
		strcpy(spot, "ot");
		goto bottom;
	}

	/* child/children (for wise guys who give their food funny names) */
	if (len >= 5 && !strcmp(spot - 4, "child")) {
		strcpy(spot, "dren");
		goto bottom;
	}

	/* note: -eau/-eaux (gateau, bordeau...) */
	/* note: ox/oxen, VAX/VAXen, goose/geese */

	/* Ends in z, x, s, ch, sh; add an "es" */
	if (index("zxs", *spot) || (len >= 2 && *spot == 'h' && index("cs", *(spot - 1)))
	    /* Kludge to get "tomatoes" and "potatoes" right */
	    || (len >= 4 && !strcmp(spot - 2, "ato"))) {
		strcpy(spot + 1, "es");
		goto bottom;
	}

	/* Ends in y preceded by consonant (note: also "qu") change to "ies" */
	if (*spot == 'y' &&
	    (!index(vowels, *(spot - 1)))) {
		strcpy(spot, "ies");
		goto bottom;
	}

	/* Default: append an 's' */
	strcpy(spot + 1, "s");

bottom:
	if (excess) strcpy(eos(str), excess);
	return str;
}

struct o_range {
	const char *name, oclass;
	int f_o_range, l_o_range;
};

/* wishable subranges of objects */
/* KMH, balance patch -- fixed */
static const struct o_range o_ranges[] = {
	{"bag",		TOOL_CLASS,	SACK,			BAG_OF_TRICKS},
	{"lamp",	TOOL_CLASS,	OIL_LAMP,		MAGIC_LAMP},
	{"candle",	TOOL_CLASS,	TALLOW_CANDLE,		MAGIC_CANDLE},
	{"horn",	TOOL_CLASS,	TOOLED_HORN,		HORN_OF_PLENTY},
	{"shield",	ARMOR_CLASS,	SMALL_SHIELD,		SHIELD_OF_REFLECTION},
	{"hat",		ARMOR_CLASS,	FEDORA,			DUNCE_CAP},
	{"helm",	ARMOR_CLASS,	ELVEN_LEATHER_HELM,	HELM_OF_TELEPATHY},
	{"gloves",	ARMOR_CLASS,	LEATHER_GLOVES,		GAUNTLETS_OF_DEXTERITY},
	{"gauntlets",	ARMOR_CLASS,	LEATHER_GLOVES,		GAUNTLETS_OF_DEXTERITY},
	{"boots",	ARMOR_CLASS,	LOW_BOOTS,		LEVITATION_BOOTS},
	{"shoes",	ARMOR_CLASS,	LOW_BOOTS,		IRON_SHOES},
	{"cloak",	ARMOR_CLASS,	MUMMY_WRAPPING,		CLOAK_OF_DISPLACEMENT},
	{"shirt",	ARMOR_CLASS,	HAWAIIAN_SHIRT,		T_SHIRT},
	{"dragon scales",ARMOR_CLASS,	GRAY_DRAGON_SCALES,	YELLOW_DRAGON_SCALES},
	{"dragon scale mail",ARMOR_CLASS,GRAY_DRAGON_SCALE_MAIL,YELLOW_DRAGON_SCALE_MAIL},
	{"sword",	WEAPON_CLASS,	ORCISH_SHORT_SWORD,	TSURUGI},
	{"polearm",	WEAPON_CLASS,	PARTISAN,		LANCE},
	{"lightsaber",	WEAPON_CLASS,	GREEN_LIGHTSABER,	RED_DOUBLE_LIGHTSABER},
	{"firearm",	WEAPON_CLASS,	PISTOL,			AUTO_SHOTGUN},
	{"gun",		WEAPON_CLASS,	PISTOL,			AUTO_SHOTGUN},
	{"grenade",	WEAPON_CLASS,	FRAG_GRENADE,		GAS_GRENADE},
	{"venom",	VENOM_CLASS,	BLINDING_VENOM,		ACID_VENOM},
	{"gray stone",	GEM_CLASS,	LUCKSTONE,		FLINT},
	{"grey stone",	GEM_CLASS,	LUCKSTONE,		FLINT},
};

#define BSTRCMP(base, ptr, string)	  ((ptr) < base || strcmp((ptr), string))
#define BSTRCMPI(base, ptr, string)	  ((ptr) < base || strcmpi((ptr), string))
#define BSTRNCMP(base, ptr, string, num)  ((ptr) < base || strncmp((ptr), string, num))
#define BSTRNCMPI(base, ptr, string, num) ((ptr) < base || strncmpi((ptr), string, num))

/*
 * Singularize a string the user typed in; this helps reduce the complexity
 * of readobjnam, and is also used in pager.c to singularize the string
 * for which help is sought.
 * WAC made most of the STRCMP ==> STRCMPI so that they are case insensitive
 * catching things like "bag of Tricks"
 */
char *makesingular(const char *oldstr) {
	char *p, *bp;
	char *str = nextobuf();

	if (!oldstr || !*oldstr) {
		impossible("singular of null?");
		str[0] = 0;
		return str;
	}
	strcpy(str, oldstr);
	bp = str;

	while (*bp == ' ')
		bp++;
	/* find "cloves of garlic", "worthless pieces of blue glass",
	 * "mothers-in-law"; note: this should probably be implemented
	 * recursively since it can't recognize whether we should be
	 * removing "es" rather than just "s" */
	if ((p = strstri(bp, " of ")) != 0 ||
	    (p = strstri(bp, "-in-")) != 0 ||
	    (p = strstri(bp, "-at-")) != 0) {
		// [wo]men-at-arms -> [wo]man-at-arms; takes "not end in s" exit
		if (!BSTRNCMP(bp, p-3, "men", 3)) *(p-2) = 'a';
		if (BSTRNCMP(bp, p-1, "s", 1)) return bp;   /* wasn't plural */
		--p;                /* back up to the 's' */
		/* but don't singularize "gauntlets", "boots", "Eyes of the.." */
		if (BSTRNCMPI(bp, p - 3, "Eye", 3) &&
		    BSTRNCMP(bp, p - 4, "boot", 4) &&
		    BSTRNCMP(bp, p - 8, "gauntlet", 8))
			while ((*p = *(p + 1)) != 0)
				p++;
		return bp;
	}

	/* remove -s or -es (boxes) or -ies (rubies) */
	p = eos(bp);
	if (p >= bp + 1 && p[-1] == 's') {
		if (p >= bp + 2 && p[-2] == 'e') {
			if (p >= bp + 3 && p[-3] == 'i') {
				if (!BSTRCMPI(bp, p - 7, "cookies") ||
				    !BSTRCMPI(bp, p - 4, "pies"))
					goto mins;
				strcpy(p - 3, "y");
				return bp;
			}

			/* note: cloves / knives from clove / knife */
			if (!BSTRCMPI(bp, p - 6, "knives")) {
				strcpy(p - 3, "fe");
				return bp;
			}
			if (!BSTRCMPI(bp, p - 6, "staves")) {
				strcpy(p - 3, "ff");
				return bp;
			}
			if (!BSTRCMPI(bp, p - 6, "leaves")) {
				strcpy(p - 3, "f");
				return bp;
			}
			if (!BSTRCMP(bp, p - 8, "vortices")) {
				strcpy(p - 4, "ex");
				return bp;
			}

			/* note: nurses, axes but boxes */
			if (!BSTRCMP(bp, p - 5, "boxes") ||
			    !BSTRCMP(bp, p - 4, "ches")) {
				p[-2] = 0;
				return bp;
			}

			if (!BSTRCMPI(bp, p - 6, "gloves") ||
			    !BSTRCMP(bp, p - 6, "lenses") ||
			    !BSTRCMPI(bp, p - 5, "shoes") ||
			    !BSTRCMPI(bp, p - 6, "scales"))
				return bp;
		} else if (!BSTRCMP(bp, p-2, "us")) { /* lotus, fungus... */
			if (BSTRCMP(bp, p-6, "tengus") && /* but not these... */
			    BSTRCMP(bp, p-7, "hezrous"))
				return bp;
		} else if (!BSTRCMPI(bp, p - 5, "boots") ||
			   !BSTRCMP(bp, p - 9, "gauntlets") ||
			   !BSTRCMPI(bp, p - 6, "tricks") ||
			   !BSTRCMPI(bp, p - 9, "paralysis") ||
			   !BSTRCMPI(bp, p - 5, "glass") ||
			   !BSTRCMPI(bp, p - 4, "ness") ||
			   !BSTRCMPI(bp, p - 14, "shape changers") ||
			   !BSTRCMPI(bp, p - 15, "detect monsters") ||
			   !BSTRCMPI(bp, p - 21, "Medallion of Shifters") ||
			   /* WAC added */
			   !BSTRCMPI(bp, p - 12, "Key of Chaos") ||
			   !BSTRCMP(bp, p - 9, "iron bars") ||
			   !BSTRCMP(bp, p - 5, "aklys"))
			return bp;
	mins:
		p[-1] = 0;

	} else {
		if (!BSTRCMPI(bp, p-5, "teeth")) {
			strcpy(p-5, "tooth");
			return bp;
		}

		if (!BSTRCMP(bp, p-5, "fungi")) {
			strcpy(p-5, "fungus");
			return bp;
		}

		if (!BSTRCMP(bp, p-3, "men")) {
			strcpy(p-3, "man");
			return bp;
		}

		/* here we cannot find the plural suffix */
	}
	return bp;
}

// compare user string against object name string using fuzzy matching
/* u_str is from the user, so it might have variant spelling; o_str is from
 * objects[], so is in canonical form
 */
// retry_inverted => optional extra "of" handling
static boolean wishymatch(const char *u_str, const char *o_str, boolean retry_inverted) {
	/* special case: wizards can wish for traps.  The object is "beartrap"
	 * and the trap is "bear trap", so to let wizards wish for both we
	 * must not fuzzymatch.
	 */
	if (wizard && !strcmp(o_str, "beartrap"))
		return !strncmpi(o_str, u_str, 8);

	/* ignore spaces & hyphens and upper/lower case when comparing */
	if (fuzzymatch(u_str, o_str, " -", true)) return true;

	if (retry_inverted) {
		const char *u_of, *o_of;
		char *p, buf[BUFSZ];

		/* when just one of the strings is in the form "foo of bar",
		   convert it into "bar foo" and perform another comparison */
		u_of = strstri(u_str, " of ");
		o_of = strstri(o_str, " of ");
		if (u_of && !o_of) {
			strcpy(buf, u_of + 4);
			p = eos(strcat(buf, " "));
			while (u_str < u_of)
				*p++ = *u_str++;
			*p = '\0';
			return fuzzymatch(buf, o_str, " -", true);
		} else if (o_of && !u_of) {
			strcpy(buf, o_of + 4);
			p = eos(strcat(buf, " "));
			while (o_str < o_of)
				*p++ = *o_str++;
			*p = '\0';
			return fuzzymatch(u_str, buf, " -", true);
		}
	}

	/* [note: if something like "elven speed boots" ever gets added, these
	   special cases should be changed to call wishymatch() recursively in
	   order to get the "of" inversion handling] */
	if (!strncmp(o_str, "dwarvish ", 9)) {
		if (!strncmpi(u_str, "dwarven ", 8))
			return fuzzymatch(u_str + 8, o_str + 9, " -", true);
	} else if (!strncmp(o_str, "elven ", 6)) {
		if (!strncmpi(u_str, "elvish ", 7))
			return fuzzymatch(u_str + 7, o_str + 6, " -", true);
		else if (!strncmpi(u_str, "elfin ", 6))
			return fuzzymatch(u_str + 6, o_str + 6, " -", true);
	} else if (!strcmp(o_str, "aluminum")) {
		/* this special case doesn't really fit anywhere else... */
		/* (note that " wand" will have been stripped off by now) */
		if (!strcmpi(u_str, "aluminium"))
			return fuzzymatch(u_str + 9, o_str + 8, " -", true);
	}

	return false;
}

/* alternate spellings; if the difference is only the presence or
   absence of spaces and/or hyphens (such as "pickaxe" vs "pick axe"
   vs "pick-axe") then there is no need for inclusion in this list;
   likewise for ``"of" inversions'' ("boots of speed" vs "speed boots") */
struct alt_spellings {
	const char *sp;
	int ob;
} spellings[] = {
	{"pickax", PICK_AXE},
	{"whip", BULLWHIP},
	{"saber", SILVER_SABER},
	{"silver sabre", SILVER_SABER},
	{"smooth shield", SHIELD_OF_REFLECTION},
	{"grey dragon scale mail", GRAY_DRAGON_SCALE_MAIL},
	{"grey dragon scales", GRAY_DRAGON_SCALES},
	{"iron ball", HEAVY_IRON_BALL},
	{"lantern", BRASS_LANTERN},
	{"mattock", DWARVISH_MATTOCK},
	{"amulet of poison resistance", AMULET_VERSUS_POISON},
	{"potion of sleep", POT_SLEEPING},
	{"stone", ROCK},
	{"can", TIN},
	{"can opener", TIN_OPENER},
	{"kelp", KELP_FROND},
	{"eucalyptus", EUCALYPTUS_LEAF},
	{"hook", GRAPPLING_HOOK},
	{"grappling iron", GRAPPLING_HOOK},
	{"grapnel", GRAPPLING_HOOK},
	{"grapple", GRAPPLING_HOOK},
	{"amulet versus stoning", AMULET_VERSUS_STONE},
	{"amulet of stone resistance", AMULET_VERSUS_STONE},
	{"health stone", HEALTHSTONE},
	{"handgun", PISTOL},
	{"hand gun", PISTOL},
	{"revolver", PISTOL},
	{"bazooka", ROCKET_LAUNCHER},
	{"hand grenade", FRAG_GRENADE},
	{"dynamite", STICK_OF_DYNAMITE},
#ifdef ZOUTHERN
	{"kiwifruit", APPLE},
	{"kiwi fruit", APPLE},
	{"kiwi", APPLE}, /* Actually refers to the bird */
#endif
	/* KMH, balance patch -- How lazy are we going to let the players get? */
	/* WAC Added Abbreviations */
	/* Tools */
	{"BoH", BAG_OF_HOLDING},
	{"BoO", BELL_OF_OPENING},
	{"ML", MAGIC_LAMP},
	{"MM", MAGIC_MARKER},
	{"UH", UNICORN_HORN},
	/* Rings */
	{"RoC", RIN_CONFLICT},
	{"RoPC", RIN_POLYMORPH_CONTROL},
	{"RoTC", RIN_TELEPORT_CONTROL},
	/* Scrolls */
	{"SoC", SCR_CHARGING},
	{"SoEA", SCR_ENCHANT_ARMOR},
	{"SoEW", SCR_ENCHANT_WEAPON},
	{"SoG", SCR_GENOCIDE},
	{"SoI", SCR_IDENTIFY},
	{"SoRC", SCR_REMOVE_CURSE},
	/* Potions */
	{"PoEH", POT_EXTRA_HEALING},
	{"PoGL", POT_GAIN_LEVEL},
	{"PoW", POT_WATER},
	/* Amulet */
	{"AoESP", AMULET_OF_ESP},
	{"AoLS", AMULET_OF_LIFE_SAVING},
	{"AoY", AMULET_OF_YENDOR},
	/* Wands */
	{"WoW", WAN_WISHING},
	{"WoCM", WAN_CREATE_MONSTER},
	{"WoT", WAN_TELEPORTATION},
	{"WoUT", WAN_UNDEAD_TURNING},
	/* Armour */
	{"BoL", LEVITATION_BOOTS},
	{"BoS", SPEED_BOOTS},
	{"SB", SPEED_BOOTS},
	{"BoWW", WATER_WALKING_BOOTS},
	{"WWB", WATER_WALKING_BOOTS},
	{"CoD", CLOAK_OF_DISPLACEMENT},
	{"CoI", CLOAK_OF_INVISIBILITY},
	{"CoMR", CLOAK_OF_MAGIC_RESISTANCE},
	{"GoD", GAUNTLETS_OF_DEXTERITY},
	{"GoP", GAUNTLETS_OF_POWER},
	{"HoB", HELM_OF_BRILLIANCE},
	{"HoOA", HELM_OF_OPPOSITE_ALIGNMENT},
	{"HoT", HELM_OF_TELEPATHY},
	{"SoR", SHIELD_OF_REFLECTION},
	{"camera", EXPENSIVE_CAMERA},
	{"T shirt", T_SHIRT},
	{"tee shirt", T_SHIRT},
	{NULL, 0},
};

/*
 * Return something wished for.  Specifying a null pointer for
 * the user request string results in a random object.  Otherwise,
 * if asking explicitly for "nothing" (or "nil") return no_wish;
 * if not an object return &zeroobj; if an error (no matching object),
 * return null.
 */
struct obj *readobjnam(char *bp, struct obj *no_wish) {
	char *p;
	int i;
	struct obj *otmp;
	int cnt, spe, spesgn, typ, very, rechrg;
	int blessed, uncursed, iscursed, ispoisoned, isgreased, isdrained;
	int eroded, eroded2, erodeproof;
	int isinvisible;
	int halfeaten, halfdrained, mntmp, contents;
	int islit, unlabeled, ishistoric, isdiluted, trapped;
	int tmp, tinv, tvariety = RANDOM_TIN;
	struct fruit *f;
	int ftype = current_fruit;
	char fruitbuf[BUFSZ];
	/* Fruits may not mess up the ability to wish for real objects (since
	 * you can leave a fruit in a bones file and it will be added to
	 * another person's game), so they must be checked for last, after
	 * stripping all the possible prefixes and seeing if there's a real
	 * name in there.  So we have to save the full original name.  However,
	 * it's still possible to do things like "uncursed burnt Alaska",
	 * or worse yet, "2 burned 5 course meals", so we need to loop to
	 * strip off the prefixes again, this time stripping only the ones
	 * possible on food.
	 * We could get even more detailed so as to allow food names with
	 * prefixes that _are_ possible on food, so you could wish for
	 * "2 3 alarm chilis".  Currently this isn't allowed; options.c
	 * automatically sticks 'candied' in front of such names.
	 */

	char oclass;
	char *un, *dn, *actualn;
	const char *name = 0;

	cnt = spe = spesgn = typ = very = rechrg =
	blessed = uncursed = iscursed = isdrained = halfdrained =
	isinvisible = ispoisoned = isgreased = eroded = eroded2 =
	erodeproof = halfeaten = islit = unlabeled = ishistoric = isdiluted = trapped = 0;

	mntmp = NON_PM;
#define UNDEFINED 0
#define EMPTY	  1
#define SPINACH	  2
	contents = UNDEFINED;
	oclass = 0;
	actualn = dn = un = 0;

	if (!bp) goto any;
	/* first, remove extra whitespace they may have typed */
	mungspaces(bp);
	/* allow wishing for "nothing" to preserve wishless conduct...
	   [now requires "wand of nothing" if that's what was really wanted] */
	if (!strcmpi(bp, "nothing") || !strcmpi(bp, "nil") ||
	    !strcmpi(bp, "none")) return no_wish;
	/* save the [nearly] unmodified choice string */
	strcpy(fruitbuf, bp);

	for (;;) {
		int l;

		if (!bp || !*bp) goto any;
		if (!strncmpi(bp, "an ", l=3) ||
		    !strncmpi(bp, "a ", l=2)) {
			cnt = 1;
		} else if (!strncmpi(bp, "the ", l=4)) {
			; /* just increment `bp' by `l' below */
		} else if (!cnt && digit(*bp) && strcmp(bp, "0")) {
			cnt = atoi(bp);
			while (digit(*bp))
				bp++;
			while (*bp == ' ')
				bp++;
			l = 0;
		} else if (*bp == '+' || *bp == '-') {
			spesgn = (*bp++ == '+') ? 1 : -1;
			spe = atoi(bp);
			while (digit(*bp))
				bp++;
			while (*bp == ' ')
				bp++;
			l = 0;
		} else if (!strncmpi(bp, "blessed ", l=8)
			   /*WAC removed this.  Holy is in some artifact weapon names
		                                || !strncmpi(bp, "holy ", l=5)
		                */
		) {
			blessed = 1;
		} else if (!strncmpi(bp, "cursed ", l=7) ||
			   !strncmpi(bp, "unholy ", l=7)) {
			iscursed = 1;
		} else if (!strncmpi(bp, "uncursed ", l=9)) {
			uncursed = 1;
		} else if (!strncmpi(bp, "visible ", l=8)) {
			isinvisible = -1;
		} else if (!strncmpi(bp, "invisible ", l=10)) {
			isinvisible = 1;
		} else if (!strncmpi(bp, "rustproof ", l=10) ||
			   !strncmpi(bp, "erodeproof ", l=11) ||
			   !strncmpi(bp, "corrodeproof ", l=13) ||
			   !strncmpi(bp, "fixed ", l=6) ||
			   !strncmpi(bp, "fireproof ", l=10) ||
			   !strncmpi(bp, "rotproof ", l=9)) {
			erodeproof = 1;
		} else if (!strncmpi(bp, "lit ", l=4) ||
			   !strncmpi(bp, "burning ", l=8)) {
			islit = 1;
		} else if (!strncmpi(bp, "unlit ", l=6) ||
			   !strncmpi(bp, "extinguished ", l=13)) {
			islit = 0;
			/* "unlabeled" and "blank" are synonymous */
		} else if (!strncmpi(bp, "unlabeled ", l=10) ||
			   !strncmpi(bp, "unlabelled ", l=11) ||
			   !strncmpi(bp, "blank ", l=6)) {
			unlabeled = 1;
		} else if (!strncmpi(bp, "poisoned ", l=9)) {
			ispoisoned = 1;
		} else if (!strncmpi(bp, "trapped ", l=8)) {
			trapped = 0;
			if (wizard) trapped = 1;
		} else if (!strncmpi(bp, "untrapped ", l=10)) {
			trapped = 2; // not trapped
		} else if (!strncmpi(bp, "greased ", l=8)) {
			isgreased = 1;
		} else if (!strncmpi(bp, "very ", l=5)) {
			/* very rusted very heavy iron ball */
			very = 1;
		} else if (!strncmpi(bp, "thoroughly ", l=11)) {
			very = 2;
		} else if (!strncmpi(bp, "rusty ", l=6) ||
			   !strncmpi(bp, "rusted ", l=7) ||
			   !strncmpi(bp, "burnt ", l=6) ||
			   !strncmpi(bp, "burned ", l=7)) {
			eroded = 1 + very;
			very = 0;
		} else if (!strncmpi(bp, "corroded ", l=9) ||
			   !strncmpi(bp, "rotted ", l=7)) {
			eroded2 = 1 + very;
			very = 0;
		} else if (!strncmpi(bp, "partly drained ", l=15)) {
			isdrained = 1;
			halfdrained = 1;
		} else if (!strncmpi(bp, "drained ", l=8)) {
			isdrained = 1;
			halfdrained = 0;
		} else if (!strncmpi(bp, "partly eaten ", l=13) ||
			   !strncmpi(bp, "partially eaten ", l=16)) {
			halfeaten = 1;
		} else if (!strncmpi(bp, "historic ", l=9)) {
			ishistoric = 1;
		} else if (!strncmpi(bp, "diluted ", l=8)) {
			isdiluted = 1;
		} else if (!strncmpi(bp, "empty ", l=6)) {
			contents = EMPTY;
		} else
			break;
		bp += l;
	}
	if (!cnt) cnt = 1; /* %% what with "gems" etc. ? */
	if (strlen(bp) > 1) {
		if ((p = rindex(bp, '(')) != 0) {
			if (p > bp && p[-1] == ' ')
				p[-1] = 0;
			else
				*p = 0;
			p++;
			if (!strcmpi(p, "lit)")) {
				islit = 1;
			} else {
				spe = atoi(p);
				while (digit(*p))
					p++;
				if (*p == ':') {
					p++;
					rechrg = spe;
					spe = atoi(p);
					while (digit(*p))
						p++;
				}
				if (*p != ')') {
					spe = rechrg = 0;
				} else {
					spesgn = 1;
					p++;
					if (*p) strcat(bp, p);
				}
			}
		}
	}
	/*
	   otmp->spe is type schar; so we don't want spe to be any bigger or smaller.
	   also, spe should always be positive  -- some cheaters may try to confuse
	   atoi()
	*/
	if (spe < 0) {
		spesgn = -1; /* cheaters get what they deserve */
		spe = abs(spe);
	}
	if (spe > SCHAR_LIM)
		spe = SCHAR_LIM;
	if (rechrg < 0 || rechrg > 7) rechrg = 7; /* recharge_limit */

	/* now we have the actual name, as delivered by xname, say
		green potions called whisky
		scrolls labeled "QWERTY"
		egg
		fortune cookies
		very heavy iron ball named hoei
		wand of wishing
		elven cloak
	*/
	if ((p = strstri(bp, " named ")) != 0) {
		*p = 0;
		name = p + 7;
	}
	if ((p = strstri(bp, " called ")) != 0) {
		*p = 0;
		un = p + 8;
		/* "helmet called telepathy" is not "helmet" (a specific type)
		 * "shield called reflection" is not "shield" (a general type)
		 */
		for (i = 0; i < SIZE(o_ranges); i++)
			if (!strcmpi(bp, o_ranges[i].name)) {
				oclass = o_ranges[i].oclass;
				goto srch;
			}
	}
	if ((p = strstri(bp, " labeled ")) != 0) {
		*p = 0;
		dn = p + 9;
	} else if ((p = strstri(bp, " labelled ")) != 0) {
		*p = 0;
		dn = p + 10;
	}
	if ((p = strstri(bp, " of spinach")) != 0) {
		*p = 0;
		contents = SPINACH;
	}

	/*
	Skip over "pair of ", "pairs of", "set of" and "sets of".

	Accept "3 pair of boots" as well as "3 pairs of boots". It is valid
	English either way.  See makeplural() for more on pair/pairs.

	We should only double count if the object in question is not
	refered to as a "pair of".  E.g. We should double if the player
	types "pair of spears", but not if the player types "pair of
	lenses".  Luckily (?) all objects that are refered to as pairs
	-- boots, gloves, and lenses -- are also not mergable, so cnt is
	ignored anyway.
	*/
	if (!strncmpi(bp, "pair of ", 8)) {
		bp += 8;
		cnt *= 2;
	} else if (cnt > 1 && !strncmpi(bp, "pairs of ", 9)) {
		bp += 9;
		cnt *= 2;
	} else if (!strncmpi(bp, "set of ", 7)) {
		bp += 7;
	} else if (!strncmpi(bp, "sets of ", 8)) {
		bp += 8;
	}

	/*
	 * Find corpse type using "of" (figurine of an orc, tin of orc meat)
	 * Don't check if it's a wand or spellbook.
	 * (avoid "wand/finger of death" confusion).
	 * (WAC avoid "hand/eye of vecna", "wallet of perseus"
	 *  "medallion of shifters", "stake of van helsing" similarly
	 *  ALI "potion of vampire blood" also).
	 */
	if (!strstri(bp, "wand ")
	&& !strstri(bp, "spellbook ")
	&& !strstri(bp, "hand ")
	&& !strstri(bp, "eye ")
	&& !strstri(bp, "medallion ")
	&& !strstri(bp, "stake ")
	&& !strstri(bp, "potion ")
	&& !strstri(bp, "potions ")
	&& !strstri(bp, "finger ")) {
		if (((p = strstri(bp, "tin of ")) != 0) &&
		    (tmp = tin_variety_txt(p+7, &tinv)) && (mntmp = name_to_mon(p+7+tmp)) >= LOW_PM) {
			*(p+3) = 0;
			tvariety = tinv;
		} else if ((p = strstri(bp, " of ")) != 0 && (mntmp = name_to_mon(p + 4)) >= LOW_PM) {
			*p = 0;
		}
	}

	/* Find corpse type w/o "of" (red dragon scale mail, yeti corpse) */
	if (strncmpi(bp, "samurai sword", 13))	/* not the "samurai" monster! */
	if (strncmpi(bp, "wizard lock", 11))	/* not the "wizard" monster! */
	if (strncmpi(bp, "ninja-to", 8))	/* not the "ninja" rank */
	if (strncmpi(bp, "master key", 10))	/* not the "master" rank */
	if (strncmpi(bp, "magenta", 7))		/* not the "mage" rank */
	if (strncmpi(bp, "Thiefbane", 9))	/* not the "thief" rank */
	if (strncmpi(bp, "Ogresmasher", 11))	/* not the "ogre" monster */
	if (strncmpi(bp, "Bat from Hell", 13))	/* not the "bat" monster */
	if (strncmpi(bp, "vampire blood", 13))	/* not the "vampire" monster */
		if (mntmp < LOW_PM && strlen(bp) > 2 && (mntmp = name_to_mon(bp)) >= LOW_PM) {
			int mntmptoo, mntmplen; /* double check for rank title */
			char *obp = bp;
			mntmptoo = title_to_mon(bp, NULL, &mntmplen);
			bp += mntmp != mntmptoo ? (int)strlen(mons[mntmp].mname) : mntmplen;
			if (*bp == ' ')
				bp++;
			else if (!strncmpi(bp, "s ", 2))
				bp += 2;
			else if (!strncmpi(bp, "es ", 3))
				bp += 3;
			else if (!*bp && !actualn && !dn && !un && !oclass) {
				/* no referent; they don't really mean a monster type */
				bp = obp;
				mntmp = NON_PM;
			}
		}

	/* first change to singular if necessary */
	if (*bp) {
		char *sng = makesingular(bp);
		if (strcmp(bp, sng)) {
			if (cnt == 1) cnt = 2;
			strcpy(bp, sng);
		}
	}

	/* Alternate spellings (pick-ax, silver sabre, &c) */
	{
		struct alt_spellings *as = spellings;

		while (as->sp) {
			if (fuzzymatch(bp, as->sp, " -", true)) {
				typ = as->ob;
				goto typfnd;
			}
			as++;
		}
		/* can't use spellings list for this one due to shuffling */
		if (!strncmpi(bp, "grey spell", 10))
			*(bp + 2) = 'a';

		if ((p = strstri(bp, "armour")) != 0) {
			// skip past "armo", then copy remainer beyond "u"
			p += 4;
			while ((*p = *(p + 1)) != '\0') ++p;	// self terminating
		}

	}

	/* dragon scales - assumes order of dragons */
	if (!strcmpi(bp, "scales") &&
	    mntmp >= PM_GRAY_DRAGON && mntmp <= PM_YELLOW_DRAGON) {
		typ = GRAY_DRAGON_SCALES + mntmp - PM_GRAY_DRAGON;
		mntmp = NON_PM; /* no monster */
		goto typfnd;
	}

	p = eos(bp);
	if (!BSTRCMPI(bp, p - 10, "holy water")) {
		typ = POT_WATER;
		if ((p - bp) >= 12 && *(p - 12) == 'u')
			iscursed = 1; /* unholy water */
		else
			blessed = 1;
		goto typfnd;
	}
	if (unlabeled && !BSTRCMPI(bp, p - 6, "scroll")) {
		typ = SCR_BLANK_PAPER;
		goto typfnd;
	}
	if (unlabeled && !BSTRCMPI(bp, p - 9, "spellbook")) {
		typ = SPE_BLANK_PAPER;
		goto typfnd;
	}
	/*
	 * NOTE: Gold pieces are handled as objects nowadays, and therefore
	 * this section should probably be reconsidered as well as the entire
	 * gold/money concept.  Maybe we want to add other monetary units as
	 * well in the future. (TH)
	 */
	if (!BSTRCMPI(bp, p - 10, "gold piece") || !BSTRCMPI(bp, p - 7, "zorkmid") ||
	    !strcmpi(bp, "gold") || !strcmpi(bp, "money") ||
	    !strcmpi(bp, "coin") || *bp == GOLD_SYM) {
		if (cnt > 5000 && !wizard) cnt = 5000;
		if (cnt < 1) cnt = 1;
		otmp = mksobj(GOLD_PIECE, false, false);
		otmp->quan = cnt;
		otmp->owt = weight(otmp);
		context.botl = 1;
		return otmp;
	}
	if (strlen(bp) == 1 &&
	    (i = def_char_to_objclass(*bp)) < MAXOCLASSES && i > ILLOBJ_CLASS && (wizard || i != VENOM_CLASS)) {
		oclass = i;
		goto any;
	}

	/* Search for class names: XXXXX potion, scroll of XXXXX.  Avoid */
	/* false hits on, e.g., rings for "ring mail". */
	/* false hits on "GrayWAND", "Staff of WitheRING"  -- WAC */
	if (strncmpi(bp, "enchant ", 8) &&
	    strncmpi(bp, "destroy ", 8) &&
	    strncmpi(bp, "food detection", 14) &&
	    strncmpi(bp, "ring mail", 9) &&
	    strncmpi(bp, "studded leather armor", 21) &&
	    strncmpi(bp, "leather armor", 13) &&
	    strncmpi(bp, "tooled horn", 11) &&
	    strncmpi(bp, "graywand", 8) &&
	    strncmpi(bp, "staff of withering", 18) &&
	    strncmpi(bp, "one ring", 8) &&
	    strncmpi(bp, "food ration", 11) &&
	    strncmpi(bp, "meat ring", 9))
		for (i = 0; i < (int)(sizeof wrpsym); i++) {
			int j = strlen(wrp[i]);
			if (!strncmpi(bp, wrp[i], j)) {
				oclass = wrpsym[i];
				if (oclass != AMULET_CLASS) {
					bp += j;
					if (!strncmpi(bp, " of ", 4)) actualn = bp + 4;
					/* else if(*bp) ?? */
				} else
					actualn = bp;
				goto srch;
			}
			if (!BSTRCMPI(bp, p - j, wrp[i])) {
				oclass = wrpsym[i];
				p -= j;
				*p = 0;
				if (p > bp && p[-1] == ' ') p[-1] = 0;
				actualn = dn = bp;
				goto srch;
			}
		}

retry:
	/* "grey stone" check must be before general "stone" */
	for (i = 0; i < SIZE(o_ranges); i++)
		if (!strcmpi(bp, o_ranges[i].name)) {
			typ = rnd_class(o_ranges[i].f_o_range, o_ranges[i].l_o_range);
			goto typfnd;
		}

	if (!BSTRCMPI(bp, p - 6, " stone")) {
		p[-6] = 0;
		oclass = GEM_CLASS;
		dn = actualn = bp;
		goto srch;
	} else if (!strcmpi(bp, "looking glass")) {
		; /* avoid false hit on "* glass" */
	} else if (!BSTRCMPI(bp, p - 6, " glass") || !strcmpi(bp, "glass")) {
		char *g = bp;
		if (strstri(g, "broken")) return NULL;
		if (!strncmpi(g, "worthless ", 10)) g += 10;
		if (!strncmpi(g, "piece of ", 9)) g += 9;
		if (!strncmpi(g, "colored ", 8))
			g += 8;
		else if (!strncmpi(g, "coloured ", 9))
			g += 9;
		if (!strcmpi(g, "glass")) { /* choose random color */
			/* 9 different kinds */
			typ = LAST_GEM + rnd(9);
			if (objects[typ].oc_class == GEM_CLASS)
				goto typfnd;
			else
				typ = 0; /* somebody changed objects[]? punt */
		} else {		 /* try to construct canonical form */
			char tbuf[BUFSZ];
			strcpy(tbuf, "worthless piece of ");
			strcat(tbuf, g); /* assume it starts with the color */
			strcpy(bp, tbuf);
		}
	}

	actualn = bp;
	if (!dn) dn = actualn; /* ex. "skull cap" */
srch:
	/* check real names of gems first */
	if (!oclass && actualn) {
		for (i = bases[GEM_CLASS]; i <= LAST_GEM; i++) {
			const char *zn;

			if ((zn = OBJ_NAME(objects[i])) && !strcmpi(actualn, zn)) {
				typ = i;
				goto typfnd;
			}
		}
	}
	i = oclass ? bases[(int)oclass] : 1;
	while (i < NUM_OBJECTS && (!oclass || objects[i].oc_class == oclass)) {
		const char *zn;

		if (actualn && (zn = OBJ_NAME(objects[i])) != 0 &&
		    wishymatch(actualn, zn, true)) {
			typ = i;
			goto typfnd;
		}
		if (dn && (zn = OBJ_DESCR(objects[i])) != 0 &&
		    wishymatch(dn, zn, false)) {
			/* don't match extra descriptions (w/o real name) */
			if (!OBJ_NAME(objects[i])) return NULL;
			typ = i;
			goto typfnd;
		}
		if (un && (zn = objects[i].oc_uname) != 0 &&
		    wishymatch(un, zn, false)) {
			typ = i;
			goto typfnd;
		}
		i++;
	}
	if (actualn) {
		struct Jitem *j = Japanese_items;
		while (j->item) {
			if (actualn && !strcmpi(actualn, j->name)) {
				typ = j->item;
				goto typfnd;
			}
			j++;
		}
	}
	/* if we've stripped off "armor" and failed to match anything
	   in objects[], append "mail" and try again to catch misnamed
	   requests like "plate armor" and "yellow dragon scale armor" */
	if (oclass == ARMOR_CLASS && !strstri(bp, "mail")) {
		/* modifying bp's string is ok; we're about to resort
		   to random armor if this also fails to match anything */
		strcat(bp, " mail");
		goto retry;
	}

	if (!strcmpi(bp, "spinach")) {
		contents = SPINACH;
		typ = TIN;
		goto typfnd;
	}

	/* Note: not strcmpi.  2 fruits, one capital, one not, are possible.
	 * Also not strncmp.  We used to ignore trailing text with it, but
	 * that resulted in "grapefruit" matching "grape" if the latter came
	 * earlier than the former in the fruit list. */
	{
		char *fp;
		int l, cntf;
		int blessedf, iscursedf, uncursedf, halfeatenf;

		blessedf = iscursedf = uncursedf = halfeatenf = 0;
		cntf = 0;

		fp = fruitbuf;
		for (;;) {
			if (!fp || !*fp) break;
			if (!strncmpi(fp, "an ", l=3) ||
			    !strncmpi(fp, "a ", l=2)) {
				cntf = 1;
			} else if (!cntf && digit(*fp)) {
				cntf = atoi(fp);
				while (digit(*fp))
					fp++;
				while (*fp == ' ')
					fp++;
				l = 0;
			} else if (!strncmpi(fp, "blessed ", l=8)) {
				blessedf = 1;
			} else if (!strncmpi(fp, "cursed ", l=7)) {
				iscursedf = 1;
			} else if (!strncmpi(fp, "uncursed ", l=9)) {
				uncursedf = 1;
			} else if (!strncmpi(fp, "partly eaten ", l=13) ||
				   !strncmpi(fp, "partially eaten ", l=16)) {
				halfeatenf = 1;
			} else
				break;
			fp += l;
		}

		for (f = ffruit; f; f = f->nextf) {
			enum {none, exact, singular, plural} ftyp = none;

			if (!strcmp(fp, f->fname)) ftyp = exact;
			else if (!strcmp(fp, makesingular(f->fname))) ftyp = singular;
			else if (!strcmp(fp, makeplural(f->fname))) ftyp = plural;

			if (ftyp != none) {
				typ = SLIME_MOLD;
				blessed = blessedf;
				iscursed = iscursedf;
				uncursed = uncursedf;
				halfeaten = halfeatenf;
				/* adjust count if user explicitly asked for
				 * singular amount (can't happen unless fruit
				 * has been given an already pluralized name)
				 * or for plural amount */
				if (ftyp == singular && !cntf) cntf = 1;
				else if (ftyp == plural && !cntf) cntf = 2;

				cnt = cntf;
				ftype = f->fid;
				goto typfnd;
			}
		}
	}

	if (!oclass && actualn) {
		short objtyp;

		/* Perhaps it's an artifact specified by name, not type */
		name = artifact_name(actualn, &objtyp);
		if (name) {
			typ = objtyp;
			goto typfnd;
		}
	}
	/* Let wizards wish for traps and furniture.
	 * Must come after objects check so wizards can still wish for
	 * trap objects like beartraps.
	 * Disallow such topology tweaks for WIZKIT startup wishes.
	 */
	if (wizard && !program_state.wizkit_wishing) {
		int trap;

		for (trap = NO_TRAP + 1; trap < TRAPNUM; trap++) {
			const char *tname;

			tname = sym_desc[trap_to_defsym(trap)].explanation;
			if (!strncmpi(tname, bp, strlen(tname))) {
				/* avoid stupid mistakes */
				if (is_holelike(trap) && !Can_fall_thru(&u.uz)) trap = ROCKTRAP;
				maketrap(u.ux, u.uy, trap);
				pline("%s.", An(tname));
				return &zeroobj;
			}
		}
		// furniture and terrain
		p = eos(bp);
		if (!BSTRCMP(bp, p - 8, "fountain")) {
			levl[u.ux][u.uy].typ = FOUNTAIN;
			level.flags.nfountains++;
			if (!strncmpi(bp, "magic ", 6))
				levl[u.ux][u.uy].blessedftn = 1;
			pline("A %sfountain.",
			      levl[u.ux][u.uy].blessedftn ? "magic " : "");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 6, "throne")) {
			levl[u.ux][u.uy].typ = THRONE;
			pline("A throne.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 9, "headstone") || !BSTRCMP(bp, p - 5, "grave")) {
			levl[u.ux][u.uy].typ = GRAVE;
			make_grave(u.ux, u.uy, NULL);
			pline("A grave.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 4, "tree")) {
			levl[u.ux][u.uy].typ = TREE;
			pline("A tree.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 4, "sink")) {
			levl[u.ux][u.uy].typ = SINK;
			level.flags.nsinks++;
			pline("A sink.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 6, "toilet")) {
			levl[u.ux][u.uy].typ = TOILET;
			level.flags.nsinks++;
			pline("A toilet.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 4, "pool")) {
			levl[u.ux][u.uy].typ = POOL;
			del_engr_at(u.ux, u.uy);
			pline("A pool.");
			/* Must manually make kelp! */
			water_damage(level.objects[u.ux][u.uy], false, true);
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
		if (!BSTRCMP(bp, p - 4, "lava")) { /* also matches "molten lava" */
			levl[u.ux][u.uy].typ = LAVAPOOL;
			del_engr_at(u.ux, u.uy);
			pline("A pool of molten lava.");
			if (!(Levitation || Flying)) lava_effects();
			newsym(u.ux, u.uy);
			return &zeroobj;
		}

		if (!BSTRCMP(bp, p - 5, "altar")) {
			aligntyp al;

			levl[u.ux][u.uy].typ = ALTAR;
			if (!strncmpi(bp, "chaotic ", 8))
				al = A_CHAOTIC;
			else if (!strncmpi(bp, "neutral ", 8))
				al = A_NEUTRAL;
			else if (!strncmpi(bp, "lawful ", 7))
				al = A_LAWFUL;
			else if (!strncmpi(bp, "unaligned ", 10))
				al = A_NONE;
			else /* -1 - A_CHAOTIC, 0 - A_NEUTRAL, 1 - A_LAWFUL */
				al = (!rn2(6)) ? A_NONE : rn2((int)A_LAWFUL + 2) - 1;
			levl[u.ux][u.uy].altarmask = Align2amask(al);
			pline("%s altar.", An(align_str(al)));
			newsym(u.ux, u.uy);
			return &zeroobj;
		}

		if (!BSTRCMP(bp, p - 5, "grave") || !BSTRCMP(bp, p - 9, "headstone")) {
			make_grave(u.ux, u.uy, NULL);
			pline("A grave.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}

		if (!BSTRCMP(bp, p - 4, "tree")) {
			levl[u.ux][u.uy].typ = TREE;
			pline("A tree.");
			newsym(u.ux, u.uy);
			block_point(u.ux, u.uy);
			return &zeroobj;
		}

		if (!BSTRCMP(bp, p - 4, "bars")) {
			levl[u.ux][u.uy].typ = IRONBARS;
			pline("Iron bars.");
			newsym(u.ux, u.uy);
			return &zeroobj;
		}
	}
	if (!oclass) return NULL;
any:
	if (!oclass) oclass = wrpsym[rn2((int)sizeof(wrpsym))];
typfnd:
	if (typ) oclass = objects[typ].oc_class;

	// check for some objects that are not allowed
	if (typ && objects[typ].oc_unique) {
		// allow unique objects for wizard
		if (!wizard)
			switch (typ) {
				case AMULET_OF_YENDOR:
					typ = FAKE_AMULET_OF_YENDOR;
					break;
				case CANDELABRUM_OF_INVOCATION:
					typ = rnd_class(TALLOW_CANDLE, WAX_CANDLE);
					break;
				case BELL_OF_OPENING:
					typ = BELL;
					break;
				case SPE_BOOK_OF_THE_DEAD:
					typ = SPE_BLANK_PAPER;
					break;
			}
	}

	/* catch any other non-wishable objects */
	if (objects[typ].oc_nowish && !wizard)
		return NULL;

	/* convert magic lamps to regular lamps before lighting them or setting
	   the charges */
	if (typ == MAGIC_LAMP && !wizard)
		typ = OIL_LAMP;

	if (typ) {
		otmp = mksobj(typ, true, false);
	} else {
		otmp = mkobj(oclass, false);
		if (otmp) typ = otmp->otyp;
	}

	if (islit &&
	    (typ == OIL_LAMP || typ == MAGIC_LAMP ||
	     typ == BRASS_LANTERN || typ == TORCH ||
	     Is_candle(otmp) || typ == POT_OIL)) {
		place_object(otmp, u.ux, u.uy); /* make it viable light source */
		begin_burn(otmp, false);
		obj_extract_self(otmp); /* now release it for caller's use */
	}

	if (cnt > 0 && objects[typ].oc_merge && oclass != SPBOOK_CLASS &&
	    (typ != CORPSE || !is_reviver(&mons[mntmp])) &&
	    (cnt < rnd(6) ||
	     wizard ||
	     (cnt <= 7 && Is_candle(otmp)) ||
	     (cnt <= 20 &&
	      ((oclass == WEAPON_CLASS && is_ammo(otmp)) || typ == ROCK || is_missile(otmp)))))
		otmp->quan = (long)cnt;

	if (oclass == VENOM_CLASS) otmp->spe = 1;

	if (spesgn == 0) {
		spe = otmp->spe;
	} else if (wizard) { // no alteration to spe
		;
	} else if (oclass == ARMOR_CLASS || oclass == WEAPON_CLASS ||
		 is_weptool(otmp) ||
		 (oclass == RING_CLASS && objects[typ].oc_charged)) {
		if (spe > rnd(5) && spe > otmp->spe) spe = 0;
		if (spe > 2 && Luck < 0) spesgn = -1;
	} else {
		if (oclass == WAND_CLASS) {
			if (spe > 1 && spesgn == -1) spe = 1;
		} else {
			if (spe > 0 && spesgn == -1) spe = 0;
		}
		if (spe > otmp->spe) spe = otmp->spe;
	}

	if (spesgn == -1) spe = -spe;

	/* set otmp->spe.  This may, or may not, use spe... */
	switch (typ) {
		case TIN:
			if (contents == EMPTY) {
				otmp->corpsenm = NON_PM;
				otmp->spe = 0;
			} else if (contents == SPINACH) {
				otmp->corpsenm = NON_PM;
				otmp->spe = 1;
			}
			break;
		case SLIME_MOLD:
			otmp->spe = ftype;
		fallthru;
		case SKELETON_KEY:
		case CHEST:
		case LARGE_BOX:
		case HEAVY_IRON_BALL:
		case IRON_CHAIN:
		case STATUE:
			/* otmp->cobj already done in mksobj() */
			break;
#ifdef MAIL
		case SCR_MAIL:
			otmp->spe = 1;
			break;
#endif
		case WAN_WISHING:
			if (!wizard) {
				otmp->spe = (rn2(10) ? -1 : 0);
				break;
			}
		fallthru;
		//(but only if wizard)

		default:
			otmp->spe = spe;
	}

	/* set otmp->corpsenm or dragon scale [mail] */
	if (mntmp >= LOW_PM) {
		if (mntmp == PM_LONG_WORM_TAIL) mntmp = PM_LONG_WORM;

		switch (typ) {
			case TIN:
				otmp->spe = 0; /* No spinach */
				if (dead_species(mntmp, false)) {
					otmp->corpsenm = NON_PM; /* it's empty */
				} else if (!(mons[mntmp].geno & G_UNIQ) &&
					   !(mvitals[mntmp].mvflags & G_NOCORPSE) &&
					   mons[mntmp].cnutrit != 0) {
					otmp->corpsenm = mntmp;
				}
				break;
			case CORPSE:
				if ((wizard) ||
				    (!(mons[mntmp].geno & G_UNIQ) &&
				     !(mvitals[mntmp].mvflags & G_NOCORPSE))) {
					/* beware of random troll or lizard corpse,
				   or of ordinary one being forced to such */
					if (otmp->timed) obj_stop_timers(otmp);
					if (mons[mntmp].msound == MS_GUARDIAN)
						otmp->corpsenm = genus(mntmp, 1);
					else
						otmp->corpsenm = mntmp;
					start_corpse_timeout(otmp);
				}
				break;
			case FIGURINE:
				if (wizard ||
				    ((!(mons[mntmp].geno & G_UNIQ) && !is_human(&mons[mntmp]))
#ifdef MAIL
				     && mntmp != PM_MAIL_DAEMON
#endif
				     ))
					otmp->corpsenm = mntmp;
				break;
			case EGG:
				mntmp = can_be_hatched(mntmp);
				if (mntmp != NON_PM) {
					otmp->corpsenm = mntmp;
					if (!dead_species(mntmp, true))
						attach_egg_hatch_timeout(otmp);
					else
						kill_egg(otmp);
				}
				break;
			case STATUE:
				otmp->corpsenm = mntmp;
				if (Has_contents(otmp) && verysmall(&mons[mntmp]))
					delete_contents(otmp); /* no spellbook */
				otmp->spe = ishistoric ? STATUE_HISTORIC : 0;
				break;
			case SCALE_MAIL:
				/* Dragon mail - depends on the order of objects */
				/*		 & dragons.			 */
				if (mntmp >= PM_GRAY_DRAGON &&
				    mntmp <= PM_YELLOW_DRAGON)
					otmp->otyp = GRAY_DRAGON_SCALE_MAIL +
						     mntmp - PM_GRAY_DRAGON;
				break;
		}
	}

	/* set blessed/cursed -- setting the fields directly is safe
	 * since weight() is called below and addinv() will take care
	 * of luck */
	if (iscursed) {
		curse(otmp);
	} else if (uncursed) {
		otmp->blessed = 0;
		otmp->cursed = Luck < 0 && !wizard;
	} else if (blessed) {
		otmp->blessed = Luck >= 0 || wizard;
		otmp->cursed = Luck < 0 && !wizard;
	} else if (spesgn < 0) {
		curse(otmp);
	}

	if (isinvisible)
		obj_set_oinvis(otmp, isinvisible > 0, false);

	/* set eroded */
	if (is_damageable(otmp) || otmp->otyp == CRYSKNIFE) {
		if (eroded && (is_flammable(otmp) || is_rustprone(otmp)))
			otmp->oeroded = eroded;
		if (eroded2 && (is_corrodeable(otmp) || is_rottable(otmp)))
			otmp->oeroded2 = eroded2;

		/* set erodeproof */
		if (erodeproof && !eroded && !eroded2)
			otmp->oerodeproof = Luck >= 0 || wizard;
	}

	/* set otmp->recharged */
	if (oclass == WAND_CLASS) {
		/* prevent wishing abuse */
		if (otmp->otyp == WAN_WISHING && !wizard) rechrg = 1;
		otmp->recharged = (unsigned)rechrg;
	}

	/* set poisoned */
	if (ispoisoned) {
		if (is_poisonable(otmp))
			otmp->opoisoned = (Luck >= 0);
		else if (oclass == FOOD_CLASS)
			/* try to taint by making it as old as possible */
			otmp->age = 1L;
	}

	if (trapped) {
		if (Is_box(otmp) || typ == TIN)
			otmp->otrapped = (trapped == 1);
	}

	if (isgreased) otmp->greased = 1;

	if (isdiluted && otmp->oclass == POTION_CLASS &&
	    otmp->otyp != POT_WATER)
		otmp->odiluted = 1;

	// set tin variety
	if (otmp->otyp == TIN && tvariety >= 0 && (rn2(20) || wizard))
		set_tin_variety(otmp, tvariety);

	if (name) {
		const char *aname;
		short objtyp;
		char nname[256];
		strcpy(nname, name);

		/* an artifact name might need capitalization fixing */
		aname = artifact_name(name, &objtyp);
		if (aname && objtyp == otmp->otyp) name = aname;

		place_object(otmp, u.ux, u.uy); /* make it viable light source */
		otmp = oname(otmp, nname);
		obj_extract_self(otmp); /* now release it for caller's use */
		if (otmp->oartifact) {
			otmp->quan = 1L;
			u.uconduct.wisharti++; /* KMH, conduct */
		}
	}

	/* more wishing abuse: don't allow wishing for certain artifacts */
	/* and make them pay; charge them for the wish anyway! */
	if ((is_quest_artifact(otmp) ||
	     /* [ALI] Can't wish for artifacts which have a set location */
	     (otmp->oartifact &&
	      (otmp->oartifact == ART_KEY_OF_CHAOS ||
	       otmp->oartifact == ART_KEY_OF_NEUTRALITY ||
	       otmp->oartifact == ART_KEY_OF_LAW ||
	       otmp->oartifact == ART_HAND_OF_VECNA ||
	       otmp->oartifact == ART_EYE_OF_THE_BEHOLDER ||
	       otmp->oartifact == ART_NIGHTHORN ||
	       otmp->oartifact == ART_THIEFBANE)) ||
	     (otmp->oartifact && rn2(nartifact_exist()) > 1)) &&
	    !wizard) {
		artifact_exists(otmp, ONAME(otmp), false);
		if (Has_contents(otmp))
			delete_contents(otmp);
		obfree(otmp, NULL);
		otmp = &zeroobj;
		pline("For a moment, you feel something in your %s, but it disappears!",
		      makeplural(body_part(HAND)));
	}

	if (halfeaten && otmp->oclass == FOOD_CLASS) {
		if (otmp->otyp == CORPSE)
			otmp->oeaten = mons[otmp->corpsenm].cnutrit;
		else
			otmp->oeaten = objects[otmp->otyp].oc_nutrition;
		/* (do this adjustment before setting up object's weight) */
		consume_oeaten(otmp, 1);
	}
	if (isdrained && otmp->otyp == CORPSE && mons[otmp->corpsenm].cnutrit) {
		int amt;
		otmp->odrained = 1;
		amt = mons[otmp->corpsenm].cnutrit - drainlevel(otmp);
		if (halfdrained) {
			amt /= 2;
			if (amt == 0)
				amt++;
		}
		/* (do this adjustment before setting up object's weight) */
		consume_oeaten(otmp, -amt);
	}
	otmp->owt = weight(otmp);
	if (very && otmp->otyp == HEAVY_IRON_BALL) otmp->owt += 160;

	return otmp;
}

int rnd_class(int first, int last) {
	int i, x, sum = 0;

	if (first == last)
		return first;
	for (i = first; i <= last; i++)
		sum += objects[i].oc_prob;
	if (!sum) /* all zero */
		return first + rn2(last - first + 1);
	x = rnd(sum);
	for (i = first; i <= last; i++)
		if (objects[i].oc_prob && (x -= objects[i].oc_prob) <= 0)
			return i;
	return 0;
}

static const char *Japanese_item_name(int i) {
	struct Jitem *j = Japanese_items;

	while (j->item) {
		if (i == j->item)
			return j->name;
		j++;
	}
	return NULL;
}

const char *cloak_simple_name(const struct obj *cloak) {
	if (cloak) {
		switch (cloak->otyp) {
			case ROBE:
				return "robe";
			case MUMMY_WRAPPING:
				return "wrapping";
			case LAB_COAT:
				return "coat";
#if 0
			case ALCHEMY_SMOCK:
				return (objects[cloak->otyp].oc_name_known &&
					cloak->dknown) ?
					       "smock" :
					       "apron";
#endif
			default:
				break;
		}
	}
	return "cloak";
}

// helm vs hat for messages
const char *helm_simple_name(const struct obj *helmet) {
	/*
	 * There is some wiggle room here; the result has been chosen
	 * for consistency with the "protected by hard helmet" messages
	 * given for various bonks on the head:  headgear that provides
	 * such protection is a "helm", that which doesn't is a "hat".
	 *
	 *     elven leather helm / leather hat    -> hat
	 *     dwarvish iron helm / hard hat       -> helm
	 * The rest are completely straightforward:
	 *     fedora, cornuthaum, dunce cap       -> hat
	 *     all other types of helmets          -> helm
	 */
	return (helmet && !is_metallic(helmet)) ? "hat" : "helm";
}



const char *mimic_obj_name(const struct monst *mtmp) {
	if (mtmp->m_ap_type == M_AP_OBJECT && mtmp->mappearance != STRANGE_OBJECT) {
		int idx = objects[mtmp->mappearance].oc_descr_idx;
		if (mtmp->mappearance == GOLD_PIECE) return "gold";
		return objects[idx].oc_name;
	}
	return "whatcha-may-callit";
}

/*objnam.c*/
