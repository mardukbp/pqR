/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2017 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1997--2008  The R Core Team
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
#include "Defn.h"

static void cumsum(SEXP x, SEXP s)
{
    int i;
    R_len_t len = LENGTH(x);
    double *rx = REAL(x), *rs = REAL(s);
    long double sum = rx[0];
    rs[0] = rx[0];
    for (i = 1; i < len; i++) {
        sum += rx[i];
        rs[i] = sum;
    }
    if (ISNAN(rs[len-1])) { /* note: len is not zero */
        for (i = 0; i < len; i++)
            if (ISNA(rx[i]))
                break;
        for ( ; i < len; i++)
            rs[i] = NA_REAL;
    }
}

static void icumsum(SEXP x, SEXP s)
{
    int i;
    R_len_t len = LENGTH(x);
    int *ix = INTEGER(x), *is = INTEGER(s);
    int64_t sum = 0;
    for (i = 0; i < len; i++) {
        if (ix[i] == NA_INTEGER) break;
        sum += ix[i];
        /* We need to ensure that overflow gives NA here */
        if (sum > INT_MAX || sum < 1 + INT_MIN) { /* INT_MIN is NA_INTEGER */
            warning(_("Integer overflow in 'cumsum'; use 'cumsum(as.numeric(.))'"));
            break;
        }
        is[i] = sum;
    }
    for ( ; i < len; i++)
        is[i] = NA_INTEGER;
}

static void ccumsum(SEXP x, SEXP s)
{
    int i;
    R_len_t len = LENGTH(x);
    Rcomplex *cx = COMPLEX(x), *cs = COMPLEX(s);
    Rcomplex sum = cx[0];
    cs[0] = cx[0];
    for (i = 1; i < len; i++) {
        sum.r += cx[i].r;
        sum.i += cx[i].i;
        cs[i].r = sum.r;
        cs[i].i = sum.i;
    }
}

static void cumprod(SEXP x, SEXP s)
{
    int i;
    R_len_t len = LENGTH(x);
    double *rx = REAL(x), *rs = REAL(s);
    long double prod = rx[0];
    rs[0] = rx[0];
    for (i = 1; i < len; i++) {
        prod *= rx[i];
        rs[i] = prod;
    }
    if (ISNAN(rs[len-1])) { /* note: len is not zero */
        for (i = 0; i < len; i++)
            if (ISNA(rx[i]))
                break;
        for ( ; i < len; i++)
            rs[i] = NA_REAL;
    }
}

static void ccumprod(SEXP x, SEXP s)
{
    int i;
    R_len_t len = LENGTH(x);
    Rcomplex *cx = COMPLEX(x), *cs = COMPLEX(s);
    Rcomplex prod = cx[0];  /* Note: initializing prod to 1+0i and then      */
    cs[0] = cx[0] ;         /* multiplying by cx[0] gives a different result */
    for (i = 1; i < len; i++) {
        Rcomplex tmp = prod;
        R_from_C99_complex(&prod, 
                           C99_from_R_complex(&tmp) * C99_from_R_complex(cx+i));
        cs[i] = prod;
    }
}

static void cummax(SEXP x, SEXP s)
{
    double max = R_NegInf;
    R_len_t len = LENGTH(x);
    int i;
    double *rx = REAL(x), *rs = REAL(s);
    for (i = 0; i < len; i++) {
        if (ISNAN(rx[i])) {
            max = rx[i];
            break;
        }
        max = max > rx[i] ? max : rx[i];
        rs[i] = max;
    }
    for ( ; i < len; i++) {
        if (ISNA(rx[i])) {
            max = rx[i];
            break;
        }
        rs[i] = max;
    }
    for ( ; i < len; i++)
        rs[i] = max;
}

static void cummin(SEXP x, SEXP s)
{
    double min = R_PosInf;
    R_len_t len = LENGTH(x);
    int i;
    double *rx = REAL(x), *rs = REAL(s);
    for (i = 0; i < len; i++) {
        if (ISNAN(rx[i])) {
            min = rx[i];
            break;
        }
        min = min < rx[i] ? min : rx[i];
        rs[i] = min;
    }
    for ( ; i < len; i++) {
        if (ISNA(rx[i])) {
            min = rx[i];
            break;
        }
        rs[i] = min;
    }
    for ( ; i < len; i++)
        rs[i] = min;
}

