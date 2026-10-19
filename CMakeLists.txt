cmake_minimum_required(VERSION 3.13)

# ── Port selection ─────────────────────────────────────────────────────────
# One tree, two builds:
#   PORT=rp2350  (default) → firmware .uf2 via the Pico SDK (Docker toolchain)
#   PORT=host              → native gtest harness + headless demo (dev/test)
# The host build doubles as the portability proof: core/app sources carry NO
# hardware headers, so a leak fails to compile here.
set(PORT rp2350 CACHE STRING "Target port: rp2350 | host")

# ── Portable sources (shared by every port) ────────────────────────────────
set(CORE_SOURCES
    src/common/Debouncer.cpp
    src/common/device_info.c        # shared identity contract
    src/common/FrameScaler.cpp      # source frame → display RGB565 blit (host-tested)
)

if(PORT STREQUAL "rp2350")

    # The board is a Waveshare RP2350-PiZero (RP2350B / QFN-80, 48 GPIOs, 16 MB).
    # The RP2350B variant is required for DVI: its TMDS pins live on GPIO 32..39,
    # which the RP2350A (pico2, 30 GPIOs) cannot reach. The board header is
    # vendored under src/port/rp2350/boards (it sets PICO_RP2350A 0). Earlier
    # targets used pico2 (RP2350A); they only touch GPIO < 30, so the switch is
    # transparent to them while it unblocks the DVI output.
    set(PICO_PLATFORM rp2350 CACHE STRING "RP2350 (the PiZero is RP2350B)")
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/port/rp2350/boards)
    set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "Board type")
    include(pico_sdk_import.cmake)

    project(proteus C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # GLOBAL defs mirroring the vendor RP2350-PiZero SD demo. The vendor compiles
    # the SD driver straight INTO its executable (INTERFACE lib) so it inherits
    # these; we build a separate static lib, so we must set them globally to get
    # the same behavior. PICO_PIO_USE_GPIO_BASE lets PIO reach GPIO 40-43 (SD
    # data lines); PICO_STACK_SIZE bump matches the demo (a too-small stack can
    # corrupt the on-stack PIO pin config during SDIO init -> CMD8 timeout).
    add_compile_definitions(
        PICO_PIO_USE_GPIO_BASE=1
        PARAM_ASSERTIONS_ENABLE_ALL=1
        PICO_MALLOC_PANIC=1
        PICO_USE_STACK_GUARDS=1
        PICO_STACK_SIZE=0x1000
        PICO_CORE1_STACK_SIZE=0x800
        USE_PRINTF)

    pico_sdk_init()

    # ── microSD: vendored carlk3 no-OS-FatFS-SD (Apache-2.0) + Proteus glue ─
    # FatFs config (ffconf.h) and the board hw_config live in the port; they go
    # FIRST on the include path so the port's ffconf.h wins over the vendor copy.
    include(external/no-OS-FatFS-SD/proteus_sd.cmake)
    add_library(proteus_sd STATIC
        ${PROTEUS_SD_SOURCES}
        src/port/rp2350/sd/hw_config.c
        src/port/rp2350/sd/SdCard.cpp
    )
    target_include_directories(proteus_sd PUBLIC
        src
        src/port/rp2350
        src/port/rp2350/sd                # port ffconf.h (overrides vendor)
        ${PROTEUS_SD_INCLUDES}
    )
    target_link_libraries(proteus_sd
        pico_stdlib hardware_spi hardware_dma hardware_pio pico_aon_timer
        cmsis_core)  # util.h pulls in RP2350.h (CMSIS device header)
    # PICO_PIO_USE_GPIO_BASE=1 is REQUIRED for the SDIO driver: it enables the
    # SDK's pio_set_gpio_base() so PIO1 can address GPIO >31. Our SD data lines
    # are at GPIO 40-43, so without this the driver's pio_set_gpio_base(pio1,16)
    # is a no-op, the SDIO state machines drive the wrong pins, and the card
    # never responds (CMD8 timeout). The vendor demo defines this globally.
    target_compile_definitions(proteus_sd PUBLIC
        PICO_MAX_SHARED_IRQ_HANDLERS=8u
        PICO_PIO_USE_GPIO_BASE=1)
    # Generate the SDIO PIO header (CMD/CLK + data RX/TX state machines) FROM
    # SOURCE, exactly like the vendor demo, instead of trusting a pre-vendored
    # rp2040_sdio.pio.h that could be stale/wrong for RP2350 (a broken PIO
    # program loads silently and the card never clocks -> f_mount FR_NOT_READY).
    pico_generate_pio_header(proteus_sd
        ${CMAKE_CURRENT_SOURCE_DIR}/external/no-OS-FatFS-SD/src/sd_driver/SDIO/rp2040_sdio.pio)

    # ── DVI/HDMI output (vendored PicoDVI libdvi) ──────────────────────────
    # PIO+DMA+software-TMDS DVI on GPIO 32..39 (no HSTX on this board). The PIO
    # GPIO base must be enabled to reach pins >= 32 on the RP2350B.
    add_compile_definitions(PICO_PIO_USE_GPIO_BASE=1)
    add_subdirectory(external/picodvi/libdvi)

    # Standalone DVI bring-up firmware: 640x480p60 colour-bar test pattern.
    # Verifies the libdvi path + board pin map without the emulator or LCD.
    add_executable(proteus_dvi
        ${CORE_SOURCES}
        src/port/rp2350/dvi_main.cpp
        src/port/rp2350/DviDisplay.cpp
    )
    target_include_directories(proteus_dvi PRIVATE src src/port/rp2350)
    target_link_libraries(proteus_dvi
        pico_stdlib pico_multicore hardware_dma hardware_pio hardware_pwm libdvi)
    target_compile_definitions(proteus_dvi PRIVATE PICO_FLASH_SIZE_BYTES=16777216)
    pico_enable_stdio_usb(proteus_dvi 1)
    pico_enable_stdio_uart(proteus_dvi 0)
    pico_add_extra_outputs(proteus_dvi)

    # ── LCD button-coupling isolation test ──────────────────────────────────
    # Static colour bars on the LCD + button GPIOs configured but never read.
    # If a button press shifts the static image, the fault is electrical coupling
    # on the LCD wiring, not software. See src/app/lcdtest/main.cpp.
    add_executable(lcd_buttontest
        src/app/lcdtest/main.cpp
        src/port/rp2350/phy/SpiDisplayBus.cpp
    )
    target_include_directories(lcd_buttontest PRIVATE src src/port/rp2350)
    target_link_libraries(lcd_buttontest
        pico_stdlib hardware_spi hardware_pwm hardware_clocks)
    target_compile_definitions(lcd_buttontest PRIVATE PICO_FLASH_SIZE_BYTES=16777216)
    pico_enable_stdio_usb(lcd_buttontest 1)
    pico_enable_stdio_uart(lcd_buttontest 1)
    pico_add_extra_outputs(lcd_buttontest)

    # ── Proteus TestAgent (tester firmware) ────────────────────────────────
    # Event-driven manufacturing-test agent: TinyUSB CDC carries the aether RPC
    # link (we own TinyUSB directly — no pico_stdio_usb — so CDC is binary, not
    # stdio). Separate .uf2; the DemoApp firmware above is untouched.
    add_executable(proteus_mfg
        src/app/mfg/main.cpp
        src/app/mfg/usb_descriptors.c
        src/app/TestAgent.cpp
        src/common/Debouncer.cpp        # used by the Buttons driver
        src/common/device_info.c        # GetDeviceInfo reads the identity block (XIP)
        # The SAME generic drivers + PHYs the gift app ships (driver-identity): the
        # ops run the real shipping code paths, not a parallel test-only copy.
        src/port/rp2350/phy/SpiDisplayBus.cpp   # LCD: drivers::St7789<SpiDisplayBus>
        src/port/rp2350/phy/GpioButtonPort.cpp  # buttons: drivers::Buttons<GpioButtonPort>
        src/port/rp2350/phy/PwmSpeakerOut.cpp   # audio: drivers::PwmSpeaker<PwmSpeakerOut>
        src/port/rp2350/DviDisplay.cpp          # HDMI test: real DVI driver
        external/aether/aether-lite/aether_lite_crc.c
        external/aether/aether-lite/aether_lite_framing.c
        external/aether/aether-lite/aether_lite_dispatch.c
        proto/gen/aether_lite/TestStation.c
    )
    target_include_directories(proteus_mfg PRIVATE
        src
        src/port/rp2350
        src/app/mfg
        external/aether/aether-lite
        proto/gen/aether_lite
    )
    target_link_libraries(proteus_mfg pico_stdlib tinyusb_device tinyusb_board
                          hardware_spi hardware_pwm hardware_dma hardware_pio
                          pico_multicore proteus_sd libdvi)
    target_compile_definitions(proteus_mfg PRIVATE PICO_FLASH_SIZE_BYTES=16777216)
    pico_add_extra_outputs(proteus_mfg)

    # ── Proteus PIO-USB bring-up (Stage 1) — the second USB-C port (J4) ─────
    # Native USB stays a stdio CDC (debug + picotool reset); PIO-USB runs the
    # Pico-PIO-USB device stack on core1 (GP28=D+/GP29=D-). Separate .uf2.
    add_subdirectory(external/Pico-PIO-USB pico_pio_usb)
    add_executable(proteus_pio src/app/pio_usb/main.c)
    target_link_libraries(proteus_pio pico_stdlib pico_multicore pico_pio_usb hardware_clocks)
    target_compile_definitions(proteus_pio PRIVATE PICO_FLASH_SIZE_BYTES=16777216)
    pico_enable_stdio_usb(proteus_pio 1)
    pico_enable_stdio_uart(proteus_pio 0)
    pico_add_extra_outputs(proteus_pio)

    # ── Proteus GAME BOY (DMG) unified firmware — SD menu + LCD/HDMI output ────
    # Peanut-GB (MIT) core + minigb_apu → St7789<SpiDisplayBus> (LCD, core1 present
    # worker) OR DviDisplay (HDMI, core1 TMDS encoder, core0 present); PwmSpeaker out.
    # SD menu: mount card, list .gb files, browse with D-pad, load into 128 KB
    # SRAM buffer, unmount. THEN overclock + bring up chosen output + run game.
    # Output selection: hold SELECT at power-on → HDMI; release → LCD (default).
    # NOT copy_to_ram: DVI's 300 KB double-buffer + 128 KB ROM buffer leave ~92 KB
    # free for gb_s + audio + framebuf; XIP code fetch is fine (GB is light).
    add_executable(proteus_gb
        src/common/Debouncer.cpp
        src/common/FrameScaler.cpp
        src/app/gb/main.cpp
        external/peanut-gb/minigb_apu.c
        src/port/rp2350/phy/SpiDisplayBus.cpp   # LCD: drivers::St7789<SpiDisplayBus>
        src/port/rp2350/phy/GpioButtonPort.cpp  # buttons: drivers::Buttons<GpioButtonPort>
        src/port/rp2350/phy/PwmSpeakerOut.cpp   # audio: drivers::PwmSpeaker<PwmSpeakerOut>
        src/port/rp2350/flash/FlashRom.cpp     # SD→QSPI-flash ROM streamer (XIP)
        src/port/rp2350/flash/DiagStore.cpp    # flash-backed diag capsule storage adapter
        src/port/rp2350/DviDisplay.cpp
        # Diagnostics library (github Mrunmoy/diagnostics): persistent boot/reset tracking.
        external/diagnostics/src/diag_context.c
        external/diagnostics/src/diag_lifecycle.c
        external/diagnostics/src/diag_storage.c
        external/diagnostics/src/diag_capsule.c
        external/diagnostics/src/diag_dtc.c
        external/diagnostics/src/diag_identity.c
        external/diagnostics/src/diag_transport.c
    )
    target_include_directories(proteus_gb PRIVATE
        src src/port/rp2350 external/peanut-gb external/diagnostics/include)
    # proteus_sd = proven carlk3 SDIO + ChaN-FatFs facade (sd::SdCard).
    # libdvi = PicoDVI PIO/DMA/TMDS encoder (already added via add_subdirectory above).
    # hardware_dma = dma_claim_unused_channel + channel config (used by DviDisplay).
    # pico_multicore = multicore_launch_core1 (LCD path launches core1 present worker;
    #   DVI path: core1 launched internally by DviDisplay::init() for TMDS encoder).
    target_link_libraries(proteus_gb pico_stdlib hardware_spi hardware_pwm
        hardware_dma hardware_pio hardware_timer hardware_flash pico_flash
        pico_multicore proteus_sd libdvi)
    # AUDIO_SAMPLE_RATE must match across main.cpp and minigb_apu.c (sample count /
    # ctx size). PICO_FLASH_ASSUME_CORE1_SAFE=1 is safe: ALL SD access happens before
    # any core1 launch (DVI::init or core1Present), so flash_safe_execute coordination
    # is unnecessary (there is no flash writing here, but the SD driver may call it
    # internally; this silences the resulting assertion).
    target_compile_definitions(proteus_gb PRIVATE PICO_FLASH_SIZE_BYTES=16777216
        PICO_FLASH_ASSUME_CORE1_SAFE=1 AUDIO_SAMPLE_RATE=32768
        MINIGB_APU_AUDIO_FORMAT_S16SYS=1)
    pico_enable_stdio_usb(proteus_gb 1)
    pico_enable_stdio_uart(proteus_gb 1)
    pico_add_extra_outputs(proteus_gb)

    # ── Driver-layer contract check (build-only; not flashed) ───────────────
    # Compiles each generic driver instantiated against its concrete RP2350 PHY
    # (phy_selfcheck.cpp), proving the new driver/PHY layer builds against the SDK
    # before app/gb + app/mfg are rewired onto it. A static library: no main, no
    # .uf2 — building the target IS the check. Removed once the apps instantiate
    # these drivers for real.
    add_library(proteus_phy_check STATIC
        src/port/rp2350/phy/SpiDisplayBus.cpp
        src/port/rp2350/phy/GpioButtonPort.cpp
        src/port/rp2350/phy/PwmSpeakerOut.cpp
        src/port/rp2350/phy/phy_selfcheck.cpp
        src/common/Debouncer.cpp        # used by the Buttons driver
    )
    target_include_directories(proteus_phy_check PRIVATE src src/port/rp2350)
    target_link_libraries(proteus_phy_check pico_stdlib hardware_spi hardware_pwm)

elseif(PORT STREQUAL "host")

    project(proteus_host C CXX)   # C for aether-lite (device runtime)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    enable_testing()

    add_compile_options(-Wall -Wextra -g)

    # ASan+UBSan gated behind an option: ./build.py --host-tests is fast (off),
    # ./build.py --asan turns it on for the sanitized correctness pass.
    option(PROTEUS_SANITIZE "Build host tests with AddressSanitizer + UBSan" OFF)
    if(PROTEUS_SANITIZE)
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()

    # Portable core, compiled natively (no Pico SDK). The host build is the
    # portability proof: these sources carry no hardware headers, so a leak fails
    # to compile here.
    add_library(core ${CORE_SOURCES})
    target_include_directories(core PUBLIC src)

    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # aether-lite device runtime (C99) + generated TestStation dispatch — the
    # manufacturing-test protocol, exercised on the host with a fake link.
    add_library(aetherlite STATIC
        external/aether/aether-lite/aether_lite_crc.c
        external/aether/aether-lite/aether_lite_framing.c
        external/aether/aether-lite/aether_lite_dispatch.c
        proto/gen/aether_lite/TestStation.c
    )
    set_target_properties(aetherlite PROPERTIES C_STANDARD 99)
    target_include_directories(aetherlite PUBLIC
        external/aether/aether-lite
        proto/gen/aether_lite
    )

    add_executable(unit_tests
        test/test_debounce.cpp
        test/test_framescaler.cpp       # source frame → display RGB565 blit
        test/test_teststation_rpc.cpp
        test/test_device_info.cpp
        test/display/test_st7789.cpp    # generic St7789 driver over a mock bus
        test/input/test_buttons.cpp     # generic Buttons driver over a mock GPIO port
        test/audio/test_pwmspeaker.cpp  # generic PwmSpeaker driver over a mock PWM out
        src/app/TestAgent.cpp           # portable TestStation handlers (shared w/ firmware)
    )
    target_include_directories(unit_tests PRIVATE test)   # Debouncer comes from `core`
    target_link_libraries(unit_tests PRIVATE core aetherlite GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(unit_tests)

else()
    message(FATAL_ERROR "Unknown PORT '${PORT}' — use rp2350 | host")
endif()
