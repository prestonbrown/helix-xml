/**
 * @file test_expr.c
 *
 * The expression language in src/xml/lv_xml_expr.h: lexer, compiler,
 * evaluator, subject introspection and the reactive bind lifecycle.
 *
 * This is the engine behind `<subject_expr>` and inline `cond="a or b gt c"`,
 * so its precedence table and its failure modes are load-bearing for every
 * declarative conditional in a consuming app. Compilation failure is reported
 * only as NULL plus a log line, so every documented failure mode gets its own
 * assertion here.
 *
 * ---------------------------------------------------------------------------
 * NOT TESTED, DELIBERATELY
 *
 *  - INT32_MIN / -1 and INT32_MIN % -1: unguarded, and on x86 they raise
 *    SIGFPE, which kills the runner instead of failing a test.
 *  - Integer literals that do not fit in int32_t ("2147483648"): the lexer
 *    accumulates with `v = v*10 + d` and no overflow check, so it is
 *    signed-overflow UB rather than a defined wrap.
 *  - lv_xml_expr_tokenize_for_test(NULL, ...) and (src, NULL, cap>0): neither
 *    is guarded. lv_xml_expr_compile(NULL, ...) IS guarded and is tested.
 *  - lv_xml_expr_bind() with a NULL owner or a NULL cb: both dereference
 *    immediately. A NULL expr IS handled and is tested.
 *  - lv_xml_expr_unbind() after the owner was deleted, and double-unbind:
 *    both are use-after-free with no guard. The header documents that unbind
 *    must happen BEFORE the owner goes away; that ordering is tested, the
 *    violation is not.
 * ---------------------------------------------------------------------------
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Log capture: the divide-by-zero and token-limit warnings are the only
 * observable difference in a couple of cases. */
#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#include "xml/lv_xml_expr.h"

/*---------------------------------------------------------------------------
 * Unity fixture
 *--------------------------------------------------------------------------*/

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

/*---------------------------------------------------------------------------
 * Test subjects + resolver
 *
 * The subjects are file-static but are re-initialised at the top of every test
 * that uses them. That is safe because helix_test_env_teardown() deletes the
 * screen first, which deletes every owner object, which detaches every
 * observer - so no observer is ever left pointing at a subject that is about
 * to be re-initialised.
 *--------------------------------------------------------------------------*/

#define EXPR_SUBJECT_POOL 40

static lv_subject_t g_subs[EXPR_SUBJECT_POOL];
static char g_sub_names[EXPR_SUBJECT_POOL][8];

/** Last name the resolver was asked for - used to observe identifier truncation. */
static char g_last_resolved[128];

static void subjects_reset(void)
{
    for(int i = 0; i < EXPR_SUBJECT_POOL; i++) {
        snprintf(g_sub_names[i], sizeof(g_sub_names[i]), "s%d", i);
        lv_subject_init_int(&g_subs[i], 0);
    }
    g_last_resolved[0] = '\0';
}

/**
 * Resolver over the pool. "a", "b", "c", "d" are friendly aliases for s0..s3
 * so the expression sources below read like real XML.
 */
static lv_subject_t * pool_resolver(void * ctx, const char * name)
{
    LV_UNUSED(ctx);

    lv_strncpy(g_last_resolved, name, sizeof(g_last_resolved) - 1);
    g_last_resolved[sizeof(g_last_resolved) - 1] = '\0';

    static const char * aliases[] = {"a", "b", "c", "d"};
    for(int i = 0; i < 4; i++) {
        if(strcmp(name, aliases[i]) == 0) return &g_subs[i];
    }
    for(int i = 0; i < EXPR_SUBJECT_POOL; i++) {
        if(strcmp(name, g_sub_names[i]) == 0) return &g_subs[i];
    }
    return NULL;
}

/** A resolver that never knows anything - proves the unresolved-ident path. */
static lv_subject_t * null_resolver(void * ctx, const char * name)
{
    LV_UNUSED(ctx);
    lv_strncpy(g_last_resolved, name, sizeof(g_last_resolved) - 1);
    g_last_resolved[sizeof(g_last_resolved) - 1] = '\0';
    return NULL;
}

/** Compile with the pool resolver and require success. */
static lv_xml_expr_t * must_compile(const char * src)
{
    lv_xml_expr_t * e = lv_xml_expr_compile(src, pool_resolver, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, helix_xml_assert_msgf("expected \"%s\" to compile", src));
    return e;
}

/** Compile a subject-free expression and evaluate it in one go. */
static int32_t eval_literal(const char * src)
{
    lv_xml_expr_t * e = lv_xml_expr_compile(src, NULL, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, helix_xml_assert_msgf("expected \"%s\" to compile", src));
    int32_t v = lv_xml_expr_eval(e);
    lv_xml_expr_free(e);
    return v;
}

/*===========================================================================
 * Tokenizer  (lv_xml_expr_tokenize_for_test)
 *
 * Ownership: `out` is caller-provided storage and the function allocates
 * nothing, so there is never anything to free.
 *==========================================================================*/

/** Compare a tokenisation against an expected kind sequence. */
static void assert_tokens(const char * src, const lv_xml_expr_tok_kind_t * expect, size_t n_expect)
{
    lv_xml_expr_tok_kind_t got[64];
    size_t n = lv_xml_expr_tokenize_for_test(src, got, sizeof(got) / sizeof(got[0]));

    TEST_ASSERT_EQUAL_size_t_MESSAGE(n_expect, n,
                                     helix_xml_assert_msgf("wrong token count for \"%s\"", src));
    for(size_t i = 0; i < n_expect; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)expect[i], (int)got[i],
                                      helix_xml_assert_msgf("wrong token %u for \"%s\"",
                                                            (unsigned)i, src));
    }
}

static void test_tokenize_emits_eof_even_for_an_empty_source(void)
{
    static const lv_xml_expr_tok_kind_t want[] = {LV_XML_EXPR_TOK_EOF};
    assert_tokens("", want, 1);
}

