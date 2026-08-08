/**
 * @file lv_xml_expr.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */
#include "lv_xml_expr.h"
#if LV_USE_XML
#include <lvgl.h>
#include <lvgl_private.h>
#include <string.h>
#include <stdbool.h>

typedef struct { lv_xml_expr_tok_kind_t kind; const char * start; size_t len; int32_t ival; } tok_t;

static bool is_ident_start(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static bool is_ident_char(char c){ return is_ident_start(c)||(c>='0'&&c<='9'); }

/* Fold a word operator to its symbolic kind, else IDENT. */
static lv_xml_expr_tok_kind_t word_kind(const char * s, size_t n){
    struct { const char * w; lv_xml_expr_tok_kind_t k; } tbl[] = {
        {"and",LV_XML_EXPR_TOK_AND},{"or",LV_XML_EXPR_TOK_OR},{"not",LV_XML_EXPR_TOK_NOT},
        {"eq",LV_XML_EXPR_TOK_EQ},{"ne",LV_XML_EXPR_TOK_NE},{"lt",LV_XML_EXPR_TOK_LT},
        {"le",LV_XML_EXPR_TOK_LE},{"gt",LV_XML_EXPR_TOK_GT},{"ge",LV_XML_EXPR_TOK_GE},
    };
    for(size_t i=0;i<sizeof(tbl)/sizeof(tbl[0]);i++)
        if(strlen(tbl[i].w)==n && strncmp(tbl[i].w,s,n)==0) return tbl[i].k;
    return LV_XML_EXPR_TOK_IDENT;
}

/* Core tokenizer. Returns token count; last token is always EOF (or ERROR). */
static size_t tokenize(const char * s, tok_t * out, size_t cap){
    size_t n = 0;
    #define PUSH(K,ST,LN,IV) do{ if(n<cap){out[n].kind=(K);out[n].start=(ST);out[n].len=(LN);out[n].ival=(IV);} n++; }while(0)
    while(*s){
        if(*s==' '||*s=='\t'||*s=='\n'||*s=='\r'){ s++; continue; }
        char c=*s;
        if(c>='0'&&c<='9'){ int32_t v=0; const char*st=s; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} PUSH(LV_XML_EXPR_TOK_INT,st,(size_t)(s-st),v); continue; }
        if(is_ident_start(c)){ const char*st=s; while(is_ident_char(*s))s++; size_t ln=(size_t)(s-st); PUSH(word_kind(st,ln),st,ln,0); continue; }
        switch(c){
            case '(': PUSH(LV_XML_EXPR_TOK_LPAREN,s,1,0); s++; break;
            case ')': PUSH(LV_XML_EXPR_TOK_RPAREN,s,1,0); s++; break;
            case '+': PUSH(LV_XML_EXPR_TOK_PLUS,s,1,0); s++; break;
            case '-': PUSH(LV_XML_EXPR_TOK_MINUS,s,1,0); s++; break;
            case '*': PUSH(LV_XML_EXPR_TOK_STAR,s,1,0); s++; break;
            case '/': PUSH(LV_XML_EXPR_TOK_SLASH,s,1,0); s++; break;
            case '%': PUSH(LV_XML_EXPR_TOK_PERCENT,s,1,0); s++; break;
            case '&': if(s[1]=='&'){ PUSH(LV_XML_EXPR_TOK_AND,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; } break;
            case '|': if(s[1]=='|'){ PUSH(LV_XML_EXPR_TOK_OR,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; } break;
            case '!': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_NE,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_NOT,s,1,0); s++; } break;
            case '=': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_EQ,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; } break;
            case '<': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_LE,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_LT,s,1,0); s++; } break;
            case '>': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_GE,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_GT,s,1,0); s++; } break;
            default:  PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; break;
        }
    }
    PUSH(LV_XML_EXPR_TOK_EOF,s,0,0);
    #undef PUSH
    return n;
}

#define LV_XML_EXPR_MAX_TOKENS 128
size_t lv_xml_expr_tokenize_for_test(const char * src, lv_xml_expr_tok_kind_t * out, size_t cap){
    tok_t buf[LV_XML_EXPR_MAX_TOKENS];
    size_t n = tokenize(src, buf, LV_XML_EXPR_MAX_TOKENS);
    /* `n` is the TRUE token count and may exceed both `cap` and the internal
     * buffer. Return what was actually WRITTEN, so a caller looping to the
     * return value never reads past what it owns or past what we filled in. */
    if(n > LV_XML_EXPR_MAX_TOKENS) n = LV_XML_EXPR_MAX_TOKENS;
    size_t m = n < cap ? n : cap;
    for (size_t i = 0; i < m; i++) out[i] = buf[i].kind;
    return m;
}

/*=====================
 *  AST + Pratt parser
 *====================*/

