set(src_dirs LED/src KEY/src EXIT/src )

set(include_dirs LED/include KEY/include EXIT/include )

set(requires esp_driver_gpio )

idf_component_register(SRC_DIRS ${src_dirs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)