/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019 by Radford M. Neal
 *
 *  Based on R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1997--2012  The R Core Team
 *  Copyright (C) 2003, 2004  The R Foundation
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

#define __R_Names__ /* used in Defn.h for extern on R_FunTab */
#define USE_FAST_PROTECT_MACROS
#define R_USE_SIGNALS 1
#include <Defn.h>
#include <Print.h>

#include <Rinterface.h>
#include <lphash/lphash-app.h>

#undef NOT_LVALUE         /* Allow PRINTNAME, etc. on left of assignment here */
#define NOT_LVALUE(x) (x) /* since it's needed to set up R_MissingUnder  */


#define HSIZE (1<<14)   /* Initial size of lphash symbol table (a power of 2) */

#define MAXIDSIZE 10000	/* Largest symbol size, in bytes excluding terminator.
			   Was 256 prior to 2.13.0, now just a sanity check */


/* Table of .Internal(.) and .Primitive(.) R functions
 *
 * Each entry is a line with
 *
 *  printname	c-entry	 offset	 eval	arity	  pp-kind   precedence	    rightassoc
 *  ---------	-------	 ------	 ----	-----	  -------   ----------	    ----------
 *2 name	cfun	 code	 eval	arity	  gram.kind gram.precedence gram.rightassoc
 *3 PRIMNAME	PRIMFUN	 PRIMVAL [*]    PRIMARITY PPINFO    PPINFO	    PPINFO
 *
 * where "2" are the component names of the FUNTAB struct (Defn.h)
 * and	 "3" are the accessor macros. [*]: PRIMPRINT(.) uses the eval component
 *
 * printname:	The function name in R
 *
 * c-entry:	The name of the corresponding C function,
 *		actually declared in ../include/Internal.h .
 *		Convention:
 *		 - all start with "do_",
 *		 - all return SEXP.
 *		 - have argument list
 *		      (SEXP call, SEXP op, SEXP args, SEXP env)
 *                 or 
 *                    (SEXP call, SEXP op, SEXP args, SEXP env, int variant)
 *
 * offset:	the 'op' (offset pointer) above; used for C functions
 *		which deal with more than one R function...
 *
 * eval:	= STUVWXYZ (8/9 digits) --- where e.g. '1' means '00000001'
 *              S   (for internals only, possibly two digits) If S is 0, don't
 *                  ask for for gradient of any arguments; otherwise, S&7 is
 *                  the number of arguments that might have gradient, except
 *                  that S>>3 initial ones don't.  Gradients are attached 
 *                  to CONS cells holding arguments by evalList_gradient.
 *              T=1 says do special processing for BUILTIN internal function
 *                  when called with VARIANT_WHOLE_BODY
 *              T=0 not what it says above
 *              U=1 says that this is a subassign primitive that is written to
 *                  be able to use the fast interface
 *              U=0 not what it says above
 *              V=1 says that the (builtin) primitive can handle operands
 *                  whose computation by a task may be pending.  Currently
 *                  effective only for internal functions
 *              V=0 says must not be passed operands still being computed
 *              W=1 says pass a "variant" argument to the c-entry procedure
 *              W=0 says don't pass a "variant" argument
 *		X=0 says that we should force R_Visible on
 *		X=1 for internals says that we should force R_Visible off
 *		X=2 (and X=1 for primitives) says that we should let the code
 *                  set R_Visible, but default to TRUE
 *		Y=1 says that this is an internal function which must
 *		    be accessed with a	.Internal(.) call, any other value is
 *		    accessible directly and printed in R as ".Primitive(..)".
 *		Z=1 says evaluate arguments before calling (BUILTINSXP) and
 *		Z=0 says don't evaluate (SPECIALSXP).
 *
 * arity:	How many arguments are required/allowed;  "-1"	meaning ``any''
 *
 * pp-kind:	Deparsing Info (-> PPkind in ../include/Defn.h )
 *              --- NO LONGER USED, EXCEPT IN PRIMFOREIGN ---
 *
 * precedence:  Operator precedence (-> PPprec in ../include/Defn.h )
 *              --- NO LONGER USED ---
 *
 * rightassoc:  Right (1) or left (0) associative operator
 *              --- NO LONGER USED ---
 */


/* Entries for the FUNTAB table are defined in smaller tables in the source 
   files where the functions are defined, and copied to the table below on
   initialization.  Some entries are defined in this source file (names.c). */

attribute_hidden FUNTAB R_FunTab[R_MAX_FUNTAB_ENTRIES+1]; 
                                /* terminated by NULL name */

/* Pointers to the FUNTAB tables in the various source files, to be copied to
   R_FunTab.  Their names have the form R_FunTab_srcfilename. */