static void test_tokenize_recognises_literals_identifiers_and_parens(void)
{
    static const lv_xml_expr_tok_kind_t want[] = {
        LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_PLUS, LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("a + 1", want, 4);

    static const lv_xml_expr_tok_kind_t parens[] = {
        LV_XML_EXPR_TOK_LPAREN, LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_RPAREN, LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("(42)", parens, 4);

    /* Identifiers may contain digits and underscores after the first char. */
    static const lv_xml_expr_tok_kind_t ident[] = {LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_EOF};
    assert_tokens("_my_subject_2", ident, 2);
}

static void test_tokenize_skips_all_four_whitespace_characters(void)
{
    static const lv_xml_expr_tok_kind_t want[] = {
        LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_PLUS, LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens(" \t\n\r1\t+\n2\r ", want, 4);
}

static void test_tokenize_recognises_every_symbolic_operator(void)
{
    static const lv_xml_expr_tok_kind_t want[] = {
        LV_XML_EXPR_TOK_PLUS,  LV_XML_EXPR_TOK_MINUS, LV_XML_EXPR_TOK_STAR,
        LV_XML_EXPR_TOK_SLASH, LV_XML_EXPR_TOK_PERCENT,
        LV_XML_EXPR_TOK_EQ,    LV_XML_EXPR_TOK_NE,
        LV_XML_EXPR_TOK_LE,    LV_XML_EXPR_TOK_GE,
        LV_XML_EXPR_TOK_LT,    LV_XML_EXPR_TOK_GT,
        LV_XML_EXPR_TOK_AND,   LV_XML_EXPR_TOK_OR,    LV_XML_EXPR_TOK_NOT,
        LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("+ - * / % == != <= >= < > && || !", want, 15);
}

/** Word operators are folded to symbol kinds in the lexer, not the parser. */
static void test_tokenize_folds_word_operators_to_symbol_kinds(void)
{
    static const lv_xml_expr_tok_kind_t want[] = {
        LV_XML_EXPR_TOK_AND, LV_XML_EXPR_TOK_OR, LV_XML_EXPR_TOK_NOT,
        LV_XML_EXPR_TOK_EQ,  LV_XML_EXPR_TOK_NE, LV_XML_EXPR_TOK_LT,
        LV_XML_EXPR_TOK_LE,  LV_XML_EXPR_TOK_GT, LV_XML_EXPR_TOK_GE,
        LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("and or not eq ne lt le gt ge", want, 10);
}

/** Word operators are case-sensitive: uppercase lexes as a plain identifier. */
static void test_tokenize_word_operators_are_case_sensitive(void)
{
    static const lv_xml_expr_tok_kind_t want[] = {
        LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("AND Not OR", want, 4);
}

static void test_tokenize_reports_unknown_and_half_written_operators_as_errors(void)
{
    static const lv_xml_expr_tok_kind_t lone_amp[] = {
        LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_ERROR, LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("a & b", lone_amp, 4);
    assert_tokens("a | b", lone_amp, 4);
    assert_tokens("a = b", lone_amp, 4);

    /* Any other character is one ERROR token and lexing continues. */
    static const lv_xml_expr_tok_kind_t junk[] = {LV_XML_EXPR_TOK_ERROR, LV_XML_EXPR_TOK_EOF};
    assert_tokens("$", junk, 2);
    assert_tokens("#", junk, 2);
    assert_tokens(",", junk, 2);
    assert_tokens("@", junk, 2);

    /* A float literal is an INT, an ERROR ('.') and another INT. */
    static const lv_xml_expr_tok_kind_t flt[] = {
        LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_ERROR, LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_EOF,
    };
    assert_tokens("1.5", flt, 4);
}

static void test_tokenize_writes_nothing_when_cap_is_zero(void)
{
    lv_xml_expr_tok_kind_t sentinel = LV_XML_EXPR_TOK_ERROR;

    size_t n = lv_xml_expr_tokenize_for_test("1 + 2", &sentinel, 0);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, n, "nothing was written, so the return must be 0");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_XML_EXPR_TOK_ERROR, (int)sentinel,
                                  "cap == 0 must not write to the output buffer");
}

/**
 * The return value is the number of entries actually WRITTEN, i.e.
 * min(true token count, cap, 128) - 128 being the internal token buffer. It
 * used to be the TRUE token count, so `for(i = 0; i < n; i++)` over `out` read
 * uninitialised memory on any source longer than the buffer or the cap.
 */
static void test_tokenize_returns_the_number_of_entries_it_wrote(void)
{
    /* 200 INT tokens plus EOF = 201, well past the internal 128-token buffer. */
    char src[1024];
    size_t pos = 0;
    for(int i = 0; i < 200; i++) {
        src[pos++] = '1';
        src[pos++] = ' ';
    }
    src[pos] = '\0';

    lv_xml_expr_tok_kind_t out[256];
    for(size_t i = 0; i < sizeof(out) / sizeof(out[0]); i++) out[i] = LV_XML_EXPR_TOK_ERROR;

    size_t n = lv_xml_expr_tokenize_for_test(src, out, sizeof(out) / sizeof(out[0]));

    TEST_ASSERT_EQUAL_size_t_MESSAGE(128, n,
                                     "the internal buffer holds 128 tokens, so 128 is all that "
                                     "can be written however large cap is");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_XML_EXPR_TOK_INT, (int)out[127],
                                  "every entry up to the returned count must be written");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_XML_EXPR_TOK_ERROR, (int)out[128],
                                  "nothing past the returned count may be touched");

    /* `cap` is the other clamp, and it wins when it is the smaller one. */
    for(size_t i = 0; i < sizeof(out) / sizeof(out[0]); i++) out[i] = LV_XML_EXPR_TOK_ERROR;
    n = lv_xml_expr_tokenize_for_test("1 + 2", out, 2);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, n, "cap must clamp the return value too");
    TEST_ASSERT_EQUAL_INT((int)LV_XML_EXPR_TOK_INT, (int)out[0]);
    TEST_ASSERT_EQUAL_INT((int)LV_XML_EXPR_TOK_PLUS, (int)out[1]);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)LV_XML_EXPR_TOK_ERROR, (int)out[2],
                                  "writing must stop at cap");
}

/*===========================================================================
 * lv_xml_expr_compile - success
 *==========================================================================*/

static void test_compile_accepts_subject_free_expressions_without_a_resolver(void)
{
    static const char * ok[] = {
        "1", "-1", "1+2*3", "(1+2)*3", "!0", "1 < 2", "1 and 0", "10 % 3", "--5",
    };

    for(size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        lv_xml_expr_t * e = lv_xml_expr_compile(ok[i], NULL, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(e, helix_xml_assert_msgf("\"%s\" should compile with no resolver",
                                                              ok[i]));
        lv_xml_expr_free(e);
    }
}

static void test_compile_resolves_identifiers_through_the_resolver(void)
{
    subjects_reset();

    lv_xml_expr_t * e = must_compile("a + b");
    TEST_ASSERT_EQUAL_size_t(2, lv_xml_expr_subject_count(e));
    lv_xml_expr_free(e);
}

/*===========================================================================
 * lv_xml_expr_compile - every documented failure mode
 *==========================================================================*/

/** Cases 1 and 2: NULL and empty source are rejected SILENTLY. */
static void test_compile_rejects_null_and_empty_source_without_warning(void)
{
    log_capture_start();
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile(NULL, pool_resolver, NULL),
                             "a NULL source must be rejected, not dereferenced");
    TEST_ASSERT_NULL(lv_xml_expr_compile("", pool_resolver, NULL));
    log_capture_stop();

    TEST_ASSERT_FALSE_MESSAGE(log_contains("expr:"),
                              "an empty ${} splice currently produces no diagnostic at all");
}

