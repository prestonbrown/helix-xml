/**
 * @file test_responsive_grid.c
 *
 * `<responsive_grid>`: lv_xml_grid_fit_columns() picks the columns, and the
 * widget's attributes feed it.
 *
 * Per xml_assert.h, nothing here asserts placed pixel geometry. The column
 * rule is a pure function over numbers the test sets, and the widget is checked
 * through lv_xml_responsive_grid_get_fit() on a grid whose width, gap and
 * children the XML declared. Where the children land on screen is the
 * application's to test against a real display config.
 *
 * SPDX-License-Identifier: MIT
 */

#include "helpers/helix_test_env.h"
#include "helpers/helix_test_pump.h"
#include "helpers/xml_assert.h"

#include "../../src/xml/parsers/lv_xml_responsive_grid_parser.h"

void setUp(void)
{
    helix_test_env_setup();
}

void tearDown(void)
{
    helix_test_env_teardown();
}

#define ASSERT_FIT(expected_cols, expected_w, fit)                                   \
    do {                                                                             \
        lv_xml_grid_fit_t f_ = (fit);                                                \
        TEST_ASSERT_EQUAL_INT_MESSAGE((expected_cols), f_.cols, "column count");     \
        TEST_ASSERT_EQUAL_INT_MESSAGE((expected_w), f_.col_w, "column width");       \
    } while(0)

/*---------------------------------------------------------------------------
 * The column rule
 *--------------------------------------------------------------------------*/

static void test_fits_as_many_min_width_columns_as_the_width_holds(void)
{
    lv_xml_grid_fit_input_t in = {.avail_w = 500, .gap = 10, .min_col_w = 150};
    /* Three columns need 3*150 + 2*10 = 470; four need 630. */
    ASSERT_FIT(3, 160, lv_xml_grid_fit_columns(&in));

    in.avail_w = 470;
    ASSERT_FIT(3, 150, lv_xml_grid_fit_columns(&in));
    in.avail_w = 469;
    ASSERT_FIT(2, 229, lv_xml_grid_fit_columns(&in));
}

static void test_never_drops_below_one_column(void)
{
    lv_xml_grid_fit_input_t in = {.avail_w = 100, .gap = 10, .min_col_w = 150};
    ASSERT_FIT(1, 100, lv_xml_grid_fit_columns(&in));
    in.avail_w = 0;
    ASSERT_FIT(1, 0, lv_xml_grid_fit_columns(&in));
    in.avail_w = -20;
    ASSERT_FIT(1, 0, lv_xml_grid_fit_columns(&in));

    /* No minimum: one column the whole width. */
    in.avail_w = 800;
    in.min_col_w = 0;
    ASSERT_FIT(1, 800, lv_xml_grid_fit_columns(&in));
}

static void test_max_cols_caps_the_count(void)
{
    lv_xml_grid_fit_input_t in = {.avail_w = 1000, .gap = 10, .min_col_w = 100, .max_cols = 4};
    ASSERT_FIT(4, 242, lv_xml_grid_fit_columns(&in));
}

static void test_fit_items_caps_at_the_item_count(void)
{
    lv_xml_grid_fit_input_t in = {.avail_w = 1000, .gap = 10, .min_col_w = 100, .item_count = 2};
    /* Without fit_items the count does nothing. */
    ASSERT_FIT(9, 102, lv_xml_grid_fit_columns(&in));

    in.fit_items = true;
    ASSERT_FIT(2, 495, lv_xml_grid_fit_columns(&in));

    /* An empty grid keeps the width's count rather than collapsing to zero. */
    in.item_count = 0;
    ASSERT_FIT(9, 102, lv_xml_grid_fit_columns(&in));
}

static void test_max_col_w_narrows_the_cells_not_the_count(void)
{
    lv_xml_grid_fit_input_t in = {.avail_w = 1000, .gap = 10, .min_col_w = 80};
    ASSERT_FIT(11, 81, lv_xml_grid_fit_columns(&in));
    in.max_col_w = 70;
    ASSERT_FIT(11, 70, lv_xml_grid_fit_columns(&in));
}

/*---------------------------------------------------------------------------
 * The widget
 *--------------------------------------------------------------------------*/

static const char * GRID_XML =
    "<component>"
    "  <view extends=\"lv_obj\" name=\"grid_host\" width=\"600\" height=\"content\" style_pad_all=\"0\">"
    "    <responsive_grid name=\"grid\" width=\"500\" min_col_width=\"150\" max_cols=\"2\""
    "                     style_pad_column=\"10\">"
    "      <lv_obj name=\"a\"/>"
    "      <lv_obj name=\"b\"/>"
    "      <lv_obj name=\"c\"/>"
    "    </responsive_grid>"
    "  </view>"
    "</component>";

