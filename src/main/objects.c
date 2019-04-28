/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 2002-3	      The R Foundation
 *  Copyright (C) 1999-2007   The R Core Team.
 *
 *  The changes in pqR from R-2.15.0 distributed by the R Core Team are
 *  documented in the NEWS and MODS files in the top-level source directory.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

/*  This module contains support for S-style generic */
/*  functions and "class" support.  Gag, barf ...  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define USE_FAST_PROTECT_MACROS
#define R_USE_SIGNALS 1
#include "Defn.h"
#include <R_ext/RS.h> /* for Calloc, Realloc and for S4 object bit */

#include <helpers/helpers-app.h>

static SEXP GetObject(RCNTXT *cptr)
{
    SEXP s, a, b, formals, tag;

    b = cptr->callfun;
    if (TYPEOF(b) != CLOSXP) error(_("generic 'function' is not a function"));
    formals = FORMALS(b);

    tag = TAG(formals);
    a = cptr->promargs;

    if (tag == R_NilValue || tag == R_DotsSymbol)
        s = CAR(a);
    else {

        SEXP exact, partial, partial2;
        exact = partial = partial2 = R_NoObject;  /* Not R_NilValue! */

	for (b = a; b != R_NilValue ; b = CDR(b)) {
	    if (TAG(b) != R_NilValue) {
                int m = ep_match_exprs(tag,TAG(b));
                if (m != 0) {
                    if (m > 0) { 
                        if (exact != R_NoObject)
                            error (_("formal argument \"%s\" matched by multiple actual arguments"), 
                                   tag);
                        exact = CAR(b);
                    }
                    else {
                        if (partial == R_NoObject)
                            partial = CAR(b);
                        else
                            partial2 = CAR(b);
                    }
                }
            }
        }

        if (exact != R_NoObject)
            s = exact;
        else if (partial != R_NoObject) {
            if (partial2 != R_NoObject)
                error (_("formal argument \"%s\" matched by multiple actual arguments"), 
                       tag);
            s = partial;
        }
        else { /* no exact or partial match - use first untagged argument */
            for (b = a; b != R_NilValue; b = CDR(b))
                if (TAG(b) == R_NilValue) {
                    s = CAR(b);
                    break;
                }
            if (b == R_NilValue) /* no untagged argument */
                s = CAR(a);
                /* had once been the following?
                      error("failed to match argument for dispatch"); */
        }
    }

    if (TYPEOF(s) == PROMSXP) {
	if (PRVALUE_PENDING_OK(s) == R_UnboundValue)
	    s = forcePromise_v(s,VARIANT_PENDING_OK);
	else
	    s = PRVALUE_PENDING_OK(s);
    }

    return s;
}

static SEXP applyMethod (SEXP call, SEXP op, SEXP args, SEXP rho, 
                         SEXP *supplied, int variant)
{
    SEXP ans;

    variant &= ~ VARIANT_SCALAR_STACK_OK;

    if (TYPEOF(op) == SPECIALSXP) {
	int save = R_PPStackTop;
	const void *vmax = VMAXGET();
	R_Visible = TRUE;
	ans = CALL_PRIMFUN(call, op, args, rho, variant);
        if (PRIMVISON(op))
            R_Visible = TRUE;
        else if (PRIMVISOFF(op))
            R_Visible = FALSE;
	check_stack_balance(op, save);
	VMAXSET(vmax);
    }
    /* In other places we add a context to builtins when profiling,
       but we have not bothered here (as there seem to be no primitives
       used as methods, and this would have to be a primitive to be
       found).
     */
    else if (TYPEOF(op) == BUILTINSXP) {
	int save = R_PPStackTop;
	const void *vmax = VMAXGET();
	PROTECT(args = evalList(args, rho));
	R_Visible = TRUE;
	ans = CALL_PRIMFUN(call, op, args, rho, variant);
        if (PRIMVISON(op))
            R_Visible = TRUE;
        else if (PRIMVISOFF(op))
            R_Visible = FALSE;
	UNPROTECT(1);
	check_stack_balance(op, save);
	VMAXSET(vmax);
    }
    else if (TYPEOF(op) == CLOSXP) {
	ans = applyClosure_v(call, op, args, rho, supplied, variant);
    }
    else {
	ans = R_NilValue;
    }

    return ans;
}


/* "newintoold" -  a destructive matching of arguments; */
/* newargs comes first; any element of oldargs with */
/* a name that matches a named newarg is deleted; the */
/* two resulting lists are appended and returned. */
/* S claims to do this (white book) but doesn't seem to. */

static inline SEXP newintoold(SEXP _new, SEXP old)
{
    if (_new == R_NilValue) return R_NilValue;
    SETCDR(_new, newintoold(CDR(_new),old));
    while (old != R_NilValue) {
	if (TAG(old) != R_NilValue && TAG(old) == TAG(_new)) {
	    SETCAR(old, CAR(_new));
	    return CDR(_new);
	}
	old = CDR(old);
    }
    return _new;
}

static inline SEXP matchmethargs(SEXP oldargs, SEXP newargs)
{
    newargs = newintoold(newargs, oldargs);
    return listAppend(oldargs, newargs);
}

#ifdef S3_for_S4_warn /* not currently used */
static SEXP s_check_S3_for_S4 = R_NoObject;
void R_warn_S3_for_S4(SEXP method) {
  SEXP call;
  if(s_check_S3_for_S4 == R_NoObject)
    s_check_S3_for_S4 = install(".checkS3forS4");
  PROTECT(call = lang2(s_check_S3_for_S4, method));
  eval(call, R_MethodsNamespace);
  UNPROTECT(1);
}
#endif

/*  usemethod  -  calling functions need to evaluate the object
 *  (== 2nd argument).	They also need to ensure that the
 *  argument list is set up in the correct manner.
 *
 *    1. find the context for the calling function (i.e. the generic)
 *	 this gives us the unevaluated arguments for the original call
 *
 *    2. create an environment for evaluating the method and insert
 *	 a handful of variables (.Generic, .Class and .Method) into
 *	 that environment. Also copy any variables in the env of the
 *	 generic that are not formal (or actual) arguments.
 *
 *    3. fix up the argument list; it should be the arguments to the
 *	 generic matched to the formals of the method to be invoked */

SEXP R_LookupMethod(SEXP method, SEXP rho, SEXP callrho, SEXP defrho)
{
    SEXP val;

    if (TYPEOF(callrho) != ENVSXP) {
        if (callrho == R_NilValue)
	    error(_("use of NULL environment is defunct"));
        else 
            error(_("bad generic call environment"));
    }
    if (defrho == R_BaseEnv)
	defrho = R_BaseNamespace;
    else if (TYPEOF(defrho) != ENVSXP) {
        if (defrho == R_NilValue)
	    error(_("use of NULL environment is defunct"));
        else 
            error(_("bad generic definition environment"));
    }

    /* This evaluates promises */
    val = findFunMethod (method, callrho);
    if (isFunction(val))
	return val;

    SEXP table = findVarInFrame3 (defrho, R_S3MethodsTable, TRUE);
    if (TYPEOF(table) == PROMSXP)
        table = forcePromise(table);
    if (TYPEOF(table) == ENVSXP) {
        val = findVarInFrame3(table, method, TRUE);
        if (TYPEOF(val) == PROMSXP) 
            val = forcePromise(val);
        if (isFunction(val))
            return val;
    }
    return R_UnboundValue;
}

#ifdef UNUSED
static int match_to_obj(SEXP arg, SEXP obj) {
  return (arg == obj) ||
    (TYPEOF(arg) == PROMSXP && PRVALUE(arg) == obj);
}
#endif

