/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1997--2012  The R Core Team
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

#define USE_FAST_PROTECT_MACROS
#include <Defn.h>
#include <Rmath.h>

static void R_NORETURN NULL_error (void)
{
    error(_("attempt to set an attribute on NULL"));
}

static SEXP installAttrib(SEXP, SEXP, SEXP);
static SEXP removeAttrib(SEXP, SEXP);

SEXP comment(SEXP);
static SEXP commentgets(SEXP, SEXP);

static SEXP row_names_gets(SEXP vec , SEXP val)
{
    SEXP ans;

    if (vec == R_NilValue)
	NULL_error();

    if(isReal(val) && length(val) == 2 && ISNAN(REAL(val)[0]) ) {
	/* This should not happen, but if a careless user dput()s a
	   data frame and sources the result, it will */
        PROTECT(vec);
	PROTECT(val = coerceVector(val, INTSXP));
	ans =  installAttrib(vec, R_RowNamesSymbol, val);
	UNPROTECT(2);
	return ans;
    }
    if(isInteger(val)) {
	Rboolean OK_compact = TRUE;
	int i, n = LENGTH(val);
	if(n == 2 && INTEGER(val)[0] == NA_INTEGER) {
	    n = INTEGER(val)[1];
	} else if (n > 2) {
	    for(i = 0; i < n; i++)
		if(INTEGER(val)[i] != i+1) {
		    OK_compact = FALSE;
		    break;
		}
	} else OK_compact = FALSE;
	if(OK_compact) {
	    /* we hide the length in an impossible integer vector */
	    PROTECT(vec);
	    PROTECT(val = allocVector(INTSXP, 2));
	    INTEGER(val)[0] = NA_INTEGER;
	    INTEGER(val)[1] = n;
	    ans =  installAttrib(vec, R_RowNamesSymbol, val);
	    UNPROTECT(2); /* vec, val */
	    return ans;
	}
    } else if(!isString(val))
	error(_("row names must be 'character' or 'integer', not '%s'"),
	      type2char(TYPEOF(val)));
    PROTECT(vec);
    PROTECT(val);
    ans =  installAttrib(vec, R_RowNamesSymbol, val);
    UNPROTECT(2); /* vec, val */
    return ans;
}

/* used in removeAttrib, commentgets and classgets */
static SEXP stripAttrib(SEXP tag, SEXP lst)
{
    SEXP last = R_NilValue;
    SEXP next = lst;

    while (next != R_NilValue) {
        if (TAG(next) != tag) {
            last = next;
            next = CDR(next);
        }
        else {
            next = CDR(next);
            if (last == R_NilValue)
                lst = next;
            else
                SETCDR(last,next);
        }
    }
    
    return lst;
}

/* Get the "names" attribute, and don't change its NAMEDCNT unless
   it's really taken from another attribute, or is zero even though in
   the attribute list.  Used directly in subassign.c, and in
   getAttrib0 below. */

static SEXP getAttrib0(SEXP vec, SEXP name);

/* Note:  getNamesAttrib won't always set NAMEDCNT of answer to maximum. */
SEXP attribute_hidden getNamesAttrib (SEXP vec) 
{
    SEXP s;

    if (isVector(vec) || isList(vec) || isLanguage(vec)) {
        s = getDimAttrib(vec);
        if (TYPEOF(s) == INTSXP && LENGTH(s) == 1) {
            s = getAttrib0(vec, R_DimNamesSymbol);
            if (s != R_NilValue) {
                s = VECTOR_ELT(s,0);
                SET_NAMEDCNT_MAX(s);
                return s;
            }
        }
    }

    if (isList(vec) || isLanguage(vec)) {
        R_len_t len = length(vec);
        int any = 0;
        int i = 0;
        PROTECT(s = allocVector(STRSXP, len));
        for ( ; vec != R_NilValue; vec = CDR(vec), i++) {
            if (TAG(vec) == R_NilValue)
                SET_STRING_ELT_BLANK(s, i);
            else if (isSymbol(TAG(vec))) {
                any = 1;
                SET_STRING_ELT(s, i, PRINTNAME(TAG(vec)));
            }
            else
                error(_("getAttrib: invalid type (%s) for TAG"),
                      type2char(TYPEOF(TAG(vec))));
        }
        UNPROTECT(1);
        return any ? s : R_NilValue;
    }

    for (s = ATTRIB(vec); s != R_NilValue; s = CDR(s)) {
        if (TAG(s) == R_NamesSymbol) {

            SEXP nms = CAR(s);

            /* Ensure that code getting names of vectors always gets a
               character vector - silently for now. */

            if (isVector(vec) && TYPEOF(nms) != STRSXP)
                return R_NilValue;

            SET_NAMEDCNT_NOT_0(nms);
            return nms;
        }
    }

    return R_NilValue;
}

/* The 0 version of getAttrib can be called when it is known that "name'
   is a symbol (not a string) and is not R_RowNamesSymbol (or the special
   processing for R_RowNamesSymbol is not desired).  (Currently static,
   so not callable outside this module.) */

static SEXP getAttrib0(SEXP vec, SEXP name)
{
    SEXP s;

    if (name == R_NamesSymbol) {
        s = getNamesAttrib(vec);
        SET_NAMEDCNT_NOT_0(s);
        return s;
    }

    s = getAttrib00(vec,name);

    /* This is where the old/new list adjustment happens. */

    if (name == R_DimNamesSymbol && TYPEOF(s) == LISTSXP) {
        SEXP new, old;
        int i;
        new = allocVector(VECSXP, length(s));
        old = s;
        i = 0;
        while (old != R_NilValue) {
            SET_VECTOR_ELT(new, i++, CAR(old));
            old = CDR(old);
        }
        SET_NAMEDCNT_MAX(new);
        return new;
    }

    return s;
}

/* General version of getAttrib.  Can be called when "name" may be a string.
   Does all special handling.

   NOTE: For environments serialize.c calls getAttrib to find if
   there is a class attribute in order to reconstruct the object bit
   if needed.  This means the function cannot use OBJECT(vec) == 0 to
   conclude that the class attribute is R_NilValue.  If you want to
   rewrite this function to use such a pre-test, be sure to adjust
   serialize.c accordingly.  LT */

SEXP getAttrib(SEXP vec, SEXP name)
{
    if(TYPEOF(vec) == CHARSXP)
        error("cannot have attributes on a CHARSXP");
    /* pre-test to avoid expensive operations if clearly not needed -- LT */
    if (ATTRIB(vec) == R_NilValue
          && TYPEOF(vec) != LISTSXP && TYPEOF(vec) != LANGSXP)
	return R_NilValue;

    if (isString(name)) name = install_translated (STRING_ELT(name,0));

    /* special test for c(NA, n) rownames of data frames: */
    if (name == R_RowNamesSymbol) {
	SEXP s = getAttrib00(vec, R_RowNamesSymbol);
	if(isInteger(s) && LENGTH(s) == 2 && INTEGER(s)[0] == NA_INTEGER) {
	    int i, n = abs(INTEGER(s)[1]);
	    s = allocVector(INTSXP, n);
	    for (i = 0; i < n; i++)
		INTEGER(s)[i] = i+1;
	}
	return s;
    } else
	return getAttrib0(vec, name);
}

SEXP R_shortRowNames(SEXP vec, SEXP stype)
{
    /* return  n if the data frame 'vec' has c(NA, n) rownames;
     *	       nrow(.) otherwise;  note that data frames with nrow(.) == 0
     *		have no row.names.
     ==> is also used in dim.data.frame() */
    SEXP s = getAttrib0(vec, R_RowNamesSymbol), ans = s;
    int type = asInteger(stype);

    if( type < 0 || type > 2)
	error(_("invalid '%s' argument"), "type");

    if(type >= 1) {
	int n = (isInteger(s) && LENGTH(s) == 2 && INTEGER(s)[0] == NA_INTEGER)
	    ? INTEGER(s)[1] : (s == R_NilValue ? 0 : LENGTH(s));
	ans = ScalarIntegerMaybeConst((type == 1) ? n : abs(n));
    }
    return ans;
}

/* This is allowed to change 'out' */
SEXP R_copyDFattr(SEXP in, SEXP out)
{
    SET_ATTRIB(out, ATTRIB(in));
    if (IS_S4_OBJECT(in)) SET_S4_OBJECT(out); else UNSET_S4_OBJECT(out);
    SET_OBJECT(out, OBJECT(in));
    SET_NAMEDCNT_MAX(out);  /* Kludge to undo invalid sharing done above */
    return out;
}

/* Possibly duplicate value that will be set as an attribute.

   This should probably be replaced by normal incrementing of
   NAMEDCNT, but there could be some code that doesn't check NAMEDCNT,
   assuming that the duplicate below is done.  Avoiding a duplicate
   for a vector when NAMEDCNT is already MAX_NAMEDCNT, or the vector
   is length zero, and there is no possibility of cycles, is a
   conservative attempt to reduce the number of duplications done, for
   example, when the value is a literal constant that is part of an
   expression being evaluated.

   For "names", just increment NAMEDCNT (unless could create cycle).
   Also, don't dup "class" attribute, just set to MAX_NAMEDCNT, which
   seems safe, and saves space on every object of the class.

   It is also of interest that getAttrib00 sets NAMEDCNT to
   MAX_NAMEDCNT, except for "names". */