/** Case 3: more than 128 tokens (EOF included) is rejected with a warning. */
static void test_compile_rejects_more_than_128_tokens(void)
{
    char at_limit[512];
    char over_limit[512];
    size_t pos;

    /* "1+1+...+1" with 63 '+' is 1 + 2*63 + EOF = 128 tokens: exactly the cap. */
    pos = 0;
    at_limit[pos++] = '1';
    for(int i = 0; i < 63; i++) {
        at_limit[pos++] = '+';
        at_limit[pos++] = '1';
    }
    at_limit[pos] = '\0';

    /* One unary minus in front pushes it to 129. */
    pos = 0;
    over_limit[pos++] = '-';
    memcpy(over_limit + pos, at_limit, strlen(at_limit) + 1);

    lv_xml_expr_t * ok = lv_xml_expr_compile(at_limit, NULL, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(ok, "exactly 128 tokens must still compile");
    TEST_ASSERT_EQUAL_INT32(64, lv_xml_expr_eval(ok));
    lv_xml_expr_free(ok);

    log_capture_start();
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile(over_limit, NULL, NULL),
                             "129 tokens must be rejected");
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("expr: too many tokens"));
}

/** Case 4: any ERROR token anywhere fails the whole compile. */
static void test_compile_rejects_any_bad_token(void)
{
    static const char * bad[] = {"1 & 2", "1 | 2", "1 = 2", "1.5", "a $ b", "1 # 2"};

    log_capture_start();
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile(bad[i], pool_resolver, NULL),
                                 helix_xml_assert_msgf("\"%s\" contains an ERROR token and must fail",
                                                       bad[i]));
    }
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("expr: bad token"));
}

/** Case 5: an identifier the resolver cannot resolve, including no resolver. */
static void test_compile_rejects_unresolved_identifiers(void)
{
    subjects_reset();

    log_capture_start();
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile("a", NULL, NULL),
                             "a NULL resolver must fail every identifier");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile("nope", null_resolver, NULL),
                             "a resolver returning NULL must fail the compile");
    /* "0x10" lexes as INT(0) followed by IDENT(x10), so it fails on the ident. */
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile("0x10", null_resolver, NULL),
                             "there is no hex literal syntax");
    log_capture_stop();

    TEST_ASSERT_TRUE(log_contains("expr: unknown subject 'a'"));
    TEST_ASSERT_TRUE_MESSAGE(log_contains("expr: parse error"),
                             "an unresolved subject warns twice: unknown subject, then parse error");
}

/** Cases 6-8: syntax errors, an empty parse and trailing tokens. */
static void test_compile_rejects_syntax_errors(void)
{
    static const char * bad[] = {
        "(",        /* unterminated group                        */
        ")",        /* nothing to parse                          */
        "(1",       /* missing RPAREN                            */
        "1 +",      /* missing right operand                     */
        "* 3",      /* missing left operand                      */
        "1 2",      /* trailing token after a complete expression*/
        "1)",       /* trailing RPAREN                           */
        "-",        /* unary with no operand                     */
        "!",        /* unary with no operand                     */
        "+1",       /* there is NO unary plus                    */
        "()",       /* empty group                               */
    };

    log_capture_start();
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_compile(bad[i], pool_resolver, NULL),
                                 helix_xml_assert_msgf("\"%s\" is not a valid expression", bad[i]));
    }
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("expr: parse error"));
}

/**
 * Identifiers are copied into a char[64], so anything longer is truncated to 63
 * characters BEFORE the resolver is called - two subject names sharing a
 * 63-character prefix resolve to the same subject. The truncation itself is a
 * buffer limit and stays; what must not happen is it being SILENT.
 */
static void test_compile_warns_when_it_truncates_a_long_identifier(void)
{
    char long_name[71];
    memset(long_name, 'a', 70);
    long_name[70] = '\0';

    log_capture_start();
    TEST_ASSERT_NULL(lv_xml_expr_compile(long_name, null_resolver, NULL));
    log_capture_stop();

    TEST_ASSERT_EQUAL_size_t_MESSAGE(63, strlen(g_last_resolved),
                                     "the resolver is handed only the first 63 characters");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(long_name, g_last_resolved, 63,
                                     "the truncated prefix must be the head of the identifier");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("truncated to 'aaaaaaaa"),
                             "truncation must warn and show what the resolver actually got");

    /* Exactly 63 characters fits, so nothing is truncated and nothing warns. */
    char exact_name[64];
    memset(exact_name, 'b', 63);
    exact_name[63] = '\0';

    log_capture_start();
    TEST_ASSERT_NULL(lv_xml_expr_compile(exact_name, null_resolver, NULL));
    log_capture_stop();

    TEST_ASSERT_EQUAL_size_t(63, strlen(g_last_resolved));
    TEST_ASSERT_FALSE_MESSAGE(log_contains("truncated"),
                              "an identifier that fits exactly must not warn");
}

/*===========================================================================
 * lv_xml_expr_eval - arithmetic, comparison, boolean
 *==========================================================================*/

