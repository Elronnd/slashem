/*	SCCS Id: @(#)hacklib.c	3.4	2002/12/13	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* Copyright (c) Robert Patrick Rankin, 1991		  */
/* NetHack may be freely redistributed.  See license for details. */

/* We could include only config.h, except for the overlay definitions... */
#include "hack.h"

#ifdef __FreeBSD__
#define __XSI_VISIBLE 500  //gettimeofday()
#endif
#ifdef _POSIX_C_SOURCE
#include <sys/time.h>
#endif

/*=
    Assorted 'small' utility routines.	They're virtually independent of
NetHack, except that rounddiv may call panic().

      return type     routine name    argument type(s)
	bool		digit		(char)
	bool		letter		(char)
	char		highc		(char)
	char		lowc		(char)
	char *		lcase		(char *)
	char *		upstart		(char *)
	char *		mungspaces	(char *)
	char *		eos		(char *)
	bool		str_end_is	(const char *, const char *)
	char *		strkitten	(char *,char)
	char *		s_suffix	(const char *)
	bool		onlyspace	(const char *)
	char *		tabexpand	(char *)
	char *		visctrl		(char)
	char *		strsubst	(char *, const char *, const char *)
	const char *	ordin		(int)
	char *		sitoa		(int)
	int		sgn		(int)
	int		rounddiv	(long, int)
	int		distmin		(int, int, int, int)
	int		dist2		(int, int, int, int)
	bool		online2		(int, int)
	bool		regmatch	(const char *, const char *, bool)
	int		strncmpi	(const char *, const char *, int)
	char *		strstri		(const char *, const char *)
	bool		fuzzymatch	(const char *,const char *,const char *,bool)
	void		setrandom	(void)
	int		getyear		(void)
	long		yyyymmdd	(time_t)
	long		hhmmss		(time_t)
	int		phase_of_the_moon(void)
	bool		friday_13th	(void)
	bool		groundhog_day	(void)
	bool		night		(void)
	bool		midnight	(void)
	void		msleep		(uint)
=*/

// is 'c' a digit?
bool digit(char c) {
	return '0' <= c && c <= '9';
}

