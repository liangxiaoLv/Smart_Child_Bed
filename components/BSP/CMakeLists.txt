file(GLOB src_dirs "${CMAKE_CURRENT_SOURCE_DIR}/*/src")
file(GLOB include_dirs "${CMAKE_CURRENT_SOURCE_DIR}/*/include")

set(requires 
        esp_driver_gpio 
        esp_driver_uart 
        esp_timer 
        esp_driver_gptimer 
        esp_driver_ledc
    )

idf_component_register(SRC_DIRS ${src_dirs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)