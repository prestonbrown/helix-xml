/**
 * @file lv_xml_expr.h
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 356C LLC
 */
#ifndef LV_XML_EXPR_H
#define LV_XML_EXPR_H

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lv_xml_expr_t lv_xml_expr_t;

/* Resolve a subject name to an lv_subject_t*. Returns NULL if unknown. */
typedef lv_subject_t * (*lv_xml_expr_resolver_t)(void * ctx, const char * name);

/* Token kinds — exposed only for the lexer test hook. */
typedef enum {
    LV_XML_EXPR_TOK_EOF,
    LV_XML_EXPR_TOK_INT,       /* integer literal            */
    LV_XML_EXPR_TOK_IDENT,     /* subject name / word-op     */
    LV_XML_EXPR_TOK_LPAREN, LV_XML_EXPR_TOK_RPAREN,
    LV_XML_EXPR_TOK_PLUS, LV_XML_EXPR_TOK_MINUS,
    LV_XML_EXPR_TOK_STAR, LV_XML_EXPR_TOK_SLASH, LV_XML_EXPR_TOK_PERCENT,
    LV_XML_EXPR_TOK_EQ, LV_XML_EXPR_TOK_NE,
    LV_XML_EXPR_TOK_LT, LV_XML_EXPR_TOK_LE, LV_XML_EXPR_TOK_GT, LV_XML_EXPR_TOK_GE,
    LV_XML_EXPR_TOK_AND, LV_XML_EXPR_TOK_OR, LV_XML_EXPR_TOK_NOT,
    LV_XML_EXPR_TOK_ERROR,
} lv_xml_expr_tok_kind_t;

/* Test-only: tokenize `src` into `out`.
 *
 * Returns the number of entries actually WRITTEN to `out`, which is
 * min(true token count, `cap`, 128) - 128 being the internal token buffer.
 * It is NOT the true token count: a source with more tokens than that is
 * truncated, and the return value shrinks with it. Looping `for(i = 0; i < ret;
 * i++)` over `out` is therefore always safe.
 *
 * Word operators (and/or/not/eq/ne/lt/le/gt/ge) are folded to their symbolic
 * kind here so the parser never sees the distinction. */
size_t lv_xml_expr_tokenize_for_test(const char * src,
                                     lv_xml_expr_tok_kind_t * out, size_t cap);

/* Compile / evaluate / introspect / free — implemented in later tasks. */
lv_xml_expr_t * lv_xml_expr_compile(const char * src,
                                    lv_xml_expr_resolver_t resolver, void * resolver_ctx);
int32_t lv_xml_expr_eval(const lv_xml_expr_t * expr);
size_t  lv_xml_expr_subject_count(const lv_xml_expr_t * expr);
lv_subject_t * lv_xml_expr_subject_at(const lv_xml_expr_t * expr, size_t i);
void    lv_xml_expr_free(lv_xml_expr_t * expr);

/* Opaque handle for a reactive bind; pass to lv_xml_expr_unbind to detach early. */
typedef struct lv_xml_expr_bind_t lv_xml_expr_bind_t;

/* Observe every referenced subject; call cb(user_data, eval(expr)) on any change and once
 * immediately. Frees `expr` AND detaches observers when `owner` is deleted (LV_EVENT_DELETE).
 * Takes ownership of `expr`. Returns a handle (or NULL on OOM) usable with lv_xml_expr_unbind. */
lv_xml_expr_bind_t * lv_xml_expr_bind(lv_xml_expr_t * expr, lv_obj_t * owner,
                                      void (*cb)(void * user_data, int32_t value), void * user_data);

/* Detach a bind created by lv_xml_expr_bind BEFORE its owner is deleted: removes every
 * per-subject observer and the owner's delete hook (so a later owner delete does not
 * double-free), then frees the expr and the bind context. NULL-safe. */
void lv_xml_expr_unbind(lv_xml_expr_bind_t * handle);

#ifdef __cplusplus
}
#endif
#endif /* LV_XML_EXPR_H */
