/*	SCCS Id: @(#)save.c	3.4	2003/11/14	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "lev.h"
#include "quest.h"

#ifndef NO_SIGNAL
#include <signal.h>
#endif
#if !defined(LSC) && !defined(O_WRONLY)
#include <fcntl.h>
#endif

/*WAC boolean here to keep track of quit status*/
boolean saverestore;

static void savelevchn(int, int);
static void savedamage(int, int);
static void saveobjchn(int, struct obj *, int);
static void savemonchn(int, struct monst *, int);
static void savetrapchn(int, struct trap *, int);
static void savegamestate(int, int);
#define nulls nul

#if defined(UNIX) || defined(WIN32)
#define HUP if (!program_state.done_hup)
#else
#define HUP
#endif

extern struct menucoloring *menu_colorings;
extern const struct percent_color_option *hp_colors;
extern const struct percent_color_option *pw_colors;
extern const struct text_color_option *text_colors;

/* need to preserve these during save to avoid accessing freed memory */
static unsigned ustuck_id = 0, usteed_id = 0;

int dosave(void) {
#ifdef KEEP_SAVE
	/*WAC for reloading*/
	int fd;
#endif

	clear_nhwindow(WIN_MESSAGE);
	if (yn("Really save?") == 'n') {
		clear_nhwindow(WIN_MESSAGE);
		if (multi > 0) nomul(0);
	} else {
		clear_nhwindow(WIN_MESSAGE);
		pline("Saving...");
#if defined(UNIX) || defined(__EMX__)
		program_state.done_hup = 0;
#endif
#ifdef KEEP_SAVE
		saverestore = false;
		if (flags.keep_savefile)
			if (yn("Really quit?") == 'n') saverestore = true;
		if (dosave0() && !saverestore)
#else
		if (dosave0())
#endif
		{
			program_state.something_worth_saving = 0;
			u.uhp = -1; /* universal game's over indicator */
			/* make sure they see the Saving message */
			display_nhwindow(WIN_MESSAGE, true);
			exit_nhwindows("Be seeing you...");
			terminate(EXIT_SUCCESS);
		}
		/*WAC redraw later
				else doredraw();*/
	}
#ifdef KEEP_SAVE
	if (saverestore) {
		/*WAC pulled this from pcmain.c - restore game from the file just saved*/
		fd = create_levelfile(0);
		if (fd < 0) {
			raw_print("Cannot create lock file");
		} else {
			hackpid = 1;
			write(fd, (void *)&hackpid, sizeof(hackpid));
			close(fd);
		}

		fd = restore_saved_game();
		if (fd >= 0) dorecover(fd);
		check_special_room(false);
		context.move = 0;
		/*WAC correct these after restore*/
		if (flags.moonphase == FULL_MOON)
			change_luck(1);
		if (flags.friday13)
			change_luck(-1);
		if (iflags.window_inited)
			clear_nhwindow(WIN_MESSAGE);
	}
	saverestore = false;
#endif
	doredraw();
	return 0;
}

#if defined(UNIX) || defined(__EMX__) || defined(WIN32)
/* called as signal() handler, so sent at least one arg */
void hangup(int sig_unused) {
#ifdef NOSAVEONHANGUP
	signal(SIGINT, SIG_IGN);
	clearlocks();
	terminate(EXIT_FAILURE);
#else /* SAVEONHANGUP */
	if (!program_state.done_hup++) {
		if (program_state.something_worth_saving) {
			// AIS: record levels on which there were hangups
			if (ledger_no(&u.uz) >= 0 /*&& (int)ledger_no(&u.uz) < MAXLINFO*/) {
				level_info[ledger_no(&u.uz)].flags |= HANGUP_HERE;
			}
			dosave0();
		}
		{
			clearlocks();
			terminate(EXIT_FAILURE);
		}
	}
#endif
	return;
}
#endif

