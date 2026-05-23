/**
 * @file test_encoder.c
 * Tests for the protocol encoder. No LVGL/lws deps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"

#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: %s (line %d) — %s\n", msg, __LINE__, #cond); \
        return 1;                                                        \
    }                                                                    \
} while (0)

static int test_frame_header_layout(void)
{
    uint8_t buf[1024];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0x1234, 800, 480);
    lhc_enc_end_frame(&e);
    size_t n = lhc_enc_finalize(&e);
    /* header(4) + BEGIN_FRAME(4+4) + END_FRAME(4+0) = 16 */
    CHECK(n == 16, "frame size");
    /* frame_id LE */
    CHECK(buf[0] == 0x34 && buf[1] == 0x12, "frame_id LE");
    /* cmd_count = 2 (BEGIN + END) */
    CHECK(buf[2] == 2 && buf[3] == 0, "cmd_count LE");
    /* first cmd opcode */
    CHECK(buf[4] == LHC_OP_BEGIN_FRAME, "BEGIN opcode");
    CHECK(buf[5] == 0, "BEGIN flags");
    CHECK(buf[6] == 4 && buf[7] == 0, "BEGIN payload_len");
    /* w=800 → 0x320 → 0x20 0x03; h=480 → 0x1E0 → 0xE0 0x01 */
    CHECK(buf[8] == 0x20 && buf[9] == 0x03, "BEGIN w LE");
    CHECK(buf[10] == 0xE0 && buf[11] == 0x01, "BEGIN h LE");
    /* END_FRAME at offset 12 */
    CHECK(buf[12] == LHC_OP_END_FRAME, "END opcode");
    CHECK(buf[14] == 0 && buf[15] == 0, "END payload_len=0");
    return 0;
}

static int test_fill_rect_payload(void)
{
    uint8_t buf[1024];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0, 100, 100);
    lhc_enc_fill_rect(&e, 10, 20, 30, 40, 0xFFAABBCCu, 5);
    lhc_enc_end_frame(&e);
    size_t n = lhc_enc_finalize(&e);
    CHECK(n > 0, "no overflow");
    CHECK(e.cmd_count == 3, "3 cmds");

    /* FILL_RECT starts at: 4(hdr) + (4+4)(BEGIN) = 12 */
    const uint8_t *p = buf + 12;
    CHECK(p[0] == LHC_OP_FILL_RECT, "FILL_RECT opcode");
    CHECK(p[1] == 0, "FILL_RECT no-alpha flag (argb=0xFF*) → flag bit0 clear");
    CHECK(p[2] == 13 && p[3] == 0, "FILL_RECT payload_len=13");
    /* x=10 i16 LE */
    CHECK(p[4] == 10 && p[5] == 0, "x LE");
    CHECK(p[6] == 20 && p[7] == 0, "y LE");
    CHECK(p[8] == 30 && p[9] == 0, "w LE");
    CHECK(p[10] == 40 && p[11] == 0, "h LE");
    /* argb 0xFFAABBCC → CC BB AA FF */
    CHECK(p[12] == 0xCC && p[13] == 0xBB && p[14] == 0xAA && p[15] == 0xFF, "argb LE");
    CHECK(p[16] == 5, "radius");
    return 0;
}

static int test_alpha_flag(void)
{
    uint8_t buf[256];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0, 10, 10);
    /* argb with alpha 0x80 → flag bit0 set */
    lhc_enc_fill_rect(&e, 0, 0, 1, 1, 0x80FF0000u, 0);
    lhc_enc_end_frame(&e);
    CHECK(lhc_enc_finalize(&e) > 0, "no overflow");
    const uint8_t *p = buf + 12;
    CHECK(p[0] == LHC_OP_FILL_RECT, "opcode");
    CHECK((p[1] & LHC_FLAG_HAS_ALPHA_HINT) != 0, "alpha hint set");
    return 0;
}

static int test_overflow_safe(void)
{
    uint8_t buf[16];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0, 1, 1);
    /* This will overflow. */
    lhc_enc_fill_rect(&e, 0, 0, 1, 1, 0xFF000000u, 0);
    lhc_enc_end_frame(&e);
    CHECK(e.overflow, "overflow flagged");
    CHECK(lhc_enc_finalize(&e) == 0, "finalize returns 0 on overflow");
    return 0;
}