extern FUNTAB 
    R_FunTab_eval[], 
    R_FunTab_bytecode[],
    R_FunTab_arithmetic[], 
    R_FunTab_complex[], 
    R_FunTab_array[], 
    R_FunTab_summary[], 
    R_FunTab_seq[], 
    R_FunTab_coerce[], 
    R_FunTab_attrib[], 
    R_FunTab_names[],
    R_FunTab_serialize[],

    R_FunTab_CommandLineArgs[],
    R_FunTab_RNG[],
    R_FunTab_Rdynload[],
    R_FunTab_Renviron[],
    R_FunTab_agrep[],
    R_FunTab_apply[],
    R_FunTab_bind[],
    R_FunTab_builtin[],
    R_FunTab_character[],
    R_FunTab_colors[],
    R_FunTab_connections[],
    R_FunTab_context[],
    R_FunTab_cov[],
    R_FunTab_cum[],
    R_FunTab_datetime[],
    R_FunTab_dcf[],
    R_FunTab_debug[],
    R_FunTab_deparse[],
    R_FunTab_deriv[],
    R_FunTab_devices[],
    R_FunTab_dotcode[],
    R_FunTab_dounzip[],
    R_FunTab_engine[],
    R_FunTab_envir[],
    R_FunTab_errors[],
    R_FunTab_fourier[],
    R_FunTab_gradient[],
    R_FunTab_gramLatex[],
    R_FunTab_gramRd[],
    R_FunTab_grep[],
    R_FunTab_identical[],
    R_FunTab_inspect[],
    R_FunTab_internet[],
    R_FunTab_list[],
    R_FunTab_main[],
    R_FunTab_memory[],
    R_FunTab_model[],
    R_FunTab_objects[],
    R_FunTab_optim[],
    R_FunTab_optimize[],
    R_FunTab_options[],
    R_FunTab_par[],
    R_FunTab_paste[],
    R_FunTab_platform[],
    R_FunTab_plot[],
    R_FunTab_plot3d[],
    R_FunTab_print[],
    R_FunTab_profile[],
    R_FunTab_qsort[],
    R_FunTab_radixsort[],
    R_FunTab_random[],
    R_FunTab_raw[],
    R_FunTab_saveload[],
    R_FunTab_scan[],
    R_FunTab_sort[],
    R_FunTab_source[],
    R_FunTab_split[],
    R_FunTab_sprintf[],
    R_FunTab_subset[],
    R_FunTab_sysutils[],
    R_FunTab_unique[],
    R_FunTab_util[],
    R_FunTab_version[],

    R_FunTab_lapack[];

static FUNTAB *FunTab_ptrs[] = { 
    R_FunTab_eval,
    R_FunTab_bytecode,
    R_FunTab_arithmetic, 
    R_FunTab_complex, 
    R_FunTab_array,
    R_FunTab_summary,
    R_FunTab_seq,
    R_FunTab_coerce,
    R_FunTab_attrib,
    R_FunTab_names,
    R_FunTab_serialize,

    R_FunTab_CommandLineArgs,
    R_FunTab_RNG,
    R_FunTab_Rdynload,
    R_FunTab_Renviron,
    R_FunTab_agrep,
    R_FunTab_apply,
    R_FunTab_bind,
    R_FunTab_builtin,
    R_FunTab_character,
    R_FunTab_colors,
    R_FunTab_connections,
    R_FunTab_context,
    R_FunTab_cov,
    R_FunTab_cum,
    R_FunTab_datetime,
    R_FunTab_dcf,
    R_FunTab_debug,
    R_FunTab_deparse,
    R_FunTab_deriv,
    R_FunTab_devices,
    R_FunTab_dotcode,
    R_FunTab_dounzip,
    R_FunTab_engine,
    R_FunTab_envir,
    R_FunTab_errors,
    R_FunTab_fourier,
    R_FunTab_gradient,
    R_FunTab_gramLatex,
    R_FunTab_gramRd,
    R_FunTab_grep,
    R_FunTab_identical,
    R_FunTab_inspect,
    R_FunTab_internet,
    R_FunTab_list,
    R_FunTab_main,
    R_FunTab_memory,
    R_FunTab_model,
    R_FunTab_objects,
    R_FunTab_optim,
    R_FunTab_optimize,
    R_FunTab_options,
    R_FunTab_par,
    R_FunTab_paste,
    R_FunTab_platform,
    R_FunTab_plot,
    R_FunTab_plot3d,
    R_FunTab_print,
    R_FunTab_profile,
    R_FunTab_qsort,
    R_FunTab_radixsort,
    R_FunTab_random,
    R_FunTab_raw,
    R_FunTab_saveload,
    R_FunTab_scan,
    R_FunTab_sort,
    R_FunTab_source,
    R_FunTab_split,
    R_FunTab_sprintf,
    R_FunTab_subset,
    R_FunTab_sysutils,
    R_FunTab_unique,
    R_FunTab_util,
    R_FunTab_version,

    R_FunTab_lapack,
    NULL
};


/* Tables of fast built-in functions.  These tables are found in various
   source files, with names having the form R_FastFunTab_srcfilename. */

extern FASTFUNTAB 
    R_FastFunTab_complex[],
    R_FastFunTab_eval[],
    R_FastFunTab_array[],
    R_FastFunTab_coerce[],
    R_FastFunTab_attrib[];

