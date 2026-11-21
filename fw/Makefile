# BusCraft adapter firmware. Development board: NUCLEO-WBA55CG (STM32WBA55CG, BLE 5.4).
# Based on ST's BLE_p2pServer example (Core/, STM32_WPAN/, System/ copied from it; those files keep ST's licence headers).
#   make cube       fetch the needed parts of STM32CubeWBA (pinned to CUBE_REV) into ../lib/STM32CubeWBA (sparse clone, once)
#   make            build/buscraft.bin
#   make flash      program over ST-LINK (STM32_Programmer_CLI; STLINK=<serial> picks one probe)
#   make console    serial console (PORT=/dev/... 115200)
# Toolchain: arm-none-eabi-gcc from PATH, or STM32CubeIDE's bundled one on macOS.
CUBEIDE := /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins
GCC_BIN ?= $(firstword $(wildcard $(CUBEIDE)/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin))
PROG    ?= $(or $(firstword $(wildcard $(CUBEIDE)/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macos*/tools/bin/STM32_Programmer_CLI)),STM32_Programmer_CLI)
CROSS   := $(if $(GCC_BIN),$(GCC_BIN)/,)arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
CUBE    ?= ../lib/STM32CubeWBA
CUBE_REV := 4ccfd1526870263657ec8e0854c64cda6ed2b5da
HAL     := $(CUBE)/Drivers/STM32WBAxx_HAL_Driver
WPAN    := $(CUBE)/Middlewares/ST/STM32_WPAN
COMMON  := $(CUBE)/Projects/Common/WPAN
UTIL    := $(CUBE)/Utilities
VERSIO  ?= 1
STLINK  ?=

DEFS := -DUSE_FULL_LL_DRIVER -DUSE_HAL_DRIVER -DSTM32WBA55xx -DPHY_40nm_3_00_a -DBLE_LL -DBLE_STACK_BASIC_FEATURES \
        -DVERSIO=$(VERSIO)
INC  := App Core/Inc System/Config System/Config/Log System/Config/LowPower System/Config/Debug_GPIO System/Config/Flash \
        System/Config/CRC_Ctrl System/Config/ADC_Ctrl System/Interfaces System/Modules STM32_WPAN/App STM32_WPAN/Target \
        $(HAL)/Inc $(HAL)/Inc/Legacy $(CUBE)/Drivers/CMSIS/Device/ST/STM32WBAxx/Include $(CUBE)/Drivers/CMSIS/Include \
        $(CUBE)/Drivers/BSP/STM32WBAxx_Nucleo \
        $(UTIL)/trace/adv_trace $(UTIL)/misc $(UTIL)/sequencer $(UTIL)/tim_serv $(UTIL)/lpm/tiny_lpm \
        $(WPAN) $(WPAN)/link_layer/ll_cmd_lib/config/ble_basic $(WPAN)/link_layer/ll_cmd_lib/inc \
        $(WPAN)/link_layer/ll_cmd_lib/inc/_40nm_reg_files $(WPAN)/link_layer/ll_cmd_lib/inc/ot_inc \
        $(WPAN)/link_layer/ll_cmd_lib/porting $(WPAN)/link_layer/ll_sys/inc \
        $(WPAN)/link_layer/ll_cmd_lib/src/shrd_utils/inc $(WPAN)/ble/stack/include $(WPAN)/ble/stack/include/auto \
        $(WPAN)/ble/svc/Inc $(WPAN)/ble/svc/Src \
        $(COMMON)/Interfaces $(COMMON)/Modules $(COMMON)/Modules/BasicAES $(COMMON)/Modules/Flash \
        $(COMMON)/Modules/MemoryManager $(COMMON)/Modules/Nvm $(COMMON)/Modules/RTDebug \
        $(COMMON)/Modules/SerialCmdInterpreter $(COMMON)/Modules/Log

CFLAGS  := -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -Os -g -std=gnu11 -Wall \
           -ffunction-sections -fdata-sections $(DEFS) $(addprefix -I,$(INC))
LDFLAGS := -TSTM32WBA55CGUX_FLASH.ld -Wl,--gc-sections --specs=nano.specs -Wl,--no-warn-rwx-segments -z noexecstack \
           $(WPAN)/ble/stack/lib/stm32wba_ble_stack_full.a $(WPAN)/link_layer/ll_cmd_lib/lib/LinkLayer_BLE_Basic_V3_0.a -lm

