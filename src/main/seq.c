/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995-1998  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1998-2011  The R Core Team.
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

/* The x:y primitive calls do_colon(); do_colon() calls cross_colon() if
   both arguments are factors and seq_colon() otherwise.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define USE_FAST_PROTECT_MACROS
#include <Defn.h>
#include <float.h>  /* for DBL_EPSILON */
#include <Rmath.h>

#include "scalar-stack.h"

#include <helpers/helpers-app.h>

#include "RBufferUtils.h"
static R_StringBuffer cbuff = {NULL, 0, MAXELTSIZE};

#define _S4_rep_keepClass
/* ==>  rep(<S4>, .) keeps class e.g., for list-like */

static SEXP cross_colon(SEXP call, SEXP s, SEXP t)
{
    SEXP a, la, ls, lt, rs, rt;
    int i, j, k, n, nls, nlt, vs, vt;
    char *cbuf;

    if (length(s) != length(t))
	errorcall(call, _("unequal factor lengths"));
    n = length(s);
    ls = getAttrib(s, R_LevelsSymbol);
    lt = getAttrib(t, R_LevelsSymbol);
    nls = LENGTH(ls);
    nlt = LENGTH(lt);
    PROTECT(a = allocVector(INTSXP, n));
    PROTECT(rs = coerceVector(s, INTSXP));
    PROTECT(rt = coerceVector(t, INTSXP));
    for (i = 0; i < n; i++) {
	vs = INTEGER(rs)[i];
	vt = INTEGER(rt)[i];
	if ((vs == NA_INTEGER) || (vt == NA_INTEGER))
	    INTEGER(a)[i] = NA_INTEGER;
	else
	    INTEGER(a)[i] = vt + (vs - 1) * nlt;
    }
    UNPROTECT(2);
    if (!isNull(ls) && !isNull(lt)) {
	PROTECT(la = allocVector(STRSXP, nls * nlt));
	k = 0;
	/* FIXME: possibly UTF-8 version */
	for (i = 0; i < nls; i++) {
	    const char *vi = translateChar(STRING_ELT(ls, i));
	    vs = strlen(vi);
	    for (j = 0; j < nlt; j++) {
		const char *vj = translateChar(STRING_ELT(lt, j));
		vt = strlen(vj);
                int len = vs + vt + 1;
		cbuf = R_AllocStringBuffer(len, &cbuff);
		(void) copy_3_strings(cbuf,len+1,vi,":",vj);
		SET_STRING_ELT(la, k, mkChar(cbuf));
		k++;
	    }
	}
	setAttrib(a, R_LevelsSymbol, la);
	UNPROTECT(1);
    }
    PROTECT(la = mkString("factor"));
    setAttrib(a, R_ClassSymbol, la);
    UNPROTECT(2);
    R_FreeStringBufferL(&cbuff);
    return(a);
}

/* Create a simple integer sequence, or as variant, a description of it. 
   Sets R_variant_result to 1 if a sequence description is returned in
   R_variant_seq_spec (with R_NilValue being the returned SEXP).  Won't
   put all zeros in R_variant_seq_spec.

   If dotdot is true, attaches 1D dim attribute (or spec says to do so). */

static SEXP make_seq (int from, int len, int variant, int dotdot)
{
    SEXP ans;

    if (VARIANT_KIND(variant) == VARIANT_SEQ && (from|len|dotdot) != 0) {
        R_variant_seq_spec = 
          ((int64_t)from * ((int64_t)1<<32)) /* Note: -ve<<. is undef in C99 */
            | ((int64_t)len<<1) 
            | dotdot;
        R_variant_result = 1;
        ans = R_NilValue;
    }
    else {
        ans = Rf_VectorFromRange (from, from+(len-1));
        if (dotdot) {
            SEXP dim1;
            PROTECT(ans);
            PROTECT(dim1 = ScalarInteger(len));
            setAttrib (ans, R_DimSymbol, dim1);
            UNPROTECT(2);
        }
    }

    return ans;
}