static FASTFUNTAB *FastFunTab_ptrs[] = { 
    R_FastFunTab_complex, 
    R_FastFunTab_eval,
    R_FastFunTab_array,
    R_FastFunTab_coerce,
    R_FastFunTab_attrib,
    NULL
};

/* also used in eval.c */
SEXP attribute_hidden R_Primitive(const char *primname)
{
    for (int i = 0; R_FunTab[i].name; i++) 
	if (strcmp(primname, R_FunTab[i].name) == 0) { /* all names are ASCII */
	    if ((R_FunTab[i].eval % 100 )/10)
		return R_NilValue; /* it is a .Internal */
	    else
		return mkPRIMSXP(i, R_FunTab[i].eval % 10);
	}
    return R_NilValue;
}

static SEXP do_primitive(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP name, prim;
    checkArity(op, args);
    name = CAR(args);
    if (!isString(name) || length(name) != 1 ||
	STRING_ELT(name, 0) == R_NilValue)
	errorcall(call, _("string argument required"));
    prim = R_Primitive(CHAR(STRING_ELT(name, 0)));
    if (prim == R_NilValue)
	errorcall(call, _("no such primitive function"));
    return prim;
}

int StrToInternal(const char *s)
{
    int i;
    for (i = 0; R_FunTab[i].name; i++)
	if (strcmp(s, R_FunTab[i].name) == 0) return i;
    return NA_INTEGER;
}

static void SymbolShortcuts(void)
{
    /* ../include/Rinternals.h : */

    R_BraceSymbol = install("{");
    R_BracketSymbol = install("[");
    R_Bracket2Symbol = install("[[");
    R_SubAssignSymbol = install("[<-");
    R_SubSubAssignSymbol = install("[[<-");
    R_DollarAssignSymbol = install("$<-");
    R_DollarSymbol = install("$");
    R_DotsSymbol = install("...");
    R_LocalAssignSymbol = R_AssignSymbols[1] = install("<-");
    R_GlobalAssignSymbol = R_AssignSymbols[2] = install("<<-");
    R_EqAssignSymbol = R_AssignSymbols[3] = install("=");
    R_LocalRightAssignSymbol = install("->");
    R_GlobalRightAssignSymbol = install("->>");

    R_ClassSymbol = install("class");
    R_DeviceSymbol = install(".Device");
    R_DimNamesSymbol = install("dimnames");
    R_DimSymbol = install("dim");
    R_DropSymbol = install("drop");
    R_LastvalueSymbol = install(".Last.value");
    R_LevelsSymbol = install("levels");
    R_ModeSymbol = install("mode");
    R_NameSymbol  = install("name");
    R_NamesSymbol = install("names");
    R_NaRmSymbol = install("na.rm");
    R_xSymbol = install("x");
    R_PackageSymbol = install("package");
    R_QuoteSymbol = install("quote");
    R_RowNamesSymbol = install("row.names");
    R_SeedsSymbol = install(".Random.seed");
    R_SourceSymbol = install("source");   /* for back compatibility, unused */
    R_SrcrefSymbol = install("srcref");
    R_TspSymbol = install("tsp");
    R_LogarithmSymbol = install("logarithm");
    R_LogSymbol = install("log");
    R_ValueSymbol = install("value");
    R_GradientSymbol = install("gradient");
    R_HessianSymbol = install("hessian");

    R_dot_defined = install(".defined");
    R_dot_Method = install(".Method");
    R_dot_target = install(".target");

    R_NaokSymbol = install("NAOK");
    R_DupSymbol = install("DUP");
    R_PkgSymbol = install("PACKAGE");
    R_EncSymbol = install("ENCODING");
    R_HelperSymbol = install("HELPER");
    R_CSingSymbol = install("Csingle");

    R_NativeSymbolSymbol = install("native symbol");
    R_RegisteredNativeSymbolSymbol = install("registered native symbol");

    /* ../include/Defn.h , i.e. non-public : */

    R_FunctionSymbol = install("function");
    R_WhileSymbol = install("while");
    R_RepeatSymbol = install("repeat");
    R_ForSymbol = install("for");
    R_InSymbol = install("in");
    R_AlongSymbol = install("along");
    R_AcrossSymbol = install("across");
    R_DownSymbol = install("down");
    R_IfSymbol = install("if");
    R_NextSymbol = install("next");
    R_BreakSymbol = install("break");
    R_WithGradientSymbol = install("with gradient");
    R_TrackGradientSymbol = install("track gradient");
    R_BackGradientSymbol = install("back gradient");
    R_ComputeGradientSymbol = install("compute gradient");
    R_AsSymbol = install("as");
    R_ColonSymbol = install(":");
    R_DoubleColonSymbol = install("::");
    R_TripleColonSymbol = install(":::");
    R_AddSymbol = install("+");
    R_SubSymbol = install("-");
    R_MulSymbol = install("*");
    R_DivSymbol = install("/");
    R_ExptSymbol = install("^");
    R_Expt2Symbol = install("**");
    R_BangBangSymbol = install("!!");
    R_EqSymbol = install("==");
    R_NeSymbol = install("!=");
    R_LtSymbol = install("<");
    R_LeSymbol = install("<=");
    R_GeSymbol = install(">=");
    R_GtSymbol = install(">");
    R_AndSymbol = install("&");
    R_And2Symbol = install("&&");
    R_OrSymbol = install("|");
    R_Or2Symbol = install("||");
    R_NotSymbol = install("!");
    R_TildeSymbol = install("~");
    R_QuerySymbol = install("?");
    R_ColonAssignSymbol = install(":=");
    R_AtSymbol = install("@");
    R_DotDotSymbol = install("..");
    R_ParenSymbol = install("(");

    R_CommentSymbol = install("comment");
    R_DotEnvSymbol = install(".Environment");
    R_ExactSymbol = install("exact");
    R_RecursiveSymbol = install("recursive");
    R_SrcfileSymbol = install("srcfile");
    R_WholeSrcrefSymbol = install("wholeSrcref");
    R_TmpvalSymbol = install("*tmp*");
    R_UseNamesSymbol = install("use.names");
    R_ConnIdSymbol = install("conn_id");
    R_DevicesSymbol = install(".Devices");
    R_dot_Generic = install(".Generic");
    R_dot_Methods = install(".Methods");
    R_dot_Group = install(".Group");
    R_dot_Class = install(".Class");
    R_dot_GenericCallEnv = install(".GenericCallEnv");
    R_dot_GenericDefEnv = install(".GenericDefEnv");
    R_sort_list = install("sort.list");
    R_S3MethodsTable = install(".__S3MethodsTable__.");
    R_previousSymbol = install("previous");
}

