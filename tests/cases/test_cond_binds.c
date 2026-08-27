/**
 * @file test_cond_binds.c
 *
 * The three expression-driven conditional bindings in
 * src/xml/parsers/lv_xml_obj_parser.c:
 *
 *   <bind_flag_if  cond="EXPR" flag="hidden"   invert="true|false"/>
 *   <bind_state_if cond="EXPR" state="disabled" invert="true|false"/>
 *   <bind_style_if cond="EXPR" name="STYLE"     invert="true|false"/>
 *
 * ---------------------------------------------------------------------------
 * SCOPE: THE BINDING, NOT THE EXPRESSION LANGUAGE
 *
 * The tokenizer, the precedence table, short-circuiting, subject resolution and
 * the bind/unbind lifetime all belong to lv_xml_expr.c and are covered in
 * test_expr.c. Nothing here re-tests them. What IS tested here is the layer on
 * top: that the XML element wires a compiled cond to the right widget property,
 * that a truthy result applies the flag/state/style and a falsy one removes it,
 * and that `invert` flips exactly that mapping and nothing else.
 *
 * `invert` is why each binding gets two tests rather than one. Its whole reason
 * for existing is markup like `flag="hidden" invert="true"`, i.e. "SHOW when
 * cond" - and an implementation that ignored the attribute would still pass a
 * test that only ever checked the non-inverted direction against a cond that
 * happens to start false. So both directions are driven through a full
 * false -> true -> false cycle, and the inverted and non-inverted cases assert
 * opposite results from the same subject values.
 *
 * <bind_style_if>'s style plumbing (the `parts=` list, selectors, the cascade
 * against inline attributes) is test_style.c's territory; this file only proves
 * that cond drives lv_obj_style_set_disabled() in the right direction.
 * ---------------------------------------------------------------------------
 *
 * The instances are deliberately NOT unregistered inside the test bodies.
 * The subjects these bindings observe live in the component scope, so the
 * widget must die before the scope does; helix_test_env_teardown() already
 * does exactly that (lv_obj_clean(screen), then lv_xml_deinit()). Unregistering
 * the component while its instance is still on the screen would free the
 * subjects out from under live observers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"


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
 * Local helpers
 *--------------------------------------------------------------------------*/

/**
 * Assert an integer style property does NOT hold @p unexpected.
 *
 * A disabled style falls back to whatever the theme provides, and that value is
 * a property of tests/lv_conf.h rather than of the engine - so the negative
 * assertion is "not the value the XML declared", never "equal to some other
 * number". Same rule as test_style.c, where this macro also lives; the two
 * files are separate executables with nothing to share it through.
 */
#define ASSERT_STYLE_INT_NOT(obj, prop, selector, unexpected)                            \
    do {                                                                                 \
        lv_obj_t * hx_o_ = (lv_obj_t *)(obj);                                            \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_o_, "ASSERT_STYLE_INT_NOT on a NULL object");    \
        lv_style_value_t hx_v_ = lv_obj_get_style_prop(hx_o_, (selector), (prop));       \
        TEST_ASSERT_TRUE_MESSAGE(                                                        \
            (int32_t)(unexpected) != (int32_t)hx_v_.num,                                 \
            helix_xml_assert_msgf("%s on \"%s\" still holds the value %d",                \
                                  #prop, helix_xml_assert_name_of(hx_o_),                \
                                  (int)(int32_t)(unexpected)));                          \
    } while(0)

