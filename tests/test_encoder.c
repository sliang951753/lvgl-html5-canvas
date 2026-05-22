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

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "frame_header_layout", test_frame_header_layout },
        { "fill_rect_payload",   test_fill_rect_payload },
        { "alpha_flag",          test_alpha_flag },
        { "overflow_safe",       test_overflow_safe },
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