/* Set up symbols for subassign functions that will be allocated
   immediately after their subset counterpart, allowing mapping from,
   for example, "names" to "names<-" to be done very quickly.  Mark
   the subset version in its sxpinfo to indicate this.

   This scheme relies on this function being called when the symbol
   table is first created, so the symbols will be allocated
   sequentially, within one segment, rather than some possibly having
   been already created (hence out of sequence). 

   The table should have at most eight symbols, in order to ensure that
   all symbols go into one segment (64 chunks, up to 4 chunks per symbol).

   It is preferrable for the subassign functions here to not be "internal"
   to avoid possible expansion in the size of the internal table. 

   Also sets the MAYBE_FAST_SUBASSIGN flag for [<-, [[<-, and $<-; keep in
   sync with flags for the primitives. */

#define SUBASSIGN_TBL_SIZE 8   /* Maximum is 8 */
   
static char *subset_subassign_table[SUBASSIGN_TBL_SIZE] = {
  "[",  "[[",  "$",    /* first three marked as maybe using fast interface */
  "@",  "attr",  "class",  "dim",  "names"
};

static void SetupSubsetSubassign(void)
{
    SEXP subset_sym, subassign_sym;
    char sa[30];
    int i;

    for (i = 0; i < SUBASSIGN_TBL_SIZE; i++) {
        char *n = subset_subassign_table[i];
        subset_sym = install(n);
        copy_2_strings (sa, sizeof sa, n, "<-");
        subassign_sym = install(sa);
        if (i < 3) 
            SET_MAYBE_FAST_SUBASSIGN(subassign_sym);
        if (0)  /* can enable for debugging */
            REprintf ("Subset_subassign: %x %x\n",
                      CPTR_FROM_SEXP(subset_sym),
                      CPTR_FROM_SEXP(subassign_sym));
        SYMSEXP sub = (SYMSEXP)UPTR_FROM_SEXP(subset_sym);
        SYMSEXP suba = (SYMSEXP)UPTR_FROM_SEXP(subassign_sym);
        if (suba != sub+1) abort();
        SET_SUBASSIGN_FOLLOWS(subset_sym);
    }
}

/* Set up built-in/special/internal functions from R_FunTab and the
   FastFunTab_srcfile tables.  The R_FunTab tables is created from
   smaller tables in the various source files. 
 
   Also sets bit 0 of "symbits" for symbols unlikely to be redefined
   outside "base", according to the table below.  Any symbols can be
   put there, but ones that contain special characters, or are
   reserved words, are the ones unlikely to be defined in any
   environment other than base, and hence the ones where this is most
   likely to help. */

static char *Spec_name[] = { 
  "if", "while", "repeat", "for", "break", "next", "return", "function",
  "(", "{",
  "+", "-", "*", "/", "^", "%%", "%/%", "%*%", ":", "..",
  "==", "!=", "<", ">", "<=", ">=", "%in%",
  "&", "|", "&&", "||", "!", "!!",
  "<-", "<<-", "=", "->", "->>",
  "$", "[", "[[", "@",
  "$<-", "[<-", "[[<-", "@<-",
  ".C", ".Fortran", ".Call", ".External", ".Internal",
  "with gradient", "track gradient", "compute gradient",
  0
};

