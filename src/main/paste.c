/*
 *  pqR : A pretty quick version of R
 *  Copyright (C) 2013, 2014, 2015, 2017, 2018 by Radford M. Neal
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
 *  http://www.r-project.org/Licenses/
 *
 *
 *  See ./printutils.c	 for general remarks on Printing
 *                       and the Encode.. utils.
 *
 *  See ./format.c	 for the  format_Foo_  functions.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define USE_FAST_PROTECT_MACROS
#include "Defn.h"
#define imax2(x, y) ((x < y) ? y : x)

#include "Print.h"
#include "RBufferUtils.h"
static R_StringBuffer cbuff = {NULL, 0, MAXELTSIZE};


/* .Internal(paste (sep, collapse, ...))

    Note that NA_STRING is implicitly coerced to "NA". */

#define N_AUTO 15  /* Size limit of arrays as auto vars rather than R_alloc */

static SEXP pasteop (SEXP call, SEXP op, SEXP sep, SEXP collapse, 
                     SEXP xpl, SEXP env);

static SEXP do_paste (SEXP call, SEXP op, SEXP args, SEXP env)
{ 
    return pasteop (call, op, CAR(args), CADR(args), CDDR(args), env);
}

static SEXP pasteop (SEXP call, SEXP op, SEXP sep, SEXP collapse, 
                     SEXP xpl, SEXP env)
{
    int i, j, k, maxlen, sepw, u_sepw, ienc;
    const char *s, *csep, *u_csep;
    SEXP ans;

    const void *vmax0 = VMAXGET();

    /* Handle sep argument */

    if (!isString(sep) || LENGTH(sep) <= 0 || STRING_ELT(sep,0)==NA_STRING)
        error(_("invalid separator"));
    sep = STRING_ELT(sep, 0);
    if (LENGTH(sep) == 0) {
        csep = "";
        sepw = 0;
    }
    else {
        csep = translateChar(sep);
        sepw = strlen(csep);
    }
    u_csep = NULL;

    int sepKnown = ENC_KNOWN(sep) > 0;
    int sepUTF8 = IS_UTF8(sep);
    int sepBytes = IS_BYTES(sep);

    /* Handle collapse argument */

    if (!isNull(collapse))
        if(!isString(collapse) || LENGTH(collapse) <= 0 ||
           STRING_ELT(collapse, 0) == NA_STRING)
            error(_("invalid '%s' argument"), "collapse");

    /* Look at remaining arguments. */

    int nx = length(xpl);

    if (nx == 0)
        return !isNull(collapse) ? mkString("") : allocVector(STRSXP, 0);

    /* Find maximum argument length, coerce if needed.  Also store string
       vectors in xa. */

    SEXP xaa[N_AUTO];
    SEXP *xa =           /* Array of string vectors to paste */
      nx <= N_AUTO ? xaa : (SEXP *) R_alloc (nx, sizeof(SEXP));

    maxlen = 0;
    for (j = 0; j < nx; j++) {

        SEXP xj;
        xj = CAR(xpl);
        xpl = CDR(xpl);

        if (!isString(xj)) {
            if (OBJECT(xj)) { /* method dispatch */
                SEXP call;
                PROTECT(call = lang2(install("as.character"), xj));
                xj = eval(call, env);
                UNPROTECT(1);
            } 
            else if (isSymbol(xj))
                xj = ScalarString(PRINTNAME(xj));
            else if (TYPEOF(xj) == INTSXP)
                ; /* will be handled directly below */
            else if (TYPEOF(xj) != STRSXP)
                xj = coerceVector(xj, STRSXP);
            if (TYPEOF(xj) != STRSXP && TYPEOF(xj) != INTSXP)
                error(_("argument to .Internal paste not string or integer"));
        }

        if (LENGTH(xj) > maxlen)
            maxlen = LENGTH(xj);

        PROTECT(xa[j] = xj);
    }

    if (maxlen == 0) {
        UNPROTECT(nx);
        VMAXSET(vmax0);
        return (!isNull(collapse)) ? mkString("") : allocVector(STRSXP, 0);
    }

    if (nx == 1) {

        /* If only one argument, just turn NA into "NA" for strings, or
           convert from integer to string.  Allocate new STRSXP so it
           won't have any attributes. */

        if (TYPEOF(xa[0]) == INTSXP)
            PROTECT (ans = coerceVector (xa[0], STRSXP));
        else {
            PROTECT(ans = allocVector(STRSXP, maxlen));
            SEXP NA = mkChar(CHAR(NA_STRING)); /* will not be the same thing! */
            for (i = 0; i < maxlen; i++)
                SET_STRING_ELT (ans, i, STRING_ELT(xa[0],i) == NA_STRING ? NA
                                         : STRING_ELT (xa[0], i));
         }
    }
    else {

        /* Concatenate, if more than one argument. */

        const void *vmax1 = VMAXGET();

        char *chra[2*N_AUTO];
        char **chr = /* Space to store pointers to the strings + NULL */
          nx <= N_AUTO ? chra : (char **) R_alloc (2*nx, sizeof(char*));

        int lena[2*N_AUTO];
        int *len =         /* Space to store lengths of strings */
          nx <= N_AUTO ? lena : (int *) R_alloc (2*nx, sizeof(int));

        /* Allocate space to put character conversions of integer arguments. */

        char ichr[12]; int used_ichr = 0;
        for (j = 0; j < nx; j++)
            if (TYPEOF(xa[j]) == INTSXP)
                chr [sepw == 0 ? j : 2*j] = 
                  !used_ichr ? (used_ichr = 1, ichr) : (char *) R_alloc(12,1);

        /* Create the string vector result. */

        PROTECT (ans = allocVector(STRSXP, maxlen));

        for (i = 0; i < maxlen; i++) {

            const void *vmax2 = VMAXGET();

            /* Strategy for marking the encoding:  If all inputs
               (including the separator) are ASCII, so is the output
               and we don't need to mark.  (Note that integers are
               converted to ASCII.)  Otherwise if all non-ASCII inputs
               are of declared encoding, we should mark.  Need to be
               careful only to include separator if it is used. */
    
            Rboolean use_Bytes, use_UTF8, any_known, declare_encoding;
    
            if (nx > 1) { 
                use_Bytes = sepBytes; 
                use_UTF8 = sepUTF8; 
                any_known = sepKnown;
            }
            else
                use_Bytes = use_UTF8 = any_known = FALSE;
    
            for (j = 0; j < nx && !use_Bytes; j++) {
                SEXP xj = xa[j];
                if (TYPEOF(xj) == INTSXP)
                    continue;
                k = LENGTH(xj);
                if (k > 0) {
                    SEXP cs = STRING_ELT (xj, k==1 ? 0 : k==maxlen ? i : i % k);
                    if (IS_BYTES(cs))
                        use_Bytes = TRUE;
                    else if (IS_UTF8(cs))
                        use_UTF8 = TRUE;
                    else if (ENC_KNOWN(cs) > 0)
                        any_known = TRUE;
                }
            }
    
            if (use_Bytes) 
                use_UTF8 = FALSE;
    
            if (use_UTF8 && u_csep == NULL) { /* do at most once to save time */
                u_csep = translateCharUTF8(sep);
                u_sepw = strlen(u_csep);
            }

            declare_encoding = any_known 
                                && (known_to_be_latin1 || known_to_be_utf8);

            const char *this_csep = csep;
            int this_sepw = sepw;

            if (use_UTF8) {
                this_csep = u_csep;
                this_sepw = u_sepw;
            }

            unsigned first_hash = 0;

            if (sepw == 0) {
                for (j = 0; j < nx; j++) {
                    SEXP xj = xa[j];
                    k = LENGTH(xj);
                    if (k == 0) {
                        chr[j] = "";
                        len[j] = 0;
                    }
                    else if (TYPEOF(xj) == INTSXP) {
                        integer_to_string (chr[j], 
                          INTEGER(xj) [k==1 ? 0 : k==maxlen ? i : i % k]);
                        len[j] = strlen(chr[j]);
                    }
                    else {
                        SEXP cs;
                        cs = STRING_ELT (xj, k==1 ? 0 : k==maxlen ? i : i % k);
                        if (use_Bytes)
                            chr[j] = (char *) CHAR(cs);
                        else if (use_UTF8)
                            chr[j] = (char *) translateCharUTF8(cs);
                        else {
                            chr[j] = (char *) translateChar(cs);
                            if (declare_encoding && !ENC_KNOWN(cs) 
                                                 && !strIsASCII(chr[j]))
                                declare_encoding = FALSE;
                        }
                        len[j] = chr[j]==CHAR(cs) ? LENGTH(cs) : strlen(chr[j]);
                        if (j == 0 && chr[j] == CHAR(cs)) 
                            first_hash = CHAR_HASH(cs);
                    }
                }
                chr[nx] = NULL;
            }
            else {
                for (j = 0; j < nx; j++) {
                    SEXP xj = xa[j];
                    k = LENGTH(xj);
                    if (k == 0) {
                        chr[2*j] = "";
                        len[2*j] = 0;
                    }
                    else if (TYPEOF(xj) == INTSXP) {
                        integer_to_string (chr[2*j], 
                          INTEGER(xj) [k==1 ? 0 : k==maxlen ? i : i % k]);
                        len[2*j] = strlen(chr[2*j]);
                    }
                    else {
                        SEXP cs;
                        cs = STRING_ELT (xj, k==1 ? 0 : k==maxlen ? i : i % k);
                        if (use_Bytes)
                            chr[2*j] = (char *) CHAR(cs);
                        else if (use_UTF8)
                            chr[2*j] = (char *) translateCharUTF8(cs);
                        else {
                            chr[2*j] = (char *) translateChar(cs);
                            if (declare_encoding && !ENC_KNOWN(cs) 
                                                 && !strIsASCII(chr[2*j]))
                                declare_encoding = FALSE;
                        }
                        len[2*j] = chr[2*j]==CHAR(cs) ? LENGTH(cs) 
                                                      : strlen(chr[j]);
                        if (j == 0 && chr[2*j] == CHAR(cs)) 
                            first_hash = CHAR_HASH(cs);
                    }
                    chr[2*j+1] = (char *) this_csep;
                    len[2*j+1] = this_sepw;
                }
                chr[2*nx-1] = NULL;
            }

            ienc = CE_NATIVE;
            if (use_UTF8) 
                ienc = CE_UTF8;
            else if (use_Bytes)
                ienc = CE_BYTES;
            else if (declare_encoding) {
                if(known_to_be_latin1) ienc = CE_LATIN1;
                if(known_to_be_utf8) ienc = CE_UTF8;
            }
    
            SET_STRING_ELT(ans, i, Rf_mkCharMulti ((const char **) chr, 
                                                   len, first_hash, ienc));

            VMAXSET(vmax2);
        }

        VMAXSET(vmax1);
    }

    UNPROTECT(nx+1);  /* args + ans */
    PROTECT(ans);

    /* Now collapse, if required. */

    int na = LENGTH(ans);

    if (collapse != R_NilValue && na > 1) {

        Rboolean use_Bytes, use_UTF8, any_known, declare_encoding;

        const char *asa[2*N_AUTO];
        const char **as = na <= N_AUTO ? asa
                        : (const char **) R_alloc (2*na, sizeof (const char *));

        int lena[2*N_AUTO];
        int *len =         /* Space to store lengths of strings */
          na <= N_AUTO ? lena : (int *) R_alloc (2*na, sizeof(int));

        sep = STRING_ELT(collapse, 0);
        use_UTF8 = IS_UTF8(sep);
        use_Bytes = IS_BYTES(sep);
        any_known = ENC_KNOWN(sep) > 0;

        for (i = 0; i < na && !use_Bytes; i++) {
            SEXP cs = STRING_ELT(ans,i);
            if (IS_BYTES(cs))
                use_Bytes = TRUE;
            else if (IS_UTF8(cs))
                use_UTF8 = TRUE;
            else if (ENC_KNOWN(cs) > 0)
                any_known = TRUE;
        }

        if (use_Bytes) {
            csep = CHAR(sep);
            use_UTF8 = FALSE;
        } 
        else if (use_UTF8)
            csep = translateCharUTF8(sep);
        else
            csep = translateChar(sep);

        sepw = strlen(csep);

        declare_encoding = any_known && (known_to_be_latin1||known_to_be_utf8);

        if (*csep == 0) {
            for (i = 0; i < na; i++) {
                SEXP cs = STRING_ELT(ans,i);
                if (use_Bytes)
                    as[i] = CHAR(cs);
                else if (use_UTF8)
                    as[i] = translateCharUTF8(cs);
                else {
                    /* no translation needed - done already */
                    as[i] = CHAR(cs);
                    if (declare_encoding && !ENC_KNOWN(cs) 
                                         && !strIsASCII(s))
                        declare_encoding = FALSE;
                }
                len[i] = as[i] == CHAR(cs) ? LENGTH(cs) : strlen(as[i]);
            }
            as[na] = NULL;
           
        }
        else {
            for (i = 0; i < na; i++) {
                SEXP cs = STRING_ELT(ans,i);
                if (use_Bytes)
                    as[2*i] = CHAR(cs);
                else if (use_UTF8)
                    as[2*i] = translateCharUTF8(cs);
                else {
                    /* no translation needed - done already */
                    as[2*i] = CHAR(cs);
                    if (declare_encoding && !ENC_KNOWN(cs) 
                                         && !strIsASCII(s))
                        declare_encoding = FALSE;
                }
                len[2*i] = as[2*i] == CHAR(cs) ? LENGTH(cs) : strlen(as[2*i]);
                as[2*i+1] = csep;
                len[2*i+1] = sepw;
            }
            as[2*na-1] = NULL;
        }

        unsigned first_hash = as[0] != CHAR (STRING_ELT(ans,0)) ? 0
                               : CHAR_HASH (STRING_ELT(ans,0));

        PROTECT(ans = allocVector(STRSXP, 1));

        ienc = CE_NATIVE;
        if (use_UTF8) 
            ienc = CE_UTF8;
        else if (use_Bytes) 
            ienc = CE_BYTES;
        else if (declare_encoding) {
            if(known_to_be_latin1) ienc = CE_LATIN1;
            if(known_to_be_utf8) ienc = CE_UTF8;
        }

        SET_STRING_ELT (ans, 0, Rf_mkCharMulti (as, len, first_hash, ienc));

        UNPROTECT(1);  /* newer ans */
    }

    UNPROTECT(1);  /* ans (original) */
    VMAXSET(vmax0);
    return ans;
}

