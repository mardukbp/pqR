/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1997--2011  The R Core Team
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
 *  http://www.r-project.org/Licenses/ */


/* IMPLEMENTATION NOTES:

   Deparsing has 3 layers.  The user interface, do_deparse, should not
   be called from an internal function, the actual deparsing needs to
   be done twice, once to count things up and a second time to put
   them into the string vector for return.  Printing this to a file is
   handled by the calling routine.

   Indentation is carried out in the routine printtab2buff at the
   botton of this file.  It seems like this should be settable via
   options.

   'lbreak' is often used to indicate whether a line has been broken,
    this makes sure that that indenting behaves itself.

   The previous issue with the global "cutoff" variable is now
   implemented by creating a deparse1WithCutoff() routine which takes
   the cutoff from the caller and passes this to the different routines
   as a member of the LocalParseData struct. Access to the deparse1()
   routine remains unaltered.  This is exactly as Ross had suggested...
 
   One possible fix is to restructure the code with another function
   which takes a cutoff value as a parameter.  Then "do_deparse" and
   "deparse1" could each call this deeper function with the appropriate
   argument.  I wonder why I didn't just do this? -- it would have been
   quicker than writing this note.  I guess it needs a bit more thought...
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define USE_FAST_PROTECT_MACROS
#define R_USE_SIGNALS 1
#include <Defn.h>
#include <float.h> /* for DBL_DIG */
#include <Print.h>
#include <Fileio.h>
#include <Parse.h>

#define BUFSIZE 512

#define MIN_Cutoff 20
#define DEFAULT_Cutoff 60
#define MAX_Cutoff (BUFSIZE - 12)
/* ----- MAX_Cutoff  <	BUFSIZE !! */

#include "RBufferUtils.h"

typedef R_StringBuffer DeparseBuffer;

/* The code here used to use static variables to share values across
   the different routines. These have now been collected into a struct
   named LocalParseData, which is explicitly passed between the
   different routines. This avoids the needs for the global variables
   and allows multiple evaluators, potentially in different threads,
   to work on their own independent copies that are local to their
   call stacks. This avoids any issues with interrupts, etc. not
   restoring values. */

typedef struct {

    int linenumber;      /* counts the number of lines that have been written,
                            this is used to setup storage for deparsing */
    int len;             /* counts the length of the current line, it will be
                            used to determine when to break lines */
    int incurly;         /* keeps track of whether we are inside a curly or not,
                            this affects the printing of if-then-else */
    int inlist;          /* keeps track of whether we are inside a list or not,
                            this affects the printing of if-then-else */
    Rboolean startline;  /* TRUE indicates start of a line (so we can tab out to
                            the correct place) */
    int indent;          /* how many tabs should be written at the start of 
                            a line */

    SEXP strvec;

    DeparseBuffer buffer;  /* contains the current string, we attempt to break
                              lines at cutoff, but can unlimited length */
    int cutoff;
    int backtick;
    int opts;
    int sourceable;
    int longstring;
    int maxlines;
    Rboolean active;
    int isS4;
} LocalParseData;

static SEXP deparse1WithCutoff(SEXP call, Rboolean abbrev, int cutoff,
			       Rboolean backtick, int opts, int nlines);
static void args2buff(SEXP, int, int, LocalParseData *);
static void deparse2buff(SEXP, LocalParseData *);
static void print2buff(const char *, LocalParseData *);
static void printtab2buff(int, LocalParseData *);
static void writeline(LocalParseData *);
static void vector2buff(SEXP, LocalParseData *);
static void src2buff1(SEXP, LocalParseData *);
static Rboolean src2buff(SEXP, int, LocalParseData *);
static void vec2buff(SEXP, LocalParseData *);
static void linebreak(Rboolean *lbreak, LocalParseData *);
static void deparse2(SEXP, SEXP, LocalParseData *);

static int has_n_tags (SEXP s, int n)
{
    while (n > 0) {
        if (TAG(s) == R_NilValue)  /* note that TAG(R_NilValue) == R_NilValue */
            return 0;
        s = CDR(s);
        n -= 1;
    }

    return 1;
}

static SEXP do_deparse(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP ca1;
    int  cut0, backtick, opts, nlines;

    checkArity(op, args);

    if(length(args) < 1) error(_("too few arguments"));

    ca1 = CAR(args); args = CDR(args);
    cut0 = DEFAULT_Cutoff;
    if(!isNull(CAR(args))) {
	cut0 = asInteger(CAR(args));
	if(cut0 == NA_INTEGER|| cut0 < MIN_Cutoff || cut0 > MAX_Cutoff) {
	    warning(_("invalid 'cutoff' for deparse, using default"));
	    cut0 = DEFAULT_Cutoff;
	}
    }
    args = CDR(args);
    backtick = 0;
    if(!isNull(CAR(args)))
	backtick = asLogical(CAR(args));
    args = CDR(args);
    opts = SHOWATTRIBUTES;
    if(!isNull(CAR(args)))
	opts = asInteger(CAR(args));
    args = CDR(args);
    nlines = asInteger(CAR(args));
    if (nlines == NA_INTEGER) nlines = -1;
    ca1 = deparse1WithCutoff(ca1, 0, cut0, backtick, opts, nlines);
    return ca1;
}

SEXP deparse1(SEXP call, Rboolean abbrev, int opts)
{
    Rboolean backtick = TRUE;
    return deparse1WithCutoff(call, abbrev, DEFAULT_Cutoff, backtick,
			      opts, -1);
}

static SEXP deparse1WithCutoff(SEXP call, Rboolean abbrev, int cutoff,
			       Rboolean backtick, int opts, int nlines)
{
/* Arg. abbrev:
	If abbrev is TRUE, then the returned value
	is a STRSXP of length 1 with at most 13 characters.
	This is used for plot labelling etc.
*/
    SEXP svec;
    int savedigits;
    Rboolean need_ellipses = FALSE;
    LocalParseData localData =
	    {0, 0, 0, 0, /*startline = */TRUE, 0,
	     R_NoObject,
	     /*DeparseBuffer=*/{NULL, 0, BUFSIZE},
	     DEFAULT_Cutoff, FALSE, 0, TRUE, FALSE, INT_MAX, TRUE, 0};
    R_AllocStringBuffer (0, &localData.buffer);
    localData.cutoff = cutoff;
    localData.backtick = backtick;
    localData.opts = opts;
    localData.strvec = R_NilValue;

    PrintDefaults(); /* from global options() */
    savedigits = R_print.digits;
    R_print.digits = DBL_DIG;/* MAX precision */

    svec = R_NilValue;
    if (nlines > 0) {
	localData.linenumber = localData.maxlines = nlines;
    } else {
	deparse2(call, svec, &localData);/* just to determine linenumber..*/
	localData.active = TRUE;
	if(R_BrowseLines > 0 && localData.linenumber > R_BrowseLines) {
	    localData.linenumber = localData.maxlines = R_BrowseLines + 1;
	    need_ellipses = TRUE;
	}
    }
    PROTECT(svec = allocVector(STRSXP, localData.linenumber));
    deparse2(call, svec, &localData);
    if (abbrev) {
	char data[14];
	strncpy(data, CHAR(STRING_ELT(svec, 0)), 10);
	data[10] = '\0';
	if (strlen(CHAR(STRING_ELT(svec, 0))) > 10) strcat(data, "...");
	svec = mkString(data);
    } else if(need_ellipses) {
	SET_STRING_ELT(svec, R_BrowseLines, mkChar("  ..."));
    }
    if(nlines > 0 && localData.linenumber < nlines) {
	UNPROTECT(1); /* old svec value */
	PROTECT(svec);
	svec = lengthgets(svec, localData.linenumber);
    }
    UNPROTECT(1);
    PROTECT(svec); /* protect from warning() allocating, PR#14356 */
    R_print.digits = savedigits;
    if ((opts & WARNINCOMPLETE) && localData.isS4)
	warning(_("deparse of an S4 object will not be source()able"));
    else if ((opts & WARNINCOMPLETE) && !localData.sourceable)
	warning(_("deparse may be incomplete"));
    if ((opts & WARNINCOMPLETE) && localData.longstring)
	warning(_("deparse may be not be source()able in R < 2.7.0"));
    /* somewhere lower down might have allocated ... */
    R_FreeStringBuffer(&(localData.buffer));
    UNPROTECT(1);
    return svec;
}