static SEXP attr_val_dup (SEXP obj, SEXP name, SEXP val)
{
    if (name == R_NamesSymbol && !HAS_ATTRIB(val) && val != obj) {
        INC_NAMEDCNT(val);
        return val;
    }

    if (NAMEDCNT_EQ_0(val) || name == R_ClassSymbol) {
        SET_NAMEDCNT_MAX(val);
        return val;
    }

    if (isVectorAtomic(val) && !HAS_ATTRIB(val) && val != obj) {
        if (LENGTH(val) == 0) {
            SET_NAMEDCNT_MAX(val);
            return val;
        }
        if (NAMEDCNT(val) == MAX_NAMEDCNT)
            return val;
    }

    SEXP v = duplicate(val);
    if (v != val)
        SET_NAMEDCNT_MAX(v);

    return v;
}


/* Duplicate an attribute pairlist, and some of its attributes, that will be
   used for an object.  When to duplicate uses the same criterion as for
   setting an attribute. */

SEXP attribute_hidden Rf_attributes_dup (SEXP obj, SEXP attrs)
{
   if (attrs == R_NilValue)
       return R_NilValue;

   SEXP first = cons_with_tag (attr_val_dup (obj, TAG(attrs), CAR(attrs)),
                               R_NilValue, TAG(attrs));
   SEXP next = first;
   PROTECT(first);

   while ((attrs = CDR(attrs)) != R_NilValue) {
       SEXP dv = cons_with_tag (attr_val_dup (obj, TAG(attrs), CAR(attrs)),
                                R_NilValue, TAG(attrs));
       SETCDR (next, dv);
       next = dv;
   }

   UNPROTECT(1);
   return first;
}


/* 'name' should be 1-element STRSXP or SYMSXP */

SEXP setAttrib(SEXP vec, SEXP name, SEXP val)
{
    PROTECT3(vec,name,val);

    if (isString(name))
	name = install_translated (STRING_ELT(name,0));

    if (val == R_NilValue) {
	UNPROTECT(3);
	return removeAttrib(vec, name);
    }

    if (vec == R_NilValue) /* after above: we allow removing names from NULL */
	NULL_error();

    val = attr_val_dup (vec, name, val);

    UNPROTECT(3);

    if (name == R_NamesSymbol)
	return namesgets(vec, val);
    else if (name == R_DimSymbol)
	return dimgets(vec, val);
    else if (name == R_DimNamesSymbol)
	return dimnamesgets(vec, val);
    else if (name == R_ClassSymbol)
	return classgets(vec, val);
    else if (name == R_TspSymbol)
	return tspgets(vec, val);
    else if (name == R_CommentSymbol)
	return commentgets(vec, val);
    else if (name == R_RowNamesSymbol)
	return row_names_gets(vec, val);
    else
	return installAttrib(vec, name, val);
}


/* Remove dim and dimnames attributes. */

void attribute_hidden no_dim_attributes (SEXP x)
{
    PROTECT(x);

    SEXP s = ATTRIB(x);
    SEXP t = s;
    SEXP p;

    while (t != R_NilValue) {
        if (TAG(t) == R_DimSymbol || TAG(t) == R_DimNamesSymbol) {
            if (t == s) 
                SET_ATTRIB(x,CDR(t));
            else
                SETCDR(p,CDR(t));
        }
        p = t;
        t = CDR(t);
    }

    UNPROTECT(1);
}


/* This is called in the case of binary operations to copy */
/* most attributes from (one of) the input arguments to */
/* the output.	Note that the Dim and Names attributes */
/* should have been assigned elsewhere. */

void copyMostAttrib(SEXP inp, SEXP ans)
{
    SEXP s;

    if (ans == R_NilValue)
	NULL_error();

    PROTECT2(ans,inp);
    for (s = ATTRIB(inp); s != R_NilValue; s = CDR(s)) {
	if ((TAG(s) != R_NamesSymbol) &&
	    (TAG(s) != R_DimSymbol) &&
	    (TAG(s) != R_DimNamesSymbol)) {
	    installAttrib(ans, TAG(s), CAR(s));
	}
    }
    SET_OBJECT(ans, OBJECT(inp));
    if (IS_S4_OBJECT(inp)) SET_S4_OBJECT(ans); else UNSET_S4_OBJECT(ans);
    UNPROTECT(2);
}

/* Version that doesn't copy class, for VARIANT_UNCLASS. */

void attribute_hidden copyMostAttribNoClass(SEXP inp, SEXP ans)
{
    SEXP s;

    if (ans == R_NilValue)
	NULL_error();

    PROTECT2(ans,inp);
    for (s = ATTRIB(inp); s != R_NilValue; s = CDR(s)) {
	if ((TAG(s) != R_NamesSymbol) &&
	    (TAG(s) != R_ClassSymbol) &&
	    (TAG(s) != R_DimSymbol) &&
	    (TAG(s) != R_DimNamesSymbol)) {
	    installAttrib(ans, TAG(s), CAR(s));
	}
    }
    UNPROTECT(2);
}

/* version that does not preserve ts information, for subsetting */
void attribute_hidden copyMostAttribNoTs(SEXP inp, SEXP ans)
{
    SEXP s;

    if (ans == R_NilValue)
	NULL_error();

    PROTECT2(ans,inp);
    for (s = ATTRIB(inp); s != R_NilValue; s = CDR(s)) {
	if ((TAG(s) != R_NamesSymbol) &&
	    (TAG(s) != R_ClassSymbol) &&
	    (TAG(s) != R_TspSymbol) &&
	    (TAG(s) != R_DimSymbol) &&
	    (TAG(s) != R_DimNamesSymbol)) {
	    installAttrib(ans, TAG(s), CAR(s));
	} else if (TAG(s) == R_ClassSymbol) {
	    SEXP cl = CAR(s);
	    int i;
	    Rboolean ists = FALSE;
	    for (i = 0; i < LENGTH(cl); i++)
		if (strcmp(CHAR(STRING_ELT(cl, i)), "ts") == 0) { /* ASCII */
		    ists = TRUE;
		    break;
		}
	    if (!ists) installAttrib(ans, TAG(s), cl);
	    else if(LENGTH(cl) <= 1) {
	    } else {
		SEXP new_cl;
		int i, j, l = LENGTH(cl);
		PROTECT(new_cl = allocVector(STRSXP, l - 1));
		for (i = 0, j = 0; i < l; i++)
		    if (strcmp(CHAR(STRING_ELT(cl, i)), "ts")) /* ASCII */
			SET_STRING_ELT(new_cl, j++, STRING_ELT(cl, i));
		installAttrib(ans, TAG(s), new_cl);
		UNPROTECT(1);
	    }
	}
    }
    SET_OBJECT(ans, OBJECT(inp));
    if (IS_S4_OBJECT(inp)) SET_S4_OBJECT(ans); else UNSET_S4_OBJECT(ans);
    UNPROTECT(2);
}

static SEXP installAttrib(SEXP vec, SEXP name, SEXP val)
{
    SEXP s, t, last;

    if(TYPEOF(vec) == CHARSXP)
	error("cannot set attribute on a CHARSXP");

    for (s = ATTRIB(vec); s != R_NilValue; s = CDR(s)) {
	if (TAG(s) == name) {
	    SETCAR(s, val);
	    return val;
	}
        last = s;
    }

    PROTECT(vec);
    t = cons_with_tag (val, R_NilValue, name);
    UNPROTECT(1);

    if (ATTRIB(vec) == R_NilValue)
	SET_ATTRIB(vec,t);
    else
	SETCDR(last,t);

    return val;
}

static SEXP removeAttrib(SEXP vec, SEXP name)
{
    SEXP t;
    if(TYPEOF(vec) == CHARSXP)
	error("cannot set attribute on a CHARSXP");
    if (name == R_NamesSymbol && isList(vec)) {
	for (t = vec; t != R_NilValue; t = CDR(t))
	    SET_TAG_NIL(t);
	return R_NilValue;
    }
    else if (ATTRIB(vec) != R_NilValue || OBJECT(vec)) { 
        /* The "if" above avoids writing to constants (eg, R_NilValue) */
	if (name == R_DimSymbol)
	    SET_ATTRIB(vec, stripAttrib(R_DimNamesSymbol, ATTRIB(vec)));
	SET_ATTRIB(vec, stripAttrib(name, ATTRIB(vec)));
	if (name == R_ClassSymbol)
	    SET_OBJECT(vec, 0);
    }
    return R_NilValue;
}

static void checkNames(SEXP x, SEXP s)
{
    if (isVector(x) || isList(x) || isLanguage(x)) {
	if (!isVector(s) && !isList(s))
	    error(_("invalid type (%s) for 'names': must be vector"),
		  type2char(TYPEOF(s)));
	if (length(x) != length(s))
	    error(_("'names' attribute [%d] must be the same length as the vector [%d]"), length(s), length(x));
    }
    else if(IS_S4_OBJECT(x)) {
      /* leave validity checks to S4 code */
    }
    else error(_("names() applied to a non-vector"));
}


