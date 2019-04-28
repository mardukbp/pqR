/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2017, 2018 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1999-2007   The R Core Team.
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
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  http://www.r-project.org/Licenses/
 */

/* this header is always to be included from others.
   It is only called if COMPILING_R is defined (in util.c) or
   from GNU C systems.

   There are different conventions for inlining across compilation units.
   See http://www.greenend.org.uk/rjk/2003/03/inline.html
 */
#ifndef R_INLINES_H_
#define R_INLINES_H_

#ifdef COMPILING_R  /* only in inlined.c, to get externally linkable code */
#   define INLINE_FUN
#else
#   define INLINE_FUN static inline
#endif

#include <string.h> /* for strlen, strcmp */
#include <complex.h>


/* Keep myfmod and myfloor in step. */

static inline double myfmod(double x1, double x2)
{
#if 0  /* seems to be slower */
    int i1 = (int) x1;
    if ((double)i1 == x1 && i1 >= 0) {
        int i2 = (int) x2;
        if ((double)i2 == x2 && i2 > 0)
            return (double) (i1 % i2);
    }
#endif

    if (x2 == 0.0) return R_NaN;

    double q = x1 / x2;
    double f = floor(q);
    double tmp = x1 - f * x2;
    return tmp - floor(tmp/x2) * x2;
}

static inline double myfloor(double x1, double x2)
{
#if 0  /* seems to be slower */
    int i1 = (int) x1;
    if ((double)i1 == x1 && i1 >= 0) {
        int i2 = (int) x2;
        if ((double)i2 == x2 && i2 > 0)
            return (double) (i1 / i2);
    }
#endif

    if (x2 == 0.0) return R_NaN;

    double q = x1 / x2;
    double f = floor(q);
    double tmp = x1 - f * x2;
    return f + floor(tmp/x2);
}


/* ATTRIBUTE FETCHING */

/* The 00 version of getAttrib can be called when it is known that "name"
   is a symbol (not a string) and is not one that is handled specially. */

static inline SEXP getAttrib00 (SEXP vec, SEXP name)
{
    SEXP s;
    for (s = ATTRIB(vec); s != R_NilValue; s = CDR(s)) {
	if (TAG(s) == name) {
	    SET_NAMEDCNT_MAX(CAR(s));
	    return CAR(s);
	}
    }
    return R_NilValue;
}

INLINE_FUN SEXP getDimAttrib (SEXP vec)
{
    return getAttrib00 (vec, R_DimSymbol);
}

INLINE_FUN SEXP getClassAttrib (SEXP vec)
{
    return getAttrib00 (vec, R_ClassSymbol);
}


/* from list.c */

/* Get the i-th element of a list */
INLINE_FUN SEXP elt(SEXP list, int i)
{
    int j;
    SEXP result = list;

    if ((i < 0) || (i > length(list)))
	return R_NilValue;
    else
	for (j = 0; j < i; j++)
	    result = CDR(result);

    return CAR(result);
}


/* Return the last element of a list */
INLINE_FUN SEXP lastElt(SEXP list)
{
    SEXP result = R_NilValue;
    while (list != R_NilValue) {
	result = list;
	list = CDR(list);
    }
    return result;
}


/* Shorthands for creating small lists */

INLINE_FUN SEXP list1(SEXP s)
{
    return CONS(s, R_NilValue);
}


INLINE_FUN SEXP list2(SEXP s, SEXP t)
{
    PROTECT(s);
    s = CONS(s, CONS(t, R_NilValue));
    UNPROTECT(1);
    return s;
}


INLINE_FUN SEXP list3(SEXP s, SEXP t, SEXP u)
{
    PROTECT2(s,t);
    s = CONS(s, CONS(t, CONS(u, R_NilValue)));
    UNPROTECT(2);
    return s;
}


INLINE_FUN SEXP list4(SEXP s, SEXP t, SEXP u, SEXP v)
{
    PROTECT3(s,t,u);
    s = CONS(s, CONS(t, CONS(u, CONS(v, R_NilValue))));
    UNPROTECT(3);
    return s;
}