static void test_eval_computes_arithmetic(void)
{
    TEST_ASSERT_EQUAL_INT32(3, eval_literal("1 + 2"));
    TEST_ASSERT_EQUAL_INT32(-1, eval_literal("1 - 2"));
    TEST_ASSERT_EQUAL_INT32(12, eval_literal("3 * 4"));
    TEST_ASSERT_EQUAL_INT32(3, eval_literal("7 / 2"));
    /* Integer division truncates toward zero, so this is -3 not -4. */
    TEST_ASSERT_EQUAL_INT32(-3, eval_literal("-7 / 2"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("7 % 3"));
    /* The remainder takes the sign of the dividend. */
    TEST_ASSERT_EQUAL_INT32(-1, eval_literal("-7 % 3"));
}

static void test_eval_computes_comparisons_as_one_or_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("2 == 2"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("2 == 3"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("2 != 3"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("2 < 3"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("2 <= 2"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("2 > 3"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("3 >= 3"));
}

static void test_eval_normalises_boolean_results_to_one_or_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("1 && 1"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("1 && 0"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("0 || 1"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("0 || 0"));
    /* Any non-zero operand is true, but the RESULT is always exactly 1. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, eval_literal("5 && 7"),
                                    "&& must yield a canonical 1, not one of its operands");
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("5 || 7"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("!5"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("!0"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("!!5"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("!!0"));
}

static void test_eval_applies_unary_minus_and_not_recursively(void)
{
    TEST_ASSERT_EQUAL_INT32(-5, eval_literal("-5"));
    TEST_ASSERT_EQUAL_INT32(5, eval_literal("--5"));
    TEST_ASSERT_EQUAL_INT32(-6, eval_literal("-2 * 3"));
    TEST_ASSERT_EQUAL_INT32(-6, eval_literal("2 * -3"));
    TEST_ASSERT_EQUAL_INT32(-1, eval_literal("-(2 - 1)"));
}

static void test_eval_returns_zero_for_a_null_expression(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, lv_xml_expr_eval(NULL),
                                    "lv_xml_expr_eval(NULL) must be 0, not a crash");
}

/*===========================================================================
 * Precedence and associativity
 *==========================================================================*/

static void test_precedence_binds_multiplication_before_addition(void)
{
    TEST_ASSERT_EQUAL_INT32(7, eval_literal("1 + 2 * 3"));
    TEST_ASSERT_EQUAL_INT32(9, eval_literal("(1 + 2) * 3"));
    TEST_ASSERT_EQUAL_INT32(7, eval_literal("2 * 3 + 1"));
}

static void test_precedence_orders_arithmetic_comparison_and_and_or(void)
{
    /* Arithmetic binds tighter than comparison. */
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("1 + 1 == 2"));
    /* Comparison binds tighter than &&, which binds tighter than ||. */
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("1 < 2 && 3 > 2"));
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, eval_literal("0 && 0 || 1"),
                                    "|| is the lowest level, so this is (0 && 0) || 1");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, eval_literal("0 && (0 || 1)"),
                                    "parentheses must be able to override the default grouping");
}

static void test_binary_operators_are_left_associative(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(5, eval_literal("10 - 3 - 2"),
                                    "subtraction must group left: (10-3)-2, not 10-(3-2)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(2, eval_literal("2 * 3 % 4"),
                                    "* and %% share a level and group left: (2*3)%%4");
    TEST_ASSERT_EQUAL_INT32(4, eval_literal("16 / 2 / 2"));
}

/* PINS INTENTIONAL BEHAVIOUR: all six comparison operators share one
 * left-associative level, so "1 < 2 < 3" is (1<2) < 3. That is exactly what C
 * does, and this language is deliberately C-shaped - rejecting the chain would
 * be a grammar change, not a bug fix. It is an authoring trap, not a defect. */
static void test_chained_comparisons_fold_left_instead_of_being_rejected(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, eval_literal("1 < 2 < 3"),
                                    "(1<2) < 3 == 1 < 3 == 1");
    /* Proof the reading really is left-folded: 3 > 2 > 1 is (1) > 1 == 0. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, eval_literal("3 > 2 > 1"),
                                    "(3>2) > 1 == 1 > 1 == 0, NOT the mathematical reading");
}

/* PINS INTENTIONAL BEHAVIOUR: the unary operators bind TIGHTER than comparison,
 * which is the opposite of Python's `not`. "not 5 == 1" is "(not 5) == 1", not
 * "not (5 == 1)". This is correct per the grammar (parse_unary sits below
 * parse_cmp) and matches C's `!`; only the word spelling makes it read
 * otherwise. A classic authoring trap, not a defect. */
static void test_unary_not_binds_tighter_than_comparison(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, eval_literal("not 5 == 1"),
                                    "(not 5) == 1 -> 0 == 1 -> 0");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, eval_literal("not (5 == 1)"),
                                    "the grouped form is the one that means what it reads like");
}

/*===========================================================================
 * Word forms vs symbol forms
 *==========================================================================*/

/**
 * The lexer folds word operators onto the symbolic token kinds, so the two
 * spellings must be indistinguishable all the way through evaluation.
 */
static void test_word_and_symbol_operators_are_equivalent(void)
{
    static const struct {
        const char * sym;
        const char * word;
    } pairs[] = {
        {"1 && 0", "1 and 0"},
        {"1 || 0", "1 or 0"},
        {"!0", "not 0"},
        {"2 == 2", "2 eq 2"},
        {"2 != 3", "2 ne 3"},
        {"2 < 3", "2 lt 3"},
        {"2 <= 2", "2 le 2"},
        {"3 > 2", "3 gt 2"},
        {"3 >= 3", "3 ge 3"},
        {"1 < 2 && 3 > 2", "1 lt 2 and 3 gt 2"},
        {"!(1 == 2) || 0", "not (1 eq 2) or 0"},
    };

    for(size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        int32_t s = eval_literal(pairs[i].sym);
        int32_t w = eval_literal(pairs[i].word);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(s, w,
                                        helix_xml_assert_msgf("\"%s\" and \"%s\" must evaluate alike",
                                                              pairs[i].sym, pairs[i].word));
    }
}

/*===========================================================================
 * Divide and modulo by zero
 *==========================================================================*/

/**
 * Division by zero is not an error that propagates: the sub-expression yields
 * 0, a warning is logged, and evaluation continues. Because eval_node runs
 * afresh on every subject change, the warning re-fires on every update.
 */
static void test_divide_and_modulo_by_zero_yield_zero_and_warn(void)
{
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("10 / 0"));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("expr: divide by zero"));

    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("10 % 0"));
    log_capture_stop();
    TEST_ASSERT_TRUE(log_contains("expr: mod by zero"));

    /* The surrounding expression keeps evaluating around the zeroed term. */
    TEST_ASSERT_EQUAL_INT32(5, eval_literal("5 + 10 / 0"));
}

/**
 * && and || short-circuit, exactly like C. eval_node used to compute both
 * operands before looking at the operator, so the guard idiom
 * `d != 0 && n / d > 5` did not actually protect the division: it warned on
 * every evaluation, which in a bound expression is log spam on every update.
 *
 * The VALUE never changed - only the spurious evaluation and its warning.
 */
static void test_boolean_operators_short_circuit(void)
{
    log_capture_start();
    int32_t v = eval_literal("0 && (10 / 0)");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_FALSE_MESSAGE(log_contains("expr: divide by zero"),
                              "a false left operand must skip the right side of &&");

    log_capture_start();
    v = eval_literal("1 || (10 / 0)");
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT32(1, v);
    TEST_ASSERT_FALSE_MESSAGE(log_contains("expr: divide by zero"),
                              "a true left operand must skip the right side of ||");

    /* The mirror cases: when the left operand does NOT decide the result, the
     * right one still has to be evaluated. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("1 && (10 / 0)"));
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("expr: divide by zero"),
                             "a true left operand must NOT skip the right side of &&");

    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("0 || (10 / 0)"));
    log_capture_stop();
    TEST_ASSERT_TRUE_MESSAGE(log_contains("expr: divide by zero"),
                             "a false left operand must NOT skip the right side of ||");

    /* Values are unchanged across the whole truth table. */
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("1 && 1"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("1 && 0"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("0 && 1"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("0 && 0"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("1 || 1"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("1 || 0"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("0 || 1"));
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("0 || 0"));
    /* Non-zero operands still canonicalise to exactly 1. */
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("5 && 7"));
    TEST_ASSERT_EQUAL_INT32(1, eval_literal("5 || 7"));

    /* The real-world idiom this fix exists for: the guard now guards. */
    log_capture_start();
    TEST_ASSERT_EQUAL_INT32(0, eval_literal("0 != 0 && 10 / 0 > 5"));
    log_capture_stop();
    TEST_ASSERT_FALSE_MESSAGE(log_contains("expr: divide by zero"),
                              "`d != 0 && n / d > 5` must not evaluate the division when d is 0");
}

/*===========================================================================
 * Subject operands
 *==========================================================================*/

static void test_eval_reads_subject_values_at_evaluation_time(void)
{
    subjects_reset();
    lv_subject_set_int(&g_subs[0], 7);
    lv_subject_set_int(&g_subs[1], 3);

    lv_xml_expr_t * e = must_compile("a * 10 + b");
    TEST_ASSERT_EQUAL_INT32(73, lv_xml_expr_eval(e));

    /* No memoisation: the next eval sees the new values. */
    lv_subject_set_int(&g_subs[0], 1);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(13, lv_xml_expr_eval(e),
                                    "eval must re-read subjects, not cache the first result");

    lv_xml_expr_free(e);
}

static void test_subject_list_is_deduplicated_by_pointer_in_first_use_order(void)
{
    subjects_reset();

    lv_xml_expr_t * one = must_compile("a + a");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, lv_xml_expr_subject_count(one),
                                     "the same subject twice must be collected once");
    TEST_ASSERT_EQUAL_PTR(&g_subs[0], lv_xml_expr_subject_at(one, 0));
    lv_xml_expr_free(one);

    /* Order is first occurrence, left to right through the source. */
    lv_xml_expr_t * two = must_compile("b + a + b");
    TEST_ASSERT_EQUAL_size_t(2, lv_xml_expr_subject_count(two));
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&g_subs[1], lv_xml_expr_subject_at(two, 0),
                                  "\"b\" appears first in the source so it must be subject 0");
    TEST_ASSERT_EQUAL_PTR(&g_subs[0], lv_xml_expr_subject_at(two, 1));
    lv_xml_expr_free(two);
}

static void test_subject_accessors_are_null_safe_and_bounds_checked(void)
{
    subjects_reset();

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, lv_xml_expr_subject_count(NULL),
                                     "subject_count(NULL) must be 0");
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_subject_at(NULL, 0), "subject_at(NULL, ...) must be NULL");

    lv_xml_expr_t * e = must_compile("a");
    TEST_ASSERT_EQUAL_size_t(1, lv_xml_expr_subject_count(e));
    TEST_ASSERT_NOT_NULL(lv_xml_expr_subject_at(e, 0));
    TEST_ASSERT_NULL_MESSAGE(lv_xml_expr_subject_at(e, 1),
                             "an out-of-range index must be NULL, never a read past the array");
    TEST_ASSERT_NULL(lv_xml_expr_subject_at(e, 1000));
    lv_xml_expr_free(e);

    /* An expression with no subjects still reports 0 and yields NULL. */
    lv_xml_expr_t * lit = lv_xml_expr_compile("1 + 1", NULL, NULL);
    TEST_ASSERT_NOT_NULL(lit);
    TEST_ASSERT_EQUAL_size_t(0, lv_xml_expr_subject_count(lit));
    TEST_ASSERT_NULL(lv_xml_expr_subject_at(lit, 0));
    lv_xml_expr_free(lit);
}