/* Time Series Parameters */

static void badtsp(void)
{
    error(_("invalid time series parameters specified"));
}

SEXP tspgets(SEXP vec, SEXP val)
{
    double start, end, frequency;
    int n;

    if (vec == R_NilValue)
	NULL_error();

    if(IS_S4_OBJECT(vec)) { /* leave validity checking to validObject */
        if (!isNumeric(val)) /* but should have been checked */
	    error(_("'tsp' attribute must be numeric"));
	installAttrib(vec, R_TspSymbol, val);
	return vec;
    }

    if (!isNumeric(val) || length(val) != 3)
	error(_("'tsp' attribute must be numeric of length three"));

    if (isReal(val)) {
	start = REAL(val)[0];
	end = REAL(val)[1];
	frequency = REAL(val)[2];
    }
    else {
	start = (INTEGER(val)[0] == NA_INTEGER) ?
	    NA_REAL : INTEGER(val)[0];
	end = (INTEGER(val)[1] == NA_INTEGER) ?
	    NA_REAL : INTEGER(val)[1];
	frequency = (INTEGER(val)[2] == NA_INTEGER) ?
	    NA_REAL : INTEGER(val)[2];
    }
    if (frequency <= 0) badtsp();
    n = nrows(vec);
    if (n == 0) error(_("cannot assign 'tsp' to zero-length vector"));

    /* FIXME:  1.e-5 should rather be == option('ts.eps') !! */
    if (fabs(end - start - (n - 1)/frequency) > 1.e-5)
	badtsp();

    PROTECT(vec);
    val = allocVector(REALSXP, 3);
    PROTECT(val);
    REAL(val)[0] = start;
    REAL(val)[1] = end;
    REAL(val)[2] = frequency;
    installAttrib(vec, R_TspSymbol, val);
    UNPROTECT(2);
    return vec;
}

static SEXP commentgets(SEXP vec, SEXP comment)
{
    if (vec == R_NilValue)
	NULL_error();

    if (isNull(comment) || isString(comment)) {
	if (length(comment) <= 0) {
	    SET_ATTRIB(vec, stripAttrib(R_CommentSymbol, ATTRIB(vec)));
	}
	else {
	    installAttrib(vec, R_CommentSymbol, comment);
	}
	return R_NilValue;
    }
    error(_("attempt to set invalid 'comment' attribute"));
}

static SEXP do_commentgets(SEXP call, SEXP op, SEXP args, SEXP env)
{
    checkArity(op, args);
    if (NAMEDCNT_GT_1(CAR(args))) 
        SETCAR(args, dup_top_level(CAR(args)));
    if (length(CADR(args)) == 0) 
        SETCADR(args, R_NilValue);
    setAttrib(CAR(args), R_CommentSymbol, CADR(args));
    return CAR(args);
}

static SEXP do_comment(SEXP call, SEXP op, SEXP args, SEXP env)
{
    checkArity(op, args);
    return getAttrib(CAR(args), R_CommentSymbol);
}

SEXP classgets(SEXP vec, SEXP klass)
{
    if (isNull(klass) || isString(klass)) {
	if (length(klass) <= 0) {
	    SET_ATTRIB(vec, stripAttrib(R_ClassSymbol, ATTRIB(vec)));
	    SET_OBJECT(vec, 0);
	}
	else {
	    /* When data frames were a special data type */
	    /* we had more exhaustive checks here.  Now that */
	    /* use JMCs interpreted code, we don't need this */
	    /* FIXME : The whole "classgets" may as well die. */

	    /* HOWEVER, it is the way that the object bit gets set/unset */

	    int i;
	    Rboolean isfactor = FALSE;

	    if (vec == R_NilValue)
		NULL_error();

	    for(i = 0; i < LENGTH(klass); i++)
		if(streql(CHAR(STRING_ELT(klass, i)), "factor")) { /* ASCII */
		    isfactor = TRUE;
		    break;
		}
	    if(isfactor && TYPEOF(vec) != INTSXP) {
		/* we cannot coerce vec here, so just fail */
		error(_("adding class \"factor\" to an invalid object"));
	    }

	    installAttrib(vec, R_ClassSymbol, klass);
	    SET_OBJECT(vec, 1);
	}
	return R_NilValue;
    }
    error(_("attempt to set invalid 'class' attribute"));
}

/* oldClass<-, SPECIALSXP so can pass on gradient. */

static SEXP do_oldclassgets (SEXP call, SEXP op, SEXP args, SEXP env,
                             int variant)
{
    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));
    
    checkArity(op, args);
    check1arg_x (args, call);

    if (NAMEDCNT_GT_1(CAR(args))) 
        SETCAR(args, dup_top_level(CAR(args)));
    if (length(CADR(args)) == 0) 
        SETCADR(args, R_NilValue);
    if (IS_S4_OBJECT(CAR(args)))
        UNSET_S4_OBJECT(CAR(args));
 
    setAttrib(CAR(args), R_ClassSymbol, CADR(args));

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return CAR(args);
}

/* oldClass, primitive */
static SEXP do_oldclass(SEXP call, SEXP op, SEXP args, SEXP env)
{
    checkArity(op, args);
    check1arg_x (args, call);
    SEXP x = CAR(args), s3class;
    if(IS_S4_OBJECT(x)) {
        if((s3class = S3Class(x)) != R_NilValue) {
            return s3class;
        }
    } 
    return getClassAttrib(x);
}

/* character elements corresponding to the syntactic types in the
   grammar */
static SEXP lang2str(SEXP obj, SEXPTYPE t)
{
  SEXP symb = CAR(obj);
  static SEXP if_sym = 0, while_sym, for_sym, eq_sym, gets_sym,
    lpar_sym, lbrace_sym, call_sym;
  if(!if_sym) {
    /* initialize:  another place for a hash table */
    if_sym = install("if");
    while_sym = install("while");
    for_sym = install("for");
    eq_sym = install("=");
    gets_sym = install("<-");
    lpar_sym = install("(");
    lbrace_sym = install("{");
    call_sym = install("call");
  }
  if(isSymbol(symb)) {
    if(symb == if_sym || symb == for_sym || symb == while_sym ||
       symb == lpar_sym || symb == lbrace_sym ||
       symb == eq_sym || symb == gets_sym)
      return PRINTNAME(symb);
  }
  return PRINTNAME(call_sym);
}

/* the S4-style class: for dispatch required to be a single string;
   for the new class() function;
   if(!singleString) , keeps S3-style multiple classes.
   Called from the methods package, so exposed.
 */
SEXP R_data_class(SEXP obj, Rboolean singleString)
{
    SEXP klass = getClassAttrib(obj);
    int n = 0;

    if (TYPEOF(klass) == STRSXP && (n = LENGTH(klass)) > 0 
                                && (n == 1 || !singleString))
        return klass;

    if (n == 0) {
	int nd;
	SEXP dim = getDimAttrib(obj);
	if (dim != R_NilValue && (nd = LENGTH(dim)) > 0)
	    klass = nd==2 ? R_matrix_CHARSXP : R_array_CHARSXP;
	else {
	  SEXPTYPE t = TYPEOF(obj);
	  switch(t) {
	  case CLOSXP: case SPECIALSXP: case BUILTINSXP:
	    klass = R_function_CHARSXP;
	    break;
	  case REALSXP:
	    klass = R_numeric_CHARSXP;
	    break;
	  case SYMSXP:
	    klass = R_name_CHARSXP;
	    break;
	  case LANGSXP:
	    klass = lang2str(obj, t);
	    break;
	  default:
	    return type2rstr(t);
	  }
	}
    }
    else
	klass = asChar(klass);

    return ScalarString(klass);
}

static SEXP s_dot_S3Class = 0;

static SEXP R_S4_extends_table = 0;

 
static SEXP cache_class(const char *class, SEXP klass) {
    if(!R_S4_extends_table) {
	R_S4_extends_table = R_NewHashedEnv(R_NilValue, ScalarIntegerMaybeConst(0));
	R_PreserveObject(R_S4_extends_table);
    }
    if(isNull(klass)) { /* retrieve cached value */
	SEXP val;
	val = findVarInFrame(R_S4_extends_table, install(class));
	return (val == R_UnboundValue) ? klass : val;
    }
    defineVar(install(class), klass, R_S4_extends_table);
    return klass;
}

static SEXP S4_extends(SEXP klass) {
  static SEXP s_extends = 0, s_extendsForS3;
    SEXP e, val; const char *class;
    if(!s_extends) {
	s_extends = install("extends");
	s_extendsForS3 = install(".extendsForS3");
	R_S4_extends_table = R_NewHashedEnv(R_NilValue, ScalarIntegerMaybeConst(0));
	R_PreserveObject(R_S4_extends_table);
    }
    /* sanity check for methods package available */
    if(findVar(s_extends, R_GlobalEnv) == R_UnboundValue)
        return klass;
    class = translateChar(STRING_ELT(klass, 0)); /* TODO: include package attr. */
    val = findVarInFrame(R_S4_extends_table, install(class));
    if(val != R_UnboundValue)
       return val;
    PROTECT(e = allocVector(LANGSXP, 2));
    SETCAR(e, s_extendsForS3);
    val = CDR(e);
    SETCAR(val, klass);
    PROTECT(val = eval(e, R_MethodsNamespace));
    cache_class(class, val);
    UNPROTECT(2); /* val, e */
    return(val);
}


