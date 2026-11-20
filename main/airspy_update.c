#include "airspy_update.h"
#include "airspy.h"
#include "airspy_samba.h"
#include "airspy_update_check.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "airspy_update";

static size_t s_image_len;

static airspy_update_state_t s_state = AIRSPY_UPDATE_IDLE;
static uint8_t s_progress;
static const char *s_message = "idle";
static int64_t s_deadline;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

#define SAMBA_TIMEOUT_US 30000000ll /* 30 s */

static void set_state(airspy_update_state_t state, const char *message);

/* Update progress survives the ESP32 reboot (the receiver only re-enumerates
 * cleanly after a full USB host reset). Phases: 1 = waiting for SAM-BA after
 * reboot, 2 = waiting for normal mode after flash/recover. */
static void persist_phase(uint8_t phase, uint32_t len)
{
    nvs_handle_t h;
    if (nvs_open("airspy", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, "phase", phase);
    nvs_set_u32(h, "len", len);
    nvs_commit(h);
    nvs_close(h);
}

static void clear_phase(void)
{
    nvs_handle_t h;
    if (nvs_open("airspy", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, "phase", 0);
    nvs_set_u32(h, "len", 0);
    nvs_commit(h);
    nvs_close(h);
}

static bool s_resume_done = false;

static void resume_from_nvs(void)
{
    s_resume_done = true;
    nvs_handle_t h;
    if (nvs_open("airspy", NVS_READONLY, &h) != ESP_OK)
        return;
    uint8_t phase = 0;
    uint32_t len = 0;
    nvs_get_u8(h, "phase", &phase);
    nvs_get_u32(h, "len", &len);
    nvs_close(h);
    if (phase == 1) {
        s_image_len = len;
        set_state(AIRSPY_UPDATE_WAIT_SAMBA, "resumed: waiting for SAM-BA bootloader");
        s_deadline = esp_timer_get_time() + SAMBA_TIMEOUT_US;
    } else if (phase == 2) {
        set_state(AIRSPY_UPDATE_BOOTING, "resumed: waiting for receiver restart");
        s_deadline = esp_timer_get_time() + SAMBA_TIMEOUT_US;
    }
}

static void set_state(airspy_update_state_t state, const char *message)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    s_message = message;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "%s", message);
}

static void set_progress(uint8_t pct)
{
    portENTER_CRITICAL(&s_lock);
    s_progress = pct;
    portEXIT_CRITICAL(&s_lock);
}

/* --- flash scratch area for the uploaded image (internal RAM is too tight for
 * a full-image buffer) --- */

esp_err_t airspy_update_scratch_prepare(void)
{
    esp_err_t e = esp_flash_erase_region(NULL, AIRSPY_UPDATE_SCRATCH_OFFSET,
                                         AIRSPY_UPDATE_SCRATCH_SIZE);
    if (e != ESP_OK)
        ESP_LOGE(TAG, "scratch erase failed: %s", esp_err_to_name(e));
    return e;
}

esp_err_t airspy_update_scratch_write(size_t offset, const void *data, size_t len)
{
    if (!data || offset + len > AIRSPY_UPDATE_SCRATCH_SIZE)
        return ESP_ERR_INVALID_ARG;
    return esp_flash_write(NULL, data, AIRSPY_UPDATE_SCRATCH_OFFSET + (uint32_t)offset,
                           (uint32_t)len);
}

static esp_err_t scratch_read(size_t offset, void *data, size_t len)
{
    if (!data || offset + len > AIRSPY_UPDATE_SCRATCH_SIZE)
        return ESP_ERR_INVALID_ARG;
    return esp_flash_read(NULL, data, AIRSPY_UPDATE_SCRATCH_OFFSET + (uint32_t)offset,
                          (uint32_t)len);
}

/* --- transport glue to the USB host --- */
static int transport_write(const uint8_t *data, size_t len)
{
    return airspy_samba_write(data, len) == ESP_OK ? 0 : -1;
}
static int transport_read(uint8_t *data, size_t len, uint32_t timeout_ms)
{
    return airspy_samba_read(data, len, timeout_ms);
}
static void transport_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static const char *samba_err_str(airspy_samba_err_t e)
{
    switch (e) {
    case AIRSPY_SAMBA_OK:
        return "ok";
    case AIRSPY_SAMBA_ERR_IO:
        return "USB transfer failed";
    case AIRSPY_SAMBA_ERR_TIMEOUT:
        return "flash controller timeout";
    case AIRSPY_SAMBA_ERR_FLASH_CMD:
        return "flash command error";
    case AIRSPY_SAMBA_ERR_FLASH_LOCK:
        return "flash lock error";
    case AIRSPY_SAMBA_ERR_FLASH:
        return "flash error";
    case AIRSPY_SAMBA_ERR_UNSUPPORTED:
        return "receiver is not an ATSAM3U1";
    case AIRSPY_SAMBA_ERR_VERIFY:
        return "written image did not verify";
    case AIRSPY_SAMBA_ERR_BAD_ARG:
        return "invalid argument";
    default:
        return "unknown error";
    }
}

