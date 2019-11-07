/*	SCCS Id: @(#)allmain.c	3.4	2003/04/02	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/* various code that was replicated in *main.c */

#include "hack.h"
#ifndef NO_SIGNAL
#include <signal.h>
#endif
#include "patchlevel.h"

#ifdef POSITIONBAR
static void do_positionbar(void);
#endif

#define decrnknow(spell)	spl_book[spell].sp_know--
#define spellid(spell)		spl_book[spell].sp_id
#define spellknow(spell)	spl_book[spell].sp_know

void moveloop(void) {
#ifdef WIN32
	char ch;
	int abort_lev;
#endif
	int moveamt = 0, wtcap = 0, change = 0;
	boolean didmove = false, monscanmove = false;

	flags.moonphase = phase_of_the_moon();
	if (flags.moonphase == FULL_MOON) {
		pline("You are lucky!  Full moon tonight.");
		change_luck(1);
	} else if(flags.moonphase == NEW_MOON) {
		pline("Be careful!  New moon tonight.");
	}
	flags.friday13 = friday_13th();
	if (flags.friday13) {
		pline("Watch out!  Bad things can happen on Friday the 13th.");
		change_luck(-1);
	}
	/* KMH -- February 2 */
	flags.groundhogday = groundhog_day();
	if (flags.groundhogday) {
		pline("Happy Groundhog Day!");
	}

	initrack();


	/* Note:  these initializers don't do anything except guarantee that
	   we're linked properly.
	 */
	decl_init();
	monst_init();
	monstr_init();	/* monster strengths */
	objects_init();

	commands_init();

	encumber_msg(); /* in case they auto-picked up something */
	if (defer_see_monsters) {
		defer_see_monsters = false;
		see_monsters();
	}

	u.uz0.dlevel = u.uz.dlevel;
	youmonst.movement = NORMAL_SPEED;	/* give the hero some movement points */

	for(;;) {
		get_nh_event();
#ifdef POSITIONBAR
		do_positionbar();
#endif

		didmove = flags.move;
		if(didmove) {
			/* actual time passed */
			youmonst.movement -= NORMAL_SPEED;

			do { /* hero can't move this turn loop */
				wtcap = encumber_msg();

				flags.mon_moving = true;
				do {
					monscanmove = movemon();
					if (youmonst.movement > NORMAL_SPEED) {
						break;        /* it's now your turn */
					}
				} while (monscanmove);
				flags.mon_moving = false;

				if (!monscanmove && youmonst.movement < NORMAL_SPEED) {
					/* both you and the monsters are out of steam this round */
					/* set up for a new turn */
					struct monst *mtmp;
					mcalcdistress();	/* adjust monsters' trap, blind, etc */

					/* reallocate movement rations to monsters */
					for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
						mtmp->movement += mcalcmove(mtmp);
					}

					if(!rn2(u.uevent.udemigod ? 25 :
					                (depth(&u.uz) > depth(&stronghold_level)) ? 50 : 70)) {
						makemon(NULL, 0, 0, NO_MM_FLAGS);
					}

					/* calculate how much time passed. */
					if (u.usteed && u.umoved) {
						/* your speed doesn't augment steed's speed */
						moveamt = mcalcmove(u.usteed);
					} else {
						moveamt = youmonst.data->mmove;

						if (Very_fast) {	/* speed boots or potion */
							/* average movement is 1.67 times normal */
							moveamt += NORMAL_SPEED / 2;
							if (rn2(3) == 0) {
								moveamt += NORMAL_SPEED / 2;
							}
						} else if (Fast) {
							/* average movement is 1.33 times normal */
							if (rn2(3) != 0) {
								moveamt += NORMAL_SPEED / 2;
							}
						}
						if (tech_inuse(T_BLINK)) { /* TECH: Blinking! */
							/* Case    Average  Variance
							 * -------------------------
							 * Normal    12         0
							 * Fast      16        12
							 * V fast    20        12
							 * Blinking  24        12
							 * F & B     28        18
							 * V F & B   30        18
							 */
							moveamt += NORMAL_SPEED * 2 / 3;
							if (rn2(3) == 0) {
								moveamt += NORMAL_SPEED / 2;
							}
						}
					}

					switch (wtcap) {
					case UNENCUMBERED:
						break;
					case SLT_ENCUMBER:
						moveamt -= (moveamt / 4);
						break;
					case MOD_ENCUMBER:
						moveamt -= (moveamt / 2);
						break;
					case HVY_ENCUMBER:
						moveamt -= ((moveamt * 3) / 4);
						break;
					case EXT_ENCUMBER:
						moveamt -= ((moveamt * 7) / 8);
						break;
					default:
						break;
					}

					youmonst.movement += moveamt;
					if (youmonst.movement < 0) {
						youmonst.movement = 0;
					}
					settrack();

					monstermoves++;
					moves++;

					/********************************/
					/* once-per-turn things go here */
					/********************************/

					if (flags.bypasses) {
						clear_bypasses();
					}
					if(Glib) {
						glibr();
					}
					nh_timeout();
					run_regions();

#ifdef DUNGEON_GROWTH
					dgn_growths(true, true);
#endif

					if (u.ublesscnt) {
						u.ublesscnt--;
					}

					if(flags.time && !flags.run) {
						flags.botl = 1;
					}

					/* One possible result of prayer is healing.  Whether or
					 * not you get healed depends on your current hit points.
					 * If you are allowed to regenerate during the prayer, the
					 * end-of-prayer calculation messes up on this.
					 * Another possible result is rehumanization, which requires
					 * that encumbrance and movement rate be recalculated.
					 */
					if (u.uinvulnerable) {
						/* for the moment at least, you're in tiptop shape */
						wtcap = UNENCUMBERED;
					} else if (Upolyd && youmonst.data->mlet == S_EEL && !is_pool(u.ux,u.uy) && !Is_waterlevel(&u.uz)) {
						if (u.mh > 1) {
							u.mh--;
							flags.botl = 1;
						} else if (u.mh < 1) {
							rehumanize();
						}
					} else if (Upolyd && u.mh < u.mhmax) {
						if (u.mh < 1) {
							rehumanize();
						} else if (Regeneration ||
						                (wtcap < MOD_ENCUMBER && !(moves%20))) {
							flags.botl = 1;
							u.mh++;
						}
					} else if (u.uhp < u.uhpmax &&
					                (wtcap < MOD_ENCUMBER || !u.umoved || Regeneration)) {
						/*
						 * KMH, balance patch -- New regeneration code
						 * Healthstones have been added, which alter your effective
						 * experience level and constitution (-2 cursed, +1 uncursed,
						 * +2 blessed) for the basis of regeneration calculations.
						 */

						int efflev = u.ulevel + u.uhealbonus;
						int effcon = ACURR(A_CON) + u.uhealbonus;
						int heal = 1;


						if (efflev > 9 && !(moves % 3)) {
							if (effcon <= 12) {
								heal = 1;
							} else {
								heal = rnd(effcon);
								if (heal > efflev-9) {
									heal = efflev-9;
								}
							}
							flags.botl = 1;
							u.uhp += heal;
							if(u.uhp > u.uhpmax) {
								u.uhp = u.uhpmax;
							}
						} else if (Regeneration ||
						                (efflev <= 9 &&
						                 !(moves % ((MAXULEV+12) / (u.ulevel+2) + 1)))) {
							flags.botl = 1;
							u.uhp++;
						}
					}

					if (!u.uinvulnerable && u.uen > 0 && u.uhp < u.uhpmax &&
					                tech_inuse(T_CHI_HEALING)) {
						u.uen--;
						u.uhp++;
						flags.botl = 1;
					}

					/* moving around while encumbered is hard work */
					if (wtcap > MOD_ENCUMBER && u.umoved) {
						if(!(wtcap < EXT_ENCUMBER ? moves%30 : moves%10)) {
							if (Upolyd && u.mh > 1) {
								u.mh--;
							} else if (!Upolyd && u.uhp > 1) {
								u.uhp--;
							} else {
								pline("You pass out from exertion!");
								exercise(A_CON, false);
								fall_asleep(-10, false);
							}
						}
					}


					/* KMH -- OK to regenerate if you don't move */
					if ((u.uen < u.uenmax) && (Energy_regeneration ||
					                           ((wtcap < MOD_ENCUMBER || !flags.mv) &&
					                            (!(moves%((MAXULEV + 15 - u.ulevel) *
					                                      (Role_if(PM_WIZARD) ? 3 : 4) / 6)))))) {
						u.uen += rn1((int)(ACURR(A_WIS) + ACURR(A_INT)) / 15 + 1,1);
#ifdef WIZ_PATCH_DEBUG
						pline("mana was = %d now = %d",temp,u.uen);
#endif

						if (u.uen > u.uenmax) {
							u.uen = u.uenmax;
						}
						flags.botl = 1;
					}

					if(!u.uinvulnerable) {
						if(Teleportation && !rn2(85)) {
							xchar old_ux = u.ux, old_uy = u.uy;
							tele();
							if (u.ux != old_ux || u.uy != old_uy) {
								if (!next_to_u()) {
									check_leash(&youmonst, old_ux, old_uy, true);
								}
								/* clear doagain keystrokes */
								pushch(0);
								savech(0);
							}
						}
						/* delayed change may not be valid anymore */
						if ((change == 1 && !Polymorph) ||
						                (change == 2 && u.ulycn == NON_PM)) {
							change = 0;
						}
						if(Polymorph && !rn2(100)) {
							change = 1;
						} else if (u.ulycn >= LOW_PM && !Upolyd &&
						                !rn2(80 - (20 * night()))) {
							change = 2;
						}
						if (change && !Unchanging) {
							if (multi >= 0) {
								if (occupation) {
									stop_occupation();
								} else {
									nomul(0);
								}
								if (change == 1) {
									polyself(false);
								} else {
									you_were();
								}
								change = 0;
							}
						}
					}	/* !u.uinvulnerable */

					if(Searching && multi >= 0) {
						(void) dosearch0(true);
					}
					dosounds();
					do_storms();
					gethungry();
					age_spells();
					exerchk();
					invault();
					if (u.uhave.amulet) {
						amulet();
					}
					if (!rn2(40+(int)(ACURR(A_DEX)*3))) {
						u_wipe_engr(rnd(3));
					}
					if (u.uevent.udemigod && !u.uinvulnerable) {
						if (u.udg_cnt) {
							u.udg_cnt--;
						}
						if (!u.udg_cnt) {
							intervene();
							u.udg_cnt = rn1(200, 50);
						}
					}
					restore_attrib();

					/* underwater and waterlevel vision are done here */
					if (Is_waterlevel(&u.uz)) {
						movebubbles();
					} else if (Underwater) {
						under_water(0);
					}
					/* vision while buried done here */
					else if (u.uburied) {
						under_ground(0);
					}

					/* when immobile, count is in turns */
					if(multi < 0) {
						if (++multi == 0) {	/* finished yet? */
							unmul(NULL);
							/* if unmul caused a level change, take it now */
							if (u.utotype) {
								deferred_goto();
							}
						}
					}
				}
			} while (youmonst.movement<NORMAL_SPEED); /* hero can't move loop */

			/******************************************/
			/* once-per-hero-took-time things go here */
			/******************************************/


		} /* actual time passed */

		/****************************************/
		/* once-per-player-input things go here */
		/****************************************/

		find_ac();
		if(!flags.mv || Blind) {
			/* redo monsters if hallu or wearing a helm of telepathy */
			if (Hallucination) {	/* update screen randomly */
				see_monsters();
				see_objects();
				see_traps();
				if (u.uswallow) {
					swallowed(0);
				}
			} else if (Unblind_telepat) {
				see_monsters();
			} else if (Warning || Warn_of_mon) {
				see_monsters();
			}

			if (vision_full_recalc) {
				vision_recalc(0);        /* vision! */
			}
		}

#ifdef REALTIME_ON_BOTL
		if(iflags.showrealtime) {
			/* Update the bottom line if the number of minutes has
			 * changed */
			if(get_realtime() / 60 != realtime_data.last_displayed_time / 60) {
				flags.botl = 1;
			}
		}
#endif

		if(flags.botl || flags.botlx) {
			bot();
		}

		flags.move = 1;

		if(multi >= 0 && occupation) {
#ifdef WIN32
			abort_lev = 0;
			if (kbhit()) {
				if ((ch = Getchar()) == ABORT) {
					abort_lev++;
				} else {
					pushch(ch);
				}
			}
			if (!abort_lev && (*occupation)() == 0)
#else
			if ((*occupation)() == 0)
#endif
				occupation = 0;
			if(
#ifdef WIN32
			        abort_lev ||
#endif
			        monster_nearby()) {
				stop_occupation();
				reset_eat();
			}
#ifdef WIN32
			if (!(++occtime % 7)) {
				display_nhwindow(WIN_MAP, false);
			}
#endif
			continue;
		}

		if ((u.uhave.amulet || Clairvoyant) &&
		                !In_endgame(&u.uz) && !BClairvoyant &&
		                !(moves % 15) && !rn2(2)) {
			do_vicinity_map();
		}

		if(u.utrap && u.utraptype == TT_LAVA) {
			if(!is_lava(u.ux,u.uy)) {
				u.utrap = 0;
			} else if (!u.uinvulnerable) {
				u.utrap -= 1<<8;
				if(u.utrap < 1<<8) {
					killer_format = KILLED_BY;
					killer = "molten lava";
					pline("You sink below the surface and die.");
					done(DISSOLVED);
				} else if(didmove && !u.umoved) {
					Norep("You sink deeper into the lava.");
					u.utrap += rnd(4);
				}
			}
		}

		if (iflags.sanity_check || iflags.debug_fuzzer) {
			sanity_check();
		}

#ifdef CLIPPING
		/* just before rhack */
		cliparound(u.ux, u.uy);
#endif

		u.umoved = false;

		if (multi > 0) {
			lookaround();
			if (!multi) {
				/* lookaround may clear multi */
				flags.move = 0;
				if (flags.time) {
					flags.botl = 1;
				}
				continue;
			}
			if (flags.mv) {
				if(multi < COLNO && !--multi) {
					flags.travel = iflags.travel1 = flags.mv = false;
					flags.run = 0;
				}
				domove();
			} else {
				--multi;
				rhack(save_cm);
			}
		} else if (multi == 0) {
#ifdef MAIL
			ckmailstatus();
#endif
			rhack(NULL);
		}
		if (u.utotype) {	/* change dungeon level */
			deferred_goto();        /* after rhack() */
		}
		/* !flags.move here: multiple movement command stopped */
		else if (flags.time && (!flags.move || !flags.mv)) {
			flags.botl = 1;
		}

		if (vision_full_recalc) {
			vision_recalc(0);        /* vision! */
		}
		/* when running in non-tport mode, this gets done through domove() */
		if ((!flags.run || iflags.runmode == RUN_TPORT) &&
		                (multi && (!flags.travel ? !(multi % 7) : !(moves % 7L)))) {
			if (flags.time && flags.run) {
				flags.botl = 1;
			}
			display_nhwindow(WIN_MAP, false);
		}
	}
}

