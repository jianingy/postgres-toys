/* 
 * Text Similarity using Trigram
 *
 * Copyright (C) Jianing Yang <detrox@gmai.com>, 2008
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "tree.h"

#ifndef CLI_DEBUG

#include <postgres.h>
#include <fmgr.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(trgm_sml);
Datum trgm_sml(PG_FUNCTION_ARGS);

#endif

struct term_vector {
	RB_ENTRY(term_vector) ptr;

	size_t		lhs;
	size_t		rhs;
	double		lscore;
	double		rscore;
	char		trgm[1];
};

struct term_seq {
	struct term_vector	**tv;
	struct term_vector	**last;
};

/* Self defined RB_HEAD(term_space, term_vector); */
struct term_space {
	struct term_vector	*rbh_root; /* root of the tree */
	struct term_seq		seq;
};

RB_PROTOTYPE(term_space, term_vector, ptr, term_cmp);

static int 
term_cmp(const struct term_vector *lhs, const struct term_vector *rhs)
{
	return strcmp(lhs->trgm, rhs->trgm);
}

RB_GENERATE(term_space, term_vector, ptr, term_cmp);

static int
term_space_add(struct term_space *ts, const char *s, int side)
{
	struct term_vector		*tv = NULL;
	struct term_vector		*found = NULL;
	size_t					size;


	size = sizeof(struct term_vector) + strlen(s) + 1;
	tv = (struct term_vector *)malloc(size);
	if (tv == NULL) return -1;
	
	memset(tv, 0, size);
	strcpy(tv->trgm, s);
	if (side)
		tv->rhs = 1;
	else
		tv->lhs = 1;
	
	if ((found = RB_FIND(term_space, ts, tv))) {
		/* count old one */

		if (side)
			found->rhs++;
		else
			found->lhs++;

		free(tv);

	} else {
		/* insert a new term */
		
		if (RB_INSERT(term_space, ts, tv)) 
			return -1;
		
		*(ts->seq.last++) = tv;

	}
	
	return 0;
}

static int term_vector_cmp(const void *lhs, const void *rhs)
{
	struct term_vector	*lv, *rv;
	
	lv = (struct term_vector *)*(struct term_vector **)lhs;
	rv = (struct term_vector *)*(struct term_vector **)rhs;

	return -(((lv->lhs + lv->rhs) - (rv->lhs + rv->rhs )));
}

static void term_space_sort(struct term_space *ts)
{
	qsort(ts->seq.tv, 
		(ts->seq.last - ts->seq.tv),
		sizeof(struct term_vector **),
		term_vector_cmp);
}

static int term_space_add_trgm(struct term_space *ts, char *s, int side)
{
	size_t		off, len;
	char 		*p = NULL, *q = NULL, *r = NULL;
	int			error = -1, i;

	len = strlen(s);
	if ((p = malloc(len + 1)) == NULL) goto safe_exit;
	if ((q = malloc(len * 4 + 1)) == NULL) goto safe_exit;
	*q = '\0';

	for (off = 0, i = 0;; off += strlen(p), ++i) {
		off += strspn(s + off, " \t");
		if (sscanf(s + off, "%s", p) < 1) break;
		
#ifdef CLI_DEBUG			
		fprintf(stderr, "term => %s\n", p);
#endif
		strcat(q, p);
		strcat(q, " ");
		if (i > 2) {
			r = strchr(q, ' ') + 1;
			if (*r) {
				len = strlen(r);
				memmove(q, r, len);
				*(q + len) = '\0';
			}
		}
		if (i > 1) {
#ifdef CLI_DEBUG			
			fprintf(stderr, "trgm => %s\n", q);
#endif
			if (term_space_add(ts, q, side) == -1)
				goto safe_exit;
		}
	}


	error = 0;

safe_exit:
	if (p) free(p);	
	if (q) free(q);	

	return error;
}

static double cosine_angle(struct term_space *ts, int top)
{
	struct term_vector	**v;
	double prod = 0.0;
	double len[2] = {0.0, 0.0};
	double denominator = 0.0;

	for (v = ts->seq.tv; v < ts->seq.last && top != 0; v++, top--) {
		prod += (*v)->lscore * (*v)->rscore;
		len[0] += (*v)->lscore * (*v)->lscore;
		len[1] += (*v)->rscore * (*v)->rscore;
#ifdef DEBUG		
		fprintf(stderr, "%s => lhs = %d, rhs = %d, lscore = %f, rscore = %f\n"
			, (*v)->trgm, (*v)->lhs, (*v)->rhs, (*v)->lscore, (*v)->rscore);
#endif		
	}


	denominator = sqrt(len[0] * len[1]);

	return prod / denominator;
}

static int
_trgm_sml(double *score, const char *s, const char *t, int n)
{
	struct term_space	*ts = NULL;
	struct term_vector	**v, *tv;
	int					retval = -1;
	char				*p = NULL, *q = NULL;

	if (s == NULL || t == NULL) return -1;

	if ((p = strdup(s)) == NULL) goto safe_exit;
	if ((q = strdup(t)) == NULL) goto safe_exit;

	ts = (struct term_space *)malloc(sizeof(struct term_space));
	if (ts == NULL) goto safe_exit;

	RB_INIT(ts);

	ts->seq.tv = (struct term_vector **)malloc(sizeof(struct term_vector *) *
			(strlen(s) + strlen(t)));
	if (ts->seq.tv == NULL) goto safe_exit;
	ts->seq.last = ts->seq.tv;

	if (term_space_add_trgm(ts, p, 0) == -1
		|| term_space_add_trgm(ts, q, 1) == -1)
		goto safe_exit;

	term_space_sort(ts);

	for (v = ts->seq.tv; v < ts->seq.last; v++) {
		(*v)->lscore = (*v)->lhs;
		(*v)->rscore = (*v)->rhs;
#if CLI_DEBUG
		fprintf(stderr, "trgm >> %s %d %d\n", (*v)->trgm, (*v)->lhs, (*v)->rhs);
#endif
	}

	*score = cosine_angle(ts, n);

	/* RB_FOREACH uses RB_NEXT to acces next element.
	 * this causes uninitialized value accessing with RB_REMOVE */
	for(tv = RB_MIN(term_space, ts); tv; tv = RB_MIN(term_space, ts)) {
		RB_REMOVE(term_space, ts, tv);
		free(tv);
	}

	retval = 0;	
		
safe_exit:
	if (ts->seq.tv) free(ts->seq.tv);
	if (ts) free(ts);
	if (p) free(p);
	if (q) free(q);
	return retval;
}

#ifdef CLI_DEBUG

int main()
{
        double score;

        printf("retval = %d\n", _trgm_sml(&score, "我 喜欢 北京 天安门", "我 爱 北京 生活", -1));
        printf("score = %f\n", score );
        return 0;
}

#else

Datum trgm_sml(PG_FUNCTION_ARGS)
{
	text			*datum[2];
	double			score = 0.0;
	char			*lhs = NULL;
	char			*rhs = NULL;


	datum[0] = PG_GETARG_TEXT_P(0);
	datum[1] = PG_GETARG_TEXT_P(1);

#define VAR_STRLEN(S) (VARSIZE(S) - VARHDRSZ)

	lhs = strndup(VARDATA(datum[0]), VAR_STRLEN(datum[0]));
	rhs = strndup(VARDATA(datum[1]), VAR_STRLEN(datum[1]));

#undef VAR_STRLEN

	_trgm_sml(&score, lhs, rhs, -1);

	free(lhs);
	free(rhs);

	PG_RETURN_FLOAT8(score);
}

#endif
