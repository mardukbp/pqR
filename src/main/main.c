/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1998-2011   The R Core Team
 *  Copyright (C) 2002-2005  The R Foundation
 *
 *  The changes in pqR from R-2.15.0 distributed by the R Core Team are
 *  documented in the NEWS and MODS files in the top-level source directory.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  http://www.r-project.org/Licenses/
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h> /* avoid redefinition of extern in Defn.h */
#include <float.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Global Variables:  For convenience, all interpeter global symbols
 * ================   are declared in Defn.h as extern -- and defined here.
 *
 * NOTE: This is done by using some preprocessor trickery.  If __MAIN__
 * is defined as below, there is a sneaky
 *     #define extern
 * so that the same code produces both declarations and definitions.
 *
 * This does not include user interface symbols which are included
 * in separate platform dependent modules.
 */

#define __MAIN__
#define USE_FAST_PROTECT_MACROS
#define R_USE_SIGNALS 1
#include "Defn.h"
#include "Rinterface.h"
#include "IOStuff.h"
#include "Fileio.h"
#include "Parse.h"
#include "Startup.h"
#include <helpers/helpers-app.h>

#include <locale.h>

#ifdef ENABLE_NLS
void attribute_hidden nl_Rdummy(void)
{
    /* force this in as packages use it */
    dgettext("R", "dummy - do not translate");
}
#endif


/* The 'real' main() program is in Rmain.c on Unix-alikes, and
   src/gnuwin/front-ends/graphappmain.c on Windows, unless of course
   R is embedded */


/* Detemine whether the stack grows down (+1) or up (-1).  Passed the
   address of a local variable in the caller's stack frame.

   This is put here, though called only from system.c, so that the compiler
   will not be able to inline it.  NOT CURRENTLY USED - stack direction
   is now a constant. */

int R_stack_growth_direction (uintptr_t cvaraddr)
{ 
    int dummy; 
    return (uintptr_t) &dummy < cvaraddr ? 1 : -1; 
}


/* Define a stub for linking to by any module that doesn't know that the helpers
   apparatus is disabled.  (The parentheses below suppress macro expansion.) */

#ifdef HELPERS_DISABLED
void (helpers_wait_until_not_in_use) (SEXP v) { /* no need to do anything */ }
#endif


/* WAIT FOR HELPERS BEOFRE FORK.  Should be called before a call of "fork", 
   so that the child will see memory that is not still being updated.   Will 
   be referenced with an 'extern' declaration in the referencing source file, 
   since helpers-app.h may not be accessible. */

void Rf_wait_for_helpers_before_fork (void)
{
    helpers_wait_for_all();
}


/* DISABLE HELPERS AFTER FORK.  Should be called in the child process after a 
   "fork", so it won't try to use helper threads.  Will be referenced with an 
   'extern' declaration in the referencing source file, since helpers-app.h 
   may not be accessible. */

void Rf_disable_helpers_after_fork (void)
{
    helpers_disable(1);
    helpers_trace(0);
}


void Rf_callToplevelHandlers(SEXP expr, SEXP value, Rboolean succeeded,
			     Rboolean visible);

static int ParseBrowser(SEXP, SEXP);


extern void InitDynload(void);


/* --------------------------------------------------------------------------
   READ-EVAL-PRINT LOOP (REPL) WITH INPUT FROM A FILE

   Repeatedly calls R_Parse1Stream (in parse.c) to read an expression 
   to evaluate.  See R_ReplIteration below for some relevant comments. 
*/

extern int R_fgetc (FILE*);

static int file_getc (void *fp) { return R_fgetc ((FILE *) fp); }

static void R_ReplFile(FILE *fp, SEXP rho)
{
    ParseStatus status;
    SrcRefState ParseState;
    int savestack;
    SEXP value;

    char saved_ps[R_parse_state_size];
    int retain = 0;

    /* next must be paired with R_FinalizeSrcRefState (see parse.c) */
    R_InitSrcRefState (&ParseState, FALSE);

    savestack = R_PPStackTop;

    for (;;) {

	R_PPStackTop = savestack;

        /* Note: R_CurrentExpr is protected as a root by the garbage collector*/

	R_CurrentExpr = R_Parse1Stream (file_getc, (void *) fp, 
                                        &status, &ParseState,
                                        FALSE, retain, saved_ps);
        retain = 1;

	switch (status) {
	case PARSE_NULL:
	    break;

	case PARSE_OK:

	    R_Visible = FALSE;
	    R_EvalDepth = 0;
	    resetTimeLimits();

	    PROTECT(value = evalv (R_CurrentExpr, rho, VARIANT_PENDING_OK));
	    SET_SYMVALUE(R_LastvalueSymbol, value);

	    if (R_Visible) {
                WAIT_UNTIL_COMPUTED(value);
		PrintValueEnv (value, rho);
            }
	    if (R_CollectWarnings)
		PrintWarnings();

            R_CurrentExpr = R_NilValue;  /* recover memory */
            UNPROTECT(1);
	    break;

	case PARSE_ERROR:
	    parseError(R_NilValue, R_ParseError);
	    break;

	case PARSE_EOF:

            /* next must be paired with R_InitSrcRefState (see parse.c) */
	    R_FinalizeSrcRefState(&ParseState);
	    return;
	    break;

	default:
            abort();
	}
    }
}


/* --------------------------------------------------------------------------
   READ-EVAL-PRINT LOOP WITH INTERACTIVE INPUT
 */

/*
  Comment from 2.15.0 (still more-or-less right, though a bit misplaced):

  This is a reorganization of the REPL (Read-Eval-Print Loop) to separate
  the loop from the actions of the body. The motivation is to make the iteration
  code (Rf_ReplIteration) available as a separately callable routine
  to avoid cutting and pasting it when one wants a single iteration
  of the loop. This is needed as we allow different implementations
  of event loops. Currently (summer 2002), we have a package in
  preparation that uses Rf_ReplIteration within either the
  Tcl or Gtk event loop and allows either (or both) loops to
  be used as a replacement for R's loop and take over the event
  handling for the R process.

  The modifications here are intended to leave the semantics of the REPL
  unchanged, just separate into routines. So the variables that maintain
  the state across iterations of the loop are organized into a structure
  and passed to Rf_ReplIteration() from Rf_ReplConsole(). 
*/


/* (local) Structure for maintaining and exchanging the state between
   Rf_ReplConsole and its worker routine Rf_ReplIteration which is the
   implementation of the body of the REPL.

   In the future, we may need to make this accessible to packages
   and so put it into one of the public R header files. */

typedef struct {
  ParseStatus    status;
  int            prompt_type;
  int            browselevel;
  unsigned char  buf[CONSOLE_BUFFER_SIZE+1];
  unsigned char *bufp;
} R_ReplState;

static char BrowsePrompt[20];