static void icummax(SEXP x, SEXP s)
{
    R_len_t len = LENGTH(x);
    int *ix = INTEGER(x), *is = INTEGER(s);
    int i;
    int max = INT_MIN;
    for (i = 0; i < len; i++) {
        if (ix[i] == NA_INTEGER)
            break;
        max = max > ix[i] ? max : ix[i];
        is[i] = max;
    }
    for ( ; i < len; i++)
        is[i] = NA_INTEGER;
}

static void icummin(SEXP x, SEXP s)
{
    R_len_t len = LENGTH(x);
    int *ix = INTEGER(x), *is = INTEGER(s);
    int i;
    int min = INT_MAX;
    for (i = 0; i < len; i++ ) {
        if (ix[i] == NA_INTEGER)
            break;
        min = min < ix[i] ? min : ix[i];
        is[i] = min;
    }
    for ( ; i < len; i++)
        is[i] = NA_INTEGER;
}


/* SPECIAL, so it can handle gradients. */

static SEXP do_cum(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP s, t, ans, t_grad, s_grad;

    PROTECT (args = (variant & VARIANT_GRADIENT)
                     ? evalList_gradient (args, env, 0, 1, 0)
                     : evalList_v (args, env, 0));

    PROTECT (t_grad = HAS_GRADIENT_IN_CELL(args) ? GRADIENT_IN_CELL(args)
                                                 : R_NilValue);
    s_grad = R_NilValue;

    checkArity(op, args);
    if (DispatchGroup("Math", call, op, args, env, &ans, 0)) {
        UNPROTECT(2);
        return ans;
    }

    if (isComplex(CAR(args))) {
        PROTECT(t = CAR(args));
        PROTECT(s = allocVector(CPLXSXP, LENGTH(t)));
        setAttrib(s, R_NamesSymbol, getNamesAttrib(t));
        if (LENGTH(t) == 0) { UNPROTECT(4); return s; }
        switch (PRIMVAL(op) ) {
        case 1:        /* cumsum */
            ccumsum(t,s);
            break;
        case 2: /* cumprod */
            ccumprod(t,s);
            break;
        case 3: /* cummax */
        case 4: /* cummin */
            errorcall(call, _("min/max not defined for complex numbers"));
            break;
        default:
            errorcall(call, _("unknown cumxxx function"));
        }
    } 

    else if ((isInteger(CAR(args)) || isLogical(CAR(args)))
               && PRIMVAL(op) != 2) {
        PROTECT(t = CAR(args));  /* no need to coerce LGL to INT */
        PROTECT(s = allocVector(INTSXP, LENGTH(t)));
        setAttrib(s, R_NamesSymbol, getNamesAttrib(t));
        if (LENGTH(t) == 0) { UNPROTECT(4); return s; }
        switch (PRIMVAL(op) ) {
        case 1: /* cumsum */
            icumsum(t,s);  /* may produce a warning, which allocates */
            break;
        case 3: /* cummax */
            icummax(t,s);
            break;
        case 4: /* cummin */
            icummin(t,s);
            break;
        default:
            errorcall(call, _("unknown cumxxx function"));
        }
    }

    else {
        PROTECT(t = coerceVector(CAR(args), REALSXP));
        PROTECT(s = allocVector(REALSXP, LENGTH(t)));
        setAttrib(s, R_NamesSymbol, getNamesAttrib(t));
        if (LENGTH(t) == 0) { UNPROTECT(4); return s; }
        switch (PRIMVAL(op) ) {
        case 1: /* cumsum */
            cumsum(t,s);
            if (t_grad != R_NilValue)
                s_grad = cumsum_gradient (t_grad, s, LENGTH(t));
            break;
        case 2: /* cumprod */
            cumprod(t,s);
            break;
        case 3: /* cummax */
            cummax(t,s);
            break;
        case 4: /* cummin */
            cummin(t,s);
            break;
        default:
            errorcall(call, _("unknown cumxxx function"));
        }
    }

    if (s_grad != R_NilValue) {
        R_gradient = s_grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(4);  /* args, t_grad, t, s */
    return s;
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_cum[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"cumsum",	do_cum,		1,	1000,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"cumprod",	do_cum,		2,	1000,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"cummax",	do_cum,		3,	1000,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"cummin",	do_cum,		4,	1000,	1,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};
