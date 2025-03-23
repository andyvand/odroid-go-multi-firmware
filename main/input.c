#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <driver/i2c.h>
#include <esp_log.h>

#include "input.h"

#ifdef TARGET_MRGC_G32
#define TRY(x) if ((err = (x)) != ESP_OK) { goto fail; }

static bool rg_i2c_read(uint8_t addr, int reg, void *read_data, size_t read_len)
{
    esp_err_t err = ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (reg >= 0)
    {
        TRY(i2c_master_start(cmd));
        TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true));
        TRY(i2c_master_write_byte(cmd, (uint8_t)reg, true));
    }
    TRY(i2c_master_start(cmd));
    TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true));
    TRY(i2c_master_read(cmd, read_data, read_len, I2C_MASTER_LAST_NACK));
    TRY(i2c_master_stop(cmd));
    TRY(i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(500)));
    i2c_cmd_link_delete(cmd);
    return true;

fail:
    ESP_LOGE(__func__, "Read from 0x%02x failed. reg=%d, err=0x%x\n", addr, reg, err);
    return false;
}
#else
#if CONFIG_HW_ODROID_GO
#define ODROID_GAMEPAD_IO_X ADC1_CHANNEL_6
#define ODROID_GAMEPAD_IO_Y ADC1_CHANNEL_7
#define ODROID_GAMEPAD_IO_SELECT GPIO_NUM_27
#define ODROID_GAMEPAD_IO_START GPIO_NUM_39
#define ODROID_GAMEPAD_IO_A GPIO_NUM_32
#define ODROID_GAMEPAD_IO_B GPIO_NUM_33
#define ODROID_GAMEPAD_IO_MENU GPIO_NUM_13
#define ODROID_GAMEPAD_IO_VOLUME GPIO_NUM_0
#else
#define ODROID_GAMEPAD_IO_UP GPIO_NUM_1
#define ODROID_GAMEPAD_IO_DOWN GPIO_NUM_2
#define ODROID_GAMEPAD_IO_LEFT GPIO_NUM_3
#define ODROID_GAMEPAD_IO_RIGHT GPIO_NUM_4
#define ODROID_GAMEPAD_IO_SELECT GPIO_NUM_38
#define ODROID_GAMEPAD_IO_START GPIO_NUM_37
#define ODROID_GAMEPAD_IO_A GPIO_NUM_5
#define ODROID_GAMEPAD_IO_B GPIO_NUM_6
#define ODROID_GAMEPAD_IO_MENU GPIO_NUM_39
#define ODROID_GAMEPAD_IO_VOLUME -1
#endif
#endif

#if CONFIG_HW_ODROID_GO
adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
};
adc_oneshot_chan_cfg_t config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
};
#endif

static uint32_t gamepad_state = 0;

uint32_t input_read_raw(void)
{
    uint32_t state = 0;

#ifdef TARGET_MRGC_G32
    uint8_t data[5];
    if (rg_i2c_read(0x20, -1, &data, 5))
    {
        int buttons = ~((data[2] << 8) | data[1]);

        if (buttons & (1 << 2)) state |= (1 << ODROID_INPUT_UP);
        if (buttons & (1 << 3)) state |= (1 << ODROID_INPUT_DOWN);
        if (buttons & (1 << 4)) state |= (1 << ODROID_INPUT_LEFT);
        if (buttons & (1 << 5)) state |= (1 << ODROID_INPUT_RIGHT);
        if (buttons & (1 << 8)) state |= (1 << ODROID_INPUT_MENU);
        // if (buttons & (1 << 0)) state |= (1 << ODROID_INPUT_OPTION);
        if (buttons & (1 << 1)) state |= (1 << ODROID_INPUT_SELECT);
        if (buttons & (1 << 0)) state |= (1 << ODROID_INPUT_START);
        if (buttons & (1 << 6)) state |= (1 << ODROID_INPUT_A);
        if (buttons & (1 << 7)) state |= (1 << ODROID_INPUT_B);
    }
#else
#if CONFIG_HW_ODROID_GO
    int joyX = 0;
    int joyY = 0;

    adc_oneshot_read(adc1_handle, ODROID_GAMEPAD_IO_X, &joyX);
    adc_oneshot_read(adc1_handle, ODROID_GAMEPAD_IO_Y, &joyY);

    if (joyX > 2048 + 1024)
        state |= (1 << ODROID_INPUT_LEFT);
    else if (joyX > 1024)
        state |= (1 << ODROID_INPUT_RIGHT);

    if (joyY > 2048 + 1024)
        state |= (1 << ODROID_INPUT_UP);
    else if (joyY > 1024)
        state |= (1 << ODROID_INPUT_DOWN);
#else
    if (ODROID_GAMEPAD_IO_UP != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_UP)) ? (1 << ODROID_INPUT_UP) : 0;

    if (ODROID_GAMEPAD_IO_DOWN != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_DOWN)) ? (1 << ODROID_INPUT_DOWN) : 0;

    if (ODROID_GAMEPAD_IO_LEFT != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_LEFT)) ? (1 << ODROID_INPUT_LEFT) : 0;

    if (ODROID_GAMEPAD_IO_RIGHT != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_RIGHT)) ? (1 << ODROID_INPUT_RIGHT) : 0;
