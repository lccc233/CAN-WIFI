cmake_minimum_required(VERSION 3.16)

# 最小化构建：main 显式拉入 PSRAM 支持（esp_psram 提供 CONFIG_SPIRAM 菜单）
set(COMPONENTS main esp_psram)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(can_monitor)