/* deparse1line concatenates all lines into one long one */
/* This is needed in terms.formula, where we must be able */
/* to deparse a term label into a single line of text so */
/* that it can be reparsed correctly */
SEXP deparse1line(SEXP call, Rboolean abbrev)
{
    SEXP temp;
    Rboolean backtick=TRUE;
    int lines;

    PROTECT(temp = deparse1WithCutoff(call, abbrev, MAX_Cutoff, backtick,
			     SIMPLEDEPARSE, -1));
    if ((lines = length(temp)) > 1) {
	char *buf;
	int i, len;
	const void *vmax;
	cetype_t enc = CE_NATIVE;
	for (len=0, i = 0; i < length(temp); i++) {
	    SEXP s = STRING_ELT(temp, i);
	    cetype_t thisenc = getCharCE(s);
	    len += strlen(CHAR(s));
	    if (thisenc != CE_NATIVE) 
	    	enc = thisenc; /* assume only one non-native encoding */ 
	}    
	vmax = VMAXGET();
	buf = R_alloc((size_t) len+lines, sizeof(char));
	*buf = '\0';
	for (i = 0; i < length(temp); i++) {
	    strcat(buf, CHAR(STRING_ELT(temp, i)));
	    if (i < lines - 1)
	    	strcat(buf, "\n");
	}
	temp = ScalarString(mkCharCE(buf, enc));
	VMAXSET(vmax);
    }		
    UNPROTECT(1);	
    return(temp);
}

SEXP attribute_hidden deparse1s(SEXP call)
{
   SEXP temp;
   Rboolean backtick=TRUE;

   temp = deparse1WithCutoff(call, FALSE, DEFAULT_Cutoff, backtick,
			     DEFAULTDEPARSE | CODEPROMISES, 1);
   return(temp);
}

#include "Rconnections.h"

static void con_cleanup(void *data)
{
    Rconnection con = data;
    if(con->isopen) con->close(con);
}

static SEXP do_dput(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP saveenv, tval;
    int i, ifile, res;
    Rboolean wasopen, havewarned = FALSE, opts;
    Rconnection con = (Rconnection) 1; /* stdout */
    RCNTXT cntxt;

    checkArity(op, args);

    tval = CAR(args);
    saveenv = R_NilValue;	/* -Wall */
    if (TYPEOF(tval) == CLOSXP) {
	PROTECT(saveenv = CLOENV(tval));
	SET_CLOENV(tval, R_GlobalEnv);
    }
    opts = SHOWATTRIBUTES;
    if(!isNull(CADDR(args)))
	opts = asInteger(CADDR(args));

    tval = deparse1(tval, 0, opts);
    if (TYPEOF(CAR(args)) == CLOSXP) {
	SET_CLOENV(CAR(args), saveenv);
	UNPROTECT(1);
    }
    PROTECT(tval); /* against Rconn_printf */
    ifile = asInteger(CADR(args));

    wasopen = 1;
    if (ifile != 1) {
	con = getConnection(ifile);
	wasopen = con->isopen;
	if(!wasopen) {
	    char mode[5];	
	    strcpy(mode, con->mode);
	    strcpy(con->mode, "w");
	    if(!con->open(con)) error(_("cannot open the connection"));
	    strcpy(con->mode, mode);
	    /* Set up a context which will close the connection on error */
	    begincontext(&cntxt, CTXT_CCODE, R_NilValue, R_BaseEnv, R_BaseEnv,
			 R_NilValue, R_NilValue);
	    cntxt.cend = &con_cleanup;
	    cntxt.cenddata = con;
	}
	if(!con->canwrite) error(_("cannot write to this connection"));
    }/* else: "Stdout" */
    for (i = 0; i < LENGTH(tval); i++)
	if (ifile == 1)
	    Rprintf("%s\n", CHAR(STRING_ELT(tval, i)));
	else {
	    res = Rconn_printf(con, "%s\n", CHAR(STRING_ELT(tval, i)));
	    if(!havewarned &&
	       res < strlen(CHAR(STRING_ELT(tval, i))) + 1)
		warning(_("wrote too few characters"));
	}
    UNPROTECT(1); /* tval */
    if(!wasopen) {endcontext(&cntxt); con->close(con);}
    return (CAR(args));
}

