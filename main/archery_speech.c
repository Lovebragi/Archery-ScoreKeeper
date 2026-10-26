// main/archery_speech.c —— 报靶文本解析实现（纯逻辑）。
#include "archery_speech.h"

#include <string.h>

#include "archery_score.h"

typedef struct {
    const char *text;
    uint8_t value;
} word_t;

static const word_t NUMERALS[] = {
    {"十一", 11}, {"十二", 12}, {"十", 10}, {"零", 0}, {"〇", 0}, {"洞", 0},
    {"一", 1}, {"二", 2}, {"两", 2}, {"俩", 2}, {"三", 3}, {"仨", 3},
    {"四", 4}, {"五", 5}, {"六", 6}, {"七", 7}, {"八", 8}, {"九", 9},
};
static const word_t RING_WORDS[] = {
    {"脱靶", ARCHERY_SCORE_MISS}, {"拖靶", ARCHERY_SCORE_MISS},
    {"脱把", ARCHERY_SCORE_MISS}, {"拖把", ARCHERY_SCORE_MISS},
    {"内十", ARCHERY_SCORE_X}, {"叉儿", ARCHERY_SCORE_X},
    {"叉", ARCHERY_SCORE_X}, {"X", ARCHERY_SCORE_X}, {"x", ARCHERY_SCORE_X},
};
static const char *const CLASSIFIERS[] = {"根", "个", "支", "箭", "发", "只"};
static const char *const ALL_PHRASES[] = {
    "全都是", "剩下的都是", "剩下都是", "余下都是", "全是", "都是", "全",
};

typedef enum { TOK_NUM = 0, TOK_RING, TOK_ALL } token_kind_t;

typedef struct {
    token_kind_t kind;
    uint8_t value;
    bool has_class;
} token_t;

static bool starts_with(const char *text, const char *word) {
    const size_t length = strlen(word);
    return strncmp(text, word, length) == 0;
}

static bool skip_separator(const char **cursor) {
    static const char *const separators[] = {"，", "。", "、", "；", "！", "？",
                                             "：", " ", "\t", ",", ".", ";", "?", "!"};
    for (size_t i = 0; i < sizeof(separators) / sizeof(separators[0]); i++) {
        if (starts_with(*cursor, separators[i])) {
            *cursor += strlen(separators[i]);
            return true;
        }
    }
    return false;
}

static int find_word(const char *text, const word_t *table, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (starts_with(text, table[i].text)) {
            return (int)table[i].value;
        }
    }
    return -1;
}