char *R_PromptString(int browselevel, int type)
{
    if (R_Slave) {
	BrowsePrompt[0] = '\0';
	return BrowsePrompt;
    }
    else {
	if(type == 1) {
	    if(browselevel) {
		sprintf(BrowsePrompt, "Browse[%d]> ", browselevel);
		return BrowsePrompt;
	    }
	    return (char *)CHAR(STRING_ELT(GetOption1(install("prompt")), 0));
	}
	else {
	    return (char *)CHAR(STRING_ELT(GetOption1(install("continue")), 0));
	}
    }
}

static int keepSource;

static int ReplGetc (void *vstate)
{
    R_ReplState *state = (R_ReplState *) vstate;
    int c;

    while (*state->bufp == 0) {
        R_Busy(0);
        if (R_ReadConsole(R_PromptString(state->browselevel,state->prompt_type),
                          state->buf, CONSOLE_BUFFER_SIZE, 1) == 0)
            return EOF;
        state->buf[CONSOLE_BUFFER_SIZE] = 0;  /* just in case... */
        state->bufp = state->buf;
        state->prompt_type = 2;
    }

    c = *state->bufp++;
    if (keepSource) 
        R_IoBufferPutc (c, &R_ConsoleIob);

    return c;
}

/* The body of the Read-Eval-Print Loop.  The Rf_ReplIteration
   function is possibly called from somewhere else, so it's interface
   is preserved here.  But internally, the static function REPL_iter
   is used, which allows for preserving parse state, and hence proper
   handling of "else" after "if" in a non-interactive session.

   If the input can be parsed correctly,

       1) the resulting expression is evaluated,
       2) the result assigned to .Last.Value,
       3) top-level task handlers are invoked.

   The bufp pointer into buf is moved to the next starting point, 
   i.e. the end of the first line or after the terminating ';'. */

static int REPL_iter (SEXP rho, R_ReplState *state, int first, char *saved_ps)
{
    SEXP R_scalar_stack_at_start = R_scalar_stack;

    int browsevalue;
    SEXP value, thisExpr;
    SrcRefState ParseState;
    Rboolean wasDisplayed = FALSE;

    state->prompt_type = *state->bufp == 0 ? 1 : 2;

    keepSource = asLogical (GetOption1 (install ("keep.source")));

    /* next must be paired with R_FinalizeSrcRefState (see parse.c) */
    R_InitSrcRefState (&ParseState, keepSource);

    if (keepSource)
        R_IoBufferWriteReset(&R_ConsoleIob);

   /* Note:  R_CurrentExpr is protected as a root by the garbage collector.
      If it were protected explicitly, the pairing of R_InitSrcRefState with
      R_FinalizeSrcRefState would be disrupted. */

    R_CurrentExpr = R_Parse1Stream (ReplGetc, (void *) state, 
                                    &state->status, &ParseState, 
                                    !R_PeekForElse, !first, saved_ps);
    if (keepSource) {
        int buflen = R_IoBufferWriteOffset(&R_ConsoleIob);
        char buf[buflen+1];
        int i;
        R_IoBufferReadReset(&R_ConsoleIob);
        for (i = 0; i < buflen; i++)
            buf[i] = R_IoBufferGetc(&R_ConsoleIob);
        buf[buflen] = 0;
        R_IoBufferWriteReset(&R_ConsoleIob);
        R_TextForSrcRefState (&ParseState, buf);
    }

    /* next must be paired with R_InitSrcRefState (see parse.c) */
    R_FinalizeSrcRefState(&ParseState);
    
    switch (state->status) {

    case PARSE_NULL:

	/* The intention here is to break on CR but not on other
	   null statements: see PR#9063 */
	if (state->browselevel > 0 && !R_DisableNLinBrowser
	    && !strcmp((char *) state->buf, "\n")) 
            return -1;
	return 1;

    case PARSE_OK:

	if (state->browselevel > 0) {
	    browsevalue = ParseBrowser(R_CurrentExpr, rho);
	    if (browsevalue == 1) {
                UNPROTECT(1);
                return -1;
            }
	    if (browsevalue == 2) {
		*state->bufp = 0;  /* discard rest of line */
                UNPROTECT(1);
		return 0;
	    }
	}
	R_Visible = FALSE;
	R_EvalDepth = 0;
	resetTimeLimits();
	thisExpr = R_CurrentExpr;
	R_Busy(1);

        PROTECT(thisExpr);
	PROTECT(value = evalv (thisExpr, rho, VARIANT_PENDING_OK));

	SET_SYMVALUE(R_LastvalueSymbol, value);
	wasDisplayed = R_Visible;
	if (R_Visible) {
            WAIT_UNTIL_COMPUTED(value);
	    PrintValueEnv(value, rho);
        }
	if (R_CollectWarnings)
	    PrintWarnings();
	Rf_callToplevelHandlers(thisExpr, value, TRUE, wasDisplayed);

	R_CurrentExpr = R_NilValue;  /* recover memory */
	UNPROTECT(2);

        if (R_scalar_stack != R_scalar_stack_at_start) abort();
	return 1;

    case PARSE_ERROR:

	parseError(R_NilValue, 0);
	*state->bufp = 0;  /* discard rest of line */
	return 1;

    case PARSE_EOF:

	return -1;

    default:
        abort();
    }
}

int Rf_ReplIteration (SEXP rho, R_ReplState *state)
{
    return REPL_iter (rho, state, TRUE, NULL);
}

static void R_ReplConsole(SEXP rho, int savestack, int browselevel)
{
    char saved_ps [R_Interactive ? 1 : R_parse_state_size];
    R_ReplState state;
    int first, status;

    state.status = PARSE_NULL;
    state.prompt_type = 1;
    state.browselevel = browselevel;
    state.buf[0] = 0;
    state.bufp = state.buf;

    first = TRUE;

    do {

        R_PPStackTop = savestack;

        if (R_Interactive)
            status = REPL_iter (rho, &state, TRUE, NULL);
        else {
            status = REPL_iter (rho, &state, first, saved_ps);
            first = FALSE;
        }

    } while (status >= 0);
}

/* -------------------------------------------------------------------------- */


/* A version of REPL for use with embedded R; just an interface to
   Rf_ReplIteration.  Caller calls R_ReplDLLinit, then repeatedly
   calls R_ReplDLLdo1, which returns -1 on EOF, +1 on evaluation
   without error, and +2 on evaluation that causes an error to
   be trapped at top level. */

static R_ReplState DLLstate;

void R_ReplDLLinit(void)
{
    R_IoBufferInit(&R_ConsoleIob);  /* No longer needed here. Used elsewhere? */
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    DLLstate.browselevel = 0;
    DLLstate.prompt_type = 1;
    DLLstate.buf[0] = DLLstate.buf[CONSOLE_BUFFER_SIZE] = 0;
    DLLstate.bufp = DLLstate.buf;
    keepSource = 0;
}