static void SetupBuiltins(void)
{
    int i, j, k;

    /* Combine tables in various source files into one table here. */

    i = 0;

    for (j = 0; FunTab_ptrs[j]!=NULL; j++) {
        for (k = 0; FunTab_ptrs[j][k].name!=NULL; k++) {
            if (i > R_MAX_FUNTAB_ENTRIES) {
                REprintf(
                "Too many builtin functions - increase R_MAX_FUNTAB_ENTRIES\n");
                exit(1);
            }
            R_FunTab[i++] = FunTab_ptrs[j][k];
        }
    }

    R_FunTab[i].name = NULL;

    /* Install the symbols for internals first, so they will have small
       compressed pointers, allowing for a small table of internals. 
       Some may have already been installed, but that should be OK. */

    R_first_internal = 0;
    R_max_internal = 0;

    for (i = 0; R_FunTab[i].name!=NULL; i++) {
        if ((R_FunTab[i].eval % 100) / 10) {
            SEXP sym = install(R_FunTab[i].name);
            if (R_first_internal == 0 || CPTR_FROM_SEXP(sym) < R_first_internal)
                R_first_internal = CPTR_FROM_SEXP(sym);
            if (CPTR_FROM_SEXP(sym) > R_max_internal)
                R_max_internal = CPTR_FROM_SEXP(sym);
        }
    }

    if (0) /* can enable for debugging */
        REprintf("first_internal: %d, max_internal: %d\n",
                 (int) R_first_internal, (int) R_max_internal);

    R_internal_table = calloc (sizeof(SEXP), R_max_internal-R_first_internal+1);
    if (R_internal_table == NULL) 
        R_Suicide("Can't allocate R_internal_table");

    /* Install the primitive and internal functions.  Look for fast versions
       of the primitives (but not internals).  Set bits 1-13 of symbits for
       symbols for primitives, based on their index in FunTab (not name). */

    for (i = 0; R_FunTab[i].name!=NULL; i++) {

        SEXP (*this_cfun)() = R_FunTab[i].cfun;
        int this_code = R_FunTab[i].code;
        SEXP prim;
        /* prim needs protect since install can (and does here) allocate.
           Except... they're now uncollected... */
        PROTECT(prim = mkPRIMSXP(i, R_FunTab[i].eval % 10));

        if (0) { /* enable to display info */
            Rprintf (
            "SETUP: %d %s, builtin %d, int %d, vis %d, pend_ok %d, gradn %d\n",
                    i, PRIMNAME(prim), TYPEOF(prim) == BUILTINSXP,
                    PRIMINTERNAL(prim), PRIMPRINT(prim), PRIMPENDINGOK(prim),
                    PRIMGRADN(prim));
        }

        if (TYPEOF(prim) == SPECIALSXP && !PRIMINTERNAL(prim)
              && !PRIMVARIANT(prim)) {
            REprintf("SPECIAL primitive must take a 'variant' argument: %s\n",
                      PRIMNAME(prim));
            abort();
        }

        SEXP sym = install(R_FunTab[i].name);

        if ((R_FunTab[i].eval % 100) / 10) {
            SET_INTERNAL(sym, prim);
        }
        else {

            SET_SYMVALUE(sym, prim);

            for (j = 0; FastFunTab_ptrs[j]!=NULL; j++) {
                for (k = 0; FastFunTab_ptrs[j][k].slow!=0; k++) {
                    FASTFUNTAB *f = &FastFunTab_ptrs[j][k];
                    if (f->slow==this_cfun 
                          && (f->code==-1 || f->code==this_code)) {
                        SET_PRIMFUN_FAST_UNARY (prim, f->fast, f->dsptch1,
                                                f->var1);
                        goto found;
                    }
                }
            }

          found: ;

            R_symbits_t w;
            int b1, b2;
            b1 = 1 + i % 13;
            b2 = 1 + i % 12;
            if (b2 >= b1) b2 += 1;
            w = (((R_symbits_t)1) << b1) | (((R_symbits_t)1) << b2);
            SET_SYMBITS (sym, SYMBITS(sym) | w);
        }

        UNPROTECT(1);
    }

    /* Set bit 0 in symbits for "special" symbols, to make extra sure we
       will be likely to skip straight to base environment on lookup. */

    for (i = 0; Spec_name[i]; i++) {
        SEXP sym = install(Spec_name[i]);
        SET_SYMBITS (sym, SYMBITS(sym) | 1);
    }
}

extern SEXP framenames; /* from model.c */

void lphash_setup_bucket (lphash_bucket_t *bucket, lphash_key_t key)
{
    bucket->entry =  (lphash_entry_t) /* SEXP32 might be pointer or unsigned */
                      SEXP32_FROM_SEXP (mkSYMSXP (mkChar(key), R_UnboundValue));
}

int lphash_match (lphash_bucket_t *bucket, lphash_key_t key)
{
    SEXP sym = SEXP_FROM_SEXP32 ((SEXP32) bucket->entry);

    return strcmp (key, CHAR(PRINTNAME(sym))) == 0;
}