/* returns 1 if save successful */
int dosave0() {
	int fd, ofd;
	xchar ltmp;
	d_level uz_save;
	char whynot[BUFSZ];

	if (!SAVEF[0])
		return 0;

#ifdef UNIX
	signal(SIGHUP, SIG_IGN);
#endif
#ifndef NO_SIGNAL
	signal(SIGINT, SIG_IGN);
#endif

	HUP if (iflags.window_inited) {
		fd = open_savefile();
		if (fd > 0) {
			close(fd);
			clear_nhwindow(WIN_MESSAGE);
			pline("There seems to be an old save file.");
			if (yn("Overwrite the old file?") == 'n') {
#ifdef KEEP_SAVE
				/*WAC don't restore if you didn't save*/
				saverestore = false;
#endif
				return 0;
			}
		}
	}

	HUP mark_synch(); /* flush any buffered screen output */

	fd = create_savefile();
	if (fd < 0) {
		HUP pline("Cannot open save file.");
		delete_savefile(); /* ab@unido */
		return 0;
	}

	vision_recalc(2); /* shut down vision to prevent problems
				   in the event of an impossible() call */

	/* undo date-dependent luck adjustments made at startup time */
	if (flags.moonphase == FULL_MOON) /* ut-sally!fletcher */
		change_luck(-1);	  /* and unido!ab */
	if (flags.friday13)
		change_luck(1);
	if (iflags.window_inited)
		HUP clear_nhwindow(WIN_MESSAGE);

	store_version(fd);
#ifdef STORE_PLNAME_IN_FILE
	bwrite(fd, (void *)plname, PL_NSIZ);
#endif
	ustuck_id = (u.ustuck ? u.ustuck->m_id : 0);
	usteed_id = (u.usteed ? u.usteed->m_id : 0);

	savelev(fd, ledger_no(&u.uz), WRITE_SAVE | FREE_SAVE);
	/*Keep things from beeing freed if not restoring*/
	/*
	#ifdef KEEP_SAVE
		if (saverestore) savegamestate(fd, WRITE_SAVE);
		else
	#endif
	*/
	savegamestate(fd, WRITE_SAVE | FREE_SAVE);

	/* While copying level files around, zero out u.uz to keep
	 * parts of the restore code from completely initializing all
	 * in-core data structures, since all we're doing is copying.
	 * This also avoids at least one nasty core dump.
	 */
	uz_save = u.uz;
	u.uz.dnum = u.uz.dlevel = 0;
	/* these pointers are no longer valid, and at least u.usteed
	 * may mislead place_monster() on other levels
	 */
	setustuck(NULL);
	u.usteed = NULL;

	for (ltmp = (xchar)1; ltmp <= maxledgerno(); ltmp++) {
		if (ltmp == ledger_no(&uz_save)) continue;
		if (!(level_info[ltmp].flags & LFILE_EXISTS)) continue;

		ofd = open_levelfile(ltmp, whynot);
		if (ofd < 0) {
			HUP pline("%s", whynot);
			close(fd);
			delete_savefile();
			HUP killer.name = nhsdupz(whynot);
			HUP done(TRICKED);
			return 0;
		}
		getlev(ofd, hackpid, ltmp, false);
		close(ofd);
		bwrite(fd, (void *)&ltmp, sizeof ltmp);	   /* level number*/
		savelev(fd, ltmp, WRITE_SAVE | FREE_SAVE); /* actual level*/
		delete_levelfile(ltmp);
	}
	bclose(fd);

	u.uz = uz_save;

	/* get rid of current level --jgm */

	delete_levelfile(ledger_no(&u.uz));
	delete_levelfile(0);
	return 1;
}