// is 'c' a letter?  note: '@' classed as letter
bool letter(char c) {
	return ('@' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
}

// force 'c' into uppercase
char highc(char c) {
	return ('a' <= c && c <= 'z') ? (c & ~040) : c;
}

// force 'c' into lowercase
char lowc(char c) {
	return ('A' <= c && c <= 'Z') ? (c | 040) : c;
}

// convert a string into all lowercase
char *lcase(char *s) {
	char *p;

	for (p = s; *p; p++)
		if ('A' <= *p && *p <= 'Z') *p |= 040;
	return s;
}

// convert first character of a string to uppercase
char *upstart(char *s) {
	if (s)
		s[0] = highc(s[0]);

	return s;
}

/* remove excess whitespace from a string buffer (in place) */
char *mungspaces(char *bp) {
	char c, *p, *p2;
	bool was_space = true;

	for (p = p2 = bp; (c = *p) != '\0'; p++) {
		if (c == '\t') c = ' ';
		if (c != ' ' || !was_space) *p2++ = c;
		was_space = (c == ' ');
	}
	if (was_space && p2 > bp) p2--;
	*p2 = '\0';
	return bp;
}

// return the end of a string (pointing at '\0')
char *eos(char *s) {
	while (*s)
		s++;  // s += strlen(s);
	return s;
}

// returns true if str ends in chkstr
bool str_end_is(const char *str, const char *chkstr) {
	size_t slen, clen;
	return str && chkstr && ((slen=strlen(str)) >= (clen=strlen(chkstr))) && !memcmp(str + slen - clen, chkstr, clen);
}

// strcat(s, {c,'\0'});
// append a character to a string (in place)
char *strkitten(char *s, char c) {
	char *p = eos(s);

	*p++ = c;
	*p = '\0';
	return s;
}

// return a name converted to possessive
char *s_suffix(const char *s) {
	static char buf[BUFSZ];

	strcpy(buf, s);
	if (!strcmpi(buf, "it"))
		strcat(buf, "s");
	else if (*(eos(buf) - 1) == 's')
		strcat(buf, "'");
	else
		strcat(buf, "'s");
	return buf;
}

// is a string entirely whitespace?
bool onlyspace(const char *s) {
	for (; *s; s++)
		if (*s != ' ' && *s != '\t')
			return false;

	return true;
}

// expand tabs into proper number of spaces
char *tabexpand(char *sbuf) {
	char buf[BUFSZ];
	char *bp, *s = sbuf;
	int idx;

	if (!*s) return sbuf;

	/* warning: no bounds checking performed */
	for (bp = buf, idx = 0; *s; s++)
		if (*s == '\t') {
			do
				*bp++ = ' ';
			while (++idx % 8);
		} else {
			*bp++ = *s;
			idx++;
		}
	*bp = 0;
	return strcpy(sbuf, buf);
}

// make a displayable string from a character
char *visctrl(char c) {
	static char ccc[3];

	c &= 0177;

	ccc[2] = '\0';
	if (c < 040) {
		ccc[0] = '^';
		ccc[1] = c | 0100; /* letter */
	} else if (c == 0177) {
		ccc[0] = '^';
		ccc[1] = c & ~0100; /* '?' */
	} else {
		ccc[0] = c; /* printable character */
		ccc[1] = '\0';
	}
	return ccc;
}

/* substitute a word or phrase in a string (in place) */
/* caller is responsible for ensuring that bp points to big enough buffer */
char *strsubst(char *bp, const char *orig, const char *replacement) {
	char *found;
	char buf[BUFSZ];

	if (bp) {
		found = strstr(bp, orig);
		if (found) {
			strcpy(buf, found + strlen(orig));
			strcpy(found, replacement);
			strcat(bp, buf);
		}
	}

	return bp;
}

// return the ordinal suffix of a number
const char *ordin(uint n) {
	int dd = n % 10;

	return	(dd == 0 || dd > 3 || (n % 100) / 10 == 1) ? "th" :
	    	(dd == 1) ? "st" :
		(dd == 2) ? "nd" :
		"rd";
}

// make a signed digit string from a number
char *sitoa(int n) {
	static char buf[13];

	sprintf(buf, (n < 0) ? "%d" : "+%d", n);
	return buf;
}

// return the sign of a number: -1, 0, or 1
int sgn(int n) {
	return (n < 0) ? -1 : (n != 0);
}

// calculate x/y, rounding as appropriate
int rounddiv(long x, int y) {
	int r, m;
	int divsgn = 1;

	if (y == 0)
		panic("division by zero in rounddiv");
	else if (y < 0) {
		divsgn = -divsgn;
		y = -y;
	}
	if (x < 0) {
		divsgn = -divsgn;
		x = -x;
	}
	r = x / y;
	m = x % y;
	if (2 * m >= y) r++;

	return divsgn * r;
}

// distance between two points, in moves
int distmin(int x0, int y0, int x1, int y1) {
	int dx = x0 - x1, dy = y0 - y1;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	/*  The minimum number of moves to get from (x0,y0) to (x1,y1) is the
	 :  larger of the [absolute value of the] two deltas.
	 */
	return (dx < dy) ? dy : dx;
}

// square of euclidean distance between pair of pts
int dist2(int x0, int y0, int x1, int y1) {
	int dx = x0 - x1, dy = y0 - y1;
	return dx * dx + dy * dy;
}

// are two points lined up (on a straight line)?
bool online2(int x0, int y0, int x1, int y1) {
	int dx = x0 - x1, dy = y0 - y1;
	/*  If either delta is zero then they're on an orthogonal line,
	 *  else if the deltas are equal (signs ignored) they're on a diagonal.
	 */
	return !dy || !dx || (dy == dx) || (dy + dx == 0); /* (dy == -dx) */
}

bool regmatch(const char *pattern, const char *string, bool caseblind) {
	char rpattern[BUFSZ];
	snprintf(rpattern, sizeof(rpattern), "^(%s)$", pattern);
	regex_t regex;
	int errnum = tre_regcomp(&regex, rpattern, REG_EXTENDED | (caseblind ? REG_ICASE : 0));
	if (errnum != 0) {
		char errbuf[BUFSZ];
		tre_regerror(errnum, &regex, errbuf, sizeof(errbuf));
		impossible("Bad regex syntax in \"%s\": \"%s\" (matching against \"%s\")", pattern, errbuf, string);
		return false;
	}

	bool ret = tre_regexec(&regex, string, 0, NULL, 0) == 0;

	tre_regfree(&regex);

	return ret;
}

#ifndef STRNCMPI
// case insensitive counted string comparison
int strncmpi(const char *s1, const char *s2, usize n) {
	//{ aka strncasecmp }
	char t1, t2;

	while (n--) {
		if (!*s2)
			return *s1 != 0; /* s1 >= s2 */
		else if (!*s1)
			return -1; /* s1  < s2 */
		t1 = lowc(*s1++);
		t2 = lowc(*s2++);
		if (t1 != t2) return (t1 > t2) ? 1 : -1;
	}
	return 0; /* s1 == s2 */
}
#endif /* STRNCMPI */

#ifndef STRSTRI

// case insensitive substring search
char *strstri(const char *str, const char *sub) {
	const char *s1, *s2;
	int i, k;
#define TABSIZ 0x20			 /* 0x40 would be case-sensitive */
	char tstr[TABSIZ], tsub[TABSIZ]; /* nibble count tables */
#if 0
	assert( (TABSIZ & ~(TABSIZ-1)) == TABSIZ ); /* must be exact power of 2 */
	assert( &lowc != 0 );			/* can't be unsafe macro */
#endif

	/* special case: empty substring */
	if (!*sub) return (char *)str;

	/* do some useful work while determining relative lengths */
	for (i = 0; i < TABSIZ; i++)
		tstr[i] = tsub[i] = 0; /* init */
	for (k = 0, s1 = str; *s1; k++)
		tstr[*s1++ & (TABSIZ - 1)]++;
	for (s2 = sub; *s2; --k)
		tsub[*s2++ & (TABSIZ - 1)]++;

	/* evaluate the info we've collected */
	if (k < 0) return NULL;			    /* sub longer than str, so can't match */
	for (i = 0; i < TABSIZ; i++)		    /* does sub have more 'x's than str? */
		if (tsub[i] > tstr[i]) return NULL; /* match not possible */

	/* now actually compare the substring repeatedly to parts of the string */
	for (i = 0; i <= k; i++) {
		s1 = &str[i];
		s2 = sub;
		while (lowc(*s1++) == lowc(*s2++))
			if (!*s2) return (char *)&str[i]; /* full match */
	}
	return NULL; /* not found */
}
#endif /* STRSTRI */

/* compare two strings for equality, ignoring the presence of specified
   characters (typically whitespace) and possibly ignoring case */
bool fuzzymatch(const char *s1, const char *s2, const char *ignore_chars, bool caseblind) {
	char c1, c2;

	do {
		while ((c1 = *s1++) != '\0' && index(ignore_chars, c1) != 0)
			continue;
		while ((c2 = *s2++) != '\0' && index(ignore_chars, c2) != 0)
			continue;
		if (!c1 || !c2) break; /* stop when end of either string is reached */

		if (caseblind) {
			c1 = lowc(c1);
			c2 = lowc(c2);
		}
	} while (c1 == c2);

	/* match occurs only when the end of both strings has been reached */
	return !c1 && !c2;
}

/*
 * Time routines
 *
 * The time is used for:
 *	- seed for rand()
 *	- year on tombstone and yyyymmdd in record file
 *	- phase of the moon (various monsters react to NEW_MOON or FULL_MOON)
 *	- night and midnight (the undead are dangerous at midnight)
 *	- determination of what files are "very old"
 */

static struct tm *getlt(void);

void setrandom(void) {
	char rnbuf[64];
	memset(rnbuf, 0xaa, SIZE(rnbuf));  // 0xaa = alternating 0s and 1s

	//TODO: something that works on windows
	FILE *fp = fopen("/dev/urandom", "rb");
	if (!fp) fp = fopen("/dev/random", "rb");

	if (fp) {
		assert(fread(rnbuf, 1, 32, fp) == 32, "fread: failed to read random data from /dev/u?random");
		fclose(fp);
	}

#ifdef _POSIX_C_SOURCE
	struct timeval tv;
	gettimeofday(&tv, NULL);
	memcpy(&rnbuf[32], &tv.tv_sec, sizeof(int));
	memcpy(&rnbuf[32 + sizeof(int)], &tv.tv_usec, sizeof(int));
#endif

	memcpy(&rnbuf[63 - sizeof(void*)], (void*)malloc, sizeof(void*));

	seed_good_random(rnbuf);
}

static struct tm *getlt(void) {
	time_t date;

	time(&date);
	return localtime(&date);
}

int getyear(void) {
	return 1900 + getlt()->tm_year;
}

/* KMH -- Used by gypsies */
int getmonth(void) {
	return getlt()->tm_mon;
}

long yyyymmdd(time_t date) {
	long datenum;
	struct tm *lt;

	if (date == 0)
		lt = getlt();
	else
		lt = localtime(&date);

	/* just in case somebody's localtime supplies (year % 100)
	   rather than the expected (year - 1900) */
	if (lt->tm_year < 70)
		datenum = (long)lt->tm_year + 2000L;
	else
		datenum = (long)lt->tm_year + 1900L;
	/* yyyy --> yyyymm */
	datenum = datenum * 100L + (long)(lt->tm_mon + 1);
	/* yyyymm --> yyyymmdd */
	datenum = datenum * 100L + (long)lt->tm_mday;
	return datenum;
}

long hhmmss(time_t date) {
	long timenum;
	struct tm *lt;

	if (date == 0)
		lt = getlt();
	else
		lt = localtime(&date);

	timenum = lt->tm_hour * 10000L + lt->tm_min * 100L + lt->tm_sec;
	return timenum;
}

/*
 * moon period = 29.53058 days ~= 30, year = 365.2422 days
 * days moon phase advances on first day of year compared to preceding year
 *	= 365.2422 - 12*29.53058 ~= 11
 * years in Metonic cycle (time until same phases fall on the same days of
 *	the month) = 18.6 ~= 19
 * moon phase on first day of year (epact) ~= (11*(year%19) + 29) % 30
 *	(29 as initial condition)
 * current phase in days = first day phase + days elapsed in year
 * 6 moons ~= 177 days
 * 177 ~= 8 reported phases * 22
 * + 11/22 for rounding
 */
// 0-7, with 0: new, 4: full
int phase_of_the_moon(void) {
	struct tm *lt = getlt();
	int epact, diy, goldn;

	diy = lt->tm_yday;
	goldn = (lt->tm_year % 19) + 1;
	epact = (11 * goldn + 18) % 30;
	if ((epact == 25 && goldn > 11) || epact == 24)
		epact++;

	return (((((diy + epact) * 6) + 11) % 177) / 22) & 7;
}

bool friday_13th(void) {
	struct tm *lt = getlt();

	return lt->tm_wday == 5 /* friday */ && lt->tm_mday == 13;
}

bool groundhog_day(void) {
	struct tm *lt = getlt();

	/* KMH -- Groundhog Day is February 2 */
	return lt->tm_mon == 1 && lt->tm_mday == 2;
}

bool night(void) {
	int hour = getlt()->tm_hour;

	return hour < 6 || hour > 21;
}

bool midnight(void) {
	return getlt()->tm_hour == 0;
}

void msleep(uint ms) {
	const struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000};
	nanosleep(&ts, NULL);
}

/*hacklib.c*/
