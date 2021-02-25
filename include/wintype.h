/*	SCCS Id: @(#)wintype.h	3.4	1996/02/18	*/
/* Copyright (c) David Cohrs, 1991				  */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef WINTYPE_H
#define WINTYPE_H

typedef int winid; /* a window identifier */

/* generic parameter - must not be any larger than a pointer */
typedef union {
	void *a_void;
	struct obj *a_obj;
	struct monst *a_monst;
	struct autopickup_exception *a_ape;
	int a_int;
	uint a_uint;
	char a_char;
	schar a_schar;
	long a_long;
	/* add types as needed */
} anything;
#define anything_zero ((anything){0})

/* menu return list */
typedef struct {
	anything item; /* identifier */
	long count;    /* count */
} menu_item;

/* select_menu() "how" argument types */
#define PICK_NONE 0 /* user picks nothing (display only) */
#define PICK_ONE  1 /* only pick one */
#define PICK_ANY  2 /* can pick any amount */

/* window types */
/* any additional port specific types should be defined in win*.h */
#define NHW_MESSAGE 1
#define NHW_STATUS  2
#define NHW_MAP	    3
#define NHW_MENU    4
#define NHW_TEXT    5

/* attribute types for putstr; the same as the ANSI value, for convenience */
#define ATR_NONE    0
#define ATR_BOLD    0x1
#define ATR_ULINE   0x2
#define ATR_BLINK   0x4
#define ATR_INVERSE 0x8

/* nh_poskey() modifier types */
#define CLICK_1 1
#define CLICK_2 2

/* invalid winid */
#define WIN_ERR ((winid)-1)

/* menu window keyboard commands (may be mapped) */
#define MENU_FIRST_PAGE	   '^'
#define MENU_LAST_PAGE	   '|'
#define MENU_NEXT_PAGE	   '>'
#define MENU_PREVIOUS_PAGE '<'
#define MENU_SELECT_ALL	   '.'
#define MENU_UNSELECT_ALL  '-'
#define MENU_INVERT_ALL	   '@'
#define MENU_SELECT_PAGE   ','
#define MENU_UNSELECT_PAGE '\\'
#define MENU_INVERT_PAGE   '~'
#define MENU_SEARCH	   ':'

#endif /* WINTYPE_H */