/* PINS INTENTIONAL BEHAVIOUR: the 33rd distinct subject is dropped from the
 * dependency list with a warning, but its AST node is still built. So the
 * expression evaluates correctly while subject_count under-reports, and a bind
 * will not react to changes in the dropped subjects. The cap is a fixed-size
 * collector that already warns; raising it (or making it dynamic) is a design
 * change with an allocation story attached, not a bug fix. */
static void test_more_than_32_distinct_subjects_are_dropped_from_the_dependency_list(void)
{
    subjects_reset();

    /* s0 + s1 + ... + s32 : 33 distinct subjects, 67 tokens. */
    char src[512];
    size_t pos = 0;
    for(int i = 0; i <= 32; i++) {
        pos += (size_t)snprintf(src + pos, sizeof(src) - pos, "%ss%d", i ? "+" : "", i);
        lv_subject_set_int(&g_subs[i], 1);
    }

    log_capture_start();
    lv_xml_expr_t * e = must_compile(src);
    log_capture_stop();

    TEST_ASSERT_EQUAL_size_t_MESSAGE(32, lv_xml_expr_subject_count(e),
                                     "the dependency list is capped at 32 entries");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(33, lv_xml_expr_eval(e),
                                    "the 33rd subject is still read by eval - only the dependency "
                                    "is dropped");
    TEST_ASSERT_TRUE(log_contains("expr: more than 32 distinct subjects"));

    lv_xml_expr_free(e);
}

/*===========================================================================
 * Reactive bind lifecycle
 *==========================================================================*/

typedef struct {
    int calls;
    int32_t last;
} bind_probe_t;

static void bind_probe_cb(void * user_data, int32_t value)
{
    bind_probe_t * p = user_data;
    p->calls++;
    p->last = value;
}

static void test_bind_fires_once_immediately_with_the_current_value(void)
{
    subjects_reset();
    lv_subject_set_int(&g_subs[0], 21);

    lv_obj_t * owner = lv_obj_create(helix_test_env_screen());
    bind_probe_t probe = {0, 0};

    lv_xml_expr_bind_t * h = lv_xml_expr_bind(must_compile("a * 2"), owner, bind_probe_cb, &probe);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, probe.calls,
                                  "bind must deliver the initial value exactly once, not once "
                                  "per observed subject");
    TEST_ASSERT_EQUAL_INT32(42, probe.last);

    lv_xml_expr_unbind(h);
}

static void test_bind_reevaluates_when_any_referenced_subject_changes(void)
{
    subjects_reset();

    lv_obj_t * owner = lv_obj_create(helix_test_env_screen());
    bind_probe_t probe = {0, 0};

    lv_xml_expr_bind_t * h = lv_xml_expr_bind(must_compile("a + b"), owner, bind_probe_cb, &probe);
    TEST_ASSERT_EQUAL_INT(1, probe.calls);

    lv_subject_set_int(&g_subs[0], 10);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, probe.calls, "changing \"a\" must re-fire the binding");
    TEST_ASSERT_EQUAL_INT32(10, probe.last);

    lv_subject_set_int(&g_subs[1], 5);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, probe.calls, "changing \"b\" must re-fire the binding");
    TEST_ASSERT_EQUAL_INT32(15, probe.last);

    /* A subject the expression does not reference must not fire it. */
    lv_subject_set_int(&g_subs[2], 99);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, probe.calls,
                                  "an unreferenced subject must not trigger the binding");

    lv_xml_expr_unbind(h);
}

/** De-duplication means one observer, hence one callback per change. */
static void test_bind_fires_once_per_change_for_a_repeated_subject(void)
{
    subjects_reset();

    lv_obj_t * owner = lv_obj_create(helix_test_env_screen());
    bind_probe_t probe = {0, 0};

    lv_xml_expr_bind_t * h = lv_xml_expr_bind(must_compile("a + a"), owner, bind_probe_cb, &probe);
    TEST_ASSERT_EQUAL_INT(1, probe.calls);

    lv_subject_set_int(&g_subs[0], 4);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, probe.calls,
                                  "\"a + a\" registers one observer, so one change is one callback");
    TEST_ASSERT_EQUAL_INT32(8, probe.last);

    lv_xml_expr_unbind(h);
}

static void test_unbind_detaches_the_binding(void)
{
    subjects_reset();

    lv_obj_t * owner = lv_obj_create(helix_test_env_screen());
    bind_probe_t probe = {0, 0};

    lv_xml_expr_bind_t * h = lv_xml_expr_bind(must_compile("a"), owner, bind_probe_cb, &probe);
    lv_subject_set_int(&g_subs[0], 1);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT(2, probe.calls);

    lv_xml_expr_unbind(h);

    lv_subject_set_int(&g_subs[0], 2);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, probe.calls,
                                  "no callback may fire after unbind");

    /* The owner outliving the unbind must not re-trigger the freed hook. */
    lv_obj_delete(owner);
    helix_test_pump(30);
    TEST_ASSERT_EQUAL_INT(2, probe.calls);
}