static SEXP do_dump(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP file, names, o, objs, tval, source, outnames;
    int i, j, nobjs, nout, res;
    int wasopen, havewarned = FALSE, evaluate;
    Rconnection con;
    int opts;
    const char *obj_name;
    RCNTXT cntxt;

    checkArity(op, args);

    names = CAR(args);
    file = CADR(args);
    if(!isString(names))
	error( _("character arguments expected"));
    nobjs = length(names);
    if(nobjs < 1 || length(file) < 1)
	error(_("zero length argument"));
    source = CADDR(args);
    if (source != R_NilValue && TYPEOF(source) != ENVSXP)
	error(_("invalid '%s' argument"), "envir");
    opts = asInteger(CADDDR(args));
    /* <NOTE>: change this if extra options are added */
    if(opts == NA_INTEGER || opts < 0 || opts > 256)
	errorcall(call, _("'opts' should be small non-negative integer"));
    evaluate = asLogical(CAD4R(args));
    if (evaluate==FALSE) opts |= DELAYPROMISES;
    if (evaluate==NA_LOGICAL) opts |= CODEPROMISES;

    PROTECT(o = objs = allocList(nobjs));

    for (j = 0, nout = 0; j < nobjs; j++, o = CDR(o)) {
	SET_TAG(o, install_translated (STRING_ELT(names,j)));
	SETCAR(o, findVar(TAG(o), source));
	if (CAR(o) == R_UnboundValue)
	    warning(_("object '%s' not found"), CHAR(PRINTNAME(TAG(o))));
	else nout++;
    }
    o = objs;
    PROTECT(outnames = allocVector(STRSXP, nout));
    if(nout > 0) {
	if(INTEGER(file)[0] == 1) {
	    for (i = 0, nout = 0; i < nobjs; i++) {
		if (CAR(o) == R_UnboundValue) continue;
		obj_name = translateChar(STRING_ELT(names, i));
		SET_STRING_ELT(outnames, nout++, STRING_ELT(names, i));
		if(isValidName(obj_name)) Rprintf("%s <-\n", obj_name);
		else if(opts & S_COMPAT) Rprintf("\"%s\" <-\n", obj_name);
		else Rprintf("`%s` <-\n", obj_name);
		tval = PROTECT(deparse1(CAR(o), 0, opts));
		for (j = 0; j < LENGTH(tval); j++)
		    Rprintf("%s\n", CHAR(STRING_ELT(tval, j)));/* translated */
		UNPROTECT(1); /* tval */
		o = CDR(o);
	    }
	}
	else {
	    con = getConnection(INTEGER(file)[0]);
	    wasopen = con->isopen;
	    if(!wasopen) {
		char mode[5];	
		strcpy(mode, con->mode);
		strcpy(con->mode, "w");
		if(!con->open(con)) error(_("cannot open the connection"));
		strcpy(con->mode, mode);
		/* Set up a context which will close the connection on error */
		begincontext(&cntxt, CTXT_CCODE, R_NilValue, R_BaseEnv, R_BaseEnv,
			     R_NilValue, R_NilValue);
		cntxt.cend = &con_cleanup;
		cntxt.cenddata = con;
	    } 
	    if(!con->canwrite) error(_("cannot write to this connection"));
	    for (i = 0, nout = 0; i < nobjs; i++) {
		const char *s;
		unsigned int extra = 6;
		if (CAR(o) == R_UnboundValue) continue;
		SET_STRING_ELT(outnames, nout++, STRING_ELT(names, i));
		s = translateChar(STRING_ELT(names, i));
		if(isValidName(s)) {
		    extra = 4;
		    res = Rconn_printf(con, "%s <-\n", s);
		} else if(opts & S_COMPAT)
		    res = Rconn_printf(con, "\"%s\" <-\n", s);
		else
		    res = Rconn_printf(con, "`%s` <-\n", s);
		if(!havewarned && res < strlen(s) + extra)
		    warning(_("wrote too few characters"));
		PROTECT(tval = deparse1(CAR(o), 0, opts));
		for (j = 0; j < LENGTH(tval); j++) {
		    res = Rconn_printf(con, "%s\n", CHAR(STRING_ELT(tval, j)));
		    if(!havewarned &&
		       res < strlen(CHAR(STRING_ELT(tval, j))) + 1)
			warning(_("wrote too few characters"));
		}
		UNPROTECT(1); /* tval */
		o = CDR(o);
	    }
	    if(!wasopen) {endcontext(&cntxt); con->close(con);}
	}
    }

    UNPROTECT(2);
    return outnames;
}

static void linebreak(Rboolean *lbreak, LocalParseData *d)
{
    if (d->len > d->cutoff) {
	if (!*lbreak) {
	    *lbreak = TRUE;
	    d->indent++;
	}
	writeline(d);
    }
}

static void deparse2(SEXP what, SEXP svec, LocalParseData *d)
{
    d->strvec = svec;
    d->linenumber = 0;
    d->indent = 0;
    deparse2buff(what, d);
    writeline(d);
}


/* curlyahead looks at s to see if it is a list with
   the first op being a curly.  You need this kind of
   lookahead info to print if statements correctly.  */
static Rboolean
curlyahead(SEXP s)
{
    if (isList(s) || isLanguage(s))
	if (TYPEOF(CAR(s)) == SYMSXP && CAR(s) == R_BraceSymbol)
	    return TRUE;
    return FALSE;
}


/* Check whether an expression is a complex literal that will be deparsed
   as a sum. */

static int complex_literal (SEXP expr)
{
    return TYPEOF(expr) == CPLXSXP && LENGTH(expr)==1 && 
           !ISNAN(COMPLEX(expr)->r) && COMPLEX(expr)->r != 0;
}


/* Find the lowest precedence of an unenclosed operator at the right edge of
   an expression.  For example, for the expression

       a * b ^ if (v) 1 else a/2 

   the right edge precedence is that of "if" (which is the lowest), not 
   that of the top-level "*" operator, or of the rightmost "/" operator.

   Returns 0 if the expression is not of operator form. */

static int right_edge_prec (SEXP expr)
{
    if (TYPEOF(expr) == LANGSXP && TYPEOF(CAR(expr)) == SYMSXP) {
        int nargs = length(CDR(expr));
        int lowest = INT_MAX;
        int prec;
        if (nargs == 1 && (prec = unary_prec(CAR(expr)))) lowest = prec;
        if (nargs == 2 && (prec = binary_prec(CAR(expr)))) lowest = prec;
        if (lowest == INT_MAX && (prec = misc_prec(CAR(expr)))) lowest = prec;
        if (lowest == INT_MAX)
            return 0;
        if (nargs > 0) {
            prec = right_edge_prec (CAR(nthcdr(expr,nargs)));
            if (prec && prec < lowest) lowest = prec;
        }
        return lowest;
    }
    else if (complex_literal(expr)) {
        /* Handle a complex number that will be deparsed as a sum */
        return binary_prec(R_AddSymbol);
    }
    else
        return 0;
}


/* Determine whether an argument to a postfix operator needs to be 
   parenthesized when deparsed.  The symbol op is the outer postfix
   operator (eg, $).  The expression arg is the first operand. */

attribute_hidden Rboolean needsparens_postfix (SEXP op, SEXP arg)
{
    int op_prec = misc_prec(op);
    if (!op_prec) abort();

    int right_prec = right_edge_prec (arg);
    return right_prec && right_prec < op_prec;
}

/* Determine whether an argument to a unary operator needs to be 
   parenthesized when deparsed.  The symbol op is the outer unary
   operator.  The expression arg is the operand. */

attribute_hidden Rboolean needsparens_unary (SEXP op, SEXP arg)
{
    int op_prec = unary_prec(op);
    int in_prec;

    if (!op_prec) abort();

    if (TYPEOF(arg) == LANGSXP && TYPEOF(CAR(arg)) == SYMSXP) {
        int nargs = length(CDR(arg));
        if (nargs == 2) {
            in_prec = binary_prec(CAR(arg));
            if (in_prec && in_prec < op_prec)
                return TRUE;
        }
    }
    else if (complex_literal(arg)) {
        /* Handle a complex number that will be deparsed as a sum */
        in_prec = binary_prec(R_AddSymbol);
        if (!in_prec) abort();
        if (in_prec < op_prec)
            return TRUE;
    }

    return FALSE;
}

/* Determine whether an argument to a binary operator needs to be 
   parenthesized when deparsed.  The symbol op is the outer binary
   operator.  The expression arg is the operand, on the left if
   left is 1. */

