cmake_minimum_required(VERSION 3.16)

# ESP32-P4 only. The board has no other target.
set(COMPONENTS main)
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tempest_display)

# Bake firmware/spiffs/ into the `storage` partition. Regenerated on every
# build, so `python tools/build_icons.py` followed by `idf.py build` is the
# whole icon workflow. FLASH_IN_PROJECT makes `idf.py flash` write it too.
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