static SEXP seq_colon(double n1, double n2, int dotdot, SEXP call, int variant)
{
    int i, n, in1;
    double r;
    SEXP ans;
    Rboolean useInt;

    if (dotdot)  /* .. operator */
        r = n2 >= n1 - FLT_EPSILON ? n2 - n1 : -1;
    else  /* : */
        r = fabs (n2 - n1);

    if (r + FLT_EPSILON >= INT_MAX) 
        errorcall(call,_("result would be too long a vector"));

    n = r + 1 + FLT_EPSILON;

    in1 = (int)(n1);
    useInt = (n1 == in1);
    if(useInt) {
	if(n1 <= INT_MIN || n1 > INT_MAX)
	    useInt = FALSE;
	else {
	    /* r := " the effective 'to' "  of  from:to */
	    r = dotdot? n1 + (n-1) : n1 + ((n1 <= n2) ? n-1 : -(n-1));
	    if(r <= INT_MIN || r > INT_MAX)
		useInt = FALSE;
	}
    }
    if (useInt) {
        if (dotdot || n1 <= n2)
            ans = make_seq (in1, n, variant, dotdot);
        else {
	    ans = allocVector(INTSXP, n);
            for (i = 0; i < n; i++) INTEGER(ans)[i] = in1 - i;
        }
    } else {
	ans = allocVector(REALSXP, n);
	if (dotdot || n1 <= n2)
	    for (i = 0; i < n; i++) REAL(ans)[i] = n1 + i;
	else
	    for (i = 0; i < n; i++) REAL(ans)[i] = n1 - i;
    }
    return ans;
}