attribute_hidden Rboolean needsparens_binary (SEXP op, SEXP arg, int left)
{
    int op_prec = binary_prec(op);
    if (!op_prec) abort();

    if (left) {
        int right_prec = right_edge_prec (arg);
        if (right_prec && right_prec < op_prec)
            return TRUE;
    }

    int in_prec;

    if (TYPEOF(arg) == LANGSXP && TYPEOF(CAR(arg)) == SYMSXP) {
        int nargs = length(CDR(arg));
        if (nargs == 2) {
            in_prec = binary_prec(CAR(arg));
            if (in_prec) {
                if (in_prec < op_prec || in_prec == op_prec &&
                      (NON_ASSOC(op_prec) || LEFT_ASSOC(op_prec) != left))
                    return TRUE;
            }
        }
    }
    else if (complex_literal(arg)) {
        /* Handle a complex number that will be deparsed as a sum */
        in_prec = binary_prec(R_AddSymbol); /* Note + is left associative */
        if (!in_prec) abort();
        if (in_prec < op_prec || in_prec == op_prec && !left)
            return TRUE;
    }

    return FALSE;
}


/* Determine whether an argument of a function call, a subscript, or the
   condition in an if or while statement needs to be parenthesized when 
   deparsed.  It's necessary only for assignments with the = operator. */

attribute_hidden Rboolean needsparens_arg (SEXP arg)
{
    return TYPEOF(arg) == LANGSXP && CAR(arg) == R_EqAssignSymbol;
}


/* check for attributes other than function source */
static Rboolean hasAttributes(SEXP s)
{
    SEXP a = ATTRIB(s);
    if (length(a) > 2) return(TRUE);
    while(!isNull(a)) {
	if(TAG(a) != R_SrcrefSymbol
	   && (TYPEOF(s) != CLOSXP || TAG(a) != R_SourceSymbol))
	    return(TRUE);
	a = CDR(a);
    }
    return(FALSE);
}

static void attr1(SEXP s, LocalParseData *d)
{
    if(hasAttributes(s))
	print2buff("structure(", d);
}

static void attr2(SEXP s, LocalParseData *d)
{
    int localOpts = d->opts;

    if(hasAttributes(s)) {
	SEXP a = ATTRIB(s);
	while(!isNull(a)) {
	    if(TAG(a) != R_SourceSymbol && TAG(a) != R_SrcrefSymbol) {
		print2buff(", ", d);
		if(TAG(a) == R_DimSymbol) {
		    print2buff(".Dim", d);
		}
		else if(TAG(a) == R_DimNamesSymbol) {
		    print2buff(".Dimnames", d);
		}
		else if(TAG(a) == R_NamesSymbol) {
		    print2buff(".Names", d);
		}
		else if(TAG(a) == R_TspSymbol) {
		    print2buff(".Tsp", d);
		}
		else if(TAG(a) == R_LevelsSymbol) {
		    print2buff(".Label", d);
		}
		else {
		    /* TAG(a) might contain spaces etc */
		    const char *tag = CHAR(PRINTNAME(TAG(a)));
		    d->opts = SIMPLEDEPARSE; /* turn off quote()ing */
		    if(isValidName(tag))
			deparse2buff(TAG(a), d);
		    else {
			print2buff("\"", d);
			deparse2buff(TAG(a), d);
			print2buff("\"", d);
		    }
		    d->opts = localOpts;
		}
		print2buff(" = ", d);
		deparse2buff(CAR(a), d);
	    }
	    a = CDR(a);
	}
	print2buff(")", d);
    }
}


static void printcomment(SEXP s, LocalParseData *d)
{
    SEXP cmt;
    int i, ncmt;

    /* look for old-style comments first */

    if(isList(TAG(s)) && !isNull(TAG(s))) {
	for (s = TAG(s); s != R_NilValue; s = CDR(s)) {
	    print2buff(translateChar(STRING_ELT(CAR(s), 0)), d);
	    writeline(d);
	}
    }
    else {
	cmt = getAttrib(s, R_CommentSymbol);
	ncmt = length(cmt);
	for(i = 0 ; i < ncmt ; i++) {
	    print2buff(translateChar(STRING_ELT(cmt, i)), d);
	    writeline(d);
	}
    }
}


static const char * quotify(SEXP name, int quote)
{
    const char *s = CHAR(name);

    /* If a symbol is not a valid name, put it in quotes, escaping
       any quotes in the string itself */

    if (*s == 0 /* really?? */ || isValidName(s))
        return s;
    else
        return EncodeString(name, 0, quote, Rprt_adj_none);
}


/* This is the recursive part of deparsing. */

#define SIMPLE_OPTS (~QUOTEEXPRESSIONS & ~SHOWATTRIBUTES & ~DELAYPROMISES)
/* keep KEEPINTEGER | USESOURCE | KEEPNA | S_COMPAT, also
   WARNINCOMPLETE but that is not used below this point. */