/* look up the class name in the methods package table of S3 classes
   which should be explicitly converted when an S3 method is applied
   to an object from an S4 subclass.
*/
int isBasicClass(const char *ss) {
    static SEXP s_S3table = R_NoObject;
    if(s_S3table == R_NoObject) {
      s_S3table = findVarInFrame3(R_MethodsNamespace, install(".S3MethodsClasses"), TRUE);
      if(s_S3table == R_UnboundValue)
	error(_("No .S3MethodsClass table, can't use S4 objects with S3 methods (methods package not attached?)"));
	if (TYPEOF(s_S3table) == PROMSXP)  /* findVar... ignores lazy data */
	    s_S3table = eval(s_S3table, R_MethodsNamespace);
    }
    if(s_S3table == R_UnboundValue)
      return FALSE; /* too screwed up to do conversions */
    return findVarInFrame3(s_S3table, install(ss), FALSE) != R_UnboundValue;
}

SEXP strngsv;

int usemethod(const char *generic, SEXP obj, SEXP call, SEXP args,
	      SEXP rho, SEXP callrho, SEXP defrho, int variant, SEXP *ans)
{
    SEXP klass, method, sxp, setcl, s;
    int i, nclass;

    RCNTXT *cptr = R_GlobalContext;  /* Context from which UseMethod called */

    char buf[512];
    int hash;
    int len;

    PROTECT(klass = R_data_class2(obj));
    if (TYPEOF(klass) != STRSXP) {
        hash = 0;
        len = 0;
        goto not_found;
    }

    nclass = LENGTH(klass);

    for (len = 0; generic[len] != 0; len++) {
        buf[len] = generic[len];
        if (len >= (sizeof buf) - 2)
            error(_("class name too long in '%s'"), generic);
    }
    buf[len++] = '.';
    hash = Rf_char_hash_len (buf, len);
    const void *vmax = VMAXGET();

    for (i = 0; i < nclass; i++) {
        SEXP elt = STRING_ELT (klass, i);
        const char *ss = translateChar(elt);
        const int ss_len = ss == CHAR(elt) ? LENGTH(elt) : strlen(ss);
        if (len+ss_len >= sizeof buf)
            error(_("class name too long in '%s'"), generic);
        strcpy(buf+len,ss);
        method = installed_already_with_hash 
                   (buf, Rf_char_hash_more_len(hash,ss,ss_len));
        if (method != R_NoObject) {
            sxp = R_LookupMethod (method, rho, callrho, defrho);
            if (sxp != R_UnboundValue) {
                if (method == R_sort_list && CLOENV(sxp) == R_BaseNamespace)
                    continue; /* kludge because sort.list is not a method */
                PROTECT(sxp);
                if (i > 0) {
                    setcl = allocVector (STRSXP, nclass - i);
                    copy_string_elements (setcl, 0, klass, i, nclass-i);
                    setAttrib (setcl, R_previousSymbol, klass);
                } 
                else
                    setcl = klass;
                PROTECT(setcl);
                goto found;
            }
        }
        VMAXSET(vmax);
    }

not_found: ;

    if (len + 7 >= sizeof buf)  /* 7 = number of characters in "default" */
        error(_("class name too long in '%s'"), generic);
    strcpy (buf+len, "default");
    method = installed_already_with_hash
               (buf, Rf_char_hash_more_len(hash,"default",7));
    if (method != R_NoObject) {
        sxp = R_LookupMethod(method, rho, callrho, defrho);
        if (sxp != R_UnboundValue) {
            setcl = R_NilValue;
            PROTECT2(sxp,setcl);
            goto found;
        }
    }

    UNPROTECT(1);
    cptr->callflag = CTXT_RETURN;
    return 0;

found: ;

    SEXP op = cptr->callfun;

    SEXP matchedarg, newcall;
    newcall = LCONS(method,CDR(cptr->call)); /* previously duplicated - why? */
    matchedarg = cptr->promargs;
    PROTECT2(newcall,matchedarg);

    cptr->callflag = CTXT_GENERIC;

    if (RDEBUG(op) || RSTEP(op)) SET_RSTEP(sxp, 1);

    SEXP supplied[11];
    supplied[0] = R_NilValue;
    int nprotect = 0;

    if (TYPEOF(sxp) == CLOSXP) {

        SEXP genstr, methstr;
        PROTECT(genstr = mkString(generic));
        PROTECT(methstr = ScalarString(PRINTNAME(method)));
        nprotect += 2;

        SEXP bindings = R_NilValue;

        if (TYPEOF(op) == CLOSXP) {
            SEXP formals = FORMALS(op);
            /* Optimize for the typical case where tags of formals same as s. */
            SEXP t, u;
            u = formals;
            for (s = FRAME(cptr->cloenv); s != R_NilValue; s = CDR(s)) {
                if (TAG(s) == TAG(u)) {
                    u = CDR(u);
                    goto next;
                }
                for (t = formals; t != R_NilValue; t = CDR(t)) {
                    if (TAG(s) == TAG(t)) {
                        u = CDR(t);
                        goto next;
                    }
                }
                if (TAG(s) != R_dot_Class  && TAG(s) != R_dot_Generic
                 && TAG(s) != R_dot_Method && TAG(s) != R_dot_GenericCallEnv
                 && TAG(s) != R_dot_GenericDefEnv)
                    bindings = cons_with_tag (CAR(s), bindings, TAG(s));
              next: ;
            }
            PROTECT(bindings);
            nprotect += 1;
        }

        int i = 0;

        supplied[i++] = R_dot_Class;          supplied[i++] = setcl;
        supplied[i++] = R_dot_Generic;        supplied[i++] = genstr;
        supplied[i++] = R_dot_Method;         supplied[i++] = methstr;
        supplied[i++] = R_dot_GenericCallEnv; supplied[i++] = callrho;
        supplied[i++] = R_dot_GenericDefEnv;  supplied[i++] = defrho;

        supplied[i] = bindings;
    }

    *ans = applyMethod (newcall, sxp, matchedarg, rho, supplied, 
                        variant & VARIANT_WHOLE_BODY ? variant 
                          : variant | VARIANT_NOT_WHOLE_BODY);
    R_GlobalContext->callflag = CTXT_RETURN;
    UNPROTECT(nprotect+5);
    return 1;
}

/* Note: "do_usemethod" is not the only entry point to
   "usemethod". Things like [ and [[ call usemethod directly,
   hence do_usemethod should just be an interface to usemethod.
*/