void stop_occupation(void) {
	if(occupation) {
		if (!maybe_finished_meal(true)) {
			pline("You stop %s.", occtxt);
		}
		occupation = 0;
		flags.botl = 1; /* in case u.uhs changed */
		/* fainting stops your occupation, there's no reason to sync.
				sync_hunger();
		*/
		nomul(0);
		pushch(0);
	}
}

void create_gamewindows(void) {
	curses_stupid_hack = false;
	WIN_MESSAGE = create_nhwindow(NHW_MESSAGE);
	WIN_STATUS = create_nhwindow(NHW_STATUS);
	WIN_MAP = create_nhwindow(NHW_MAP);
	WIN_INVEN = create_nhwindow(NHW_MENU);
}

void show_gamewindows(void) {
	display_nhwindow(WIN_STATUS, false);
	display_nhwindow(WIN_MESSAGE, false);
	clear_glyph_buffer();
	display_nhwindow(WIN_MAP, false);
}

void newgame(void) {
	int i;

	flags.ident = 1;

	for (i = 0; i < NUMMONS; i++) {
		mvitals[i].mvflags = mons[i].geno & G_NOCORPSE;
	}

	init_objects();		/* must be before u_init() */
	monstr_init();

	flags.pantheon = -1;	/* role_init() will reset this */
	role_init();		/* must be before init_dungeons(), u_init(),
				 * and init_artifacts() */

	init_dungeons();	/* must be before u_init() to avoid rndmonst()
				 * creating odd monsters for any tins and eggs
				 * in hero's initial inventory */
	init_artifacts();	/* before u_init() in case $WIZKIT specifies
				 * any artifacts */
	u_init();
	init_artifacts1();	/* must be after u_init() */

#ifndef NO_SIGNAL
	signal(SIGINT, (SIG_RET_TYPE) done1);
#endif
#ifdef NEWS
	if(iflags.news) {
		display_file(NEWS, false);
	}
#endif

	load_qtlist();	/* load up the quest text info */
	/*	quest_init();*/	/* Now part of role_init() */

	mklev();
	u_on_upstairs();
	vision_reset();		/* set up internals for level (after mklev) */
	check_special_room(false);

	flags.botlx = 1;

	/* Move the monster from under you or else
	 * makedog() will fail when it calls makemon().
	 *			- ucsfcgl!kneller
	 */

	if(MON_AT(u.ux, u.uy)) {
		mnexto(m_at(u.ux, u.uy));
	}
	makedog();

	docrt();

	if (flags.legacy) {
		flush_screen(1);
		com_pager(1);
	}
#ifdef INSURANCE
	save_currentstate();
#endif
	program_state.something_worth_saving++;	/* useful data now exists */

	/* Start the timer here */
	realtime_data.realtime = (time_t)0L;

#if defined(BSD) && !defined(POSIX_TYPES)
	time((long *)&realtime_data.restoretime);
#else
	time(&realtime_data.restoretime);
#endif

	/* Success! */
	welcome(true);
	return;
}