/* -------------------------------------------------------------------------- */
/* PRE-ALLOCATED DEFAULT CLASS ATTRIBUTES.  Taken from R-3.2.0 (or later),    */
/*                                          modified a bit for pqR.           */

static struct {
    SEXP vector;
    SEXP matrix;
    SEXP array;
} Type2DefaultClass[MAX_NUM_SEXPTYPE];


static SEXP createDefaultClass(SEXP part1, SEXP part2, SEXP part3)
{
    int size = 0;
    if (part1 != R_NilValue) size++;
    if (part2 != R_NilValue) size++;
    if (part3 != R_NilValue) size++;

    if (size == 0 || part2 == R_NilValue) return R_NilValue;

    SEXP res = allocVector(STRSXP, size);
    R_PreserveObject(res);

    int i = 0;
    if (part1 != R_NilValue) SET_STRING_ELT(res, i++, part1);
    if (part2 != R_NilValue) SET_STRING_ELT(res, i++, part2);
    if (part3 != R_NilValue) SET_STRING_ELT(res, i, part3);

    SET_NAMEDCNT_MAX(res);
    return res;
}

attribute_hidden
void InitS3DefaultTypes()
{
    for(int type = 0; type < MAX_NUM_SEXPTYPE; type++) {

	SEXP part2 = R_NilValue;
	SEXP part3 = R_NilValue;
	int nprotected = 0;

	switch(type) {
	    case CLOSXP:
	    case SPECIALSXP:
	    case BUILTINSXP:
		part2 = PROTECT(mkChar("function"));
		nprotected++;
		break;
	    case INTSXP:
	    case REALSXP:
		part2 = PROTECT(type2str_nowarn(type));
		part3 = PROTECT(mkChar("numeric"));
		nprotected += 2;
		break;
	    case LANGSXP:
		/* part2 remains R_NilValue: default type cannot be
		   pre-allocated, as it depends on the object value */
		break;
	    case SYMSXP:
		part2 = PROTECT(mkChar("name"));
		nprotected++;
		break;
	    default:
		part2 = PROTECT(type2str_nowarn(type));
		nprotected++;
	}

	Type2DefaultClass[type].vector =
	    createDefaultClass(R_NilValue, part2, part3);

	SEXP part1;
	PROTECT(part1 = mkChar("matrix"));
	Type2DefaultClass[type].matrix =
	    createDefaultClass(part1, part2, part3);
	UNPROTECT(1);

	PROTECT(part1 = mkChar("array"));
	Type2DefaultClass[type].array =
	    createDefaultClass(part1, part2, part3);
	UNPROTECT(1);

	UNPROTECT(nprotected);
    }
}


/* Version for S3-dispatch.  Some parts adapted from R-3.2.0 (or later). */

SEXP attribute_hidden R_data_class2 (SEXP obj)
{
    SEXP klass = getClassAttrib(obj);

    if (klass != R_NilValue)
        return IS_S4_OBJECT(obj) ? S4_extends(klass) : klass;

    SEXPTYPE t = TYPEOF(obj);
    SEXP dim = getDimAttrib(obj);
    SEXP defaultClass;
    int n = 0;

    defaultClass = dim == R_NilValue      ? Type2DefaultClass[t].vector
                 : (n = LENGTH(dim)) == 2 ? Type2DefaultClass[t].matrix
                 :                          Type2DefaultClass[t].array;

    if (defaultClass == R_NilValue) {

        /* now t == LANGSXP, but check to make sure */
        if (t != LANGSXP)
            error("type must be LANGSXP at this point");

        if (n == 0)
            defaultClass = ScalarString (lang2str(obj,t));
        else {
            PROTECT (defaultClass = allocVector(STRSXP,2));
            SET_STRING_ELT (defaultClass, 0, 
                            n == 2 ? R_matrix_CHARSXP : R_array_CHARSXP);
            SET_STRING_ELT (defaultClass, 1, 
                            lang2str(obj, t));
            UNPROTECT(1);
        }
    }

    return defaultClass;
}

/* class() : */
SEXP attribute_hidden R_do_data_class(SEXP call, SEXP op, SEXP args, SEXP env)
{
  checkArity(op, args);
  if(PRIMVAL(op) == 1) {
      const char *class; SEXP klass;
      check1arg(args, call, "class");
      klass = CAR(args);
      if(TYPEOF(klass) != STRSXP || LENGTH(klass) < 1)
	  errorcall(call,"invalid class argument to internal .cache_class");
      class = translateChar(STRING_ELT(klass, 0));
      return cache_class(class, CADR(args));
  }
  check1arg_x (args, call);
  return R_data_class(CAR(args), FALSE);
}

/* names(obj) <- name.  SPECIAL, so can handle gradient for obj. */

static SEXP do_namesgets(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP ans;

    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "names<-", args, env, &ans, 0, 1, 0)) {
        UNPROTECT(1);
        return(ans);
    }

    SEXP obj = CAR(args);
    SEXP nms = CADR(args);

    /* Special case: removing non-existent names, to avoid a copy */

    if (nms == R_NilValue && getNamesAttrib(CAR(args)) == R_NilValue)
        goto ret;

    if (NAMEDCNT_GT_1(obj)) {
        obj = dup_top_level(obj);
        SETCAR (args, obj);
    }

    if (IS_S4_OBJECT(obj)) {
        const char *klass = CHAR(STRING_ELT(R_data_class(obj, FALSE), 0));
        if(getNamesAttrib(obj) == R_NilValue) {
            /* S4 class w/o a names slot or attribute */
            if(TYPEOF(obj) == S4SXP)
                errorcall(call,_("Class '%s' has no 'names' slot"), klass);
            else
                warningcall(call,
                  _("Class '%s' has no 'names' slot; assigning a names attribute will create an invalid object"), 
                  klass);
        }
        else if(TYPEOF(obj) == S4SXP)
            errorcall(call,
              _("Illegal to use names()<- to set the 'names' slot in a non-vector class ('%s')"),
              klass);
        /* else, go ahead, but can't check validity of replacement*/
    }

    if (nms != R_NilValue && (TYPEOF(nms) != STRSXP || HAS_ATTRIB(nms))) {
        static SEXP asc = R_NoObject;
        if (asc == R_NoObject) asc = install("as.character");
        SEXP prom = mkPROMISE (nms, R_EmptyEnv);
        SET_PRVALUE (prom, nms);
        SEXP cl = LCONS (asc, CONS(prom,R_NilValue));
        PROTECT(cl);
        nms = eval(cl, env);
        UNPROTECT(1);
    }

    PROTECT(nms);
    setAttrib(obj, R_NamesSymbol, nms);
    SET_NAMEDCNT_0(obj);  /* the standard kludge for subassign primitives */
    UNPROTECT(1);

  ret:

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return obj;
}

SEXP namesgets(SEXP vec, SEXP val)
{
    int i;
    SEXP s, rval, tval;

    PROTECT2(vec,val);

    /* Ensure that the labels are indeed */
    /* a vector of character strings */

    if (isList(val)) {
	if (!isVectorizable(val))
	    error(_("incompatible 'names' argument"));
	else {
	    rval = allocVector(STRSXP, length(vec));
	    PROTECT(rval);
	    /* See PR#10807 */
	    for (i = 0, tval = val;
		 i < length(vec) && tval != R_NilValue;
		 i++, tval = CDR(tval)) {
		s = coerceVector(CAR(tval), STRSXP);
		SET_STRING_ELT(rval, i, STRING_ELT(s, 0));
	    }
	    UNPROTECT(1);
	    val = rval;
	}
    } else val = coerceVector(val, STRSXP);
    UNPROTECT_PROTECT(val);

    /* Check that the lengths and types are compatible */

    if (length(val) < length(vec)) {
	val = lengthgets(val, length(vec));
	UNPROTECT_PROTECT(val);
    }

    checkNames(vec, val);

   /* Special treatment for one dimensional arrays */

    if (isVector(vec) || isList(vec) || isLanguage(vec)) {
	s = getDimAttrib(vec);
	if (TYPEOF(s) == INTSXP && length(s) == 1) {
	    PROTECT(val = CONS(val, R_NilValue));
	    setAttrib(vec, R_DimNamesSymbol, val);
	    UNPROTECT(3);
	    return vec;
	}
    }

    if (isList(vec) || isLanguage(vec)) {
	/* Cons-cell based objects */
	i = 0;
	for (s = vec; s != R_NilValue; s = CDR(s), i++)
	    if (STRING_ELT(val, i) != R_NilValue
		&& STRING_ELT(val, i) != R_NaString
		&& *CHAR(STRING_ELT(val, i)) != 0) /* test of length */
		SET_TAG(s, install_translated (STRING_ELT(val,i)));
	    else
		SET_TAG_NIL(s);
    }
    else if (isVector(vec) || IS_S4_OBJECT(vec))
	/* Normal case */
	installAttrib(vec, R_NamesSymbol, val);
    else
	error(_("invalid type (%s) to set 'names' attribute"),
	      type2char(TYPEOF(vec)));
    UNPROTECT(2);
    return vec;
}

