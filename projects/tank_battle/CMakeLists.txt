# ============================================================
#  Tank battle  STM32H750XBH6  CMake 构建脚本 (arm-none-eabi-gcc)
#
#  用法:
#    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#    cmake --build build
#
#  产物: build/tank_battle.elf / .hex / .bin / .map
#  注意: 与 Keil (MDK-ARM) 工程完全独立,互不影响
# ============================================================

cmake_minimum_required(VERSION 3.20)

# ---------- 交叉编译环境 (必须在 project() 之前) ----------
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_ROOT "D:/STM32Tools/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi"
    CACHE PATH "arm-none-eabi 工具链根目录 (也允许通过 PATH 找到)")
find_program(CMAKE_C_COMPILER   NAMES arm-none-eabi-gcc     PATHS "${TOOLCHAIN_ROOT}/bin" REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES arm-none-eabi-gcc     PATHS "${TOOLCHAIN_ROOT}/bin" REQUIRED)
find_program(CMAKE_OBJCOPY      NAMES arm-none-eabi-objcopy PATHS "${TOOLCHAIN_ROOT}/bin" REQUIRED)
find_program(CMAKE_SIZE         NAMES arm-none-eabi-size    PATHS "${TOOLCHAIN_ROOT}/bin" REQUIRED)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

project(tank_battle C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- 源文件 ----------
set(CORE_SOURCES
    Core/Src/main.c
    Core/Src/board.c
    Core/Src/stm32h7xx_it.c
    Core/Src/system_stm32h7xx.c
    Core/Src/exit_run0mode.c
    Core/Src/tank_app.c
    Core/Src/tank_display.c
    Core/Src/board_sdram.c
    Core/Src/syscalls.c
    Game/battle.c
    Learning/tank_agent.c
    Game/arena_maps.c
)
set(HAL_SOURCES
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sdram.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_fmc.c
)
set(USER_SOURCES
    Drivers/User/Src/lcd_spi_200.c
    Drivers/User/Src/lcd_fonts.c
)
set(STARTUP_SOURCE startup_stm32h750xx.s)

add_executable(${PROJECT_NAME}
    ${CORE_SOURCES}
    ${HAL_SOURCES}
    ${USER_SOURCES}
    ${STARTUP_SOURCE}
)

# 可执行文件固定带 .elf 后缀 (launch.json / tasks.json / flash.ps1 均引用此文件名)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

# ---------- 宏定义 (与 Keil "STM32H750" target 一致) ----------
target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H750xx
    USE_PWR_LDO_SUPPLY
)

# ---------- 头文件路径 ----------
target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    Learning
    Game
    Drivers/User/Inc
    Drivers/STM32H7xx_HAL_Driver/Inc
    Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H7xx/Include
    Drivers/CMSIS/Include
)

# ---------- 编译选项 ----------
# -Wno-invalid-utf8: 部分源文件是 GBK 编码注释,字节原样保留,仅关闭警告
# -fno-strict-aliasing: 图像缓冲区的多类型转换 (uint16_t/u32) 兼容 Keil 行为
set(CPU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${CPU_FLAGS}
    -Wall
    -Wno-invalid-utf8
    -Wno-unused-parameter
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:MinSizeRel>:-Os>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g3>
)

# ---------- 链接选项 ----------
# nano.specs 使用精简 newlib；未启用浮点 printf，界面通过整数拆分显示小数。
target_link_options(${PROJECT_NAME} PRIVATE
    ${CPU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H750XBH6_FLASH.ld
    --specs=nosys.specs
    --specs=nano.specs
   
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${PROJECT_NAME}.map
)

# ---------- 后处理: 生成 .hex / .bin 并打印占用 ----------
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex    $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary  $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "生成 .hex / .bin 并输出内存占用"
    VERBATIM
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/STM32H750XBH6_FLASH.ld")
