if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND extra_reqs esp_driver_ppa nvs_flash spi_flash esp_event esp_adc driver app_update fatfs)
else()
    list(APPEND extra_reqs spi_flash nvs_flash esp_event esp_adc driver app_update fatfs)
endif()
set(COMPONENT_SRCDIRS ". ugui")
set(COMPONENT_ADD_INCLUDEDIRS ".")
idf_component_register(SRCS
                       "display.c"
                       "input.c"
                       "main.c"
                       "sdcard.c"
                       "ugui/ugui.c"
                       INCLUDE_DIRS
                       "."
                       REQUIRES
                       esp_bsp_generic
                       PRIV_REQUIRES
                       ${extra_reqs})
component_compile_options(-DPROJECT_VER="${PROJECT_VER}")