/* show "welcome [back] to nethack" message at program startup */
void welcome(boolean new_game /* false => restoring an old game */ ) {
	char buf[BUFSZ];
	boolean currentgend = Upolyd ? u.mfemale : flags.female;

	/*
	 * The "welcome back" message always describes your innate form
	 * even when polymorphed or wearing a helm of opposite alignment.
	 * Alignment is shown unconditionally for new games; for restores
	 * it's only shown if it has changed from its original value.
	 * Sex is shown for new games except when it is redundant; for
	 * restores it's only shown if different from its original value.
	 */
	*buf = '\0';
	if (new_game || u.ualignbase[A_ORIGINAL] != u.ualignbase[A_CURRENT]) {
		sprintf(eos(buf), " %s", align_str(u.ualignbase[A_ORIGINAL]));
	}
	if (!urole.name.f &&
	                (new_game ? (urole.allow & ROLE_GENDMASK) == (ROLE_MALE|ROLE_FEMALE) :
	                 currentgend != flags.initgend)) {
		sprintf(eos(buf), " %s", genders[currentgend].adj);
	}

#if 0
	pline(new_game ? "%s %s, welcome to NetHack!  You are a%s %s %s."
	      : "%s %s, the%s %s %s, welcome back to NetHack!",
	      Hello(NULL), plname, buf, urace.adj,
	      (currentgend && urole.name.f) ? urole.name.f : urole.name.m);
#endif
	if (new_game) pline("%s %s, welcome to %s!  You are a%s %s %s.",
		                    Hello(NULL), plname, DEF_GAME_NAME, buf, urace.adj,
		                    (currentgend && urole.name.f) ? urole.name.f : urole.name.m);
	else pline("%s %s, the%s %s %s, welcome back to %s!",
		           Hello(NULL), plname, buf, urace.adj,
		           (currentgend && urole.name.f) ? urole.name.f : urole.name.m,
		           DEF_GAME_NAME);
}