INLINE_FUN SEXP list5(SEXP s, SEXP t, SEXP u, SEXP v, SEXP w)
{
    PROTECT3(s,t,u);
    PROTECT(v);
    s = CONS(s, CONS(t, CONS(u, CONS(v, CONS(w, R_NilValue)))));
    UNPROTECT(4);
    return s;
}


/* Destructive list append : See also ``append'' */

INLINE_FUN SEXP listAppend(SEXP s, SEXP t)
{
    SEXP r;
    if (s == R_NilValue)
	return t;
    r = s;
    while (CDR(r) != R_NilValue)
	r = CDR(r);
    SETCDR(r, t);
    return s;
}


/* Language based list constructs.  These are identical to the list */
/* constructs, but the results can be evaluated. */

/* Return a (language) dotted pair with the given car and cdr */

INLINE_FUN SEXP lcons(SEXP car, SEXP cdr)
{
    SEXP e = cons(car, cdr);
    SET_TYPEOF(e, LANGSXP);
    return e;
}

/* Define the langN functions so that the nodes are allocated in order,
   not reverse order, so that access will be sequential if node allocation
   is sequential.  Be sure arguments get protected here, so caller needn't. */

INLINE_FUN SEXP lang1(SEXP s)
{
    return LCONS(s, R_NilValue);
}

INLINE_FUN SEXP lang2(SEXP s, SEXP t)
{
    PROTECT (t);
    PROTECT (s = LCONS(s,R_NilValue));
    SETCDR (s, CONS(t,R_NilValue));
    UNPROTECT(2);
    return s;
}

INLINE_FUN SEXP lang3(SEXP s, SEXP t, SEXP u)
{
    PROTECT2 (t,u);
    PROTECT (s = LCONS(s,R_NilValue));
    SETCDR (s, t = CONS(t,R_NilValue));
    SETCDR (t, CONS(u,R_NilValue));
    UNPROTECT(3);
    return s;
}

INLINE_FUN SEXP lang4(SEXP s, SEXP t, SEXP u, SEXP v)
{
    PROTECT3 (t,u,v);
    PROTECT (s = LCONS(s,R_NilValue));
    SETCDR (s, t = CONS(t,R_NilValue));
    SETCDR (t, u = CONS(u,R_NilValue));
    SETCDR (u, CONS(v,R_NilValue));
    UNPROTECT(4);
    return s;
}

INLINE_FUN SEXP lang5(SEXP s, SEXP t, SEXP u, SEXP v, SEXP w)
{
    PROTECT3 (t,u,v);
    PROTECT (w);
    PROTECT (s = LCONS(s,R_NilValue));
    SETCDR (s, t = CONS(t,R_NilValue));
    SETCDR (t, u = CONS(u,R_NilValue));
    SETCDR (u, v = CONS(v,R_NilValue));
    SETCDR (v, CONS(w,R_NilValue));
    UNPROTECT(5);
    return s;
}

INLINE_FUN SEXP lang6(SEXP s, SEXP t, SEXP u, SEXP v, SEXP w, SEXP x)
{
    PROTECT3 (t,u,v);
    PROTECT2 (w,x);
    PROTECT (s = LCONS(s,R_NilValue));
    SETCDR (s, t = CONS(t,R_NilValue));
    SETCDR (t, u = CONS(u,R_NilValue));
    SETCDR (u, v = CONS(v,R_NilValue));
    SETCDR (v, w = CONS(w,R_NilValue));
    SETCDR (w, CONS(x,R_NilValue));
    UNPROTECT(6);
    return s;
}

/* from util.c */

/* Check to see if the arrays "x" and "y" have the identical extents */

INLINE_FUN Rboolean conformable(SEXP x, SEXP y)
{
    int i, n;
    PROTECT(x = getAttrib00(x, R_DimSymbol));
    y = getAttrib00(y, R_DimSymbol);
    UNPROTECT(1);
    if ((n = LENGTH(x)) != LENGTH(y))
	return FALSE;
    for (i = 0; i < n; i++)
	if (INTEGER(x)[i] != INTEGER(y)[i])
	    return FALSE;
    return TRUE;
}