static SEXP do_names(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP obj, nms, ans;
    checkArity(op, args);
    check1arg_x (args, call);
    if (DispatchOrEval(call, op, "names", args, env, &ans, 0, 1, variant))
	return ans;
    PROTECT(obj = CAR(ans));
    if (isVector(obj) || isList(obj) || isLanguage(obj) || IS_S4_OBJECT(obj))
	nms = getNamesAttrib(obj);
    else 
        nms = R_NilValue;
    UNPROTECT(1);

    /* See if names are an unshared subset of the object.  Not true if
       names are not from the "names" attribute, which is detected by 
       obj not being a vector (names created from tags in pairlist, for
       example) or by NAMEDCNT being greater than one (shared names, or
       names actually from dimnames). */

    if ((VARIANT_KIND(variant) == VARIANT_QUERY_UNSHARED_SUBSET 
           || VARIANT_KIND(variant) == VARIANT_FAST_SUB) 
         && !NAMEDCNT_GT_1(obj) && !NAMEDCNT_GT_1(nms)
         && isVector(obj))
        R_variant_result = 1;

    return nms;
}

/* Implements dimnames(obj) <- dn.  SPECIAL, so can handle gradient for obj. */

static SEXP do_dimnamesgets
                 (SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP ans;

    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "dimnames<-", args, env, &ans, 0, 1, 0)) {
        UNPROTECT(1);
	return(ans);
    }

    if (NAMEDCNT_GT_1(CAR(args))) 
        SETCAR(args, dup_top_level(CAR(args)));

    setAttrib(CAR(args), R_DimNamesSymbol, CADR(args));

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return CAR(args);
}

static SEXP dimnamesgets1(SEXP val1)
{
    SEXP this2;

    if (LENGTH(val1) == 0) return R_NilValue;
    /* if (isObject(val1)) dispatch on as.character.foo, but we don't
       have the context at this point to do so */

    if (inherits_CHAR (val1, R_factor_CHARSXP))  /* mimic as.character.factor */
        return asCharacterFactor(val1);

    if (!isString(val1)) { /* mimic as.character.default */
	PROTECT(this2 = coerceVector(val1, STRSXP));
	SET_ATTRIB(this2, R_NilValue);
	SET_OBJECT(this2, 0);
	UNPROTECT(1);
	return this2;
    }
    return val1;
}


SEXP dimnamesgets(SEXP vec, SEXP val)
{
    SEXP dims, top, newval;
    int i, k;

    PROTECT2(vec,val);

    if (!isArray(vec) && !isList(vec))
	error(_("'dimnames' applied to non-array"));
    /* This is probably overkill, but you never know; */
    /* there may be old pair-lists out there */
    /* There are, when this gets used as names<- for 1-d arrays */
    if (!isPairList(val) && !isNewList(val))
	error(_("'dimnames' must be a list"));
    dims = getDimAttrib(vec);
    if ((k = LENGTH(dims)) < length(val))
	error(_("length of 'dimnames' [%d] must match that of 'dims' [%d]"),
	      length(val), k);
    if (length(val) == 0) {
	removeAttrib(vec, R_DimNamesSymbol);
	UNPROTECT(2);
	return vec;
    }
    /* Old list to new list */
    if (isList(val)) {
	newval = allocVector(VECSXP, k);
	for (i = 0; i < k; i++) {
	    SET_VECTOR_ELT(newval, i, CAR(val));
	    val = CDR(val);
	}
	UNPROTECT_PROTECT(val = newval);
    }
    if (length(val) > 0 && length(val) < k) {
	newval = lengthgets(val, k);
	UNPROTECT_PROTECT(val = newval);
    }
    if (k != length(val))
	error(_("length of 'dimnames' [%d] must match that of 'dims' [%d]"),
	      length(val), k);
    for (i = 0; i < k; i++) {
	SEXP _this = VECTOR_ELT(val, i);
	if (_this != R_NilValue) {
	    if (!isVector(_this))
		error(_("invalid type (%s) for 'dimnames' (must be a vector)"),
		      type2char(TYPEOF(_this)));
	    if (INTEGER(dims)[i] != LENGTH(_this) && LENGTH(_this) != 0)
		error(_("length of 'dimnames' [%d] not equal to array extent"),
		      i+1);
	    SET_VECTOR_ELT(val, i, dimnamesgets1(_this));
	}
    }
    installAttrib(vec, R_DimNamesSymbol, val);
    if (isList(vec) && k == 1) {
	top = VECTOR_ELT(val, 0);
	i = 0;
	for (val = vec; !isNull(val); val = CDR(val))
	    SET_TAG(val, install_translated (STRING_ELT(top,i++)));
    }
    UNPROTECT(2);
    return vec;
}

static SEXP do_dimnames(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP ans;
    checkArity(op, args);
    check1arg_x (args, call);
    if (DispatchOrEval(call, op, "dimnames", args, env, &ans, 0, 1, 0))
	return(ans);
    PROTECT(args = ans);
    ans = getAttrib(CAR(args), R_DimNamesSymbol);
    UNPROTECT(1);
    return ans;
}

static SEXP do_fast_dim (SEXP call, SEXP op, SEXP arg, SEXP env, int variant)
{
    return getDimAttrib(arg);
}

static SEXP do_dim(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP ans;

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "dim", args, env, &ans, 0, 1, variant))
	return(ans);

    return do_fast_dim (call, op, CAR(args), env, variant);
}

/* Implements dim(obj) <- dims.  SPECIAL, so can handle gradient for obj. */

static SEXP do_dimgets(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP ans, x;

    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    checkArity(op, args);

    if (DispatchOrEval(call, op, "dim<-", args, env, &ans, 0, 1, 0)) {
        UNPROTECT(1);
	return ans;
    }

    x = CAR(args);

    /* Avoid duplication if setting to NULL when already NULL */
    if (CADR(args) == R_NilValue) {
	SEXP s;
	for (s = ATTRIB(x); s != R_NilValue; s = CDR(s))
	    if (TAG(s) == R_DimSymbol || TAG(s) == R_NamesSymbol) break;
	if (s == R_NilValue)
            goto ret;
    }

    if (NAMEDCNT_GT_1(x)) 
        SETCAR(args, x = dup_top_level(x));

    setAttrib(x, R_DimSymbol, CADR(args));
    setAttrib(x, R_NamesSymbol, R_NilValue);

  ret:

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return x;
}

SEXP dimgets(SEXP vec, SEXP val)
{
    int len, ndim, i, total;
    PROTECT2(vec,val);
    if ((!isVector(vec) && !isList(vec)))
	error(_("invalid first argument"));

    if (!isVector(val) && !isList(val))
	error(_("invalid second argument"));
    val = coerceVector(val, INTSXP);
    UNPROTECT_PROTECT(val);

    len = length(vec);
    ndim = length(val);
    if (ndim == 0)
	error(_("length-0 dimension vector is invalid"));
    total = 1;
    for (i = 0; i < ndim; i++) {
	/* need this test first as NA_INTEGER is < 0 */
	if (INTEGER(val)[i] == NA_INTEGER)
	    error(_("the dims contain missing values"));
	if (INTEGER(val)[i] < 0)
	    error(_("the dims contain negative values"));
	total *= INTEGER(val)[i];
    }
    if (total != len)
	error(_("dims [product %d] do not match the length of object [%d]"), total, len);
    removeAttrib(vec, R_DimNamesSymbol);
    installAttrib(vec, R_DimSymbol, val);
    UNPROTECT(2);
    return vec;
}

static SEXP do_attributes(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP attrs, names, namesattr, value;
    int nvalues;

    checkArity(op, args);
    check1arg_x (args, call);
    namesattr = R_NilValue;
    attrs = ATTRIB(CAR(args));
    nvalues = length(attrs);
    if (isList(CAR(args))) {
	namesattr = getNamesAttrib(CAR(args));
	if (namesattr != R_NilValue) {
            SET_NAMEDCNT_MAX(namesattr);
	    nvalues++;
        }
    }
    /* FIXME */
    if (nvalues <= 0)
	return R_NilValue;
    /* FIXME */
    PROTECT(namesattr);
    PROTECT(value = allocVector(VECSXP, nvalues));
    PROTECT(names = allocVector(STRSXP, nvalues));
    nvalues = 0;
    if (namesattr != R_NilValue) {
	SET_VECTOR_ELT(value, nvalues, namesattr);
        INC_NAMEDCNT(namesattr);
	SET_STRING_ELT(names, nvalues, PRINTNAME(R_NamesSymbol));
	nvalues++;
    }
    while (attrs != R_NilValue) {
	/* treat R_RowNamesSymbol specially */
	if (TAG(attrs) == R_RowNamesSymbol) {
            SEXP rn = getAttrib (CAR(args), R_RowNamesSymbol);
	    SET_VECTOR_ELT(value, nvalues, rn);
            INC_NAMEDCNT(rn);
        }
	else {
	    SET_VECTOR_ELT (value, nvalues, CAR(attrs));
            INC_NAMEDCNT(CAR(attrs));
        }
	if (TAG(attrs) == R_NilValue)
	    SET_STRING_ELT_BLANK(names, nvalues);
	else
	    SET_STRING_ELT(names, nvalues, PRINTNAME(TAG(attrs)));
	attrs = CDR(attrs);
	nvalues++;
    }
    setAttrib(value, R_NamesSymbol, names);
    UNPROTECT(3);
    return value;
}