static void savegamestate(int fd, int mode) {
	int uid;
	time_t realtime;

	uid = getuid();
	bwrite(fd, &uid, sizeof(uid));
	bwrite(fd, &context, sizeof(struct context_info));
	bwrite(fd, &flags, sizeof(struct flag));
	bwrite(fd, &u, sizeof(struct you));
	bwrite(fd, &youmonst, sizeof(struct monst));

	ptrdiff_t umonst_data_offset;
	if ((&mons[0] <= youmonst.data) && (youmonst.data <= &mons[NUMMONS-1])) {
		umonst_data_offset = youmonst.data - &mons[0];
	} else if (youmonst.data == &upermonst) {
		umonst_data_offset = -1; // magic number
	} else {
		impossible("Bad youmonst.data (%p)", youmonst.data);
		umonst_data_offset = -1; // probably reasonable
	}

	bwrite(fd, &umonst_data_offset, sizeof(ptrdiff_t));
	bwrite(fd, &upermonst, sizeof(struct permonst));

	save_killers(fd, mode);
	/* must come before migrating_objs and migrating_mons are freed */
	save_timers(fd, mode, RANGE_GLOBAL);
	save_light_sources(fd, mode, RANGE_GLOBAL);

	saveobjchn(fd, invent, mode);
	saveobjchn(fd, migrating_objs, mode);
	savemonchn(fd, migrating_mons, mode);
	if (release_data(mode)) {
		invent = 0;
		migrating_objs = 0;
		migrating_mons = 0;
	}
	bwrite(fd, (void *)mvitals, sizeof(mvitals));

	save_dungeon(fd, (boolean) !!perform_bwrite(mode),
		     (boolean) !!release_data(mode));
	savelevchn(fd, mode);
	bwrite(fd, (void *)&moves, sizeof moves);
	bwrite(fd, (void *)&monstermoves, sizeof monstermoves);
	bwrite(fd, (void *)&quest_status, sizeof(struct q_score));
	bwrite(fd, (void *)spl_book,
	       sizeof(struct spell) * (MAXSPELL + 1));
	bwrite(fd, (void *)tech_list,
	       sizeof(struct tech) * (MAXTECH + 1));
	save_artifacts(fd);
	if (ustuck_id)
		bwrite(fd, (void *)&ustuck_id, sizeof ustuck_id);

	if (usteed_id)
		bwrite(fd, (void *)&usteed_id, sizeof usteed_id);

	bwrite(fd, (void *)pl_character, sizeof pl_character);
	bwrite(fd, (void *)pl_fruit, sizeof pl_fruit);
	bwrite(fd, (void *)&current_fruit, sizeof current_fruit);
	savefruitchn(fd, mode);
	savenames(fd, mode);
	save_waterlevel(fd, mode);

	bwrite(fd, (void *)&achieve, sizeof achieve);
	realtime = get_realtime();
	bwrite(fd, (void *)&realtime, sizeof realtime);

	bflush(fd);
}

#ifdef INSURANCE
void savestateinlock() {
	int fd, hpid;
	static boolean havestate = true;
	char whynot[BUFSZ];

	/* When checkpointing is on, the full state needs to be written
	 * on each checkpoint.  When checkpointing is off, only the pid
	 * needs to be in the level.0 file, so it does not need to be
	 * constantly rewritten.  When checkpointing is turned off during
	 * a game, however, the file has to be rewritten once to truncate
	 * it and avoid restoring from outdated information.
	 *
	 * Restricting havestate to this routine means that an additional
	 * noop pid rewriting will take place on the first "checkpoint" after
	 * the game is started or restored, if checkpointing is off.
	 */
	if (flags.ins_chkpt || havestate) {
		/* save the rest of the current game state in the lock file,
		 * following the original int pid, the current level number,
		 * and the current savefile name, which should not be subject
		 * to any internal compression schemes since they must be
		 * readable by an external utility
		 */
		fd = open_levelfile(0, whynot);
		if (fd < 0) {
			pline("%s", whynot);
			pline("Probably someone removed it.");
			killer.name = nhsdupz(whynot);
			done(TRICKED);
			return;
		}

		mread(fd, &hpid, sizeof(hpid));
		if (hackpid != hpid) {
			sprintf(whynot,
				"Level #0 pid (%d) doesn't match ours (%d)!",
				hpid, hackpid);
			pline("%s", whynot);
			killer.name = nhsdupz(whynot);
			done(TRICKED);
		}
		close(fd);

		fd = create_levelfile(0, whynot);
		if (fd < 0) {
			pline("%s", whynot);
			killer.name = nhsdupz(whynot);
			done(TRICKED);
			return;
		}
		bwrite(fd, (void *)&hackpid, sizeof(hackpid));
		if (flags.ins_chkpt) {
			int currlev = ledger_no(&u.uz);

			bwrite(fd, (void *)&currlev, sizeof(currlev));
			save_savefile_name(fd);
			store_version(fd);
#ifdef STORE_PLNAME_IN_FILE
			bwrite(fd, (void *)plname, PL_NSIZ);
#endif
			ustuck_id = (u.ustuck ? u.ustuck->m_id : 0);
			usteed_id = (u.usteed ? u.usteed->m_id : 0);

			savegamestate(fd, WRITE_SAVE);
		}
		bclose(fd);
	}
	havestate = flags.ins_chkpt;
}
#endif