typedef enum { N_INT, N_SUB, N_UNARY, N_BINARY } node_kind_t;
typedef struct expr_node {
    node_kind_t kind;
    lv_xml_expr_tok_kind_t op;      /* for UNARY/BINARY */
    int32_t ival;                   /* for N_INT */
    lv_subject_t * subject;         /* for N_SUB */
    struct expr_node * a, * b;
} expr_node_t;

struct lv_xml_expr_t {
    expr_node_t * root;
    lv_subject_t ** subjects;       /* distinct referenced subjects */
    size_t subject_count;
};

typedef struct {
    tok_t * t; size_t pos; bool error;
    lv_xml_expr_resolver_t resolver; void * ctx;
    lv_subject_t * subs[32]; size_t nsub;   /* distinct collector */
} parser_t;

static expr_node_t * parse_or(parser_t * p);   /* fwd */

static tok_t * peek(parser_t * p){ return &p->t[p->pos]; }
static tok_t * advance(parser_t * p){ return &p->t[p->pos++]; }
static bool accept(parser_t * p, lv_xml_expr_tok_kind_t k){ if(peek(p)->kind==k){p->pos++;return true;} return false; }

static expr_node_t * node_new(node_kind_t k){ expr_node_t * n=lv_zalloc(sizeof(expr_node_t)); n->kind=k; return n; }
static void node_free(expr_node_t * n){ if(!n)return; node_free(n->a); node_free(n->b); lv_free(n); }

static void collect_subject(parser_t * p, lv_subject_t * s){
    for(size_t i=0;i<p->nsub;i++) if(p->subs[i]==s) return;
    if(p->nsub < 32) p->subs[p->nsub++]=s;
    else LV_LOG_WARN("expr: more than 32 distinct subjects; dependency dropped");
}

static expr_node_t * parse_primary(parser_t * p){
    tok_t * t = peek(p);
    if(t->kind==LV_XML_EXPR_TOK_INT){ advance(p); expr_node_t*n=node_new(N_INT); n->ival=t->ival; return n; }
    if(t->kind==LV_XML_EXPR_TOK_IDENT){
        advance(p);
        char name[64]; size_t ln=t->len<63?t->len:63; memcpy(name,t->start,ln); name[ln]='\0';
        /* Truncation happens BEFORE the resolver sees the name, so two subjects
         * sharing a 63-char prefix would silently resolve to the same one. */
        if(t->len>ln) LV_LOG_WARN("expr: identifier longer than 63 characters, truncated to '%s'", name);
        lv_subject_t * s = p->resolver ? p->resolver(p->ctx,name) : NULL;
        if(!s){ LV_LOG_WARN("expr: unknown subject '%s'", name); p->error=true; return NULL; }
        collect_subject(p,s);
        expr_node_t*n=node_new(N_SUB); n->subject=s; return n;
    }
    if(accept(p,LV_XML_EXPR_TOK_LPAREN)){
        expr_node_t * n = parse_or(p);
        if(!accept(p,LV_XML_EXPR_TOK_RPAREN)){ p->error=true; }
        return n;
    }
    p->error=true; return NULL;
}

static expr_node_t * parse_unary(parser_t * p){
    tok_t * t = peek(p);
    if(t->kind==LV_XML_EXPR_TOK_NOT || t->kind==LV_XML_EXPR_TOK_MINUS){
        advance(p); expr_node_t*n=node_new(N_UNARY); n->op=t->kind; n->a=parse_unary(p); return n;
    }
    return parse_primary(p);
}

/* generic left-assoc binary level */
static expr_node_t * parse_bin(parser_t * p, const lv_xml_expr_tok_kind_t * ops, size_t nops,
                               expr_node_t * (*next)(parser_t *)){
    expr_node_t * left = next(p);
    for(;;){
        lv_xml_expr_tok_kind_t k = peek(p)->kind; bool m=false;
        for(size_t i=0;i<nops;i++) if(ops[i]==k){m=true;break;}
        if(!m) break;
        advance(p);
        expr_node_t*n=node_new(N_BINARY); n->op=k; n->a=left; n->b=next(p); left=n;
    }
    return left;
}
static expr_node_t * parse_mul(parser_t * p){ static const lv_xml_expr_tok_kind_t o[]={LV_XML_EXPR_TOK_STAR,LV_XML_EXPR_TOK_SLASH,LV_XML_EXPR_TOK_PERCENT}; return parse_bin(p,o,3,parse_unary);}
static expr_node_t * parse_add(parser_t * p){ static const lv_xml_expr_tok_kind_t o[]={LV_XML_EXPR_TOK_PLUS,LV_XML_EXPR_TOK_MINUS}; return parse_bin(p,o,2,parse_mul);}
static expr_node_t * parse_cmp(parser_t * p){ static const lv_xml_expr_tok_kind_t o[]={LV_XML_EXPR_TOK_EQ,LV_XML_EXPR_TOK_NE,LV_XML_EXPR_TOK_LT,LV_XML_EXPR_TOK_LE,LV_XML_EXPR_TOK_GT,LV_XML_EXPR_TOK_GE}; return parse_bin(p,o,6,parse_add);}
static expr_node_t * parse_and(parser_t * p){ static const lv_xml_expr_tok_kind_t o[]={LV_XML_EXPR_TOK_AND}; return parse_bin(p,o,1,parse_cmp);}
static expr_node_t * parse_or(parser_t * p){ static const lv_xml_expr_tok_kind_t o[]={LV_XML_EXPR_TOK_OR}; return parse_bin(p,o,1,parse_and);}