static SEXP do_levelsgets(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP ans;

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "levels<-", args, env, &ans, 0, 1, 0))
	/* calls, e.g., levels<-.factor() */
	return(ans);

    PROTECT(args = ans);
    if(!isNull(CADR(args)) && any_duplicated(CADR(args), FALSE))
	warningcall(call, 
          _("duplicated levels will not be allowed in factors anymore"));
         /* TODO errorcall(call, 
             _("duplicated levels are not allowed in factors anymore")); */
    if (NAMEDCNT_GT_1(CAR(args))) 
        SETCAR(args, dup_top_level(CAR(args)));
    setAttrib(CAR(args), R_LevelsSymbol, CADR(args));
    UNPROTECT(1);
    return CAR(args);
}

/* attributes(object) <- attrs. SPECIAL so it can handle gradients for object */

static SEXP do_attributesgets (SEXP call, SEXP op, SEXP args, SEXP env,
                               int variant)
{
    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    /* NOTE: The following code ensures that when an attribute list 
       is attached to an object, that the "dim" attibute is always 
       brought to the front of the list.  This ensures that when both 
       "dim" and "dimnames" are set that the "dim" is attached first. */

    SEXP object, attrs, names = R_NilValue /* -Wall */;
    int i, i0 = -1, nattrs;

    /* Extract the arguments from the argument list */

    checkArity(op, args);
    check1arg_x (args, call);

    object = CAR(args);
    attrs = CADR(args);

    /* Do checks before duplication */
    if (!isNewList(attrs))
        errorcall(call,_("attributes must be a list or NULL"));
    nattrs = length(attrs);
    if (nattrs > 0) {
        names = getNamesAttrib(attrs);
        if (names == R_NilValue)
            errorcall(call,_("attributes must be named"));
        for (i = 1; i < nattrs; i++) {
            if (STRING_ELT(names, i) == R_NilValue ||
                CHAR(STRING_ELT(names, i))[0] == '\0') { /* all ASCII tests */
                errorcall(call,_("all attributes must have names [%d does not]"), i+1);
            }
        }
    }

    if (object == R_NilValue) {
        if (attrs == R_NilValue) {
            UNPROTECT(1);
            return R_NilValue;
        }
        else
            PROTECT(object = allocVector(VECSXP, 0));
    } else {
        /* Unlikely to have NAMED == 0 here.
           As from R 2.7.0 we don't optimize NAMED == 1 _if_ we are
           setting any attributes as an error later on would leave
           'obj' changed */
        if (NAMEDCNT_GT_1(object) || (NAMEDCNT_GT_0(object) && nattrs > 0))
            object = dup_top_level(object);
        PROTECT(object);
    }


    /* Empty the existing attribute list */

    if (isList(object))
        setAttrib(object, R_NamesSymbol, R_NilValue);
    SET_ATTRIB(object, R_NilValue);
    /* We have just removed the class, but might reset it later */
    SET_OBJECT(object, 0);
    /* Probably need to fix up S4 bit in other cases, but
       definitely in this one */
    if(nattrs == 0) UNSET_S4_OBJECT(object);

    /* We do two passes through the attributes; the first 
       finding and transferring "dim" and the second 
       transferring the rest.  This is to ensure that 
       "dim" occurs in the attribute list before "dimnames". */

    if (nattrs > 0) {
        for (i = 0; i < nattrs; i++) {
            if (!strcmp(CHAR(STRING_ELT(names, i)), "dim")) {
                i0 = i;
                setAttrib(object, R_DimSymbol, VECTOR_ELT(attrs, i));
                break;
            }
        }
        for (i = 0; i < nattrs; i++) {
            if (i == i0) continue;
            setAttrib(object, install_translated (STRING_ELT(names,i)),
                      VECTOR_ELT(attrs, i));
        }
    }

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(2);
    return object;
}

/*  This code replaces an R function defined as

    attr <- function (x, which)
    {
        if (!is.character(which))
            stop("attribute name must be of mode character")
        if (length(which) != 1)
            stop("exactly one attribute name must be given")
        attributes(x)[[which]]
    }

    The R functions was being called very often and replacing it by
    something more efficient made a noticeable difference on several
    benchmarks.  There is still some inefficiency since using getAttrib
    means the attributes list will be searched twice, but this seems
    fairly minor.  LT */

static SEXP do_attr(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP argList, s, t, tag = R_NilValue, alist, ans;
    const char *str;
    size_t n;
    int nargs = length(args), exact = 0;
    enum { NONE, PARTIAL, PARTIAL2, FULL } match = NONE;
    static const char * const ap[3] = { "x", "which", "exact" };

    if (nargs < 2 || nargs > 3)
	errorcall(call, "either 2 or 3 arguments are required");

    /* argument matching */
    argList = matchArgs_strings (ap, 3, args, call);

    PROTECT(argList);
    s = CAR(argList);
    t = CADR(argList);
    if (!isString(t))
	errorcall(call, _("'which' must be of mode character"));
    if (length(t) != 1)
	errorcall(call, _("exactly one attribute 'which' must be given"));

    if(nargs == 3) {
	exact = asLogical(CADDR(args));
	if(exact == NA_LOGICAL) exact = 0;
    }

    if(STRING_ELT(t, 0) == NA_STRING) {
	UNPROTECT(1);
	return R_NilValue;
    }
    str = translateChar(STRING_ELT(t, 0));
    n = strlen(str);

    /* try to find a match among the attributes list */
    for (alist = ATTRIB(s); alist != R_NilValue; alist = CDR(alist)) {
	SEXP tmp = TAG(alist);
	const char *s = CHAR(PRINTNAME(tmp));
	if (! strncmp(s, str, n)) {
	    if (strlen(s) == n) {
		tag = tmp;
		match = FULL;
		break;
	    }
            else if (match == PARTIAL || match == PARTIAL2) {
		/* this match is partial and we already have a partial match,
		   so the query is ambiguous and we will return R_NilValue
		   unless a full match comes up.
		*/
		match = PARTIAL2;
	    } else {
		tag = tmp;
		match = PARTIAL;
	    }
	}
    }
    if (match == PARTIAL2) {
	UNPROTECT(1);
	return R_NilValue;
    }

    /* Unless a full match has been found, check for a "names" attribute.
       This is stored via TAGs on pairlists, and via rownames on 1D arrays.
    */
    if (match != FULL && strncmp("names", str, n) == 0) {
	if (strlen("names") == n) {
	    /* we have a full match on "names", if there is such an
	       attribute */
	    tag = R_NamesSymbol;
	    match = FULL;
	}
	else if (match == NONE && !exact) {
	    /* no match on other attributes and a possible
	       partial match on "names" */
	    tag = R_NamesSymbol;
	    PROTECT(t = getAttrib(s, tag));
	    if(t != R_NilValue && R_warn_partial_match_attr)
		warningcall(call, _("partial match of '%s' to '%s'"), str,
			    CHAR(PRINTNAME(tag)));
	    UNPROTECT(2);
	    return t;
	}
	else if (match == PARTIAL && strcmp(CHAR(PRINTNAME(tag)), "names")) {
	    /* There is a possible partial match on "names" and on another
	       attribute. If there really is a "names" attribute, then the
	       query is ambiguous and we return R_NilValue.  If there is no
	       "names" attribute, then the partially matched one, which is
	       the current value of tag, can be used. */
	    if (getNamesAttrib(s) != R_NilValue) {
		UNPROTECT(1);
		return R_NilValue;
	    }
	}
    }

    if (match == NONE  || (exact && match != FULL)) {
	UNPROTECT(1);
	return R_NilValue;
    }
    if (match == PARTIAL && R_warn_partial_match_attr)
	warningcall(call, _("partial match of '%s' to '%s'"), str,
		    CHAR(PRINTNAME(tag)));

    ans =  getAttrib(s, tag);
    UNPROTECT(1);
    return ans;
}

static void check_slot_assign(SEXP obj, SEXP input, SEXP value, SEXP env)
{
    SEXP
	valueClass = PROTECT(R_data_class(value, FALSE)),
	objClass   = PROTECT(R_data_class(obj, FALSE));
    static SEXP checkAt = R_NoObject;
    /* 'methods' may *not* be in search() ==> do as if calling 
        methods::checkAtAssignment(..) */
    if(!isMethodsDispatchOn()) { // needed?
	SEXP e = PROTECT(lang1(install("initMethodDispatch")));
	eval(e, R_MethodsNamespace); // only works with methods loaded
	UNPROTECT(1);
    }
    if(checkAt == R_NoObject)
	checkAt = findFun(install("checkAtAssignment"), R_MethodsNamespace);
    SEXP e = PROTECT(lang4(checkAt, objClass, input, valueClass));
    eval(e, env);
    UNPROTECT(3);
}