void savelev(int fd, xchar lev, int mode) {
	/* if we're tearing down the current level without saving anything
	   (which happens upon entrance to the endgame or after an aborted
	   restore attempt) then we don't want to do any actual I/O */
	if (mode == FREE_SAVE) goto skip_lots;
	if (iflags.purge_monsters) {
		/* purge any dead monsters (necessary if we're starting
		 * a panic save rather than a normal one, or sometimes
		 * when changing levels without taking time -- e.g.
		 * create statue trap then immediately level teleport) */
		dmonsfree();
	}

	if (fd < 0) panic("Save on bad file!"); /* impossible */
	if (lev >= 0 && lev <= maxledgerno())
		level_info[lev].flags |= VISITED;
	bwrite(fd, &hackpid, sizeof(hackpid));
	bwrite(fd, &lev, sizeof(lev));
	bwrite(fd, levl, sizeof(levl));
	bwrite(fd, &monstermoves, sizeof(monstermoves));
	bwrite(fd, &upstair, sizeof(stairway));
	bwrite(fd, &dnstair, sizeof(stairway));
	bwrite(fd, &upladder, sizeof(stairway));
	bwrite(fd, &dnladder, sizeof(stairway));
	bwrite(fd, &sstairs, sizeof(stairway));
	bwrite(fd, &updest, sizeof(dest_area));
	bwrite(fd, &dndest, sizeof(dest_area));
	bwrite(fd, &level.flags, sizeof(level.flags));
	bwrite(fd, doors, sizeof(doors));
	save_rooms(fd); /* no dynamic memory to reclaim */

	/* from here on out, saving also involves allocated memory cleanup */
skip_lots:
	/* must be saved before mons, objs, and buried objs */
	save_timers(fd, mode, RANGE_LEVEL);
	save_light_sources(fd, mode, RANGE_LEVEL);

	savemonchn(fd, fmon, mode);
	save_worm(fd, mode); /* save worm information */
	savetrapchn(fd, ftrap, mode);
	saveobjchn(fd, fobj, mode);
	saveobjchn(fd, level.buriedobjlist, mode);
	saveobjchn(fd, billobjs, mode);
	if (release_data(mode)) {
		fmon = 0;
		ftrap = 0;
		fobj = 0;
		level.buriedobjlist = 0;
		billobjs = 0;
	}
	save_engravings(fd, mode);
	savedamage(fd, mode);
	save_regions(fd, mode);
	if (mode != FREE_SAVE) bflush(fd);
}

static int bw_fd = -1;
static FILE *bw_FILE = 0;
static boolean buffering = false;

void bufon(int fd) {
#ifdef UNIX
	if (bw_fd != fd) {
		if (bw_fd >= 0)
			panic("double buffering unexpected");
		bw_fd = fd;
		if ((bw_FILE = fdopen(fd, "w")) == 0)
			panic("buffering of file %d failed", fd);
	}
#endif
	buffering = true;
}

void bufoff(int fd) {
	bflush(fd);
	buffering = false;
}

void bflush(int fd) {
#ifdef UNIX
	if (fd == bw_fd) {
		if (fflush(bw_FILE) == EOF)
			panic("flush of savefile failed!");
	}
#endif
	return;
}

void bwrite(int fd, const void *loc, unsigned num) {
	boolean failed;

#ifdef UNIX
	if (buffering) {
		if (fd != bw_fd)
			panic("unbuffered write to fd %d (!= %d)", fd, bw_fd);

		failed = (fwrite(loc, (int)num, 1, bw_FILE) != 1);
	} else
#endif /* UNIX */
	{
		/* lint wants the 3rd arg of write to be an int; lint -p an unsigned */
		failed = (write(fd, loc, num) != num);
	}

	if (failed) {
#if defined(UNIX) || defined(__EMX__)
		if (program_state.done_hup)
			terminate(EXIT_FAILURE);
		else
#endif
			panic("cannot write %u bytes to file #%d", num, fd);
	}
}

void bclose(int fd) {
	bufoff(fd);
#ifdef UNIX
	if (fd == bw_fd) {
		fclose(bw_FILE);
		bw_fd = -1;
		bw_FILE = 0;
	} else
#endif
		close(fd);
}