/* Write (page-by-page from the flash scratch area) and verify the image, then
 * set the boot-from-flash bit. Each page write uses erase-and-write (EWP), so
 * only the firmware pages are touched; the factory calibration page (255) is
 * left intact and does not need to be restored. */
static airspy_samba_err_t program_from_scratch(airspy_samba_t *samba)
{
    uint32_t pages = (uint32_t)((s_image_len + AIRSPY_SAMBA_PAGE_SIZE - 1) / AIRSPY_SAMBA_PAGE_SIZE);
    uint32_t total = pages * AIRSPY_SAMBA_PAGE_SIZE;

    uint8_t page[AIRSPY_SAMBA_PAGE_SIZE];
    uint8_t expected[AIRSPY_SAMBA_PAGE_SIZE];
    airspy_samba_err_t e;
    for (uint32_t p = 0; p < pages; p++) {
        size_t offset = (size_t)p * AIRSPY_SAMBA_PAGE_SIZE;
        size_t n = s_image_len - offset;
        if (n > AIRSPY_SAMBA_PAGE_SIZE)
            n = AIRSPY_SAMBA_PAGE_SIZE;
        if (scratch_read(offset, page, n) != ESP_OK)
            return AIRSPY_SAMBA_ERR_IO;
        /* Pad a partial last page with the flash erase value. */
        if (n < AIRSPY_SAMBA_PAGE_SIZE)
            memset(page + n, 0xFF, AIRSPY_SAMBA_PAGE_SIZE - n);
        e = airspy_samba_write_page(samba, p, page);
        if (e != AIRSPY_SAMBA_OK)
            return e;
        set_progress((uint8_t)(((p + 1) * AIRSPY_SAMBA_PAGE_SIZE * 100u) / (total * 2)));
    }

    for (uint32_t p = 0; p < pages; p++) {
        size_t offset = (size_t)p * AIRSPY_SAMBA_PAGE_SIZE;
        e = airspy_samba_read_page(samba, p, page);
        if (e != AIRSPY_SAMBA_OK)
            return e;
        /* The scratch tail is still erased (0xFF), so a full-page compare is valid. */
        if (scratch_read(offset, expected, AIRSPY_SAMBA_PAGE_SIZE) != ESP_OK)
            return AIRSPY_SAMBA_ERR_IO;
        if (memcmp(page, expected, AIRSPY_SAMBA_PAGE_SIZE) != 0)
            return AIRSPY_SAMBA_ERR_VERIFY;
        set_progress((uint8_t)(((total + (p + 1) * AIRSPY_SAMBA_PAGE_SIZE) * 100u) / (total * 2)));
    }

    return airspy_samba_set_boot_flash(samba);
}

static void run_program(void)
{
    set_state(AIRSPY_UPDATE_PROGRAMMING, "erasing and writing firmware");
    set_progress(0);

    airspy_samba_transport_t t = {
        .write = transport_write,
        .read = transport_read,
        .delay_ms = transport_delay,
    };
    airspy_samba_t *samba = airspy_samba_create(&t);
    if (!samba) {
        set_state(AIRSPY_UPDATE_FAILED, "out of memory");
        return;
    }

    airspy_samba_err_t e = airspy_samba_connect(samba, NULL);
    if (e == AIRSPY_SAMBA_OK && !airspy_samba_is_sam3u1(samba))
        e = AIRSPY_SAMBA_ERR_UNSUPPORTED;
    if (e == AIRSPY_SAMBA_OK)
        e = program_from_scratch(samba);
    if (e == AIRSPY_SAMBA_OK) {
        /* Watchdog-reset the receiver so it boots the new firmware from flash,
         * then reboot the ESP32 so the host re-enumerates it cleanly. */
        (void)airspy_samba_reset(samba);
        airspy_samba_destroy(samba);
        persist_phase(2, 0);
        ESP_LOGI(TAG, "flash complete; rebooting");
        esp_restart();
    }

    airspy_samba_destroy(samba);
    set_state(AIRSPY_UPDATE_FAILED, samba_err_str(e));
}

/* Recover a receiver left in SAM-BA mode (e.g. a previous update was
 * interrupted): set the boot-from-flash bit and reset, without touching the
 * firmware or the calibration page. */