/* This is a primitive SPECIALSXP */
static SEXP do_usemethod (SEXP call, SEXP op, SEXP args, SEXP env,
                          int variant)
{
    SEXP ans, generic, obj, val;
    SEXP callenv, defenv;
    SEXP argList;
    RCNTXT *cptr;
    static const char * const ap[2] = { "generic", "object" };

    if (TAG(args) == R_NilValue && CDR(args) == R_NilValue) /* typical case */
        argList = args;  /* note that "object" is not here explicitly */
    else
        argList = matchArgs_strings (ap, 2, args, call);
    PROTECT(argList);

    generic = CAR(argList);
    if (TYPEOF(generic) == SYMSXP /* quick pretest */
          && (generic == R_MissingArg || generic == R_MissingUnder))
        errorcall(call, _("there must be a 'generic' argument"));
    if (TYPEOF(generic) == STRSXP)
        ;  /* the usual case, no eval needed */
    else
        generic = eval (generic, env);
    if (TYPEOF(generic) != STRSXP || LENGTH(generic) != 1)
        errorcall(call, _("'generic' argument must be a character string"));
    PROTECT(generic);

    /* get environments needed for dispatching.
       callenv = environment from which the generic was called
       defenv = environment where the generic was defined */
    cptr = R_GlobalContext;
    if ( !(cptr->callflag & CTXT_FUNCTION) || cptr->cloenv != env)
        errorcall(call, _("'UseMethod' used in an inappropriate fashion"));
    callenv = cptr->sysparent;

    /* We need to find the generic to find out where it is defined.
       This is set up to avoid getting caught by things like

          mycoef <- function(x)
          {  mycoef <- function(x) stop("not this one")
             UseMethod("mycoef")
          }

        The generic need not be a closure (Henrik Bengtsson writes
        UseMethod("$"), although only functions are documented.)
    */

    SEXP generic_name = STRING_ELT(generic, 0);

    if (CHAR(generic_name)[0] == 0)
        errorcall(call, _("first argument must be a generic name"));

    const char *generic_trans = translateChar(generic_name);
    SEXP generic_symbol = generic_trans == CHAR(generic_name)
                           ? installChar(generic_name) : install(generic_trans);

    val = findFunMethod (generic_symbol, ENCLOS(env)); /* evaluates promises */

    defenv = TYPEOF(val) == CLOSXP ? CLOENV(val) : R_BaseNamespace;

    obj = CDR(argList) == R_NilValue || CADR(argList) == R_MissingArg 
           ? GetObject(cptr) : eval (CADR(argList), env);
    PROTECT (obj);

    if (!usemethod(generic_trans, obj, call, CDR(args),
                   env, callenv, defenv, variant, &ans)) {
        /* SHOULD FIX THIS TO PROTECT AGAINST BUFFER OVERFLOW */
        SEXP klass;
        int nclass;
        char cl[1000];
        PROTECT(klass = R_data_class2(obj));
        nclass = length(klass);
        if (nclass == 1)
            strcpy(cl, translateChar(STRING_ELT(klass, 0)));
        else {
            int i;
            strcpy(cl, "c('");
            for (i = 0; i < nclass; i++) {
                if (i > 0) strcat(cl, "', '");
                strcat(cl, translateChar(STRING_ELT(klass, i)));
            }
            strcat(cl, "')");
        }
        errorcall(call,
        _("no applicable method for '%s' applied to an object of class \"%s\""),
           generic_trans, cl);
    }

    UNPROTECT(3); /* obj, generic, argList  - but unnecessary? */

    if (variant & VARIANT_DIRECT_RETURN) {
        R_variant_result |= VARIANT_RTN_FLAG;
        return ans;
    }
    else     
        findcontext (CTXT_RETURN, env, ans); 
}

/*
   fixcall: fixes up the call when arguments to the function may
   have changed; for now we only worry about tagged args, appending
   them if they are not already there
*/

static inline SEXP fixcall(SEXP call, SEXP args)
{
    SEXP s, t;

    for(t = args; t != R_NilValue; t = CDR(t)) {
        SEXP tag = TAG(t);
        if (tag != R_NilValue) {
            for (s = call; CDR(s) != R_NilValue; s = CDR(s))
                if (TAG(CDR(s)) == tag)
                    goto next_arg;
            SETCDR (s, cons_with_tag (duplicate(CAR(t)), R_NilValue, tag));
        }
      next_arg: ;
    }

    return call;
}


/* equalS3Signature:  compares "signature" and "left.right";
   arguments must be non-null */

static inline Rboolean equalS3Signature(const char *signature, const char *left,
                                        const char *right)
{
    const char *s = signature;
    const char *a;

    for(a = left; *a; s++, a++) {
        if (*s != *a)
            return FALSE;
    }
    if (*s++ != '.')
        return FALSE;
    for(a = right; *a; s++, a++) {
        if (*s != *a)
            return FALSE;
    }
    return *s == 0;
}

/* If NextMethod has any arguments the first must be the generic */
/* the second the object and any remaining are matched with the */
/* formals of the chosen method. */

#define ARGUSED(x) LEVELS(x)