/* NOTE: R's inherits() is based on inherits3() in ../main/objects.c
 * Here, use char / CHAR() instead of the slower more general translateChar()
 */
INLINE_FUN Rboolean inherits(SEXP s, const char *name)
{
    SEXP klass;
    int i, nclass;
    if (OBJECT(s)) {
	klass = getClassAttrib(s);
        if (klass == R_NilValue)
            return FALSE;
	nclass = LENGTH(klass);
	for (i = 0; i < nclass; i++) {
	    if (!strcmp(CHAR(STRING_ELT(klass, i)), name))
		return TRUE;
	}
    }
    return FALSE;
}

/* Like inherits, but with name as a CHARSXP. */
INLINE_FUN Rboolean inherits_CHAR(SEXP s, SEXP name)
{
    SEXP klass;
    int i, nclass;
    if (OBJECT(s)) {
	klass = getClassAttrib(s);
        if (klass == R_NilValue)
            return FALSE;
	nclass = LENGTH(klass);
	for (i = 0; i < nclass; i++) {
	    if (STRING_ELT(klass,i) == name)
		return TRUE;
	}
    }
    return FALSE;
}

INLINE_FUN Rboolean isValidString(SEXP x)
{
    return TYPEOF(x) == STRSXP && LENGTH(x) > 0 && TYPEOF(STRING_ELT(x, 0)) != NILSXP;
}

/* non-empty ("") valid string :*/
INLINE_FUN Rboolean isValidStringF(SEXP x)
{
    return isValidString(x) && CHAR(STRING_ELT(x, 0))[0];
}

INLINE_FUN Rboolean isUserBinop(SEXP s)
{
    if (TYPEOF(s) == SYMSXP) {
	const char *str = CHAR(PRINTNAME(s));
	if (strlen(str) >= 2 && str[0] == '%' && str[strlen(str)-1] == '%')
	    return TRUE;
    }
    return FALSE;
}