static void deparse2buff(SEXP s, LocalParseData *d)
{
    Rboolean lookahead = FALSE, lbreak = FALSE, parens;
    SEXP op, t;
    int localOpts = d->opts, i, n;

    if (!d->active) return;

    if (IS_S4_OBJECT(s)) d->isS4 = TRUE;

    switch (TYPEOF(s)) {
    case NILSXP:
	print2buff("NULL", d);
	break;
    case SYMSXP:
	if (localOpts & QUOTEEXPRESSIONS) {
	    attr1(s, d);
	    print2buff("quote(", d);
	}
	if (localOpts & S_COMPAT) {
	    print2buff(quotify(PRINTNAME(s), '"'), d);
	} else if (d->backtick)
	    print2buff(quotify(PRINTNAME(s), '`'), d);
	else
	    print2buff(CHAR(PRINTNAME(s)), d);
	if (localOpts & QUOTEEXPRESSIONS) {
	    print2buff(")", d);
	    attr2(s, d);
	}
	break;
    case CHARSXP:
    {
	const char *ts = translateChar(s);
	/* versions of R < 2.7.0 cannot parse strings longer than 8192 chars */
	if(strlen(ts) >= 8192) d->longstring = TRUE;
	print2buff(ts, d);
	break;
    }
    case SPECIALSXP:
    case BUILTINSXP:
	print2buff(".Primitive(\"", d);
	print2buff(PRIMNAME(s), d);
	print2buff("\")", d);
	break;
    case PROMSXP:
	if(d->opts & (DELAYPROMISES | CODEPROMISES)) {
	    d->sourceable = FALSE;
	    if (d->opts & DELAYPROMISES) print2buff("<promise: ", d);
	    d->opts &= ~QUOTEEXPRESSIONS; /* don't want delay(quote()) */
	    deparse2buff(PREXPR(s), d);
	    d->opts = localOpts;
	    if (d->opts & DELAYPROMISES) print2buff(">", d);
	} else {
	    PROTECT(s = eval(s, R_NilValue)); /* eval uses env of promise */
	    deparse2buff(s, d);
	    UNPROTECT(1);
	}
	break;
    case CLOSXP:
	if (localOpts & SHOWATTRIBUTES) attr1(s, d);
	if ((d->opts & USESOURCE)
	    && !isNull(t = getAttrib(s, R_SrcrefSymbol))) 
	    	src2buff1(t, d);
	else {
	    /* We have established that we don't want to use the
	       source for this function */
	    d->opts &= SIMPLE_OPTS & ~USESOURCE;
	    print2buff("function (", d);
	    args2buff(FORMALS(s), 0, 1, d);
	    print2buff(") ", d);

	    writeline(d);
	    deparse2buff(BODY_EXPR(s), d);
	    d->opts = localOpts;
	}
	if (localOpts & SHOWATTRIBUTES) attr2(s, d);
	break;
    case ENVSXP:
	d->sourceable = FALSE;
	print2buff("<environment>", d);
	break;
    case VECSXP:
	if (localOpts & SHOWATTRIBUTES) attr1(s, d);
	print2buff("list(", d);
	vec2buff(s, d);
	print2buff(")", d);
	if (localOpts & SHOWATTRIBUTES) attr2(s, d);
	break;
    case EXPRSXP:
	if (localOpts & SHOWATTRIBUTES) attr1(s, d);
	if(length(s) <= 0)
	    print2buff("expression()", d);
	else {
	    print2buff("expression(", d);
	    d->opts &= SIMPLE_OPTS;
	    vec2buff(s, d);
	    d->opts = localOpts;
	    print2buff(")", d);
	}
	if (localOpts & SHOWATTRIBUTES) attr2(s, d);
	break;
    case LISTSXP:
	if (localOpts & SHOWATTRIBUTES) attr1(s, d);
	print2buff("list(", d);
	d->inlist++;
	for (t=s ; CDR(t) != R_NilValue ; t=CDR(t) ) {
	    if( TAG(t) != R_NilValue ) {
		d->opts = SIMPLEDEPARSE; /* turn off quote()ing */
		deparse2buff(TAG(t), d);
		d->opts = localOpts;
		print2buff(" = ", d);
	    }
	    deparse2buff(CAR(t), d);
	    print2buff(", ", d);
	}
	if( TAG(t) != R_NilValue ) {
	    d->opts = SIMPLEDEPARSE; /* turn off quote()ing */
	    deparse2buff(TAG(t), d);
	    d->opts = localOpts;
	    print2buff(" = ", d);
	}
	deparse2buff(CAR(t), d);
	print2buff(")", d);
	d->inlist--;
	if (localOpts & SHOWATTRIBUTES) attr2(s, d);
	break;
    case LANGSXP:
	printcomment(s, d);
	if (!isNull(ATTRIB(s)))
	    d->sourceable = FALSE;
	if (localOpts & QUOTEEXPRESSIONS) {
	    print2buff("quote(", d);
	    d->opts &= SIMPLE_OPTS;
	}
        op = CAR(s);
        s = CDR(s);
	if (TYPEOF(op) == SYMSXP) {
            const char *opname = CHAR(PRINTNAME(op));
            int nargs = length(s);
            if (op == R_IfSymbol && nargs >= 2 && nargs <= 3) {
                print2buff("if (", d);
                /* print the predicate */
                int np = needsparens_arg(CAR(s));
                if (np) print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (np) print2buff(")", d);
                print2buff(") ", d);
                if (d->incurly && !d->inlist ) {
                    lookahead = curlyahead(CAR(CDR(s)));
                    if (!lookahead) {
                        writeline(d);
                        d->indent++;
                    }
                }
                if (nargs > 2) { /* else present */
                    SEXP e = CADR(s);
                    int inner_if_with_no_else = TYPEOF(e) == LANGSXP 
                          && CAR(e) == R_IfSymbol && length(e) == 3;
                    if (inner_if_with_no_else)
                        print2buff("( ", d);
                    deparse2buff(e, d);
                    if (d->incurly && !d->inlist) {
                        writeline(d);
                        if (!lookahead)
                            d->indent--;
                    }
                    else
                        print2buff(" ", d);
                    if (inner_if_with_no_else)
                        print2buff(") ", d);
                    print2buff("else ", d);
                    deparse2buff(CAR(CDDR(s)), d);
                }
                else { /* no else present */
                    deparse2buff(CAR(CDR(s)), d);
                    if (d->incurly && !lookahead && !d->inlist)
                    d->indent--;
                }
            }
            else if (op == R_WhileSymbol && nargs == 2) {
                print2buff("while (", d);
                int np = needsparens_arg(CAR(s));
                if (np) print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (np) print2buff(")", d);
                print2buff(") ", d);
                deparse2buff(CADR(s), d);
            }
            else if (op == R_ForSymbol && nargs == 3 
                                       && isSymbol(CAR(s))
                                       && (TAG(CDR(s))==R_NilValue
                                           || TAG(CDR(s))==R_AcrossSymbol
                                           || TAG(CDR(s))==R_DownSymbol)) {
                print2buff("for (", d);
                deparse2buff(CAR(s), d);
                if (TAG(CDR(s)) == R_AcrossSymbol)
                    print2buff(" across ", d);
                else if (TAG(CDR(s)) == R_DownSymbol)
                    print2buff(" down ", d);
                else
                    print2buff(" in ", d);
                int np = needsparens_arg(CADR(s));
                if (np) print2buff("(", d);
                deparse2buff(CADR(s), d);
                if (np) print2buff(")", d);
                print2buff(") ", d);
                deparse2buff(CADR(CDR(s)), d);
            }
            else if (op == R_ForSymbol && nargs >= 3
                                     && isSymbol(CAR(s))
                                     && TAG(nthcdr(s,nargs-2))==R_AlongSymbol) {
                print2buff("for (", d);
                deparse2buff(CAR(s), d);
                SEXP t = CDR(s);
                while (CDDR(t) != R_NilValue) {
                    print2buff(",", d);
                    deparse2buff(CAR(t), d);
                    t = CDR(t);
                }
                print2buff(" along ", d);
                int np = needsparens_arg(CAR(t));
                if (np) print2buff("(", d);
                deparse2buff(CAR(t), d);
                if (np) print2buff(")", d);
                print2buff(") ", d);
                deparse2buff(CADR(t), d);
            }
            else if (op == R_RepeatSymbol && nargs == 1) {
                print2buff("repeat ", d);
                deparse2buff(CAR(s), d);
            }
            else if ((op == R_BreakSymbol || op == R_NextSymbol) && nargs==0) {
                print2buff(opname, d);
            }
            else if ((op == R_WithGradientSymbol 
                        || op == R_TrackGradientSymbol
                        || op == R_BackGradientSymbol)
                       && nargs >= 2 && has_n_tags(s,nargs-1)
                   || op == R_ComputeGradientSymbol && nargs > 2 && nargs%2 == 1
                       && has_n_tags(s,nargs/2)) {
                SEXP skip = 
                     nthcdr(s, op==R_ComputeGradientSymbol ? nargs/2 : nargs-1);
                SEXP t;
                print2buff(opname, d);
                print2buff(" (", d);
                for (t = s; t != skip; t = CDR(t)) {
                    print2buff(CHAR(PRINTNAME(TAG(t))), d);
                    if (TAG(t) != CAR(t)) {
                        print2buff(" = ", d);
                        int np = needsparens_arg(CAR(t));
                        if (np) print2buff("(", d);
                        deparse2buff(CAR(t), d);
                        if (np) print2buff(")", d);
                    }
                    if (CDR(t) != skip) print2buff(", ",d);
                }
                print2buff(") ", d);
                deparse2buff(CAR(skip), d);
                if (op == R_ComputeGradientSymbol) {
                    print2buff(" ", d);
                    print2buff("as ", d);
                    for (t = CDR(skip); t != R_NilValue; t = CDR(t)) {
                        deparse2buff(CAR(t), d);
                        if (CDR(t) != R_NilValue) print2buff(", ",d);
                    }
                }
            }
            else if (op == R_BraceSymbol) {
                print2buff("{", d);
                d->incurly += 1;
                d->indent++;
                writeline(d);
                while (s != R_NilValue) {
                    deparse2buff(CAR(s), d);
                    writeline(d);
                    s = CDR(s);
                }
                d->indent--;
                print2buff("}", d);
                d->incurly -= 1;
            }
            else if (op == R_ParenSymbol && nargs == 1) {
                print2buff("(", d);
                deparse2buff(CAR(s), d);
                print2buff(")", d);
            }
            else if (op == R_BracketSymbol && nargs >= 2) {
                if ((parens = needsparens_postfix(op,CAR(s))))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
                print2buff("[", d);
                args2buff(CDR(s), 0, 0, d);
                print2buff("]", d);
            }
            else if (op == R_Bracket2Symbol && nargs >= 2) {
                if ((parens = needsparens_postfix(op,CAR(s))))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
                print2buff("[[", d);
                args2buff(CDR(s), 0, 0, d);
                print2buff("]]", d);
            }
            else if (op == R_FunctionSymbol && (nargs == 2 || nargs == 3)) {
                /* may have hidden third argument with source references */
                printcomment(s, d);
                if (!(d->opts & USESOURCE) || !isString(CADDR(s))) {
                    print2buff("function(", d);
                    args2buff(CAR(s), 0, 1, d);
                    print2buff(") ", d);
                    deparse2buff(CADR(s), d);
                }
                else {
                    s = CADDR(s);
                    n = length(s);
                    for(i = 0 ; i < n ; i++) {
                        print2buff(translateChar(STRING_ELT(s, i)), d);
                        writeline(d);
                    }
                }
            }
            else if ((op == R_DoubleColonSymbol || op == R_TripleColonSymbol)
                   && nargs == 2 && (isSymbol(CAR(s)) || isString(CAR(s)))
                                 && (isSymbol(CADR(s)) || isString(CADR(s)))) {
                deparse2buff(CAR(s), d);
                print2buff(opname, d);
                deparse2buff(CADR(s), d);
            }
            else if ((op == R_DollarSymbol || op == R_AtSymbol)
                   && nargs == 2 && (isSymbol(CADR(s)) || isString(CADR(s)))) {
                if ((parens = needsparens_postfix(op,CAR(s))))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
                print2buff(opname, d);
                if (0) { /* old way, unclear why deliberately does wrong thing*/
                    /*temp fix to handle printing of x$a's */
                    if (isString(CADR(s))
                           && isValidName(CHAR(STRING_ELT(CADR(s), 0))))
                        deparse2buff(STRING_ELT(CADR(s), 0), d);
                    else
                        deparse2buff(CADR(s), d);
                }
                else
                    deparse2buff(CADR(s), d);
            }
            else if (nargs == 2 && (op == R_LocalAssignSymbol
                                     || op == R_EqAssignSymbol
                                     || op == R_GlobalAssignSymbol
                                     || op == R_LocalRightAssignSymbol
                                     || op == R_GlobalRightAssignSymbol
                                     || op == R_ColonAssignSymbol)) {
                if ((parens = needsparens_binary(op,CAR(s),1)))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
                print2buff(" ", d);
                print2buff(opname, d);
                print2buff(" ", d);
                if ((parens = needsparens_binary(op,CADR(s),0)))
                    print2buff("(", d);
                deparse2buff(CADR(s), d);
                if (parens)
                    print2buff(")", d);
            }
            else if (nargs == 1 && 
                    (op == R_AddSymbol
                      || op == R_SubSymbol
                      || op == R_TildeSymbol
                      || op == R_NotSymbol
                      || op == R_QuerySymbol)) {
                print2buff(opname, d);
                if ((parens = needsparens_unary(op,CAR(s))))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
            }
            else if (nargs == 2 &&              /* space between op and args */
                    (isUserBinop(op) && TAG(s) == R_NilValue
                         && TAG(CDR(s)) == R_NilValue  /* no arg names */
                      || op == R_DotDotSymbol && R_parse_dotdot
                      || op == R_AddSymbol
                      || op == R_SubSymbol
                      || op == R_MulSymbol
                      || op == install("%*%")
                      || op == R_AndSymbol
                      || op == R_OrSymbol
                      || op == R_And2Symbol
                      || op == R_Or2Symbol
                      || op == R_TildeSymbol
                      || op == R_NotSymbol
                      || op == R_BangBangSymbol
                      || op == R_EqSymbol
                      || op == R_NeSymbol
                      || op == R_LtSymbol
                      || op == R_LeSymbol
                      || op == R_GeSymbol
                      || op == R_GtSymbol
                      || op == R_QuerySymbol)) {
                if ((parens = needsparens_binary(op,CAR(s),1)))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
                print2buff(" ", d);
                print2buff(translateChar(PRINTNAME(op)), d);
                print2buff(" ", d);
                linebreak(&lbreak, d);
                if ((parens = needsparens_binary(op,CADR(s),0)))
                    print2buff("(", d);
                deparse2buff(CADR(s), d);
                if (parens)
                    print2buff(")", d);
                if (lbreak) {
                    d->indent--;
                    lbreak = FALSE;
                }
            }
            else if (nargs == 2 &&            /* no space between op and args */
                    (op == R_DivSymbol
                      || op == R_ExptSymbol
                      || op == R_Expt2Symbol
                      || op == install("%%")
                      || op == install("%/%")
                      || op == R_ColonSymbol)) { 
                if ((parens = needsparens_binary(op,CAR(s),1)))
                    print2buff("(", d);
                deparse2buff(CAR(s), d);
                if (parens)
                    print2buff(")", d);
                print2buff(opname, d);
                if ((parens = needsparens_binary(op,CADR(s),0)))
                    print2buff("(", d);
                deparse2buff(CADR(s), d);
                if (parens)
                    print2buff(")", d);
            }
            else {
                if (d->opts & S_COMPAT) 
                    print2buff(quotify(PRINTNAME(op), '\''), d);
                else 
                    print2buff(quotify(PRINTNAME(op), '`'), d);
                print2buff("(", d);
                d->inlist++;
                args2buff(s, 0, 0, d);
                d->inlist--;
                print2buff(")", d);
            }
        }
        else if (TYPEOF(op) == LANGSXP) {
            /* use fact that xxx[] behaves the same as xxx() */
            if ((parens = needsparens_postfix(R_BracketSymbol,op)))
                print2buff("(", d);
            deparse2buff(op, d);
            if (parens)
                print2buff(")", d);
            print2buff("(", d);
            args2buff(s, 0, 0, d);
            print2buff(")", d);
        }
	else if (op == R_NilValue ||  /* silly things accepted by parser */
                 length(op) == 1 &&   /* (though giving run-time errors) */
                 ( TYPEOF(op) == LGLSXP || TYPEOF(op) == REALSXP ||
                   TYPEOF(op) == CPLXSXP && !ISNAN(COMPLEX(op)->r)
                                         && COMPLEX(op)->r == 0)) {
	    deparse2buff(op, d);
	    print2buff("(", d);
	    args2buff(s, 0, 0, d);
	    print2buff(")", d);
	}
	else {
	    print2buff("(", d);
	    deparse2buff(op, d);
	    print2buff(")", d);
	    print2buff("(", d);
	    args2buff(s, 0, 0, d);
	    print2buff(")", d);
	}
	if (localOpts & QUOTEEXPRESSIONS) {
	    d->opts = localOpts;
	    print2buff(")", d);
	}
	break;
    case STRSXP:
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case RAWSXP:
	if (localOpts & SHOWATTRIBUTES) attr1(s, d);
	vector2buff(s, d);
	if (localOpts & SHOWATTRIBUTES) attr2(s, d);
	break;
    case EXTPTRSXP:
    {
	char tpb[32]; /* need 12+2+2*sizeof(void*) */
	d->sourceable = FALSE;
	snprintf(tpb, 32, "<pointer: %p>", R_ExternalPtrAddr(s));
	tpb[31] = '\0';
	print2buff(tpb, d);
    }
	break;
    case BCODESXP:
	d->sourceable = FALSE;
	print2buff("<bytecode>", d);
	break;
    case WEAKREFSXP:
	d->sourceable = FALSE;
	print2buff("<weak reference>", d);
	break;
    case S4SXP:
	d->sourceable = FALSE;
	d->isS4 = TRUE;
	print2buff("<S4 object of class ", d);
	deparse2buff(getClassAttrib(s), d);
	print2buff(">", d);
      break;
    default:
	d->sourceable = FALSE;
	UNIMPLEMENTED_TYPE("deparse2buff", s);
    }
}