/* This is a special .Internal */
static SEXP do_nextmethod (SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    char buf[512], b[512], bb[512], tbuf[14];
    const char *sb, *sg, *sk;
    SEXP ans, s, t, klass, method, matchedarg, generic, nextfun;
    SEXP sysp, formals, actuals, tmp, newcall;
    SEXP a, group, basename;
    SEXP callenv, defenv;
    RCNTXT *cptr;
    int i, j;
    int len_klass;

    cptr = R_GlobalContext;
    cptr->callflag = CTXT_GENERIC;

    /* get the env NextMethod was called from */
    sysp = R_GlobalContext->sysparent;
    while (cptr != NULL) {
	if (cptr->callflag & CTXT_FUNCTION && cptr->cloenv == sysp) break;
	cptr = cptr->nextcontext;
    }
    if (cptr == NULL)
	error(_("'NextMethod' called from outside a function"));

    PROTECT(newcall = duplicate(cptr->call));

    /* eg get("print.ts")(1) */
    if (TYPEOF(CAR(cptr->call)) == LANGSXP)
       error(_("'NextMethod' called from an anonymous function"));

    /* Find dispatching environments. Promises shouldn't occur, but
       check to be on the safe side.  If the variables are not in the
       environment (the method was called outside a method dispatch)
       then chose reasonable defaults. */
    callenv = findVarInFrame3(R_GlobalContext->sysparent,
			      R_dot_GenericCallEnv, TRUE);
    if (TYPEOF(callenv) == PROMSXP)
	callenv = forcePromise(callenv);
    else if (callenv == R_UnboundValue)
        callenv = env;
    defenv = findVarInFrame3(R_GlobalContext->sysparent,
			     R_dot_GenericDefEnv, TRUE);
    if (TYPEOF(defenv) == PROMSXP) defenv = forcePromise(defenv);
    else if (defenv == R_UnboundValue) defenv = R_GlobalEnv;

    /* set up the arglist */
    if (TYPEOF(CAR(cptr->call)) == CLOSXP)
	// e.g., in do.call(function(x) NextMethod('foo'),list())
	s = CAR(cptr->call);
    else
	s = R_LookupMethod(CAR(cptr->call), env, callenv, defenv);
    if (s == R_UnboundValue)
	error(_("no calling generic was found: was a method called directly?"));
    if (TYPEOF(s) != CLOSXP){ /* R_LookupMethod looked for a function */
	errorcall(R_NilValue,
		  _("'function' is not a function, but of type %d"),
		  TYPEOF(s));
    }

    /* Get formals and actuals; matchArgs attaches the names of the formals to
       the actuals.  Then expand any ... that occurs. */
    formals = FORMALS(s);
    PROTECT(actuals = matchArgs_pairlist (formals, cptr->promargs, call));

    i = 0;
    for(t = actuals; t != R_NilValue; t = CDR(t)) {
	if(TAG(t) == R_DotsSymbol) {
            i = length(CAR(t)); 
            break;
        }
    }
    if(i) {   /* we need to expand out the dots */
        SEXP m;
	PROTECT(t = allocList(i+length(actuals)-1));
	for(s = actuals, m = t; s != R_NilValue; s = CDR(s)) {
	    if(TYPEOF(CAR(s)) == DOTSXP && i!=0) {
		for(i = 1, a = CAR(s); a != R_NilValue;
		    a = CDR(a), i++, m = CDR(m)) {
                    tbuf[0] = tbuf[1] = '.';
                    integer_to_string(tbuf+2,i);
		    SET_TAG(m, install(tbuf));
		    SETCAR(m, CAR(a));
		}
                i = 0; /* precaution just in case there are multiple ... args */
	    } else {
		SET_TAG(m, TAG(s));
		SETCAR(m, CAR(s));
		m = CDR(m);
	    }
	}
	UNPROTECT(1);
	actuals = t;
    }
    PROTECT(actuals);

    /* We can't duplicate because it would force the promises */
    /* so we do our own duplication of the promargs */

    PROTECT(matchedarg = allocList(length(cptr->promargs)));
    for (t = matchedarg, s = cptr->promargs; t != R_NilValue;
	 s = CDR(s), t = CDR(t)) {
	SETCAR(t, CAR(s));
	SET_TAG(t, TAG(s));
	for (SEXP m = actuals; m != R_NilValue; m = CDR(m))
	    if (CAR(m) == CAR(t))  {
		if (CAR(m) == R_MissingArg) {
		    tmp = findVarInFrame3(cptr->cloenv, TAG(m), TRUE);
		    if (tmp == R_MissingArg) break;
		}
		SETCAR(t, mkPROMISE(TAG(m), cptr->cloenv));
                if (TYPEOF(CAR(m)) == PROMSXP && STORE_GRAD(CAR(m)))
                    SET_STORE_GRAD (CAR(t), 1);
		break;
	   }
    }

    /* Now see if there were any other arguments passed in Currently
       we seem to only allow named args to change or to be added, this
       is at variance with p. 470 of the White Book */

    s = CADDR(args); /* this is ... and we need to see if it's bound */
    if (s == R_DotsSymbol) {
	t = findVarInFrame3(env, s, TRUE);
	if (t != R_NilValue && t != R_MissingArg) {
	    SET_TYPEOF(t, LISTSXP); /* a safe mutation */
	    s = matchmethargs(matchedarg, t);
	    UNPROTECT_PROTECT(matchedarg = s);
	    newcall = fixcall(newcall, matchedarg);
	}
    }
    else
	error(_("wrong argument ..."));

    /*
      .Class is used to determine the next method; if it doesn't
      exist the first argument to the current method is used
      the second argument to NextMethod is another option but
      isn't currently used).
    */
    klass = findVarInFrame3(R_GlobalContext->sysparent,
			    R_dot_Class, TRUE);

    if (klass == R_UnboundValue) {
	s = GetObject(cptr);
	if (!isObject(s)) error(_("object not specified"));
	klass = getClassAttrib(s);
    }
    len_klass = length(klass);

    /* the generic comes from either the sysparent or it's named */
    generic = findVarInFrame3(R_GlobalContext->sysparent,
			      R_dot_Generic, TRUE);
    if (generic == R_UnboundValue)
	generic = eval(CAR(args), env);
    if( generic == R_NilValue )
	error(_("generic function not specified"));
    PROTECT(generic);

    if (!isString(generic) || LENGTH(generic) != 1)
	error(_("invalid generic argument to NextMethod"));

    if (CHAR(STRING_ELT(generic, 0))[0] == '\0')
	error(_("generic function not specified"));

    /* Determine whether we are in a Group dispatch.  Also determine the root: 
       either the group or the generic will be it. */

    group = findVarInFrame3(R_GlobalContext->sysparent,
			    R_dot_Group, TRUE);

    if (group == R_UnboundValue) {
       group = R_BlankScalarString;
       basename = generic;
    } else {
       if (!isString(group) || LENGTH(group) != 1)
            error(_("invalid 'group' argument found in 'NextMethod'"));
       if (CHAR(STRING_ELT(group, 0))[0] == '\0') basename = generic;
       else basename = group;
    }
    PROTECT(group);

    nextfun = R_NilValue;

    /*
       Find the method currently being invoked and jump over the current call
       If t is R_UnboundValue then we called the current method directly
    */

    method = findVarInFrame3(R_GlobalContext->sysparent,
			     R_dot_Method, TRUE);
    if( method != R_UnboundValue) {
	const char *ss;
	if( !isString(method) )
	    error(_("wrong value for .Method"));
	for(i = 0; i < LENGTH(method); i++) {
	    ss = translateChar(STRING_ELT(method, i));
            if (!copy_1_string (b, sizeof b, ss))
		error(_("method name too long in '%s'"), ss);
	    if(*b!=0) break;
	}
	/* for binary operators check that the second argument's method
	   is the same or absent */
	for(j = i; j < LENGTH(method); j++){
	    const char *ss = translateChar(STRING_ELT(method, j));
            if (!copy_1_string (bb, sizeof bb, ss))
		error(_("method name too long in '%s'"), ss);
	    if (*bb!=0 && strcmp(b,bb))
	        warning(_("Incompatible methods ignored"));
	}
    }
    else {
        if (!copy_1_string (b, sizeof b, CHAR(PRINTNAME(CAR(cptr->call)))))
	   error(_("call name too long in '%s'"),
		 CHAR(PRINTNAME(CAR(cptr->call))));
    }

    sb = translateChar(STRING_ELT(basename, 0));
    for (j = 0; j < len_klass; j++) {
	sk = translateChar(STRING_ELT(klass, j));
        if (equalS3Signature(b, sb, sk))  /*  b == sb.sk */
            break;
    }

    if (j < len_klass) /* we found a match and start from there */
      j++;
    else
      j = 0;  /*no match so start with the first element of .Class */

    /* we need the value of i on exit from the for loop to figure out
	   how many classes to drop. */

    sg = translateChar(STRING_ELT(generic, 0));
    for (i = j ; i < len_klass; i++) {
	sk = translateChar(STRING_ELT(klass, i));
        if (!copy_3_strings (buf, sizeof buf, sg, ".", sk))
	    error(_("class name too long in '%s'"), sg);
	nextfun = R_LookupMethod(install(buf), env, callenv, defenv);
	if (nextfun != R_UnboundValue) 
            break;
	if (group != R_UnboundValue) {
	    /* if not Generic.foo, look for Group.foo */
            if (!copy_3_strings (buf, sizeof buf, sb, ".", sk))
		error(_("class name too long in '%s'"), sb);
	    nextfun = R_LookupMethod(install(buf), env, callenv, defenv);
	    if(nextfun != R_UnboundValue)
		break;
	}
	if (isFunction(nextfun))
	    break;
    }
    if (!isFunction(nextfun)) {
        if (!copy_2_strings (buf, sizeof buf, sg, ".default"))
            error(_("class name too long in '%s'"), sg);
	nextfun = R_LookupMethod(install(buf), env, callenv, defenv);
	/* If there is no default method, try the generic itself,
	   provided it is primitive or a wrapper for a .Internal
	   function of the same name.
	 */
	if (nextfun == R_UnboundValue) {
	    t = install(sg);
	    nextfun = findVar(t, env);
	    if (TYPEOF(nextfun) == PROMSXP)
		nextfun = forcePromise(nextfun);
	    if (!isFunction(nextfun))
		error(_("no method to invoke"));
	    if (TYPEOF(nextfun) == CLOSXP) {
		if (INTERNAL(t) != R_NilValue)
		    nextfun = INTERNAL(t);
		else
		    error(_("no method to invoke"));
	    }
	}
    }
    PROTECT(nextfun);

    SEXP supplied[13];
    supplied[0] = R_NilValue;
    int nprotect = 0;

    if (TYPEOF(nextfun) == CLOSXP) {

        PROTECT(klass = duplicate(klass));
        PROTECT(s = allocVector(STRSXP, len_klass - i));
        copy_string_elements (s, 0, klass, i, LENGTH(s));
        setAttrib(s, install("previous"), klass);
        /* It is possible that if a method was called directly that
            'method' is unset */
        if (method != R_UnboundValue) {
            /* for Ops we need `method' to be a vector */
            PROTECT(method = duplicate(method));
            int len_method = length(method);
            for(j = 0; j < len_method; j++) {
                if (CHAR(STRING_ELT(method,j))[0] != 0) /* not empty string */
                    SET_STRING_ELT(method, j,  mkChar(buf));
            }
        } else
            PROTECT(method = mkString(buf));

        nprotect = 3;

        int i = 0;

        supplied[i++] = R_dot_Class;          supplied[i++] = s;
        supplied[i++] = R_dot_Generic;        supplied[i++] = generic;
        supplied[i++] = R_dot_Method;         supplied[i++] = method;
        supplied[i++] = R_dot_GenericCallEnv; supplied[i++] = callenv;
        supplied[i++] = R_dot_GenericDefEnv;  supplied[i++] = defenv;
        supplied[i++] = R_dot_Group;          supplied[i++] = group;

        supplied[i] = R_NilValue;
    }

    
    SETCAR(newcall, install(buf));

    ans = applyMethod (newcall, nextfun, matchedarg, env, supplied, 
                       variant & ~VARIANT_WHOLE_BODY);

    UNPROTECT(nprotect+7);
    return(ans);
}