int R_ReplDLLdo1(void)
{
    int status;
    R_PPStackTop = 0;
    if (SETJMP(R_Toplevel.cjmpbuf) == 0)
        status = Rf_ReplIteration (R_GlobalEnv, &DLLstate);
    else { /* Error trapped */
        *DLLstate.bufp = 0;  /* Discard rest of line */
        status = 2;
    }

    return status;
}


static RETSIGTYPE handleInterrupt(int dummy)
{
    R_interrupts_pending = 1;
    signal(SIGINT, handleInterrupt);
}

/* this flag is set if R internal code is using send() and does not
   want to trigger an error on SIGPIPE (e.g., the httpd code).
   [It is safer and more portable than other methods of handling
   broken pipes on send().]
 */

#ifndef Win32
int R_ignore_SIGPIPE = 0;

static RETSIGTYPE handlePipe(int dummy)
{
    signal(SIGPIPE, handlePipe);
    if (!R_ignore_SIGPIPE) error("ignoring SIGPIPE signal");
}
#endif


#ifdef Win32
static int num_caught = 0;

static void win32_segv(int signum)
{
    /* NB: stack overflow is not an access violation on Win32 */
    {   /* A simple customized print of the traceback */
	SEXP trace, p, q;
	int line = 1, i;
	PROTECT(trace = R_GetTraceback(0));
	if(trace != R_NilValue) {
	    REprintf("\nTraceback:\n");
	    for(p = trace; p != R_NilValue; p = CDR(p), line++) {
		q = CAR(p); /* a character vector */
		REprintf("%2d: ", line);
		for(i = 0; i < LENGTH(q); i++)
		    REprintf("%s", CHAR(STRING_ELT(q, i)));
		REprintf("\n");
	    }
	    UNPROTECT(1);
	}
    }
    num_caught++;
    if(num_caught < 10) signal(signum, win32_segv);
    if(signum == SIGILL)
	error("caught access violation - continue with care");
    else
	error("caught access violation - continue with care");
}
#endif

#if defined(HAVE_SIGALTSTACK) && defined(HAVE_SIGACTION) && defined(HAVE_WORKING_SIGACTION) && defined(HAVE_SIGEMPTYSET)

/* NB: this really isn't safe, but suffices for experimentation for now.
   In due course just set a flag and do this after the return.  OTOH,
   if we do want to bail out with a core dump, need to do that here.

   2005-12-17 BDR */

static unsigned char ConsoleBuf[CONSOLE_BUFFER_SIZE];
extern void R_CleanTempDir(void);

static void sigactionSegv(int signum, siginfo_t *ip, void *context)
{
    char *s;

    /* First check for stack overflow if we know the stack position.
       We assume anything within 16Mb beyond the stack end is a stack overflow.
     */
    if(signum == SIGSEGV && (ip != (siginfo_t *)0) &&
       (intptr_t) R_CStackStart != -1) {
	uintptr_t addr = (uintptr_t) ip->si_addr;
	intptr_t diff = R_CStackDir < 0 ? addr - R_CStackStart 
                                        : R_CStackStart - addr;
	uintptr_t upper = 0x1000000;  /* 16Mb */
	if((intptr_t) R_CStackLimit != -1) upper += R_CStackLimit;
	if(diff > 0 && diff < upper) {
	    REprintf(_("Error: segfault from C stack overflow\n"));
            if (getenv("R_ABORT") == 0 || *getenv("R_ABORT") == 0)
                jump_to_toplevel();
            REprintf("aborting ...\n");
            abort();
	}
    }

    /* need to take off stack checking as stack base has changed */
    R_CStackLimit = (uintptr_t)-1;

    /* Do not translate these messages */
    REprintf("\n *** caught %s ***\n",
	     signum == SIGILL ? "illegal operation" :
	     signum == SIGBUS ? "bus error" : "segfault");
    if(ip != (siginfo_t *)0) {
	if(signum == SIGILL) {

	    switch(ip->si_code) {
#ifdef ILL_ILLOPC
	    case ILL_ILLOPC:
		s = "illegal opcode";
		break;
#endif
#ifdef ILL_ILLOPN
	    case ILL_ILLOPN:
		s = "illegal operand";
		break;
#endif
#ifdef ILL_ILLADR
	    case ILL_ILLADR:
		s = "illegal addressing mode";
		break;
#endif
#ifdef ILL_ILLTRP
	    case ILL_ILLTRP:
		s = "illegal trap";
		break;
#endif
#ifdef ILL_COPROC
	    case ILL_COPROC:
		s = "coprocessor error";
		break;
#endif
	    default:
		s = "unknown";
		break;
	    }
	} else if(signum == SIGBUS)
	    switch(ip->si_code) {
#ifdef BUS_ADRALN
	    case BUS_ADRALN:
		s = "invalid alignment";
		break;
#endif
#ifdef BUS_ADRERR /* not on MacOS X, apparently */
	    case BUS_ADRERR:
		s = "non-existent physical address";
		break;
#endif
#ifdef BUS_OBJERR /* not on MacOS X, apparently */
	    case BUS_OBJERR:
		s = "object specific hardware error";
		break;
#endif
	    default:
		s = "unknown";
		break;
	    }
	else
	    switch(ip->si_code) {
#ifdef SEGV_MAPERR
	    case SEGV_MAPERR:
		s = "memory not mapped";
		break;
#endif
#ifdef SEGV_ACCERR
	    case SEGV_ACCERR:
		s = "invalid permissions";
		break;
#endif
	    default:
		s = "unknown";
		break;
	    }
	REprintf("address %p, cause '%s'\n", ip->si_addr, s);
    }

    if (getenv("R_ABORT") == 0 || *getenv("R_ABORT") == 0)
    {   
        /* A simple customized print of the traceback */
	SEXP trace, p, q;
	int line = 1, i;
	PROTECT(trace = R_GetTraceback(0));
	if(trace != R_NilValue) {
	    REprintf("\nTraceback:\n");
	    for(p = trace; p != R_NilValue; p = CDR(p), line++) {
		q = CAR(p); /* a character vector */
		REprintf("%2d: ", line);
		for(i = 0; i < LENGTH(q); i++)
		    REprintf("%s", CHAR(STRING_ELT(q, i)));
		REprintf("\n");
	    }
	    UNPROTECT(1);
	}

        if (R_Interactive) {
            REprintf("\nPossible actions:\n1: %s\n2: %s\n3: %s\n4: %s\n",
                     "abort (with core dump, if enabled)",
                     "normal R exit",
                     "exit R without saving workspace",
                     "exit R saving workspace");
            while(1) {
                if(R_ReadConsole("Selection: ", ConsoleBuf, CONSOLE_BUFFER_SIZE,
                                 0) > 0) {
                    if(ConsoleBuf[0] == '1') break;
                    if(ConsoleBuf[0] == '2') R_CleanUp(SA_DEFAULT, 0, 1);
                    if(ConsoleBuf[0] == '3') R_CleanUp(SA_NOSAVE, 70, 0);
                    if(ConsoleBuf[0] == '4') R_CleanUp(SA_SAVE, 71, 0);
                }
            }
        }
    }

    REprintf("aborting ...\n");
    R_CleanTempDir();
    /* now do normal behaviour, e.g. core dump */
    signal(signum, SIG_DFL);
    raise(signum);
}