lv_xml_expr_t * lv_xml_expr_compile(const char * src, lv_xml_expr_resolver_t resolver, void * ctx){
    if(src==NULL || src[0]=='\0') return NULL;
    tok_t toks[LV_XML_EXPR_MAX_TOKENS]; size_t nt = tokenize(src, toks, LV_XML_EXPR_MAX_TOKENS);
    if(nt > LV_XML_EXPR_MAX_TOKENS){ LV_LOG_WARN("expr: too many tokens in '%s'", src); return NULL; }
    for(size_t i=0;i<nt;i++) if(toks[i].kind==LV_XML_EXPR_TOK_ERROR){ LV_LOG_WARN("expr: bad token in '%s'", src); return NULL; }
    parser_t p = { .t=toks, .pos=0, .error=false, .resolver=resolver, .ctx=ctx, .nsub=0 };
    expr_node_t * root = parse_or(&p);
    if(p.error || root==NULL || peek(&p)->kind!=LV_XML_EXPR_TOK_EOF){
        LV_LOG_WARN("expr: parse error in '%s'", src);
        node_free(root); return NULL;
    }
    lv_xml_expr_t * e = lv_zalloc(sizeof(lv_xml_expr_t));
    e->root = root; e->subject_count = p.nsub;
    e->subjects = lv_malloc(sizeof(lv_subject_t*) * (p.nsub?p.nsub:1));
    for(size_t i=0;i<p.nsub;i++) e->subjects[i]=p.subs[i];
    return e;
}

/*=====================
 *  Evaluator
 *====================*/

static int32_t eval_node(const expr_node_t * n){
    switch(n->kind){
        case N_INT: return n->ival;
        case N_SUB: return lv_subject_get_int(n->subject);
        case N_UNARY: {
            int32_t v = eval_node(n->a);
            return n->op==LV_XML_EXPR_TOK_NOT ? (v?0:1) : -v;
        }
        case N_BINARY: {
            /* && and || short-circuit, exactly like C. The VALUE is the same
             * either way; what changes is that the guard idiom
             * `d != 0 && n / d > 5` no longer evaluates the division when d is
             * 0 - so no divide-by-zero warning, which in a bound expression was
             * log spam on every single update. */
            if(n->op==LV_XML_EXPR_TOK_AND) return eval_node(n->a) ? (eval_node(n->b)?1:0) : 0;
            if(n->op==LV_XML_EXPR_TOK_OR)  return eval_node(n->a) ? 1 : (eval_node(n->b)?1:0);

            int32_t l = eval_node(n->a), r = eval_node(n->b);
            switch(n->op){
                case LV_XML_EXPR_TOK_EQ:  return l==r; case LV_XML_EXPR_TOK_NE: return l!=r;
                case LV_XML_EXPR_TOK_LT:  return l<r;  case LV_XML_EXPR_TOK_LE: return l<=r;
                case LV_XML_EXPR_TOK_GT:  return l>r;  case LV_XML_EXPR_TOK_GE: return l>=r;
                case LV_XML_EXPR_TOK_PLUS:return l+r;  case LV_XML_EXPR_TOK_MINUS:return l-r;
                case LV_XML_EXPR_TOK_STAR:return l*r;
                case LV_XML_EXPR_TOK_SLASH:  if(r==0){LV_LOG_WARN("expr: divide by zero");return 0;} return l/r;
                case LV_XML_EXPR_TOK_PERCENT:if(r==0){LV_LOG_WARN("expr: mod by zero");return 0;} return l%r;
                default: return 0;
            }
        }
    }
    return 0;
}

int32_t lv_xml_expr_eval(const lv_xml_expr_t * e){ return (e && e->root) ? eval_node(e->root) : 0; }

void lv_xml_expr_free(lv_xml_expr_t * e){ if(!e)return; node_free(e->root); lv_free(e->subjects); lv_free(e); }
size_t lv_xml_expr_subject_count(const lv_xml_expr_t * e){ return e?e->subject_count:0; }
lv_subject_t * lv_xml_expr_subject_at(const lv_xml_expr_t * e, size_t i){ return (e && i<e->subject_count)?e->subjects[i]:NULL; }