/* Implements   attr(obj, which = "<name>")  <-  value.  
   SPECIAL, so can handle gradient for obj. */

static SEXP do_attrgets(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP obj, name;

    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    checkArity(op, args);

    obj = CAR(args);
    if (NAMEDCNT_GT_1(obj))
        PROTECT(obj = dup_top_level(obj));
    else
        PROTECT(obj);

    /* Argument matching - does not actually make much sense to do this! */
    static const char * const ap[3] = { "x", "which", "value" };
    SEXP argList = matchArgs_strings (ap, 3, args, call);

    PROTECT(argList);

    name = CADR(argList);
    if (!isValidString(name) || STRING_ELT(name, 0) == NA_STRING)
        errorcall(call,_("'name' must be non-null character string"));
    /* TODO?  if (isFactor(obj) && !strcmp(asChar(name), "levels"))
     * ---         if(any_duplicated(CADDR(args)))
     *                  error(.....)
     */
    setAttrib(obj, name, CADDR(args));

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(3);
    return obj;
}

/* Implements   obj @ <name>  <-  value   (SPECIAL) 

   Code adapted from R-3.0.0 */

static SEXP do_ATgets(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP obj, name, input, ans, value;
    PROTECT(input = allocVector(STRSXP, 1));

    name = CADR(args);
    if (TYPEOF(name) == PROMSXP)
        name = PRCODE(name);
    if (isSymbol(name))
        SET_STRING_ELT(input, 0, PRINTNAME(name));
    else if(isString(name) )
        SET_STRING_ELT(input, 0, STRING_ELT(name, 0));
    else {
        error(_("invalid type '%s' for slot name"), type2char(TYPEOF(name)));
        return R_NilValue; /*-Wall*/
    }

    /* replace the second argument with a string */
    SETCADR(args, input);
    UNPROTECT(1);  /* 'input' is now protected */

    if (DispatchOrEval(call, op, "@<-", args, env, &ans, 0, 0, variant))
        return(ans);

    PROTECT(obj = CAR(ans));
    PROTECT(value = CADDR(ans));
    check_slot_assign(obj, input, value, env);
    obj = R_do_slot_assign(obj, input, value);

    SET_NAMEDCNT_0(obj);  /* The standard kludge for subassign primitives */
    R_Visible = TRUE;
    UNPROTECT(2);
    return obj;
}

/* Implements   `@internal`(obj,name)        <-  value 
                ** for internal use only, no validity check **
 */

static SEXP do_ATinternalgets(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP obj, name, input, value;

    PROTECT(obj = CAR(args));
    PROTECT(value = CADDR(args));
    PROTECT(input = allocVector(STRSXP, 1));

    name = CADR(args);
    if (TYPEOF(name) == PROMSXP)
        name = PRCODE(name);
    if (isSymbol(name))
        SET_STRING_ELT(input, 0, PRINTNAME(name));
    else if(isString(name) )
        SET_STRING_ELT(input, 0, STRING_ELT(name, 0));
    else {
        error(_("invalid type '%s' for slot name"),
              type2char(TYPEOF(name)));
        return R_NilValue; /*-Wall*/
    }

    obj = R_do_slot_assign(obj, input, value);

    SET_NAMEDCNT_0(obj);  /* The standard kludge for subassign primitives */
    UNPROTECT(3);
    return obj;
}


/* These provide useful shortcuts which give access to */
/* the dimnames for matrices and arrays in a standard form. */

void GetMatrixDimnames(SEXP x, SEXP *rl, SEXP *cl,
        	       const char **rn, const char **cn)
{
    SEXP dimnames = getAttrib(x, R_DimNamesSymbol);
    SEXP nn;

    if (isNull(dimnames)) {
	*rl = R_NilValue;
	*cl = R_NilValue;
	*rn = NULL;
	*cn = NULL;
    }
    else {
	*rl = VECTOR_ELT(dimnames, 0);
	*cl = VECTOR_ELT(dimnames, 1);
	nn = getNamesAttrib(dimnames);
	if (isNull(nn)) {
	    *rn = NULL;
	    *cn = NULL;
	}
	else {
	    *rn = translateChar(STRING_ELT(nn, 0));
	    *cn = translateChar(STRING_ELT(nn, 1));
	}
    }
}


SEXP GetArrayDimnames(SEXP x)
{
    return getAttrib(x, R_DimNamesSymbol);
}


/* the code to manage slots in formal classes. These are attributes,
   but without partial matching and enforcing legal slot names (it's
   an error to get a slot that doesn't exist. */


static SEXP pseudo_NULL = 0;

static SEXP s_dot_Data;
static SEXP s_getDataPart;
static SEXP s_setDataPart;

static void init_slot_handling(void) {
    s_dot_Data = install(".Data");
    s_dot_S3Class = install(".S3Class");
    s_getDataPart = install("getDataPart");
    s_setDataPart = install("setDataPart");
    /* create and preserve an object that is NOT R_NilValue, and is used
       to represent slots that are NULL (which an attribute can not
       be).  The point is not just to store NULL as a slot, but also to
       provide a check on invalid slot names (see get_slot below).

       The object has to be a symbol if we're going to check identity by
       just looking at referential equality. */
    pseudo_NULL = install("\001NULL\001");
}

static SEXP data_part(SEXP obj) {
    SEXP e, val;
    if(!s_getDataPart)
	init_slot_handling();
    PROTECT(e = allocVector(LANGSXP, 2));
    SETCAR(e, s_getDataPart);
    val = CDR(e);
    SETCAR(val, obj);
    val = eval(e, R_MethodsNamespace);
    UNSET_S4_OBJECT(val); /* data part must be base vector */
    UNPROTECT(1);
    return(val);
}

static SEXP set_data_part(SEXP obj,  SEXP rhs) {
    SEXP e, val;
    if(!s_setDataPart)
	init_slot_handling();
    PROTECT(e = allocVector(LANGSXP, 3));
    SETCAR(e, s_setDataPart);
    val = CDR(e);
    SETCAR(val, obj);
    val = CDR(val);
    SETCAR(val, rhs);
    val = eval(e, R_MethodsNamespace);
    SET_S4_OBJECT(val);
    UNPROTECT(1);
    return(val);
}

SEXP S3Class(SEXP obj)
{
    if(!s_dot_S3Class) init_slot_handling();
    return getAttrib(obj, s_dot_S3Class);
}

/* Slots are stored as attributes to
   provide some back-compatibility
*/

/**
 * R_has_slot() : a C-level test if a obj@<name> is available;
 *                as R_do_slot() gives an error when there's no such slot.
 */
int R_has_slot(SEXP obj, SEXP name) {

#define R_SLOT_INIT							\
    if(!(isSymbol(name) || (isString(name) && LENGTH(name) == 1)))	\
	error(_("invalid type or length for slot name"));		\
    if(!s_dot_Data)							\
	init_slot_handling();						\
    if(isString(name)) name = installChar(STRING_ELT(name, 0))

    R_SLOT_INIT;
    if(name == s_dot_Data && TYPEOF(obj) != S4SXP)
	return(1);
    /* else */
    return(getAttrib(obj, name) != R_NilValue);
}


/* The @ operator, and its assignment form.  Processed much like $
   (see do_subset3) but without S3-style methods.
*/

/* Slot getting for the 'methods' package.  In pqR, the 'method'
   package has been changed to call R_do_slot via .Internal, in order
   to bypass the safeguarding of .Call argument modification that is
   now done (to avoid this overhead), and because it also makes more
   sense given the dependence of 'methods' on the interpreter.

   R_do_slot can be called with .Call, which is done by the 'Matrix'
   package via GET_SLOT (defined in Rdefines.h, but not mentioned in
   the manuals). */

static SEXP do_get_slot (SEXP call, SEXP op, SEXP args, SEXP env)
{
   checkArity (op, args);

   return R_do_slot (CAR(args), CADR(args));
}

SEXP R_do_slot(SEXP obj, SEXP name) {
    R_SLOT_INIT;
    if(name == s_dot_Data)
	return data_part(obj);
    else {
	SEXP value = getAttrib(obj, name);
	if(value == R_NilValue) {
	    SEXP input = name, classString;
	    if(name == s_dot_S3Class) /* defaults to class(obj) */
	        return R_data_class(obj, FALSE);
	    else if(name == R_NamesSymbol &&
		    TYPEOF(obj) == VECSXP) /* needed for namedList class */
	        return value;
	    if(isSymbol(name) ) {
		PROTECT(input = ScalarString(PRINTNAME(name)));
		classString = getClassAttrib(obj);
		if(isNull(classString))
		    error(_("cannot get a slot (\"%s\") from an object of type \"%s\""),
			  translateChar(asChar(input)),
			  CHAR(type2str(TYPEOF(obj))));
	        UNPROTECT(1);
	    }
	    else 
                classString = R_NilValue; /* make sure it is initialized */

	    /* not there.  But since even NULL really does get stored, this
	       implies that there is no slot of this name.  Or somebody
	       screwed up by using attr(..) <- NULL */

	    error(_("no slot of name \"%s\" for this object of class \"%s\""),
		  translateChar(asChar(input)),
		  translateChar(asChar(classString)));
	}
	else if(value == pseudo_NULL)
	    value = R_NilValue;
	return value;
    }
}
#undef R_SLOT_INIT