#ifndef SIGSTKSZ
# define SIGSTKSZ 8192    /* just a guess */
#endif

#ifdef HAVE_STACK_T
static stack_t sigstk;
#else
static struct sigaltstack sigstk;
#endif
static void *signal_stack;

#define R_USAGE 100000 /* Just a guess */
static void init_signal_handlers(void)
{
    /* <FIXME> may need to reinstall this if we do recover. */
    struct sigaction sa;
    signal_stack = malloc(SIGSTKSZ + R_USAGE);
    if (signal_stack != NULL) {
	sigstk.ss_sp = signal_stack;
	sigstk.ss_size = SIGSTKSZ + R_USAGE;
	sigstk.ss_flags = 0;
	if(sigaltstack(&sigstk, NULL) < 0)
	    warning("failed to set alternate signal stack");
    } else
	warning("failed to allocate alternate signal stack");
    sa.sa_sigaction = sigactionSegv;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS, &sa, NULL);
#endif

    signal(SIGINT,  handleInterrupt);
    signal(SIGUSR1, onsigusr1);
    signal(SIGUSR2, onsigusr2);
    signal(SIGPIPE, handlePipe);
}

#else /* not sigaltstack and sigaction and sigemptyset*/
static void init_signal_handlers(void)
{
    signal(SIGINT,  handleInterrupt);
    signal(SIGUSR1, onsigusr1);
    signal(SIGUSR2, onsigusr2);
#ifndef Win32
    signal(SIGPIPE, handlePipe);
#else
    signal(SIGSEGV, win32_segv);
    signal(SIGILL, win32_segv);
#endif
}
#endif


static void R_LoadProfile(FILE *fparg, SEXP env)
{
    FILE * volatile fp = fparg; /* is this needed? */
    if (fp != NULL) {
	if (! SETJMP(R_Toplevel.cjmpbuf)) {
	    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
	    R_ReplFile(fp, env);
	}
	fclose(fp);
    }
}


/* ------------------------------------------------------------------------
   Routines that set up for the Read-Eval-Print Loop, calling R_ReplConsole
   to actually do it.
*/


int R_SignalHandlers = 1;  /* Exposed in R_interface.h */

unsigned int TimeToSeed(void); /* datetime.c */

const char* get_workspace_name();  /* from startup.c */


/* Setup for main loop.  It is assumed that at this point that
   operating system specific tasks (dialog window creation etc) have
   been performed.  We can now print a greeting, run the .First
   function, in preparation for entering the read-eval-print loop. */