SEXP attribute_hidden Rf_makeUnclassed (SEXP a)
{
    if (isObject(a)) {
        PROTECT(a = dup_top_level(a));
        setAttrib(a, R_ClassSymbol, R_NilValue);
        UNPROTECT(1);
    }

    return a;
}

/* SPECIAL, so can pass on gradient. */

static SEXP do_unclass(SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    PROTECT (args = variant & VARIANT_GRADIENT 
                      ? evalList_gradient (args, env, 0, 1, 0)
                      : evalList (args, env));
    
    checkArity(op, args);
    check1arg_x (args, call);

    SEXP a = CAR(args);

    if (isObject(a)) {
        switch(TYPEOF(a)) {
        case ENVSXP:
            errorcall(call, _("cannot unclass an environment"));
            break;
        case EXTPTRSXP:
            errorcall(call, _("cannot unclass an external pointer"));
            break;
        default:
            if (variant & VARIANT_UNCLASS)
                R_variant_result = VARIANT_UNCLASS_FLAG;
            else
                a = Rf_makeUnclassed(a);
            break;
        }
    }

    if (! (variant & VARIANT_PENDING_OK))
        WAIT_UNTIL_COMPUTED(a);

    if (HAS_GRADIENT_IN_CELL(args)) {
        R_gradient = GRADIENT_IN_CELL(args);
        R_variant_result |= VARIANT_GRADIENT_FLAG;
    }

    UNPROTECT(1);
    return a;
}



/* NOTE: Fast  inherits(x, what)    in ../include/Rinlinedfuns.h
 * ----        ----------------- */
/** C API for  R  inherits(x, what, which)
 *
 * @param x any R object
 * @param what character vector
 * @param which logical: "want vector result" ?
 *
 * @return if which is false, logical TRUE or FALSE
 *	   if which is true, integer vector of length(what) ..
 */
SEXP inherits3(SEXP x, SEXP what, SEXP which)
{
    SEXP klass, rval = R_NilValue /* -Wall */;
    if(IS_S4_OBJECT(x))
	PROTECT(klass = R_data_class2(x));
    else
	PROTECT(klass = R_data_class(x, FALSE));
    int nclass = length(klass);

    if(!isString(what))
	error(_("'what' must be a character vector"));
    int j, nwhat = length(what);

    if( !isLogical(which) || (length(which) != 1) )
	error(_("'which' must be a length 1 logical vector"));
    int isvec = asLogical(which);

#ifdef _be_too_picky_
    if(IS_S4_OBJECT(x) && nwhat == 1 && !isvec &&
       !isNull(R_getClassDef(translateChar(STRING_ELT(what, 0)))))
	warning(_("use 'is()' instead of 'inherits()' on S4 objects"));
#endif

    if(isvec)
	PROTECT(rval = allocVector(INTSXP, nwhat));

    for(j = 0; j < nwhat; j++) {
	const char *ss = translateChar(STRING_ELT(what, j)); int i;
	if(isvec)
	    INTEGER(rval)[j] = 0;
	for(i = 0; i < nclass; i++) {
	    if(!strcmp(translateChar(STRING_ELT(klass, i)), ss)) {
		if(isvec)
		    INTEGER(rval)[j] = i+1;
		else {
		    UNPROTECT(1);
		    return mkTrue();
		}
		break;
	    }
	}
    }
    if(!isvec) {
    	UNPROTECT(1);
	return mkFalse();
    }
    UNPROTECT(2);
    return rval;
}

static SEXP do_inherits(SEXP call, SEXP op, SEXP args, SEXP env)
{
    checkArity(op, args);

    SEXP x = CAR(args);
    SEXP what = CADR(args);
    SEXP which = CADDR(args);

    WAIT_UNTIL_COMPUTED(what);
    WAIT_UNTIL_COMPUTED(which);

    return inherits3(x,what,which);
}


/**
 * Return the 0-based index of an is() match in a vector of class-name
 * strings terminated by an empty string.  Returns -1 for no match.
 *
 * @param x  an R object, about which we want is(x, .) information.
 * @param valid vector of possible matches terminated by an empty string.
 * @param rho  the environment in which the class definitions exist.
 *
 * @return index of match or -1 for no match
 */
int R_check_class_and_super(SEXP x, const char **valid, SEXP rho)
{
    int ans;
    SEXP cl = PROTECT(asChar(getClassAttrib(x)));
    const char *class = CHAR(cl);
    for (ans = 0; ; ans++) {
	if (valid[ans][0]==0) /* empty string */
	    break;
	if (strcmp(class, valid[ans])==0) {
            UNPROTECT(1); /* cl */
            return ans;
        }
    }
    /* if not found directly, now search the non-virtual super classes :*/
    if(IS_S4_OBJECT(x)) {
	/* now try the superclasses, i.e.,  try   is(x, "....");  superCl :=
	   .selectSuperClasses(getClass("....")@contains, dropVirtual=TRUE)  */
	SEXP classExts, superCl, _call;
	static SEXP s_contains = R_NoObject, s_selectSuperCl = R_NoObject;
	int i;
	if (s_contains == R_NoObject) {
	    s_contains      = install("contains");
	    s_selectSuperCl = install(".selectSuperClasses");
	}
	SEXP classDef = PROTECT(R_getClassDef(class));
	PROTECT(classExts = R_do_slot(classDef, s_contains));
	PROTECT(_call = lang3(s_selectSuperCl, classExts,
			      /* dropVirtual = */ ScalarLogical(1)));
	superCl = eval(_call, rho);
	UNPROTECT(3); /* _call, classExts, classDef */
	PROTECT(superCl);
        if (isString(superCl))
            for(i=0; i < LENGTH(superCl); i++) {
                const char *s_class = CHAR(STRING_ELT(superCl, i));
                for (ans = 0; ; ans++) {
                    if (valid[ans][0]==0) /* empty string */
                        break;
                    if (strcmp(s_class, valid[ans])==0) {
                        UNPROTECT(2); /* superCl, cl */
                        return ans;
                    }
                }
            }
	UNPROTECT(1); /* superCl */
    }
    UNPROTECT(1); /* cl */
    return -1;
}


/**
 * Return the 0-based index of an is() match in a vector of class-name
 * strings terminated by an empty string.  Returns -1 for no match.
 * Strives to find the correct environment() for is(), using .classEnv()
 * (from \pkg{methods}).
 *
 * @param x  an R object, about which we want is(x, .) information.
 * @param valid vector of possible matches terminated by an empty string.
 *
 * @return index of match or -1 for no match
 */
int R_check_class_etc(SEXP x, const char **valid)
{
    static SEXP meth_classEnv = R_NoObject;
    SEXP cl = getClassAttrib(x), rho = R_GlobalEnv, pkg;
    if (meth_classEnv == R_NoObject)
	meth_classEnv = install(".classEnv");

    pkg = getAttrib(cl, R_PackageSymbol); /* ==R== packageSlot(class(x)) */
    if(!isNull(pkg)) { /* find  rho := correct class Environment */
	SEXP clEnvCall;
	// FIXME: fails if 'methods' is not attached.
	PROTECT(clEnvCall = lang2(meth_classEnv, cl));
	rho = eval(clEnvCall, R_GlobalEnv);
	UNPROTECT(1);
	if(!isEnvironment(rho))
	    error(_("could not find correct environment; please report!"));
    }
    PROTECT(rho);
    int res = R_check_class_and_super(x, valid, rho);
    UNPROTECT(1);
    return res;
}