static SEXP do_filepath(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP ans, sep, x;
    int i, j, k, ln, maxlen, nx, nzero, pwidth, sepw;
    const char *s, *csep, *cbuf; char *buf;

    checkArity(op, args);

    /* Check the arguments */

    x = CAR(args);
    if (!isVectorList(x))
	error(_("invalid first argument"));
    nx = LENGTH(x);
    if(nx == 0) return allocVector(STRSXP, 0);


    sep = CADR(args);
    if (!isString(sep) || LENGTH(sep) <= 0 || STRING_ELT(sep, 0) == NA_STRING)
	error(_("invalid separator"));
    sep = STRING_ELT(sep, 0);
    csep = CHAR(sep);
    sepw = strlen(csep); /* hopefully 1 */

    /* Any zero-length argument gives zero-length result */
    maxlen = 0; nzero = 0;
    for (j = 0; j < nx; j++) {
	if (!isString(VECTOR_ELT(x, j))) {
	    /* formerly in R code: moved to C for speed */
	    SEXP call, xj = VECTOR_ELT(x, j);
	    if(OBJECT(xj)) { /* method dispatch */
		PROTECT(call = lang2(install("as.character"), xj));
		SET_VECTOR_ELT(x, j, eval(call, env));
		UNPROTECT(1);
	    } else if (isSymbol(xj))
		SET_VECTOR_ELT(x, j, ScalarString(PRINTNAME(xj)));
	    else
		SET_VECTOR_ELT(x, j, coerceVector(xj, STRSXP));

	    if (!isString(VECTOR_ELT(x, j)))
		error(_("non-string argument to Internal paste"));
	}
	ln = LENGTH(VECTOR_ELT(x, j));
	if(ln > maxlen) maxlen = ln;
	if(ln == 0) {nzero++; break;}
    }
    if(nzero || maxlen == 0) return allocVector(STRSXP, 0);

    PROTECT(ans = allocVector(STRSXP, maxlen));

    for (i = 0; i < maxlen; i++) {
	pwidth = 0;
	for (j = 0; j < nx; j++) {
	    k = LENGTH(VECTOR_ELT(x, j));
	    pwidth += strlen(translateChar(STRING_ELT(VECTOR_ELT(x, j), i % k)));
	}
	pwidth += (nx - 1) * sepw;
	cbuf = buf = ALLOC_STRING_BUFF(pwidth,&cbuff);
	for (j = 0; j < nx; j++) {
	    k = LENGTH(VECTOR_ELT(x, j));
	    if (k > 0) {
		s = translateChar(STRING_ELT(VECTOR_ELT(x, j), i % k));
		strcpy(buf, s);
		buf += strlen(s);
	    }
	    if (j != nx - 1 && sepw != 0) {
		strcpy(buf, csep);
		buf += sepw;
	    }
	}
	SET_STRING_ELT(ans, i, mkChar(cbuf));
    }
    R_FreeStringBufferL(&cbuff);
    UNPROTECT(1);
    return ans;
}