static void test_unbind_is_null_safe(void)
{
    lv_xml_expr_unbind(NULL);
    /* Reaching here at all is the assertion; prove the world still works. */
    TEST_ASSERT_EQUAL_INT32(3, eval_literal("1 + 2"));
}

/**
 * Deleting the owner is the other half of the free-once design: the binding
 * detaches itself, and later subject changes must not reach the callback.
 */
static void test_deleting_the_owner_detaches_the_binding(void)
{
    subjects_reset();

    lv_obj_t * owner = lv_obj_create(helix_test_env_screen());
    bind_probe_t probe = {0, 0};

    lv_xml_expr_bind(must_compile("a"), owner, bind_probe_cb, &probe);
    TEST_ASSERT_EQUAL_INT(1, probe.calls);

    lv_obj_delete(owner);
    helix_test_pump(30);

    lv_subject_set_int(&g_subs[0], 12);
    helix_test_pump(30);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, probe.calls,
                                  "deleting the owner must detach every per-subject observer");
}

/**
 * bind() takes ownership of the expression, including when the expression is
 * NULL: it degenerates into a constant-0 binding rather than crashing.
 */
static void test_bind_of_a_null_expression_is_a_constant_zero(void)
{
    lv_obj_t * owner = lv_obj_create(helix_test_env_screen());
    bind_probe_t probe = {0, 0};

    lv_xml_expr_bind_t * h = lv_xml_expr_bind(NULL, owner, bind_probe_cb, &probe);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, probe.calls, "a NULL expr still fires the initial callback");
    TEST_ASSERT_EQUAL_INT32(0, probe.last);

    lv_xml_expr_unbind(h);
}

/**
 * Free-once, proved by heap accounting rather than by "it did not crash".
 *
 * bind() takes ownership of the expression and the binding frees it exactly
 * once, whether the teardown comes from unbind() or from the owner being
 * deleted. A repeated subject ("a + a") is the interesting input, because the
 * de-duplicated dependency list means one observer for two AST references - the
 * shape that would produce a double free if the free walked references instead
 * of distinct subjects.
 *
 * Neither failure mode is observable from the callback: a leak still fires
 * correctly and a double free only aborts under a sanitizer. lv_mem_monitor is,
 * so a cycle that does not return the heap to where the previous cycle left it
 * is the assertion. (A genuine double free aborts inside tlsf, which fails the
 * test by killing the runner - also a failure, just a louder one.)
 */
static void test_the_bind_lifecycle_returns_the_heap_to_steady_state(void)
{
    subjects_reset();

    size_t after_cycle[4];

    for(int i = 0; i < 4; i++) {
        /* Both teardown paths in one cycle: explicit unbind, and owner delete. */
        lv_obj_t * unbound_owner = lv_obj_create(helix_test_env_screen());
        bind_probe_t p1 = {0, 0};
        lv_xml_expr_bind_t * h = lv_xml_expr_bind(must_compile("a + a"), unbound_owner,
                                                  bind_probe_cb, &p1);
        TEST_ASSERT_EQUAL_INT(1, p1.calls);
        lv_xml_expr_unbind(h);
        lv_obj_delete(unbound_owner);

        lv_obj_t * deleted_owner = lv_obj_create(helix_test_env_screen());
        bind_probe_t p2 = {0, 0};
        lv_xml_expr_bind(must_compile("a + a"), deleted_owner, bind_probe_cb, &p2);
        TEST_ASSERT_EQUAL_INT(1, p2.calls);
        lv_obj_delete(deleted_owner);

        helix_test_pump(30);

        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        after_cycle[i] = mon.free_size;
    }

    /* Cycle 0 is a warm-up - the first pass through a size class carves blocks
     * the later ones reuse. Cycles 1..3 are steady state. */
    for(int i = 2; i < 4; i++) {
        TEST_ASSERT_EQUAL_size_t_MESSAGE(
            after_cycle[1], after_cycle[i],
            helix_xml_assert_msgf("cycle %d did not return the heap to where cycle 1 left it - "
                                  "a bind is leaking its expression or its observer list", i));
    }
}

/*===========================================================================
 * Inline `${expr}` splices in XML attributes
 *
 * The other consumer of this compiler. xml_compose_indexed() (lv_xml.c) splices
 * `${...}` tokens into an attribute value at parse time; a token that is a bare
 * identifier takes the legacy name-substitution path, and ANYTHING else is
 * handed to lv_xml_expr_compile with a resolver that maps `i` to the <repeat>
 * loop index, a numeric component param to a transient int subject, and
 * everything else to a real scope subject.
 *
 * The split of duty: tests/cases/test_indexed_subject.c owns the bare-token
 * path (`${i}`, `${grp}`, and their unresolvable cases). This section owns the
 * arithmetic path, which is the part that runs the expression engine.
 *
 * RESOLVE-ONCE is the contract that makes this different from `<subject_expr>`:
 * compile, eval and free all happen inside one compose call and no observer is
 * ever attached, so the spliced value is frozen at parse time.
 *
 * Every assertion below reads a style property the XML itself declared, never a
 * laid-out coordinate - see the rule at the top of helpers/xml_assert.h.
 *==========================================================================*/

/** Storage for the per-iteration string subjects the splice tests bind to. */
#define SPLICE_SLOTS 4
static lv_subject_t g_slot_subjects[SPLICE_SLOTS];
static char g_slot_bufs[SPLICE_SLOTS][32];

static void register_slot_string(uint32_t slot, const char * name, const char * value)
{
    lv_subject_init_string(&g_slot_subjects[slot], g_slot_bufs[slot], NULL,
                           sizeof(g_slot_bufs[slot]), value);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LV_RESULT_OK,
                                  (int)lv_xml_register_subject(NULL, name, &g_slot_subjects[slot]),
                                  helix_xml_assert_msgf("could not register subject '%s'", name));
}

/** Drop every observer off the static subjects before the env tears LVGL down. */
static void release_slots(uint32_t used)
{
    for(uint32_t i = 0; i < used; i++) lv_subject_deinit(&g_slot_subjects[i]);
}

static const char * SPLICE_NAME_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"sn_root\">"
    "    <lv_obj name=\"row\">"
    "      <repeat count=\"4\">"
    "        <lv_label bind_text=\"slot_${i + 1}_label\"/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Arithmetic over the loop index, spliced into a SUBJECT NAME: the 0-based `i`
 * becomes the 1-based numbering an app's subjects are usually declared with.
 * The four subjects carry four different strings, so a splice that dropped the
 * arithmetic (binding slot_0..slot_3) resolves nothing and every label keeps its
 * empty default; one that composed the name once lands all four on "one".
 */
static void test_an_expr_splice_composes_a_per_iteration_subject_name(void)
{
    register_slot_string(0, "slot_1_label", "one");
    register_slot_string(1, "slot_2_label", "two");
    register_slot_string(2, "slot_3_label", "three");
    register_slot_string(3, "slot_4_label", "four");

    ASSERT_XML_REGISTERS("expr_splice_name", SPLICE_NAME_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_name", NULL);
    helix_test_pump(30);

    lv_obj_t * row = ASSERT_NAMED(root, "row");
    ASSERT_CHILD_COUNT(row, 4);

    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 0), "one");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 1), "two");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 2), "three");
    ASSERT_LABEL_TEXT(lv_obj_get_child(row, 3), "four");

    release_slots(SPLICE_SLOTS);
}

