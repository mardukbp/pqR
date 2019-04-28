/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2017, 2018, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995,1996  Robert Gentleman, Ross Ihaka
 *  Copyright (C) 1997-2011  The R Core Team
 *  Copyright (C) 2003-2009 The R Foundation
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
#include <Defn.h> /*-- Maybe modularize into own Coerce.h ..*/
#include <float.h> /* for DBL_DIG */
#define R_MSG_mode	_("invalid 'mode' argument")
#define R_MSG_list_vec	_("applies only to lists and vectors")
#include <Rmath.h>
#include <Print.h>

#include "scalar-stack.h"


static SEXP ItemName(SEXP names, int i)
{
  /* return  names[i]  if it is a character (>= 1 char), or NULL otherwise */
    if (names != R_NilValue &&
	STRING_ELT(names, i) != R_NilValue &&
	CHAR(STRING_ELT(names, i))[0] != '\0') /* length test */
	return STRING_ELT(names, i);
    else
	return R_NilValue;
}


/* This section of code handles type conversion for elements */
/* of data vectors.  Type coercion throughout R should use these */
/* routines to ensure consistency. */

/* The following two macros copy or clear the attributes.  They also
   ensure that the object bit is properly set.  They avoid calling the
   assignment functions when possible, since the write barrier (and
   possibly cache behavior on some architectures) makes assigning more
   costly than dereferencing. */
#define DUPLICATE_ATTRIB(to, from) do {\
  SEXP __from__ = (from); \
  if (ATTRIB(__from__) != R_NilValue) { \
    SEXP __to__ = (to); \
    (DUPLICATE_ATTRIB)(__to__, __from__);	\
  } \
} while (0)

#define CLEAR_ATTRIB(x) do {\
  SEXP __x__ = (x); \
  if (ATTRIB(__x__) != R_NilValue) { \
    SET_ATTRIB(__x__, R_NilValue); \
    if (OBJECT(__x__)) SET_OBJECT(__x__, 0); \
    if (IS_S4_OBJECT(__x__)) UNSET_S4_OBJECT(__x__); \
  } \
} while (0)

void attribute_hidden CoercionWarning(int warn)
{
    if (warn & WARN_NA)
	warning(_("NAs introduced by coercion"));
    if (warn & WARN_INACC)
	warning(_("inaccurate integer conversion in coercion"));
    if (warn & WARN_IMAG)
	warning(_("imaginary parts discarded in coercion"));
    if (warn & WARN_RAW)
	warning(_("out-of-range values treated as 0 in coercion to raw"));
}

int attribute_hidden RawFromLogical(int x, int *warn)
{
    if (x == NA_LOGICAL) {
        *warn |= WARN_RAW;
        return 0;
    }
    return x;
}

int attribute_hidden RawFromInteger(int x, int *warn)
{
    if (x < 0 || x > 255) {
        *warn |= WARN_RAW;
        return 0;
    }
    return x;
}

int attribute_hidden RawFromReal(double x, int *warn)
{
    if (ISNAN(x) || (int)x < 0 || (int)x > 255) {
        *warn |= WARN_RAW;
        return 0;
    }
    return (int)x;
}

int attribute_hidden RawFromComplex(Rcomplex x, int *warn)
{
    if (ISNAN(x.r) || ISNAN(x.i) || (int)x.r < 0 || (int)x.r > 255) {
        *warn |= WARN_RAW;
        return 0;
    }
    if (x.i != 0)
	*warn |= WARN_IMAG;
    return (int)x.r;
}

int attribute_hidden RawFromString(SEXP x, int *warn)
{
    int tmp = IntegerFromString(x,warn);
    if (tmp == NA_INTEGER || tmp < 0 || tmp > 255)
    { *warn |= WARN_RAW;
      return 0;
    }
    return tmp;
}

int attribute_hidden LogicalFromRaw(int x, int *warn)
{
    return x != 0;
}

int attribute_hidden LogicalFromInteger(int x, int *warn)
{
    return (x == NA_INTEGER) ? NA_LOGICAL : (x != 0);
}

int attribute_hidden LogicalFromReal(double x, int *warn)
{
    return ISNAN(x) ? NA_LOGICAL : (x != 0);
}

int attribute_hidden LogicalFromComplex(Rcomplex x, int *warn)
{
    return (ISNAN(x.r) || ISNAN(x.i)) ? NA_LOGICAL : (x.r != 0 || x.i != 0);
}

int attribute_hidden LogicalFromString(SEXP x, int *warn)
{

    if (x == R_TRUE_CHARSXP)
        return TRUE;
    if (x == R_FALSE_CHARSXP)
        return FALSE;

    if (x == R_NaString) 
        return NA_LOGICAL;
    
    const char *cx = CHAR(x);
    if (StringTrue(cx))
        return TRUE;
    if (StringFalse(cx))
        return FALSE;

    return NA_LOGICAL;
}

int attribute_hidden IntegerFromRaw(int x, int *warn)
{
    return x;
}

int attribute_hidden IntegerFromLogical(int x, int *warn)
{
    return (x == NA_LOGICAL) ? NA_INTEGER : x;
}

int attribute_hidden IntegerFromReal(double x, int *warn)
{
    if (ISNAN(x))
	return NA_INTEGER;
    else if (x > INT_MAX || x <= INT_MIN ) {
	*warn |= WARN_NA;
	return NA_INTEGER;
    }
    return x;
}

int attribute_hidden IntegerFromComplex(Rcomplex x, int *warn)
{
    if (ISNAN(x.r) || ISNAN(x.i))
	return NA_INTEGER;
    else if (x.r > INT_MAX || x.r <= INT_MIN ) {
	*warn |= WARN_NA;
	return NA_INTEGER;;
    }
    if (x.i != 0)
	*warn |= WARN_IMAG;
    return x.r;
}

int attribute_hidden IntegerFromString(SEXP x, int *warn)
{
    const char *cx = CHAR(x);
    double xdouble;
    char *endp;
    if (x != R_NaString && !isBlankString(cx)) { /* ASCII */
	xdouble = R_strtod(cx, &endp); /* ASCII */
	if (isBlankString(endp)) {
	    if (xdouble > INT_MAX) {
		*warn |= WARN_INACC;
		return INT_MAX;
	    }
	    else if(xdouble < INT_MIN+1) {
		*warn |= WARN_INACC;
		return INT_MIN;
	    }
	    else
		return xdouble;
	}
	else *warn |= WARN_NA;
    }
    return NA_INTEGER;
}

double attribute_hidden RealFromRaw(int x, int *warn)
{
    return x;
}

double attribute_hidden RealFromLogical(int x, int *warn)
{
    return (x == NA_LOGICAL) ? NA_REAL : x;
}

double attribute_hidden RealFromInteger(int x, int *warn)
{
    if (x == NA_INTEGER)
	return NA_REAL;
    else
	return x;
}

double attribute_hidden RealFromComplex(Rcomplex x, int *warn)
{
    if (ISNAN(x.r) || ISNAN(x.i))
	return NA_REAL;
    if (x.i != 0)
	*warn |= WARN_IMAG;
    return x.r;
}

double attribute_hidden RealFromString(SEXP x, int *warn)
{
    const char *cx = CHAR(x);
    double xdouble;
    char *endp;
    if (x != R_NaString && !isBlankString(cx)) { /* ASCII */
	xdouble = R_strtod(cx, &endp); /* ASCII */
	if (isBlankString(endp))
	    return xdouble;
	else
	    *warn |= WARN_NA;
    }
    return NA_REAL;
}

Rcomplex attribute_hidden ComplexFromRaw(int x, int *warn)
{
    Rcomplex z;
    if (x == NA_LOGICAL) {
	z.r = NA_REAL;
	z.i = NA_REAL;
    }
    else {
	z.r = x;
	z.i = 0;
    }
    return z;
}

Rcomplex attribute_hidden ComplexFromLogical(int x, int *warn)
{
    Rcomplex z;
    if (x == NA_LOGICAL) {
	z.r = NA_REAL;
	z.i = NA_REAL;
    }
    else {
	z.r = x;
	z.i = 0;
    }
    return z;
}

Rcomplex attribute_hidden ComplexFromInteger(int x, int *warn)
{
    Rcomplex z;
    if (x == NA_INTEGER) {
	z.r = NA_REAL;
	z.i = NA_REAL;
    }
    else {
	z.r = x;
	z.i = 0;
    }
    return z;
}

Rcomplex attribute_hidden ComplexFromReal(double x, int *warn)
{
    Rcomplex z;
    if (ISNAN(x)) {
	z.r = NA_REAL;
	z.i = NA_REAL;
    }
    else {
	z.r = x;
	z.i = 0;
    }
    return z;
}

Rcomplex attribute_hidden ComplexFromString(SEXP x, int *warn)
{
    double xr, xi;
    Rcomplex z;
    const char *cx = CHAR(x); /* ASCII */
    char *endp;

    z.r = z.i = NA_REAL;
    if (x != R_NaString && !isBlankString(cx)) {
	xr = R_strtod(cx, &endp);
	if (isBlankString(endp)) {
	    z.r = xr;
	    z.i = 0.0;
	}
	else if (*endp == '+' || *endp == '-') {
	    xi = R_strtod(endp, &endp);
	    if (*endp++ == 'i' && isBlankString(endp)) {
		z.r = xr;
		z.i = xi;
	    }
	    else *warn |= WARN_NA;
	}
	else *warn |= WARN_NA;
    }
    return z;
}

static SEXP StringFromRaw(Rbyte x, int *warn)
{
    char buf[3];
    sprintf(buf, "%02x", x);
    return mkChar(buf);
}

SEXP attribute_hidden StringFromLogical(int x, int *warn)
{
    return x==NA_LOGICAL ? NA_STRING : x ? R_TRUE_CHARSXP : R_FALSE_CHARSXP;
}

SEXP attribute_hidden StringFromInteger(int x, int *warn)
{
    if (x == NA_INTEGER) 
        return NA_STRING;
    else {
        char s[12];
        integer_to_string(s,x);
        return mkChar(s);
    }
}

SEXP attribute_hidden StringFromReal(double x, int *warn)
{
    if (ISNA(x))
        return NA_STRING;
    else {
        static char s[1000];
        double_to_string(s,x);
        return mkChar(s);
    }
}

SEXP attribute_hidden StringFromComplex(Rcomplex x, int *warn)
{
    if (ISNA(x.r) || ISNA(x.i))
        return NA_STRING;
    else {
        int wr, dr, er, wi, di, ei;
        formatComplex(&x, 1, &wr, &dr, &er, &wi, &di, &ei, 0);
        /* EncodeComplex has its own anti-trailing-0 care */
	return mkChar(EncodeComplex(x, wr, dr, er, wi, di, ei, OutDec));
    }
}

/* Copy n elements from a numeric (raw, logical, integer, real, or complex) 
   or string vector v (starting at j, stepping by t) to a numeric or string 
   vector x (starting at i, stepping by s), which is not necessarily of the 
   same type.  The value returned is the OR of all warning flags produced 
   as a result of conversions, zero if no warnings.  Note: t may be zero, 
   s should not be zero. 

   The arguments x and v are protected within this procedure if necessary,
   but this will only be if at least one is of string type (allowing it to
   be used in task procedures not involving strings). */