#ifdef POSITIONBAR
static void do_positionbar(void) {
	static char pbar[COLNO];
	char *p;

	p = pbar;
	/* up stairway */
	if (upstair.sx &&
#ifdef DISPLAY_LAYERS
	                (level.locations[upstair.sx][upstair.sy].mem_bg == S_upstair ||
	                 level.locations[upstair.sx][upstair.sy].mem_bg == S_upladder)) {
#else
	                (glyph_to_cmap(level.locations[upstair.sx][upstair.sy].glyph) ==
	                 S_upstair ||
	                 glyph_to_cmap(level.locations[upstair.sx][upstair.sy].glyph) ==
	                 S_upladder)) {
#endif
		*p++ = '<';
		*p++ = upstair.sx;
	}
	if (sstairs.sx &&
#ifdef DISPLAY_LAYERS
	                (level.locations[sstairs.sx][sstairs.sy].mem_bg == S_upstair ||
	                 level.locations[sstairs.sx][sstairs.sy].mem_bg == S_upladder)) {
#else
	                (glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph) ==
	                 S_upstair ||
	                 glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph) ==
	                 S_upladder)) {
#endif
		*p++ = '<';
		*p++ = sstairs.sx;
	}

	/* down stairway */
	if (dnstair.sx &&
#ifdef DISPLAY_LAYERS
	                (level.locations[dnstair.sx][dnstair.sy].mem_bg == S_dnstair ||
	                 level.locations[dnstair.sx][dnstair.sy].mem_bg == S_dnladder)) {
#else
	                (glyph_to_cmap(level.locations[dnstair.sx][dnstair.sy].glyph) ==
	                 S_dnstair ||
	                 glyph_to_cmap(level.locations[dnstair.sx][dnstair.sy].glyph) ==
	                 S_dnladder)) {
#endif
		*p++ = '>';
		*p++ = dnstair.sx;
	}
	if (sstairs.sx &&
#ifdef DISPLAY_LAYERS
	                (level.locations[sstairs.sx][sstairs.sy].mem_bg == S_dnstair ||
	                 level.locations[sstairs.sx][sstairs.sy].mem_bg == S_dnladder)) {
#else
	                (glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph) ==
	                 S_dnstair ||
	                 glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph) ==
	                 S_dnladder)) {
#endif
		*p++ = '>';
		*p++ = sstairs.sx;
	}

	/* hero location */
	if (u.ux) {
		*p++ = '@';
		*p++ = u.ux;
	}
	/* fence post */
	*p = 0;

	update_positionbar(pbar);
}
#endif

time_t get_realtime(void) {
	time_t curtime;

	/* Get current time */
#if defined(BSD) && !defined(POSIX_TYPES)
	time((long *)&curtime);
#else
	time(&curtime);
#endif

	/* Since the timer isn't set until the game starts, this prevents us
	 * from displaying nonsense on the bottom line before it does. */
	if(realtime_data.restoretime == 0) {
		curtime = realtime_data.realtime;
	} else {
		curtime -= realtime_data.restoretime;
		curtime += realtime_data.realtime;
	}

	return curtime;
}

/*allmain.c*/