SRC := $(wildcard App/*.c) $(wildcard Core/Src/*.c) $(wildcard STM32_WPAN/App/*.c) $(wildcard STM32_WPAN/Target/*.c) \
       $(wildcard System/Config/*/*.c) $(wildcard System/Interfaces/*.c) $(wildcard System/Modules/*.c) \
       syscalls.c sysmem.c startup_stm32wba55cgux.s \
       $(addprefix $(HAL)/Src/stm32wbaxx_, hal.c hal_adc.c hal_adc_ex.c hal_cortex.c hal_crc.c hal_crc_ex.c hal_cryp.c \
         hal_cryp_ex.c hal_dma.c hal_dma_ex.c hal_exti.c hal_flash.c hal_flash_ex.c hal_gpio.c hal_gtzc.c hal_hsem.c \
         hal_i2c.c hal_i2c_ex.c hal_icache.c hal_pwr.c hal_pwr_ex.c hal_ramcfg.c hal_rcc.c hal_rcc_ex.c hal_rng.c \
         hal_rng_ex.c hal_rtc.c hal_rtc_ex.c hal_uart.c hal_uart_ex.c ll_adc.c ll_dma.c ll_exti.c ll_gpio.c \
         ll_icache.c ll_pka.c ll_tim.c) \
       $(UTIL)/trace/adv_trace/stm32_adv_trace.c $(UTIL)/lpm/tiny_lpm/stm32_lpm.c $(UTIL)/misc/stm32_mem.c \
       $(UTIL)/sequencer/stm32_seq.c $(UTIL)/misc/stm32_systime.c $(UTIL)/tim_serv/stm32_timer.c \
       $(UTIL)/misc/stm32_tiny_sscanf.c $(UTIL)/misc/stm32_tiny_vsnprintf.c \
       $(CUBE)/Drivers/BSP/STM32WBAxx_Nucleo/stm32wbaxx_nucleo.c \
       $(addprefix $(COMMON)/Interfaces/, adv_trace_usart_if.c app_bsp.c hw_aes.c hw_otp.c hw_rng.c timer_if.c) \
       $(addprefix $(COMMON)/Modules/, adc_ctrl.c app_sys.c crc_ctrl.c otp.c pka_ctrl.c scm.c stm_list.c \
         stm_queue.c temp_measurement.c wpan_utils.c BasicAES/baes_ccm.c BasicAES/baes_cmac.c BasicAES/baes_ecb.c \
         Flash/flash_driver.c Flash/flash_manager.c Flash/rf_timing_synchro.c Flash/simple_nvm_arbiter.c \
         Log/log_module.c MemoryManager/advanced_memory_manager.c MemoryManager/stm32_mm.c RTDebug/RTDebug.c \
         RTDebug/RTDebug_dtb.c SerialCmdInterpreter/serial_cmd_interpreter.c) \
       $(COMMON)/Startup/stm32wbaxx_ResetHandler_GCC.s \
       $(WPAN)/ble/svc/Src/svc_ctl.c \
       $(addprefix $(WPAN)/link_layer/ll_sys/src/ll_sys_, cs.c dp_slp.c intf.c startup.c)

all: build/buscraft.bin

build/buscraft.elf: $(SRC) $(wildcard App/*.h */Inc/*.h */*/*.h */*/*/*.h) STM32WBA55CGUX_FLASH.ld Makefile
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -Wl,-Map=build/buscraft.map -o $@
	$(SIZE) $@

build/buscraft.bin: build/buscraft.elf
	$(OBJCOPY) -O binary $< $@

flash: build/buscraft.bin
	$(PROG) -c port=SWD $(if $(STLINK),sn=$(STLINK)) mode=UR -w build/buscraft.bin 0x08000000 -v -rst

console:
	@test -n "$(PORT)" || { echo "usage: make console PORT=/dev/..."; exit 1; }
	(stty 115200 raw -echo; cat) < $(PORT)

cube:
	git clone --filter=blob:none --no-checkout --sparse https://github.com/STMicroelectronics/STM32CubeWBA.git $(CUBE)
	git -C $(CUBE) checkout -q $(CUBE_REV)
	git -C $(CUBE) sparse-checkout set --skip-checks Drivers/CMSIS Drivers/STM32WBAxx_HAL_Driver \
	  Drivers/BSP/STM32WBAxx_Nucleo Middlewares/ST/STM32_WPAN Utilities Projects/Common
	git -C $(CUBE) submodule update --init --depth 1 Drivers/CMSIS/Device/ST/STM32WBAxx \
	  Drivers/STM32WBAxx_HAL_Driver Drivers/BSP/STM32WBAxx_Nucleo Middlewares/ST/STM32_WPAN

.PHONY: all flash console cube