static int test_multiple_fills(void)
{
    uint8_t buf[4096];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 7, 800, 480);
    for (int i = 0; i < 10; i++) {
        lhc_enc_fill_rect(&e, (int16_t)(i * 10), 0, 8, 8, 0xFF112233u, 0);
    }
    lhc_enc_end_frame(&e);
    size_t n = lhc_enc_finalize(&e);
    CHECK(n > 0, "no overflow");
    /* BEGIN + 10 FILL + END = 12 cmds */
    CHECK(e.cmd_count == 12, "12 cmds total");
    CHECK(buf[2] == 12 && buf[3] == 0, "cmd_count patched into header");
    /* frame_id = 7 LE */
    CHECK(buf[0] == 7 && buf[1] == 0, "frame_id");
    return 0;
}

static int test_reinit_resets_state(void)
{
    uint8_t buf[256];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 1, 10, 10);
    lhc_enc_fill_rect(&e, 0, 0, 1, 1, 0xFF000000u, 0);
    lhc_enc_end_frame(&e);
    lhc_enc_finalize(&e);

    /* re-init must zero pos/cmd_count/overflow/frame_open */
    lhc_enc_init(&e, buf, sizeof(buf));
    CHECK(e.pos == 0, "pos reset");
    CHECK(e.cmd_count == 0, "cmd_count reset");
    CHECK(e.overflow == false, "overflow reset");
    CHECK(e.frame_open == false, "frame_open reset");
    return 0;
}

static int test_border_payload(void)
{
    uint8_t buf[1024];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0, 100, 100);
    /* border: rect 5,6 70x80, argb opaque red, w=3, r=4, side=FULL(0x0F) */
    lhc_enc_border(&e, 5, 6, 70, 80, 0xFFFF0000u, 3, 4, 0x0F);
    lhc_enc_end_frame(&e);
    size_t n = lhc_enc_finalize(&e);
    CHECK(n > 0, "no overflow");
    CHECK(e.cmd_count == 3, "3 cmds");
    const uint8_t *p = buf + 12;
    CHECK(p[0] == LHC_OP_BORDER, "BORDER opcode");
    CHECK(p[1] == 0, "no alpha hint (FF)");
    CHECK(p[2] == 15 && p[3] == 0, "payload_len=15");
    CHECK(p[4] == 5 && p[5] == 0, "x LE");
    CHECK(p[6] == 6 && p[7] == 0, "y LE");
    CHECK(p[8] == 70 && p[9] == 0, "w LE");
    CHECK(p[10] == 80 && p[11] == 0, "h LE");
    /* argb 0xFFFF0000 LE → 00 00 FF FF */
    CHECK(p[12] == 0x00 && p[13] == 0x00 && p[14] == 0xFF && p[15] == 0xFF, "argb LE");
    CHECK(p[16] == 3, "border width");
    CHECK(p[17] == 4, "radius");
    CHECK(p[18] == 0x0F, "side bitmap");
    return 0;
}

static int test_line_payload(void)
{
    uint8_t buf[1024];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0, 100, 100);
    /* line: (12,34)->(78,90), semi-transparent cyan-ish color, width=6 */
    lhc_enc_line(&e, 12, 34, 78, 90, 0x8011CCEEu, 6);
    lhc_enc_end_frame(&e);
    size_t n = lhc_enc_finalize(&e);
    CHECK(n > 0, "no overflow");
    CHECK(e.cmd_count == 3, "3 cmds");
    const uint8_t *p = buf + 12;
    CHECK(p[0] == LHC_OP_LINE, "LINE opcode");
    CHECK((p[1] & LHC_FLAG_HAS_ALPHA_HINT) != 0, "alpha hint set");
    CHECK(p[2] == 13 && p[3] == 0, "payload_len=13");
    CHECK(p[4] == 12 && p[5] == 0, "x1 LE");
    CHECK(p[6] == 34 && p[7] == 0, "y1 LE");
    CHECK(p[8] == 78 && p[9] == 0, "x2 LE");
    CHECK(p[10] == 90 && p[11] == 0, "y2 LE");
    /* argb 0x8011CCEE LE -> EE CC 11 80 */
    CHECK(p[12] == 0xEE && p[13] == 0xCC && p[14] == 0x11 && p[15] == 0x80, "argb LE");
    CHECK(p[16] == 6, "line width");
    return 0;
}

