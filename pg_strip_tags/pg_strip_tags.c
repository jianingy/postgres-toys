/* created: jianingy <detrox@gmail.com> */

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <postgres.h>
#include <fmgr.h>

#define TRIM_SCRIPT         1
#define CONVERT_BR          2
#define CONVERT_P           4

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(strip_tags);
Datum strip_tags(PG_FUNCTION_ARGS);

struct html_replace {
    char        *from;
    size_t      fsize;
    char        *to;
};

static struct html_replace html_replace[] = {
    {"&nbsp;", 6, " "},
    {"&amp;", 5, "&"},
    {"&gt;", 4, ">"},
    {"&lt;", 4, "<"},
    {NULL, 0, NULL},
};
    

static text* _strip_tags(const char *src, size_t srclen, int flag)
{
    const char      *sp = NULL;
    char            *dst = NULL, *dp = NULL, *tp = NULL;
    char            tagname[1024];
    text            *out = NULL;

    enum running_state {
        RS_UNKNOW = 0,
        RS_BRACKET_BEGIN,
        RS_BRACKET_TAG_BEGIN,
        RS_BRACKET_CONTENT_BEGIN,
        RS_BRACKET_CONTENT_END,
        RS_BRACKET_END,
        RS_QUOTE_BEGIN,
        RS_QUOTE_END,
        RS_SINGLEQUOTE_BEGIN,
        RS_SINGLEQUOTE_END,
        RS_CONTENT,
    } state = RS_UNKNOW, last, out_quote_state = RS_UNKNOW;

#define IN_QUOTE(S) (S == RS_QUOTE_BEGIN || S == RS_SINGLEQUOTE_BEGIN)    
#define IN_BRACKET(S) (S == RS_BRACKET_TAG_BEGIN            \
                    || S == RS_BRACKET_BEGIN                \
                    || S == RS_BRACKET_CONTENT_BEGIN        \
                    || S == RS_BRACKET_CONTENT_END )

    if ((out = (text *)palloc(srclen + VARHDRSZ)) == NULL)
        return NULL;
	SET_VARSIZE(out, srclen + 1);
    dst = VARDATA(out);
    *dst = '\0';

    for (sp = src, dp = dst, tp = tagname; sp - src < srclen; sp++) {
        last = state;
        switch (*sp) {
            case '<':
                if (!IN_QUOTE(state))
                    state = RS_BRACKET_BEGIN;
                break;
            case '>':
                if (!IN_QUOTE(state) && IN_BRACKET(state))
                    state = RS_BRACKET_END;
                break;
            case ' ':
                if (!IN_QUOTE(state) && IN_BRACKET(state))
                    state = RS_BRACKET_CONTENT_BEGIN;
                break;
            case '/':
                if (!IN_QUOTE(state) && IN_BRACKET(state))
                    state = RS_BRACKET_CONTENT_END;
                break;
            case '"':
                if (IN_BRACKET(state) && state != RS_SINGLEQUOTE_BEGIN) {
                    out_quote_state = state;
                    state = RS_QUOTE_BEGIN;
                } else if (state == RS_QUOTE_BEGIN) {
                    state = out_quote_state;
                }
                break;
            case '\'':
                if (IN_BRACKET(state) && state != RS_QUOTE_BEGIN) {
                    out_quote_state = state;
                    state = RS_SINGLEQUOTE_BEGIN;
                } else if (state == RS_SINGLEQUOTE_BEGIN) {
                    state = out_quote_state;
                }
        }
        
        if ((    state == RS_BRACKET_CONTENT_BEGIN 
              || state == RS_BRACKET_CONTENT_END 
              || state == RS_BRACKET_END
            ) && (last != state)) 
        {
            *(tp) = '\0';
            if ((flag & CONVERT_BR) && strcmp("br", tagname) == 0)
                *(dp++) = '\n';
            else if ((flag & CONVERT_P) && strcmp("p", tagname) == 0)
                *(dp++) = '\n';
        }
        
        if (state == RS_BRACKET_BEGIN) {

            tp = tagname;
            *tp = '\0';
            state = RS_BRACKET_TAG_BEGIN;
        
        } else if (state == RS_BRACKET_TAG_BEGIN) {
        
            *(tp++) = tolower(*(sp));
        
        } else if (state == RS_BRACKET_END) {
        
            state = RS_CONTENT;
        
        } else if (state == RS_CONTENT)  {
                
            if ((flag & TRIM_SCRIPT) && strcmp("script", tagname) == 0)
                /* pass */;
            else 
                *dp = *(sp);
            
            if (*dp == ';') {
                struct html_replace *p;
                for (p = html_replace; p->from; p++) {
                    int offset = p->fsize - 1;

                    if (dp >= dst + offset && strncmp(dp - offset, p->from, p->fsize) == 0) {
                        dp -= offset;
                        strcpy(dp, p->to);
                    }
                }
            }

            dp++;
        }
    }

    *dp = '\0';
    return out;

#undef IN_BRACKET    
#undef IN_QUOTE
}


Datum strip_tags(PG_FUNCTION_ARGS)
{
	text			*in, *out;

	in = PG_GETARG_TEXT_P(0);
	out = _strip_tags(VARDATA(in), VARSIZE(in) - VARHDRSZ, TRIM_SCRIPT | CONVERT_BR | CONVERT_P);

	PG_RETURN_TEXT_P(out);
}

// vim: ts=4 sw=4 et cindent