static lv_obj_t * create_grid(void)
{
    ASSERT_XML_REGISTERS("grid_card", GRID_XML);
    lv_obj_t * host = lv_xml_create(helix_test_env_screen(), "grid_card", NULL);
    TEST_ASSERT_NOT_NULL(host);
    lv_obj_t * grid = ASSERT_NAMED(host, "grid");
    lv_obj_update_layout(host);
    return grid;
}

static void test_attributes_feed_the_fit(void)
{
    lv_obj_t * grid = create_grid();
    /* min 150 fits three in 500, max_cols takes it to two of (500-10)/2. */
    ASSERT_FIT(2, 245, lv_xml_responsive_grid_get_fit(grid));
}

static void test_is_a_bare_box_laid_out_by_its_own_layout(void)
{
    lv_obj_t * grid = create_grid();
    TEST_ASSERT_FALSE_MESSAGE(lv_obj_has_flag(grid, LV_OBJ_FLAG_SCROLLABLE), "grid scrolls by default");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lv_obj_get_style_pad_left(grid, LV_PART_MAIN), "grid has padding by default");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(LV_LAYOUT_NONE, lv_obj_get_style_layout(grid, LV_PART_MAIN), "no layout");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(LV_LAYOUT_FLEX, lv_obj_get_style_layout(grid, LV_PART_MAIN), "flex layout");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(LV_LAYOUT_GRID, lv_obj_get_style_layout(grid, LV_PART_MAIN), "grid layout");
}

static void test_fit_items_counts_only_visible_children(void)
{
    static const char * xml =
        "<component>"
        "  <view extends=\"lv_obj\" style_pad_all=\"0\" width=\"600\" height=\"content\">"
        "    <responsive_grid name=\"grid\" width=\"500\" min_col_width=\"100\" fit_items=\"true\""
        "                     style_pad_column=\"10\">"
        "      <lv_obj name=\"a\"/>"
        "      <lv_obj name=\"b\" hidden=\"true\"/>"
        "    </responsive_grid>"
        "  </view>"
        "</component>";
    ASSERT_XML_REGISTERS("fit_card", xml);
    lv_obj_t * host = lv_xml_create(helix_test_env_screen(), "fit_card", NULL);
    TEST_ASSERT_NOT_NULL(host);
    lv_obj_t * grid = ASSERT_NAMED(host, "grid");
    lv_obj_update_layout(host);
    ASSERT_FIT(1, 500, lv_xml_responsive_grid_get_fit(grid));

    lv_obj_remove_flag(ASSERT_NAMED(host, "b"), LV_OBJ_FLAG_HIDDEN);
    ASSERT_FIT(2, 245, lv_xml_responsive_grid_get_fit(grid));
}

static void test_get_fit_on_a_plain_object_is_one_full_column(void)
{
    lv_obj_t * plain = lv_obj_create(helix_test_env_screen());
    lv_obj_set_width(plain, 300);
    lv_obj_update_layout(plain);
    /* The theme's border and padding decide the content width; compare against it. */
    ASSERT_FIT(1, lv_obj_get_content_width(plain), lv_xml_responsive_grid_get_fit(plain));
    ASSERT_FIT(1, 0, lv_xml_responsive_grid_get_fit(NULL));
}

/** Deleting the grid frees its parameters; under a sanitizer this is the leak/UAF check. */
static void test_delete_frees_cleanly(void)
{
    lv_obj_t * grid = create_grid();
    lv_obj_delete(grid);
    helix_test_pump(10);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_fits_as_many_min_width_columns_as_the_width_holds);
    RUN_TEST(test_never_drops_below_one_column);
    RUN_TEST(test_max_cols_caps_the_count);
    RUN_TEST(test_fit_items_caps_at_the_item_count);
    RUN_TEST(test_max_col_w_narrows_the_cells_not_the_count);
    RUN_TEST(test_attributes_feed_the_fit);
    RUN_TEST(test_is_a_bare_box_laid_out_by_its_own_layout);
    RUN_TEST(test_fit_items_counts_only_visible_children);
    RUN_TEST(test_get_fit_on_a_plain_object_is_one_full_column);
    RUN_TEST(test_delete_frees_cleanly);

    return UNITY_END();
}