INLINE_FUN Rboolean isFunction(SEXP s)
{
    return (FUNCTION_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isPrimitive(SEXP s)
{
    return (PRIMITIVE_FUN_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isList(SEXP s)
{
    return (s == R_NilValue || TYPEOF(s) == LISTSXP);
}

INLINE_FUN Rboolean isNewList(SEXP s)
{
    return (s == R_NilValue || TYPEOF(s) == VECSXP);
}

INLINE_FUN Rboolean isPairList(SEXP s)
{
    return (PAIRLIST_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isVectorList(SEXP s)
{
    return (NONATOMIC_VECTOR_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isVectorAtomic(SEXP s)
{
    return (ATOMIC_VECTOR_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isVectorNonpointer(SEXP s)
{
    return (NONPOINTER_VECTOR_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isVector(SEXP s)/* === isVectorList() or isVectorAtomic() */
{
    return (VECTOR_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isFrame(SEXP s)
{
    return inherits (s, "data.frame");
}

INLINE_FUN Rboolean isLanguage(SEXP s)
{
    return (s == R_NilValue || TYPEOF(s) == LANGSXP);
}

INLINE_FUN Rboolean isMatrix(SEXP s)
{
    SEXP t;
    if (isVector(s)) {
	t = getAttrib00(s, R_DimSymbol);
	/* You are not supposed to be able to assign a non-integer dim,
	   although this might be possible by misuse of ATTRIB. */
	if (TYPEOF(t) == INTSXP && LENGTH(t) == 2)
	    return TRUE;
    }
    return FALSE;
}

INLINE_FUN Rboolean isArray(SEXP s)
{
    SEXP t;
    if (isVector(s)) {
	t = getAttrib00(s, R_DimSymbol);
	/* You are not supposed to be able to assign a 0-length dim,
	 nor a non-integer dim */
	if (TYPEOF(t) == INTSXP && LENGTH(t) > 0)
	    return TRUE;
    }
    return FALSE;
}

INLINE_FUN Rboolean isTs(SEXP s)
{
    return (isVector(s) && getAttrib00(s, R_TspSymbol) != R_NilValue);
}


INLINE_FUN Rboolean isInteger(SEXP s)
{
    return TYPEOF(s) == INTSXP && !inherits_CHAR (s, R_factor_CHARSXP);
}

INLINE_FUN Rboolean isFactor(SEXP s)
{
    return TYPEOF(s) == INTSXP  && inherits_CHAR (s, R_factor_CHARSXP);
}

INLINE_FUN int nlevels(SEXP f)
{
    return isFactor(f) ? length(getAttrib00(f, R_LevelsSymbol)) : 0;
}

/* Is an object of numeric type? */
/* FIXME:  the LGLSXP case should be excluded here
 * (really? in many places we affirm they are treated like INTs)*/

INLINE_FUN Rboolean isNumeric(SEXP s)
{
    return TYPEOF(s) == INTSXP ? !inherits_CHAR (s, R_factor_CHARSXP) :
                                 ((NUMERIC_TYPES >> TYPEOF(s)) & 1);
}

/** Is an object a number, including both "numeric" and "complex"? */
INLINE_FUN Rboolean isNumber(SEXP s)
{
    return TYPEOF(s) == INTSXP ? !inherits_CHAR (s, R_factor_CHARSXP) :
                                 ((NUMBER_TYPES >> TYPEOF(s)) & 1);
}

static inline Rboolean isNumericOrFactor(SEXP s)
{
    return (NUMERIC_TYPES >> TYPEOF(s)) & 1;
}

INLINE_FUN Rboolean isNumberOrFactor(SEXP s)
{
    return (NUMBER_TYPES >> TYPEOF(s)) & 1;
}

/* The ScalarXXX functions plus ScalarLogicalMaybeConst are here, the other 
   ScalarXXXMaybeConst functions are in memory.c. */

INLINE_FUN SEXP ScalarLogical(int x)
{
    SEXP ans = allocVector1LGL();
    LOGICAL(ans)[0] = x == 0 || x == NA_LOGICAL ? x : 1;
    return ans;
}

INLINE_FUN SEXP ScalarInteger(int x)
{
    SEXP ans = allocVector1INT();
    INTEGER(ans)[0] = x;
    return ans;
}

INLINE_FUN SEXP ScalarReal(double x)
{
    SEXP ans = allocVector1REAL();
    REAL(ans)[0] = x;
    return ans;
}

INLINE_FUN SEXP ScalarComplex(Rcomplex x)
{
    SEXP ans = allocVector(CPLXSXP, 1);
    COMPLEX(ans)[0] = x;
    return ans;
}

INLINE_FUN SEXP ScalarString(SEXP x)
{
    SEXP ans;
    PROTECT(x);
    ans = allocVector(STRSXP, 1);
    SET_STRING_ELT(ans, 0, x);
    UNPROTECT(1);
    return ans;
}

INLINE_FUN SEXP ScalarRaw(Rbyte x)
{
    SEXP ans = allocVector1RAW();
    RAW(ans)[0] = x;
    return ans;
}

INLINE_FUN SEXP ScalarLogicalMaybeConst(int x)
{
    return x == 0 ? R_ScalarLogicalFALSE
         : x == NA_LOGICAL ? R_ScalarLogicalNA
         : R_ScalarLogicalTRUE;
}

/* Conversion between R and C99 complex values. */

INLINE_FUN double complex C99_from_R_complex (Rcomplex *x)
{
#if __GNUC__
    double complex ans = (double complex) 0; /* -Wall */
    __real__ ans = x->r;
    __imag__ ans = x->i;
    return ans;
#else
    return x->r + x->i * I;
#endif
}

INLINE_FUN void R_from_C99_complex (Rcomplex *x, double complex value)
{
    x->r = creal(value);
    x->i = cimag(value);
}


/* Check to see if a list can be made into a vector. */
/* it must have every element being a vector of length 1. */
/* BUT it does not exclude 0! */

INLINE_FUN Rboolean isVectorizable(SEXP s)
{
    if (s == R_NilValue) return TRUE;
    else if (isNewList(s)) {
	int i, n;

	n = LENGTH(s);
	for (i = 0 ; i < n; i++)
	    if (!isVector(VECTOR_ELT(s, i)) || LENGTH(VECTOR_ELT(s, i)) > 1)
		return FALSE;
	return TRUE;
    }
    else if (isList(s)) {
	for ( ; s != R_NilValue; s = CDR(s))
	    if (!isVector(CAR(s)) || LENGTH(CAR(s)) > 1) return FALSE;
	return TRUE;
    }
    else return FALSE;
}


/**
 * Create a named vector of type TYP
 *
 * @example const char *nms[] = {"xi", "yi", "zi", ""};
 *          mkNamed(VECSXP, nms);  =~= R  list(xi=, yi=, zi=)
 *
 * @param TYP a vector SEXP type (e.g. REALSXP)
 * @param names names of list elements with null string appended
 *
 * @return (pointer to a) named vector of type TYP
 */
INLINE_FUN SEXP mkNamed(SEXPTYPE TYP, const char **names)
{
    SEXP ans, nms;
    int i, n;

    for (n = 0; strlen(names[n]) > 0; n++) {}
    ans = PROTECT(allocVector(TYP, n));
    nms = PROTECT(allocVector(STRSXP, n));
    for (i = 0; i < n; i++)
	SET_STRING_ELT(nms, i, mkChar(names[i]));
    setAttrib(ans, R_NamesSymbol, nms);
    UNPROTECT(2);
    return ans;
}


/* short cut for  ScalarString(mkChar(s)) : */
INLINE_FUN SEXP mkString(const char *s)
{
    return ScalarString (mkChar(s));
}


/* String matching - moved from match.c, where related functions still are. */

/*  Exact or partial string match.  Returns 0 if f and t do not match at all, 
    1 if they match exactly, and -1 if t is a prefix of f, but does not match 
    exactly.  Note that the empty string is a prefix of any string. */

static inline int Rf_ep_match_strings (const char *f, const char *t)
{
    if (f == t)
        return 1;

    while (*t) {
        if (*t != *f)
            return 0;
        t++;
        f++;
    }

    return *f==0 ? 1 : -1;
}


/* Character translation.  Strings (maybe) needing translation are marked
   by TYPE_ET_CETERA_VEC_DOTS_TR in TYPE_ETC, so we can quickly check for
   the non-trivial case here. */

INLINE_FUN const char *Rf_translateChar (SEXP x)
{
    return TYPE_ETC(x) == CHARSXP ? CHAR(x) : Rf_translateChar_nontrivial(x);
}


/* This function extracts one srcref, and confirms the format.  It is 
   passed an index and the array and length from getBlockSrcrefs. */

INLINE_FUN SEXP getSrcref(SEXP *refs, int len, int ind)
{
    if (ind < len) {
        SEXP result = refs[ind];
        if (TYPEOF(result) == INTSXP && LENGTH(result) >= 6)
            return result;
    }

    return R_NilValue;
}


/* This function gets the srcref attribute from a statement block, 
   and confirms it's in the expected format */
   
INLINE_FUN void getBlockSrcrefs(SEXP call, SEXP **refs, int *len)
{
    SEXP srcrefs = getAttrib00(call, R_SrcrefSymbol);
    if (TYPEOF(srcrefs) == VECSXP) {
        *refs = (SEXP *) DATAPTR(srcrefs);
        *len = LENGTH(srcrefs);
    }
    else
    {   *len = 0;
    }
}


/* Compute offset from k array indexes, and advance to next index set. 
   Sets 'last' to one if this is the last index.*/

INLINE_FUN R_len_t array_offset_from_index
   (int **subs, int *bound, int *indx, int *offset, int k, int chkNA, int *last)
{
    R_len_t ii, jj;
    int j;

    jj = subs[0][indx[0]];

    if (chkNA && jj == NA_INTEGER)
        ii = NA_INTEGER;    
    else {
        ii = jj-1;
        for (j = 1; j < k; j++) {
            jj = subs[j][indx[j]];
            if (chkNA && jj == NA_INTEGER)
                break;
            ii += (jj-1) * offset[j];
        }
    }

    j = 0;
    while (++indx[j] >= bound[j]) {
        indx[j] = 0;
        if (++j >= k) {
            *last = 1;
            break;
        }
    }

    return ii;
}


#endif /* R_INLINES_H_ */
