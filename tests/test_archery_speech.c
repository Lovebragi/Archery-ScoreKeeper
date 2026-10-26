// tests/test_archery_speech.c —— 报靶文本解析的主机测试。
#include "archery_speech.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "archery_score.h"

static void expect_ok(const char *text, uint8_t arrows, uint8_t target,
                      const uint8_t *rings, const uint8_t *counts, int n) {
    archery_speech_result_t result;
    archery_speech_parse(text, target, arrows, &result);
    if (!result.ok) {
        printf("FAIL %s: error=%d count=%u\n", text, (int)result.error,
               (unsigned)result.count);
    }
    assert(result.ok);
    assert(result.count == n);
    for (int i = 0; i < n; i++) {
        assert(result.items[i].ring == rings[i]);
        assert(result.items[i].count == counts[i]);
    }
}

static void expect_error(const char *text, uint8_t arrows, uint8_t target,
                         archery_speech_error_t error) {
    archery_speech_result_t result;
    archery_speech_parse(text, target, arrows, &result);
    if (result.ok || result.error != error) {
        printf("FAIL %s: ok=%d error=%d (want %d)\n", text, result.ok,
               (int)result.error, (int)error);
    }
    assert(!result.ok);
    assert(result.error == error);
}

static void test_orders(void) {
    const uint8_t r[] = {9};
    const uint8_t c[] = {3};
    expect_ok("三根九环", 3, ARCHERY_TARGET_HALF_40, r, c, 1);
    expect_ok("三个九", 3, ARCHERY_TARGET_HALF_40, r, c, 1);
    expect_ok("三根九", 3, ARCHERY_TARGET_HALF_40, r, c, 1);
    expect_ok("九环三根", 3, ARCHERY_TARGET_HALF_40, r, c, 1);
}

static void test_colloquial(void) {
    const uint8_t r10[] = {10};
    const uint8_t c3[] = {3};
    expect_ok("十环仨", 3, ARCHERY_TARGET_HALF_40, r10, c3, 1);
    const uint8_t r9[] = {9};
    const uint8_t c2[] = {2};
    expect_ok("俩九环", 2, ARCHERY_TARGET_HALF_40, r9, c2, 1);
    expect_ok("仨十", 3, ARCHERY_TARGET_HALF_40, r10, c3, 1);
    const uint8_t r0[] = {0};
    expect_ok("俩脱靶", 2, ARCHERY_TARGET_HALF_40, r0, c2, 1);
    const uint8_t c1[] = {1};
    expect_ok("一个脱靶", 1, ARCHERY_TARGET_HALF_40, r0, c1, 1);
    const uint8_t c6[] = {6};
    expect_ok("六根十环", 6, ARCHERY_TARGET_HALF_40, r10, c6, 1);
}

static void test_inner_ten(void) {
    const uint8_t rX[] = {ARCHERY_SCORE_X};
    const uint8_t c1[] = {1};
    expect_ok("叉儿一根", 1, ARCHERY_TARGET_HALF_40, rX, c1, 1);
    const uint8_t c2[] = {2};
    expect_ok("俩叉儿", 2, ARCHERY_TARGET_HALF_40, rX, c2, 1);
}

static void test_bare_pairs(void) {
    const uint8_t r9[] = {9};
    const uint8_t c1[] = {1};
    expect_ok("一九", 1, ARCHERY_TARGET_HALF_40, r9, c1, 1);
    const uint8_t c6[] = {6};
    expect_ok("九六", 6, ARCHERY_TARGET_HALF_40, r9, c6, 1);
    const uint8_t r6[] = {6};
    const uint8_t c9[] = {9};
    expect_ok("九六", 9, ARCHERY_TARGET_FULL_40, r6, c9, 1);
}

static void test_group(void) {
    const uint8_t rings[] = {10, 9, 0};
    const uint8_t counts[] = {3, 2, 1};
    expect_ok("十环三根，九环两根，脱靶一个", 6, ARCHERY_TARGET_HALF_40, rings, counts, 3);
    expect_ok("十环三根九环两根脱靶一个", 6, ARCHERY_TARGET_HALF_40, rings, counts, 3);
    expect_ok("十 环三 根 九环两根拖把一个", 6, ARCHERY_TARGET_HALF_40, rings, counts, 3);
    expect_ok("10。环3。根。九环两根拖把一个。", 6, ARCHERY_TARGET_HALF_40, rings, counts, 3);
}

static void test_all_phrases(void) {
    archery_speech_result_t result;
    archery_speech_parse("全都是十", ARCHERY_TARGET_HALF_40, 6, &result);
    assert(result.ok && result.count == 1 && result.items[0].all &&
           result.items[0].ring == 10);
    archery_speech_parse("全十", ARCHERY_TARGET_HALF_40, 6, &result);
    assert(result.ok && result.items[0].all && result.items[0].ring == 10);
    archery_speech_parse("剩下都是九", ARCHERY_TARGET_HALF_40, 6, &result);
    assert(result.ok && result.items[0].all && result.items[0].ring == 9);
    archery_speech_parse("十环俩剩下全是九", ARCHERY_TARGET_HALF_40, 6, &result);
    assert(result.ok && result.count == 2);
    assert(!result.items[0].all && result.items[0].ring == 10 &&
           result.items[0].count == 2);
    assert(result.items[1].all && result.items[1].ring == 9);
}

static void test_failures(void) {
    expect_error("十环三根", 6, ARCHERY_TARGET_HALF_40, ARCHERY_SPEECH_MISMATCH);
    expect_error("十环", 6, ARCHERY_TARGET_HALF_40, ARCHERY_SPEECH_INCOMPLETE);
    expect_error("不知道", 6, ARCHERY_TARGET_HALF_40, ARCHERY_SPEECH_UNPARSED);
    expect_error("四环两根", 2, ARCHERY_TARGET_HALF_40, ARCHERY_SPEECH_RING_INVALID);
    expect_error("全都是四", 6, ARCHERY_TARGET_HALF_40, ARCHERY_SPEECH_RING_INVALID);
    expect_error("十环六根全都是九", 6, ARCHERY_TARGET_HALF_40, ARCHERY_SPEECH_EXCESS);
}

int main(void) {
    test_orders();
    test_colloquial();
    test_inner_ten();
    test_bare_pairs();
    test_group();
    test_all_phrases();
    test_failures();
    puts("archery speech parser tests: PASS");
    return 0;
}