int copy_elements_coerced
  (SEXP x, int i, int s, SEXP v, int j, int t, int n)
{
    if (n == 0) 
        return 0;

    int typx = TYPEOF(x);
    int typv = TYPEOF(v);

    if (typx == typv) {
        copy_elements(x,i,s,v,j,t,n);
        return 0;
    }

    int e = i + n*s;
    int w = 0;

    int prot = typx == STRSXP || typv == STRSXP;
    if (prot) PROTECT2(x,v); 

    if (n & 1) {
        switch ((typx<<5) + typv) {
        case (RAWSXP<<5) + LGLSXP:
            RAW(x)[i] = RawFromLogical(LOGICAL(v)[j],&w);
            break;
        case (RAWSXP<<5) + INTSXP:
            RAW(x)[i] = RawFromInteger(INTEGER(v)[j],&w);
            break;
        case (RAWSXP<<5) + REALSXP:
            RAW(x)[i] = RawFromReal(REAL(v)[j],&w);
            break;
        case (RAWSXP<<5) + CPLXSXP:
            RAW(x)[i] = RawFromComplex(COMPLEX(v)[j],&w);
            break;
        case (RAWSXP<<5) + STRSXP:
            RAW(x)[i] = RawFromString(STRING_ELT(v,j),&w);
            break;
        case (LGLSXP<<5) + RAWSXP:
            LOGICAL(x)[i] = LogicalFromRaw(RAW(v)[j],&w);
            break;
        case (LGLSXP<<5) + INTSXP:
            LOGICAL(x)[i] = LogicalFromInteger(INTEGER(v)[j],&w);
            break;
        case (LGLSXP<<5) + REALSXP:
            LOGICAL(x)[i] = LogicalFromReal(REAL(v)[j],&w);
            break;
        case (LGLSXP<<5) + CPLXSXP:
            LOGICAL(x)[i] = LogicalFromComplex(COMPLEX(v)[j],&w);
            break;
        case (LGLSXP<<5) + STRSXP:
            LOGICAL(x)[i] = LogicalFromString(STRING_ELT(v,j),&w);
            break;
        case (INTSXP<<5) + RAWSXP:
            INTEGER(x)[i] = IntegerFromRaw(RAW(v)[j],&w);
            break;
        case (INTSXP<<5) + LGLSXP:
            INTEGER(x)[i] = IntegerFromLogical(LOGICAL(v)[j],&w);
            break;
        case (INTSXP<<5) + REALSXP:
            INTEGER(x)[i] = IntegerFromReal(REAL(v)[j],&w);
            break;
        case (INTSXP<<5) + CPLXSXP:
            INTEGER(x)[i] = IntegerFromComplex(COMPLEX(v)[j],&w);
            break;
        case (INTSXP<<5) + STRSXP:
            INTEGER(x)[i] = IntegerFromString(STRING_ELT(v,j),&w);
            break;
        case (REALSXP<<5) + RAWSXP:
            REAL(x)[i] = RealFromRaw(RAW(v)[j],&w);
            break;
        case (REALSXP<<5) + LGLSXP:
            REAL(x)[i] = RealFromLogical(LOGICAL(v)[j],&w);
            break;
        case (REALSXP<<5) + INTSXP:
            REAL(x)[i] = RealFromInteger(INTEGER(v)[j],&w);
            break;
        case (REALSXP<<5) + CPLXSXP:
            REAL(x)[i] = RealFromComplex(COMPLEX(v)[j],&w);
            break;
        case (REALSXP<<5) + STRSXP:
            REAL(x)[i] = RealFromString(STRING_ELT(v,j),&w);
            break;
        case (CPLXSXP<<5) + RAWSXP:
            COMPLEX(x)[i] = ComplexFromRaw(RAW(v)[j],&w);
            break;
        case (CPLXSXP<<5) + LGLSXP:
            COMPLEX(x)[i] = ComplexFromLogical(LOGICAL(v)[j],&w);
            break;
        case (CPLXSXP<<5) + INTSXP:
            COMPLEX(x)[i] = ComplexFromInteger(INTEGER(v)[j],&w);
            break;
        case (CPLXSXP<<5) + REALSXP:
            COMPLEX(x)[i] = ComplexFromReal(REAL(v)[j],&w);
            break;
        case (CPLXSXP<<5) + STRSXP:
            COMPLEX(x)[i] = ComplexFromString(STRING_ELT(v,j),&w);
            break;
        case (STRSXP<<5) + RAWSXP:
            SET_STRING_ELT (x, i, StringFromRaw(RAW(v)[j],&w)); 
            break;
        case (STRSXP<<5) + LGLSXP:
            SET_STRING_ELT (x, i, StringFromLogical(LOGICAL(v)[j],&w)); 
            break;
        case (STRSXP<<5) + INTSXP:
            SET_STRING_ELT (x, i, StringFromInteger(INTEGER(v)[j],&w)); 
            break;
        case (STRSXP<<5) + REALSXP:
            SET_STRING_ELT (x, i, StringFromReal(REAL(v)[j],&w)); 
            break;
        case (STRSXP<<5) + CPLXSXP:
            SET_STRING_ELT (x, i, StringFromComplex(COMPLEX(v)[j],&w)); 
            break;
        default:
            UNIMPLEMENTED_TYPE("copy_elements_coerced", x);
        }
        i += s; j += t;
    }

    if (n == 1) {
        if (prot) UNPROTECT(2);
        return w;
    }

    switch ((typx<<5) + typv) {

    case (RAWSXP<<5) + LGLSXP:
        do {
            RAW(x)[i] = RawFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
            RAW(x)[i] = RawFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (RAWSXP<<5) + INTSXP:
        do {
            RAW(x)[i] = RawFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
            RAW(x)[i] = RawFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (RAWSXP<<5) + REALSXP:
        do {
            RAW(x)[i] = RawFromReal(REAL(v)[j],&w);
            i += s; j += t;
            RAW(x)[i] = RawFromReal(REAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (RAWSXP<<5) + CPLXSXP:
        do {
            RAW(x)[i] = RawFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
            RAW(x)[i] = RawFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (RAWSXP<<5) + STRSXP:
        do {
            RAW(x)[i] = RawFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
            RAW(x)[i] = RawFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (LGLSXP<<5) + RAWSXP:
        do {
            LOGICAL(x)[i] = LogicalFromRaw(RAW(v)[j],&w);
            i += s; j += t;
            LOGICAL(x)[i] = LogicalFromRaw(RAW(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (LGLSXP<<5) + INTSXP:
        do {
            LOGICAL(x)[i] = LogicalFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
            LOGICAL(x)[i] = LogicalFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (LGLSXP<<5) + REALSXP:
        do {
            LOGICAL(x)[i] = LogicalFromReal(REAL(v)[j],&w);
            i += s; j += t;
            LOGICAL(x)[i] = LogicalFromReal(REAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (LGLSXP<<5) + CPLXSXP:
        do {
            LOGICAL(x)[i] = LogicalFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
            LOGICAL(x)[i] = LogicalFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (LGLSXP<<5) + STRSXP:
        do {
            LOGICAL(x)[i] = LogicalFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
            LOGICAL(x)[i] = LogicalFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (INTSXP<<5) + RAWSXP:
        do {
            INTEGER(x)[i] = IntegerFromRaw(RAW(v)[j],&w);
            i += s; j += t;
            INTEGER(x)[i] = IntegerFromRaw(RAW(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (INTSXP<<5) + LGLSXP:
        do {
            INTEGER(x)[i] = IntegerFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
            INTEGER(x)[i] = IntegerFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (INTSXP<<5) + REALSXP:
        do {
            INTEGER(x)[i] = IntegerFromReal(REAL(v)[j],&w);
            i += s; j += t;
            INTEGER(x)[i] = IntegerFromReal(REAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (INTSXP<<5) + CPLXSXP:
        do {
            INTEGER(x)[i] = IntegerFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
            INTEGER(x)[i] = IntegerFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (INTSXP<<5) + STRSXP:
        do {
            INTEGER(x)[i] = IntegerFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
            INTEGER(x)[i] = IntegerFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (REALSXP<<5) + RAWSXP:
        do {
            REAL(x)[i] = RealFromRaw(RAW(v)[j],&w);
            i += s; j += t;
            REAL(x)[i] = RealFromRaw(RAW(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (REALSXP<<5) + LGLSXP:
        do {
            REAL(x)[i] = RealFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
            REAL(x)[i] = RealFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (REALSXP<<5) + INTSXP:
        do {
            REAL(x)[i] = RealFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
            REAL(x)[i] = RealFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (REALSXP<<5) + CPLXSXP:
        do {
            REAL(x)[i] = RealFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
            REAL(x)[i] = RealFromComplex(COMPLEX(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (REALSXP<<5) + STRSXP:
        do {
            REAL(x)[i] = RealFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
            REAL(x)[i] = RealFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (CPLXSXP<<5) + RAWSXP:
        do {
            COMPLEX(x)[i] = ComplexFromRaw(RAW(v)[j],&w);
            i += s; j += t;
            COMPLEX(x)[i] = ComplexFromRaw(RAW(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (CPLXSXP<<5) + LGLSXP:
        do {
            COMPLEX(x)[i] = ComplexFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
            COMPLEX(x)[i] = ComplexFromLogical(LOGICAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (CPLXSXP<<5) + INTSXP:
        do {
            COMPLEX(x)[i] = ComplexFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
            COMPLEX(x)[i] = ComplexFromInteger(INTEGER(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (CPLXSXP<<5) + REALSXP:
        do {
            COMPLEX(x)[i] = ComplexFromReal(REAL(v)[j],&w);
            i += s; j += t;
            COMPLEX(x)[i] = ComplexFromReal(REAL(v)[j],&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (CPLXSXP<<5) + STRSXP:
        do {
            COMPLEX(x)[i] = ComplexFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
            COMPLEX(x)[i] = ComplexFromString(STRING_ELT(v,j),&w);
            i += s; j += t;
        } while (i != e);
        break;
    case (STRSXP<<5) + RAWSXP:
        do {
            SET_STRING_ELT (x, i, StringFromRaw(RAW(v)[j],&w)); 
            i += s; j += t;
            SET_STRING_ELT (x, i, StringFromRaw(RAW(v)[j],&w)); 
            i += s; j += t;
        } while (i != e);
        break;
    case (STRSXP<<5) + LGLSXP:
        do {
            SET_STRING_ELT (x, i, StringFromLogical(LOGICAL(v)[j],&w)); 
            i += s; j += t;
            SET_STRING_ELT (x, i, StringFromLogical(LOGICAL(v)[j],&w)); 
            i += s; j += t;
        } while (i != e);
        break;
    case (STRSXP<<5) + INTSXP:
        do {
            SET_STRING_ELT (x, i, StringFromInteger(INTEGER(v)[j],&w)); 
            i += s; j += t;
            SET_STRING_ELT (x, i, StringFromInteger(INTEGER(v)[j],&w)); 
            i += s; j += t;
        } while (i != e);
        break;
    case (STRSXP<<5) + REALSXP:
        do {
            SET_STRING_ELT (x, i, StringFromReal(REAL(v)[j],&w)); 
            i += s; j += t;
            SET_STRING_ELT (x, i, StringFromReal(REAL(v)[j],&w)); 
            i += s; j += t;
        } while (i != e);
        break;
    case (STRSXP<<5) + CPLXSXP:
        do {
            SET_STRING_ELT (x, i, StringFromComplex(COMPLEX(v)[j],&w)); 
            i += s; j += t;
            SET_STRING_ELT (x, i, StringFromComplex(COMPLEX(v)[j],&w)); 
            i += s; j += t;
        } while (i != e);
        break;

    default:
       UNIMPLEMENTED_TYPE("copy_elements_coerced", x);
    }

    if (prot) UNPROTECT(2);
    return w;
}

/* Conversion between the two list types (LISTSXP and VECSXP). */

SEXP PairToVectorList(SEXP x)
{
    SEXP xptr, xnew, xnames;
    int i, len = 0, named = 0;
    for (xptr = x ; xptr != R_NilValue ; xptr = CDR(xptr)) {
	named = named | (TAG(xptr) != R_NilValue);
	len++;
    }
    PROTECT(x);
    PROTECT(xnew = allocVector(VECSXP, len));
    for (i = 0, xptr = x; i < len; i++, xptr = CDR(xptr)) {
	SET_VECTOR_ELT(xnew, i, CAR(xptr));
        SET_NAMEDCNT_MAX(CAR(xptr));
    }
    if (named) {
	PROTECT(xnames = allocVector(STRSXP, len));
	for (i = 0, xptr = x; i < len; i++, xptr = CDR(xptr)) {
	    if(TAG(xptr) == R_NilValue)
		SET_STRING_ELT_BLANK(xnames, i);
	    else
		SET_STRING_ELT(xnames, i, PRINTNAME(TAG(xptr)));
	}
	setAttrib(xnew, R_NamesSymbol, xnames);
	UNPROTECT(1);
    }
    copyMostAttrib(x, xnew);
    UNPROTECT(2);
    return xnew;
}

SEXP VectorToPairList(SEXP x)
{
    SEXP xptr, xnew, xnames;
    int i, len, named;
    len = length(x);
    PROTECT(x);
    PROTECT(xnew = allocList(len));
    PROTECT(xnames = getAttrib(x, R_NamesSymbol));
    named = (xnames != R_NilValue);
    xptr = xnew;
    for (i = 0; i < len; i++) {
	SETCAR(xptr, VECTOR_ELT(x, i));
        SET_NAMEDCNT_MAX(CAR(xptr));
	if (named && CHAR(STRING_ELT(xnames, i))[0] != '\0') /* ASCII */
	    SET_TAG(xptr, install_translated (STRING_ELT(xnames,i)));
	xptr = CDR(xptr);
    }
    if (len>0)       /* can't set attributes on NULL */
	copyMostAttrib(x, xnew);
    UNPROTECT(3);
    return xnew;
}

static SEXP coerceToSymbol(SEXP v)
{
    SEXP ans = R_NilValue;
    int warn = 0;
    if (length(v) <= 0)
	error(_("invalid data of mode '%s' (too short)"),
	      type2char(TYPEOF(v)));
    PROTECT(v);
    switch(TYPEOF(v)) {
    case LGLSXP:
	ans = StringFromLogical(LOGICAL(v)[0], &warn);
	break;
    case INTSXP:
	ans = StringFromInteger(INTEGER(v)[0], &warn);
	break;
    case REALSXP:
	ans = StringFromReal(REAL(v)[0], &warn);
	break;
    case CPLXSXP:
	ans = StringFromComplex(COMPLEX(v)[0], &warn);
	break;
    case STRSXP:
	ans = STRING_ELT(v, 0);
	break;
    case RAWSXP:
	ans = StringFromRaw(RAW(v)[0], &warn);
	break;
    default:
	UNIMPLEMENTED_TYPE("coerceToSymbol", v);
    }
    PROTECT(ans);
    if (warn) CoercionWarning(warn);/*2000/10/23*/
    ans = installChar(ans);
    UNPROTECT(2); /* ans, v */
    return ans;
}

static SEXP coerceToExpression(SEXP v)
{
    SEXP ans;
    int i, n;
    if (isVectorAtomic(v)) {
	n = LENGTH(v);
	PROTECT(ans = allocVector(EXPRSXP, n));
	switch (TYPEOF(v)) {
	case LGLSXP:
	    for (i = 0; i < n; i++)
		SET_VECTOR_ELT(ans, i, ScalarLogicalMaybeConst(LOGICAL(v)[i]));
	    break;
	case INTSXP:
	    for (i = 0; i < n; i++)
		SET_VECTOR_ELT(ans, i, ScalarIntegerMaybeConst(INTEGER(v)[i]));
	    break;
	case REALSXP:
	    for (i = 0; i < n; i++)
		SET_VECTOR_ELT(ans, i, ScalarRealMaybeConst(REAL(v)[i]));
	    break;
	case CPLXSXP:
	    for (i = 0; i < n; i++)
		SET_VECTOR_ELT(ans, i, ScalarComplexMaybeConst(COMPLEX(v)[i]));
	    break;
	case STRSXP:
	    for (i = 0; i < n; i++)
		SET_VECTOR_ELT(ans, i, ScalarStringMaybeConst(STRING_ELT(v, i)));
	    break;
	case RAWSXP:
	    for (i = 0; i < n; i++)
		SET_VECTOR_ELT(ans, i, ScalarRawMaybeConst(RAW(v)[i]));
	    break;
	default:
	    UNIMPLEMENTED_TYPE("coerceToExpression", v);
	}
    }
    else {/* not used either */
	PROTECT(ans = allocVector(EXPRSXP, 1));
	SET_VECTOR_ELT(ans, 0, duplicate(v));
    }
    UNPROTECT(1);
    return ans;
}

static SEXP coerceToVectorList(SEXP v)
{
    SEXP ans, tmp;
    int i, n;
    n = length(v);
    PROTECT(ans = allocVector(VECSXP, n));
    switch (TYPEOF(v)) {
    case LGLSXP:
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, ScalarLogicalMaybeConst(LOGICAL(v)[i]));
	break;
    case INTSXP:
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, ScalarIntegerMaybeConst(INTEGER(v)[i]));
	break;
    case REALSXP:
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, ScalarRealMaybeConst(REAL(v)[i]));
	break;
    case CPLXSXP:
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, ScalarComplexMaybeConst(COMPLEX(v)[i]));
	break;
    case STRSXP:
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, ScalarStringMaybeConst(STRING_ELT(v, i)));
	break;
    case RAWSXP:
	for (i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, ScalarRawMaybeConst(RAW(v)[i]));
	break;
    case LISTSXP:
    case LANGSXP:
	tmp = v;
	for (i = 0; i < n; i++) {
	    SET_VECTOR_ELT(ans, i, CAR(tmp));
            INC_NAMEDCNT(CAR(tmp));
	    tmp = CDR(tmp);
	}
	break;
    default:
	UNIMPLEMENTED_TYPE("coerceToVectorList", v);
    }
    tmp = getAttrib(v, R_NamesSymbol);
    if (tmp != R_NilValue)
	setAttrib(ans, R_NamesSymbol, tmp);
    UNPROTECT(1);
    return (ans);
}

static SEXP coerceToPairList(SEXP v)
{
    SEXP ans, ansp;
    int i, n;
    n = LENGTH(v);
    PROTECT(ansp = ans = allocList(n));
    for (i = 0; i < n; i++) {
	switch (TYPEOF(v)) {
	case LGLSXP:
	    SETCAR(ansp, allocVector1LGL());
	    INTEGER(CAR(ansp))[0] = INTEGER(v)[i];
	    break;
	case INTSXP:
	    SETCAR(ansp, allocVector1INT());
	    INTEGER(CAR(ansp))[0] = INTEGER(v)[i];
	    break;
	case REALSXP:
	    SETCAR(ansp, allocVector1REAL());
	    REAL(CAR(ansp))[0] = REAL(v)[i];
	    break;
	case CPLXSXP:
	    SETCAR(ansp, allocVector(CPLXSXP, 1));
	    COMPLEX(CAR(ansp))[0] = COMPLEX(v)[i];
	    break;
	case STRSXP:
	    SETCAR(ansp, ScalarString(STRING_ELT(v, i)));
	    break;
	case RAWSXP:
	    SETCAR(ansp, allocVector1RAW());
	    RAW(CAR(ansp))[0] = RAW(v)[i];
	    break;
	case VECSXP:
	    SETCAR(ansp, VECTOR_ELT(v, i));
	    break;
	case EXPRSXP:
	    SETCAR(ansp, VECTOR_ELT(v, i));
	    break;
	default:
	    UNIMPLEMENTED_TYPE("coerceToPairList", v);
	}
	ansp = CDR(ansp);
    }
    ansp = getAttrib(v, R_NamesSymbol);
    if (ansp != R_NilValue)
	setAttrib(ans, R_NamesSymbol, ansp);
    UNPROTECT(1);
    return (ans);
}

/* Coerce a pairlist to the given type */
static SEXP coercePairList(SEXP v, SEXPTYPE type)
{
    int i, n=0;
    SEXP rval= R_NilValue, vp;

    /* Hmm, this is also called to LANGSXP, and coerceVector already
       did the check of TYPEOF(v) == type */
    if(type == LISTSXP) return v;/* IS pairlist */

    if (type == EXPRSXP) {
	PROTECT(rval = allocVector(type, 1));
	SET_VECTOR_ELT(rval, 0, v);
	UNPROTECT(1);
	return rval;
    }
    else if (type == STRSXP) {
	n = length(v);
	PROTECT(rval = allocVector(type, n));
	for (vp = v, i = 0; vp != R_NilValue; vp = CDR(vp), i++) {
            SEXP str = isString(CAR(vp)) && length(CAR(vp)) == 1 
                        ? CAR(vp) : deparse1line (CAR(vp), 0);
            SET_STRING_ELT (rval, i, STRING_ELT(str,0));
	}
    }
    else if (type == VECSXP) {
	rval = PairToVectorList(v);
	return rval;
    }
    else if (isVectorizable(v)) {
	n = length(v);
	PROTECT(rval = allocVector(type, n));
	switch (type) {
	case LGLSXP:
	    for (i = 0, vp = v; i < n; i++, vp = CDR(vp))
		LOGICAL(rval)[i] = asLogical(CAR(vp));
	    break;
	case INTSXP:
	    for (i = 0, vp = v; i < n; i++, vp = CDR(vp))
		INTEGER(rval)[i] = asInteger(CAR(vp));
	    break;
	case REALSXP:
	    for (i = 0, vp = v; i < n; i++, vp = CDR(vp))
		REAL(rval)[i] = asReal(CAR(vp));
	    break;
	case CPLXSXP:
	    for (i = 0, vp = v; i < n; i++, vp = CDR(vp))
		COMPLEX(rval)[i] = asComplex(CAR(vp));
	    break;
	case RAWSXP:
	    for (i = 0, vp = v; i < n; i++, vp = CDR(vp))
		RAW(rval)[i] = (Rbyte) asInteger(CAR(vp));
	    break;
	default:
	    UNIMPLEMENTED_TYPE("coercePairList", v);
	}
    }
    else
	error(_("'pairlist' object cannot be coerced to type '%s'"),
	      type2char(type));

    /* If any tags are non-null then we */
    /* need to add a names attribute. */
    for (vp = v, i = 0; vp != R_NilValue; vp = CDR(vp))
	if (TAG(vp) != R_NilValue)
	    i = 1;

    if (i) {
	SEXP names = allocVector(STRSXP, n);
	for (i = 0, vp = v; vp != R_NilValue; vp = CDR(vp), i++)
	    if (TAG(vp) != R_NilValue)
		SET_STRING_ELT(names, i, PRINTNAME(TAG(vp)));
	setAttrib(rval, R_NamesSymbol, names);
    }
    UNPROTECT(1);
    return rval;
}

/* Coerce a vector list to the given type.  Caller needn't protect v. */
static SEXP coerceVectorList(SEXP v, SEXPTYPE type)
{
    int i, n, warn = 0, tmp;
    SEXP rval;

    /* expression -> list, new in R 2.4.0 */
    if (type == VECSXP && TYPEOF(v) == EXPRSXP) {
	/* This is sneaky but saves us rewriting a lot of the duplicate code */
	rval = NAMEDCNT_GT_0(v) ? duplicate(v) : v;
	SET_TYPEOF(rval, VECSXP);
	return rval;
    }

    if (type == EXPRSXP && TYPEOF(v) == VECSXP) {
	rval = NAMEDCNT_GT_0(v) ? duplicate(v) : v;
	SET_TYPEOF(rval, EXPRSXP);
	return rval;
    }

    PROTECT(v);

    if (type == STRSXP) {
	n = length(v);
	PROTECT(rval = allocVector(type, n));
	for (i = 0; i < n;  i++) {
            SEXP str = isString(VECTOR_ELT(v,i)) && length(VECTOR_ELT(v,i)) == 1
                        ? VECTOR_ELT(v,i) : deparse1line (VECTOR_ELT(v,i), 0);
            SET_STRING_ELT (rval, i, STRING_ELT(str,0));
	}
    }
    else if (type == LISTSXP) {
	rval = VectorToPairList(v);
        UNPROTECT(1);
	return rval;
    }
    else if (isVectorizable(v)) {
	n = length(v);
	PROTECT(rval = allocVector(type, n));
	switch (type) {
	case LGLSXP:
	    for (i = 0; i < n; i++)
		LOGICAL(rval)[i] = asLogical(VECTOR_ELT(v, i));
	    break;
	case INTSXP:
	    for (i = 0; i < n; i++)
		INTEGER(rval)[i] = asInteger(VECTOR_ELT(v, i));
	    break;
	case REALSXP:
	    for (i = 0; i < n; i++)
		REAL(rval)[i] = asReal(VECTOR_ELT(v, i));
	    break;
	case CPLXSXP:
	    for (i = 0; i < n; i++)
		COMPLEX(rval)[i] = asComplex(VECTOR_ELT(v, i));
	    break;
	case RAWSXP:
	    for (i = 0; i < n; i++) {
		tmp = asInteger(VECTOR_ELT(v, i));
		if (tmp < 0 || tmp > 255) { /* includes NA_INTEGER */
		    tmp = 0;
		    warn |= WARN_RAW;
		}
		RAW(rval)[i] = (Rbyte) tmp;
	    }
	    break;
	default:
	    UNIMPLEMENTED_TYPE("coerceVectorList", v);
	}
    }
    else
	error(_("(list) object cannot be coerced to type '%s'"), 
	      type2char(type));

    if (warn) CoercionWarning(warn);
    SEXP names = getAttrib(v, R_NamesSymbol);
    if (names != R_NilValue)
	setAttrib(rval, R_NamesSymbol, names);
    UNPROTECT(2);
    return rval;
}

static SEXP coerceSymbol(SEXP v, SEXPTYPE type)
{
    SEXP rval = R_NilValue;
    if (type == EXPRSXP) {
	PROTECT(rval = allocVector(type, 1));
	SET_VECTOR_ELT(rval, 0, v);
	UNPROTECT(1);
    } else if (type == CHARSXP)
	rval = PRINTNAME(v);	
    else if (type == STRSXP)
	rval = ScalarString(PRINTNAME(v));
    else
	warning(_("(symbol) object cannot be coerced to type '%s'"), 
		type2char(type));
    return rval;
}

static SEXP coerce_numeric_or_string (SEXP v, int type)
{
    int n = LENGTH(v);
    int warn;
    SEXP ans;

    PROTECT(ans = allocVector(type,n));
    DUPLICATE_ATTRIB(ans, v);

    warn = copy_elements_coerced (ans, 0, 1, v, 0, 1, n);
    if (warn) CoercionWarning(warn);

    UNPROTECT(1);
    return ans;
}

/* The first argument of coerceVector need not be protected by the caller. */
SEXP coerceVector(SEXP v, SEXPTYPE type)
{
    SEXP op, vp, ans;
    int i,n;

    if (TYPEOF(v) == type)
	return v;

    PROTECT(v);
    WAIT_UNTIL_COMPUTED(v);

    /* code to allow classes to extend ENVSXP, SYMSXP, etc */
    if(IS_S4_OBJECT(v) && TYPEOF(v) == S4SXP) {
        SEXP vv = R_getS4DataSlot(v, ANYSXP);
        UNPROTECT(1);
	if(vv == R_NilValue)
	  error(_("no method for coercing this S4 class to a vector"));
	else if(TYPEOF(vv) == type)
	  return vv;
	PROTECT(v = vv);
    }

    switch (TYPEOF(v)) {
#ifdef NOTYET
    case NILSXP:
	ans = coerceNull(v, type);
	break;
#endif
    case SYMSXP:
	ans = coerceSymbol(v, type);
	break;
    case NILSXP:
    case LISTSXP:
	ans = coercePairList(v, type);
	break;
    case LANGSXP:
	if (type != STRSXP) {
	    ans = coercePairList(v, type);
	    break;
	}

	/* This is mostly copied from coercePairList, but we need to
	 * special-case the first element so as not to get operators
	 * put in backticks. */
	n = length(v);
	PROTECT(ans = allocVector(type, n));
	if (n == 0) {
            /* Can this actually happen? */
            UNPROTECT(1);
            break;
        }
	i = 0;
	op = CAR(v);
	/* The case of practical relevance is "lhs ~ rhs", which
	 * people tend to split using as.character(), modify, and
	 * paste() back together. However, we might as well
	 * special-case all symbolic operators here. */
	if (TYPEOF(op) == SYMSXP) {
	    SET_STRING_ELT(ans, i, PRINTNAME(op));
	    i++;
	    v = CDR(v);
	}

	/* The distinction between strings and other elements was
	 * here "always", but is really dubious since it makes x <- a
	 * and x <- "a" come out identical. Won't fix just now. */
	for (vp = v;  vp != R_NilValue; vp = CDR(vp), i++) {
            SEXP str = isString(CAR(vp)) && length(CAR(vp)) == 1 
                        ? CAR(vp) : deparse1line (CAR(vp), 0);
            SET_STRING_ELT (ans, i, STRING_ELT(str,0));
	}
	UNPROTECT(1);
	break;
    case VECSXP:
    case EXPRSXP:
	ans = coerceVectorList(v, type);
	break;
    case ENVSXP:
	error(_("environments cannot be coerced to other types"));
	break;
    case RAWSXP:
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case STRSXP:
	switch (type) {
	case SYMSXP:
	    ans = coerceToSymbol(v);
	    break;
	case EXPRSXP:
	    ans = coerceToExpression(v);
	    break;
	case VECSXP:
	    ans = coerceToVectorList(v);
	    break;
	case LISTSXP:
	    ans = coerceToPairList(v);
	    break;
	case RAWSXP:
	case LGLSXP:
	case INTSXP:
	case REALSXP:
	case CPLXSXP:
            ans = coerce_numeric_or_string (v, type);
	    break;
	case STRSXP:
            if (TYPEOF(v)==REALSXP || TYPEOF(v)==CPLXSXP) {
                int savedigits;
                PrintDefaults();
                savedigits = R_print.digits; 
                R_print.digits = DBL_DIG; /* MAX precision */
                ans = coerce_numeric_or_string (v, type);
                R_print.digits = savedigits;
            }
            else
                ans = coerce_numeric_or_string (v, type);
            break;
	default:
	    goto coerce_error;
	}
	break;
    default:
	goto coerce_error;
    }

    UNPROTECT(1);
    return ans;

coerce_error:
    error(_("cannot coerce type '%s' to vector of type '%s'"), 
             type2char(TYPEOF(v)), type2char(type));
}


SEXP CreateTag(SEXP x)
{
    if (isNull(x) || isSymbol(x))
	return x;
    else if (isString(x) && LENGTH(x) >= 1 && LENGTH(STRING_ELT(x, 0)) >= 1)
	return install_translated (STRING_ELT(x,0));
    else
	return installChar(STRING_ELT(deparse1(x, 1, SIMPLEDEPARSE), 0));
}

static SEXP asFunction(SEXP x)
{
    SEXP f, pf;
    int n;
    if (isFunction(x)) return x;
    PROTECT(f = allocSExp(CLOSXP));
    SET_CLOENV(f, R_GlobalEnv);
    if (NAMEDCNT_GT_0(x)) PROTECT(x = duplicate(x));
    else PROTECT(x);

    if (isNull(x) || !isList(x)) {
	SET_FORMALS(f, R_NilValue);
	SET_BODY(f, x);
    }
    else {
	n = length(x);
	pf = allocList(n - 1);
	SET_FORMALS(f, pf);
	while (--n) {
	    if (TAG(x) == R_NilValue) {
		SET_TAG(pf, CreateTag(CAR(x)));
		SETCAR(pf, R_MissingArg);
	    }
	    else {
		SETCAR(pf, CAR(x));
		SET_TAG(pf, TAG(x));
	    }
	    pf = CDR(pf);
	    x = CDR(x);
	}
	SET_BODY(f, CAR(x));
    }
    UNPROTECT(2);
    return f;
}

/* Convert to type.  When final type is REALXP or VECSXP, may set 
   R_gradient to gradient (leaves unchanged otherwise). */

static SEXP ascommon(SEXP call, SEXP u, SEXP u_grad, SEXPTYPE type)
{
    /* -> as.vector(..) or as.XXX(.) : coerce 'u' to 'type' : */
    /* code assumes u is protected */

    SEXP v;
    if (type == CLOSXP) {
        return asFunction(u);
    }
    else if (isVector(u) || isList(u) || isLanguage(u)
             || (isSymbol(u) && type == EXPRSXP)) {
        v = u;
        /* this duplication may appear not to be needed in all cases,
           but beware that other code relies on it.
           (E.g  we clear attributes in do_asvector and do_ascharacter.)

           Generally coerceVector will copy over attributes.
        */
        if (type != ANYSXP && TYPEOF(u) != type) {
            v = coerceVector(u, type);
            if (u_grad != R_NilValue) {
                if (TYPEOF(u) == REALSXP && TYPEOF(v) == VECSXP)
                    R_gradient = as_list_gradient (u_grad, LENGTH(v));
                else if (TYPEOF(u) == VECSXP && TYPEOF(v) == REALSXP)
                    R_gradient = as_numeric_gradient (u_grad, LENGTH(v));
            }
        }
        else if (NAMEDCNT_GT_0(u)) {
            v = duplicate(u);
            if (u_grad != R_NilValue) {
                if (TYPEOF(v) == VECSXP || TYPEOF(v) == REALSXP)
                    R_gradient = u_grad;
            }
        }

        /* drop attributes() and class() in some cases for as.pairlist:
           But why?  (And who actually coerces to pairlists?)
         */
        if ((type == LISTSXP) &&
            !(TYPEOF(u) == LANGSXP || TYPEOF(u) == LISTSXP ||
              TYPEOF(u) == EXPRSXP || TYPEOF(u) == VECSXP)) {
            CLEAR_ATTRIB(v);
        }
        return v;
    }
    else if (isSymbol(u) && type == STRSXP)
        return ScalarString(PRINTNAME(u));
    else if (isSymbol(u) && type == SYMSXP)
        return u;
    else if (isSymbol(u) && type == VECSXP) {
        v = allocVector(VECSXP, 1);
        SET_VECTOR_ELT(v, 0, u);
        return v;
    }
    else 
        errorcall(call, _("cannot coerce type '%s' to vector of type '%s'"),
                             type2char(TYPEOF(u)), type2char(type));
}

/* used in attrib.c, eval.c and unique.c */
SEXP asCharacterFactor(SEXP x)
{
    SEXP ans;

    PROTECT(x);
    if( !inherits_CHAR (x, R_factor_CHARSXP) )
        error(_("attempting to coerce non-factor"));

    int i, n = LENGTH(x);
    SEXP labels = getAttrib(x, install("levels"));
    PROTECT(ans = allocVector(STRSXP, n));
    for(i = 0; i < n; i++) {
      int ii = INTEGER(x)[i];
      SET_STRING_ELT(ans, i,
		   (ii == NA_INTEGER) ? NA_STRING
		   : STRING_ELT(labels, ii - 1));
    }
    UNPROTECT(2);
    return ans;
}


/* do_ascharacter implements as.character, as.integer, as.complex, as.logical,
   and as.raw, but not as.double (which handles gradients). */

static SEXP do_ascharacter (SEXP call, SEXP op, SEXP args, SEXP rho, 
                            int variant)
{
    SEXP ans, x;

    int type = STRSXP, op0 = PRIMVAL(op);
    char *name = NULL /* -Wall */;

    check1arg_x (args, call);
    switch(op0) {
	case 0:
	    name = "as.character"; break;
	case 1:
	    name = "as.integer"; type = INTSXP; break;
	case 2:
	    name = "as.double"; type = REALSXP; break;
	case 3:
	    name = "as.complex"; type = CPLXSXP; break;
	case 4:
	    name = "as.logical"; type = LGLSXP; break;
	case 5:
	    name = "as.raw"; type = RAWSXP; break;
    }
    if (DispatchOrEval(call, op, name, args, rho, &ans, 0, 1, variant))
	return(ans);

    /* Method dispatch has failed, we now just */
    /* run the generic internal code */

    checkArity(op, args);
    x = CAR(args);
    if(TYPEOF(x) == type) {
        if (! (variant & VARIANT_PENDING_OK) )
            WAIT_UNTIL_COMPUTED(x);
        if (!HAS_ATTRIB(x)) 
            return x;
        if (NAMEDCNT_EQ_0(x))
            ans = x;
        else {
            ans = duplicate(x);
        }
	CLEAR_ATTRIB(ans);
	return ans;
    }

    WAIT_UNTIL_COMPUTED(x);
    ans = ascommon(call, x, R_NilValue, type);
    CLEAR_ATTRIB(ans);
    return ans;
}


/* Implement as.double/as.real/as.numeric. SPECIAL, so can  handle gradients. */

static SEXP do_asdouble (SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    PROTECT (args = variant & VARIANT_GRADIENT
                      ? evalList_gradient (args, rho, 0, 1, 0)
                      : evalList (args, rho));
    SEXP ans;

    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "as.double", args, rho, &ans, 0, 1, variant)) {
        UNPROTECT(1);
        return ans;
    }

    /* Method dispatch has failed, we now just run the generic internal code */

    checkArity(op, args);

    SEXP x = CAR(args);
    SEXP x_grad = HAS_GRADIENT_IN_CELL(args) ? GRADIENT_IN_CELL(args)
                                             : R_NilValue;

    if (TYPEOF(x) == REALSXP) {
        if (!HAS_ATTRIB(x))
            ans = x;
        else {
            if (NAMEDCNT_EQ_0(x))
                ans = x;
            else {
                ans = duplicate(x);
            }
            CLEAR_ATTRIB(ans);
        }
        if (x_grad != R_NilValue && TYPEOF(x) == REALSXP) {
            R_gradient = x_grad;
            R_variant_result = VARIANT_GRADIENT_FLAG;
        }
        if (! (variant & VARIANT_PENDING_OK) )
            WAIT_UNTIL_COMPUTED(ans);
        UNPROTECT(1);
        return ans;
    }

    WAIT_UNTIL_COMPUTED(x);

    R_gradient = R_NilValue;

    PROTECT (ans = ascommon(call, x, x_grad, REALSXP));

    if (R_gradient != R_NilValue)
        R_variant_result = VARIANT_GRADIENT_FLAG;

    CLEAR_ATTRIB(ans);

    UNPROTECT(2);
    return ans;
}


/* NB: as.vector is used for several other as.xxxx, including
   as.expression, as.list, as.pairlist, as.symbol, (as.single)

   First arg may come with gradient, may be pending. */

static SEXP do_asvector (SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP x, ans;
    int type;

    if (DispatchOrEval(call, op, "as.vector", args, rho, &ans, 0, 1, variant))
        return ans;

    /* Method dispatch has failed, we now just */
    /* run the generic internal code */

    checkArity(op, args);
    x = CAR(args);
    WAIT_UNTIL_COMPUTED(CADR(args));

    if (!isString(CADR(args)) || LENGTH(CADR(args)) != 1)
        errorcall_return(call, R_MSG_mode);
    if (!strcmp("function", (CHAR(STRING_ELT(CADR(args), 0))))) /* ASCII */
        type = CLOSXP;
    else
        type = str2type(CHAR(STRING_ELT(CADR(args), 0))); /* ASCII */

    SEXP x_grad = HAS_GRADIENT_IN_CELL(args) ? GRADIENT_IN_CELL(args)
                                             : R_NilValue;

    /* "any" case added in 2.13.0 */
    if (type == ANYSXP || TYPEOF(x) == type) {
        switch(TYPEOF(x)) {
        case LGLSXP:
        case INTSXP:
        case REALSXP:
        case CPLXSXP:
        case STRSXP:
        case RAWSXP:
            if (!HAS_ATTRIB(x))
                ans = x;
            else {
                if (NAMEDCNT_EQ_0(x))
                    ans = x;
                else {
                    ans = duplicate(x);
                }
                CLEAR_ATTRIB(ans);
            }
            if (x_grad != R_NilValue && TYPEOF(x) == REALSXP) {
                R_gradient = x_grad;
                R_variant_result = VARIANT_GRADIENT_FLAG;
            }
            if (! (variant & VARIANT_PENDING_OK) )
                WAIT_UNTIL_COMPUTED(ans);
            return ans;
        case EXPRSXP:
        case VECSXP:
            if (x_grad != R_NilValue && TYPEOF(x) == VECSXP) {
                R_gradient = x_grad;
                R_variant_result = VARIANT_GRADIENT_FLAG;
            }
            return x;
        default:
            ;
        }
    }

    WAIT_UNTIL_COMPUTED(x);

    if(IS_S4_OBJECT(x) && TYPEOF(x) == S4SXP) {
        SEXP v = R_getS4DataSlot(x, ANYSXP);
        if(v == R_NilValue)
            error(_("no method for coercing this S4 class to a vector"));
        x = v;
    }

    switch (type) {/* only those are valid : */
    case SYMSXP: /* for as.symbol */
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case STRSXP:
    case EXPRSXP: /* for as.expression */
    case VECSXP: /* list */
    case LISTSXP:/* for as.pairlist */
    case CLOSXP: /* non-primitive function */
    case RAWSXP:
    case ANYSXP: /* any */
        break;
    default:
        errorcall_return(call, R_MSG_mode);
    }

    R_gradient = R_NilValue;

    PROTECT (ans = ascommon(call, x, x_grad, type));

    if (R_gradient != R_NilValue)
        R_variant_result = VARIANT_GRADIENT_FLAG;

    switch (TYPEOF(ans)) { /* keep attributes for these: */
    case NILSXP: /* doesn't have any */
    case LISTSXP: /* but ascommon fiddled */
    case LANGSXP:
    case VECSXP:
    case EXPRSXP:
        break;
    default:
        CLEAR_ATTRIB(ans);
        break;
    }

    UNPROTECT(1);
    return ans;
}


static SEXP do_asfunction(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP arglist, envir, names, pargs, body;
    int i, n;

    checkArity(op, args);

    /* Check the arguments; we need a list and environment. */

    arglist = CAR(args);
    if (!isNewList(arglist))
	errorcall(call, _("list argument expected"));

    envir = CADR(args);
    if (isNull(envir)) {
	error(_("use of NULL environment is defunct"));
	envir = R_BaseEnv;
    } else
    if (!isEnvironment(envir))
	errorcall(call, _("invalid environment"));

    n = length(arglist);
    if (n < 1)
	errorcall(call, _("argument must have length at least 1"));
    PROTECT(names = getAttrib(arglist, R_NamesSymbol));
    PROTECT(pargs = args = allocList(n - 1));
    for (i = 0; i < n - 1; i++) {
	SETCAR(pargs, VECTOR_ELT(arglist, i));
	if (names != R_NilValue && *CHAR(STRING_ELT(names, i)) != '\0') /* ASCII */
	    SET_TAG(pargs, install_translated (STRING_ELT(names,i)));
	else
	    SET_TAG_NIL(pargs);
	pargs = CDR(pargs);
    }
    CheckFormals(args);
    PROTECT(body = VECTOR_ELT(arglist, n-1));
    /* the main (only?) thing to rule out is body being
       a function already. If we test here then
       mkCLOSXP can continue to overreact when its
       test fails (PR#1880, 7535, 7702) */
    if(isList(body) || isLanguage(body) || isSymbol(body)
       || isExpression(body) || isVector(body) || isByteCode(body)
       )
	    args =  mkCLOSXP(args, body, envir);
    else
	    errorcall(call, _("invalid body for function"));
    UNPROTECT(3); /* body, pargs, names */
    return args;
}


/* primitive */
static SEXP do_ascall(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP ap, ans, names;
    int i, n;

    checkArity(op, args);
    check1arg_x (args, call);

    args = CAR(args);
    switch (TYPEOF(args)) {
    case LANGSXP:
	ans = args;
	break;
    case VECSXP:
    case EXPRSXP:
	if(0 == (n = length(args)))
	    errorcall(call, _("invalid length 0 argument"));
	PROTECT(names = getAttrib(args, R_NamesSymbol));
	PROTECT(ap = ans = allocList(n));
	for (i = 0; i < n; i++) {
	    SETCAR(ap, VECTOR_ELT(args, i));
	    if (names != R_NilValue && !StringBlank(STRING_ELT(names, i)))
		SET_TAG(ap, install_translated (STRING_ELT(names,i)));
	    ap = CDR(ap);
	}
	UNPROTECT(2); /* ap, names */
	break;
    case LISTSXP:
	ans = duplicate(args);
	break;
    default:
	errorcall(call, _("invalid argument list"));
	ans = R_NilValue;
    }
    SET_TYPEOF(ans, LANGSXP);
    SET_TAG_NIL(ans);
    return ans;
}


/* int, not Rboolean, for NA_LOGICAL : */
int asLogical(SEXP x)
{
    int warn = 0;

    if (isVectorAtomic(x)) {
	if (LENGTH(x) < 1)
	    return NA_LOGICAL;
	switch (TYPEOF(x)) {
	case LGLSXP:
	    return LOGICAL(x)[0];
	case INTSXP:
	    return LogicalFromInteger(INTEGER(x)[0], &warn);
	case REALSXP:
	    return LogicalFromReal(REAL(x)[0], &warn);
	case CPLXSXP:
	    return LogicalFromComplex(COMPLEX(x)[0], &warn);
	case STRSXP:
	    return LogicalFromString(STRING_ELT(x, 0), &warn);
	case RAWSXP:
	    return LogicalFromInteger((int)RAW(x)[0], &warn);
	default:
	    UNIMPLEMENTED_TYPE("asLogical", x);
	}
    } else if(TYPEOF(x) == CHARSXP) {
	    return LogicalFromString(x, &warn);
    }
    return NA_LOGICAL;
}

int asInteger(SEXP x)
{
    int warn = 0, res;

    if (isVectorAtomic(x) && LENGTH(x) >= 1) {
	switch (TYPEOF(x)) {
	case LGLSXP:
	    return IntegerFromLogical(LOGICAL(x)[0], &warn);
	case INTSXP:
	    return INTEGER(x)[0];
	case REALSXP:
	    res = IntegerFromReal(REAL(x)[0], &warn);
	    CoercionWarning(warn);
	    return res;
	case CPLXSXP:
	    res = IntegerFromComplex(COMPLEX(x)[0], &warn);
	    CoercionWarning(warn);
	    return res;
	case STRSXP:
	    res = IntegerFromString(STRING_ELT(x, 0), &warn);
	    CoercionWarning(warn);
	    return res;
	default:
	    UNIMPLEMENTED_TYPE("asInteger", x);
	}
    } else if(TYPEOF(x) == CHARSXP) {
	res = IntegerFromString(x, &warn);
	CoercionWarning(warn);
	return res;
    }
    return NA_INTEGER;
}

double asReal(SEXP x)
{
    int warn = 0;
    double res;

    if (isVectorAtomic(x) && LENGTH(x) >= 1) {
	switch (TYPEOF(x)) {
	case LGLSXP:
	    res = RealFromLogical(LOGICAL(x)[0], &warn);
	    CoercionWarning(warn);
	    return res;
	case INTSXP:
	    res = RealFromInteger(INTEGER(x)[0], &warn);
	    CoercionWarning(warn);
	    return res;
	case REALSXP:
	    return REAL(x)[0];
	case CPLXSXP:
	    res = RealFromComplex(COMPLEX(x)[0], &warn);
	    CoercionWarning(warn);
	    return res;
	case STRSXP:
	    res = RealFromString(STRING_ELT(x, 0), &warn);
	    CoercionWarning(warn);
	    return res;
	default:
	    UNIMPLEMENTED_TYPE("asReal", x);
	}
    } else if(TYPEOF(x) == CHARSXP) {
	res = RealFromString(x, &warn);
	CoercionWarning(warn);
	return res;
    }
    return NA_REAL;
}

Rcomplex asComplex(SEXP x)
{
    int warn = 0;
    Rcomplex z;

    if (isVectorAtomic(x) && LENGTH(x) >= 1) {
	switch (TYPEOF(x)) {
	case LGLSXP:
	    z = ComplexFromLogical(LOGICAL(x)[0], &warn);
	    CoercionWarning(warn);
	    return z;
	case INTSXP:
	    z = ComplexFromInteger(INTEGER(x)[0], &warn);
	    CoercionWarning(warn);
	    return z;
	case REALSXP:
	    z = ComplexFromReal(REAL(x)[0], &warn);
	    CoercionWarning(warn);
	    return z;
	case CPLXSXP:
	    return COMPLEX(x)[0];
	case STRSXP:
	    z = ComplexFromString(STRING_ELT(x, 0), &warn);
	    CoercionWarning(warn);
	    return z;
	default:
	    UNIMPLEMENTED_TYPE("asComplex", x);
	}
    } else if(TYPEOF(x) == CHARSXP) {
	z = ComplexFromString(x, &warn);
	CoercionWarning(warn);
	return z;
    }
    z.r = NA_REAL;
    z.i = NA_REAL;
    return z;
}


/* return the type (= "detailed mode") of the SEXP */
static SEXP do_typeof(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    checkArity(op, args);
    return ScalarString(type2str(TYPEOF(CAR(args))));
}

/* Define many of the <primitive> "is.xxx" functions :
   Note that  isNull, isNumeric, etc are defined in util.c or Rinlinedfuns.h
*/
static SEXP do_fast_is(SEXP call, SEXP op, SEXP arg, SEXP rho, int variant)
{
    int log_ans;

    switch (PRIMVAL(op)) {
    case NILSXP:	/* is.null */
	log_ans = isNull(arg);
	break;
    case LGLSXP:	/* is.logical */
	log_ans = (TYPEOF(arg) == LGLSXP);
	break;
    case INTSXP:	/* is.integer */
	log_ans = (TYPEOF(arg) == INTSXP)
	    && !inherits_CHAR (arg, R_factor_CHARSXP);
	break;
    case REALSXP:	/* is.double == is.real */
	log_ans = (TYPEOF(arg) == REALSXP);
	break;
    case CPLXSXP:	/* is.complex */
	log_ans = (TYPEOF(arg) == CPLXSXP);
	break;
    case STRSXP:	/* is.character */
	log_ans = (TYPEOF(arg) == STRSXP);
	break;
    case SYMSXP:	/* is.symbol === is.name */
	if(IS_S4_OBJECT(arg) && (TYPEOF(arg) == S4SXP)) {
	    SEXP dot_xData = R_getS4DataSlot(arg, SYMSXP);
	    log_ans = (TYPEOF(dot_xData) == SYMSXP);
	}
	else
	    log_ans = (TYPEOF(arg) == SYMSXP);
	break;
    case ENVSXP:	/* is.environment */
	if(IS_S4_OBJECT(arg) && (TYPEOF(arg) == S4SXP)) {
	    SEXP dot_xData = R_getS4DataSlot(arg, ENVSXP);
	    log_ans = (TYPEOF(dot_xData) == ENVSXP);
	}
	else
	    log_ans = (TYPEOF(arg) == ENVSXP);
	break;
    case VECSXP:	/* is.list */
	log_ans = (TYPEOF(arg) == VECSXP ||
			   TYPEOF(arg) == LISTSXP);
	break;
    case LISTSXP:	/* is.pairlist */
	log_ans = (TYPEOF(arg) == LISTSXP ||
			   TYPEOF(arg) == NILSXP);/* pairlist() -> NULL */
	break;
    case EXPRSXP:	/* is.expression */
	log_ans = TYPEOF(arg) == EXPRSXP;
	break;
    case RAWSXP:	/* is.raw */
	log_ans = (TYPEOF(arg) == RAWSXP);
	break;

    case 50:		/* is.object */
	log_ans = OBJECT(arg);
	break;
/* no longer used: is.data.frame is R code
    case 80:
	log_ans = isFrame(arg);
	break;
*/
    case 100:		/* is.numeric */
	log_ans = 
          isNumeric(arg) && !isLogical(arg);  /* isNumeric excludes factors */
	break;
    case 101:		/* is.matrix */
	log_ans = isMatrix(arg);
	break;
    case 102:		/* is.array */
	log_ans = isArray(arg);
	break;

    case 200:		/* is.atomic */
	switch(TYPEOF(arg)) {
	case NILSXP:
	    /* NULL is atomic (S compatibly), but not in isVectorAtomic(.) */
	case CHARSXP:
	case LGLSXP:
	case INTSXP:
	case REALSXP:
	case CPLXSXP:
	case STRSXP:
	case RAWSXP:
	    log_ans = 1;
	    break;
	default:
	    log_ans = 0;
	    break;
	}
	break;
    case 201:		/* is.recursive */
	switch(TYPEOF(arg)) {
	case VECSXP:
	case LISTSXP:
	case CLOSXP:
	case ENVSXP:
	case PROMSXP:
	case LANGSXP:
	case SPECIALSXP:
	case BUILTINSXP:
	case DOTSXP:
	case ANYSXP:
	case EXPRSXP:
	case EXTPTRSXP:
	case BCODESXP:
	case WEAKREFSXP:
	    log_ans = 1;
	    break;
	default:
	    log_ans = 0;
	    break;
	}
	break;

    case 300:		/* is.call */
	log_ans = TYPEOF(arg) == LANGSXP;
	break;
    case 301:		/* is.language */
	log_ans = (TYPEOF(arg) == SYMSXP || TYPEOF(arg) == LANGSXP 
                                         || TYPEOF(arg) == EXPRSXP);
	break;
    case 302:		/* is.function */
	log_ans = isFunction(arg);
	break;

    case 999:		/* is.single */
	errorcall(call, _("type \"single\" unimplemented in R"));
    default:
	errorcall(call, _("unimplemented predicate"));
    }

    return ScalarLogicalMaybeConst(log_ans);
}

static SEXP do_is(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    checkArity(op, args);
    check1arg_x (args, call);

    /* These are all builtins, so we do not need to worry about
       evaluating arguments in DispatchOrEval */
    if (PRIMVAL(op) >= 100 && PRIMVAL(op) <= 102) { 
        /* update FASTFUNTAB at end of this file if above range expanded */
        if (isObject(CAR(args))) {
            SEXP ans;
            /* This used CHAR(PRINTNAME(CAR(call))), but that is not
               necessarily correct, e.g. when called from lapply() */
            const char *nm;
            switch(PRIMVAL(op)) {
            case 100: nm = "is.numeric"; break;
            case 101: nm = "is.matrix"; break;
            case 102: nm = "is.array"; break;
            default: nm = ""; /* -Wall */
            }
            if(DispatchOrEval(call, op, nm, args, rho, &ans, 0, 1, variant))
                return(ans);
        }
    }

    return do_fast_is (call, op, CAR(args), rho, variant);
}

/* What should is.vector do ?
 * In S, if an object has no attributes it is a vector, otherwise it isn't.
 * It seems to make more sense to check for a dim attribute.
 */

static SEXP do_isvector(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    int log_ans;
    SEXP a, x;
    const char *stype;

    checkArity(op, args);
    x = CAR(args);
    if (!isString(CADR(args)) || LENGTH(CADR(args)) != 1)
	errorcall_return(call, R_MSG_mode);

    stype = CHAR(STRING_ELT(CADR(args), 0)); /* ASCII */

    if (streql(stype, "any")) {
	/* isVector is inlined, means atomic or VECSXP or EXPRSXP */
	log_ans = isVector(x);
    } 
    else if (streql(stype, "numeric")) {
	log_ans = (isNumeric(x) && !isLogical(x));
    }
    /* So this allows any type, including undocumented ones such as
       "closure", but not aliases such as "name" and "function". */
    else if (streql(stype, type2char(TYPEOF(x)))) {
	log_ans = 1;
    }
    else
	log_ans = 0;

    /* We allow a "names" attribute on any vector. */
    if (log_ans && ATTRIB(CAR(args)) != R_NilValue) {
	a = ATTRIB(CAR(args));
	while(a != R_NilValue) {
	    if (TAG(a) != R_NamesSymbol) {
		log_ans = 0;
		break;
	    }
	    a = CDR(a);
	}
    }

    return ScalarLogicalMaybeConst(log_ans);
}

#if __AVX2__ && !defined(DISABLE_AVX_CODE)
#include <immintrin.h>
#endif

/* Prelude & postlude for fast versions of is.xxx functions.  For
   VARIANT_AND and VARIANT_OR of atomic vectors (or NULL), for which
   the result will be a scalar logical, the value is returned via
   ScalarLogicalMaybeConst - the ...fast... routine should store the
   result in 'ret' and then do a 'goto vret'.  Otherwise, 'lans' is
   set to the place to store the logical vector result; after the
   ...fast... routines stores the result in it, it should let control
   flow into IS_POSTLUDE. */

#define IS_PRELUDE \
    POP_IF_TOP_OF_STACK(x); \
    SEXP ans = R_NilValue; \
    int n = length(x);   /* Length of argument & answer */ \
    int scalar_ans;      /* Answer location if arg scalar, no dim, no names */ \
    int * restrict lans; /* Pointer to where answer is stored */ \
    int ret;             /* Scalar result value for length 1 or variant ret */ \
    if (x != R_NilValue && !isVectorAtomic(x) \
         || VARIANT_KIND(variant) != VARIANT_AND \
             && VARIANT_KIND(variant) != VARIANT_OR) { \
        SEXP dims, names; \
        dims = names = R_NilValue; \
        if (isVector(x)) { \
	    names = getAttrib(x, \
                              isArray(x) ? R_DimNamesSymbol : R_NamesSymbol); \
            if (names != R_NilValue) PROTECT(names); \
	    dims = getDimAttrib(x); \
            if (dims != R_NilValue) PROTECT(dims); \
        } \
        if (n != 1 || dims != R_NilValue || names != R_NilValue) { \
            ans = allocVector(LGLSXP, n); \
            if (dims != R_NilValue) { \
               setAttrib (ans, R_DimSymbol, dims); \
               UNPROTECT(1); \
            } \
            if (names != R_NilValue) { \
                setAttrib (ans, isArray(x) ? R_DimNamesSymbol : R_NamesSymbol, \
                           names); \
                UNPROTECT(1); \
            } \
            PROTECT(ans); \
            lans = LOGICAL(ans); \
        } \
        else \
            lans = &scalar_ans; \
    }

#define IS_POSTLUDE \
    if (ans != R_NilValue) { \
        UNPROTECT (1); \
        return ans; \
    } \
    ret = *lans;  /* Scalar return value */\
  vret: \
    return ScalarLogicalMaybeConst(ret);

static SEXP do_fast_isna (SEXP call, SEXP op, SEXP x, SEXP rho, int variant)
{
    IS_PRELUDE

    int i;
    switch (TYPEOF(x)) {
    case NILSXP:
    case RAWSXP:  /* no such thing as a raw NA */
        if (VARIANT_KIND(variant) == VARIANT_AND) { ret = n==0;  goto vret; }
        if (VARIANT_KIND(variant) == VARIANT_OR)  { ret = FALSE; goto vret; }
	for (i = 0; i < n; i++)
	    lans[i] = 0;
	break;
    case LGLSXP:  /* assumes logical same as integer */
    case INTSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (INTEGER(x)[i] != NA_INTEGER) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (INTEGER(x)[i] == NA_INTEGER) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
#       if !__AVX2__ || defined(DISABLE_AVX_CODE)
            for (i = 0; i < n; i++)
                lans[i] = (INTEGER(x)[i] == NA_INTEGER);
#       else
        {
            i = 0;
            if ((SGGC_ALIGN_FORWARD & 8) && i < n-1) {
                lans[i+0] = (INTEGER(x)[i+0] == NA_INTEGER);
                lans[i+1] = (INTEGER(x)[i+1] == NA_INTEGER);
                i += 2;
            }
            if ((SGGC_ALIGN_FORWARD & 16) && i < n-3) {
                lans[i+0] = (INTEGER(x)[i+0] == NA_INTEGER);
                lans[i+1] = (INTEGER(x)[i+1] == NA_INTEGER);
                lans[i+2] = (INTEGER(x)[i+2] == NA_INTEGER);
                lans[i+3] = (INTEGER(x)[i+3] == NA_INTEGER);
                i += 4;
            }
            if (i < n-7) {
                __m256i na = _mm256_set1_epi32 (NA_INTEGER);
                __m256i mask = _mm256_set1_epi32 (1);
                do {
                    __m256i r = _mm256_load_si256 ((__m256i*)(INTEGER(x)+i));
                    r = _mm256_cmpeq_epi32 (na, r);
                    r = _mm256_and_si256 (mask, r);
                    _mm256_store_si256 ((__m256i*)(lans+i), r);
                    i += 8;
                } while (i < n-7);
            }
            while (i < n) {
                lans[i] = (INTEGER(x)[i] == NA_INTEGER);
                i += 1;
            }
        }
#       endif
	break;
    case REALSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (!ISNAN(REAL(x)[i])) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            i = 0;
            if (n&1) {
                if (ISNAN(REAL(x)[i])) { ret = TRUE; goto vret; }
                i += 1;
            }
            if (n&2) {
                if (MAY_BE_NAN2 (REAL(x)[i], REAL(x)[i+1]) 
                     && (ISNAN (REAL(x)[i])
                          || ISNAN (REAL(x)[i+1]))) { ret = TRUE; goto vret; }
                i += 2;
            }
            for ( ; i < n; i += 4)
                if (MAY_BE_NAN4 (REAL(x)[i], REAL(x)[i+1],
                                 REAL(x)[i+2], REAL(x)[i+3])
                     && (ISNAN (REAL(x)[i]) 
                          || ISNAN (REAL(x)[i+1]) 
                          || ISNAN (REAL(x)[i+2]) 
                          || ISNAN (REAL(x)[i+3]))) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
#       if !__AVX2__ || defined(DISABLE_AVX_CODE)
            for (i = 0; i < n; i++)
                lans[i] = ISNAN_value (REAL(x)[i]);
#       else
        {
            i = 0;
            if ((SGGC_ALIGN_FORWARD & 8) && i < n) {
                lans[i] = ISNAN_value (REAL(x)[i]);
                i += 1;
            }
            if ((SGGC_ALIGN_FORWARD & 16) && i < n-1) {
                lans[i] = ISNAN_value (REAL(x)[i]);
                lans[i+1] = ISNAN_value (REAL(x)[i+1]);
                i += 2;
            }
            if (i < n-7) {
                __m256i tmp1 = _mm256_set1_epi64x ((uint64_t)0x7ff << 52);
                __m256i tmp2 = _mm256_set1_epi64x (~((uint64_t)1<<63));
                do {
                    __m256i r0 = _mm256_load_si256 ((__m256i*)(REAL(x)+i));
                    r0 = _mm256_and_si256 (tmp2, r0);
                    r0 = _mm256_sub_epi64 (tmp1, r0);
                    r0 = _mm256_srli_epi64 (r0, 63);
                    __m256i r1 = _mm256_load_si256 ((__m256i*)(REAL(x)+i+4));
                    r1 = _mm256_and_si256 (tmp2, r1);
                    r1 = _mm256_sub_epi64 (tmp1, r1);
                    r1 = _mm256_andnot_si256 (tmp2, r1);
                    r1 = _mm256_srli_epi64 (r1, 31);
                    r0 = _mm256_or_si256 (r0, r1);
                    r0 = _mm256_shuffle_epi32 (r0, 0xd8);
                    r0 = _mm256_permute4x64_epi64 (r0, 0xd8);
                    _mm256_storeu_si256 ((__m256i*)(lans+i), r0);
                    i += 8;
                } while (i < n-7);
            }
            while (i < n) {
                lans[i] = ISNAN_value (REAL(x)[i]);
                i += 1;
            }
        }
#       endif
	break;
    case CPLXSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (! (ISNAN(COMPLEX(x)[i].r) 
                    || ISNAN(COMPLEX(x)[i].i))) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (ISNAN(COMPLEX(x)[i].r) 
                 || ISNAN(COMPLEX(x)[i].i)) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
	for (i = 0; i < n; i++)
	    lans[i] = (ISNAN_value(COMPLEX(x)[i].r) |
			       ISNAN_value(COMPLEX(x)[i].i));
	break;
    case STRSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (STRING_ELT(x,i) != NA_STRING) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (STRING_ELT(x,i) == NA_STRING) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
#       if !__AVX2__ || defined(DISABLE_AVX_CODE)
            for (i = 0; i < n; i++)
                lans[i] = (STRING_ELT(x, i) == NA_STRING);
#       elif USE_COMPRESSED_POINTERS
        {
            i = 0;
            if ((SGGC_ALIGN_FORWARD & 8) && i < n-1) {
                lans[i+0] = (STRING_ELT (x, i+0) == NA_STRING);
                lans[i+1] = (STRING_ELT (x, i+1) == NA_STRING);
                i += 2;
            }
            if ((SGGC_ALIGN_FORWARD & 16) && i < n-3) {
                lans[i+0] = (STRING_ELT (x, i+0) == NA_STRING);
                lans[i+1] = (STRING_ELT (x, i+1) == NA_STRING);
                lans[i+2] = (STRING_ELT (x, i+2) == NA_STRING);
                lans[i+3] = (STRING_ELT (x, i+3) == NA_STRING);
                i += 4;
            }
            if (i < n-7) {
                __m256i na = _mm256_set1_epi32 ((uint32_t)NA_STRING);
                __m256i mask = _mm256_set1_epi32 (1);
                do {
                    __m256i r = _mm256_load_si256
                                  ((__m256i*)((SEXP*)DATAPTR(x)+i));
                    r = _mm256_cmpeq_epi32 (na, r);
                    r = _mm256_and_si256 (mask, r);
                    _mm256_store_si256 ((__m256i*)(lans+i), r);
                    i += 8;
                } while (i < n-7);
            }
            while (i < n) {
                lans[i] = (STRING_ELT (x, i) == NA_STRING);
                i += 1;
            }
        }
#       else  /* string elements are uncompressed 64-bit pointers */
        {
            i = 0;
            if ((SGGC_ALIGN_FORWARD & 8) && i < n) {
                lans[i] = (STRING_ELT (x, i) == NA_STRING);
                i += 1;
            }
            if ((SGGC_ALIGN_FORWARD & 16) && i < n-1) {
                lans[i+0] = (STRING_ELT (x, i+0) == NA_STRING);
                lans[i+1] = (STRING_ELT (x, i+1) == NA_STRING);
                i += 2;
            }
            if (i < n-7) {
                __m256i na = _mm256_set1_epi64x ((uint64_t)NA_STRING);
                __m256i mask0 = _mm256_set1_epi64x ((uint64_t)1);
                __m256i mask1 = _mm256_set1_epi64x ((uint64_t)1<<32);
                do {
                    __m256i r0 = _mm256_load_si256 
                                   ((__m256i*)((SEXP*)DATAPTR(x)+i));
                    r0 = _mm256_cmpeq_epi64 (na, r0);
                    r0 = _mm256_and_si256 (mask0, r0);
                    __m256i r1 = _mm256_load_si256
                                   ((__m256i*)((SEXP*)DATAPTR(x)+i+4));
                    r1 = _mm256_cmpeq_epi64 (na, r1);
                    r1 = _mm256_and_si256 (mask1, r1);
                    r0 = _mm256_or_si256 (r0, r1);
                    r0 = _mm256_shuffle_epi32 (r0, 0xd8);
                    r0 = _mm256_permute4x64_epi64 (r0, 0xd8);
                    _mm256_storeu_si256 ((__m256i*)(lans+i), r0);
                    i += 8;
                } while (i < n-7);
            }
            while (i < n) {
                lans[i] = (STRING_ELT (x, i) == NA_STRING);
                i += 1;
            }
        }
#       endif
	break;

/* Same code for LISTSXP and VECSXP : */
#define LIST_VEC_NA(s)							\
	if (!isVector(s) || length(s) != 1)				\
		lans[i] = 0;						\
	else {								\
		switch (TYPEOF(s)) {					\
		case LGLSXP:						\
		case INTSXP:						\
		    lans[i] = (INTEGER(s)[0] == NA_INTEGER);		\
		    break;						\
		case REALSXP:						\
		    lans[i] = ISNAN(REAL(s)[0]);			\
		    break;						\
		case STRSXP:						\
		    lans[i] = (STRING_ELT(s, 0) == NA_STRING);		\
		    break;						\
		case CPLXSXP:						\
		    lans[i] = (ISNAN(COMPLEX(s)[0].r)			\
				       || ISNAN(COMPLEX(s)[0].i));	\
		    break;						\
		default:						\
		    lans[i] = 0;					\
		}							\
	}

    case LISTSXP:
	for (i = 0; i < n; i++) {
	    LIST_VEC_NA(CAR(x));
	    x = CDR(x);
	}
	break;
    case VECSXP:
	for (i = 0; i < n; i++) {
	    SEXP s = VECTOR_ELT(x, i);
	    LIST_VEC_NA(s);
	}
	break;
    default:
	warningcall(call, 
                    _("%s() applied to non-(list or vector) of type '%s'"),
		    "is.na", type2char(TYPEOF(x)));
	for (i = 0; i < n; i++)
	    lans[i] = 0;
    }

    IS_POSTLUDE
}

static SEXP do_isna (SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP ans;

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "is.na", args, rho, &ans, 0, 1, variant))
	return(ans);

    return do_fast_isna (call, op, CAR(args), rho, variant);
}

static SEXP do_fast_isnan (SEXP call, SEXP op, SEXP x, SEXP rho, int variant)
{
    IS_PRELUDE

    int i;

    switch (TYPEOF(x)) {
    case STRSXP:
    case RAWSXP:
    case NILSXP:
    case LGLSXP:
    case INTSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) { ret = n==0;  goto vret; }
        if (VARIANT_KIND(variant) == VARIANT_OR)  { ret = FALSE; goto vret; }
	for (i = 0; i < n; i++)
	    lans[i] = 0;
	break;
    case REALSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (!ISNAN_NOT_NA(REAL(x)[i])) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (ISNAN_NOT_NA(REAL(x)[i])) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++)
            lans[i] = R_IsNaN(REAL(x)[i]);
        break;
    case CPLXSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (! (ISNAN_NOT_NA(COMPLEX(x)[i].r)
                  || ISNAN_NOT_NA(COMPLEX(x)[i].i))) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (ISNAN_NOT_NA(COMPLEX(x)[i].r)
                 || ISNAN_NOT_NA(COMPLEX(x)[i].i)) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++)
            lans[i] = (ISNAN_NOT_NA(COMPLEX(x)[i].r) ||
                               ISNAN_NOT_NA(COMPLEX(x)[i].i));
        break;
    default:
        errorcall(call, 
                  _("default method not implemented for type '%s'"), 
                  type2char(TYPEOF(x)));
    }

    IS_POSTLUDE
}

static SEXP do_isnan (SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP ans;

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "is.nan", args, rho, &ans, 0, 1, variant))
	return(ans);

    return do_fast_isnan (call, op, CAR(args), rho, variant);
}

static SEXP do_fast_isfinite (SEXP call, SEXP op, SEXP x, SEXP rho, int variant)
{
    IS_PRELUDE

    int i;

    switch (TYPEOF(x)) {
    case STRSXP:
    case RAWSXP:
    case NILSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) { ret = n==0;  goto vret; }
        if (VARIANT_KIND(variant) == VARIANT_OR)  { ret = FALSE; goto vret; }
	for (i = 0; i < n; i++)
	    lans[i] = 0;
	break;
    case LGLSXP:
    case INTSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (INTEGER(x)[i] == NA_INTEGER) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (INTEGER(x)[i] != NA_INTEGER) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++)
            lans[i] = INTEGER(x)[i] != NA_INTEGER;
        break;
    case REALSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (!R_FINITE(REAL(x)[i])) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (R_FINITE(REAL(x)[i])) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++)
            lans[i] = R_FINITE(REAL(x)[i]);
        break;
    case CPLXSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (! (R_FINITE(COMPLEX(x)[i].r)
                    && R_FINITE(COMPLEX(x)[i].i))) { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (R_FINITE(COMPLEX(x)[i].r)
                 && R_FINITE(COMPLEX(x)[i].i)) { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++)
            lans[i] = R_FINITE(COMPLEX(x)[i].r)
                               && R_FINITE(COMPLEX(x)[i].i);
        break;
    default:
        errorcall(call, 
                  _("default method not implemented for type '%s'"), 
                  type2char(TYPEOF(x)));
    }

    IS_POSTLUDE
}

static SEXP do_isfinite (SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP ans;

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "is.finite", args, rho, &ans, 0, 1, variant))
	return(ans);

    return do_fast_isfinite (call, op, CAR(args), rho, variant);
}

static SEXP do_fast_isinfinite (SEXP call, SEXP op, SEXP x, SEXP rho, 
                                int variant)
{
    IS_PRELUDE

    double xr, xi;
    int i;

    switch (TYPEOF(x)) {
    case STRSXP:
    case RAWSXP:
    case NILSXP:
    case LGLSXP:
    case INTSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) { ret = n==0;  goto vret; }
        if (VARIANT_KIND(variant) == VARIANT_OR)  { ret = FALSE; goto vret; }
	for (i = 0; i < n; i++)
	    lans[i] = 0;
	break;
    case REALSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++)
                if (!R_INFINITE(REAL(x)[i]))
                    { ret = FALSE; goto vret; }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++)
                if (R_INFINITE(REAL(x)[i]))
                    { ret = TRUE; goto vret; }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++)
            lans[i] = R_INFINITE(REAL(x)[i]);
        break;
    case CPLXSXP:
        if (VARIANT_KIND(variant) == VARIANT_AND) {
            for (i = 0; i < n; i++) {
                xr = COMPLEX(x)[i].r;
                xi = COMPLEX(x)[i].i;
                if (!R_INFINITE(xr) 
                 && !R_INFINITE(xi)) { ret = FALSE; goto vret; }
            }
            ret = TRUE; goto vret;
        }
        if (VARIANT_KIND(variant) == VARIANT_OR) {
            for (i = 0; i < n; i++) {
                xr = COMPLEX(x)[i].r;
                xi = COMPLEX(x)[i].i;
                if (R_INFINITE(xr) || R_INFINITE(xi)) { ret = TRUE; goto vret; }
            }
            ret = FALSE; goto vret;
        }
        for (i = 0; i < n; i++) {
            xr = COMPLEX(x)[i].r;
            xi = COMPLEX(x)[i].i;
            lans[i] = R_INFINITE(xr) || R_INFINITE(xi);
        }
        break;
    default:
        errorcall(call, 
                  _("default method not implemented for type '%s'"), 
                  type2char(TYPEOF(x)));
    }

    IS_POSTLUDE
}

static SEXP do_isinfinite (SEXP call, SEXP op, SEXP args, SEXP rho, 
                           int variant) 
{
    SEXP ans;

    checkArity(op, args);
    check1arg_x (args, call);

    if (DispatchOrEval(call, op, "is.infinite", args, rho, &ans, 0, 1, variant))
	return(ans);

    return do_fast_isinfinite (call, op, CAR(args), rho, variant);
}

/* This is a primitive SPECIALSXP */
static SEXP do_call(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP rest, evargs, rfun;

    if (length(args) < 1) errorcall(call, _("'name' is missing"));
    check1arg(args, call, "name");
    PROTECT(rfun = eval(CAR(args), rho));
    /* zero-length string check used to be here but install gives
       better error message.
     */
    if (!isString(rfun) || length(rfun) != 1)
	errorcall_return(call, _("first argument must be a character string"));
    PROTECT(rfun = install_translated (STRING_ELT(rfun,0)));
    PROTECT(evargs = duplicate(CDR(args)));
    for (rest = evargs; rest != R_NilValue; rest = CDR(rest))
	SETCAR(rest, eval(CAR(rest), rho));
    rfun = LCONS(rfun, evargs);
    UNPROTECT(3);
    R_Visible = TRUE;
    return (rfun);
}

static SEXP do_docall(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP c, fun, names, envir;
    int i, n;
    /* RCNTXT *cptr; */

    checkArity(op, args);

    fun = CAR(args);
    envir = CADDR(args);
    args = CADR(args);

    /* must be a string or a function:
       zero-length string check used to be here but install gives
       better error message.
     */
    if( !(isString(fun) && length(fun) == 1) && !isFunction(fun) )
	error(_("'what' must be a character string or a function"));

    if (!isNull(args) && !isNewList(args))
	error(_("'args' must be a list"));

    if (!isEnvironment(envir))
	error(_("'envir' must be an environment"));

    n = length(args);
    PROTECT(names = getAttrib(args, R_NamesSymbol));

    PROTECT(c = call = allocList(n + 1));
    SET_TYPEOF(c, LANGSXP);
    if( isString(fun) )
	SETCAR(c, install_translated (STRING_ELT(fun,0)));
    else
	SETCAR(c, fun);
    c = CDR(c);
    for (i = 0; i < n; i++) {
	SETCAR(c, VECTOR_ELT(args, i));
	if (ItemName(names, i) != R_NilValue)
	    SET_TAG(c, install_translated (ItemName(names,i)));
	c = CDR(c);
    }
    call = eval(call, envir);

    UNPROTECT(2); /* c, names */
    return call;
}


/*
   do_substitute has two arguments, an expression and an environment
   (optional).	Symbols found in the expression are substituted with their
   values as found in the environment.	There is no inheritance so only
   the supplied environment is searched. If no environment is specified
   the environment in which substitute was called is used.  If the
   specified environment is R_GlobalEnv it is converted to R_NilValue, for
   historical reasons. In substitute(), R_NilValue signals that no
   substitution should be done, only extraction of promise expressions.
   Arguments to do_substitute should not be evaluated.
*/

SEXP substitute(SEXP lang, SEXP rho)
{
    SEXP t;
    switch (TYPEOF(lang)) {
    case PROMSXP:
	return substitute(PREXPR(lang), rho);
    case SYMSXP:
	if (rho != R_NilValue) {
	    t = findVarInFrame3( rho, lang, TRUE);
	    if (t != R_UnboundValue) {
		if (TYPEOF(t) == PROMSXP) {
		    do {
			t = PREXPR(t);
		    } while(TYPEOF(t) == PROMSXP);
		    /* make sure code will not be modified: */
		    SET_NAMEDCNT_MAX(t);
		    return t;
		}
		else if (TYPEOF(t) == DOTSXP)
		    error(_("... used in an incorrect context"));
		if (rho != R_GlobalEnv)
		    return t;
	    }
	}
	return (lang);
    case LANGSXP:
	return substituteList(lang, rho);
    default:
        INC_NAMEDCNT(lang);
	return lang;
    }
}


/* Work through a list doing substitute on the
   elements taking particular care to handle '...' */

SEXP attribute_hidden substituteList(SEXP el, SEXP rho)
{
    SEXP h, p = R_NilValue, res = R_NilValue;

    if (isNull(el)) return el;

    while (el != R_NilValue) {
	/* walk along the pairlist, substituting elements.
	   res is the result
	   p is the current last element
	   h is the element currently being processed
	 */
	if (CAR(el) == R_DotsSymbol) {
	    if (rho == R_NilValue)
		h = R_UnboundValue;	/* so there is no substitution below */
	    else
		h = findVarInFrame3(rho, CAR(el), TRUE);
	    if (h == R_UnboundValue)
		h = LCONS(R_DotsSymbol, R_NilValue);
	    else if (h == R_NilValue  || h == R_MissingArg)
		h = R_NilValue;
	    else if (TYPEOF(h) == DOTSXP) {
                PROTECT(h);
		h = substituteList(h, R_NilValue);
                UNPROTECT(1);
            }
	    else
		error(_("... used in an incorrect context"));
	} else {
	    h = substitute(CAR(el), rho);
	    if (isLanguage(el))
		h = LCONS(h, R_NilValue);
	    else
		h = CONS(h, R_NilValue);
	    SET_TAG(h, TAG(el));
	}
	if (h != R_NilValue) {
	    if (res == R_NilValue)
		PROTECT(res = h);
	    else
		SETCDR(p, h);
	    /* now set 'p': dots might have expanded to a list of length > 1 */
	    while (CDR(h) != R_NilValue) h = CDR(h);
	    p = h;
	}
	el = CDR(el);
    }
    if(res != R_NilValue) UNPROTECT(1);
    return res;
}


/* This is a primitive SPECIALSXP */
static SEXP do_substitute(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP expr, env;

    if (CDR(args) == R_NilValue 
            && TAG(args) == R_NilValue && args != R_NilValue) {
        /* handle one unnamed argument specially for speed */
        expr = CAR(args);
        env = rho;
    }                                
    else {

        static const char * const ap[2] = { "expr", "env" };
        SEXP argList;

        /* argument matching */
        PROTECT(argList = matchArgs_strings (ap, 2, args, call));
        expr = CAR(argList);

        /* set up the environment for substitution */
        if (CADR(argList) == R_MissingArg)
            env = rho;
        else
            env = eval(CADR(argList), rho);
        if (env == R_GlobalEnv) /* historicaly, we don't subst in R_GlobalEnv */
            env = R_NilValue;
        else if (TYPEOF(env) == VECSXP)
            env = NewEnvironment(R_NilValue, VectorToPairList(env), R_BaseEnv);
        else if (TYPEOF(env) == LISTSXP)
            env = NewEnvironment(R_NilValue, duplicate(env), R_BaseEnv);
        if (env != R_NilValue && TYPEOF(env) != ENVSXP)
            errorcall(call, _("invalid environment specified"));
        UNPROTECT(1);
    }

    SEXP s, t;
    PROTECT(env);

    if (TYPE_ETC(expr) == SYMSXP) { 
        /* do symbols (not ..., etc.) specially for speed */
        s = substitute (expr, env);
        UNPROTECT(1);
        return s;
    }

    PROTECT(t = CONS (expr, R_NilValue));
    s = substituteList (t, env);
    UNPROTECT(2);
    return CAR(s);
}

/* This is a primitive SPECIALSXP */
static SEXP do_quote(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    checkArity(op, args);
    check1arg(args, call, "expr");
    SEXP val = CAR(args); 
    /* Make sure expression has NAMED == 2 before being returning
       in to avoid modification of source code */
    SET_NAMEDCNT_MAX(val);
    R_Visible = TRUE;
    return(val);
}

typedef struct {
    char *s;
    SEXPTYPE sexp;
    Rboolean canChange;
} classType;

static classType classTable[] = {
    { "logical",	LGLSXP,	   TRUE },
    { "integer",	INTSXP,	   TRUE },
    { "double",		REALSXP,   TRUE },
    { "raw",		RAWSXP,    TRUE },
    { "complex",	CPLXSXP,   TRUE },
    { "character",	STRSXP,	   TRUE },
    { "expression",	EXPRSXP,   TRUE },
    { "list",		VECSXP,	   TRUE },
    { "environment",    ENVSXP,    FALSE },
    { "char",		CHARSXP,   TRUE },
    { "externalptr",	EXTPTRSXP,  FALSE },
    { "weakref",	WEAKREFSXP, FALSE },
    { "name",		SYMSXP,	   FALSE },

    { (char *)NULL,	(SEXPTYPE)-1, FALSE}
};

static int class2type(const char *s)
{
    /* return the type if the class string is one of the basic types, else -1.
       Note that this is NOT str2type:  only certain types are defined to be basic
       classes; e.g., "language" is a type but many classes correspond to objects of
       this type.
    */
    int i; char *si;
    for(i = 0; ; i++) {
	si = classTable[i].s;
	if(!si)
	    return -1;
	if(!strcmp(s, si))
	    return i;
    }
    /* cannot get here return -1; */
}

static SEXP do_unsetS4(SEXP obj, SEXP newClass) {
    if(isNull(newClass))  { /* NULL class is only valid for S3 objects */
        warning(
         _("Setting class(x) to NULL;   result will no longer be an S4 object"));
    }
    else if(length(newClass) > 1)
        warning(
           _("Setting class(x) to multiple strings (\"%s\", \"%s\", ...); result will no longer be an S4 object"), 
              translateChar(STRING_ELT(newClass, 0)), 
              translateChar(STRING_ELT(newClass, 1)));
    else
        warning(
          _("Setting class(x) to \"%s\" sets attribute to NULL; result will no longer be an S4 object"), 
             CHAR(asChar(newClass)));
    UNSET_S4_OBJECT(obj);
    return obj;
}

/* Set the class to value, return modified object - implementing "class<-" */

static SEXP R_set_class(SEXP obj, SEXP value, SEXP call)
{
    int nProtect = 0;
    /* change from NULL check to zero-length check, as in R-3.0.1. */
    if(length(value)==0) { /* usually NULL */
	setAttrib(obj, R_ClassSymbol, value);
	if(IS_S4_OBJECT(obj)) /* NULL class is only valid for S3 objects */
            do_unsetS4(obj, value);
	return obj;
    }
    if(TYPEOF(value) != STRSXP) {
	SEXP dup;
	PROTECT(dup = duplicate(value));
	PROTECT(value = coerceVector(dup, STRSXP));
	nProtect += 2;
    }
    if(length(value) > 1) {
	setAttrib(obj, R_ClassSymbol, value);
	if(IS_S4_OBJECT(obj)) /*  multiple strings only valid for S3 objects */
            do_unsetS4(obj, value);
    }
    else if(length(value) == 0) {
	errorcall(call,_("invalid replacement object to be a class string"));
    }
    else {
	const char *valueString;
	int whichType;

	SEXPTYPE valueType;
	valueString = CHAR(asChar(value)); /* ASCII */
	whichType = class2type(valueString);
	valueType = (whichType == -1) ? -1 : classTable[whichType].sexp;
	/*  assigning type as a class deletes an explicit class attribute. */
	if(valueType != -1) {
	    setAttrib(obj, R_ClassSymbol, R_NilValue);
	    if(IS_S4_OBJECT(obj)) /* NULL class is only valid for S3 objects */
                do_unsetS4(obj, value);
	    if(classTable[whichType].canChange) {
		PROTECT(obj = ascommon(call, obj, R_NilValue, valueType));
		nProtect++;
	    }
	    else if(valueType != TYPEOF(obj))
		errorcall(call,
                      _("\"%s\" can only be set as the class if the object has this type; found \"%s\""),
		      valueString, type2char(TYPEOF(obj)));
	    /* else, leave alone */
	}
	else if(!strcmp("numeric", valueString)) {
	    setAttrib(obj, R_ClassSymbol, R_NilValue);
	    if(IS_S4_OBJECT(obj)) /* NULL class is only valid for S3 objects */
                do_unsetS4(obj, value);
	    switch(TYPEOF(obj)) {
	    case INTSXP: case REALSXP: break;
	    default: PROTECT(obj = coerceVector(obj, REALSXP));
		nProtect++;
	    }
	}
	/* the next 2 special cases mirror the special code in
	 * R_data_class */
	else if(!strcmp("matrix", valueString)) {
	    if(length(getDimAttrib(obj)) != 2)
		errorcall(call,_("invalid to set the class to matrix unless the dimension attribute is of length 2 (was %d)"),
		 length(getDimAttrib(obj)));
	    setAttrib(obj, R_ClassSymbol, R_NilValue);
	    if(IS_S4_OBJECT(obj))
                do_unsetS4(obj, value);
	}
	else if(!strcmp("array", valueString)) {
	    if(length(getDimAttrib(obj)) <= 0)
		errorcall(call,
                   _("cannot set class to \"array\" unless the dimension attribute has length > 0"));
	    setAttrib(obj, R_ClassSymbol, R_NilValue);
	    if(IS_S4_OBJECT(obj)) /* NULL class is only valid for S3 objects */
                UNSET_S4_OBJECT(obj);
	}
	else { /* set the class but don't do the coercion; that's
		  supposed to be done by an as() method */
	    setAttrib(obj, R_ClassSymbol, value);
	}
    }
    UNPROTECT(nProtect);
    return obj;
}

/* class(x) <- v.  SPECIAL so it can propagate gradient of x. */

static SEXP do_classgets (SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    checkArity(op, args);
    check1arg_x (args, call);

    if (NAMEDCNT_GT_1(CAR(args)))
        SETCAR(args,dup_top_level(CAR(args)));

    SEXP r = R_set_class(CAR(args), CADR(args), call);

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return r;
}


/* storage.mode(obj) <- value.  SPECIAL so can handle gradient. */

static SEXP do_storage_mode (SEXP call, SEXP op, SEXP args, SEXP env, 
                             int variant)
{
    PROTECT (args = variant & VARIANT_GRADIENT
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));

    SEXP obj, grad, value, ans;
    SEXPTYPE type;
    
    checkArity(op, args);
    check1arg_x (args, call);

    obj = CAR(args);
    grad = HAS_GRADIENT_IN_CELL(args) ? GRADIENT_IN_CELL(args) : R_NilValue;

    value = CADR(args);
    if (!isValidString(value) || STRING_ELT(value, 0) == NA_STRING)
        errorcall(call,_("'value' must be non-null character string"));

    type = str2type(CHAR(STRING_ELT(value, 0)));
    if (type == (SEXPTYPE) -1) {
        /* For backwards compatibility we used to allow "real" and "single" */
        if (streql(CHAR(STRING_ELT(value, 0)), "real"))
            errorcall(call,"use of 'real' is defunct: use 'double' instead");
        else if(streql(CHAR(STRING_ELT(value, 0)), "single"))
            errorcall(call,"use of 'single' is defunct: use mode<- instead");
        else
            errorcall(call,_("invalid value"));
    }

    if (TYPEOF(obj) == type)
        ans = obj;
    else {
        if (isFactor(obj))
            errorcall(call,_("invalid to change the storage mode of a factor"));
        PROTECT(ans = coerceVector(obj, type));
        DUPLICATE_ATTRIB(ans, obj);
        if (TYPEOF(ans) == REALSXP && TYPEOF(obj) == VECSXP)
            grad = as_numeric_gradient (grad, LENGTH(ans));
        else if (TYPEOF(ans) == VECSXP && TYPEOF(obj) == REALSXP)
            grad = as_list_gradient (grad, LENGTH(ans));
        else
            grad = R_NilValue;
        UNPROTECT(1);
    }

    if (grad != R_NilValue) {
        R_gradient = grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return ans;
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_coerce[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"as.character",do_ascharacter,	0,	1001,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"as.integer",	do_ascharacter,	1,	1001,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"as.double",	do_asdouble,	2,	1000,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"as.complex",	do_ascharacter,	3,	1001,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"as.logical",	do_ascharacter,	4,	1001,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"as.raw",	do_ascharacter,	5,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"as.vector",	do_asvector,	0,	11011011, 2,	{PP_FUNCALL, PREC_FN,	0}},
{"as.function.default",do_asfunction,0,	11,	2,	{PP_FUNCTION,PREC_FN,	  0}},
{"as.call",	do_ascall,	0,	1,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"typeof",	do_typeof,	1,    1010011,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"is.null",	do_is,		NILSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.logical",	do_is,		LGLSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.integer",	do_is,		INTSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.real",	do_is,		REALSXP,1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.double",	do_is,		REALSXP,1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.complex",	do_is,		CPLXSXP,1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.character",do_is,		STRSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.symbol",	do_is,		SYMSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.environment",do_is,	ENVSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.list",	do_is,		VECSXP,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.pairlist",	do_is,		LISTSXP,1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.expression",do_is,		EXPRSXP,1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.raw",	do_is,		RAWSXP, 1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.object",	do_is,		50,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"is.numeric",	do_is,		100,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.matrix",	do_is,		101,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.array",	do_is,		102,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"is.atomic",	do_is,		200,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.recursive",do_is,		201,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.call",	do_is,		300,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.language",	do_is,		301,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.function",	do_is,		302,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.single",	do_is,		999,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"is.vector",	do_isvector,	0,    1000011,	2,	{PP_FUNCALL, PREC_FN,	0}},

{"is.na",	do_isna,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.nan",	do_isnan,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.finite",	do_isfinite,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"is.infinite",	do_isinfinite,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},

{"call",	do_call,	0,	1000,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"do.call",	do_docall,	0,	211,	3,	{PP_FUNCALL, PREC_FN,	0}},
{"substitute",	do_substitute,	0,	1000,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"quote",	do_quote,	0,	1000,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"storage.mode<-",do_storage_mode,0,	1000,	2,	{PP_FUNCALL, PREC_FN,	0}},

{"class<-",	do_classgets,	0,	1000,	2,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};

/* Fast built-in functions in this file. See names.c for documentation */

attribute_hidden FASTFUNTAB R_FastFunTab_coerce[] = {
/*slow func	fast func,     code or -1   dsptch  variant */

{ do_is,	do_fast_is,	100,		1,  VARIANT_PENDING_OK },
{ do_is,	do_fast_is,	101,		1,  VARIANT_PENDING_OK },
{ do_is,	do_fast_is,	102,		1,  VARIANT_PENDING_OK },
{ do_is,	do_fast_is,	-1,		0,  VARIANT_PENDING_OK },

{ do_isna,	do_fast_isna,	    -1,		1,  VARIANT_SCALAR_STACK_OK },
{ do_isnan,	do_fast_isnan,	    -1,		1,  VARIANT_SCALAR_STACK_OK },
{ do_isfinite,	do_fast_isfinite,   -1,		1,  VARIANT_SCALAR_STACK_OK },
{ do_isinfinite,do_fast_isinfinite, -1,		1,  VARIANT_SCALAR_STACK_OK },

{ 0,		0,		0,		0,  0 }
};