/* If there is a string array active point to that, and */
/* otherwise we are counting lines so don't do anything. */

static void writeline(LocalParseData *d)
{
    if (d->strvec != R_NilValue && d->linenumber < d->maxlines)
	SET_STRING_ELT(d->strvec, d->linenumber, mkChar(d->buffer.data));
    d->linenumber++;
    if (d->linenumber >= d->maxlines) d->active = FALSE;
    /* reset */
    d->len = 0;
    d->buffer.data[0] = '\0';
    d->startline = TRUE;
}

static void print2buff(const char *strng, LocalParseData *d)
{
    size_t tlen;

    if (d->startline) {
	d->startline = FALSE;
	printtab2buff(d->indent, d);	/*if at the start of a line tab over */
    }
    tlen = strlen(strng);
    R_AllocStringBuffer (d->len + tlen, &d->buffer);
    strcpy (d->buffer.data + d->len, strng);
    d->len += tlen;
}

static void vector2buff(SEXP vector, LocalParseData *d)
{
    int tlen, i, quote;
    const char *strp;
    Rboolean surround = FALSE, allNA, addL = TRUE;

    tlen = LENGTH(vector);
    quote = isString(vector) ? '"' : 0;

    if (tlen == 0) {
	switch(TYPEOF(vector)) {
	case LGLSXP: print2buff("logical(0)", d); break;
	case INTSXP: print2buff("integer(0)", d); break;
	case REALSXP: print2buff("numeric(0)", d); break;
	case CPLXSXP: print2buff("complex(0)", d); break;
	case STRSXP: print2buff("character(0)", d); break;
	case RAWSXP: print2buff("raw(0)", d); break;
	default: UNIMPLEMENTED_TYPE("vector2buff", vector);
	}
    }
    else if(TYPEOF(vector) == INTSXP) {
	/* We treat integer separately, as S_compatible is relevant.

	   Also, it is neat to deparse m:n in that form,
	   so we do so as from 2.5.0.
	 */
        int *tmp = INTEGER(vector);
	Rboolean intSeq = (tlen > 1) && tmp[0] != NA_INTEGER;
        if (intSeq) {
            for (i = 1; i < tlen; i++) {
                if (tmp[i-1] == INT_MAX || tmp[i-1] + 1 != tmp[i]) {
                    intSeq = FALSE;
                    break;
                }
            }
        }
        if (intSeq) {
		strp = EncodeElement(vector, 0, '"', '.');
		print2buff(strp, d);
		print2buff(":", d);
		strp = EncodeElement(vector, tlen - 1, '"', '.');
		print2buff(strp, d);
	} else {
	    addL = d->opts & KEEPINTEGER & !(d->opts & S_COMPAT);
	    allNA = (d->opts & KEEPNA) || addL;
	    for(i = 0; i < tlen; i++)
		if(tmp[i] != NA_INTEGER) {
		    allNA = FALSE;
		    break;
		}
	    if((d->opts & KEEPINTEGER && (d->opts & S_COMPAT))) {
		surround = TRUE;
		print2buff("as.integer(", d);
	    }
	    allNA = allNA && !(d->opts & S_COMPAT);
	    if(tlen > 1) print2buff("c(", d);
	    for (i = 0; i < tlen; i++) {
		if(allNA && tmp[i] == NA_INTEGER) {
		    print2buff("NA_integer_", d);
		} else {
		    strp = EncodeElement(vector, i, quote, '.');
		    print2buff(strp, d);
		    if(addL && tmp[i] != NA_INTEGER) print2buff("L", d);
		}
		if (i < (tlen - 1)) print2buff(", ", d);
		if (tlen > 1 && d->len > d->cutoff) writeline(d);
		if (!d->active) break;
	    }
	    if(tlen > 1)print2buff(")", d);
	    if(surround) print2buff(")", d);
	}
    } else {
	allNA = d->opts & KEEPNA;
	if((d->opts & KEEPNA) && TYPEOF(vector) == REALSXP) {
	    for(i = 0; i < tlen; i++)
		if(!ISNA(REAL(vector)[i])) {
		    allNA = FALSE;
		    break;
		}
	    if(allNA && (d->opts & S_COMPAT)) {
		surround = TRUE;
		print2buff("as.double(", d);
	    }
	} else if((d->opts & KEEPNA) && TYPEOF(vector) == CPLXSXP) {
	    Rcomplex *tmp = COMPLEX(vector);
	    for(i = 0; i < tlen; i++) {
		if( !ISNA(tmp[i].r) && !ISNA(tmp[i].i) ) {
		    allNA = FALSE;
		    break;
		}
	    }
	    if(allNA && (d->opts & S_COMPAT)) {
		surround = TRUE;
		print2buff("as.complex(", d);
	    }
	} else if((d->opts & KEEPNA) && TYPEOF(vector) == STRSXP) {
	    for(i = 0; i < tlen; i++)
		if(STRING_ELT(vector, i) != NA_STRING) {
		    allNA = FALSE;
		    break;
		}
	    if(allNA && (d->opts & S_COMPAT)) {
		surround = TRUE;
		print2buff("as.character(", d);
	    }
	}
        else if(TYPEOF(vector) == RAWSXP) {
	    print2buff("as.raw(", d); surround = TRUE;
 	}
	if(tlen > 1) print2buff("c(", d);
	allNA = allNA && !(d->opts & S_COMPAT);
	for (i = 0; i < tlen; i++) {
            static char buf[50];
	    if(allNA && TYPEOF(vector) == REALSXP &&
	       ISNA(REAL(vector)[i])) {
		strp = "NA_real_";
	    } else if (allNA && TYPEOF(vector) == CPLXSXP &&
		       (ISNA(COMPLEX(vector)[i].r)
			|| ISNA(COMPLEX(vector)[i].i)) ) {
		strp = "NA_complex_";
	    } else if (allNA && TYPEOF(vector) == STRSXP &&
		       STRING_ELT(vector, i) == NA_STRING) {
		strp = "NA_character_";
	    } else if (TYPEOF(vector) == REALSXP && (d->opts & S_COMPAT)) {
		int w, d, e;
		formatReal(&REAL(vector)[i], 1, &w, &d, &e, 0);
		strp = EncodeReal(REAL(vector)[i], w, d, e, 0);
	    } else if (TYPEOF(vector) == STRSXP) {
		const char *ts = translateChar(STRING_ELT(vector, i));
		/* versions of R < 2.7.0 cannot parse strings longer than 8192 chars */
		if(strlen(ts) >= 8192) d->longstring = TRUE;
		strp = EncodeElement(vector, i, quote, '.');
            } else if (TYPEOF(vector) == CPLXSXP &&
                      !ISNAN(COMPLEX(vector)[i].r) && COMPLEX(vector)[i].r==0) {
		int w, d, e;
		formatReal(&COMPLEX(vector)[i].i, 1, &w, &d, &e, 0);
		strp = EncodeReal(COMPLEX(vector)[i].i, w, d, e, '.');
                copy_2_strings (buf, sizeof buf, strp, "i");
                strp = buf;
	    } else if (TYPEOF(vector) == RAWSXP) {
                strp = EncodeRaw(RAW(vector)[i]);
                copy_2_strings (buf, sizeof buf, "0x", strp);
                strp = buf;
            } else
		strp = EncodeElement(vector, i, quote, '.');
	    print2buff(strp, d);
	    if (i < (tlen - 1)) print2buff(", ", d);
	    if (tlen > 1 && d->len > d->cutoff) writeline(d);
	    if (!d->active) break;
	}
	if(tlen > 1) print2buff(")", d);
	if(surround) print2buff(")", d);
    }
}

