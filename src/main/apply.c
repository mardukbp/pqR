/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 2000-10  The R Core Team
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


/* .Internal(lapply(X, FUN)) */

/* This is a special .Internal, so has unevaluated arguments.  It is
   called from a closure wrapper, so X and FUN are promises. */

static SEXP do_lapply(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP R_fcall, ans, names, X, XX, FUN, dotsv, End;
    int i, n, no_dots;
    PROTECT_INDEX px;

    checkArity(op, args);
    FUN = CADR(args);  /* must be unevaluated for use in e.g. bquote */

    PROTECT_WITH_INDEX (X = CAR(args), &px);
    PROTECT (XX = eval(CAR(args), rho));

    n = length(XX);
    if (n == NA_INTEGER) error(_("invalid length"));

    dotsv = findVarInFrame3 (rho, R_DotsSymbol, 3);
    no_dots = dotsv==R_MissingArg || dotsv==R_NilValue || dotsv==R_UnboundValue;

    PROTECT(ans = allocVector(VECSXP, n));
    names = getAttrib(XX, R_NamesSymbol);
    if(!isNull(names)) setAttrib(ans, R_NamesSymbol, names);

    /* Build call: FUN(XX[[<ind>]], ...), with ... omitted if not there.

       The R level code has ensured that XX is a vector.  If it is
       atomic we can speed things up slightly by using the evaluated
       version (since it is self-evaluating), unless the gradient is
       needed as well.

       Don't try to reuse the cell holding the index - causes problems. */

    if (isVectorAtomic(XX) && !(variant & VARIANT_GRADIENT)) 
        X = XX;

    PROTECT(End = no_dots ? R_NilValue : CONS(R_DotsSymbol, R_NilValue));

    SEXP grad;
    PROTECT_INDEX gix;
    PROTECT_WITH_INDEX (grad = R_NilValue, &gix);

    for(i = 0; i < n; i++) {
        PROTECT(R_fcall = LCONS (FUN, 
                           CONS (LCONS(R_Bracket2Symbol,
                                  CONS(X, CONS(ScalarInteger(i+1),R_NilValue))),
                                 End)));
        SEXP v = evalv (R_fcall, rho, variant & VARIANT_GRADIENT);
        UNPROTECT(1);
        SEXP g = R_variant_result & VARIANT_GRADIENT_FLAG
                  ? R_gradient : R_NilValue;
        SET_VECTOR_ELEMENT_TO_VALUE (ans, i, v);
        if (g != R_NilValue) {
            grad = subassign_list_gradient (grad, g, i, n);
            REPROTECT (g, gix);
        }
    }

    if (grad != R_NilValue) {
        R_gradient = grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(5); /* X, XX, ans, End, grad */
    return ans;
}

/* .Internal(vapply(X, FUN, FUN.VALUE, USE.NAMES)) */

/* This is a special .Internal */
static SEXP do_vapply(SEXP call, SEXP op, SEXP args, SEXP rho, int variant)
{
    SEXP R_fcall, ans, X, XX, FUN, value, dim_v, dotsv, End;
    int i, n, commonLen, useNames, no_dots;
    Rboolean array_value;
    SEXPTYPE commonType;
    PROTECT_INDEX index;

    SEXP names=R_NilValue, rowNames=R_NilValue;
    int rnk_v = -1; // = array_rank(value) := length(dim(value))

    checkArity(op, args);
    PROTECT(X = CAR(args));
    PROTECT(XX = eval(CAR(args), rho));
    FUN = CADR(args);  /* must be unevaluated for use in e.g. bquote */
    PROTECT(value = eval(CADDR(args), rho));
    if (!isVector(value)) error(_("FUN.VALUE must be a vector"));
    useNames = asLogical(eval(CADDDR(args), rho));
    if (useNames == NA_LOGICAL) error(_("invalid USE.NAMES value"));

    n = length(XX);
    if (n == NA_INTEGER) error(_("invalid length"));

    dotsv = findVarInFrame3 (rho, R_DotsSymbol, 3);
    no_dots = dotsv==R_MissingArg || dotsv==R_NilValue || dotsv==R_UnboundValue;

    commonLen = length(value);
    commonType = TYPEOF(value);
    dim_v = getDimAttrib(value);
    array_value = TYPEOF(dim_v) == INTSXP && LENGTH(dim_v) >= 1;
    PROTECT(ans = allocVector(commonType, n*commonLen));
    if (useNames) {
    	PROTECT(names = getAttrib(XX, R_NamesSymbol));
    	if (isNull(names) && TYPEOF(XX) == STRSXP) {
    	    UNPROTECT(1);
    	    PROTECT(names = XX);
    	}
    	PROTECT_WITH_INDEX(rowNames = 
          getAttrib(value, array_value ? R_DimNamesSymbol : R_NamesSymbol), 
          &index);
    }

    /* Build call: FUN(XX[[<ind>]], ...), with ... omitted if not there.

       The R level code has ensured that XX is a vector.  If it is
       atomic we can speed things up slightly by using the evaluated
       version (since it is self-evaluating), unless the gradient is
       needed as well.

       Don't try to reuse the cell holding the index - causes problems. */

    if (isVectorAtomic(XX) && !(variant & VARIANT_GRADIENT)) 
        X = XX;

    SEXP grad;
    PROTECT_INDEX gix;
    PROTECT_WITH_INDEX (grad = R_NilValue, &gix);

    PROTECT(End = no_dots ? R_NilValue : CONS(R_DotsSymbol, R_NilValue));

    for(i = 0; i < n; i++) {
        SEXPTYPE tmpType;
        SEXP tmp;
        PROTECT(R_fcall = LCONS (FUN, 
                           CONS (LCONS(R_Bracket2Symbol,
                                  CONS(X, CONS(ScalarInteger(i+1),R_NilValue))),
                                 End)));
        tmp = evalv (R_fcall, rho, variant & VARIANT_GRADIENT);
        SEXP g = R_variant_result & VARIANT_GRADIENT_FLAG
                  ? R_gradient : R_NilValue;
        UNPROTECT(1); /* R_fcall */
        PROTECT2(tmp,g);
        if (length(tmp) != commonLen)
            error(_("values must be length %d,\n but FUN(X[[%d]]) result is length %d"),
                  commonLen, i+1, length(tmp));
        tmpType = TYPEOF(tmp);
        if (tmpType != commonType) {
            Rboolean okay = FALSE;
            switch (commonType) {
            case CPLXSXP: 
                okay = (tmpType == REALSXP) || (tmpType == INTSXP)
                         || (tmpType == LGLSXP); 
                break;
            case REALSXP: 
                okay = (tmpType == INTSXP) || (tmpType == LGLSXP); 
                break;
            case INTSXP:  
                okay = (tmpType == LGLSXP); 
                break;
            }
            if (!okay)
                error(_("values must be type '%s',\n but FUN(X[[%d]]) result is type '%s'"),
                      type2char(commonType), i+1, type2char(tmpType));
        }
        /* Take row names from the first result only */
        if (i == 0 && useNames && isNull(rowNames))
            REPROTECT(rowNames = 
              getAttrib (tmp, array_value ? R_DimNamesSymbol : R_NamesSymbol),
              index);
        if (tmpType != commonType)
            copy_elements_coerced (ans, i*commonLen, 1, tmp, 0, 1, commonLen);
        else {
            switch (commonType) {
            case CPLXSXP: 
            case REALSXP: 
            case INTSXP:  
            case LGLSXP:
            case RAWSXP:
            case STRSXP:
                copy_elements (ans, i*commonLen, 1, tmp, 0, 1, commonLen);
                break;
            case VECSXP:
                if (NAMEDCNT_EQ_0(tmp))  /* needn't duplicate elements */
                    copy_vector_elements (ans, i*commonLen, tmp, 0, commonLen);
                else  /* need to duplicate elements */
                    copy_elements (ans, i*commonLen, 1, tmp, 0, 1, commonLen);
                break;
            default:
                error(_("type '%s' is not supported"), type2char(commonType));
            }
        }

        if (g != R_NilValue) {
            if (commonType == VECSXP)
                grad = subassign_range_list_gradient 
                  (grad, g, i*commonLen, i*commonLen+commonLen-1, n*commonLen);
            else if (commonType == REALSXP)
                grad = subassign_range_numeric_gradient 
                  (grad, g, i*commonLen, i*commonLen+commonLen-1, n*commonLen);
            REPROTECT (grad, gix);
        }

        UNPROTECT(2); /* tmp, g */
    }

    UNPROTECT(1); /* End */

    if (commonLen != 1) {
	SEXP dim;
	rnk_v = array_value ? LENGTH(dim_v) : 1;
	PROTECT(dim = allocVector(INTSXP, rnk_v+1));
	if(array_value)
	    for(int j=0; j < rnk_v; j++)
		INTEGER(dim)[j] = INTEGER(dim_v)[j];
	else
	    INTEGER(dim)[0] = commonLen;
	INTEGER(dim)[rnk_v] = n;
	setAttrib(ans, R_DimSymbol, dim);
	UNPROTECT(1);
    }

    if (useNames) {
	if (commonLen == 1) {
	    if(!isNull(names)) setAttrib(ans, R_NamesSymbol, names);
	} else {
	    if (!isNull(names) || !isNull(rowNames)) {
		SEXP dimnames;
		PROTECT(dimnames = allocVector(VECSXP, rnk_v+1));
		if(array_value && !isNull(rowNames)) {
		    if(TYPEOF(rowNames) != VECSXP || LENGTH(rowNames) != rnk_v)
			// should never happen ..
			error(_("dimnames(<value>) is neither NULL nor list of length %d"),
			      rnk_v);
		    for(int j=0; j < rnk_v; j++)
			SET_VECTOR_ELT(dimnames, j, VECTOR_ELT(rowNames, j));
		} else
		    SET_VECTOR_ELT(dimnames, 0, rowNames);

		SET_VECTOR_ELT(dimnames, rnk_v, names);
		setAttrib(ans, R_DimNamesSymbol, dimnames);
		UNPROTECT(1);
	    }
	}
    }

    if (grad != R_NilValue) {
        R_gradient = grad;
        R_variant_result = VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(useNames ? 7 : 5); /* X, XX, value, ans + maybe names, 
                                    grad, rowNames */
    return ans;
}

static SEXP rapply_one(SEXP X, SEXP FUN, SEXP classes, SEXP deflt,
		   Rboolean replace, int no_dots, SEXP rho)
{
    SEXP ans, names, klass, R_fcall;
    int i, j, n;
    Rboolean matched = FALSE;

    /* if X is a list, recurse.  Otherwise if it matches classes call f */
    if(isNewList(X)) {
	n = length(X);
	PROTECT(ans = allocVector(VECSXP, n));
	names = getAttrib(X, R_NamesSymbol);
	/* or copy attributes if replace = TRUE? */
	if(!isNull(names)) setAttrib(ans, R_NamesSymbol, names);
	for(i = 0; i < n; i++)
	    SET_VECTOR_ELT(ans, i, rapply_one(VECTOR_ELT(X, i), FUN, classes,
					  deflt, replace, no_dots, rho));
	UNPROTECT(1);
	return ans;
    }
    if(strcmp(CHAR(STRING_ELT(classes, 0)), "ANY") == 0) /* ASCII */
	matched = TRUE;
    else {
	PROTECT(klass = R_data_class(X, FALSE));
	for(i = 0; i < LENGTH(klass) && !matched; i++)
	    for(j = 0; j < LENGTH(classes) && !matched; j++)
		if(SEQL(STRING_ELT(klass, i), STRING_ELT(classes, j)))
		    matched = TRUE;
	UNPROTECT(1);
    }
    if(matched) {
	if (no_dots)
            PROTECT(R_fcall = lang2(FUN, X));
        else
	    PROTECT(R_fcall = lang3(FUN, X, R_DotsSymbol));
	ans = eval(R_fcall, rho);
	UNPROTECT(1);
        return NAMEDCNT_EQ_0(ans) ? ans : duplicate(ans);
    } 
    else
        return duplicate (replace ? X : deflt);
}

static SEXP do_rapply(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP X, FUN, classes, deflt, how, ans, names, dotsv;
    int i, n, no_dots;
    Rboolean replace;

    checkArity(op, args);
    X = CAR(args); args = CDR(args);
    FUN = CAR(args); args = CDR(args);
    if(!isFunction(FUN)) error(_("invalid '%s' argument"), "f");
    classes = CAR(args); args = CDR(args);
    if(!isString(classes)) error(_("invalid '%s' argument"), "classes");
    deflt = CAR(args); args = CDR(args);
    how = CAR(args);
    if(!isString(how)) error(_("invalid '%s' argument"), "how");
    replace = strcmp(CHAR(STRING_ELT(how, 0)), "replace") == 0; /* ASCII */

    dotsv = findVarInFrame3 (rho, R_DotsSymbol, 3);
    no_dots = dotsv==R_MissingArg || dotsv==R_NilValue || dotsv==R_UnboundValue;
    n = length(X);
    PROTECT(ans = allocVector(VECSXP, n));
    names = getAttrib(X, R_NamesSymbol);
    /* or copy attributes if replace = TRUE? */
    if(!isNull(names)) setAttrib(ans, R_NamesSymbol, names);
    for(i = 0; i < n; i++)
	SET_VECTOR_ELT(ans, i, rapply_one(VECTOR_ELT(X, i), FUN, classes, deflt,
				          replace, no_dots,rho));
    UNPROTECT(1);
    return ans;
}

static Rboolean islistfactor(SEXP X)
{
    int i, n = length(X);

    if(n == 0) return FALSE;
    switch(TYPEOF(X)) {
    case VECSXP:
    case EXPRSXP:
	for(i = 0; i < LENGTH(X); i++)
	    if(!islistfactor(VECTOR_ELT(X, i))) return FALSE;
	return TRUE;
	break;
    }
    return isFactor(X);
}


/* is this a tree with only factor leaves? */

static SEXP do_islistfactor(SEXP call, SEXP op, SEXP args, SEXP rho)
{
    SEXP X;
    Rboolean lans = TRUE, recursive;
    int i, n;

    checkArity(op, args);
    X = CAR(args);
    recursive = asLogical(CADR(args));
    n = length(X);
    if(n == 0 || !isVectorList(X)) {
	lans = FALSE;
	goto do_ans;
    }
    if(!recursive) {
    for(i = 0; i < LENGTH(X); i++)
	if(!isFactor(VECTOR_ELT(X, i))) {
	    lans = FALSE;
	    break;
	}
    } else {
	switch(TYPEOF(X)) {
	case VECSXP:
	case EXPRSXP:
	    break;
	default:
	    goto do_ans;
	}
	for(i = 0; i < LENGTH(X); i++)
	    if(!islistfactor(VECTOR_ELT(X, i))) {
		lans = FALSE;
		break;
	    }
    }
do_ans:
    return ScalarLogicalMaybeConst(lans);
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_apply[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"lapply",	do_lapply,	0,	1010,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"vapply",	do_vapply,	0,	1010,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"rapply",	do_rapply,	0,	11,	5,	{PP_FUNCALL, PREC_FN,	0}},
{"islistfactor",do_islistfactor,0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};