void setup_Rmainloop(void)
{
    volatile int doneit;
    volatile SEXP baseEnv;
    SEXP cmd;
    FILE *fp;
#ifdef ENABLE_NLS
    char localedir[PATH_MAX+20];
#endif
    char deferred_warnings[11][250]; /* INCREASE AS NECESSARY! */
    volatile int ndeferred_warnings = 0;

    InitHighFrequencyGlobals();

    InitConnections(); /* needed to get any output at all */

    /* Initialize the interpreter's internal structures. */

#ifdef HAVE_LOCALE_H
#ifdef Win32
    {
	char *p, Rlocale[1001]; /* Windows' locales can be very long */
	p = getenv("LC_ALL");
	strncpy(Rlocale, p ? p : "", 1000); Rlocale[1000] = 0;
	if(!(p = getenv("LC_CTYPE"))) p = Rlocale;
	/* We'd like to use warning, but need to defer.
	   Also cannot translate. */
	if(!setlocale(LC_CTYPE, p))
	    snprintf(deferred_warnings[ndeferred_warnings++], 250,
		     "Setting LC_CTYPE=%s failed\n", p);
	if((p = getenv("LC_COLLATE"))) {
	    if(!setlocale(LC_COLLATE, p))
		snprintf(deferred_warnings[ndeferred_warnings++], 250,
			 "Setting LC_COLLATE=%s failed\n", p);
	} else setlocale(LC_COLLATE, Rlocale);
	if((p = getenv("LC_TIME"))) {
	    if(!setlocale(LC_TIME, p))
		snprintf(deferred_warnings[ndeferred_warnings++], 250,
			 "Setting LC_TIME=%s failed\n", p);
	} else setlocale(LC_TIME, Rlocale);
	if((p = getenv("LC_MONETARY"))) {
	    if(!setlocale(LC_MONETARY, p))
		snprintf(deferred_warnings[ndeferred_warnings++], 250,
			 "Setting LC_MONETARY=%s failed\n", p);
	} else setlocale(LC_MONETARY, Rlocale);
	/* Windows does not have LC_MESSAGES */

	/* We set R_ARCH here: Unix does it in the shell front-end */
	char Rarch[30];
	strcpy(Rarch, "R_ARCH=/");
	strcat(Rarch, R_ARCH);
	putenv(Rarch);
    }
#else /* not Win32 */
    if(!setlocale(LC_CTYPE, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_CTYPE failed, using \"C\"\n");
    if(!setlocale(LC_COLLATE, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_COLLATE failed, using \"C\"\n");
    if(!setlocale(LC_TIME, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_TIME failed, using \"C\"\n");
#ifdef ENABLE_NLS
    if(!setlocale(LC_MESSAGES, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_MESSAGES failed, using \"C\"\n");
#endif
    /* NB: we do not set LC_NUMERIC */
#ifdef LC_MONETARY
    if(!setlocale(LC_MONETARY, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_MONETARY failed, using \"C\"\n");
#endif
#ifdef LC_PAPER
    if(!setlocale(LC_PAPER, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_PAPER failed, using \"C\"\n");
#endif
#ifdef LC_MEASUREMENT
    if(!setlocale(LC_MEASUREMENT, ""))
	snprintf(deferred_warnings[ndeferred_warnings++], 250,
		 "Setting LC_MEASUREMENT failed, using \"C\"\n");
#endif
#endif /* not Win32 */
#ifdef ENABLE_NLS
    /* This ought to have been done earlier, but be sure */
    textdomain(PACKAGE);
    {
	char *p = getenv("R_SHARE_DIR");
	if(p) {
	    strcpy(localedir, p);
	    strcat(localedir, "/locale");
	} else {
	    strcpy(localedir, R_Home);
	    strcat(localedir, "/share/locale");
	}
    }
    bindtextdomain(PACKAGE, localedir);
    strcpy(localedir, R_Home); strcat(localedir, "/library/base/po");
    bindtextdomain("R-base", localedir);
#endif
#endif

    /* make sure srand is called before R_tmpnam, PR#14381 */
    srand(TimeToSeed());

    InitTempDir(); /* must be before InitEd */
    InitMemory();
    InitStringHash(); /* must be before InitNames */
    InitNames();
    InitBaseEnv();
    InitGlobalEnv();
    InitDynload();
    InitOptions();
    InitEd();
    InitArithmetic();
    InitColors();
    InitGraphics();
    InitTypeTables(); /* must be before InitS3DefaultTypes */
    InitS3DefaultTypes();
    PrintDefaults();

    R_Is_Running = 1;
    R_check_locale();

    /* Initialize the global context for error handling. */
    /* This provides a target for any non-local gotos */
    /* which occur during error handling */

    R_Toplevel.nextcontext = NULL;
    R_Toplevel.callflag = CTXT_TOPLEVEL;
    R_Toplevel.cstacktop = 0;
    R_Toplevel.promargs = R_NilValue;
    R_Toplevel.callfun = R_NilValue;
    R_Toplevel.call = R_NilValue;
    R_Toplevel.cloenv = R_BaseEnv;
    R_Toplevel.sysparent = R_BaseEnv;
    R_Toplevel.conexit = R_NilValue;
    R_Toplevel.vmax = (void *) R_NilValue;
    R_Toplevel.nodestack = R_BCNodeStackTop;
#ifdef BC_INT_STACK
    R_Toplevel.intstack = R_BCIntStackTop;
#endif
    R_Toplevel.cend = NULL;
    R_Toplevel.intsusp = FALSE;
    R_Toplevel.handlerstack = R_HandlerStack;
    R_Toplevel.restartstack = R_RestartStack;
    R_Toplevel.srcref = R_NilValue;
    R_Toplevel.local_pr = R_local_protect_start;
    R_Toplevel.scalar_stack = R_scalar_stack;
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;

    R_Warnings = R_NilValue;

    /* This is the same as R_BaseEnv, but this marks the environment
       of functions as the namespace and not the package. */
    baseEnv = R_BaseNamespace;

    /* Set up some global variables */
    Init_R_Variables(baseEnv);

    /* On initial entry we open the base language package and begin by
       running the repl on it.
       If there is an error we pass on to the repl.
       Perhaps it makes more sense to quit gracefully?
    */

    fp = R_OpenLibraryFile("base");
    if (fp == NULL)
	R_Suicide(_("unable to open the base package\n"));

    doneit = 0;
    SETJMP(R_Toplevel.cjmpbuf);
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    if (R_SignalHandlers) init_signal_handlers();
    if (!doneit) {
	doneit = 1;
	R_ReplFile(fp, baseEnv);
    }
    fclose(fp);

    /* This is where we source the system-wide, the site's and the
       user's profile (in that order).  If there is an error, we
       drop through to further processing.
    */

    R_LoadProfile(R_OpenSysInitFile(), baseEnv);
    /* These are the same bindings, so only lock them once */
    R_LockEnvironment(R_BaseNamespace, TRUE);
#ifdef NOTYET
    /* methods package needs to trample here */
    R_LockEnvironment(R_BaseEnv, TRUE);
#endif
    /* At least temporarily unlock some bindings used in graphics */
    R_unLockBinding(R_DeviceSymbol, R_BaseEnv);
    R_unLockBinding(install(".Devices"), R_BaseEnv);
    R_unLockBinding(install(".Library.site"), R_BaseEnv);

    /* require(methods) if it is in the default packages */
    doneit = 0;
    SETJMP(R_Toplevel.cjmpbuf);
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    if (!doneit) {
	doneit = 1;
	PROTECT(cmd = install(".OptRequireMethods"));
	R_CurrentExpr = findVar(cmd, R_GlobalEnv);
	if (R_CurrentExpr != R_UnboundValue &&
	    TYPEOF(R_CurrentExpr) == CLOSXP) {
		PROTECT(R_CurrentExpr = lang1(cmd));
		R_CurrentExpr = eval(R_CurrentExpr, R_GlobalEnv);
		UNPROTECT(1);
	}
	UNPROTECT(1);
    }

    if (strcmp(R_GUIType, "Tk") == 0) {
	char buf[PATH_MAX];

	snprintf(buf, PATH_MAX, "%s/library/tcltk/exec/Tk-frontend.R", R_Home);
	R_LoadProfile(R_fopen(buf, "r"), R_GlobalEnv);
    }

    /* Print a platform and version dependent greeting and a pointer to
     * the copyleft.
     */
    if(!R_Quiet) PrintGreeting();
 
    R_LoadProfile(R_OpenSiteFile(), baseEnv);
    R_LockBinding(install(".Library.site"), R_BaseEnv);
    R_LoadProfile(R_OpenInitFile(), R_GlobalEnv);

    /* This is where we try to load a user's saved data.
       The right thing to do here is very platform dependent.
       E.g. under Unix we look in a special hidden file and on the Mac
       we look in any documents which might have been double clicked on
       or dropped on the application.
    */
    doneit = 0;
    SETJMP(R_Toplevel.cjmpbuf);
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    if (!doneit) {
	doneit = 1;
	R_InitialData();
    }
    else {
    	if (! SETJMP(R_Toplevel.cjmpbuf)) {
	    warning(_("unable to restore saved data in %s\n"), 
                    get_workspace_name());
	}
    }
    
    /* Initial Loading is done.
       At this point we try to invoke the .First Function.
       If there is an error we continue. */

    doneit = 0;
    SETJMP(R_Toplevel.cjmpbuf);
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    if (!doneit) {
	doneit = 1;
	PROTECT(cmd = install(".First"));
	R_CurrentExpr = findVar(cmd, R_GlobalEnv);
	if (R_CurrentExpr != R_UnboundValue &&
	    TYPEOF(R_CurrentExpr) == CLOSXP) {
		PROTECT(R_CurrentExpr = lang1(cmd));
		R_CurrentExpr = eval(R_CurrentExpr, R_GlobalEnv);
		UNPROTECT(1);
	}
	UNPROTECT(1);
    }
    /* Try to invoke the .First.sys function, which loads the default packages.
       If there is an error we continue. */

    doneit = 0;
    SETJMP(R_Toplevel.cjmpbuf);
    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    if (!doneit) {
	doneit = 1;
	PROTECT(cmd = install(".First.sys"));
	R_CurrentExpr = findVar(cmd, baseEnv);
	if (R_CurrentExpr != R_UnboundValue &&
	    TYPEOF(R_CurrentExpr) == CLOSXP) {
		PROTECT(R_CurrentExpr = lang1(cmd));
		R_CurrentExpr = eval(R_CurrentExpr, R_GlobalEnv);
		UNPROTECT(1);
	}
	UNPROTECT(1);
    }
    {
	int i;
	for(i = 0 ; i < ndeferred_warnings; i++)
	    warning(deferred_warnings[i]);
    }
    if (R_CollectWarnings) {
	REprintf(_("During startup - "));
	PrintWarnings();
    }

    /* trying to do this earlier seems to run into bootstrapping issues. */
    R_init_jit_enabled();
}


void end_Rmainloop(void)
{
    /* refrain from printing trailing '\n' in slave mode */
    if (!R_Slave)
	Rprintf("\n");
    /* run the .Last function. If it gives an error, will drop back to main
       loop. */
    R_CleanUp(SA_DEFAULT, 0, 1);
}


void helpers_master (void)
{
    static int printed_already = 0;

    if (!printed_already && !R_Quiet && !SETJMP(R_Toplevel.cjmpbuf)) {

        Rprintf("\n");
        if (helpers_are_disabled)
            Rprintf (_("Deferred evaluation disabled (no helper threads, no task merging)"));
        else if (helpers_not_multithreading_now && helpers_not_merging)
            Rprintf (_("No helper threads, no task merging"));
        else if (helpers_not_multithreading_now)
            Rprintf (_("No helper threads, task merging enabled"));
        else if (helpers_not_merging)
            Rprintf (_("%d helper threads, no task merging"), helpers_num);
        else
            Rprintf (_("%d helper threads, task merging enabled"), helpers_num);
#       if USE_COMPRESSED_POINTERS
            Rprintf (_(", compressed pointers.\n\n"));
#       else
            Rprintf (_(", uncompressed pointers.\n\n"));
#       endif

        printed_already = 1;
    }

    R_IoBufferInit(&R_ConsoleIob);

    SETJMP(R_Toplevel.cjmpbuf);

    /* IF AN ERROR CAUSES A LONGJMP TO THE TOP LEVEL, WE WILL RESTART HERE */

    R_GlobalContext = R_ToplevelContext = &R_Toplevel;

    R_ReplConsole(R_GlobalEnv, 0, 0);   /* --- Actually do the REPL --- */

    end_Rmainloop();
}


/* The main Read-Eval-Print Loop.  Should be called after setup_Rmainloop.
   Creates helper threads (if enabled) for the duration of the REPL. */

void run_Rmainloop(void)
{
    int n_helpers;  /* Requested number of helpers to use, negative
                       if deferred evaluation is to be disabled */

    n_helpers = getenv("R_HELPERS") ? atoi(getenv("R_HELPERS")) : 0;

    if (n_helpers < 0)
        helpers_disable(1);

    if (n_helpers > HELPERS_MAX)
        n_helpers = HELPERS_MAX;

    helpers_trace (getenv("R_HELPERS_TRACE") ? 1 : 0);

    /* The call of helpers_startup below will call helpers_master above. */

    helpers_startup (n_helpers > 0 ? n_helpers : 0);
}


/* Procedure that does both the setup and the actual loop. */

void mainloop(void)
{
    setup_Rmainloop();
    run_Rmainloop();
}


/* -------------------------------------------------------------------------- */


/*this functionality now appears in 3 places-jump_to_toplevel/profile/here */

static void printwhere(void)
{
  RCNTXT *cptr;
  int lct = 1;

  for (cptr = R_GlobalContext; cptr; cptr = cptr->nextcontext) {
    if ((cptr->callflag & (CTXT_FUNCTION | CTXT_BUILTIN)) &&
	(TYPEOF(cptr->call) == LANGSXP)) {
	Rprintf("where %d", lct++);
	SrcrefPrompt("", cptr->srcref);
	PrintValue(cptr->call);
    }
  }
  Rprintf("\n");
}

static int ParseBrowser(SEXP CExpr, SEXP rho)
{
    int rval = 0;
    if (isSymbol(CExpr)) {
	const char *expr = CHAR(PRINTNAME(CExpr));
	if (!strcmp(expr, "n")) {
	    SET_RDEBUG(rho, 1);
	    rval = 1;
	}
	if (!strcmp(expr, "c")) {
	    rval = 1;
	    SET_RDEBUG(rho, 0);
	}
	if (!strcmp(expr, "cont")) {
	    rval = 1;
	    SET_RDEBUG(rho, 0);
	}
	if (!strcmp(expr, "Q")) {

	    /* Run onexit/cend code for everything above the target.
	       The browser context is still on the stack, so any error
	       will drop us back to the current browser.  Not clear
	       this is a good thing.  Also not clear this should still
	       be here now that jump_to_toplevel is used for the
	       jump. */
	    R_run_onexits(R_ToplevelContext);

	    /* this is really dynamic state that should be managed as such */
	    SET_RDEBUG(rho, 0); /*PR#1721*/

	    jump_to_toplevel();
	}
	if (!strcmp(expr, "where")) {
	    printwhere();
	    /* SET_RDEBUG(rho, 1); */
	    rval = 2;
	}
    }
    return rval;
}


/* browser(text = "", condition = NULL, expr = TRUE, skipCalls = 0L) */
/* also called directly, not just as a built-in function. */
SEXP attribute_hidden do_browser(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    RCNTXT *saveToplevelContext;
    RCNTXT *saveGlobalContext;
    RCNTXT thiscontext, returncontext, *cptr;
    int savestack, browselevel, tmp;
    SEXP topExp, argList;
    static const char * const ap[4] = 
                         { "text", "condition", "expr", "skipCalls" };

    R_Visible = FALSE;

    /* argument matching */
    argList = matchArgs_strings (ap, 4, args, call);
    PROTECT(argList);
    /* substitute defaults */
    if(CAR(argList) == R_MissingArg)
	SETCAR(argList, mkString(""));
    if(CADR(argList) == R_MissingArg)
	SETCAR(CDR(argList), R_NilValue);
    if(CADDR(argList) == R_MissingArg) 
	SETCAR(CDDR(argList), ScalarLogical(1));
    if(CADDDR(argList) == R_MissingArg) 
	SETCAR(CDR(CDDR(argList)), ScalarInteger(0));

    /* return if 'expr' is not TRUE */
    if( !asLogical(CADDR(argList)) ) {
        UNPROTECT(1);
        return R_NilValue;
    }

    /* Save the evaluator state information */
    /* so that it can be restored on exit. */

    browselevel = countContexts(CTXT_BROWSER, 1);
    savestack = R_PPStackTop;
    PROTECT(topExp = R_CurrentExpr);
    saveToplevelContext = R_ToplevelContext;
    saveGlobalContext = R_GlobalContext;

    if (!RDEBUG(rho)) {
        int skipCalls = asInteger(CADDDR(argList));
	cptr = R_GlobalContext;
	while ( ( !(cptr->callflag & CTXT_FUNCTION) || skipCalls--) 
		&& cptr->callflag )
	    cptr = cptr->nextcontext;
	Rprintf("Called from: ");
	tmp = asInteger(GetOption(install("deparse.max.lines"), R_BaseEnv));
	if(tmp != NA_INTEGER && tmp > 0) R_BrowseLines = tmp;
        if( cptr != R_ToplevelContext )
	    PrintValueRec(cptr->call, rho);
        else
            Rprintf("top level \n");

	R_BrowseLines = 0;
    }

    R_ReturnedValue = R_NilValue;

    /* Here we establish two contexts.  The first */
    /* of these provides a target for return */
    /* statements which a user might type at the */
    /* browser prompt.  The (optional) second one */
    /* acts as a target for error returns. */

    begincontext(&returncontext, CTXT_BROWSER, call, rho,
		 R_BaseEnv, argList, R_NilValue);
    if (!SETJMP(returncontext.cjmpbuf)) {
	begincontext(&thiscontext, CTXT_RESTART, R_NilValue, rho,
		     R_BaseEnv, R_NilValue, R_NilValue);
	if (SETJMP(thiscontext.cjmpbuf)) {
	    SET_RESTART_BIT_ON(thiscontext.callflag);
	    R_ReturnedValue = R_NilValue;
	    R_Visible = FALSE;
	}
	R_GlobalContext = &thiscontext;
	R_InsertRestartHandlers(&thiscontext, TRUE);
	R_ReplConsole(rho, savestack, browselevel+1);
	endcontext(&thiscontext);
    }
    endcontext(&returncontext);

    WAIT_UNTIL_COMPUTED(R_ReturnedValue);

    /* Reset the interpreter state. */

    R_CurrentExpr = topExp;
    UNPROTECT(1);
    R_PPStackTop = savestack;
    UNPROTECT(1);
    R_CurrentExpr = topExp;
    R_ToplevelContext = saveToplevelContext;
    R_GlobalContext = saveGlobalContext;

    R_Visible = FALSE;
    return R_ReturnedValue;
}

void R_dot_Last(void)
{
    SEXP cmd;

    /* Run the .Last function. */
    /* Errors here should kick us back into the repl. */

    R_GlobalContext = R_ToplevelContext = &R_Toplevel;
    PROTECT(cmd = install(".Last"));
    R_CurrentExpr = findVar(cmd, R_GlobalEnv);
    if (R_CurrentExpr != R_UnboundValue && TYPEOF(R_CurrentExpr) == CLOSXP) {
	PROTECT(R_CurrentExpr = lang1(cmd));
	R_CurrentExpr = eval(R_CurrentExpr, R_GlobalEnv);
	UNPROTECT(1);
    }
    UNPROTECT(1);
    PROTECT(cmd = install(".Last.sys"));
    R_CurrentExpr = findVar(cmd, R_BaseNamespace);
    if (R_CurrentExpr != R_UnboundValue && TYPEOF(R_CurrentExpr) == CLOSXP) {
	PROTECT(R_CurrentExpr = lang1(cmd));
	R_CurrentExpr = eval(R_CurrentExpr, R_GlobalEnv);
	UNPROTECT(1);
    }
    UNPROTECT(1);
}

static SEXP do_quit(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    const char *tmp;
    SA_TYPE ask=SA_DEFAULT;
    int status, runLast;

    /* if there are any browser contexts active don't quit */
    if(countContexts(CTXT_BROWSER, 1)) {
	warning(_("cannot quit from browser"));
	return R_NilValue;
    }
    if( !isString(CAR(args)) )
	errorcall(call, _("one of \"yes\", \"no\", \"ask\" or \"default\" expected."));
    tmp = CHAR(STRING_ELT(CAR(args), 0)); /* ASCII */
    if( !strcmp(tmp, "ask") ) {
	ask = SA_SAVEASK;
	if(!R_Interactive)
	    warning(_("save=\"ask\" in non-interactive use: command-line default will be used"));
    } else if( !strcmp(tmp, "no") )
	ask = SA_NOSAVE;
    else if( !strcmp(tmp, "yes") )
	ask = SA_SAVE;
    else if( !strcmp(tmp, "default") )
	ask = SA_DEFAULT;
    else
	errorcall(call, _("unrecognized value of 'save'"));
    status = asInteger(CADR(args));
    if (status == NA_INTEGER) {
	warning(_("invalid 'status', 0 assumed"));
	runLast = 0;
    }
    runLast = asLogical(CADDR(args));
    if (runLast == NA_LOGICAL) {
	warning(_("invalid 'runLast', FALSE assumed"));
	runLast = 0;
    }
    /* run the .Last function. If it gives an error, will drop back to main
       loop. */
    R_CleanUp(ask, status, runLast);
    exit(0);
}


#include <R_ext/Callbacks.h>

static R_ToplevelCallbackEl *Rf_ToplevelTaskHandlers = NULL;

/**
  This is the C-level entry point for registering a handler
  that is to be called when each top-level task completes.

  Perhaps we need names to make removing them handlers easier
  since they could be more identified by an invariant (rather than
  position).
 */
R_ToplevelCallbackEl *
Rf_addTaskCallback(R_ToplevelCallback cb, void *data,
		   void (*finalizer)(void *), const char *name, int *pos)
{
    int which;
    R_ToplevelCallbackEl *el;
    el = (R_ToplevelCallbackEl *) malloc(sizeof(R_ToplevelCallbackEl));
    if(!el)
	error(_("cannot allocate space for toplevel callback element"));

    el->data = data;
    el->cb = cb;
    el->next = NULL;
    el->finalizer = finalizer;

    if(Rf_ToplevelTaskHandlers == NULL) {
	Rf_ToplevelTaskHandlers = el;
	which = 0;
    } else {
	R_ToplevelCallbackEl *tmp;
	tmp = Rf_ToplevelTaskHandlers;
	which = 1;
	while(tmp->next) {
	    which++;
	    tmp = tmp->next;
	}
	tmp->next = el;
    }

    if(!name) {
	char buf[12];
	sprintf(buf, "%d", which+1);
	el->name = strdup(buf);
    } else
	el->name = strdup(name);

    if(pos)
	*pos = which;

    return(el);
}

Rboolean
Rf_removeTaskCallbackByName(const char *name)
{
    R_ToplevelCallbackEl *el = Rf_ToplevelTaskHandlers, *prev = NULL;
    Rboolean status = TRUE;

    if(!Rf_ToplevelTaskHandlers) {
	return(FALSE); /* error("there are no task callbacks registered"); */
    }

    while(el) {
	if(strcmp(el->name, name) == 0) {
	    if(prev == NULL) {
		Rf_ToplevelTaskHandlers = el->next;
	    } else {
		prev->next = el->next;
	    }
	    break;
	}
	prev = el;
	el = el->next;
    }
    if(el) {
	if(el->finalizer)
	    el->finalizer(el->data);
	free(el->name);
	free(el);
    } else {
	status = FALSE;
    }
    return(status);
}

/**
  Remove the top-level task handler/callback identified by
  its position in the list of callbacks.
 */
Rboolean
Rf_removeTaskCallbackByIndex(int id)
{
    R_ToplevelCallbackEl *el = Rf_ToplevelTaskHandlers, *tmp = NULL;
    Rboolean status = TRUE;

    if(id < 0)
	error(_("negative index passed to R_removeTaskCallbackByIndex"));

    if(Rf_ToplevelTaskHandlers) {
	if(id == 0) {
	    tmp = Rf_ToplevelTaskHandlers;
	    Rf_ToplevelTaskHandlers = Rf_ToplevelTaskHandlers->next;
	} else {
	    int i = 0;
	    while(el && i < (id-1)) {
		el = el->next;
		i++;
	    }

	    if(i == (id -1) && el) {
		tmp = el->next;
		el->next = (tmp ? tmp->next : NULL);
	    }
	}
    }
    if(tmp) {
	if(tmp->finalizer)
	    tmp->finalizer(tmp->data);
	free(tmp->name);
	free(tmp);
    } else {
	status = FALSE;
    }

    return(status);
}


/**
  R-level entry point to remove an entry from the
  list of top-level callbacks. 'which' should be an
  integer and give us the 0-based index of the element
  to be removed from the list.

  @see Rf_RemoveToplevelCallbackByIndex(int)
 */
SEXP
R_removeTaskCallback(SEXP which)
{
    int id;
    Rboolean val;

    if(TYPEOF(which) == STRSXP) {
        val = Rf_removeTaskCallbackByName(CHAR(STRING_ELT(which, 0)));
    } else {
        id = asInteger(which);
        if (id != NA_INTEGER) val = Rf_removeTaskCallbackByIndex(id-1);
        else val = FALSE;
    }
    return ScalarLogical(val);
}

SEXP
R_getTaskCallbackNames(void)
{
    SEXP ans;
    R_ToplevelCallbackEl *el;
    int n = 0;

    el = Rf_ToplevelTaskHandlers;
    while(el) {
	n++;
	el = el->next;
    }
    PROTECT(ans = allocVector(STRSXP, n));
    n = 0;
    el = Rf_ToplevelTaskHandlers;
    while(el) {
	SET_STRING_ELT(ans, n, mkChar(el->name));
	n++;
	el = el->next;
    }
    UNPROTECT(1);
    return(ans);
}

/**
  Invokes each of the different handlers giving the
  top-level expression that was just evaluated,
  the resulting value from the evaluation, and
  whether the task succeeded. The last may be useful
  if a handler is also called as part of the error handling.
  We also have information about whether the result was printed or not.
  We currently do not pass this to the handler.
 */

  /* Flag to ensure that the top-level handlers aren't called recursively.
     Simple state to indicate that they are currently being run. */
static Rboolean Rf_RunningToplevelHandlers = FALSE;

void
Rf_callToplevelHandlers(SEXP expr, SEXP value, Rboolean succeeded,
			Rboolean visible)
{
    R_ToplevelCallbackEl *h, *prev = NULL;
    Rboolean again;

    if(Rf_RunningToplevelHandlers == TRUE)
	return;

    h = Rf_ToplevelTaskHandlers;
    if (h) WAIT_UNTIL_COMPUTED(value);
    Rf_RunningToplevelHandlers = TRUE;
    while(h) {
	again = (h->cb)(expr, value, succeeded, visible, h->data);
	if(R_CollectWarnings) {
	    REprintf(_("warning messages from top-level task callback '%s'\n"),
		     h->name);
	    PrintWarnings();
	}
	if(again) {
	    prev = h;
	    h = h->next;
	} else {
	    R_ToplevelCallbackEl *tmp;
	    tmp = h;
	    if(prev)
		prev->next = h->next;
	    h = h->next;
	    if(tmp == Rf_ToplevelTaskHandlers)
		Rf_ToplevelTaskHandlers = h;
	    if(tmp->finalizer)
		tmp->finalizer(tmp->data);
	    free(tmp);
	}
    }

    Rf_RunningToplevelHandlers = FALSE;
}


Rboolean
R_taskCallbackRoutine(SEXP expr, SEXP value, Rboolean succeeded,
		      Rboolean visible, void *userData)
{
    SEXP f = (SEXP) (uintptr_t) userData;
    SEXP e, tmp, val, cur;
    int errorOccurred;
    Rboolean again, useData = LOGICAL(VECTOR_ELT(f, 2))[0];

    PROTECT(e = allocVector(LANGSXP, 5 + useData));
    SETCAR(e, VECTOR_ELT(f, 0));
    cur = CDR(e);
    SETCAR(cur, tmp = allocVector(LANGSXP, 2));
	SETCAR(tmp, R_QuoteSymbol);
	SETCAR(CDR(tmp), expr);
    cur = CDR(cur);
    SETCAR(cur, value);
    cur = CDR(cur);
    SETCAR(cur, ScalarLogical(succeeded));
    cur = CDR(cur);
    SETCAR(cur, tmp = ScalarLogical(visible));
    if(useData) {
	cur = CDR(cur);
	SETCAR(cur, VECTOR_ELT(f, 1));
    }

    val = R_tryEval(e, R_NoObject, &errorOccurred);
    if(!errorOccurred) {
	PROTECT(val);
	if(TYPEOF(val) != LGLSXP) {
	      /* It would be nice to identify the function. */
	    warning(_("top-level task callback did not return a logical value"));
	}
	again = asLogical(val);
	UNPROTECT(1);
    } else {
	/* warning("error occurred in top-level task callback\n"); */
	again = FALSE;
    }
    return(again);
}

SEXP
R_addTaskCallback(SEXP f, SEXP data, SEXP useData, SEXP name)
{
    SEXP internalData;
    SEXP index;
    R_ToplevelCallbackEl *el;
    const char *tmpName = NULL;

    internalData = allocVector(VECSXP, 3);
    R_PreserveObject(internalData);
    SET_VECTOR_ELT(internalData, 0, f);
    SET_VECTOR_ELT(internalData, 1, data);
    SET_VECTOR_ELT(internalData, 2, useData);

    if(length(name))
	tmpName = CHAR(STRING_ELT(name, 0));

    PROTECT(index = allocVector1INT());
    el = Rf_addTaskCallback(R_taskCallbackRoutine,  
                            (void *) (uintptr_t) internalData,
			    (void (*)(void*)) R_ReleaseObject, tmpName,
			    (void *) INTEGER(index));

    if(length(name) == 0) {
	PROTECT(name = mkString(el->name));
	setAttrib(index, R_NamesSymbol, name);
	UNPROTECT(1);
    } else {
	setAttrib(index, R_NamesSymbol, name);
    }

    UNPROTECT(1);
    return(index);
}

#undef __MAIN__

#ifndef Win32
/* this is here solely to pull in xxxpr.o */
#include <R_ext/RS.h>
void F77_SYMBOL(intpr) (char *, int *, int *, int *);
void attribute_hidden dummy12345(void)
{
    int i = 0;
    F77_CALL(intpr)("dummy", &i, &i, &i);
}
#endif

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_main[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"browser",	do_browser,	0,	101,	4,	{PP_FUNCALL, PREC_FN,	  0}},
{"quit",	do_quit,	0,	111,	3,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};