/* initialize the symbol table */
void InitNames()
{
    /* Create the symbol table. */

    R_lphashSymTbl = lphash_create (HSIZE);

    if (R_lphashSymTbl == NULL)
	R_Suicide("couldn't allocate memory for symbol table");

    /* Set up some functions that have subassign counterparts, so the
       subassign versions can be quickly found.  Do immediately so
       they will go in consecutive positions within one or two segments. */

    SetupSubsetSubassign();

    /* Set up built-in functions.  Do early so internals have a small
       range of compressed pointers. */

    SetupBuiltins();

    /* R_BlankString */
    R_BlankString = mkChar("");
    R_BlankScalarString = ScalarString(R_BlankString);
    SET_NAMEDCNT_MAX(R_BlankScalarString);

    /* The SYMSXP objects below are not in the symbol table. */

    /* R_MissingArg */
    PRINTNAME(R_MissingArg) = R_BlankString;
    IS_PRINTNAME(R_BlankString) = 1;
    SET_SYMVALUE(R_MissingArg, R_MissingArg);
#   if !USE_AUX_FOR_ATTRIB && !USE_COMPRESSED_POINTERS
        LENGTH(R_MissingArg) = 1;
#   endif

    /* R_MissingUnder */
    R_UnderscoreString = mkChar("_");
    PRINTNAME(R_MissingUnder) = R_UnderscoreString;
    IS_PRINTNAME(R_UnderscoreString) = 1;
    SET_SYMVALUE(R_MissingUnder, R_MissingArg);
#   if !USE_AUX_FOR_ATTRIB && !USE_COMPRESSED_POINTERS
        LENGTH(R_MissingArg) = 1;
#   endif

    /* R_RestartToken */
    R_RestartToken = mkSYMSXP(R_BlankString,R_NilValue);
    SET_SYMVALUE(R_RestartToken, R_RestartToken);

    /* String constants (CHARSXP values) */
    /* Note: we don't want NA_STRING to be in the CHARSXP cache, so that
       mkChar("NA") is distinct from NA_STRING */
    /* NA_STRING */
    NA_STRING = allocCharsxp(strlen("NA"));
    strcpy(CHAR_RW(NA_STRING), "NA");
    R_print.na_string = NA_STRING;

    /* Some CHARSXP constants, protected by making them symbol printnames
       (most are used as printnames anyways, so little waste with this). */

    (void)installChar (R_factor_CHARSXP = mkChar("factor"));
    (void)installChar (R_ordered_CHARSXP = mkChar("ordered"));
    (void)installChar (R_matrix_CHARSXP = mkChar("matrix"));
    (void)installChar (R_array_CHARSXP = mkChar("array"));
    (void)installChar (R_integer_CHARSXP = mkChar("integer"));
    (void)installChar (R_double_CHARSXP = mkChar("double"));
    (void)installChar (R_numeric_CHARSXP = mkChar("numeric"));
    (void)installChar (R_name_CHARSXP = mkChar("name"));
    (void)installChar (R_function_CHARSXP = mkChar("function"));
    (void)installChar (R_any_CHARSXP = mkChar("any"));
    (void)installChar (R_modulus_CHARSXP = mkChar("modulus"));
    (void)installChar (R_sign_CHARSXP = mkChar("sign"));
    (void)installChar (R_det_CHARSXP = mkChar("det"));
    (void)installChar (R_NativeSymbolInfo_CHARSXP = mkChar("NativeSymbolInfo"));
    (void)installChar (R_TRUE_CHARSXP = mkChar("TRUE"));
    (void)installChar (R_FALSE_CHARSXP = mkChar("FALSE"));

    /* Set up a set of globals so that a symbol table search can be
       avoided when matching something like dim or dimnames. */

    SymbolShortcuts();

    framenames = R_NilValue;

    R_initialize_bcode();
}


/*  Create a symbol in the symbol table, if not already present.
    If "name" is not found, it is installed in the symbol table.
    For install, the symbol corresponding to the C string "name" is returned.
    For instalChar, the name is given as an R CHARSXP.  */

static SEXP install_with_hashcode (char *name, int hashcode)
{
    lphash_bucket_t *bucket;

    bucket = lphash_key_lookup (R_lphashSymTbl, hashcode, name);
    if (bucket != NULL)
        return SEXP_FROM_SEXP32 ((SEXP32) bucket->entry);

    if (strlen(name) > MAXIDSIZE)
	error(_("variable names are limited to %d bytes"), MAXIDSIZE);

    bucket = lphash_insert (R_lphashSymTbl, hashcode, name);

    if (bucket == NULL)
        R_Suicide("couldn't allocate memory to expand symbol table");

    SEXP sym = SEXP_FROM_SEXP32 ((SEXP32) bucket->entry);

    /* Set up symbits.  May be fiddled to try to improve performance. 
       Currently, the low 14 bits of symbits are reserved for special and
       primitive symbols. */

    int b1, b2;
    b1 = 14 + hashcode % 50;
    b2 = 14 + hashcode % 49;
    if (b2 >= b1) b2 += 1;

    SET_SYMBITS (sym, (((R_symbits_t)1) << b1) | (((R_symbits_t)1) << b2));

    return sym;
}