static void savelevchn(int fd, int mode) {
	s_level *tmplev, *tmplev2;
	int cnt = 0;

	for (tmplev = sp_levchn; tmplev; tmplev = tmplev->next)
		cnt++;
	if (perform_bwrite(mode))
		bwrite(fd, (void *)&cnt, sizeof(int));

	for (tmplev = sp_levchn; tmplev; tmplev = tmplev2) {
		tmplev2 = tmplev->next;
		if (perform_bwrite(mode))
			bwrite(fd, (void *)tmplev, sizeof(s_level));
		if (release_data(mode))
			free(tmplev);
	}
	if (release_data(mode))
		sp_levchn = 0;
}

static void savedamage(int fd, int mode) {
	struct damage *damageptr, *tmp_dam;
	uint xl = 0;

	damageptr = level.damagelist;
	for (tmp_dam = damageptr; tmp_dam; tmp_dam = tmp_dam->next)
		xl++;
	if (perform_bwrite(mode))
		bwrite(fd, (void *)&xl, sizeof(xl));

	while (xl--) {
		if (perform_bwrite(mode))
			bwrite(fd, (void *)damageptr, sizeof(*damageptr));
		tmp_dam = damageptr;
		damageptr = damageptr->next;
		if (release_data(mode))
			free(tmp_dam);
	}
	if (release_data(mode))
		level.damagelist = 0;
}

static void saveobjchn(int fd, struct obj *otmp, int mode) {
	struct obj *otmp2;
	uint xl;
	int minusone = -1;

	while (otmp) {
		otmp2 = otmp->nobj;
		if (perform_bwrite(mode)) {
			xl = otmp->oxlth + otmp->onamelth;
			bwrite(fd, (void *)&xl, sizeof(int));
			bwrite(fd, (void *)otmp, xl + sizeof(struct obj));
		}
		if (Has_contents(otmp))
			saveobjchn(fd, otmp->cobj, mode);
		if (release_data(mode)) {
			//if (otmp->oclass == FOOD_CLASS) food_disappears(otmp);

			/*
			 * If these are on the floor, the discarding could
			 * be because of a game save, or we could just be changing levels.
			 * Always invalidate the pointer, but ensure that we have
			 * the o_id in order to restore the pointer on reload.
			 */

			if (otmp == context.victual.piece) {
				/* Store the o_id of the victual if mismatched */
				if (context.victual.o_id != otmp->o_id)
					context.victual.o_id = otmp->o_id;

				/* invalidate the pointer; on reload it will get restored */
				context.victual.piece = NULL;
			}
			if (otmp == context.tin.tin) {
				/* Store the o_id of your tin */
				if (context.tin.o_id != otmp->o_id)
					context.tin.o_id = otmp->o_id;

				/* invalidate the pointer; on reload it will get restored */
				context.tin.tin = NULL;
			}

			//if (otmp->oclass == SPBOOK_CLASS) book_disappears(otmp);

			if (otmp == context.spbook.book) {
				/* Store the o_id of your spellbook */
				if (context.spbook.o_id != otmp->o_id)
					context.spbook.o_id = otmp->o_id;

				/* invalidate the pointer; on reload it will get restored */
				context.spbook.book = NULL;
			}
			otmp->where = OBJ_FREE; /* set to free so dealloc will work */
			otmp->timed = 0;	/* not timed any more */
			otmp->lamplit = 0;	/* caller handled lights */
			dealloc_obj(otmp);
		}
		otmp = otmp2;
	}
	if (perform_bwrite(mode))
		bwrite(fd, (void *)&minusone, sizeof(int));
}

/*
 * Used by save_mtraits() in mkobj.c to ensure
 * that all the monst related information is stored in
 * an OATTACHED_MONST structure.
 */
