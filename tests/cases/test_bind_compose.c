/**
 * @file test_bind_compose.c
 *
 * Composition of the state and flag bindings in
 * src/xml/parsers/lv_xml_obj_parser.c, over src/xml/lv_xml_bind_compose.c.
 *
 * ---------------------------------------------------------------------------
 * THE RULE: BITS ARE THE OR OF EVERY BINDING THAT DRIVES THEM
 *
 * A widget may carry more than one reason to be disabled, or hidden. Each
 * binding evaluates only its own reason, and every binding on the same
 * state/flag of the same widget shares one applied result: the bits are set
 * while ANY binding holds them and cleared only once none does.
 *
 * What that buys is order independence. A binding that wrote the bits itself
 * would assert BOTH polarities on every fire, so the subject that notified most
 * recently would decide and a widget disabled for two reasons could come back
 * enabled the moment one of them cleared. Every test here therefore drives its
 * two reasons in BOTH orders and asserts the same outcome, because a test that
 * only ever raised them in one order would pass against exactly that bug.
 *
 * SCOPE: the composition, not the comparisons. That `_if_eq` matches on
 * equality and `_if_le` does not, that `invert` flips a cond, and that the
 * expression language evaluates correctly, are test_cond_binds.c's and
 * test_expr.c's territory. What is tested here is that two bindings reach one
 * widget property without cancelling each other, whichever family they come
 * from and whatever order they fire in.
 * ---------------------------------------------------------------------------
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

/** Assert @p obj is NOT in state @p s. xml_assert.h has ASSERT_STATE but no negative. */
#define ASSERT_NO_STATE(obj, s)                                                          \
    do {                                                                                 \
        lv_obj_t * hx_o_ = (lv_obj_t *)(obj);                                            \
        TEST_ASSERT_NOT_NULL_MESSAGE(hx_o_, "ASSERT_NO_STATE on a NULL object");         \
        TEST_ASSERT_FALSE_MESSAGE(                                                       \
            lv_obj_has_state(hx_o_, (s)),                                                \
            helix_xml_assert_msgf("\"%s\" is unexpectedly in state %s",                  \
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

/**
 * Raise both reasons, drop them one at a time, and assert the bits survive
 * until the last one lets go - once starting from @p a, once from @p b.
 *
 * @p check is called after every change with the outcome the widget should
 * show, so the caller decides how "the bits are set" is spelled for a state or
 * for a flag.
 */
static void assert_both_orders(lv_obj_t * box, lv_subject_t * a, lv_subject_t * b,
                               void (*check)(lv_obj_t * box, bool set))
{
    check(box, false);

    /* `a` first: `b` arriving later must not clear what `a` asserted, and `a`
     * releasing must not clear what `b` still asserts. */
    set_and_settle(a, 1);
    check(box, true);
    set_and_settle(b, 1);
    check(box, true);
    set_and_settle(a, 0);
    check(box, true);
    set_and_settle(b, 0);
    check(box, false);

    /* And the mirror image, which is the order that passes against a
     * last-writer-wins implementation. */
    set_and_settle(b, 1);
    check(box, true);
    set_and_settle(a, 1);
    check(box, true);
    set_and_settle(b, 0);
    check(box, true);
    set_and_settle(a, 0);
    check(box, false);
}

static void check_disabled(lv_obj_t * box, bool set)
{
    if(set) ASSERT_STATE(box, LV_STATE_DISABLED);
    else ASSERT_NO_STATE(box, LV_STATE_DISABLED);
}

static void check_hidden(lv_obj_t * box, bool set)
{
    if(set) ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
    else ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);
}

/*===========================================================================
 * Two bindings, one property
 *==========================================================================*/

static const char * TWO_STATE_EQ_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"job\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"op\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_state_if_eq subject=\"job\" state=\"disabled\" ref_value=\"1\"/>"
    "      <bind_state_if_eq subject=\"op\" state=\"disabled\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * The shape ui_xml/motion_panel.xml's QGL and Z-Tilt buttons carry: a job holds
 * the machine, or a controls operation is already running, and either alone must
 * disable the button.
 */
static void test_two_state_eq_binds_disable_when_either_reason_holds(void)
{
    ASSERT_XML_REGISTERS("bc_state_eq", TWO_STATE_EQ_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "bc_state_eq", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    assert_both_orders(box, scope_subject("bc_state_eq", "job"),
                       scope_subject("bc_state_eq", "op"), check_disabled);
}

static const char * TWO_FLAG_EQ_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"job\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"op\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_flag_if_eq subject=\"job\" flag=\"hidden\" ref_value=\"1\"/>"
    "      <bind_flag_if_eq subject=\"op\" flag=\"hidden\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

static void test_two_flag_eq_binds_hide_when_either_reason_holds(void)
{
    ASSERT_XML_REGISTERS("bc_flag_eq", TWO_FLAG_EQ_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "bc_flag_eq", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    assert_both_orders(box, scope_subject("bc_flag_eq", "job"),
                       scope_subject("bc_flag_eq", "op"), check_hidden);
}

static const char * MIXED_FAMILY_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"job\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"op\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_state_if cond=\"job\" state=\"disabled\"/>"
    "      <bind_state_if_eq subject=\"op\" state=\"disabled\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * The expression family and the subject/ref_value family run through different
 * observers, so composing them is a separate claim from composing two of a kind.
 * ui_xml/header_bar.xml's action button carries exactly this pair.
 */
static void test_a_cond_bind_and_an_eq_bind_compose_on_one_state(void)
{
    ASSERT_XML_REGISTERS("bc_mixed", MIXED_FAMILY_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "bc_mixed", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    assert_both_orders(box, scope_subject("bc_mixed", "job"),
                       scope_subject("bc_mixed", "op"), check_disabled);
}

/*===========================================================================
 * What composition must NOT reach
 *==========================================================================*/

static const char * TWO_PROPS_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"job\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"sel\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"gone\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"root\">"
    "    <lv_obj name=\"box\">"
    "      <bind_state_if_eq subject=\"job\" state=\"disabled\" ref_value=\"1\"/>"
    "      <bind_state_if_eq subject=\"sel\" state=\"checked\" ref_value=\"1\"/>"
    "      <bind_flag_if_eq subject=\"gone\" flag=\"hidden\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Composition is per property. Bindings on different states, and on a state
 * versus a flag, must stay independent - a shared group keyed on the object
 * alone would fold all three together and one reason would set all of them.
 */
static void test_bindings_on_different_properties_stay_independent(void)
{
    ASSERT_XML_REGISTERS("bc_two_props", TWO_PROPS_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "bc_two_props", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");

    lv_subject_t * job = scope_subject("bc_two_props", "job");
    lv_subject_t * sel = scope_subject("bc_two_props", "sel");
    lv_subject_t * gone = scope_subject("bc_two_props", "gone");

    set_and_settle(job, 1);
    ASSERT_STATE(box, LV_STATE_DISABLED);
    ASSERT_NO_STATE(box, LV_STATE_CHECKED);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(sel, 1);
    set_and_settle(job, 0);
    ASSERT_NO_STATE(box, LV_STATE_DISABLED);
    ASSERT_STATE(box, LV_STATE_CHECKED);
    ASSERT_NO_FLAG(box, LV_OBJ_FLAG_HIDDEN);

    set_and_settle(gone, 1);
    set_and_settle(sel, 0);
    ASSERT_NO_STATE(box, LV_STATE_CHECKED);
    ASSERT_FLAG(box, LV_OBJ_FLAG_HIDDEN);
}

static const char * SIBLINGS_XML =
    "<component>"
    "  <subjects>"
    "    <subject name=\"job\" type=\"int\" value=\"0\"/>"
    "    <subject name=\"op\" type=\"int\" value=\"0\"/>"
    "  </subjects>"
    "  <view extends=\"lv_obj\" name=\"root\">"
    "    <lv_obj name=\"first\">"
    "      <bind_state_if_eq subject=\"job\" state=\"disabled\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "    <lv_obj name=\"second\">"
    "      <bind_state_if_eq subject=\"op\" state=\"disabled\" ref_value=\"1\"/>"
    "    </lv_obj>"
    "  </view>"
    "</component>";

/**
 * Composition is per widget as well as per property: two widgets binding the
 * same state each keep their own result, so one staying disabled cannot pin the
 * other.
 */
static void test_bindings_on_different_widgets_stay_independent(void)
{
    ASSERT_XML_REGISTERS("bc_siblings", SIBLINGS_XML);

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "bc_siblings", NULL);
    helix_test_pump(30);
    lv_obj_t * first = ASSERT_NAMED(root, "first");
    lv_obj_t * second = ASSERT_NAMED(root, "second");

    lv_subject_t * job = scope_subject("bc_siblings", "job");
    lv_subject_t * op = scope_subject("bc_siblings", "op");

    set_and_settle(job, 1);
    ASSERT_STATE(first, LV_STATE_DISABLED);
    ASSERT_NO_STATE(second, LV_STATE_DISABLED);

    set_and_settle(op, 1);
    set_and_settle(job, 0);
    ASSERT_NO_STATE(first, LV_STATE_DISABLED);
    ASSERT_STATE(second, LV_STATE_DISABLED);
}

/*===========================================================================
 * Lifetime
 *==========================================================================*/

/**
 * A panel rebuild - hot reload, or navigating away and back - deletes the
 * widgets and builds the component again. The second instance must compose from
 * an empty slate: shares that survived the first would leave its bits asserted
 * by a widget that no longer exists.
 */
static void test_a_rebuilt_instance_composes_from_a_clean_slate(void)
{
    ASSERT_XML_REGISTERS("bc_rebuild", TWO_STATE_EQ_XML);

    lv_subject_t * job = scope_subject("bc_rebuild", "job");
    lv_subject_t * op = scope_subject("bc_rebuild", "op");

    lv_obj_t * root = XML_CREATE(helix_test_env_screen(), "bc_rebuild", NULL);
    helix_test_pump(30);
    set_and_settle(job, 1);
    set_and_settle(op, 1);
    ASSERT_STATE(ASSERT_NAMED(root, "box"), LV_STATE_DISABLED);

    lv_obj_delete(root);
    helix_test_pump(30);

    /* Both reasons are still raised, so the fresh instance must come up
     * disabled, and then let go once both drop. */
    root = XML_CREATE(helix_test_env_screen(), "bc_rebuild", NULL);
    helix_test_pump(30);
    lv_obj_t * box = ASSERT_NAMED(root, "box");
    ASSERT_STATE(box, LV_STATE_DISABLED);

    set_and_settle(job, 0);
    ASSERT_STATE(box, LV_STATE_DISABLED);
    set_and_settle(op, 0);
    ASSERT_NO_STATE(box, LV_STATE_DISABLED);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_two_state_eq_binds_disable_when_either_reason_holds);
    RUN_TEST(test_two_flag_eq_binds_hide_when_either_reason_holds);
    RUN_TEST(test_a_cond_bind_and_an_eq_bind_compose_on_one_state);

    RUN_TEST(test_bindings_on_different_properties_stay_independent);
    RUN_TEST(test_bindings_on_different_widgets_stay_independent);

    RUN_TEST(test_a_rebuilt_instance_composes_from_a_clean_slate);

    return UNITY_END();
}