static bool tokenize(const char *text, token_t *tokens, size_t capacity, size_t *out_count) {
    size_t count = 0;
    const char *cursor = text;
    while (*cursor) {
        if (skip_separator(&cursor)) continue;
        bool matched = false;
        for (size_t i = 0; i < sizeof(ALL_PHRASES) / sizeof(ALL_PHRASES[0]); i++) {
            if (starts_with(cursor, ALL_PHRASES[i])) {
                if (count < capacity) tokens[count++] = (token_t){.kind = TOK_ALL};
                cursor += strlen(ALL_PHRASES[i]);
                matched = true;
                break;
            }
        }
        if (matched) continue;

        const int ring_word = find_word(cursor, RING_WORDS,
                                        sizeof(RING_WORDS) / sizeof(RING_WORDS[0]));
        if (ring_word >= 0) {
            if (count < capacity) {
                tokens[count++] = (token_t){.kind = TOK_RING, .value = (uint8_t)ring_word};
            }
            for (size_t i = 0; i < sizeof(RING_WORDS) / sizeof(RING_WORDS[0]); i++) {
                if (starts_with(cursor, RING_WORDS[i].text)) {
                    cursor += strlen(RING_WORDS[i].text);
                    break;
                }
            }
            continue;
        }

        if (*cursor >= '0' && *cursor <= '9') {
            unsigned value = 0;
            int digits = 0;
            while (digits < 2 && cursor[digits] >= '0' && cursor[digits] <= '9') {
                value = value * 10u + (unsigned)(cursor[digits] - '0');
                digits++;
            }
            cursor += digits;
            bool has_class = false;
            for (size_t i = 0; i < sizeof(CLASSIFIERS) / sizeof(CLASSIFIERS[0]); i++) {
                if (starts_with(cursor, CLASSIFIERS[i])) {
                    has_class = true;
                    cursor += strlen(CLASSIFIERS[i]);
                    break;
                }
            }
            if (starts_with(cursor, "环")) {
                if (count < capacity) {
                    tokens[count++] = (token_t){.kind = TOK_RING, .value = (uint8_t)value};
                }
                cursor += strlen("环");
            } else if (count < capacity) {
                tokens[count++] = (token_t){
                    .kind = TOK_NUM, .value = (uint8_t)value, .has_class = has_class};
            }
            continue;
        }

        const int numeral = find_word(cursor, NUMERALS,
                                      sizeof(NUMERALS) / sizeof(NUMERALS[0]));
        if (numeral >= 0) {
            uint8_t value = (uint8_t)numeral;
            for (size_t i = 0; i < sizeof(NUMERALS) / sizeof(NUMERALS[0]); i++) {
                if (starts_with(cursor, NUMERALS[i].text)) {
                    cursor += strlen(NUMERALS[i].text);
                    value = NUMERALS[i].value;
                    break;
                }
            }
            bool has_class = false;
            for (size_t i = 0; i < sizeof(CLASSIFIERS) / sizeof(CLASSIFIERS[0]); i++) {
                if (starts_with(cursor, CLASSIFIERS[i])) {
                    has_class = true;
                    cursor += strlen(CLASSIFIERS[i]);
                    break;
                }
            }
            if (starts_with(cursor, "环")) {
                if (count < capacity) {
                    tokens[count++] = (token_t){.kind = TOK_RING, .value = value};
                }
                cursor += strlen("环");
            } else if (count < capacity) {
                tokens[count++] = (token_t){
                    .kind = TOK_NUM, .value = value, .has_class = has_class};
            }
            continue;
        }

        if (starts_with(cursor, "环")) {
            cursor += strlen("环");
            continue;
        }
        cursor++;  // 未知字符：跳过
    }
    *out_count = count;
    return true;
}

static bool ring_allowed(uint8_t target, uint8_t ring) {
    if (ring == ARCHERY_SCORE_X || ring == ARCHERY_SCORE_MISS) return true;
    if (target == ARCHERY_TARGET_FULL_40 || target == ARCHERY_TARGET_FULL_80 ||
        target == ARCHERY_TARGET_FULL_122) {
        return ring >= 1 && ring <= 10;
    }
    return ring >= 6 && ring <= 10;
}

static void add_item(archery_speech_result_t *out, uint8_t ring, uint8_t count) {
    if (out->count >= ARCHERY_SPEECH_MAX_ITEMS) return;
    out->items[out->count++] = (archery_speech_item_t){.ring = ring, .count = count};
}

static void add_all(archery_speech_result_t *out, uint8_t ring) {
    if (out->count >= ARCHERY_SPEECH_MAX_ITEMS) return;
    out->items[out->count++] = (archery_speech_item_t){.ring = ring, .all = true};
}