void *mon_to_buffer(struct monst *mtmp, int *osize) {
       char *spot;
       int lth, k, xlth[7] = {0};
       void *buffer = NULL, *xptr[7] = {0};
       struct monst *mbuf;

       lth = sizeof(struct monst);


       if (mtmp->mextra) {
               if (MNAME(mtmp)) {
                       xlth[0] = strlen(MNAME(mtmp)) + 1;
                       xptr[0] = MNAME(mtmp);
               }
               if (EGD(mtmp)) {
                       xlth[1] = sizeof(struct egd);
                       xptr[1] = EGD(mtmp);
               }
               if (EPRI(mtmp)) {
                       xlth[2] = sizeof(struct epri);
                       xptr[2] = EPRI(mtmp);
               }
               if (ESHK(mtmp)) {
                       xlth[3] = sizeof(struct eshk);
                       xptr[3] = ESHK(mtmp);
               }
               if (EMIN(mtmp)) {
                       xlth[4] = sizeof(struct emin);
                       xptr[4] = EMIN(mtmp);
               }
               if (EDOG(mtmp)) {
                       xlth[5] = sizeof(struct edog);
                       xptr[5] = EDOG(mtmp);
               }
               if (EGYP(mtmp)) {
                       xlth[6] = sizeof(struct egyp);
                       xptr[6] = EGYP(mtmp);
               }
       }
       for (k = 0; k < SIZE(xlth); ++k) {
               lth += sizeof(int);
               lth += xlth[k];
       }
       if (osize) *osize = lth;

       buffer = alloc(lth);

       spot = buffer;
       memcpy(spot, mtmp, sizeof(struct monst));
       spot += sizeof(struct monst);

       mbuf = (struct monst *)buffer;
       mbuf->mextra = NULL;

       for (k = 0; k < SIZE(xlth); ++k) {
               lth = xlth[k];
               memcpy(spot, &lth, sizeof(lth));
               spot += sizeof(lth);
               if (lth > 0 && xptr[k] != 0) {
                       memcpy(spot, xptr[k], lth);
                       spot += lth;
               }
       }

       return buffer;
}

static void savemonchn(int fd, struct monst *mtmp, int mode) {
	struct monst *mtmp2;
	int buflen, minusone = -1;

	struct permonst *monbegin = &mons[0];

	if (perform_bwrite(mode))
		bwrite(fd, (void *)&monbegin, sizeof(monbegin));

	while (mtmp) {
		mtmp2 = mtmp->nmon;

		if (perform_bwrite(mode)) {
			buflen = sizeof(struct monst);
			bwrite(fd, &buflen, sizeof(int));
#if 0
			void *buffer = mon_to_buffer(mtmp, &buflen);
			bwrite(fd, buffer, buflen);
#else
			bwrite(fd, mtmp, buflen);

			if (!mtmp->mextra) mtmp->mextra = newmextra();

			if (MNAME(mtmp)) buflen = strlen(MNAME(mtmp)) + 1;
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, MNAME(mtmp), buflen);

			if (EGD(mtmp)) buflen = sizeof(struct egd);
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, EGD(mtmp), buflen);

			if (EPRI(mtmp)) buflen = sizeof(struct epri);
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, EPRI(mtmp), buflen);


			if (ESHK(mtmp)) buflen = sizeof(struct eshk);
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, ESHK(mtmp), buflen);

			if (EMIN(mtmp)) buflen = sizeof(struct emin);
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, EMIN(mtmp), buflen);

			if (EDOG(mtmp)) buflen = sizeof(struct edog);
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, EDOG(mtmp), buflen);

			if (EGYP(mtmp)) buflen = sizeof(struct egyp);
			else buflen = 0;
			bwrite(fd, &buflen, sizeof(int));
			if (buflen > 0)
				bwrite(fd, EGYP(mtmp), buflen);
#endif

			if (mtmp->isshk) save_shk_bill(fd, mtmp);
		}
		if (mtmp->minvent)
			saveobjchn(fd, mtmp->minvent, mode);
		if (release_data(mode))
			dealloc_monst(mtmp);
		mtmp = mtmp2;
	}

	if (perform_bwrite(mode))
		bwrite(fd, (void *)&minusone, sizeof(int));
}

static void savetrapchn(int fd, struct trap *trap, int mode) {
	struct trap *trap2;

	while (trap) {
		trap2 = trap->ntrap;
		if (perform_bwrite(mode))
			bwrite(fd, (void *)trap, sizeof(struct trap));
		if (release_data(mode))
			dealloc_trap(trap);
		trap = trap2;
	}
	if (perform_bwrite(mode))
		bwrite(fd, (void *)nulls, sizeof(struct trap));
}

/* save all the fruit names and ID's; this is used only in saving whole games
 * (not levels) and in saving bones levels.  When saving a bones level,
 * we only want to save the fruits which exist on the bones level; the bones
 * level routine marks nonexistent fruits by making the fid negative.
 */