/* Slot setting for the 'methods' package.  In pqR, the 'method'
   package has been changed to call do_set_slot via .Internal, in
   order to bypass the safeguarding of .Call argument modification
   that is now done (to avoid this overhead), and because it also
   makes more sense given the dependence of 'methods' on the interpreter.

   R_do_slot_assign can be called with .Call, which is done by the
   'Matrix' package, often via SET_SLOT (defined in Rdefines.h, but
   not mentioned in the manuals). */

static SEXP do_set_slot (SEXP call, SEXP op, SEXP args, SEXP env)
{
   checkArity (op, args);

   return R_do_slot_assign (CAR(args), CADR(args), CADDR(args));
}

SEXP R_do_slot_assign(SEXP obj, SEXP name, SEXP value)
{
    if (isNull(obj)) /* cannot use !IS_S4_OBJECT(obj), because
                           slot(obj, name, check=FALSE) <- value  
                       must work on "pre-objects", currently only in 
                       makePrototypeFromClassDef() */

	error(_("attempt to set slot on NULL object"));

    PROTECT(value);
    PROTECT(obj);

    /* Ensure that name is a symbol */
    if(isString(name) && LENGTH(name) == 1)
	name = install_translated (STRING_ELT(name,0));
    if(TYPEOF(name) == CHARSXP)
	name = install_translated (name);
    if(!isSymbol(name) )
	error(_("invalid type or length for slot name"));

    /* The duplication below *should* be what is right to do, but
       it is disabled because some packages (eg, Matrix 1.0-6) cheat 
       by relying on a call of R_set_slot in C code modifying its 
       first argument in place, even if it appears to be shared.  
       Package "methods" did this as well in R-2.15.1, but has been 
       modified not to in pqR. */

    if (FALSE && /* disabled */ NAMEDCNT_GT_1(obj)) {
        obj = dup_top_level(obj);
        UNPROTECT_PROTECT(obj);
    }
        
    if(!s_dot_Data)		/* initialize */
	init_slot_handling();

    if(name == s_dot_Data) {	/* special handling */
	obj = set_data_part(obj, value);
    } else {

	if (isNull(value))	  /* Slots, but not attributes, can be NULL.*/
	    value = pseudo_NULL;  /* Store a special symbol instead. */

	/* simplified version of setAttrib(obj, name, value);
	   here we do *not* treat "names", "dimnames", "dim", .. specially : */

	PROTECT(name);
        value = attr_val_dup (obj, name, value);
	UNPROTECT(1);
	installAttrib(obj, name, value);
    }

    UNPROTECT(2);
    return obj;
}

static SEXP do_AT(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP  nlist, object, ans, klass;

    if(!isMethodsDispatchOn())
	error(_("formal classes cannot be used without the methods package"));
    nlist = CADR(args);
    if (TYPEOF(nlist) == PROMSXP)
        nlist = PRCODE(nlist);
    /* Do some checks here -- repeated in R_do_slot, but on repeat the
     * test expression should kick out on the first element. */
    if(!(isSymbol(nlist) || (isString(nlist) && LENGTH(nlist) == 1)))
	error(_("invalid type or length for slot name"));
    if(isString(nlist)) nlist = install_translated (STRING_ELT(nlist,0));
    PROTECT(object = eval(CAR(args), env));
    if(!s_dot_Data) init_slot_handling();
    if(nlist != s_dot_Data && !IS_S4_OBJECT(object)) {
	klass = getClassAttrib(object);
	if(length(klass) == 0)
	    error(_("trying to get slot \"%s\" from an object of a basic class (\"%s\") with no slots"),
		  CHAR(PRINTNAME(nlist)),
		  CHAR(STRING_ELT(R_data_class(object, FALSE), 0)));
	else
	    error(_("trying to get slot \"%s\" from an object (class \"%s\") that is not an S4 object "),
		  CHAR(PRINTNAME(nlist)),
		  translateChar(STRING_ELT(klass, 0)));
    }

    ans = R_do_slot(object, nlist);
    UNPROTECT(1);
    R_Visible = TRUE;
    return ans;
}

/* Return a suitable S3 object (OK, the name of the routine comes from
   an earlier version and isn't quite accurate.) If there is a .S3Class
   slot convert to that S3 class.
   Otherwise, unless type == S4SXP, look for a .Data or .xData slot.  The
   value of type controls what's wanted.  If it is S4SXP, then ONLY
   .S3class is used.  If it is ANYSXP, don't check except that automatic
   conversion from the current type only applies for classes that extend
   one of the basic types (i.e., not S4SXP).  For all other types, the
   recovered data must match the type.
   Because S3 objects can't have type S4SXP, .S3Class slot is not searched
   for in that type object, unless ONLY that class is wanted.
   (Obviously, this is another routine that has accumulated barnacles and
   should at some time be broken into separate parts.)
*/
SEXP attribute_hidden
R_getS4DataSlot(SEXP obj, SEXPTYPE type)
{
  static SEXP s_xData, s_dotData; SEXP value = R_NilValue;
  if(!s_xData) {
    s_xData = install(".xData");
    s_dotData = install(".Data");
  }
  if(TYPEOF(obj) != S4SXP || type == S4SXP) {
    SEXP s3class = S3Class(obj);
    if(s3class == R_NilValue && type == S4SXP)
      return R_NilValue;
    PROTECT(s3class);
    if(NAMEDCNT_GT_0(obj)) obj = duplicate(obj);
    UNPROTECT(1);
    if(s3class != R_NilValue) {/* replace class with S3 class */
      setAttrib(obj, R_ClassSymbol, s3class);
      setAttrib(obj, s_dot_S3Class, R_NilValue); /* not in the S3 class */
    }
    else { /* to avoid inf. recursion, must unset class attribute */
      setAttrib(obj, R_ClassSymbol, R_NilValue);
    }
    UNSET_S4_OBJECT(obj);
    if(type == S4SXP)
      return obj;
    value = obj;
  }
  else
      value = getAttrib(obj, s_dotData);
  if(value == R_NilValue)
      value = getAttrib(obj, s_xData);
/* the mechanism for extending abnormal types.  In the future, would b
   good to consolidate under the ".Data" slot, but this has
   been used to mean S4 objects with non-S4 type, so for now
   a secondary slot name, ".xData" is used to avoid confusion
*/  if(value != R_NilValue &&
     (type == ANYSXP || type == TYPEOF(value)))
     return value;
  else
     return R_NilValue;
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_attrib[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"class",	R_do_data_class,0,	1,	1,	{PP_FUNCALL, PREC_FN,	0}},
{".cache_class",R_do_data_class,1,	1,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"comment",	do_comment,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"comment<-",	do_commentgets,	0,	11,	2,	{PP_FUNCALL, PREC_LEFT,	1}},
{"oldClass",	do_oldclass,	0,	1,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"oldClass<-",	do_oldclassgets,0,	1000,	2,	{PP_FUNCALL, PREC_LEFT, 1}},
{"names",	do_names,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"names<-",	do_namesgets,	0,	1000,	2,	{PP_FUNCALL, PREC_LEFT,	1}},
{"dimnames",	do_dimnames,	0,	1,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"dimnames<-",	do_dimnamesgets,0,	1000,	2,	{PP_FUNCALL, PREC_LEFT,	1}},
{"dim",		do_dim,		0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"dim<-",	do_dimgets,	0,	1000,	2,	{PP_FUNCALL, PREC_LEFT,	1}},
{"attributes",	do_attributes,	0,	1,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"attributes<-",do_attributesgets,0,	1000,	2,	{PP_FUNCALL, PREC_LEFT,	1}},
{"attr",	do_attr,	0,	1,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"attr<-",	do_attrgets,	0,	1000,	3,	{PP_FUNCALL, PREC_LEFT,	1}},
{"levels<-",	do_levelsgets,	0,	1,	2,	{PP_FUNCALL, PREC_LEFT,	1}},

{"@",		do_AT,		0,	1000,	2,	{PP_DOLLAR,  PREC_DOLLAR, 0}},
{"@<-",		do_ATgets,	0,	1000,	3,	{PP_SUBASS,  PREC_LEFT,	  1}},
{"@internal<-",	do_ATinternalgets, 0,	1,	3,	{PP_SUBASS,  PREC_LEFT,	  1}},

{"set_slot.internal", do_set_slot, 0,	11,	3,	{PP_FUNCALL, PREC_FN,	}},
{"get_slot.internal", do_get_slot, 0,	11,	2,	{PP_FUNCALL, PREC_FN,	}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};

/* Fast built-in functions in this file. See names.c for documentation */

attribute_hidden FASTFUNTAB R_FastFunTab_attrib[] = {
/*slow func	fast func,     code or -1   dsptch  variant */
{ do_dim,	do_fast_dim,	-1,		1,  VARIANT_PENDING_OK },
{ 0,		0,		0,		0,  0 }
};