static void run_recover(void)
{
    set_state(AIRSPY_UPDATE_RECOVERING, "restoring boot-from-flash");

    airspy_samba_transport_t t = {
        .write = transport_write,
        .read = transport_read,
        .delay_ms = transport_delay,
    };
    airspy_samba_t *samba = airspy_samba_create(&t);
    if (!samba) {
        set_state(AIRSPY_UPDATE_FAILED, "out of memory");
        return;
    }

    airspy_samba_err_t e = airspy_samba_connect(samba, NULL);
    if (e == AIRSPY_SAMBA_OK && !airspy_samba_is_sam3u1(samba))
        e = AIRSPY_SAMBA_ERR_UNSUPPORTED;
    if (e == AIRSPY_SAMBA_OK) {
        uint32_t sp = 0, vec = 0;
        if (airspy_samba_read_word(samba, AIRSPY_SAMBA_FLASH_BASE, &sp) == AIRSPY_SAMBA_OK &&
            airspy_samba_read_word(samba, AIRSPY_SAMBA_FLASH_BASE + 4, &vec) == AIRSPY_SAMBA_OK)
            ESP_LOGI(TAG, "flash vector SP=0x%08" PRIX32 " PC=0x%08" PRIX32, sp, vec);
        e = airspy_samba_set_boot_flash(samba);
    }
    if (e == AIRSPY_SAMBA_OK) {
        bool boot = false;
        if (airspy_samba_read_boot_flash(samba, &boot) == AIRSPY_SAMBA_OK)
            ESP_LOGI(TAG, "boot-from-flash bit is %s", boot ? "set" : "clear");
        (void)airspy_samba_reset(samba);
        airspy_samba_destroy(samba);
        persist_phase(2, 0);
        ESP_LOGI(TAG, "boot bit restored; rebooting");
        esp_restart();
    }

    airspy_samba_destroy(samba);
    set_state(AIRSPY_UPDATE_FAILED, samba_err_str(e));
}

bool airspy_update_submit(size_t len)
{
    if (len == 0 || len > AIRSPY_IMAGE_MAX_SIZE)
        return false;
    if (airspy_update_in_progress())
        return false;

    s_image_len = len;
    set_progress(0);
    /* Persist the update so it survives the reboot that follows the
     * reboot-to-bootloader. The calibration page is preserved by the page-wise
     * write, so no separate calibration restore is needed. */
    persist_phase(1, (uint32_t)len);
    set_state(AIRSPY_UPDATE_REBOOTING, "rebooting receiver to bootloader");
    return true;
}

void airspy_recover_request(void)
{
    /* Force recovery, overriding any stuck in-memory or persisted state. */
    clear_phase();
    set_progress(0);
    set_state(AIRSPY_UPDATE_RECOVERING, "restoring boot-from-flash");
}

void airspy_update_poll(void)
{
    if (!s_resume_done)
        resume_from_nvs();

    airspy_update_state_t state;
    portENTER_CRITICAL(&s_lock);
    state = s_state;
    portEXIT_CRITICAL(&s_lock);

    switch (state) {
    case AIRSPY_UPDATE_IDLE:
    case AIRSPY_UPDATE_DONE:
    case AIRSPY_UPDATE_FAILED:
        break;
    case AIRSPY_UPDATE_REBOOTING:
        if (airspy_samba_connected()) {
            /* Already in the bootloader (recovering from a previous attempt). */
            set_state(AIRSPY_UPDATE_WAIT_SAMBA, "bootloader already active");
            s_deadline = esp_timer_get_time() + SAMBA_TIMEOUT_US;
            break;
        }
        if (airspy_reboot_to_samba() != ESP_OK) {
            set_state(AIRSPY_UPDATE_FAILED, "could not send reboot command");
            break;
        }
        /* The receiver's own watchdog reboot is too fast for the host to notice.
         * Reboot the ESP32 so the USB host reset re-enumerates the bootloader. */
        ESP_LOGI(TAG, "rebooting ESP32 to re-enumerate the bootloader");
        esp_restart();
        break;
    case AIRSPY_UPDATE_WAIT_SAMBA:
        if (airspy_samba_connected())
            run_program();
        else if (esp_timer_get_time() > s_deadline) {
            clear_phase();
            set_state(AIRSPY_UPDATE_FAILED, "SAM-BA bootloader not detected");
        }
        break;
    case AIRSPY_UPDATE_BOOTING:
        if (airspy_connected()) {
            clear_phase();
            set_state(AIRSPY_UPDATE_DONE, "update complete");
        } else if (esp_timer_get_time() > s_deadline) {
            clear_phase();
            set_state(AIRSPY_UPDATE_FAILED, "receiver did not restart");
        }
        break;
    case AIRSPY_UPDATE_RECOVERING:
        if (airspy_samba_connected())
            run_recover();
        else
            set_state(AIRSPY_UPDATE_FAILED, "bootloader not connected");
        break;
    default:
        break;
    }
}

bool airspy_update_in_progress(void)
{
    airspy_update_state_t state;
    portENTER_CRITICAL(&s_lock);
    state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state != AIRSPY_UPDATE_IDLE && state != AIRSPY_UPDATE_DONE &&
           state != AIRSPY_UPDATE_FAILED;
}

void airspy_update_status(airspy_update_status_t *out)
{
    if (!out)
        return;
    portENTER_CRITICAL(&s_lock);
    out->state = s_state;
    out->progress = s_progress;
    out->message = s_message;
    portEXIT_CRITICAL(&s_lock);
}