void archery_speech_parse(const char *text, uint8_t target, uint8_t arrows,
                          archery_speech_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->arrows = arrows;
    if (!text || !*text) {
        out->error = ARCHERY_SPEECH_UNPARSED;
        return;
    }

    token_t tokens[48];
    size_t token_count = 0;
    tokenize(text, tokens, sizeof(tokens) / sizeof(tokens[0]), &token_count);

    int pending_ring = -1;
    int pending_count = -1;
    int pending_num = -1;
    bool all_pending = false;

    for (size_t i = 0; i < token_count; i++) {
        const token_t *token = &tokens[i];
        if (token->kind == TOK_ALL) {
            all_pending = true;
            continue;
        }
        if (token->kind == TOK_RING) {
            if (all_pending) {
                add_all(out, token->value);
                all_pending = false;
            } else if (pending_count >= 0) {
                add_item(out, token->value, (uint8_t)pending_count);
                pending_count = -1;
            } else if (pending_num >= 0) {
                add_item(out, token->value, (uint8_t)pending_num);
                pending_num = -1;
            } else if (pending_ring >= 0) {
                out->error = ARCHERY_SPEECH_AMBIGUOUS;
                return;
            } else {
                pending_ring = token->value;
            }
            continue;
        }

        // 数字
        if (all_pending) {
            add_all(out, token->value);
            all_pending = false;
            continue;
        }
        if (pending_ring >= 0) {
            add_item(out, (uint8_t)pending_ring, token->value);
            pending_ring = -1;
            continue;
        }
        if (token->has_class) {
            if (pending_count >= 0) {
                out->error = ARCHERY_SPEECH_INCOMPLETE;
                return;
            }
            pending_count = token->value;
            continue;
        }
        if (pending_count >= 0) {
            add_item(out, token->value, (uint8_t)pending_count);
            pending_count = -1;
            continue;
        }
        if (pending_num >= 0) {
            const int a = pending_num;
            const int b = token->value;
            const int candidates[2][2] = {{a, b}, {b, a}};
            bool found = false;
            for (int c = 0; c < 2 && !found; c++) {
                const int count = candidates[c][0];
                const int ring = candidates[c][1];
                if (count >= 1 && count <= arrows && ring_allowed(target, ring)) {
                    add_item(out, (uint8_t)ring, (uint8_t)count);
                    found = true;
                }
            }
            if (!found) {
                out->error = ARCHERY_SPEECH_AMBIGUOUS;
                return;
            }
            pending_num = -1;
            continue;
        }
        pending_num = token->value;
    }

    if (pending_ring >= 0 && pending_count >= 0) {
        add_item(out, (uint8_t)pending_ring, (uint8_t)pending_count);
        pending_ring = pending_count = -1;
    } else if (pending_ring >= 0 && pending_num >= 0) {
        add_item(out, (uint8_t)pending_ring, (uint8_t)pending_num);
        pending_ring = pending_num = -1;
    } else if (pending_count >= 0 && pending_num >= 0) {
        add_item(out, (uint8_t)pending_num, (uint8_t)pending_count);
        pending_count = pending_num = -1;
    }
    if (all_pending || pending_ring >= 0 || pending_count >= 0 || pending_num >= 0) {
        out->error = ARCHERY_SPEECH_INCOMPLETE;
        return;
    }
    if (out->count == 0) {
        out->error = ARCHERY_SPEECH_UNPARSED;
        return;
    }

    bool has_all = false;
    unsigned total = 0;
    for (uint8_t i = 0; i < out->count; i++) {
        const archery_speech_item_t *item = &out->items[i];
        if (!ring_allowed(target, item->ring)) {
            out->error = ARCHERY_SPEECH_RING_INVALID;
            return;
        }
        if (item->all) {
            if (has_all) {
                out->error = ARCHERY_SPEECH_MULTI_ALL;
                return;
            }
            has_all = true;
        } else {
            total += item->count;
        }
    }
    if (total > arrows) {
        out->error = ARCHERY_SPEECH_EXCESS;
        return;
    }
    if (has_all) {
        if (total >= arrows) {
            out->error = ARCHERY_SPEECH_EXCESS;
            return;
        }
    } else if (total != arrows) {
        out->missing = (uint8_t)(arrows - total);
        out->error = ARCHERY_SPEECH_MISMATCH;
        return;
    }
    out->ok = true;
    out->error = ARCHERY_SPEECH_OK;
}