/* src2buff1: Deparse one source ref to buffer */

static void src2buff1(SEXP srcref, LocalParseData *d)
{
    int i,n;
    PROTECT(srcref);

    PROTECT(srcref = lang2(install("as.character"), srcref));
    PROTECT(srcref = eval(srcref, R_BaseEnv));
    n = length(srcref);
    for(i = 0 ; i < n ; i++) {
	print2buff(translateChar(STRING_ELT(srcref, i)), d);
	if(i < n-1) writeline(d);
    }
    UNPROTECT(3);
}

/* src2buff : Deparse source element k to buffer, if possible; return FALSE on failure */

static Rboolean src2buff(SEXP sv, int k, LocalParseData *d)
{
    SEXP t;

    if (TYPEOF(sv) == VECSXP && length(sv) > k && !isNull(t = VECTOR_ELT(sv, k))) {
	src2buff1(t, d);
	return TRUE;
    }
    else return FALSE;
}

/* vec2buff : New Code */
/* Deparse vectors of S-expressions. */
/* In particular, this deparses objects of mode expression. */

static void vec2buff(SEXP v, LocalParseData *d)
{
    SEXP nv, sv;
    int i, n /*, localOpts = d->opts */;
    Rboolean lbreak = FALSE;

    n = length(v);
    nv = getAttrib(v, R_NamesSymbol);
    if (length(nv) == 0) nv = R_NilValue;

    if (d->opts & USESOURCE) {
	sv = getAttrib(v, R_SrcrefSymbol);
	if (TYPEOF(sv) != VECSXP)
	    sv = R_NilValue;
    } else
	sv = R_NilValue;

    for(i = 0 ; i < n ; i++) {
	if (i > 0)
	    print2buff(", ", d);
	linebreak(&lbreak, d);
	if (!isNull(nv) && !isNull(STRING_ELT(nv, i))
	    && *CHAR(STRING_ELT(nv, i))) { /* length test */
	    /* d->opts = SIMPLEDEPARSE; This seems pointless */
	    if( isValidName(translateChar(STRING_ELT(nv, i))) )
		deparse2buff(STRING_ELT(nv, i), d);
	    else if(d->backtick) {
		print2buff("`", d);
		deparse2buff(STRING_ELT(nv, i), d);
		print2buff("`", d);
	    } else {
		print2buff("\"", d);
		deparse2buff(STRING_ELT(nv, i), d);
		print2buff("\"", d);
	    }
	    /* d->opts = localOpts; */
	    print2buff(" = ", d);
	}
	if (!src2buff(sv, i, d))
	    deparse2buff(VECTOR_ELT(v, i), d);
    }
    if (lbreak)
	d->indent--;
}