static const char * SPLICE_NUM_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"snum_root\">"
    "    <lv_obj name=\"row\">"
    "      <repeat count=\"4\">"
    "        <lv_obj name=\"cell\" width=\"10\" height=\"10\" style_translate_x=\"${i * 84}\"/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * The same splice consumed as a NUMBER rather than as part of a name. The
 * multiplier is what makes it a real check: an implementation that spliced the
 * raw index would produce 0,1,2,3 and pass any assertion that only looked for
 * "distinct and increasing".
 */
static void test_an_expr_splice_computes_a_numeric_style_attribute(void)
{
    ASSERT_XML_REGISTERS("expr_splice_num", SPLICE_NUM_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_num", NULL);
    helix_test_pump(30);

    lv_obj_t * row = ASSERT_NAMED(root, "row");
    ASSERT_CHILD_COUNT(row, 4);

    ASSERT_STYLE_INT(lv_obj_get_child(row, 0), LV_STYLE_TRANSLATE_X, LV_PART_MAIN, 0);
    ASSERT_STYLE_INT(lv_obj_get_child(row, 1), LV_STYLE_TRANSLATE_X, LV_PART_MAIN, 84);
    ASSERT_STYLE_INT(lv_obj_get_child(row, 2), LV_STYLE_TRANSLATE_X, LV_PART_MAIN, 168);
    ASSERT_STYLE_INT(lv_obj_get_child(row, 3), LV_STYLE_TRANSLATE_X, LV_PART_MAIN, 252);
}

static const char * SPLICE_EQUIV_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"seq_root\">"
    "    <lv_obj name=\"row\">"
    "      <repeat count=\"3\">"
    "        <lv_obj name=\"bare\" width=\"5\" height=\"5\" style_translate_x=\"${i}\"/>"
    "        <lv_obj name=\"expr\" width=\"5\" height=\"5\" style_translate_x=\"${i + 0}\"/>"
    "      </repeat>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * `${i}` and `${i + 0}` take two DIFFERENT code paths - xml_token_is_bare_identifier()
 * sends the first to name substitution and the second to the expression
 * compiler - and must be indistinguishable in the result. This pins that
 * boundary: the loop index the expression resolver seeds is the same index the
 * legacy path formats, so adding arithmetic to an existing `${i}` cannot shift
 * a layout by one.
 */
static void test_a_bare_index_and_an_expression_over_it_agree(void)
{
    ASSERT_XML_REGISTERS("expr_splice_equiv", SPLICE_EQUIV_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_equiv", NULL);
    helix_test_pump(30);

    lv_obj_t * row = ASSERT_NAMED(root, "row");
    ASSERT_CHILD_COUNT(row, 6); /* two per iteration */

    for(int k = 0; k < 3; k++) {
        lv_style_value_t bare = lv_obj_get_style_prop(lv_obj_get_child(row, k * 2), LV_PART_MAIN,
                                                      LV_STYLE_TRANSLATE_X);
        lv_style_value_t expr = lv_obj_get_style_prop(lv_obj_get_child(row, k * 2 + 1), LV_PART_MAIN,
                                                     LV_STYLE_TRANSLATE_X);
        TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)bare.num, (int32_t)expr.num,
                                        helix_xml_assert_msgf(
                                            "${i} and ${i + 0} disagreed at iteration %d", k));
        TEST_ASSERT_EQUAL_INT32_MESSAGE(k, (int32_t)bare.num,
                                        "both spellings must yield the 0-based loop index");
    }
}

static const char * SPLICE_SUBJECT_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"ssubj_root\">"
    "    <lv_obj name=\"g\" width=\"${base * scale}\" height=\"10\"/>"
    "  </view>"
    "</component>";

/**
 * A `${expr}` over real subjects is evaluated ONCE, at parse time, and never
 * again - no observer is attached, so a later change to an operand does not
 * move the widget. That is the whole difference between this and
 * `<subject_expr>`, and it is easy to "fix" by accident into a live binding, so
 * the second half of this test is the load-bearing half.
 */
static void test_subject_operands_in_a_splice_are_resolved_once(void)
{
    subjects_reset();
    lv_subject_set_int(&g_subs[0], 10); /* base  */
    lv_subject_set_int(&g_subs[1], 3);  /* scale */
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "base", &g_subs[0]));
    TEST_ASSERT_EQUAL_INT(LV_RESULT_OK, (int)lv_xml_register_subject(NULL, "scale", &g_subs[1]));

    ASSERT_XML_REGISTERS("expr_splice_subj", SPLICE_SUBJECT_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_subj", NULL);
    helix_test_pump(30);

    lv_obj_t * g = ASSERT_NAMED(root, "g");
    ASSERT_STYLE_INT(g, LV_STYLE_WIDTH, LV_PART_MAIN, 30);

    lv_subject_set_int(&g_subs[0], 100); /* would be 300 if this were a binding */
    helix_test_pump(30);
    ASSERT_STYLE_INT(g, LV_STYLE_WIDTH, LV_PART_MAIN, 30);
}

static const char * SPLICE_PARAM_XML =
    "<component>"
    "  <api><prop name=\"cols\" type=\"string\"/></api>"
    "  <view extends=\"lv_obj\" name=\"sparam_root\">"
    "    <lv_obj name=\"g\" width=\"${cols * 2}\" height=\"10\"/>"
    "  </view>"
    "</component>";

/**
 * The resolver's second branch: a component param whose value parses as an
 * integer becomes a transient operand, so a caller can pass `cols="4"` and have
 * the component compute from it. Neither the loop index nor a registered
 * subject is involved.
 */
static void test_a_numeric_component_param_is_a_valid_splice_operand(void)
{
    ASSERT_XML_REGISTERS("expr_splice_param", SPLICE_PARAM_XML);

    const char * attrs[] = {"cols", "4", NULL, NULL};
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_param", attrs);
    helix_test_pump(30);

    ASSERT_STYLE_INT(ASSERT_NAMED(root, "g"), LV_STYLE_WIDTH, LV_PART_MAIN, 8);
}

static const char * SPLICE_BAD_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"sbad_root\">"
    "    <lv_obj name=\"unknown_ident\" width=\"5\" height=\"5\""
    "            style_translate_x=\"1${bogus_ident + 1}2\"/>"
    "    <lv_obj name=\"syntax_error\" width=\"5\" height=\"5\""
    "            style_translate_x=\"3${5 + }4\"/>"
    "  </view>"
    "</component>";

/**
 * A `${expr}` that will not compile splices the EMPTY STRING and the parse
 * carries on. Asserting only that the attribute came out 0 would prove nothing -
 * 0 is also what an absent attribute gives - so both cases here wrap the splice
 * in literal digits: "1${...}2" must come out as the number 12, which is only
 * true if exactly zero characters were spliced in the middle.
 *
 * The two inputs fail at different stages: an identifier no resolver knows
 * (compile fails on the operand) and a syntax error (compile fails on the
 * grammar). Both must warn, because the resulting widget tree is
 * indistinguishable from a correct one.
 */