/*
   ==============================================================

     code from here on down is support for the methods package

   ==============================================================
*/

/* standardGeneric:  uses a pointer to R_standardGeneric, to be
   initialized when the methods package is attached.  When and if the
   methods code is automatically included, the pointer will not be
   needed

*/
static R_stdGen_ptr_t R_standardGeneric_ptr = 0;
static SEXP dispatchNonGeneric(SEXP name, SEXP env, SEXP fdef);
#define NOT_METHODS_DISPATCH_PTR(ptr) (ptr == 0 || ptr == dispatchNonGeneric)

R_stdGen_ptr_t R_get_standardGeneric_ptr(void)
{
    return R_standardGeneric_ptr;
}

R_stdGen_ptr_t R_set_standardGeneric_ptr(R_stdGen_ptr_t val, SEXP envir)
{
    R_stdGen_ptr_t old = R_standardGeneric_ptr;
    R_standardGeneric_ptr = val;
    if (envir != R_NoObject && !isNull(envir))
	R_MethodsNamespace = envir;
    /* just in case ... */
    if(!R_MethodsNamespace)
	R_MethodsNamespace = R_GlobalEnv;
    return old;
}

SEXP R_isMethodsDispatchOn(SEXP onOff) {
    SEXP value = allocVector1LGL();
    Rboolean onOffValue;
    R_stdGen_ptr_t old = R_get_standardGeneric_ptr();
    LOGICAL(value)[0] = !NOT_METHODS_DISPATCH_PTR(old);
    if(length(onOff) > 0) {
	    onOffValue = asLogical(onOff);
	    if(onOffValue == FALSE)
		    R_set_standardGeneric_ptr(0, 0);
	    else if(NOT_METHODS_DISPATCH_PTR(old)) {
		    SEXP call;
		    PROTECT(call = allocList(2));
		    SETCAR(call, install("initMethodsDispatch"));
		    eval(call, R_GlobalEnv); /* only works with
						methods	 attached */
		    UNPROTECT(1);
	    }
    }
    return value;
}

/* simpler version for internal use */

attribute_hidden
Rboolean isMethodsDispatchOn(void)
{
    return !NOT_METHODS_DISPATCH_PTR(R_standardGeneric_ptr);
}


static SEXP dispatchNonGeneric(SEXP name, SEXP env, SEXP fdef)
{
    /* dispatch the non-generic definition of `name'.  Used to trap
       calls to standardGeneric during the loading of the methods package */
    SEXP e, value, rho, fun, symbol;
    RCNTXT *cptr;
    /* find a non-generic function */
    symbol = install_translated (asChar(name));
    for(rho = ENCLOS(env); rho != R_EmptyEnv;
	rho = ENCLOS(rho)) {
	fun = findVarInFrame3(rho, symbol, TRUE);
	if(fun == R_UnboundValue) continue;
	switch(TYPEOF(fun)) {
	case CLOSXP:
	    value = findVarInFrame3(CLOENV(fun), R_dot_Generic, TRUE);
	    if(value == R_UnboundValue) break;
	case BUILTINSXP:  case SPECIALSXP:
	default:
	    /* in all other cases, go on to the parent environment */
	    break;
	}
	fun = R_UnboundValue;
    }
    fun = SYMVALUE(symbol);
    if(fun == R_UnboundValue)
	error(_("unable to find a non-generic version of function \"%s\""),
	      translateChar(asChar(name)));
    cptr = R_GlobalContext;
    /* check this is the right context */
    while (cptr != R_ToplevelContext) {
	if (cptr->callflag & CTXT_FUNCTION )
	    if (cptr->cloenv == env)
		break;
	cptr = cptr->nextcontext;
    }

    PROTECT(e = duplicate(R_syscall(0, cptr)));
    SETCAR(e, fun);
    /* evaluate a call the non-generic with the same arguments and from
       the same environment as the call to the generic version */
    value = eval(e, cptr->sysparent);
    UNPROTECT(1);
    return value;
}


static SEXP get_this_generic(SEXP args);

static SEXP do_standardGeneric(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP arg, value, fdef; R_stdGen_ptr_t ptr = R_get_standardGeneric_ptr();

    checkArity(op, args);
    check1arg(args, call, "f");

    if(!ptr) {
	warningcall(call,
		    _("standardGeneric called without methods dispatch enabled (will be ignored)"));
	R_set_standardGeneric_ptr(dispatchNonGeneric, R_NoObject);
	ptr = R_get_standardGeneric_ptr();
    }

    checkArity(op, args); /* set to -1 */
    arg = CAR(args);
    if(!isValidStringF(arg))
	errorcall(call,
		  _("argument to standardGeneric must be a non-empty character string"));

    PROTECT(fdef = get_this_generic(args));

    if(isNull(fdef))
	error(_("call to standardGeneric(\"%s\") apparently not from the body of that generic function"), translateChar(STRING_ELT(arg, 0)));

    value = (*ptr)(arg, env, fdef);

    UNPROTECT(1);
    return value;
}

static int maxMethodsOffset = 0, curMaxOffset;
static Rboolean allowPrimitiveMethods = TRUE;
typedef enum {NO_METHODS, NEEDS_RESET, HAS_METHODS, SUPPRESSED} prim_methods_t;

static prim_methods_t *prim_methods;
static SEXP *prim_generics;
static SEXP *prim_mlist;
#define DEFAULT_N_PRIM_METHODS 100

/* This is used in the methods package, in src/methods_list_dispatch.c */
SEXP R_set_prim_method(SEXP fname, SEXP op, SEXP code_vec, SEXP fundef,
		       SEXP mlist)
{
    const char *code_string;
    if(!isValidString(code_vec))
	error(_("argument 'code' must be a character string"));
    code_string = translateChar(asChar(code_vec));
    /* with a NULL op, turns all primitive matching off or on (used to avoid possible infinite
     recursion in methods computations*/
    if(op == R_NilValue) {
	SEXP value;
	value = allowPrimitiveMethods ? mkTrue() : mkFalse();
	switch(code_string[0]) {
	case 'c': case 'C':/* clear */
	    allowPrimitiveMethods = FALSE; break;
	case 's': case 'S': /* set */
	    allowPrimitiveMethods = TRUE; break;
	default: /* just report the current state */
	    break;
	}
	return value;
    }
    do_set_prim_method(op, code_string, fundef, mlist);
    return(fname);
}

SEXP R_primitive_methods(SEXP op)
{
    int offset = PRIMOFFSET(op);
    if(offset < 0 || offset > curMaxOffset)
	return R_NilValue;
    else {
	SEXP value = prim_mlist[offset];
	return value ? value : R_NilValue;
    }
}

SEXP R_primitive_generic(SEXP op)
{
    int offset = PRIMOFFSET(op);
    if(offset < 0 || offset > curMaxOffset)
	return R_NilValue;
    else {
	SEXP value = prim_generics[offset];
	return value ? value : R_NilValue;
    }
}