SEXP install(const char *name)
{
    if (*name == '\0')
	error(_("attempt to use zero-length variable name"));

    return install_with_hashcode ((char *) name, Rf_char_hash(name));
}

SEXP attribute_hidden install_translated (SEXP charSXP)
{
    const void *vmax = VMAXGET();
    SEXP r = install (translateChar (charSXP));
    VMAXSET(vmax);
    return r;
}

SEXP installChar(SEXP charSXP)
{
    PROTECT(charSXP);
    SEXP res = install_with_hashcode((char*) CHAR(charSXP), CHAR_HASH(charSXP));
    UNPROTECT(1);

    return res;
}

/* Lookup up a symbol, returning it if it exists already, but not creating
   it if it doesn't already exist, returning R_NoObject instead. */

SEXP installed_already(const char *name)
{
    lphash_bucket_t *bucket;

    bucket = lphash_key_lookup (R_lphashSymTbl, Rf_char_hash(name), 
                                                (char *) name);

    return bucket == NULL ? R_NoObject 
                          : SEXP_FROM_SEXP32 ((SEXP32) bucket->entry);
}

/* Lookup up a symbol, returning it if it exists already, but not creating
   it if it doesn't already exist, returning R_NoObject instead.  This
   version takes the hashcode of the name as an argument (useful if the
   caller can compute it faster than the direct way). */

SEXP installed_already_with_hash (const char *name, int hashcode)
{
    lphash_bucket_t *bucket;

    bucket = lphash_key_lookup (R_lphashSymTbl, hashcode, (char *) name);

    return bucket == NULL ? R_NoObject 
                          : SEXP_FROM_SEXP32 ((SEXP32) bucket->entry);
}


/*  do_internal - This is the code for .Internal(). */

static SEXP do_internal (SEXP call, SEXP op, SEXP args, SEXP env, int variant)
{
    SEXP s, fun, ifun, ans;

    checkArity(op, args);
    s = CAR(args);
    if (!isPairList(s) || !isSymbol (fun = CAR(s)))
	errorcall(call, _("invalid .Internal() argument"));
    ifun = INTERNAL(fun);
    if (ifun == R_NilValue)
	errorcall(call, _("there is no internal function '%s'"),
		  CHAR(PRINTNAME(fun)));

    args = CDR(s);
    if (TYPEOF(ifun) == BUILTINSXP) {
        int vrnt = PRIMFUN_PENDING_OK(ifun) ? VARIANT_PENDING_OK : 0;
        int gradn = PRIMGRADN(ifun);
        args = gradn && (variant & VARIANT_GRADIENT)
                 ? evalList_gradient (args, env, vrnt, gradn&7, gradn>>3)
                 : evalList_v (args, env, vrnt);
    }
    PROTECT(args);

    R_variant_result = 0;
    R_Visible = TRUE;

    ans = CALL_PRIMFUN(s, ifun, args, env, variant);

    if (PRIMVISON(ifun))
        R_Visible = TRUE;
    else if (PRIMVISOFF(ifun))
        R_Visible = FALSE;

    /* If this .Internal function is the whole body of a function, we
       try to undo the incrementing of NAMEDCNT that was done for the
       arguments of the function.  This should be OK, because with no
       other statements in the function body, the function's environment
       won't have been captured for other uses. */

    if (PRIMWHOLE(ifun)) {  /* should only be used for BUILTINs */
        if ((variant & VARIANT_WHOLE_BODY) != 0) {
            args = R_GlobalContext->promargs;
            while (args != R_NilValue) {
                SEXP a = CAR(args);
                if (TYPEOF(a) == PROMSXP &&
                    /* NAMEDCNT_EQ_0(a) && */ /* avoid if recycled promise? */
                       PRVALUE_PENDING_OK(a) != R_UnboundValue) {
                    DEC_NAMEDCNT(PRVALUE_PENDING_OK(a));
                    /* SET_PRVALUE(a,R_NilValue); */
                       /* possible precaution - only if avoid when recycled */
                }
                args = CDR(args);
            }
        }
    }

    UNPROTECT(1);
    return ans;
}


/* FUNTAB entries defined in this source file; otherwise in non-standard way. */

