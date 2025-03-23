#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include <diskio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <bsp/esp-bsp.h>

#include <dirent.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "sdcard.h"

#if CONFIG_HW_ODROID_GO
#define SD_SLOT SPI2_HOST
#else
#define SD_SLOT SPI3_HOST
#endif

extern esp_err_t ff_diskio_get_drive(BYTE* out_pdrv);
extern void ff_diskio_register_sdmmc(unsigned char pdrv, sdmmc_card_t* card);

inline static void swap(char** a, char** b)
{
    char* t = *a;
    *a = *b;
    *b = t;
}

static int strcicmp(char const *a, char const *b)
{
    for (;; a++, b++)
    {
        int d = tolower((int)*a) - tolower((int)*b);
        if (d != 0 || !*a) return d;
    }
}

static int partition (char* arr[], int low, int high)
{
    char* pivot = arr[high];
    int i = (low - 1);

    for (int j = low; j <= high- 1; j++)
    {
        if (strcicmp(arr[j], pivot) < 0)
        {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

static void quick_sort(char* arr[], int low, int high)
{
    if (low < high)
    {
        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

static void sort_files(char** files, int count)
{
    if (count > 1)
    {
        quick_sort(files, 0, count - 1);
    }
}


int odroid_sdcard_files_get(const char* path, const char* extension, char*** filesOut)
{
    const int MAX_FILES = 1024;

    int count = 0;
    char** result = (char**)malloc(MAX_FILES * sizeof(void*));
    if (!result) abort();


    DIR *dir = opendir(path);
    if( dir == NULL )
    {
        ESP_LOGE(__func__, "opendir failed.");
        return 0;
    }

    int extensionLength = strlen(extension);
    if (extensionLength < 1) abort();

    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        size_t len = strlen(entry->d_name);

        if (len < extensionLength)
            continue;

        if (entry->d_name[0] == '.')
            continue;

        if (strcasecmp(extension, &entry->d_name[len - extensionLength]) != 0)
            continue;

        if (!(result[count++] = strdup(entry->d_name)))
            abort();

        if (count >= MAX_FILES)
            break;
    }

    closedir(dir);

    sort_files(result, count);

    *filesOut = result;
    return count;
}

void odroid_sdcard_files_free(char** files, int count)
{
    for (int i = 0; i < count; ++i)
    {
        free(files[i]);
    }

    free(files);
}

esp_err_t odroid_sdcard_open(void)
{
    esp_err_t ret = bsp_sdcard_mount();
    
    if (ret != ESP_OK)
    {
        ESP_LOGE(__func__, "bsp_sdcard_mount failed (%d)", ret);
    }

    return ret;
}

esp_err_t odroid_sdcard_close(void)
{
    esp_err_t ret = bsp_sdcard_unmount();

    if (ret != ESP_OK)
    {
        ESP_LOGE(__func__, "bsp_sdcard_unmount failed (%d)", ret);
    }

    return ret;
}

esp_err_t odroid_sdcard_format(int fs_type)
{
    esp_err_t err = ESP_FAIL;
    sdspi_dev_handle_t handle;
    const char *errmsg = "success!";
    sdmmc_card_t card;
    void *buffer = malloc(4096);
    DWORD partitions[] = {100, 0, 0, 0};
    BYTE drive = 0xFF;

#ifdef TARGET_MRGC_G32
    sdmmc_slot_config_t host_config = SDMMC_SLOT_CONFIG_DEFAULT();
    /* Note: For small devkits there may be no pullups on the board.
       This enables the internal pullups to help evaluate the driver quickly.
       However the internal pullups are not sufficient and not reliable,
       please make sure external pullups are connected to the bus in your
       real design.
    */
    //slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    //Initialize all pins to avoid them floating
    //Set slot width to 4 to ignore other pin config on S3, which support at most 8 lines
    host_config.width = 4;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    host_config.clk = CONFIG_BSP_SD_CLK;
    host_config.cmd = CONFIG_BSP_SD_CMD;
    host_config.d0 = CONFIG_BSP_SD_D0;
    host_config.d1 = CONFIG_BSP_SD_D1;
    host_config.d2 = CONFIG_BSP_SD_D2;
    host_config.d3 = CONFIG_BSP_SD_D3;
#endif  // CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
#else
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_HW_SD_PIN_NUM_MOSI,
        .miso_io_num = CONFIG_HW_SD_PIN_NUM_MISO,
        .sclk_io_num = CONFIG_HW_SD_PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    sdspi_device_config_t host_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    host_config.gpio_cs = CONFIG_HW_SD_PIN_NUM_CS;
    esp_err_t ret = spi_bus_initialize(SD_SLOT, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(__func__, "Failed to initialize bus.");
        return ret;
    }
#endif

#ifndef TARGET_MRGC_G32
    sdmmc_host_t config = SDSPI_HOST_DEFAULT();
#endif

    if (buffer == NULL) {
        return false;
    }

    odroid_sdcard_close();

    err = ff_diskio_get_drive(&drive);
    if (drive == 0xFF) {
        errmsg = "ff_diskio_get_drive() failed";
        goto _cleanup;
    }

#ifndef TARGET_MRGC_G32
    err = (*config.init)();
    if (err != ESP_OK) {
        errmsg = "config.init() failed";
        goto _cleanup;
    }
#else
    err = (*host_config.init)();
    if (err != ESP_OK) {
        errmsg = "host_config.init() failed";
        goto _cleanup;
    }
#endif

#ifdef TARGET_MRGC_G32
    err = sdmmc_host_init();

    if (err != ESP_OK) {
        errmsg = "sdmmc_host_init() failed";
        goto _cleanup;
    }

    err = sdmmc_host_init_slot(host_config.slot, &slot_config);
#else
    err = sdspi_host_init();

    if (err != ESP_OK) {
        errmsg = "sdspi_host_init() failed";
        goto _cleanup;
    }

    err = sdspi_host_init_device(&host_config, &handle);

    config.slot = handle;
#endif

    if (err != ESP_OK) {
        errmsg = "sdspi_host_init_device() failed";
        goto _cleanup;
    }

    err = sdmmc_card_init(&config, &card);
    if (err != ESP_OK) {
        errmsg = "sdmmc_card_init() failed";
        goto _cleanup;
    }

    ff_diskio_register_sdmmc(drive, &card);

    ESP_LOGI(__func__, "partitioning card %d", drive);
    if (f_fdisk(drive, partitions, buffer) != FR_OK) {
        errmsg = "f_fdisk() failed";
        err = ESP_FAIL;
        goto _cleanup;
    }

    ESP_LOGI(__func__, "formatting card %d", drive);
    char path[3] = {(char)('0' + drive), ':', 0};
    const MKFS_PARM fsparm = {
        fs_type ? FM_EXFAT : FM_FAT32, // Format
        2, // Number of FATs
        16, // Alignment
        2, // Number of root entries
        16 * 1024 // Cluster size
    };
    if (f_mkfs(path, &fsparm, buffer, 4096) != FR_OK) {
        errmsg = "f_mkfs() failed";
        err = ESP_FAIL;
        goto _cleanup;
    }

    err = ESP_OK;

_cleanup:

    if (err == ESP_OK) {
        ESP_LOGI(__func__, "%s", errmsg);
    } else {
        ESP_LOGE(__func__, "%s (%d)", errmsg, err);
    }

    free(buffer);

#ifdef TARGET_MRGC_G32
    host_config.deinit();
#else
    config.deinit();
#endif

    ff_diskio_register_sdmmc(drive, NULL);

    return err;
}
