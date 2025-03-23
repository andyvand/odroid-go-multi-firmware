#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <driver/rtc_io.h>
#include <bsp/esp-bsp.h>
#include <string.h>

#include "display.h"

esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t panel_io_handle = NULL;

static SemaphoreHandle_t lcd_semaphore;
static int max_chunk_height = 4;  // Configurable chunk height

#ifdef CONFIG_IDF_TARGET_ESP32P4
static ppa_client_handle_t ppa_srm_handle = NULL;  // PPA client handle
static uint8_t *ppa_out_buf = NULL;  // Reusable PPA output buffer
static size_t ppa_out_buf_size = 0;  // Size of the PPA output buffer

#ifndef SCALE_FACTOR
int scale_factor = 1;
float scale_factor_float = 1.0;
// Workaround to quickly pass scaling to PPA
// This should be probably handled on Render level
void set_scale_factor(int factor, float factor_float) {
    scale_factor = factor;
    scale_factor_float = factor_float;
}
#endif
#else
static uint16_t *rgb565_buffer = NULL;
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4
static bool lcd_event_callback(esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    xSemaphoreGive(lcd_semaphore);
    return false;
}
#else
static bool lcd_event_callback(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event_data, void *user_ctx)
{
    xSemaphoreGive(lcd_semaphore);
    return false;
}
#endif

void ili9341_writeLE(const uint16_t *buffer)
{
    const int left = 0;
    const int top = SCREEN_OFFSET_TOP;
    const int width = SCREEN_WIDTH;
    const int height = SCREEN_HEIGHT;

    for (int y = 0; y < height; y += max_chunk_height) {
        int chunk_height = (y + max_chunk_height > height) ? (height - y) : max_chunk_height;

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, left, top + y, width, y + chunk_height, (&buffer[width * y])));
        xSemaphoreTake(lcd_semaphore, portMAX_DELAY);
    }
}

void ili9341_deinit()
{
    // Delete the semaphore
    if (lcd_semaphore) {
        vSemaphoreDelete(lcd_semaphore);
        lcd_semaphore = NULL;
    }

#ifdef CONFIG_IDF_TARGET_ESP32P4
    // Free PPA output buffer
    if (ppa_out_buf) {
        heap_caps_free(ppa_out_buf);
        ppa_out_buf = NULL;
        ppa_out_buf_size = 0;
    }

    if (ppa_srm_handle) {
        ESP_ERROR_CHECK(ppa_unregister_client(ppa_srm_handle));
        ppa_srm_handle = NULL;
    }
#else
    // Free the RGB565 buffer
    if (rgb565_buffer) {
        heap_caps_free(rgb565_buffer);
        rgb565_buffer = NULL;
    }
#endif

    gpio_reset_pin(LCD_PIN_NUM_DC);
    gpio_reset_pin(LCD_PIN_NUM_BCKL);
}

void ili9341_init()
{
#ifdef CONFIG_IDF_TARGET_ESP32P4
    const bsp_display_config_t bsp_disp_cfg = {
        .dsi_bus = {
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        }
    };
#else
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = (BSP_LCD_H_RES * BSP_LCD_V_RES) * sizeof(uint16_t),
    };
#endif

    ESP_ERROR_CHECK(bsp_display_new(&bsp_disp_cfg, &panel_handle, &panel_io_handle));

    ESP_ERROR_CHECK(bsp_display_backlight_on());

#ifndef CONFIG_IDF_TARGET_ESP32P4
    esp_lcd_panel_disp_on_off(panel_handle, true);
#endif

    ESP_LOGI(__func__, "LCD Initialized.");

#ifndef CONFIG_IDF_TARGET_ESP32P4
    // Allocate RGB565 buffer in IRAM
    rgb565_buffer = heap_caps_malloc(SCREEN_WIDTH * max_chunk_height * sizeof(uint16_t), MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
    if (!rgb565_buffer) {
        ESP_LOGE(__func__, "Failed to create LCD RGB565 buffer\n");
        return;
    }
#endif

    // Create a semaphore to synchronize LCD transactions
    lcd_semaphore = xSemaphoreCreateBinary();
    if (!lcd_semaphore) {
        ESP_LOGE(__func__, "Failed to create LCD semaphore\n");
        return;
    }

    // Initialize PPA (only for ESP32-P4)
#ifdef CONFIG_IDF_TARGET_ESP32P4
    if (!ppa_srm_handle) {
        ppa_client_config_t ppa_srm_config = {
            .oper_type = PPA_OPERATION_SRM,
            .max_pending_trans_num = 1,
        };
        ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    }

    if (scale_factor != 1) {
        // Allocate reusable PPA output buffer
        ppa_out_buf_size = (SCREEN_WIDTH * scale_factor) * (max_chunk_height * scale_factor) * sizeof(uint16_t);  // 2x scaling
        ppa_out_buf = heap_caps_malloc(ppa_out_buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!ppa_out_buf) {
            ESP_LOGE(__func__, "Failed to allocate PPA output buffer")
            return;
        }
    }
    const esp_lcd_dpi_panel_event_callbacks_t callback = {
        .on_color_trans_done = lcd_event_callback,
    };
    esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &callback, NULL);
#else
#if !CONFIG_BSP_DISPLAY_DRIVER_QEMU
    esp_lcd_panel_io_register_event_callbacks(panel_io_handle, &(esp_lcd_panel_io_callbacks_t){ .on_color_trans_done = lcd_event_callback }, NULL);
#endif
#endif

    ESP_LOGI(__func__, "LCD Buffer and event callbacks created.");
}