static SEXP do_colon(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{   
    int opcode = PRIMVAL(op);
    SEXP ans, x, y;
    double n1, n2;

    /* Evaluate arguments, setting x to first argument and y to
       second argument.  The whole argument list is in args. */

    x = CAR(args); 
    y = CADR(args);

    if (x==R_DotsSymbol || y==R_DotsSymbol || CDDR(args)!=R_NilValue) {
        args = evalList (args, env);
        PROTECT(x = CAR(args)); 
        PROTECT(y = CADR(args));
    }
    else {
        PROTECT(x = eval(x,env));
        PROTECT(y = eval(y,env));
    }

    R_Visible = TRUE;

    checkArity(op, args);

    if (inherits_CHAR (x, R_factor_CHARSXP) 
     && inherits_CHAR (y, R_factor_CHARSXP)) {
        ans = cross_colon (call, x, y);
    }
    else {

        n1 = length(x);
        n2 = length(y);

        if (n1 == 0 || n2 == 0)
            errorcall(call, _("argument of length 0"));
        if (n1 > 1)
            warningcall(call, 
              _("numerical expression has %d elements: only the first used"), 
              (int) n1);
        if (n2 > 1)
            warningcall(call, 
              _("numerical expression has %d elements: only the first used"), 
              (int) n2);

        n1 = asReal(x);
        n2 = asReal(y);
        if (ISNAN(n1) || ISNAN(n2))
            errorcall(call, _("NA/NaN argument"));

        ans = seq_colon(n1, n2, opcode, call, variant);
    }

    UNPROTECT(2);
    return ans;
}

/* Task procedure for rep, rep.int, and rep_len.  Repeats first input
   to the length of the output.  If the second input is null, repeats
   each element once in each cycle; if it is scalar, repeats each
   element that many times each cycle; if it is a vector (same length
   as first input), it repeats each element the specified number of
   times each cycle.
  
   Should be master-only if input is not numeric.  Does not pipeline input
   or output. */

void task_rep (helpers_op_t op, SEXP a, SEXP s, SEXP t)
{
    if (TYPEOF(a) != TYPEOF(s) 
         || t != (helpers_var_ptr)0 && TYPEOF(t) != INTSXP) abort();

    int na = length(a), ns = length(s);
    int i, j, k;
    SEXP u;

    if (na <= 0) return;

    if (t == (helpers_var_ptr)0 || LENGTH(t) == 1 && INTEGER(t)[0] == 1) {
        if (ns == 1) {
            /* Repeat of a single element na times. */
            switch (TYPEOF(s)) {
            case LGLSXP:
            case INTSXP:
            case REALSXP:
            case CPLXSXP:
            case RAWSXP:
            case STRSXP:
                Rf_rep_element (a, 0, s, 0, na);
                break;
            case LISTSXP: {
                SEXP v = CAR(s);
                for (u = a; u != R_NilValue; u = CDR(u)) 
                    SETCAR (u, duplicate(v));
                break;
            }
            case EXPRSXP:
            case VECSXP: {
                SEXP v = VECTOR_ELT (s, 0);
                if (v == R_NilValue) {
                    for (i = 0; i < na; i++) {
                        SET_VECTOR_ELT_NIL (a, i);
                    }
                }
                else {
                    SET_VECTOR_ELEMENT_FROM_VECTOR (a, 0, s, 0);
                    for (i = 1; i < na; i++) {
                        SET_VECTOR_ELT (a, i, v);
                        INC_NAMEDCNT_0_AS_1 (v);
                    }
                }
        	break;
            }
            default: abort();
            }
        }
        else {
            /* Simple repeat of a vector to length na. */
            switch (TYPEOF(s)) {
            case LGLSXP:
            case INTSXP:
            case REALSXP:
            case CPLXSXP:
            case RAWSXP:
            case STRSXP:
                copy_elements_recycled (a, 0, s, na);
                break;
            case LISTSXP:
                for (u = a, j = 0; u != R_NilValue; u = CDR(u), j++) {
                    if (j >= ns) j = 0;
                    SETCAR (u, duplicate (CAR (nthcdr (s, j))));
                }
                break;
            case EXPRSXP:
            case VECSXP:
                for (i = 0, j = 0; i < na; i++, j++) {
                    if (j >= ns) j = 0;
                    if (i < ns)
                        SET_VECTOR_ELEMENT_FROM_VECTOR (a, i, s, j);
                    else {
                        SEXP v = VECTOR_ELT (s, j);
                        SET_VECTOR_ELT (a, i, v);
                        INC_NAMEDCNT_0_AS_1 (v);
                    }
                }
        	break;
            default: abort();
            }
        }
    }

    else {
        if (LENGTH(t) == 1) {
            /* Repeat each element of s same number of times in each cycle. */
            int each = INTEGER(t)[0];
            if (each == 0) return;
            switch (TYPEOF(s)) {
            case LGLSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    int v = LOGICAL(s)[j];
                    for (k = each; k > 0; k--) {
                        LOGICAL(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case INTSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    int v = INTEGER(s)[j];
                    for (k = each; k > 0; k--) {
                        INTEGER(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case REALSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    double v = REAL(s)[j];
                    for (k = each; k > 0; k--) {
                        REAL(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case CPLXSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    Rcomplex v = COMPLEX(s)[j];
                    for (k = each; k > 0; k--) {
                        COMPLEX(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case RAWSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    Rbyte v = RAW(s)[j];
                    for (k = each; k > 0; k--) {
                        RAW(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case STRSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    SEXP v = STRING_ELT (s, j);
                    for (k = each; k > 0; k--) {
                        SET_STRING_ELT (a, i, v);
                        if (++i == na) return;
                    }
                }
            case LISTSXP:
                for (u = a, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    SEXP v = CAR (nthcdr (s, j));
                    for (k = each; k > 0; k--) {
                        SETCAR (u, duplicate(v));
                        u = CDR(u);
                        if (u == R_NilValue) return;
                    }
                }
            case EXPRSXP:
            case VECSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    SEXP v = VECTOR_ELT (s, j);
                    for (k = each; k > 0; k--) {
                        SET_VECTOR_ELT (a, i, v);
                        INC_NAMEDCNT_0_AS_1 (v);
                        if (++i == na) return;
                    }
                }
            default: abort();
            }
        }
        else {
            /* Repeat elements varying numbers of times in each cycle. */
            int *eachv = INTEGER(t);
            if (LENGTH(t) != ns) abort();
            switch (TYPEOF(s)) {
            case LGLSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    int v = LOGICAL(s)[j];
                    for (k = eachv[j]; k > 0; k--) {
                        LOGICAL(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case INTSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    int v = INTEGER(s)[j];
                    for (k = eachv[j]; k > 0; k--) {
                        INTEGER(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case REALSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    double v = REAL(s)[j];
                    for (k = eachv[j]; k > 0; k--) {
                        REAL(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case CPLXSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    Rcomplex v = COMPLEX(s)[j];
                    for (k = eachv[j]; k > 0; k--) {
                        COMPLEX(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case RAWSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    Rbyte v = RAW(s)[j];
                    for (k = eachv[j]; k > 0; k--) {
                        RAW(a)[i] = v;
                        if (++i == na) return;
                    }
                }
            case STRSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    SEXP v = STRING_ELT (s, j);
                    for (k = eachv[j]; k > 0; k--) {
                        SET_STRING_ELT (a, i, v);
                        if (++i == na) return;
                    }
                }
            case LISTSXP:
                for (u = a, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    SEXP v = CAR (nthcdr (s, j));
                    for (k = eachv[j]; k > 0; k--) {
                        SETCAR (u, duplicate(v));
                        u = CDR(u);
                        if (u == R_NilValue) return;
                    }
                }
            case EXPRSXP:
            case VECSXP:
                for (i = 0, j = 0; ; j++) {
                    if (j >= ns) j = 0;
                    SEXP v = VECTOR_ELT (s, j);
                    for (k = eachv[j]; k > 0; k--) {
                        SET_VECTOR_ELT (a, i, v);
                        INC_NAMEDCNT_0_AS_1 (v);
                        if (++i == na) return;
                    }
                }
            default: abort();
            }
        }
    }
}


#define T_rep THRESHOLD_ADJUST(300)

static SEXP do_rep_int(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP s = CAR(args);
    SEXP ncopy = CADR(args);
    SEXP a;
    int na;

    if (ncopy != R_MissingArg && !isVector(ncopy))
        error(_("incorrect type for second argument"));

    if (!isVector(s) && TYPEOF(s) != LISTSXP)
	error(_("attempt to replicate non-vector"));

    int ns = length(s);
    int nc = ncopy==R_MissingArg ? 1 : LENGTH(ncopy);

    if (nc == 1) {
        int ncv = ncopy==R_MissingArg ? 1 
                : isVectorList(ncopy) ? asInteger(coerceVector(ncopy,INTSXP))
                : asInteger(ncopy);
	if (ncv == NA_INTEGER || ncv < 0 || (double)ncv*ns > INT_MAX)
	    error(_("invalid '%s' value"), "times"); /* ncv = 0 is OK */
        na = ncv * ns;
        ncopy = (helpers_var_ptr)0;
    }
    else if (nc == ns) {
        PROTECT(ncopy = coerceVector(ncopy, INTSXP));
        if (TYPEOF(ncopy) != INTSXP || LENGTH(ncopy) != nc) abort();
        na = 0;
        for (int i = 0; i < nc; i++) {
	    if (INTEGER(ncopy)[i] == NA_INTEGER || INTEGER(ncopy)[i] < 0)
	        error(_("invalid '%s' value"), "times");
            if ((double)na + INTEGER(ncopy)[i] > INT_MAX)
                error(_("invalid '%s' value"), "times");
            na += INTEGER(ncopy)[i];
        }
    }
    else 
        error(_("invalid '%s' value"), "times");

    PROTECT(a = allocVector(TYPEOF(s), na));

    HELPERS_NOW_OR_LATER (
      !helpers_not_multithreading && na >= T_rep && isVectorNonpointer(a)
        && (variant & VARIANT_PENDING_OK) != 0, 
      FALSE, 0, task_rep, 0, a, s, ncopy);

    SEXP grad = R_NilValue;

    if (HAS_GRADIENT_IN_CELL(args)) {
        SEXP x_grad = GRADIENT_IN_CELL(args);
        if (TYPEOF(a) == VECSXP) {
            if (nc == 1)
                grad = copy_list_recycled_gradient (x_grad, na);
            else
                grad = rep_each_list_gradient (x_grad, ncopy, na);
        }
        else if (TYPEOF(a) == REALSXP) {
            if (nc == 1)
                grad = copy_numeric_recycled_gradient (x_grad, na);
            else
                grad = rep_each_numeric_gradient (x_grad, ncopy, na);
        }
    }

#ifdef _S4_rep_keepClass
    if(IS_S4_OBJECT(s)) { /* e.g. contains = "list" */
	setAttrib(a, R_ClassSymbol, getClassAttrib(s));
	SET_S4_OBJECT(a);
    }
#endif

    if (inherits_CHAR (s, R_factor_CHARSXP)) {
	SEXP tmp;
	if (inherits_CHAR (s, R_ordered_CHARSXP)) {
	    PROTECT(tmp = allocVector(STRSXP, 2));
	    SET_STRING_ELT(tmp, 0, R_ordered_CHARSXP);
	    SET_STRING_ELT(tmp, 1, R_factor_CHARSXP);
	} 
        else {
	    PROTECT(tmp = allocVector(STRSXP, 1));
	    SET_STRING_ELT(tmp, 0, R_factor_CHARSXP);
        }
	setAttrib(a, R_ClassSymbol, tmp);
	UNPROTECT(1);
	setAttrib(a, R_LevelsSymbol, getAttrib(s, R_LevelsSymbol));
    }

    if (grad != R_NilValue) {
        R_gradient = grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1 + (ncopy!=(helpers_var_ptr)0));
    return a;
}

/* We are careful to use evalListKeepMissing here (inside
   DispatchOrEval) to avoid dropping missing arguments so e.g.
   rep(1:3,,8) matches length.out */

/* This is a primitive SPECIALSXP with internal argument matching */

/* NOTE:  In pqR, we now guarantee that the result of "rep" (with default
   method) is unshared (relevant to .C and .Fortran with DUP=FALSE). */

static SEXP do_rep(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP a, ans, times;
    int i, len, each, nprotect = 0;
    static const char * const ap[5] = 
                         { "x", "times", "length.out", "each", "..." };

    if (DispatchOrEval (call, op, "rep", args, rho, &ans, 
                        2 /* ask for gradient for 1st argument */, 0, variant))
	return(ans);

    /* This has evaluated all the non-missing arguments into ans */
    PROTECT(args = ans);
    nprotect++;

    SEXP grad = R_NilValue;
    SEXP x_grad = R_NilValue;
    if (HAS_GRADIENT_IN_CELL(args))
        x_grad = GRADIENT_IN_CELL(args);

    R_Visible = TRUE;

    /* This is a primitive, and we have not dispatched to a method
       so we manage the argument matching ourselves.  We pretend this is
       rep(x, times, length.out, each, ...)
    */
    PROTECT(args = matchArgs_strings (ap, 5, args, call));
    nprotect++;

    SEXP x = CAR(args); args = CDR(args);
    SEXP times_arg = CAR(args); args = CDR(args);
    SEXP length_arg = CAR(args); args = CDR(args);
    SEXP each_arg = CAR(args);

    if (x == R_NilValue) {
        unprotect(nprotect);
        return R_NilValue;
    }

    if (!isVector(x) && !isList(x))
	error(_("attempt to replicate non-vector"));
    
    int lx = length(x);

    len = asInteger(length_arg);
    if(len != NA_INTEGER && len < 0)
	errorcall(call, _("invalid '%s' argument"), "length.out");
    if(length(length_arg) != 1)
	warningcall(call, _("first element used of '%s' argument"), 
		    "length.out");

    int le = length(each_arg);
    each = asInteger(each_arg);
    if (each == NA_INTEGER) 
        each = 1;
    if (le == 0 || each < 0)
	errorcall(call, _("invalid '%s' argument"), "each");
    if (le != 1)
	warningcall(call, _("first element used of '%s' argument"), "each");

    if (lx == 0) {
        PROTECT(x = duplicate(x));
        nprotect++;
        if (len != NA_INTEGER && len > 0)
            x = lengthgets(x,len);
	UNPROTECT(nprotect);
        return x;
    }

    if (len != NA_INTEGER) { /* takes precedence over times */
        if(len > 0 && each == 0)
            errorcall(call, _("invalid '%s' argument"), "each");
        times = (helpers_var_ptr)0;
    } 
    else {  /* len == NA_INTEGER */
	int nt;
	if(times_arg == R_MissingArg) 
            PROTECT(times = ScalarIntegerMaybeConst(1));
	else 
            PROTECT(times = coerceVector(times_arg, INTSXP));
	nprotect++;
	nt = LENGTH(times);
	if (nt == 1) {
	    int it = INTEGER(times)[0];
	    if (it == NA_INTEGER || it < 0 || (double) lx * it * each > INT_MAX)
		errorcall(call, _("invalid '%s' argument"), "times");
	    len = lx * it * each;
            times = (helpers_var_ptr)0;
	} 
        else {
            if (nt != (double) lx * each)
                errorcall(call, _("invalid '%s' argument"), "times");
            len = 0;
	    for(i = 0; i < nt; i++) {
		int it = INTEGER(times)[i];
		if (it == NA_INTEGER || it < 0 || (double)len + it > INT_MAX)
		    errorcall(call, _("invalid '%s' argument"), "times");
		len += it;
	    }
            /* Here, convert calls like rep(c(T,F),each=2,times=c(1,3,5,2)) 
               to rep(c(T,F),each=1,times=c(4,7)) */
            if (each != 1) {
                SEXP old_times = times;
                int j;
                times = allocVector (INTSXP, nt/each);
                UNPROTECT(1);
                PROTECT(times);
                for (j = 0; j < LENGTH(times); j++) {
                    INTEGER(times)[j] = 0;
                    for (i = 0; i < each; i++) 
                        INTEGER(times)[j] += INTEGER(old_times)[i+each*j];
                }
                each = 1;
            }
	}
    }

    SEXP xn;
    PROTECT(xn = getAttrib(x, R_NamesSymbol));
    nprotect++;
    int len_xn = length(xn);

    if (TYPEOF(x) == LISTSXP) {
        PROTECT(x = coerceVector(x,VECSXP));
        nprotect++;
    }

    SEXP each_times;
    protect(each_times = each != 1 ? ScalarIntegerMaybeConst(each) : times);
    nprotect++;
    PROTECT(a = allocVector(TYPEOF(x), len));
    nprotect++;

    HELPERS_NOW_OR_LATER (
      !helpers_not_multithreading && len >= T_rep && isVectorNonpointer(a)
         && (len_xn > 0 || (variant & VARIANT_PENDING_OK) != 0), 
      FALSE, 0, task_rep, 0, a, x, each_times);

    if (x_grad != R_NilValue) {
        if (TYPEOF(a) == VECSXP) {
            if (each_times == (helpers_var_ptr)0
                 || LENGTH(each_times) == 1 && INTEGER(each_times)[0] == 1)
                grad = copy_list_recycled_gradient (x_grad, len);
            else
                grad = rep_each_list_gradient (x_grad, each_times, len);
        }
        else if (TYPEOF(a) == REALSXP) {
            if (each_times == (helpers_var_ptr)0
                 || LENGTH(each_times) == 1 && INTEGER(each_times)[0] == 1)
                grad = copy_numeric_recycled_gradient (x_grad, len);
            else
                grad = rep_each_numeric_gradient (x_grad, each_times, len);
        }
    }

    if (len_xn > 0) {
        SEXP an = allocVector (TYPEOF(xn), len);
        task_rep (0, an, xn, each_times);
        setAttrib(a, R_NamesSymbol, an);
        if ((variant & VARIANT_PENDING_OK) == 0) WAIT_UNTIL_COMPUTED(a);
    }

#ifdef _S4_rep_keepClass
    if(IS_S4_OBJECT(x)) { /* e.g. contains = "list" */
	setAttrib(a, R_ClassSymbol, getClassAttrib(x));
	SET_S4_OBJECT(a);
    }
#endif

    if (grad != R_NilValue) {
        R_gradient = grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(nprotect);
    return a;
}


/* Internal rep_len, with arguments x and length.out.  Mostly taken from
   R-3.4.0, apart from the call of task_rep. */

static SEXP do_rep_len(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    R_xlen_t ns, na;
    SEXP a, s, len;

    checkArity(op, args);
    s = CAR(args);

    if (!isVector(s) && s != R_NilValue)
	error(_("attempt to replicate non-vector"));

    len = CADR(args);
    if(length(len) != 1)
	error(_("invalid '%s' value"), "length.out");
    if (TYPEOF(len) != INTSXP) {
	double sna = asReal(len);
	if (ISNAN(sna) || sna <= -1. || sna >= R_XLEN_T_MAX + 1.)
	    error(_("invalid '%s' value"), "length.out");
	na = (R_xlen_t) sna;
    } else
	if ((na = asInteger(len)) == NA_INTEGER || na < 0) /* na = 0 ok */
	    error(_("invalid '%s' value"), "length.out");

    if (TYPEOF(s) == NILSXP && na > 0)
	error(_("cannot replicate NULL to a non-zero length"));
    ns = xlength(s);
    if (ns == 0) {
	SEXP a;
	PROTECT(a = duplicate(s));
	if(na > 0) a = xlengthgets(a, na);
	UNPROTECT(1);
	return a;
    }

    PROTECT(a = allocVector(TYPEOF(s), na));

    HELPERS_NOW_OR_LATER (
      !helpers_not_multithreading && na >= T_rep && isVectorNonpointer(s)
         && (variant & VARIANT_PENDING_OK) != 0, 
      FALSE, 0, task_rep, 0, a, s, (helpers_var_ptr) 0);

    SEXP grad = R_NilValue;

    if (HAS_GRADIENT_IN_CELL(args)) {
        SEXP x_grad = GRADIENT_IN_CELL(args);
        if (TYPEOF(a) == VECSXP)
            grad = copy_list_recycled_gradient (x_grad, na);
        else if (TYPEOF(a) == REALSXP)
            grad = copy_numeric_recycled_gradient (x_grad, na);
    }

#ifdef _S4_rep_keepClass
    if(IS_S4_OBJECT(s)) { /* e.g. contains = "list" */
	setAttrib(a, R_ClassSymbol, getClassAttrib(s));
	SET_S4_OBJECT(a);
    }
#endif

    if (inherits_CHAR (s, R_factor_CHARSXP)) {
	SEXP tmp;
	if (inherits_CHAR (s, R_ordered_CHARSXP)) {
	    PROTECT(tmp = allocVector(STRSXP, 2));
	    SET_STRING_ELT(tmp, 0, R_ordered_CHARSXP);
	    SET_STRING_ELT(tmp, 1, R_factor_CHARSXP);
	} 
        else {
	    PROTECT(tmp = allocVector(STRSXP, 1));
	    SET_STRING_ELT(tmp, 0, R_factor_CHARSXP);
        }
	setAttrib(a, R_ClassSymbol, tmp);
	UNPROTECT(1);
	setAttrib(a, R_LevelsSymbol, getAttrib(s, R_LevelsSymbol));
    }

    if (grad != R_NilValue) {
        R_gradient = grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return a;
}


/* do_seq implements seq.int, which dispatches on methods for seq. */

#define FEPS 1e-10
/* to match seq.default */
static SEXP do_seq(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP ans, from, to, by, len, along;
    int i, nargs = length(args), lf, lout = NA_INTEGER;
    Rboolean One = nargs == 1;
    static const char * const ap[6] =
        { "from", "to", "by", "length.out", "along.with", "..." };

    if (DispatchOrEval(call, op, "seq", args, rho, &ans, 0, 1, variant))
	return(ans);

    /* This is a primitive and we manage argument matching ourselves.
       We pretend this is
       seq(from, to, by, length.out, along.with, ...)
    */

    PROTECT(args = matchArgs_strings (ap, 6, args, call));

    from = CAR(args); args = CDR(args);
    to = CAR(args); args = CDR(args);
    by = CAR(args); args = CDR(args);
    len = CAR(args); args = CDR(args);
    along = CAR(args);

    if(One && from != R_MissingArg) {
	lf = length(from);
	if(lf == 1 && (TYPEOF(from) == INTSXP || TYPEOF(from) == REALSXP))
	    ans = seq_colon(1.0, asReal(from), 0, call, variant);
	else if (lf)
	    ans = seq_colon(1.0, (double)lf, 0, call, variant);
	else
	    ans = allocVector(INTSXP, 0);
	goto done;
    }
    if(along != R_MissingArg) {
	lout = LENGTH(along);
	if(One) {
	    ans = lout ? seq_colon(1.0, (double)lout, 0, call, variant) 
                       : allocVector(INTSXP, 0);
	    goto done;
	}
    } else if(len != R_MissingArg && len != R_NilValue) {
	double rout = asReal(len);
	if(ISNAN(rout) || rout <= -0.5)
	    errorcall(call, _("'length.out' must be a non-negative number"));
	if(length(len) != 1)
	    warningcall(call, _("first element used of '%s' argument"), 
			"length.out");
	lout = (int) ceil(rout);
    }

    if(lout == NA_INTEGER) {
        double rfrom = 1.0, rto = 1.0, rby, *ra;
        if (from != R_MissingArg) {
            if (length(from) != 1) error("'from' must be of length 1");
            rfrom = asReal(from);
        }
        if (to != R_MissingArg) {
            if (length(to) != 1) error("'to' must be of length 1");
            rto = asReal(to);
        }
	if(by == R_MissingArg)
	    ans = seq_colon(rfrom, rto, 0, call, variant);
	else {
            if (length(by) != 1) error("'by' must be of length 1");
            rby = asReal(by);
	    double del = rto - rfrom, n, dd;
	    int nn;
	    if(!R_FINITE(rfrom))
		errorcall(call, _("'from' must be finite"));
	    if(!R_FINITE(rto))
		errorcall(call, _("'to' must be finite"));
	    if(del == 0.0 && rto == 0.0) {
		ans = to;
		goto done;
	    }
	    /* printf("from = %f, to = %f, by = %f\n", rfrom, rto, rby); */
	    n = del/rby;
	    if(!R_FINITE(n)) {
		if(del == 0.0 && rby == 0.0) {
		    ans = from;
		    goto done;
		} else
		    errorcall(call, _("invalid '(to - from)/by' in 'seq'"));
	    }
	    dd = fabs(del)/fmax2(fabs(rto), fabs(rfrom));
	    if(dd < 100 * DBL_EPSILON) {
		ans = from;
		goto done;
	    }
	    if(n > (double) INT_MAX)
		errorcall(call, _("'by' argument is much too small"));
	    if(n < - FEPS)
		errorcall(call, _("wrong sign in 'by' argument"));
	    if(TYPEOF(from) == INTSXP &&
	       TYPEOF(to) == INTSXP &&
	       TYPEOF(by) == INTSXP) {
		int *ia, ifrom = asInteger(from), iby = asInteger(by);
		/* With the current limits on integers and FEPS
		   reduced below 1/INT_MAX this is the same as the
		   next, so this is future-proofing against longer integers.
		*/
		nn = (int)n;
		/* seq.default gives integer result from
		   from + (0:n)*by
		*/
		ans = allocVector(INTSXP, nn+1);
		ia = INTEGER(ans);
		for(i = 0; i <= nn; i++)
		    ia[i] = ifrom + i * iby;
	    } else {
		nn = (int)(n + FEPS);
		ans = allocVector(REALSXP, nn+1);
		ra = REAL(ans);
		for(i = 0; i <= nn; i++)
		    ra[i] = rfrom + i * rby;
		/* Added in 2.9.0 */
		if (nn > 0)
		    if((rby > 0 && ra[nn] > rto) || (rby < 0 && ra[nn] < rto))
			ra[nn] = rto;
	    }
	}
    } else if (lout == 0) {
	ans = allocVector(INTSXP, 0);
    } else if (One) {
	ans = seq_colon(1.0, (double)lout, 0, call, variant);
    } else if (by == R_MissingArg) {
	double rfrom = asReal(from), rto = asReal(to), rby;
	if(to == R_MissingArg) rto = rfrom + lout - 1;
	if(from == R_MissingArg) rfrom = rto - lout + 1;
	if(!R_FINITE(rfrom))
	    errorcall(call, _("'from' must be finite"));
	if(!R_FINITE(rto))
	    errorcall(call, _("'to' must be finite"));
	ans = allocVector(REALSXP, lout);
	if(lout > 0) REAL(ans)[0] = rfrom;
	if(lout > 1) REAL(ans)[lout - 1] = rto;
	if(lout > 2) {
	    rby = (rto - rfrom)/(double)(lout - 1);
	    for(i = 1; i < lout-1; i++) REAL(ans)[i] = rfrom + i*rby;
	}
    } else if (to == R_MissingArg) {
	double rfrom = asReal(from), rby = asReal(by), rto;
	if(from == R_MissingArg) rfrom = 1.0;
	if(!R_FINITE(rfrom))
	    errorcall(call, _("'from' must be finite"));
	if(!R_FINITE(rby))
	    errorcall(call, _("'by' must be finite"));
	rto = rfrom +(lout-1)*rby;
	if(rby == (int)rby && rfrom <= INT_MAX && rfrom >= INT_MIN
	   && rto <= INT_MAX && rto >= INT_MIN) {
	    ans = allocVector(INTSXP, lout);
	    for(i = 0; i < lout; i++)
		INTEGER(ans)[i] = rfrom + i*rby;
	} else {
	    ans = allocVector(REALSXP, lout);
	    for(i = 0; i < lout; i++)
		REAL(ans)[i] = rfrom + i*rby;
	}
    } else if (from == R_MissingArg) {
	double rto = asReal(to), rby = asReal(by),
	    rfrom = rto - (lout-1)*rby;
	if(!R_FINITE(rto))
	    errorcall(call, _("'to' must be finite"));
	if(!R_FINITE(rby))
	    errorcall(call, _("'by' must be finite"));
	if(rby == (int)rby && rfrom <= INT_MAX && rfrom >= INT_MIN
	   && rto <= INT_MAX && rto >= INT_MIN) {
	    ans = allocVector(INTSXP, lout);
	    for(i = 0; i < lout; i++)
		INTEGER(ans)[i] = rto - (lout - 1 - i)*rby;
	} else {
	    ans = allocVector(REALSXP, lout);
	    for(i = 0; i < lout; i++)
		REAL(ans)[i] = rto - (lout - 1 - i)*rby;
	}
    } else
	errorcall(call, _("too many arguments"));

done:
    UNPROTECT(1);
    return ans;
}

static SEXP do_seq_along(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    static SEXP length_op = R_NoObject;
    SEXP arg, ans;
    int len;

    /* Store the .Primitive for 'length' for DispatchOrEval to use. */
    if (length_op == R_NoObject) {
	SEXP R_lengthSymbol = install("length");
	length_op = eval(R_lengthSymbol, R_BaseEnv);
	if (TYPEOF(length_op) != BUILTINSXP) {
	    length_op = R_NoObject;
	    error("'length' is not a BUILTIN");
	}
	R_PreserveObject(length_op);
    }

    checkArity(op, args);
    check1arg(args, call, "along.with");
    arg = CAR(args);

    /* Try to dispatch to S3 or S4 methods for 'length'.  For cases
       where no methods are defined this is more efficient than an
       unconditional callback to R */

    if (isObject(arg) && DispatchOrEval (call, length_op, "length", args, rho,
                                         &ans, 0, 1, variant)) {
	len = asInteger(ans);
    }
    else
	len = length(arg);

    return make_seq (1, len, variant, 0);
}

static SEXP do_fast_seq_len (SEXP call, SEXP op, SEXP arg, SEXP rho, 
                             int variant)
{   int len = asInteger(arg);
    if(len == NA_INTEGER || len < 0)
	errorcall(call,_("argument must be coercible to non-negative integer"));
    if (length(arg) != 1)
	warningcall(call, _("first element used of '%s' argument"),
		    "length.out");

    POP_IF_TOP_OF_STACK(arg);

    return make_seq (1, len, variant, 0);
}

static SEXP do_seq_len(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{   
    checkArity(op, args);
    check1arg(args, call, "length.out");

    return do_fast_seq_len (call, op, CAR(args), rho, variant);
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_seq[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{":",		do_colon,	0,	1000,	2,	{PP_BINARY2, PREC_COLON,  0}},
{"..",		do_colon,	1,	1000,	2,	{PP_BINARY2, PREC_COLON,  0}},
{"rep.int",	do_rep_int,	0,   11001011,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"rep_len",	do_rep_len,	0,   11001011,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"rep",		do_rep,		0,	1000,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"seq.int",	do_seq,		0,	1001,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"seq_along",	do_seq_along,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"seq_len",	do_seq_len,	0,	1001,	1,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};

/* Fast built-in functions in this file. See names.c for documentation */

attribute_hidden FASTFUNTAB R_FastFunTab_seq[] = {
/*slow func	fast func,     code or -1   dsptch  variant */

{ do_seq_len,	do_fast_seq_len,-1,		0,  VARIANT_SCALAR_STACK_OK },
{ 0,		0,		0,		0,  0 }
};