#endif

    if (ODROID_GAMEPAD_IO_SELECT != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_SELECT)) ? (1 << ODROID_INPUT_SELECT) : 0;

    if (ODROID_GAMEPAD_IO_START != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_START)) ? (1 << ODROID_INPUT_START) : 0;

    if (ODROID_GAMEPAD_IO_A != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_A)) ? (1 << ODROID_INPUT_A) : 0;

    if (ODROID_GAMEPAD_IO_B != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_B)) ? (1 << ODROID_INPUT_B) : 0;

    if (ODROID_GAMEPAD_IO_MENU != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_MENU)) ? (1 << ODROID_INPUT_MENU) : 0;

    if (ODROID_GAMEPAD_IO_VOLUME != -1)
        state |= (!gpio_get_level(ODROID_GAMEPAD_IO_VOLUME)) ? (1 << ODROID_INPUT_VOLUME) : 0;
#endif

    return state;
}

int input_wait_for_button_press(int ticks)
{
    uint32_t previousState = gamepad_state;
    uint32_t timeout = xTaskGetTickCount() + ticks;

    while (true)
    {
        uint32_t state = gamepad_state;

        for (int i = 0; i < ODROID_INPUT_MAX; i++)
        {
            if (!(previousState & (1 << i)) && (state & (1 << i))) {
                return i;
            }
        }

        if (ticks > 0 && timeout < xTaskGetTickCount()) {
            break;
        }

        previousState = state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return -1;
}

static void input_task(void *arg)
{
    uint8_t debounce[ODROID_INPUT_MAX];

    // Initialize state
    for (int i = 0; i < ODROID_INPUT_MAX; ++i)
    {
        debounce[i] = 0xff;
    }

    while (1)
    {
        // Read hardware
        uint32_t state = input_read_raw();

        // Debounce
        for (int i = 0; i < ODROID_INPUT_MAX; ++i)
        {
            debounce[i] <<= 1;
            debounce[i] |= (state >> i) & 1;
            switch (debounce[i] & 0x03)
            {
                case 0x00:
                    gamepad_state &= ~(1 << i);
                    break;

                case 0x03:
                    gamepad_state |= (1 << i);
                    break;

                default:
                    // ignore
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

void input_init(void)
{
#ifdef TARGET_MRGC_G32
    const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_21,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = GPIO_NUM_22,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 200000,
    };
    esp_err_t err = ESP_FAIL;

    TRY(i2c_param_config(I2C_NUM_0, &i2c_config));
    TRY(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(__func__, "I2C driver ready (SDA:%d SCL:%d).\n", i2c_config.sda_io_num, i2c_config.scl_io_num);
    fail:
#else
#if CONFIG_HW_ODROID_GO
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ODROID_GAMEPAD_IO_X, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ODROID_GAMEPAD_IO_Y, &config));
#else
    if (ODROID_GAMEPAD_IO_UP != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_UP, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_UP, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_DOWN != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_DOWN, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_DOWN, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_LEFT != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_LEFT, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_LEFT, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_RIGHT != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_RIGHT, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_RIGHT, GPIO_PULLUP_ONLY);
    }
#endif

    if (ODROID_GAMEPAD_IO_SELECT != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_SELECT, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_SELECT, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_START != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_START, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_START, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_A != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_A, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_A, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_B != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_B, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_B, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_MENU != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_MENU, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_MENU, GPIO_PULLUP_ONLY);
    }

    if (ODROID_GAMEPAD_IO_VOLUME != -1)
    {
        gpio_set_direction(ODROID_GAMEPAD_IO_VOLUME, GPIO_MODE_INPUT);
        gpio_set_pull_mode(ODROID_GAMEPAD_IO_VOLUME, GPIO_PULLUP_ONLY);
    }
#endif

    // Start background polling
    xTaskCreatePinnedToCore(&input_task, "input_task", 1024 * 2, NULL, 5, NULL, 1);

    ESP_LOGI(__func__, "done.");
}