/*=====================
 *  Reactive bind (free-once lifetime)
 *====================*/

/* Shared context for all per-subject observers of one bind. Freed exactly
 * once, either from `expr_bind_delete_cb` on the owner's LV_EVENT_DELETE or
 * from `lv_xml_expr_unbind` if the caller detaches early - observers are
 * registered with auto_free_user_data = 0 so they never free it.
 * `registering` suppresses the synchronous callback LVGL fires when each
 * observer is added, so the initial value is delivered exactly once (by the
 * explicit fire in lv_xml_expr_bind) rather than once per distinct subject.
 * `observers`/`observer_count` retain every per-subject observer handle so
 * lv_xml_expr_unbind can lv_observer_remove() them before the owner (or its
 * subjects) go away - mirrors the <subject_expr> teardown in
 * lv_xml_component.c. */
struct lv_xml_expr_bind_t {
    lv_xml_expr_t * expr; void (*cb)(void *, int32_t); void * user_data;
    lv_obj_t * owner; bool registering;
    lv_observer_t ** observers; size_t observer_count;
};
typedef struct lv_xml_expr_bind_t expr_bind_t;

static void expr_bind_observer_cb(lv_observer_t * obs, lv_subject_t * subject){
    LV_UNUSED(subject);
    expr_bind_t * b = obs->user_data;
    if(b->registering) return;   /* suppress the add-time fire; see explicit fire below */
    b->cb(b->user_data, lv_xml_expr_eval(b->expr));
}

static void expr_bind_delete_cb(lv_event_t * e){
    expr_bind_t * b = lv_event_get_user_data(e);
    lv_free(b->observers);
    lv_xml_expr_free(b->expr);
    lv_free(b);
}

lv_xml_expr_bind_t * lv_xml_expr_bind(lv_xml_expr_t * expr, lv_obj_t * owner,
                      void (*cb)(void * user_data, int32_t value), void * user_data){
    expr_bind_t * b = lv_malloc(sizeof(expr_bind_t));
    if(b == NULL) { lv_xml_expr_free(expr); return NULL; }
    b->expr = expr; b->cb = cb; b->user_data = user_data; b->owner = owner; b->registering = true;
    b->observers = NULL; b->observer_count = 0;

    /* One observer per distinct subject, tied to `owner`; do NOT auto-free the
     * shared context (it is freed exactly once by expr_bind_delete_cb or
     * lv_xml_expr_unbind). `registering` is true here so the add-time fire
     * from each observer is suppressed - we deliver the initial value once,
     * explicitly, below. */
    size_t n = lv_xml_expr_subject_count(expr);
    if(n > 0) {
        b->observers = lv_malloc(sizeof(lv_observer_t *) * n);
        if(b->observers == NULL) {
            /* Without a tracking array we cannot detach these observers in
             * lv_xml_expr_unbind; registering them untracked would orphan them
             * on a freed bind ctx (UAF). Register NONE — the initial fire below
             * still delivers the current value; the bind is simply not reactive.
             * Mirrors the <subject_expr> OOM bail in lv_xml_component.c. */
            LV_LOG_ERROR("OOM: expr bind observer array; binding will not be reactive");
            n = 0;
        }
    }
    for(size_t i=0;i<n;i++){
        lv_observer_t * o = lv_subject_add_observer_obj(lv_xml_expr_subject_at(expr,i),
                                                        expr_bind_observer_cb, owner, b);
        if(o) o->auto_free_user_data = 0;
        if(b->observers) b->observers[i] = o;
    }
    b->observer_count = n;
    b->registering = false;

    /* Single free-once hook on owner deletion. */
    lv_obj_add_event_cb(owner, expr_bind_delete_cb, LV_EVENT_DELETE, b);

    /* Fire once now so the target reflects the initial value. */
    cb(user_data, lv_xml_expr_eval(expr));
    return b;
}

void lv_xml_expr_unbind(lv_xml_expr_bind_t * b){
    if(b == NULL) return;
    /* Detach every per-subject observer BEFORE freeing anything - each
     * observer's target is `owner`, so lv_observer_remove() also strips the
     * per-observer unsubscribe-on-delete hook it registered on `owner`. */
    for(size_t i=0;i<b->observer_count;i++){
        if(b->observers && b->observers[i]) lv_observer_remove(b->observers[i]);
    }
    lv_free(b->observers);
    /* Remove the shared delete-cb hook so a later owner delete does not double-free. */
    lv_obj_remove_event_cb_with_user_data(b->owner, expr_bind_delete_cb, b);
    lv_xml_expr_free(b->expr);
    lv_free(b);
}
#endif /* LV_USE_XML */