/* format.default(x, trim, digits, nsmall, width, justify, na.encode,
		  scientific) */
static SEXP do_format(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP l, x, y, swd;
    int i, il, n, digits, trim = 0, nsmall = 0, wd = 0, adj = -1, na, sci = 0;
    int w, d, e;
    int wi, di, ei, scikeep;
    const char *strp;

    checkArity(op, args);
    PrintDefaults();
    scikeep = R_print.scipen;

    if (isEnvironment(x = CAR(args))) {
	PROTECT(y = allocVector(STRSXP, 1));
	SET_STRING_ELT(y, 0, mkChar(EncodeEnvironment(x)));
	UNPROTECT(1);
	return y;
    }
    else if (!isVector(x))
	error(_("first argument must be atomic"));
    args = CDR(args);

    trim = asLogical(CAR(args));
    if (trim == NA_INTEGER)
	error(_("invalid '%s' argument"), "trim");
    args = CDR(args);

    if (!isNull(CAR(args))) {
	digits = asInteger(CAR(args));
	if (digits == NA_INTEGER || digits < R_MIN_DIGITS_OPT
	    || digits > R_MAX_DIGITS_OPT)
	    error(_("invalid '%s' argument"), "digits");
	R_print.digits = digits;
    }
    args = CDR(args);

    nsmall = asInteger(CAR(args));
    if (nsmall == NA_INTEGER || nsmall < 0 || nsmall > 20)
	error(_("invalid '%s' argument"), "nsmall");
    args = CDR(args);

    if (isNull(swd = CAR(args))) wd = 0; else wd = asInteger(swd);
    if(wd == NA_INTEGER)
	error(_("invalid '%s' argument"), "width");
    args = CDR(args);

    adj = asInteger(CAR(args));
    if(adj == NA_INTEGER || adj < 0 || adj > 3)
	error(_("invalid '%s' argument"), "justify");
    args = CDR(args);

    na = asLogical(CAR(args));
    if(na == NA_LOGICAL)
	error(_("invalid '%s' argument"), "na.encode");
    args = CDR(args);

    if(LENGTH(CAR(args)) != 1)
	error(_("invalid '%s' argument"), "scientific");
    if(isLogical(CAR(args))) {
	int tmp = LOGICAL(CAR(args))[0];
	if(tmp == NA_LOGICAL) sci = NA_INTEGER;
	else sci = tmp > 0 ?-100 : 100;
    } else if (isNumeric(CAR(args))) {
	sci = asInteger(CAR(args));
    } else
	error(_("invalid '%s' argument"), "scientific");
    if (sci == NA_INTEGER) {
        R_print.scipen = asInteger(GetOption1(install("scipen")));
        if (R_print.scipen == NA_INTEGER) R_print.scipen = 0;
    }
    else {
        R_print.scipen = sci;
    }

    if ((n = LENGTH(x)) <= 0) {
	PROTECT(y = allocVector(STRSXP, 0));
    } else {
	switch (TYPEOF(x)) {

	case LGLSXP:
	    PROTECT(y = allocVector(STRSXP, n));
	    if (trim) w = 0; else formatLogical(LOGICAL(x), n, &w);
	    w = imax2(w, wd);
	    for (i = 0; i < n; i++) {
		strp = EncodeLogical(LOGICAL(x)[i], w);
		SET_STRING_ELT(y, i, mkChar(strp));
	    }
	    break;

	case INTSXP:
	    PROTECT(y = allocVector(STRSXP, n));
	    if (trim) w = 0;
	    else formatInteger(INTEGER(x), n, &w);
	    w = imax2(w, wd);
	    for (i = 0; i < n; i++) {
		strp = EncodeInteger(INTEGER(x)[i], w);
		SET_STRING_ELT(y, i, mkChar(strp));
	    }
	    break;

	case REALSXP:
	    formatReal(REAL(x), n, &w, &d, &e, nsmall);
	    if (trim) w = 0;
	    w = imax2(w, wd);
	    PROTECT(y = allocVector(STRSXP, n));
	    for (i = 0; i < n; i++) {
		strp = EncodeReal(REAL(x)[i], w, d, e, OutDec);
		SET_STRING_ELT(y, i, mkChar(strp));
	    }
	    break;

	case CPLXSXP:
	    formatComplex(COMPLEX(x), n, &w, &d, &e, &wi, &di, &ei, nsmall);
	    if (trim) wi = w = 0;
	    w = imax2(w, wd); wi = imax2(wi, wd);
	    PROTECT(y = allocVector(STRSXP, n));
	    for (i = 0; i < n; i++) {
		strp = EncodeComplex(COMPLEX(x)[i], w, d, e, wi, di, ei, OutDec);
		SET_STRING_ELT(y, i, mkChar(strp));
	    }
	    break;

	case STRSXP:
	{
	    /* this has to be different from formatString/EncodeString as
	       we don't actually want to encode here */
	    const char *s;
	    char *q;
	    int b, b0, cnt = 0, j;
	    SEXP s0, xx;

	    /* This is clumsy, but it saves rewriting and re-testing
	       this complex code */
	    PROTECT(xx = duplicate(x));
	    for (i = 0; i < n; i++) {
		SEXP tmp =  STRING_ELT(xx, i);
		if(IS_BYTES(tmp)) {
		    const char *p = CHAR(tmp), *q;
		    char *pp = R_alloc(4*strlen(p)+1, 1), *qq = pp, buf[5];
		    for (q = p; *q; q++) {
			unsigned char k = (unsigned char) *q;
			if (k >= 0x20 && k < 0x80) {
			    *qq++ = *q;
			} else {
			    snprintf(buf, 5, "\\x%02x", k);
			    for(int j = 0; j < 4; j++) *qq++ = buf[j];
			}
		    }
		    *qq = '\0';
		    s = pp;
		} else s = translateChar(tmp);
		if(s != CHAR(tmp)) SET_STRING_ELT(xx, i, mkChar(s));
	    }

	    w = wd;
	    if (adj != Rprt_adj_none) {
		for (i = 0; i < n; i++)
		    if (STRING_ELT(xx, i) != NA_STRING)
			w = imax2(w, Rstrlen(STRING_ELT(xx, i), 0));
		    else if (na) w = imax2(w, R_print.na_width);
	    } else w = 0;
	    /* now calculate the buffer size needed, in bytes */
	    for (i = 0; i < n; i++)
		if (STRING_ELT(xx, i) != NA_STRING) {
		    il = Rstrlen(STRING_ELT(xx, i), 0);
		    cnt = imax2(cnt, LENGTH(STRING_ELT(xx, i)) + imax2(0, w-il));
		} else if (na) cnt  = imax2(cnt, R_print.na_width + imax2(0, w-R_print.na_width));
	    char buff[cnt+1];
	    R_CHECKSTACK();
	    PROTECT(y = allocVector(STRSXP, n));
	    for (i = 0; i < n; i++) {
		if(!na && STRING_ELT(xx, i) == NA_STRING) {
		    SET_STRING_ELT_NA(y, i);
		} else {
		    q = buff;
		    if(STRING_ELT(xx, i) == NA_STRING) s0 = R_print.na_string;
		    else s0 = STRING_ELT(xx, i) ;
		    s = CHAR(s0);
		    il = Rstrlen(s0, 0);
		    b = w - il;
		    if(b > 0 && adj != Rprt_adj_left) {
			b0 = (adj == Rprt_adj_centre) ? b/2 : b;
			for(j = 0 ; j < b0 ; j++) *q++ = ' ';
			b -= b0;
		    }
		    for(j = 0; j < LENGTH(s0); j++) *q++ = *s++;
		    if(b > 0 && adj != Rprt_adj_right)
			for(j = 0 ; j < b ; j++) *q++ = ' ';
		    *q = '\0';
		    SET_STRING_ELT(y, i, mkChar(buff));
		}
	    }
	    UNPROTECT(2); /* xx, y */
            PROTECT(y);
	    break;
	}
	default:
	    error(_("Impossible mode ( x )"));
	}
    }
    if((l = getDimAttrib(x)) != R_NilValue) {
	setAttrib(y, R_DimSymbol, l);
	if((l = getAttrib(x, R_DimNamesSymbol)) != R_NilValue)
	    setAttrib(y, R_DimNamesSymbol, l);
    } else if((l = getAttrib(x, R_NamesSymbol)) != R_NilValue)
	setAttrib(y, R_NamesSymbol, l);

    /* In case something else forgets to set PrintDefaults(), PR#14477 */
    R_print.scipen = scikeep;

    UNPROTECT(1); /* y */
    return y;
}