/** Assert @p obj is NOT in state @p s. xml_assert.h has ASSERT_STATE but no negative. */
#define ASSERT_NO_STATE(obj, s)                                                          \
    do {                                                                                 \
        lv_obj_t * hx_o_ = (lv_obj_t *)(obj);                                            \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_o_, "ASSERT_NO_STATE on a NULL object");         \
        TEST_ASSERT_FALSE_MESSAGE(                                                       \
            lv_obj_has_state(hx_o_, (s)),                                                \
            helix_xml_assert_msgf("\"%s\" is unexpectedly in state %s",                   \
                                  helix_xml_assert_name_of(hx_o_), #s));                 \
    } while(0)

/** Fetch a component-scoped subject by name, asserting it exists. */
static lv_subject_t * scope_subject(const char * component, const char * subject)
{
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope(component);
    TEST_ASSERT_NOT_NULL_MESSAGE(scope, helix_xml_assert_msgf("no scope for '%s'", component));
    lv_subject_t * s = lv_xml_get_subject(scope, subject);
    TEST_ASSERT_NOT_NULL_MESSAGE(s, helix_xml_assert_msgf("no subject '%s' in '%s'",
                                                          subject, component));
    return s;
}

/** Change a subject and let the deferred side of the reactive path settle. */
static void set_and_settle(lv_subject_t * s, int32_t v)
{
    lv_subject_set_int(s, v);
    helix_test_pump(30);
}

/*===========================================================================
 * bind_flag_if
 *==========================================================================*/

/* Two inputs, so the test can prove the binding reacts to EITHER of them and
 * not just to whichever one it happened to observe first. */
static const char * FLAG_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"err\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"tmp\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"flag_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_flag_if cond=\"err or tmp gt 100\" flag=\"hidden\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_bind_flag_if_adds_the_flag_when_the_condition_is_true(void)
{
    ASSERT_XML_REGISTERS("cb_flag", FLAG_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_flag", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * err = scope_subject("cb_flag", "err");
    lv_subject_t * tmp = scope_subject("cb_flag", "tmp");

    /* cond is false at create time, so the flag must not be there. */
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(err, 1);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    /* Falling back to false must REMOVE it again - a binding that only ever
     * added the flag would pass every assertion up to this one. */
    set_and_settle(err, 0);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    /* The second operand drives it too. */
    set_and_settle(tmp, 150);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(tmp, 0);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
}

static const char * FLAG_INVERT_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"err\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"tmp\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"flag_inv_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_flag_if cond=\"err or tmp gt 100\" flag=\"hidden\" invert=\"true\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Same cond, same subject values, opposite results throughout - which is what
 * makes this more than a copy of the test above. `invert="true"` means "hide
 * unless cond", the shape almost every real `flag="hidden"` binding wants.
 */
static void test_bind_flag_if_with_invert_adds_the_flag_when_the_condition_is_false(void)
{
    ASSERT_XML_REGISTERS("cb_flag_inv", FLAG_INVERT_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_flag_inv", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * err = scope_subject("cb_flag_inv", "err");
    lv_subject_t * tmp = scope_subject("cb_flag_inv", "tmp");

    /* cond false at create time -> inverted -> flag APPLIED. */
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(err, 1);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(err, 0);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(tmp, 150);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
}

/*===========================================================================
 * bind_state_if
 *==========================================================================*/

static const char * STATE_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"busy\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"state_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_state_if cond=\"busy\" state=\"disabled\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_bind_state_if_adds_the_state_when_the_condition_is_true(void)
{
    ASSERT_XML_REGISTERS("cb_state", STATE_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_state", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * busy = scope_subject("cb_state", "busy");

    ASSERT_NO_STATE(box, LV_STATE_DISABLED);

    set_and_settle(busy, 1);
    ASSERT_STATE(box, LV_STATE_DISABLED);

    set_and_settle(busy, 0);
    ASSERT_NO_STATE(box, LV_STATE_DISABLED);
}

static const char * STATE_INVERT_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"busy\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"state_inv_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_state_if cond=\"busy\" state=\"disabled\" invert=\"true\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_bind_state_if_with_invert_adds_the_state_when_the_condition_is_false(void)
{
    ASSERT_XML_REGISTERS("cb_state_inv", STATE_INVERT_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_state_inv", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * busy = scope_subject("cb_state_inv", "busy");

    /* cond false at create time -> inverted -> state APPLIED. */
    ASSERT_STATE(box, LV_STATE_DISABLED);

    set_and_settle(busy, 1);
    ASSERT_NO_STATE(box, LV_STATE_DISABLED);

    set_and_settle(busy, 0);
    ASSERT_STATE(box, LV_STATE_DISABLED);
}

/*===========================================================================
 * bind_style_if
 *
 * The style is ADDED unconditionally at parse time and then enabled/disabled by
 * the cond (lv_obj_style_set_disabled), so "off" means the declared property
 * value must not be readable off the widget. `radius` is used rather than a
 * colour because it reads back through lv_obj_get_style_prop as a plain int,
 * and 41 is a value no theme in tests/lv_conf.h produces by accident.
 *==========================================================================*/

static const char * STYLE_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"active\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <styles>"
    "    <style name=\"hot\" radius=\"41\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"style_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_style_if cond=\"active\" name=\"hot\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_bind_style_if_enables_the_style_when_the_condition_is_true(void)
{
    ASSERT_XML_REGISTERS("cb_style", STYLE_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_style", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * active = scope_subject("cb_style", "active");

    /* cond false at create time -> style added but disabled. */
    ASSERT_STYLE_INT_NOT(box, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    set_and_settle(active, 1);
    ASSERT_STYLE_INT(box, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    set_and_settle(active, 0);
    ASSERT_STYLE_INT_NOT(box, LV_STYLE_RADIUS, LV_PART_MAIN, 41);
}

static const char * STYLE_INVERT_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"active\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <styles>"
    "    <style name=\"hot\" radius=\"41\"/>"
    "  </styles>"
    "  <view extends=\"lv_obj\" name=\"style_inv_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_style_if cond=\"active\" name=\"hot\" invert=\"true\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_bind_style_if_with_invert_enables_the_style_when_the_condition_is_false(void)
{
    ASSERT_XML_REGISTERS("cb_style_inv", STYLE_INVERT_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_style_inv", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * active = scope_subject("cb_style_inv", "active");

    /* cond false at create time -> inverted -> style ENABLED. */
    ASSERT_STYLE_INT(box, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    set_and_settle(active, 1);
    ASSERT_STYLE_INT_NOT(box, LV_STYLE_RADIUS, LV_PART_MAIN, 41);

    set_and_settle(active, 0);
    ASSERT_STYLE_INT(box, LV_STYLE_RADIUS, LV_PART_MAIN, 41);
}

/*---------------------------------------------------------------------------
 * main
 *--------------------------------------------------------------------------*/

static const char * TWO_WAY_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"can\" type=\"int\" value=\"1\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"two_way_root\">"
    "    <lv_obj name=\"box\" hidden=\"true\">"
    "      <bind_flag_if_eq subject=\"can\" flag=\"hidden\" ref_value=\"0\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * The flag binds are TWO-WAY, and that is the half people do not expect.
 *
 * Read as English, `subject="can" flag="hidden" ref_value="0"` sounds like
 * "hide when can is 0" - a one-way rule that abstains otherwise. It is not: a
 * non-match REMOVES the flag. Here the markup even asks for `hidden="true"` up
 * front and the binding takes it straight back off, because `can` is 1.
 *
 * Worth its own test because every existing case in this file drives the
 * subject through both states and so cannot tell "removes on non-match" from
 * "never added in the first place". This one starts from a flag that is
 * already set by something else.
 */
static void test_a_non_matching_flag_bind_removes_the_flag_rather_than_abstaining(void)
{
    ASSERT_XML_REGISTERS("cb_two_way", TWO_WAY_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_two_way", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    /* hidden="true" in the markup, `can` is 1, so ref_value does NOT match -
     * and the bind actively clears the flag the attribute asked for. */
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    lv_subject_t * can = scope_subject("cb_two_way", "can");

    set_and_settle(can, 0);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(can, 1);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
}

static const char * TWO_BINDS_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"can\" type=\"int\" value=\"1\"/>"
    "    <subject name=\"dirty\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"two_binds_root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_flag_if cond=\"dirty\" flag=\"hidden\" invert=\"true\"/>"
    "      <bind_flag_if_eq subject=\"can\" flag=\"hidden\" ref_value=\"0\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Two binds for ONE flag on ONE widget do not AND - each asserts both outcomes,
 * so they overwrite each other and the last to run wins.
 *
 * The markup below reads as "show when dirty, and never when can is 0", which
 * is what someone writing it means. What it does is show the widget with
 * `dirty` at 0, because the second bind sees can != 0 and clears the flag the
 * first one just set.
 *
 * Pinned rather than fixed: each binding is individually correct and
 * independent, and making them compose would mean flag ownership shared across
 * bindings. The fix at the call site is one expression - `cond="can and dirty"`
 * - and the engine's job is to make this behaviour predictable, not silent.
 */
static void test_two_flag_binds_for_one_flag_do_not_and_together(void)
{
    ASSERT_XML_REGISTERS("cb_two_binds", TWO_BINDS_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "cb_two_binds", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    /* dirty=0 => the inverted cond bind wants HIDDEN. can=1 => the eq bind
     * wants it shown. If these ANDed, hidden would stand. It does not. */
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    lv_subject_t * can = scope_subject("cb_two_binds", "can");
    lv_subject_t * dirty = scope_subject("cb_two_binds", "dirty");

    /* Drive the eq bind to its matching value and it applies the flag, whatever
     * the cond bind wants - dirty=1 alone would mean "show". */
    set_and_settle(dirty, 1);
    set_and_settle(can, 0);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    /* And back: the eq bind clears it again even though nothing about `dirty`
     * changed. Whichever bind ran last owns the flag. */
    set_and_settle(can, 1);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bind_flag_if_adds_the_flag_when_the_condition_is_true);
    RUN_TEST(test_bind_flag_if_with_invert_adds_the_flag_when_the_condition_is_false);

    RUN_TEST(test_bind_state_if_adds_the_state_when_the_condition_is_true);
    RUN_TEST(test_bind_state_if_with_invert_adds_the_state_when_the_condition_is_false);

    RUN_TEST(test_bind_style_if_enables_the_style_when_the_condition_is_true);
    RUN_TEST(test_bind_style_if_with_invert_enables_the_style_when_the_condition_is_false);

    RUN_TEST(test_a_non_matching_flag_bind_removes_the_flag_rather_than_abstaining);
    RUN_TEST(test_two_flag_binds_for_one_flag_do_not_and_together);

    return UNITY_END();
}