/* This is used in the methods package, in src/methods_list_dispatch.c */
SEXP do_set_prim_method(SEXP op, const char *code_string, SEXP fundef,
			SEXP mlist)
{
    int offset;
    prim_methods_t code = NO_METHODS; /* -Wall */
    SEXP value;

    Rboolean errorcase = FALSE;
    switch(code_string[0]) {
    case 'c': /* clear */
	code = NO_METHODS; break;
    case 'r': /* reset */
	code = NEEDS_RESET; break;
    case 's': /* set or suppress */
	switch(code_string[1]) {
	case 'e': code = HAS_METHODS; break;
	case 'u': code = SUPPRESSED; break;
	default: errorcase = TRUE;
	}
	break;
    default:
	errorcase = TRUE;
    }
    if(errorcase) {
	error(_("invalid primitive methods code (\"%s\"): should be \"clear\", \"reset\", \"set\", or \"suppress\""), code_string);
    }

    switch(TYPEOF(op)) {
    case BUILTINSXP: case SPECIALSXP:
	offset = PRIMOFFSET(op);
	break;
    default:
	error(_("invalid object: must be a primitive function"));
    }
    if(offset >= maxMethodsOffset) {
	int n;
	n = offset + 1;
	if(n < DEFAULT_N_PRIM_METHODS)
	    n = DEFAULT_N_PRIM_METHODS;
	if(n < 2*maxMethodsOffset)
	    n = 2 * maxMethodsOffset;
	if(prim_methods) {
	    int i;

	    prim_methods  = Realloc(prim_methods,  n, prim_methods_t);
	    prim_generics = Realloc(prim_generics, n, SEXP);
	    prim_mlist	  = Realloc(prim_mlist,	   n, SEXP);

	    /* Realloc does not clear the added memory, hence: */
	    for (i = maxMethodsOffset ; i < n ; i++) {
		prim_methods[i]	 = NO_METHODS;
		prim_generics[i] = R_NoObject;
		prim_mlist[i]	 = R_NoObject;
	    }
	}
	else {
            int i;
	    prim_methods  = Calloc(n, prim_methods_t);
	    prim_generics = Calloc(n, SEXP);
	    prim_mlist	  = Calloc(n, SEXP);
	    for (i = 0; i < n; i++) {
		prim_methods[i]	 = NO_METHODS;
		prim_generics[i] = R_NoObject;
		prim_mlist[i]	 = R_NoObject;
	    }
	}
	maxMethodsOffset = n;
    }
    if(offset > curMaxOffset)
	curMaxOffset = offset;
    prim_methods[offset] = code;
    /* store a preserved pointer to the generic function if there is not
       one there currently.  Unpreserve it if no more methods, but don't
       replace it otherwise:  the generic definition is not allowed to
       change while it's still defined! (the stored methods list can,
       however) */
    value = prim_generics[offset];
    if(code == SUPPRESSED) {} /* leave the structure alone */
    else if(code == NO_METHODS && prim_generics[offset] != R_NoObject) {
	R_ReleaseObject(prim_generics[offset]);
	prim_generics[offset] = R_NoObject;
	prim_mlist[offset] = R_NoObject;
    }
    else if(fundef != R_NoObject && !isNull(fundef) 
                                 && prim_generics[offset] == R_NoObject) {
	if(TYPEOF(fundef) != CLOSXP)
	    error(_("the formal definition of a primitive generic must be a function object (got type '%s')"),
		  type2char(TYPEOF(fundef)));
	R_PreserveObject(fundef);
	prim_generics[offset] = fundef;
    }
    if(code == HAS_METHODS) {
	if(mlist == R_NoObject || isNull(mlist)) {
	    /* turning methods back on after a SUPPRESSED */
	} else {
	    if(prim_mlist[offset] != R_NoObject)
		R_ReleaseObject(prim_mlist[offset]);
	    R_PreserveObject(mlist);
	    prim_mlist[offset] = mlist;
	}
    }
    return value;
}

static SEXP get_primitive_methods(SEXP op, SEXP rho)
{
    SEXP f, e, val;
    int nprotect = 0;
    PROTECT(f = allocVector(STRSXP, 1));  nprotect++;
    SET_STRING_ELT(f, 0, mkChar(PRIMNAME(op)));
    PROTECT(e = allocVector(LANGSXP, 2)); nprotect++;
    SETCAR(e, install("getGeneric"));
    val = CDR(e); SETCAR(val, f);
    val = eval(e, rho);
    /* a rough sanity check that this looks like a generic function */
    if(TYPEOF(val) != CLOSXP || !IS_S4_OBJECT(val))
	error(_("object returned as generic function \"%s\" doesn't appear to be one"), PRIMNAME(op));
    UNPROTECT(nprotect);
    return CLOENV(val);
}


/* get the generic function, defined to be the function definition for
the call to standardGeneric(), or for primitives, passed as the second
argument to standardGeneric.
*/
static SEXP get_this_generic(SEXP args)
{
    SEXP value = R_NilValue; static SEXP gen_name = R_NoObject;
    int i, n;
    RCNTXT *cptr;
    const char *fname;

    /* a second argument to the call, if any, is taken as the function */
    if(CDR(args) != R_NilValue)
	return CAR(CDR(args));
    /* else use sys.function (this is fairly expensive-- would be good
     * to force a second argument if possible) */
    PROTECT(args);
    if(gen_name == R_NoObject)
	gen_name = install("generic");
    cptr = R_GlobalContext;
    fname = translateChar(asChar(CAR(args)));
    n = framedepth(cptr);
    /* check for a matching "generic" slot */
    for(i=0;  i<n; i++) {
	SEXP rval = R_sysfunction(i, cptr);
	if(isObject(rval)) {
            PROTECT(rval);
	    SEXP generic = getAttrib(rval, gen_name);
	    if (TYPEOF(generic) == STRSXP &&
                  !strcmp(translateChar(asChar(generic)), fname)) {
                value = rval;
                UNPROTECT(1); /* rval */
                break;
	    }
            UNPROTECT(1); /* rval */
	}
    }
    UNPROTECT(1);
    return(value);
}

/* Could there be methods for this op?	Checks
   only whether methods are currently being dispatched and, if so,
   whether methods are currently defined for this op. */
Rboolean R_has_methods(SEXP op)
{
    R_stdGen_ptr_t ptr = R_get_standardGeneric_ptr(); int offset;
    if(NOT_METHODS_DISPATCH_PTR(ptr))
	return(FALSE);
    if(op == R_NoObject || TYPEOF(op) == CLOSXP) /* except for primitives, just test for the package */
	return(TRUE);
    if(!allowPrimitiveMethods) /* all primitives turned off by a call to R_set_prim */
	return FALSE;
    offset = PRIMOFFSET(op);
    if(offset > curMaxOffset || prim_methods[offset] == NO_METHODS
       || prim_methods[offset] == SUPPRESSED)
	return(FALSE);
    return(TRUE);
}

static SEXP deferred_default_object = R_NoObject;

SEXP R_deferred_default_method()
{
    if(deferred_default_object == R_NoObject)
	deferred_default_object = install("__Deferred_Default_Marker__");
    return(deferred_default_object);
}


static R_stdGen_ptr_t quick_method_check_ptr = NULL;
void R_set_quick_method_check(R_stdGen_ptr_t value)
{
    quick_method_check_ptr = value;
}

/* try to dispatch the formal method for this primitive op, by calling
   the stored generic function corresponding to the op.	 Requires that
   the methods be set up to return a special object rather than trying
   to evaluate the default (which would get us into a loop). */

/* The promisedArgs argument should be 1 if args is a list of promises, and
   0 if not, in which case this function will create a list of promises to
   pass to the method, using CDR(call) for the unevaluated arguments, and
   args for their values. */

/* Returns R_NoObject if it didn't actually dispatch, and otherwise the
   return value of the dispatched function. */

