#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Копии определений из starter.c (нужны для компиляции test.c отдельно) */
#define CAN_MAX_DLC      8u
#define CAN_SFF_MASK     0x000007FFu
#define CAN_EFF_MASK     0x1FFFFFFFu
#define CAN_EFF_FLAG     0x80000000u
#define CAN_RTR_FLAG     0x40000000u
#define CAN_ERR_FLAG     0x20000000u

typedef struct {
    uint32_t can_id;
    uint8_t  can_dlc;
    uint8_t  __pad[3];
    uint8_t  data[CAN_MAX_DLC];
} can_frame_t;

/* Объявления из starter.c */
can_frame_t make_frame(uint32_t id, const uint8_t *data, uint8_t dlc);
can_frame_t make_frame_ext(uint32_t id, const uint8_t *data, uint8_t dlc);
int         frame_valid(const can_frame_t *f);
int         frame_to_candump(const can_frame_t *f, char *buf, size_t bufsz);
int         candump_to_frame(const char *s, can_frame_t *f);
int         frame_matches_filter(const can_frame_t *f, uint32_t filter_id, uint32_t mask);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void)
{
    /* --- make_frame (SFF) --- */
    printf("=== make_frame (SFF) ===\n");
    uint8_t d4[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    can_frame_t f = make_frame(0x123u, d4, 4);
    CHECK(f.can_id == 0x123u,                         "SFF: can_id == 0x123");
    CHECK(f.can_dlc == 4u,                             "SFF: dlc == 4");
    CHECK(f.data[0] == 0xDEu && f.data[3] == 0xEFu,  "SFF: data correct");
    CHECK(!(f.can_id & CAN_EFF_FLAG),                  "SFF: EFF_FLAG not set");

    /* make_frame с нулевыми данными */
    can_frame_t f0 = make_frame(0x7FFu, NULL, 0);
    CHECK(f0.can_id == 0x7FFu, "SFF max ID 0x7FF");
    CHECK(f0.can_dlc == 0u,    "SFF dlc=0");

    /* --- make_frame_ext (EFF) --- */
    printf("=== make_frame_ext (EFF) ===\n");
    can_frame_t fe = make_frame_ext(0x18FF0102u, d4, 4);
    CHECK((fe.can_id & CAN_EFF_FLAG) != 0u,                "EFF: EFF_FLAG set");
    CHECK((fe.can_id & CAN_EFF_MASK) == 0x18FF0102u,       "EFF: ID correct");
    CHECK(fe.can_dlc == 4u,                                 "EFF: dlc == 4");
    CHECK(fe.data[0] == 0xDEu && fe.data[3] == 0xEFu,     "EFF: data correct");

    /* --- frame_valid --- */
    printf("=== frame_valid ===\n");
    CHECK(frame_valid(&f)  == 1, "valid SFF frame");
    CHECK(frame_valid(&fe) == 1, "valid EFF frame");

    can_frame_t bad;
    memset(&bad, 0, sizeof(bad));
    bad.can_dlc = 9u;
    CHECK(frame_valid(&bad) == 0, "invalid: dlc=9");

    memset(&bad, 0, sizeof(bad));
    bad.can_id = 0x800u;   /* > 0x7FF без EFF_FLAG */
    bad.can_dlc = 0u;
    CHECK(frame_valid(&bad) == 0, "invalid: SFF id 0x800 > 0x7FF");

    memset(&bad, 0, sizeof(bad));
    bad.can_id = CAN_ERR_FLAG;
    bad.can_dlc = 0u;
    CHECK(frame_valid(&bad) == 0, "invalid: ERR_FLAG set");

    memset(&bad, 0, sizeof(bad));
    bad.can_id = 0x100u;
    bad.can_dlc = 8u;      /* dlc=8 валидно */
    CHECK(frame_valid(&bad) == 1, "valid: dlc=8 is ok");

    /* --- frame_to_candump --- */
    printf("=== frame_to_candump ===\n");
    char buf[64];
    int n = frame_to_candump(&f, buf, sizeof(buf));
    CHECK(n > 0, "to_candump: returns > 0");
    CHECK(strcmp(buf, "123#DEADBEEF") == 0, "SFF: '123#DEADBEEF'");

    n = frame_to_candump(&f0, buf, sizeof(buf));
    CHECK(n >= 0 && strcmp(buf, "7FF#") == 0, "empty SFF: '7FF#'");

    /* EFF format: 8 hex digits */
    n = frame_to_candump(&fe, buf, sizeof(buf));
    CHECK(n > 0, "EFF to_candump: returns > 0");
    CHECK(strncmp(buf, "18FF0102#", 9) == 0, "EFF: starts with '18FF0102#'");

    /* --- candump_to_frame --- */
    printf("=== candump_to_frame ===\n");
    can_frame_t fp;
    memset(&fp, 0, sizeof(fp));
    CHECK(candump_to_frame("123#DEADBEEF", &fp) == 0, "parse '123#DEADBEEF': returns 0");
    CHECK((fp.can_id & CAN_SFF_MASK) == 0x123u,       "parsed id == 0x123");
    CHECK(fp.can_dlc == 4u,                            "parsed dlc == 4");
    CHECK(fp.data[0] == 0xDEu && fp.data[3] == 0xEFu, "parsed data correct");

    memset(&fp, 0, sizeof(fp));
    CHECK(candump_to_frame("7FF#", &fp) == 0,          "parse empty '7FF#': ok");
    CHECK(fp.can_dlc == 0u,                            "parsed empty: dlc=0");

    CHECK(candump_to_frame("ZZZ#00", &fp) == -1,       "bad hex id → -1");
    CHECK(candump_to_frame("123", &fp) == -1,          "no '#' → -1");

    /* Round-trip */
    frame_to_candump(&f, buf, sizeof(buf));
    memset(&fp, 0, sizeof(fp));
    candump_to_frame(buf, &fp);
    CHECK((fp.can_id & CAN_SFF_MASK) == 0x123u && fp.can_dlc == 4u,
          "round-trip: id and dlc preserved");
    CHECK(fp.data[0] == 0xDEu && fp.data[3] == 0xEFu, "round-trip: data preserved");

    /* --- frame_matches_filter --- */
    printf("=== frame_matches_filter ===\n");
    can_frame_t fa = make_frame(0x100u, NULL, 0);
    CHECK(frame_matches_filter(&fa, 0x100u, 0x7FFu) == 1, "exact match 0x100");
    CHECK(frame_matches_filter(&fa, 0x200u, 0x7FFu) == 0, "no match: 0x200 != 0x100");
    CHECK(frame_matches_filter(&fa, 0x100u, 0x700u) == 1, "range 0x100-0x1FF: matches 0x100");

    can_frame_t fb = make_frame(0x1FFu, NULL, 0);
    CHECK(frame_matches_filter(&fb, 0x100u, 0x700u) == 1, "range 0x100-0x1FF: matches 0x1FF");
    CHECK(frame_matches_filter(&fb, 0x200u, 0x700u) == 0, "range 0x200-0x2FF: no match 0x1FF");

    /* mask=0: match everything */
    CHECK(frame_matches_filter(&fa, 0x999u, 0x000u) == 1, "mask=0: matches any ID");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