/* format.info(obj)  --> 3 integers  (w,d,e) with the formatting information
 *			w = total width (#{chars}) per item
 *			d = #{digits} to RIGHT of "."
 *			e = {0:2}.   0: Fixpoint;
 *				   1,2: exponential with 2/3 digit expon.
 *
 * for complex : 2 x 3 integers for (Re, Im)
 */

static SEXP do_formatinfo(SEXP call, SEXP op, SEXP args, SEXP env)
{
    SEXP x;
    int n, digits, nsmall, no = 1, w, d, e, wi, di, ei;

    checkArity(op, args);
    x = CAR(args);
    n = LENGTH(x);
    PrintDefaults();

    digits = asInteger(CADR(args));
    if (!isNull(CADR(args))) {
	digits = asInteger(CADR(args));
	if (digits == NA_INTEGER || digits < R_MIN_DIGITS_OPT
	    || digits > R_MAX_DIGITS_OPT)
	    error(_("invalid '%s' argument"), "digits");
	R_print.digits = digits;
    }
    nsmall = asInteger(CADDR(args));
    if (nsmall == NA_INTEGER || nsmall < 0 || nsmall > 20)
	error(_("invalid '%s' argument"), "nsmall");

    w = 0;
    d = 0;
    e = 0;
    switch (TYPEOF(x)) {

    case RAWSXP:
	formatRaw(RAW(x), n, &w);
	break;

    case LGLSXP:
	formatLogical(LOGICAL(x), n, &w);
	break;

    case INTSXP:
	formatInteger(INTEGER(x), n, &w);
	break;

    case REALSXP:
	no = 3;
	formatReal(REAL(x), n, &w, &d, &e, nsmall);
	break;

    case CPLXSXP:
	no = 6;
	wi = di = ei = 0;
	formatComplex(COMPLEX(x), n, &w, &d, &e, &wi, &di, &ei, nsmall);
	break;

    case STRSXP:
    {
	int i, il;
	for (i = 0; i < n; i++)
	    if (STRING_ELT(x, i) != NA_STRING) {
		il = Rstrlen(STRING_ELT(x, i), 0);
		if (il > w) w = il;
	    }
    }
	break;

    default:
	error(_("atomic vector arguments only"));
    }
    x = allocVector(INTSXP, no);
    INTEGER(x)[0] = w;
    if(no > 1) {
	INTEGER(x)[1] = d;
	INTEGER(x)[2] = e;
    }
    if(no > 3) {
	INTEGER(x)[3] = wi;
	INTEGER(x)[4] = di;
	INTEGER(x)[5] = ei;
    }
    return x;
}


