/**
 * @file test_image_parser.c
 *
 * src/xml/parsers/lv_xml_image_parser.c: the `inner_align` keywords of
 * `<lv_image>`. image_align_to_enum() is static, so every keyword is read back
 * through the widget it was applied to.
 *
 * SPDX-License-Identifier: MIT
 */

#include "helpers/helix_log_capture.h"
#include "helpers/helix_test_env.h"
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
 * inner_align
 *--------------------------------------------------------------------------*/

typedef struct {
    const char * keyword;
    lv_image_align_t align;
} image_align_case_t;

static const image_align_case_t IMAGE_ALIGN_CASES[] = {
    {"top_left", LV_IMAGE_ALIGN_TOP_LEFT},
    {"top_mid", LV_IMAGE_ALIGN_TOP_MID},
    {"top_right", LV_IMAGE_ALIGN_TOP_RIGHT},
    {"bottom_left", LV_IMAGE_ALIGN_BOTTOM_LEFT},
    {"bottom_mid", LV_IMAGE_ALIGN_BOTTOM_MID},
    {"bottom_right", LV_IMAGE_ALIGN_BOTTOM_RIGHT},
    {"right_mid", LV_IMAGE_ALIGN_RIGHT_MID},
    {"left_mid", LV_IMAGE_ALIGN_LEFT_MID},
    {"center", LV_IMAGE_ALIGN_CENTER},
    {"stretch", LV_IMAGE_ALIGN_STRETCH},
    {"tile", LV_IMAGE_ALIGN_TILE},
    {"contain", LV_IMAGE_ALIGN_CONTAIN},
    {"contain_downscale", LV_IMAGE_ALIGN_CONTAIN_DOWNSCALE},
    {"cover", LV_IMAGE_ALIGN_COVER},
};

static void test_inner_align_accepts_every_keyword(void)
{
    for(size_t i = 0; i < sizeof(IMAGE_ALIGN_CASES) / sizeof(IMAGE_ALIGN_CASES[0]); i++) {
        const image_align_case_t * c = &IMAGE_ALIGN_CASES[i];
        const char * attrs[] = {"inner_align", c->keyword, NULL, NULL};

        log_capture_start();
        lv_obj_t * image = XML_CREATE(helix_test_env_screen(), "lv_image", attrs);
        log_capture_stop();

        TEST_ASSERT_EQUAL_INT32_MESSAGE(c->align, lv_image_get_inner_align(image),
                                        helix_xml_assert_msgf("inner_align=\"%s\"", c->keyword));
        TEST_ASSERT_FALSE_MESSAGE(log_contains("unknown value for image align"),
                                  helix_xml_assert_msgf("inner_align=\"%s\" warned", c->keyword));
        lv_obj_delete(image);
    }
}

static void test_inner_align_warns_on_an_unknown_keyword(void)
{
    const char * attrs[] = {"inner_align", "fit", NULL, NULL};

    log_capture_start();
    lv_obj_t * image = XML_CREATE(helix_test_env_screen(), "lv_image", attrs);
    log_capture_stop();

    TEST_ASSERT_EQUAL_INT32(0, lv_image_get_inner_align(image));
    TEST_ASSERT_TRUE(log_contains("fit is an unknown value for image align"));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_inner_align_accepts_every_keyword);
    RUN_TEST(test_inner_align_warns_on_an_unknown_keyword);

    return UNITY_END();
}
