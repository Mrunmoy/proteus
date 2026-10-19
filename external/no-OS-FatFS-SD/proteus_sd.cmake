# proteus_sd.cmake — build the vendored carlk3 no-OS-FatFS-SD library (Apache-2.0)
# for the Proteus RP2350 firmware. SPI + SDIO sources are all compiled (the SDIO
# path is unused at runtime — our hw_config declares an SPI-only card — but its
# symbols must link). FatFs config comes from the PORT's ffconf.h, which must
# precede this directory on the include path.
#
# Usage (from the rp2350 branch of the top-level CMakeLists):
#   include(external/no-OS-FatFS-SD/proteus_sd.cmake)
#   add_library(proteus_sd STATIC ${PROTEUS_SD_SOURCES} <port>/sd/hw_config.c ...)
#   target_include_directories(proteus_sd PUBLIC <port>/sd ${PROTEUS_SD_INCLUDES})

set(PROTEUS_SD_ROOT ${CMAKE_CURRENT_LIST_DIR})

set(PROTEUS_SD_SOURCES
    ${PROTEUS_SD_ROOT}/src/ff15/source/ff.c
    ${PROTEUS_SD_ROOT}/src/ff15/source/ffsystem.c
    ${PROTEUS_SD_ROOT}/src/ff15/source/ffunicode.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/dma_interrupts.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/sd_card.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/SDIO/rp2040_sdio.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/SDIO/sd_card_sdio.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/SPI/my_spi.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/SPI/sd_card_spi.c
    ${PROTEUS_SD_ROOT}/src/sd_driver/SPI/sd_spi.c
    ${PROTEUS_SD_ROOT}/src/src/crash.c
    ${PROTEUS_SD_ROOT}/src/src/crc.c
    ${PROTEUS_SD_ROOT}/src/src/f_util.c
    ${PROTEUS_SD_ROOT}/src/src/ff_stdio.c
    ${PROTEUS_SD_ROOT}/src/src/file_stream.c
    ${PROTEUS_SD_ROOT}/src/src/glue.c
    ${PROTEUS_SD_ROOT}/src/src/my_debug.c
    ${PROTEUS_SD_ROOT}/src/src/my_rtc.c
    ${PROTEUS_SD_ROOT}/src/src/sd_timeouts.c
    ${PROTEUS_SD_ROOT}/src/src/util.c
)

set(PROTEUS_SD_INCLUDES
    ${PROTEUS_SD_ROOT}/src/ff15/source
    ${PROTEUS_SD_ROOT}/src/sd_driver
    ${PROTEUS_SD_ROOT}/src/include
    ${PROTEUS_SD_ROOT}/include
)