static void args2buff(SEXP arglist, int lineb, int formals, LocalParseData *d)
{
    Rboolean lbreak = FALSE;

    while (arglist != R_NilValue) {
	if (TYPEOF(arglist) != LISTSXP && TYPEOF(arglist) != LANGSXP)
	    error(_("badly formed function expression"));
        SEXP a = CAR(arglist), s = TAG(arglist);
	if (s != R_NilValue) {

	    if (d->backtick) 
		print2buff(quotify(PRINTNAME(s), '`'), d);
	    else 
	        print2buff(quotify(PRINTNAME(s), '"'), d);
	    
            if (a != R_MissingArg || !formals)
                print2buff(" = ", d);

            if (a == R_MissingUnder)
                print2buff("_", d);
            else if (a != R_MissingArg) {
                int np = needsparens_arg(a);
                if (np) print2buff("(", d);
                deparse2buff(a, d);
                if (np) print2buff(")", d);
            }
	}
        else if (a == R_MissingUnder)
            print2buff("_", d);
	else {
            /* If we get here with formals being TRUE, the expression
               is malformed.  Not clear what to do... */
            int np = needsparens_arg(a);
            if (np) print2buff("(", d);
            deparse2buff(a, d);
            if (np) print2buff(")", d);
        }
	arglist = CDR(arglist);
	if (arglist != R_NilValue) {
	    print2buff(", ", d);
	    linebreak(&lbreak, d);
	}
    }
    if (lbreak)
	d->indent--;
}

/* This code controls indentation.  Used to follow the S style, */
/* (print 4 tabs and then start printing spaces only) but I */
/* modified it to be closer to emacs style (RI). */

static void printtab2buff(int ntab, LocalParseData *d)
{
    int i;

    for (i = 1; i <= ntab; i++)
	if (i <= 4)
	    print2buff("    ", d);
	else
	    print2buff("  ", d);
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_deparse[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"deparse",	do_deparse,	0,	11,	5,	{PP_FUNCALL, PREC_FN,	0}},
{"dput",	do_dput,	0,	111,	3,	{PP_FUNCALL, PREC_FN,	0}},
{"dump",	do_dump,	0,	111,	5,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};