void savefruitchn(int fd, int mode) {
	struct fruit *f2, *f1;

	f1 = ffruit;
	while (f1) {
		f2 = f1->nextf;
		if (f1->fid >= 0 && perform_bwrite(mode))
			bwrite(fd, (void *)f1, sizeof(struct fruit));
		if (release_data(mode))
			dealloc_fruit(f1);
		f1 = f2;
	}
	if (perform_bwrite(mode))
		bwrite(fd, nulls, sizeof(struct fruit));
	if (release_data(mode))
		ffruit = 0;
}

void free_percent_color_options(const struct percent_color_option *list_head) {
	if (list_head == NULL) return;
	free_percent_color_options(list_head->next);
	free(list_head);
}

void free_text_color_options(const struct text_color_option *list_head) {
	if (list_head == NULL) return;
	free_text_color_options(list_head->next);
	free(list_head->text);
	free(list_head);
}

void free_status_colors() {
	free_percent_color_options(hp_colors);
	hp_colors = NULL;
	free_percent_color_options(pw_colors);
	pw_colors = NULL;
	free_text_color_options(text_colors);
	text_colors = NULL;
}

/* also called by prscore(); this probably belongs in dungeon.c... */
/*
 * [ALI] Also called by init_dungeons() for the sake of the GTK interface
 * and the display_score callback of the proxy interface. For this purpose,
 * the previous dungeon must be discarded.
 */
void free_dungeons() {
	savelevchn(0, FREE_SAVE);
	save_dungeon(0, false, true);
	return;
}

void free_menu_coloring() {
	struct menucoloring *tmp = menu_colorings;

	while (tmp) {
		struct menucoloring *tmp2 = tmp->next;
		tre_regfree(&tmp->match);
		free(tmp);
		tmp = tmp2;
	}
	return;
}

void freedynamicdata() {
	free_status_colors();
	unload_qtlist();
	free_invbuf(); /* let_to_name (invent.c) */
	free_menu_coloring();
	tmp_at(DISP_FREEMEM, 0); /* temporary display effects */
#define freeobjchn(X)	      (saveobjchn(0, X, FREE_SAVE), X = 0)
#define freemonchn(X)	      (savemonchn(0, X, FREE_SAVE), X = 0)
#define freetrapchn(X)	      (savetrapchn(0, X, FREE_SAVE), X = 0)
#define freefruitchn()	      savefruitchn(0, FREE_SAVE)
#define freenames()	      savenames(0, FREE_SAVE)
#define free_waterlevel()     save_waterlevel(0, FREE_SAVE)
#define free_worm()	      save_worm(0, FREE_SAVE)
#define free_killers()	      save_killers(0, FREE_SAVE)
#define free_timers(R)	      save_timers(0, FREE_SAVE, R)
#define free_light_sources(R) save_light_sources(0, FREE_SAVE, R);
#define free_engravings()     save_engravings(0, FREE_SAVE)
#define freedamage()	      savedamage(0, FREE_SAVE)
#define free_animals()	      mon_animal_list(false)

	/* move-specific data */
	dmonsfree(); /* release dead monsters */

	/* game-state data */
	free_killers();
	free_timers(RANGE_LEVEL);
	free_light_sources(RANGE_LEVEL);
	freemonchn(fmon);
	free_worm(); /* release worm segment information */
	freetrapchn(ftrap);
	freeobjchn(fobj);
	freeobjchn(level.buriedobjlist);
	freeobjchn(billobjs);
	free_engravings();
	freedamage();

	/* game-state data */
	free_timers(RANGE_GLOBAL);
	free_light_sources(RANGE_GLOBAL);
	freeobjchn(invent);
	freeobjchn(migrating_objs);
	freemonchn(migrating_mons);
	freemonchn(mydogs); /* ascension or dungeon escape */
	free_animals();
	freefruitchn();
	freenames();
	free_waterlevel();
	free_dungeons();

	/* some pointers in iflags */
	if (iflags.wc_font_map) free(iflags.wc_font_map);
	if (iflags.wc_font_message) free(iflags.wc_font_message);
	if (iflags.wc_font_text) free(iflags.wc_font_text);
	if (iflags.wc_font_menu) free(iflags.wc_font_menu);
	if (iflags.wc_font_status) free(iflags.wc_font_status);
	if (iflags.wc_tile_file) free(iflags.wc_tile_file);
	free_autopickup_exceptions();

	return;
}
/*save.c*/
