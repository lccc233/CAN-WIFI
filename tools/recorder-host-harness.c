// Host checks compile the actual firmware recorder with deterministic platform
// stubs. No CAN driver, scheduler, or hardware timing claim is made by this test.
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

static unsigned char fake_heap[6 * 1024 * 1024];
static size_t fake_free, fake_largest, fake_allocated;
static uint32_t fake_ticks, fake_missed, fake_overrun;
static int fake_twai_fail, lock_depth, lock_error;

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest; const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i]; return dest;
}
void *memset(void *dest, int value, size_t n) {
    unsigned char *d = dest; for (size_t i = 0; i < n; i++) d[i] = (unsigned char)value; return dest;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b;
}
// The recorder only formats two 8-digit hexadecimal session components.
int snprintf(char *out, size_t size, const char *format, ...) {
    (void)format; va_list args; va_start(args, format);
    unsigned long parts[2] = { va_arg(args, unsigned long), va_arg(args, unsigned long) };
    va_end(args); const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; i++) if (i + 1 < size)
        out[i] = digits[(parts[i / 8] >> (28 - (i % 8) * 4)) & 15];
    if (size) out[size > 16 ? 16 : size - 1] = 0;
    return 16;
}

#include "../main/can_logger.c"

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout) {
    (void)timeout; if (!mutex || lock_depth) lock_error = 1; lock_depth++; return 1;
}
int xSemaphoreGive(SemaphoreHandle_t mutex) {
    if (!mutex || lock_depth != 1) lock_error = 1; lock_depth--; return 1;
}
uint32_t xTaskGetTickCount(void) { return fake_ticks; }
uint32_t esp_random(void) { return 0x1234abcd; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return fake_free; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return fake_largest; }
void *heap_caps_malloc(size_t amount, unsigned caps) {
    (void)caps;
    if (amount > sizeof(fake_heap) || amount > fake_free || amount > fake_largest) return NULL;
    fake_allocated = amount; return fake_heap;
}
esp_err_t twai_get_status_info(twai_status_info_t *info) {
    if (fake_twai_fail) return ESP_ERR_INVALID_STATE;
    info->rx_missed_count = fake_missed; info->rx_overrun_count = fake_overrun; return ESP_OK;
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static void reset(size_t bytes) {
    memset(&s_log, 0, sizeof(s_log));
    fake_free = bytes; fake_largest = bytes; fake_allocated = 0;
    fake_ticks = fake_missed = fake_overrun = 0; fake_twai_fail = lock_depth = lock_error = 0;
}
static can_msg_entry_t message(uint32_t id, uint32_t n) {
    can_msg_entry_t m = {0}; m.id = id; m.timestamp_ms = 1000; m.dlc = 8; m.extended = 1;
    m.data[0] = n & 255; m.data[1] = (n >> 8) & 255; return m;
}
static int capacity_and_protection(void) {
    const uint32_t ids[] = {0x100, 0x200, 0x100};
    can_log_status_t st; static can_msg_entry_t batch[256]; uint32_t copied;
    reset(CAN_LOG_PSRAM_RESERVE + CAN_LOG_ALLOC_MARGIN + 1024 * sizeof(can_msg_entry_t));
    CHECK(can_log_init() == ESP_OK); CHECK(s_log.capacity == 1024);
    CHECK(can_log_start_config("[{\"name\":\"frozen\"}]", ids, 3, &st) == ESP_OK);
    char session[17]; memcpy(session, st.session, sizeof(session));
    CHECK(can_log_acquire(session, &st) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_unprotect(session) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_clear() == ESP_ERR_INVALID_STATE);
    can_msg_entry_t unrelated = message(0x300, 99); can_log_write(&unrelated);
    for (uint32_t i = 0; i < 1024; i++) { can_msg_entry_t m = message(i % 2 ? 0x200 : 0x100, i); can_log_write(&m); }
    can_log_get_status(&st);
    CHECK(st.count == 1024 && st.capacity == 1024 && st.full && !st.recording && st.protected_record);
    CHECK(st.dropped == 0); CHECK(st.rx_quality_known && st.rx_lost == 0);
    can_msg_entry_t extra = message(0x100, 5555); can_log_write(&extra);
    CHECK(can_log_start_config("[]", ids, 1, &st) == ESP_ERR_INVALID_STATE);
    for (uint32_t from = 0; from < 1024; from += 128) {
        CHECK(can_log_read(session, from, batch, 256, &copied, &st) == ESP_OK);
        CHECK(copied == 128); CHECK(st.count == 1024);
        for (uint32_t i = 0; i < copied; i++) {
            CHECK(batch[i].timestamp_ms == 1000);
            CHECK(((uint32_t)batch[i].data[0] | ((uint32_t)batch[i].data[1] << 8)) == from + i);
        }
    }
    CHECK(can_log_read("ffffffffffffffff", 0, batch, 128, &copied, &st) == ESP_ERR_NOT_FOUND);
    CHECK(copied == 0);
    CHECK(can_log_read(session, 1025, batch, 128, &copied, &st) == ESP_ERR_INVALID_ARG);
    CHECK(can_log_read(session, 1024, batch, 128, &copied, &st) == ESP_OK && copied == 0);
    CHECK(can_log_acquire(session, &st) == ESP_OK);
    CHECK(can_log_unprotect(session) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_clear() == ESP_ERR_INVALID_STATE);
    can_log_release("ffffffffffffffff"); CHECK(s_log.readers == 1);
    can_log_release(session); CHECK(s_log.readers == 0);
    CHECK(can_log_clear() == ESP_ERR_INVALID_STATE);
    CHECK(can_log_unprotect(session) == ESP_OK);
    CHECK(can_log_start_config("[]", ids, 1, &st) == ESP_OK);
    CHECK(strcmp(st.session, session) != 0);
    CHECK(can_log_stop_session(session, &st) == ESP_ERR_NOT_FOUND && can_log_is_recording());
    CHECK(can_log_read(session, 0, batch, 128, &copied, &st) == ESP_ERR_NOT_FOUND);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
static int invalid_arguments_preserve_source(void) {
    reset(8 * 1024 * 1024); CHECK(can_log_init() == ESP_OK);
    const uint32_t id = 0x18ff0182, invalid = 0x20000000;
    can_log_status_t st; CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
    can_msg_entry_t m = message(id, 123); can_log_write(&m);
    char previous[17]; memcpy(previous, st.session, sizeof(previous));
    CHECK(can_log_start_config(NULL, &id, 1, &st) == ESP_ERR_INVALID_ARG);
    CHECK(can_log_start_config("[]", NULL, 1, &st) == ESP_ERR_INVALID_ARG);
    CHECK(can_log_start_config("[]", &id, 0, &st) == ESP_ERR_INVALID_ARG);
    CHECK(can_log_start_config("[]", &id, CAN_LOG_MAX_IDS + 1, &st) == ESP_ERR_INVALID_ARG);
    CHECK(can_log_start_config("[]", &invalid, 1, &st) == ESP_ERR_INVALID_ARG);
    static char large[CAN_LOG_CONFIG_MAX_BYTES + 2]; memset(large, 'x', sizeof(large)); large[sizeof(large) - 1] = 0;
    CHECK(can_log_start_config(large, &id, 1, &st) == ESP_ERR_INVALID_ARG);
    can_log_get_status(&st); CHECK(st.count == 1 && st.recording && strcmp(st.session, previous) == 0);
    CHECK(can_log_stop_session(previous, &st) == ESP_OK);
    CHECK(can_log_stop_session(previous, &st) == ESP_OK);
    char copied[8]; CHECK(can_log_copy_config(previous, copied, sizeof(copied)) == ESP_OK);
    CHECK(strcmp(copied, "[]") == 0);
    CHECK(can_log_copy_config(previous, copied, 2) == ESP_ERR_INVALID_SIZE);
    CHECK(can_log_copy_config("ffffffffffffffff", copied, sizeof(copied)) == ESP_ERR_NOT_FOUND);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
static int receive_loss_and_duration(void) {
    reset(8 * 1024 * 1024); CHECK(can_log_init() == ESP_OK);
    const uint32_t id = 0x100; can_log_status_t st;
    fake_missed = 10; fake_overrun = 4;
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
    fake_ticks = 1234; fake_missed = 13; fake_overrun = 6;
    can_log_get_status(&st); CHECK(st.rx_lost == 5 && st.rx_quality_known && st.duration_ms == 1234);
    CHECK(can_log_stop_session(st.session, &st) == ESP_OK);
    fake_ticks = 5000; fake_missed = 99; can_log_get_status(&st);
    CHECK(st.duration_ms == 1234 && st.rx_lost == 5);
    CHECK(can_log_unprotect(st.session) == ESP_OK);
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
    fake_missed = 0; can_log_get_status(&st); CHECK(!st.rx_quality_known);
    CHECK(can_log_stop_session(st.session, &st) == ESP_OK);
    CHECK(can_log_unprotect(st.session) == ESP_OK);
    fake_twai_fail = 1;
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK && !st.rx_quality_known);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
static int allocation_reserve_and_no_psram(void) {
    reset(CAN_LOG_PSRAM_RESERVE + CAN_LOG_ALLOC_MARGIN + 128);
    CHECK(can_log_init() == ESP_OK); can_log_status_t st; can_log_get_status(&st);
    CHECK(!st.psram_ok && fake_allocated == 0 && !can_log_is_recording());
    CHECK(can_log_start() == ESP_ERR_NOT_SUPPORTED);
    can_msg_entry_t m = message(0x100, 1); can_log_write(&m); CHECK(s_log.count == 0);
    reset(8 * 1024 * 1024); CHECK(can_log_init() == ESP_OK);
    CHECK(fake_allocated <= CAN_LOG_PSRAM_SIZE);
    CHECK(fake_free - fake_allocated >= CAN_LOG_PSRAM_RESERVE + CAN_LOG_ALLOC_MARGIN);
    reset(CAN_LOG_PSRAM_RESERVE + CAN_LOG_ALLOC_MARGIN + 2048 * sizeof(can_msg_entry_t));
    fake_largest = 1500 * sizeof(can_msg_entry_t); CHECK(can_log_init() == ESP_OK);
    CHECK(s_log.capacity == 1500 && fake_allocated == fake_largest);
    CHECK(fake_free - fake_allocated >= CAN_LOG_PSRAM_RESERVE + CAN_LOG_ALLOC_MARGIN);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
static int frozen_config_and_id_list(void) {
    reset(8 * 1024 * 1024); CHECK(can_log_init() == ESP_OK);
    uint32_t ids[] = {0x100, 0x100}; char config[] = "[{\"factor\":0}]"; can_log_status_t st;
    CHECK(can_log_start_config(config, ids, 2, &st) == ESP_OK);
    ids[0] = ids[1] = 0x200; config[2] = 'X';
    char copied[64]; CHECK(can_log_copy_config(st.session, copied, sizeof(copied)) == ESP_OK);
    CHECK(strcmp(copied, "[{\"factor\":0}]") == 0);
    can_msg_entry_t m = message(0x100, 2); can_log_write(&m);
    m.id = 0x200; can_log_write(&m); can_log_get_status(&st); CHECK(st.count == 1);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
static int browser_sync_leases(void) {
    reset(8 * 1024 * 1024); CHECK(can_log_init() == ESP_OK);
    const uint32_t id = 0x100; can_log_status_t st; can_msg_entry_t out[4]; uint32_t count;
    const char *a = "0000000000000001", *b = "0000000000000002";
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
    char session[17]; memcpy(session, st.session, sizeof(session));
    can_msg_entry_t m = message(id, 77); can_log_write(&m);
    CHECK(can_log_read_client(session, a, 0, out, 4, &count, &st) == ESP_OK && count == 1);
    CHECK(can_log_sync_done(session, a, 1) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_stop_session(session, &st) == ESP_OK);
    CHECK(can_log_read_client(session, a, 1, out, 4, &count, &st) == ESP_OK && count == 0);
    CHECK(can_log_unprotect(session) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_read_client(session, b, 0, out, 4, &count, &st) == ESP_OK && count == 1);
    CHECK(can_log_unprotect_client(session, a) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_sync_done(session, b, 0) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_unprotect_client(session, a) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_sync_done("ffffffffffffffff", b, 1) == ESP_ERR_NOT_FOUND);
    CHECK(can_log_sync_done(session, "wrong", 1) == ESP_ERR_INVALID_ARG);
    CHECK(can_log_sync_done(session, b, 1) == ESP_OK);
    CHECK(can_log_unprotect_client(session, a) == ESP_OK);
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
    memcpy(session, st.session, sizeof(session)); can_log_write(&m);
    CHECK(can_log_stop_session(session, &st) == ESP_OK);
    char token[17] = "0000000000000000";
    for (unsigned n = 1; n <= CAN_LOG_SYNC_CLIENTS; n++) {
        token[15] = '0' + n;
        CHECK(can_log_read_client(session, token, 0, out, 4, &count, &st) == ESP_OK && count == 1);
    }
    token[15] = '9';
    CHECK(can_log_read_client(session, token, 0, out, 4, &count, &st) == ESP_ERR_INVALID_STATE && count == 0);
    fake_ticks = CAN_LOG_SYNC_LEASE_MS - 1;
    CHECK(can_log_read_client(session, a, 1, out, 4, &count, &st) == ESP_OK && count == 0);
    fake_ticks = CAN_LOG_SYNC_LEASE_MS;
    CHECK(can_log_unprotect_client(session, b) == ESP_ERR_INVALID_STATE);
    fake_ticks = CAN_LOG_SYNC_LEASE_MS * 2;
    CHECK(can_log_unprotect_client(session, b) == ESP_OK);
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK); memcpy(session, st.session, sizeof(session));
    CHECK(can_log_stop_session(session, &st) == ESP_OK);
    fake_ticks = UINT32_MAX - 5;
    CHECK(can_log_read_client(session, a, 0, out, 4, &count, &st) == ESP_OK && count == 0);
    fake_ticks = 5000;
    CHECK(can_log_unprotect_client(session, b) == ESP_ERR_INVALID_STATE);
    fake_ticks = CAN_LOG_SYNC_LEASE_MS;
    CHECK(can_log_unprotect_client(session, b) == ESP_OK);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
static int idempotent_ack_after_record_replacement(void) {
    reset(8 * 1024 * 1024); CHECK(can_log_init() == ESP_OK);
    const uint32_t id = 0x100; can_log_status_t st; can_msg_entry_t out[2]; uint32_t count;
    const char *a = "0000000000000001", *b = "0000000000000002";
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
    char old_session[17], new_session[17]; memcpy(old_session, st.session, sizeof(old_session));
    can_msg_entry_t m = message(id, 111); can_log_write(&m); CHECK(can_log_stop_session(old_session, &st) == ESP_OK);
    CHECK(can_log_read_client(old_session, a, 0, out, 2, &count, &st) == ESP_OK && count == 1);
    CHECK(can_log_sync_done(old_session, a, 1) == ESP_OK);
    CHECK(can_log_unprotect_client(old_session, b) == ESP_OK);
    CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK); memcpy(new_session, st.session, sizeof(new_session));
    m.data[0] = 222; can_log_write(&m);
    CHECK(can_log_read_client(new_session, b, 0, out, 2, &count, &st) == ESP_OK && count == 1);
    CHECK(can_log_sync_done(old_session, a, 1) == ESP_OK);
    CHECK(can_log_sync_done(old_session, a, 0) == ESP_ERR_NOT_FOUND);
    can_log_get_status(&st); CHECK(st.recording && st.count == 1 && strcmp(st.session, new_session) == 0);
    CHECK(can_log_stop_session(new_session, &st) == ESP_OK);
    CHECK(can_log_unprotect_client(new_session, a) == ESP_ERR_INVALID_STATE);
    CHECK(can_log_sync_done(new_session, b, 1) == ESP_OK);
    CHECK(can_log_unprotect_client(new_session, a) == ESP_OK);
    // Fill the bounded receipt cache with newer sessions; old receipts expire
    // through eviction, rather than consuming unbounded device memory.
    for (unsigned i = 0; i < 8; i++) {
        CHECK(can_log_start_config("[]", &id, 1, &st) == ESP_OK);
        CHECK(can_log_stop_session(st.session, &st) == ESP_OK);
        CHECK(can_log_sync_done(st.session, b, 0) == ESP_OK);
        CHECK(can_log_unprotect_client(st.session, b) == ESP_OK);
    }
    CHECK(can_log_sync_done(old_session, a, 1) == ESP_ERR_NOT_FOUND);
    CHECK(!lock_error && lock_depth == 0); return 0;
}
__declspec(dllexport) int recorder_test_case(int which) {
    switch (which) {
        case 0: return capacity_and_protection();
        case 1: return invalid_arguments_preserve_source();
        case 2: return receive_loss_and_duration();
        case 3: return allocation_reserve_and_no_psram();
        case 4: return frozen_config_and_id_list();
        case 5: return browser_sync_leases();
        case 6: return idempotent_ack_after_record_replacement();
        default: return -1;
    }
}