static int test_box_shadow_payload(void)
{
    uint8_t buf[1024];
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 0, 200, 200);
    /* shadow: rect 10,20 100x60, argb 0x80112233 (alpha=0x80, semi-transp),
       radius=12, blur=24, spread=-3, ofs=(6,-8), bg_cover=1 */
    lhc_enc_box_shadow(&e, 10, 20, 100, 60, 0x80112233u, 12, 24,
                       -3, 6, -8, 1);
    lhc_enc_end_frame(&e);
    size_t n = lhc_enc_finalize(&e);
    CHECK(n > 0, "no overflow");
    CHECK(e.cmd_count == 3, "3 cmds");
    const uint8_t *p = buf + 12;
    CHECK(p[0] == LHC_OP_BOX_SHADOW, "BOX_SHADOW opcode");
    CHECK(p[1] == LHC_FLAG_HAS_ALPHA_HINT, "alpha hint set");
    CHECK(p[2] == 20 && p[3] == 0, "payload_len=20");
    /* rect */
    CHECK(p[4] == 10 && p[5] == 0, "x LE");
    CHECK(p[6] == 20 && p[7] == 0, "y LE");
    CHECK(p[8] == 100 && p[9] == 0, "w LE");
    CHECK(p[10] == 60 && p[11] == 0, "h LE");
    /* argb 0x80112233 LE → 33 22 11 80 */
    CHECK(p[12] == 0x33 && p[13] == 0x22 && p[14] == 0x11 && p[15] == 0x80, "argb LE");
    CHECK(p[16] == 12, "radius");
    CHECK(p[17] == 24, "blur");
    CHECK((int8_t)p[18] == -3, "spread signed");
    /* ofs_x=6 LE */
    CHECK(p[19] == 6 && p[20] == 0, "ofs_x LE");
    /* ofs_y=-8 LE → F8 FF */
    CHECK(p[21] == 0xF8 && p[22] == 0xFF, "ofs_y LE signed");
    CHECK(p[23] == 1, "bg_cover");
    return 0;
}

static int test_image_and_blob_payload(void)
{
    uint8_t buf[4096];
    uint8_t rgba[16] = {
        0x10,0x20,0x30,0x40, 0x11,0x21,0x31,0x41,
        0x12,0x22,0x32,0x42, 0x13,0x23,0x33,0x43,
    };
    lhc_enc_t e;
    lhc_enc_init(&e, buf, sizeof(buf));
    lhc_enc_begin_frame(&e, 1, 64, 64);
    CHECK(lhc_enc_blob_upload(&e, 0x12345678u, 2, 2, LHC_BLOB_FMT_ARGB8888, rgba, sizeof(rgba)), "blob upload ok");
    lhc_enc_image(&e, 7, 9, 2, 2, 0x12345678u);
    lhc_enc_end_frame(&e);
    CHECK(lhc_enc_finalize(&e) > 0, "no overflow");

    /* BEGIN is first cmd at +4.., so BLOB starts at +12 */
    const uint8_t *p = buf + 12;
    CHECK(p[0] == LHC_OP_BLOB_UPLOAD, "BLOB_UPLOAD opcode");
    CHECK(p[2] == 25 && p[3] == 0, "blob payload len 9+16");
    CHECK(p[4] == 0x78 && p[5] == 0x56 && p[6] == 0x34 && p[7] == 0x12, "blob id LE");
    CHECK(p[8] == 2 && p[9] == 0 && p[10] == 2 && p[11] == 0, "blob w/h LE");
    CHECK(p[12] == LHC_BLOB_FMT_ARGB8888, "blob fmt");
    CHECK(p[13] == 0x10 && p[28] == 0x43, "blob bytes preserved");

    /* IMAGE command follows blob cmd: 4+25 bytes later */
    const uint8_t *q = p + 29;
    CHECK(q[0] == LHC_OP_IMAGE, "IMAGE opcode");
    CHECK((q[1] & LHC_FLAG_HAS_ALPHA_HINT) != 0, "image alpha hint set");
    CHECK(q[2] == 12 && q[3] == 0, "image payload len");
    CHECK(q[4] == 7 && q[5] == 0 && q[6] == 9 && q[7] == 0, "image x/y LE");
    CHECK(q[8] == 2 && q[9] == 0 && q[10] == 2 && q[11] == 0, "image w/h LE");
    CHECK(q[12] == 0x78 && q[13] == 0x56 && q[14] == 0x34 && q[15] == 0x12, "image blob id LE");
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "frame_header_layout", test_frame_header_layout },
        { "fill_rect_payload",   test_fill_rect_payload },
        { "alpha_flag",          test_alpha_flag },
        { "overflow_safe",       test_overflow_safe },
        { "multiple_fills",      test_multiple_fills },
        { "reinit_resets_state", test_reinit_resets_state },
        { "border_payload",      test_border_payload },
        { "line_payload",        test_line_payload },
        { "box_shadow_payload",  test_box_shadow_payload },
        { "image_and_blob_payload", test_image_and_blob_payload },
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int rc = tests[i].fn();
        printf("%s %s\n", rc == 0 ? "PASS" : "FAIL", tests[i].name);
        failures += (rc != 0);
    }
    printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