/* Binary ! operator. */

SEXP do_paste_bang(SEXP call, SEXP op, SEXP args, SEXP env)
{
    return pasteop(call, op, R_BlankScalarString, R_NilValue, args, env);
}


/* Binary !! operator. */

static SEXP do_paste_bangbang(SEXP call, SEXP op, SEXP args, SEXP env)
{
    return pasteop(call, op, R_ASCII_SCALAR_STRING(' '), R_NilValue, args, env);
}


/* FUNTAB entries defined in this source file. See names.c for documentation. */

attribute_hidden FUNTAB R_FunTab_paste[] =
{
/* printname	c-entry		offset	eval	arity	pp-kind	     precedence	rightassoc */

{"paste",	do_paste,	0,   1000011,	-1,	{PP_FUNCALL, PREC_FN,	0}},
{"!!",		do_paste_bangbang, 0,   1,	2,	{PP_BINARY,  PREC_FN,	0}},
{"file.path",	do_filepath,	0,   1000011,	2,	{PP_FUNCALL, PREC_FN,	0}},
{"format",	do_format,	0,	11,	8,	{PP_FUNCALL, PREC_FN,	0}},
{"format.info",	do_formatinfo,	0,   1000011,	3,	{PP_FUNCALL, PREC_FN,	0}},

{NULL,		NULL,		0,	0,	0,	{PP_INVALID, PREC_FN,	0}}
};
