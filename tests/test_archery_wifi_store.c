// tests/test_archery_wifi_store.c —— Wi-Fi 列表的去重、淘汰与编解码。
#include "archery_wifi_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_upsert_and_find(void) {
    archery_wifi_store_t store;
    archery_wifi_store_reset(&store);
    char pass[ARCHERY_WIFI_PASS_SIZE];

    assert(archery_wifi_store_upsert(&store, "Home", "12345678"));
    assert(store.count == 1);
    assert(archery_wifi_store_find(&store, "Home", pass, sizeof(pass)));
    assert(strcmp(pass, "12345678") == 0);

    // 同 SSID 更新密码，不新增
    assert(archery_wifi_store_upsert(&store, "Home", "87654321"));
    assert(store.count == 1);
    assert(archery_wifi_store_find(&store, "Home", pass, sizeof(pass)));
    assert(strcmp(pass, "87654321") == 0);

    assert(!archery_wifi_store_find(&store, "Office", pass, sizeof(pass)));
}

static void test_eviction(void) {
    archery_wifi_store_t store;
    archery_wifi_store_reset(&store);
    char name[16];
    char pass[ARCHERY_WIFI_PASS_SIZE];
    for (unsigned i = 0; i < ARCHERY_WIFI_STORE_MAX; i++) {
        snprintf(name, sizeof(name), "net%u", i);
        assert(archery_wifi_store_upsert(&store, name, "pw"));
    }
    assert(store.count == ARCHERY_WIFI_STORE_MAX);
    // 让 net0 变成最近使用，再插入一条新网络：应淘汰 net1。
    assert(archery_wifi_store_upsert(&store, "net0", "pw"));
    assert(archery_wifi_store_upsert(&store, "new", "pw"));
    assert(store.count == ARCHERY_WIFI_STORE_MAX);
    assert(archery_wifi_store_find(&store, "net0", pass, sizeof(pass)));
    assert(!archery_wifi_store_find(&store, "net1", pass, sizeof(pass)));
    assert(archery_wifi_store_find(&store, "new", pass, sizeof(pass)));
}

static void test_encode_decode(void) {
    archery_wifi_store_t store;
    archery_wifi_store_reset(&store);
    assert(archery_wifi_store_upsert(&store, "Home", "secret"));
    assert(archery_wifi_store_upsert(&store, "Office", "workpass"));

    uint8_t buffer[ARCHERY_WIFI_STORE_BYTES];
    assert(archery_wifi_store_encode(&store, buffer, sizeof(buffer)) ==
           ARCHERY_WIFI_STORE_BYTES);

    archery_wifi_store_t decoded;
    assert(archery_wifi_store_decode(&decoded, buffer, sizeof(buffer)));
    assert(decoded.count == 2);
    char pass[ARCHERY_WIFI_PASS_SIZE];
    assert(archery_wifi_store_find(&decoded, "Office", pass, sizeof(pass)));
    assert(strcmp(pass, "workpass") == 0);

    // CRC 损坏必须拒绝
    buffer[10] ^= 0xFF;
    assert(!archery_wifi_store_decode(&decoded, buffer, sizeof(buffer)));
}

int main(void) {
    test_upsert_and_find();
    test_eviction();
    test_encode_decode();
    puts("archery wifi store tests: PASS");
    return 0;
}