SEXP attribute_hidden
R_possible_dispatch(SEXP call, SEXP op, SEXP args, SEXP rho,
		    Rboolean promisedArgs)
{
    SEXP fundef, value, mlist=R_NilValue, s;
    int offset;
    prim_methods_t current;
    offset = PRIMOFFSET(op);
    if(offset < 0 || offset > curMaxOffset)
	error(_("invalid primitive operation given for dispatch"));
    current = prim_methods[offset];
    if(current == NO_METHODS || current == SUPPRESSED)
	return(R_NoObject);
    /* check that the methods for this function have been set */
    if(current == NEEDS_RESET) {
	/* get the methods and store them in the in-core primitive
	   method table.	The entries will be preserved via
	   R_preserveobject, so later we can just grab mlist from
	   prim_mlist */
	do_set_prim_method(op, "suppressed", R_NilValue, mlist);
	PROTECT(mlist = get_primitive_methods(op, rho));
	do_set_prim_method(op, "set", R_NilValue, mlist);
	current = prim_methods[offset]; /* as revised by do_set_prim_method */
	UNPROTECT(1);
    }
    mlist = prim_mlist[offset];
    if(mlist != R_NoObject && !isNull(mlist)
       && quick_method_check_ptr) {
	value = (*quick_method_check_ptr)(args, mlist, op);
	if(isPrimitive(value))
	    return(R_NoObject);
	if(isFunction(value)) {
	    /* found a method, call it with promised args */
	    if(!promisedArgs) {
		PROTECT(s = promiseArgsWithValues(CDR(call), rho, args, 0));
		value =  applyClosure(call, value, s, rho, NULL);
		UNPROTECT(1);
		return value;
	    } else
		return applyClosure(call, value, args, rho, NULL);
	}
	/* else, need to perform full method search */
    }
    fundef = prim_generics[offset];
    if(fundef == R_NoObject || TYPEOF(fundef) != CLOSXP)
	error(_("primitive function \"%s\" has been set for methods but no generic function supplied"),
	      PRIMNAME(op));
    /* To do:  arrange for the setting to be restored in case of an
       error in method search */
    if(!promisedArgs) {
	PROTECT(s = promiseArgsWithValues(CDR(call), rho, args, 0));
	value = applyClosure(call, fundef, s, rho, NULL);
	UNPROTECT(1);
    } else
	value = applyClosure(call, fundef, args, rho, NULL);
    prim_methods[offset] = current;
    if(value == deferred_default_object)
	return R_NoObject;
    else
	return value;
}

SEXP R_do_MAKE_CLASS(const char *what)
{
    static SEXP s_getClass = R_NoObject;
    SEXP e, call;
    if(!what)
	error(_("C level MAKE_CLASS macro called with NULL string pointer"));
    if (s_getClass == R_NoObject) s_getClass = install("getClass");
    PROTECT(call = allocVector(LANGSXP, 2));
    SETCAR(call, s_getClass);
    SETCAR(CDR(call), mkString(what));
    e = eval(call, R_GlobalEnv);
    UNPROTECT(1);
    return(e);
}

/* this very similar, but gives R_NoObject(?) instead of an error for a non-existing class */
SEXP R_getClassDef(const char *what)
{
    static SEXP s_getClassDef = R_NoObject;
    SEXP e, call;
    if(!what)
	error(_("R_getClassDef(.) called with NULL string pointer"));
    if (s_getClassDef == R_NoObject) s_getClassDef = install("getClassDef");
    PROTECT(call = allocVector(LANGSXP, 2));
    SETCAR(call, s_getClassDef);
    SETCAR(CDR(call), mkString(what));
    e = eval(call, R_GlobalEnv);
    UNPROTECT(1);
    return(e);
}

SEXP R_do_new_object(SEXP class_def)
{
    static SEXP s_virtual = R_NoObject, s_prototype, s_className;
    if (s_virtual == R_NoObject) {
        s_virtual = install("virtual");
        s_prototype = install("prototype");
        s_className = install("className");
    }

    if (class_def == R_NoObject)
        error(_("C level NEW macro called with null class definition pointer"));

    SEXP e;
    e = R_do_slot(class_def, s_virtual);
    if (asLogical(e) != 0) { /* includes NA, TRUE - anything other than FALSE */
        e = R_do_slot(class_def, s_className);
        error(_("trying to generate an object from a virtual class (\"%s\")"),
              translateChar(asChar(e)));
    }
    PROTECT(e = R_do_slot(class_def, s_className));

    SEXP value;
    PROTECT(value = R_do_slot(class_def, s_prototype));
    if (TYPEOF(value) == S4SXP) {
        SEXP a;
        PROTECT (a = Rf_attributes_dup (value, ATTRIB(value)));
        PROTECT (value = allocS4Object());
        SET_ATTRIB (value, a);
        UNPROTECT(3);
    }
    else {
        value = duplicate(value);
        UNPROTECT(1);
    }
    PROTECT(value);

    if (TYPEOF(value) == S4SXP || getAttrib(e, R_PackageSymbol) != R_NilValue)
    { /* Anything but an object from a base "class" (numeric, matrix,..) */
        setAttrib(value, R_ClassSymbol, e);
        SET_S4_OBJECT(value);
    }

    UNPROTECT(2); /* value, e */
    return value;
}

Rboolean attribute_hidden R_seemsOldStyleS4Object(SEXP object)
{
    SEXP klass;
    if(!isObject(object) || IS_S4_OBJECT(object)) return FALSE;
    /* We want to know about S4SXPs with no S4 bit */
    /* if(TYPEOF(object) == S4SXP) return FALSE; */
    klass = getClassAttrib(object);
    return (klass != R_NilValue && LENGTH(klass) == 1 &&
	    getAttrib(klass, R_PackageSymbol) != R_NilValue) ? TRUE: FALSE;
}



SEXP R_isS4Object(SEXP object)
{
    /* wanted: return isS4(object) ? mkTrue() : mkFalse(); */
    return IS_S4_OBJECT(object) ? mkTrue() : mkFalse(); ;
}

SEXP R_setS4Object(SEXP object, SEXP onOff, SEXP do_complete)
{
  Rboolean flag = asLogical(onOff), complete = asInteger(do_complete);
    if(flag == IS_S4_OBJECT(object))
	return object;
    else
      return asS4(object, flag, complete);
}

SEXP R_get_primname(SEXP object)
{
    SEXP f;
    if(TYPEOF(object) != BUILTINSXP && TYPEOF(object) != SPECIALSXP)
	error(_("'R_get_primname' called on a non-primitive"));
    PROTECT(f = allocVector(STRSXP, 1));
    SET_STRING_ELT(f, 0, mkChar(PRIMNAME(object)));
    UNPROTECT(1);
    return f;
}

Rboolean isS4(SEXP s)
{
    return IS_S4_OBJECT(s);
}

SEXP asS4(SEXP s, Rboolean flag, int complete)
{
    if (flag == IS_S4_OBJECT(s))
	return s;
    if (NAMEDCNT_GT_1(s))
	s = duplicate(s);
    if (flag)
        SET_S4_OBJECT(s);
    else {
	if(complete) {
	    SEXP value;
            PROTECT(s);
	    /* TENTATIVE:  how much does this change? */
	    if((value = R_getS4DataSlot(s,ANYSXP)) != R_NilValue 
                  && !IS_S4_OBJECT(value)) {
                UNPROTECT(1);
                return value;
            }
	    /* else no plausible S3 object*/
	    else if (complete == 1) /* ordinary case (2, for conditional) */
                error(_("Object of class \"%s\" does not correspond to a valid S3 object"),
		      CHAR(STRING_ELT(R_data_class(s, FALSE), 0)));
	    else {
                UNPROTECT(1);
                return s; /*  unchanged */
            }
	}
	UNSET_S4_OBJECT(s);
    }
    return s;
}

/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_objects[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"UseMethod",	do_usemethod,	0,	1200,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"NextMethod",	do_nextmethod,	0,	1210,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"unclass",	do_unclass,	0,	1000,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"inherits",	do_inherits,	0,	10011,	3,	{PP_FUNCALL, PREC_FN,	0}},
{"standardGeneric",do_standardGeneric,0, 201,	-1,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};
