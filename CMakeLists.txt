cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_PROJECT_NAME desktop_power_monitor)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(${CMAKE_PROJECT_NAME} C ASM)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "core/src/*.c")
file(GLOB_RECURSE BSP_SOURCES CONFIGURE_DEPENDS "bsp/src/*.c")
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "app/src/*.c")
file(GLOB_RECURSE CMSIS_SOURCES CONFIGURE_DEPENDS "drivers/cmsis/*.c")
set(FW_SOURCES
    drivers/STM32F10x_StdPeriph_Driver/src/misc.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_dac.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_adc.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_dma.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_gpio.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_rcc.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_tim.c
    drivers/STM32F10x_StdPeriph_Driver/src/stm32f10x_usart.c
)

set(STARTUP_SCRIPT "startup/startup_stm32f103xe.s")
set(RUST_ALGOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rust_algos)
set(RUST_TARGET thumbv7m-none-eabi)
set(RUST_PROFILE release)
set(RUST_ALGOS_LIB ${RUST_ALGOS_DIR}/target/${RUST_TARGET}/${RUST_PROFILE}/librust_algos.a)
set(RUST_ALGOS_HEADER ${RUST_ALGOS_DIR}/include/rust_algos.h)
set(RUST_ALGOS_CBINDGEN_CONFIG ${RUST_ALGOS_DIR}/cbindgen.toml)

find_program(CBINDGEN_EXECUTABLE cbindgen)

if(CBINDGEN_EXECUTABLE)
    add_custom_command(
        OUTPUT ${RUST_ALGOS_HEADER}
        COMMAND ${CBINDGEN_EXECUTABLE}
            --config ${RUST_ALGOS_CBINDGEN_CONFIG}
            --crate rust_algos
            --output ${RUST_ALGOS_HEADER}
        WORKING_DIRECTORY ${RUST_ALGOS_DIR}
        DEPENDS
            ${RUST_ALGOS_CBINDGEN_CONFIG}
            ${RUST_ALGOS_DIR}/Cargo.toml
            ${RUST_ALGOS_DIR}/src/lib.rs
        COMMENT "Generating Rust algorithms C header"
        VERBATIM
    )
    add_custom_target(rust_algos_header DEPENDS ${RUST_ALGOS_HEADER})
else()
    if(NOT EXISTS ${RUST_ALGOS_HEADER})
        message(FATAL_ERROR "cbindgen is required to generate ${RUST_ALGOS_HEADER}")
    endif()
    message(WARNING "cbindgen not found; using existing ${RUST_ALGOS_HEADER}")
    add_custom_target(rust_algos_header)
endif()

add_custom_command(
    OUTPUT ${RUST_ALGOS_LIB}
    COMMAND cargo build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${RUST_ALGOS_DIR}
    DEPENDS
        ${RUST_ALGOS_DIR}/Cargo.toml
        ${RUST_ALGOS_DIR}/src/lib.rs
        ${RUST_ALGOS_DIR}/include/rust_algos.h
    COMMENT "Building Rust algorithms static library"
    VERBATIM
)

add_custom_target(rust_algos_build DEPENDS ${RUST_ALGOS_LIB})

add_library(rust_algos STATIC IMPORTED GLOBAL)
set_target_properties(rust_algos PROPERTIES
    IMPORTED_LOCATION ${RUST_ALGOS_LIB}
)
add_dependencies(rust_algos rust_algos_build)

add_executable(${CMAKE_PROJECT_NAME}
    ${CORE_SOURCES}
    ${BSP_SOURCES}
    ${APP_SOURCES}
    ${FW_SOURCES}
    ${CMSIS_SOURCES}
    middleware/alientek_lcd/lcd.c
    ${STARTUP_SCRIPT}
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    core/inc
    bsp/inc
    app/inc
    middleware/alientek_lcd
    ${RUST_ALGOS_DIR}/include
    drivers/cmsis
    drivers/STM32F10x_StdPeriph_Driver/inc
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_STDPERIPH_DRIVER
    STM32F10X_HD
)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F103RCT6_FLASH.ld)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    -ffunction-sections
    -fdata-sections
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Release>:-O2>
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    -specs=nosys.specs
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    -Wl,--gc-sections
    -Wl,--no-warn-execstack
)

add_dependencies(${CMAKE_PROJECT_NAME} rust_algos_build)
add_dependencies(${CMAKE_PROJECT_NAME} rust_algos_header)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE rust_algos)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)