attribute_hidden FUNTAB R_FunTab_names[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{".Internal",	do_internal,	0,	1200,	1,	{PP_FUNCALL, PREC_FN,	  0}},
{".Primitive",	do_primitive,	0,	1,	1,	{PP_FUNCALL, PREC_FN,	  0}},


/* Note that the number of arguments in this group only applies
   to the default method */

{"machine",	do_machine,	0,	11,	0,	{PP_FUNCALL, PREC_FN,	0}},

#ifdef Win32
{"system",	do_system,	0,	211,	5,	{PP_FUNCALL, PREC_FN,	0}},
#else
{"system",	do_system,	0,	211,	2,	{PP_FUNCALL, PREC_FN,	0}},
#endif

#ifdef Win32
{"flush.console",do_flushconsole,0,	11,	0,	{PP_FUNCALL, PREC_FN,	0}},
{"win.version", do_winver,	0,	11,	0,	{PP_FUNCALL, PREC_FN,	0}},
{"shell.exec",	do_shellexec,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"winDialog",	do_windialog,	0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"winDialogString", do_windialogstring, 0, 11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"winMenuNames", do_winmenunames, 0,    11,     0,      {PP_FUNCALL, PREC_FN,   0}},
{"winMenuItems", do_wingetmenuitems, 0, 11, 1, {PP_FUNCALL, PREC_FN, 0}},
{"winMenuAdd",	do_winmenuadd,	0,	11,	3,	{PP_FUNCALL, PREC_FN,	0}},
{"winMenuDel",	do_winmenudel,	0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"memory.size",	do_memsize,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"DLL.version",	do_dllversion,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"bringToTop",	do_bringtotop,	0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"msgWindow",	do_msgwindow,	0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"select.list",	do_selectlist,	0,	11,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"getClipboardFormats",do_getClipboardFormats,0,11,0,	{PP_FUNCALL, PREC_FN,	0}},
{"readClipboard",do_readClipboard,0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"writeClipboard",do_writeClipboard,0,	111,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"chooseFiles", do_chooseFiles, 0,	11,	5,	{PP_FUNCALL, PREC_FN,   0}},
{"chooseDir",	do_chooseDir,	0,	11,	2,	{PP_FUNCALL, PREC_FN,   0}},
{"getIdentification", do_getIdentification,0,11,0,	{PP_FUNCALL, PREC_FN,	0}},
{"getWindowHandle", do_getWindowHandle,0,11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"getWindowHandles",do_getWindowHandles,0,11,   2,	{PP_FUNCALL, PREC_FN,   0}},
{"getWindowTitle",do_getWindowTitle,0,	11,	0,	{PP_FUNCALL, PREC_FN,	0}},
{"setWindowTitle",do_setTitle,	0,	111,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"arrangeWindows",do_arrangeWindows,0,  111,    4,      {PP_FUNCALL, PREC_FN,   0}},
{"setStatusBar",do_setStatusBar,0,	111,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"shortPathName",do_shortpath,	0,	11,	1,	{PP_FUNCALL, PREC_FN,   0}},
{"loadRconsole", do_loadRconsole,0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"Sys.which",	do_syswhich,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"readRegistry",do_readRegistry,0,	11,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"winProgressBar",do_winprogressbar,0,	11,	6,	{PP_FUNCALL, PREC_FN,	0}},
{"closeWinProgressBar",do_closewinprogressbar,0,111,1,	{PP_FUNCALL, PREC_FN,	0}},
{"setWinProgressBar",do_setwinprogressbar,0,11,	4,	{PP_FUNCALL, PREC_FN,	0}},
#endif

#if defined(__APPLE__) && defined(HAVE_AQUA)
{"wsbrowser",	do_wsbrowser,	0,	11,	8,	{PP_FUNCALL, PREC_FN,	0}},
{"pkgbrowser",	do_browsepkgs,	0,	11,	5,	{PP_FUNCALL, PREC_FN,	0}},
{"data.manager",	do_datamanger,	0,	11,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"package.manager",	do_packagemanger,	0,	11,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"flush.console",do_flushconsole,0,	11,	0,	{PP_FUNCALL, PREC_FN,	0}},
{"hsbrowser",	do_hsbrowser,	0,	11,	5,	{PP_FUNCALL, PREC_FN,	0}},
{"select.list",	do_selectlist,	0,	11,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"aqua.custom.print", do_aqua_custom_print, 0, 11, 2,   {PP_FUNCALL, PREC_FN,   0}},
#endif

{"edit",	do_edit,	0,	11,	4,	{PP_FUNCALL, PREC_FN,	0}},
{"dataentry",	do_dataentry,	0,	11,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"dataviewer",	do_dataviewer,	0,	111,	2,	{PP_FUNCALL, PREC_FN,	0}},

{"Sys.info",	do_sysinfo,	0,	11,	0,	{PP_FUNCALL, PREC_FN,	0}},
{"Sys.sleep",	do_syssleep,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},

#ifdef Unix
{"X11",		do_X11,		0,	111,	17,	{PP_FUNCALL, PREC_FN,	0}},
{"savePlot",	do_saveplot,	0,	111,	3,	{PP_FUNCALL, PREC_FN,	0}},
#endif

{"getGraphicsEvent",do_getGraphicsEvent,0,  11, 1,      {PP_FUNCALL, PREC_FN,   0}},
{"getGraphicsEventEnv",do_getGraphicsEventEnv,0,11,1,{PP_FUNCALL, PREC_FN, 0}},
{"setGraphicsEventEnv",do_setGraphicsEventEnv,0,11,2,{PP_FUNCALL, PREC_FN, 0}},

{"loadhistory", do_loadhistory,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"savehistory", do_savehistory,	0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},
{"addhistory",  do_addhistory,  0,	11,	1,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};

#undef __R_Names__