static void test_an_uncompilable_splice_yields_an_empty_string_and_warns(void)
{
    ASSERT_XML_REGISTERS("expr_splice_bad", SPLICE_BAD_XML);

    log_capture_start();
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_bad", NULL);
    helix_test_pump(30);
    log_capture_stop();

    /* Both widgets were still created - a failed splice is not a failed parse. */
    lv_obj_t * unknown = ASSERT_NAMED(root, "unknown_ident");
    lv_obj_t * syntax = ASSERT_NAMED(root, "syntax_error");

    ASSERT_STYLE_INT(unknown, LV_STYLE_TRANSLATE_X, LV_PART_MAIN, 12);
    ASSERT_STYLE_INT(syntax, LV_STYLE_TRANSLATE_X, LV_PART_MAIN, 34);

    TEST_ASSERT_TRUE_MESSAGE(log_contains("${bogus_ident + 1} could not be evaluated"),
                             "an unresolvable operand must name the whole token it came from");
    TEST_ASSERT_TRUE_MESSAGE(log_contains("${5 + } could not be evaluated"),
                             "a malformed expression must warn too - the tree looks correct "
                             "either way");
}

static const char * SPLICE_PARAM_BAD_XML =
    "<component>"
    "  <api><prop name=\"grp\" type=\"string\"/></api>"
    "  <view extends=\"lv_obj\" name=\"spbad_root\">"
    "    <lv_obj name=\"g\" width=\"7${grp * 2}\" height=\"10\"/>"
    "  </view>"
    "</component>";

/**
 * A param that EXISTS but is not an integer falls past the transient-operand
 * branch to the subject lookup, finds nothing, and fails the compile. The
 * distinction matters: `cols="4"` and `grp="fan"` reach the resolver through
 * exactly the same call, and only the xml_str_is_integer() test separates them.
 *
 * Same empty-splice proof as above: "7${...}" must come out as 7.
 */
static void test_a_non_numeric_component_param_makes_the_splice_fail(void)
{
    ASSERT_XML_REGISTERS("expr_splice_param_bad", SPLICE_PARAM_BAD_XML);

    const char * attrs[] = {"grp", "fan", NULL, NULL};

    log_capture_start();
    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "expr_splice_param_bad", attrs);
    helix_test_pump(30);
    log_capture_stop();

    ASSERT_STYLE_INT(ASSERT_NAMED(root, "g"), LV_STYLE_WIDTH, LV_PART_MAIN, 7);
    TEST_ASSERT_TRUE_MESSAGE(log_contains("${grp * 2} could not be evaluated"),
                             "a non-numeric param in arithmetic must warn, not silently splice");
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

    /* tokenizer */
    RUN_TEST(test_tokenize_emits_eof_even_for_an_empty_source);
    RUN_TEST(test_tokenize_recognises_literals_identifiers_and_parens);
    RUN_TEST(test_tokenize_skips_all_four_whitespace_characters);
    RUN_TEST(test_tokenize_recognises_every_symbolic_operator);
    RUN_TEST(test_tokenize_folds_word_operators_to_symbol_kinds);
    RUN_TEST(test_tokenize_word_operators_are_case_sensitive);
    RUN_TEST(test_tokenize_reports_unknown_and_half_written_operators_as_errors);
    RUN_TEST(test_tokenize_writes_nothing_when_cap_is_zero);
    RUN_TEST(test_tokenize_returns_the_number_of_entries_it_wrote);

    /* compile */
    RUN_TEST(test_compile_accepts_subject_free_expressions_without_a_resolver);
    RUN_TEST(test_compile_resolves_identifiers_through_the_resolver);
    RUN_TEST(test_compile_rejects_null_and_empty_source_without_warning);
    RUN_TEST(test_compile_rejects_more_than_128_tokens);
    RUN_TEST(test_compile_rejects_any_bad_token);
    RUN_TEST(test_compile_rejects_unresolved_identifiers);
    RUN_TEST(test_compile_rejects_syntax_errors);
    RUN_TEST(test_compile_warns_when_it_truncates_a_long_identifier);

    /* eval */
    RUN_TEST(test_eval_computes_arithmetic);
    RUN_TEST(test_eval_computes_comparisons_as_one_or_zero);
    RUN_TEST(test_eval_normalises_boolean_results_to_one_or_zero);
    RUN_TEST(test_eval_applies_unary_minus_and_not_recursively);
    RUN_TEST(test_eval_returns_zero_for_a_null_expression);

    /* precedence */
    RUN_TEST(test_precedence_binds_multiplication_before_addition);
    RUN_TEST(test_precedence_orders_arithmetic_comparison_and_and_or);
    RUN_TEST(test_binary_operators_are_left_associative);
    RUN_TEST(test_chained_comparisons_fold_left_instead_of_being_rejected);
    RUN_TEST(test_unary_not_binds_tighter_than_comparison);

    /* word vs symbol forms */
    RUN_TEST(test_word_and_symbol_operators_are_equivalent);

    /* division by zero + short circuit */
    RUN_TEST(test_divide_and_modulo_by_zero_yield_zero_and_warn);
    RUN_TEST(test_boolean_operators_short_circuit);

    /* subject operands */
    RUN_TEST(test_eval_reads_subject_values_at_evaluation_time);
    RUN_TEST(test_subject_list_is_deduplicated_by_pointer_in_first_use_order);
    RUN_TEST(test_subject_accessors_are_null_safe_and_bounds_checked);
    RUN_TEST(test_more_than_32_distinct_subjects_are_dropped_from_the_dependency_list);

    /* reactive bind */
    RUN_TEST(test_bind_fires_once_immediately_with_the_current_value);
    RUN_TEST(test_bind_reevaluates_when_any_referenced_subject_changes);
    RUN_TEST(test_bind_fires_once_per_change_for_a_repeated_subject);
    RUN_TEST(test_unbind_detaches_the_binding);
    RUN_TEST(test_unbind_is_null_safe);
    RUN_TEST(test_deleting_the_owner_detaches_the_binding);
    RUN_TEST(test_bind_of_a_null_expression_is_a_constant_zero);
    RUN_TEST(test_the_bind_lifecycle_returns_the_heap_to_steady_state);

    /* inline ${expr} splices */
    RUN_TEST(test_an_expr_splice_composes_a_per_iteration_subject_name);
    RUN_TEST(test_an_expr_splice_computes_a_numeric_style_attribute);
    RUN_TEST(test_a_bare_index_and_an_expression_over_it_agree);
    RUN_TEST(test_subject_operands_in_a_splice_are_resolved_once);
    RUN_TEST(test_a_numeric_component_param_is_a_valid_splice_operand);
    RUN_TEST(test_an_uncompilable_splice_yields_an_empty_string_and_warns);
    RUN_TEST(test_a_non_numeric_component_param_makes_the_splice_fail);

    return UNITY_END();
}
