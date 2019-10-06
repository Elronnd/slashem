/*	SCCS Id: @(#)eshk.h	3.4	1997/05/01	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef ESHK_H
#define ESHK_H

#define REPAIR_DELAY	5	/* minimum delay between shop damage & repair */

struct bill_x {
	unsigned bo_id;
	boolean useup;
	long price;		/* price per unit */
	long bquan;		/* amount used up */
};

struct eshk {
	long robbed;		/* amount stolen by most recent customer */
	long credit;		/* amount credited to customer */
	long debit;		/* amount of debt for using unpaid items */
	long loan;		/* shop-gold picked (part of debit) */
	int shoptype;		/* the value of rooms[shoproom].rtype */
	schar shoproom;		/* index in rooms; set by inshop() */
	schar unused;		/* to force alignment for stupid compilers */
	boolean following;	/* following customer since he owes us sth */
	boolean surcharge;	/* angry shk inflates prices */
	coord shk;		/* usual position shopkeeper */
	coord shd;		/* position shop door */
	d_level shoplevel;	/* level (& dungeon) of his shop */
	int billct;		/* no. of entries of bill_p[] in use */
	int billsz;		/* no. of entries of bill_p[] allocated */
	struct bill_x *bill_p;
	int visitct;		/* nr of visits by most recent customer */
	char customer[PL_NSIZ];	/* most recent customer */
	char shknam[PL_NSIZ];
	long services;          /* Services offered */
#define SHK_ID_BASIC    0x1
#define SHK_ID_PREMIUM  0x2
#define SHK_UNCURSE     0x4
#define SHK_APPRAISE    0x8
#define SHK_SPECIAL_A   0x10
#define SHK_SPECIAL_B   0x20
#define SHK_SPECIAL_C   0x40
};

#define ESHK(mon)	((struct eshk *)mon->mextra)

#define NOTANGRY(mon)	((mon)->mpeaceful)
#define ANGRY(mon)	(!NOTANGRY(mon))

#define SHK_NOMATCH     0       /* Shk !know this class of object       */
#define SHK_MATCH       1       /* Shk is expert                        */
#define SHK_GENERAL     2       /* Shk runs a general store             */

/*
 * FUNCTION shk_class_match
 *
 * Return true if a object class matches the shop type.
 * I.e. shk_class_match(WEAPON_CLASS, shkp)
 *
 * Return:      SHK_MATCH, SHK_NOMATCH, SHK_GENERAL
 */

#define shk_class_match(class, shkp) \
        ((shtypes[ESHK(shkp)->shoptype-SHOPBASE].symb == RANDOM_CLASS) ? \
                SHK_GENERAL : \
         ((shtypes[ESHK(shkp)->shoptype-SHOPBASE].symb == class) ? \
                SHK_MATCH : SHK_NOMATCH))

#endif /* ESHK_H */
