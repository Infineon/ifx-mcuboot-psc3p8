################################################################################
# \file PSC3.mk
# \version 1.0
#
# \brief
# This file is dedicated for PSC3 platform
#
################################################################################
# \copyright
# Copyright 2024 Cypress Semiconductor Corporation (an Infineon company) or
# an affiliate of Cypress Semiconductor Corporation
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

include host.mk

# PDL category suffix to resolve common path in pdl
PDL_CAT_SUFFIX := 1B
CRYPTO_ACC_TYPE := MXCRYPTOLITE

# MCU device selection, based on target device.
# Default chips are used for supported platforms
# This can be redefined in case of other chip usage
DEVICE ?= PSC3M5FDS2AFQ1

# Led default config
LED_PORT_DEFAULT ?= GPIO_PRT0
LED_PIN_DEFAULT ?= 0U


#UART default config
UART_TX_DEFAULT ?= CYBSP_DEBUG_UART_TX
UART_RX_DEFAULT ?= CYBSP_DEBUG_UART_RX

PLATFORM_CONFIG ?= platforms/memory/PSC3/flashmap/platform.json
FLASH_MAP ?= platforms/memory/PSC3/flashmap/overwrite_single_flash.json

# Add device name to defines
DEFINES += $(DEVICE)

# Default upgrade method
PLATFORM_DEFAULT_USE_OVERWRITE ?= 0

PLATFORM_CHUNK_SIZE := 4096U

# Minimum erase size of underlying memory hardware
PLATFORM_MEMORY_ALIGN := 512U
PLATFORM_MAX_TRAILER_PAGE_SIZE := 512U

ifndef CY_GENERATED_SOURCE_PATH
$(error CY_GENERATED_SOURCE_PATH is not defined, refer to PSC3.md)
endif
$(info CY_GENERATED_SOURCE_PATH is $(CY_GENERATED_SOURCE_PATH))

ifeq ($(USE_HW_ROLLBACK_PROT), 1)
    SOURCES_PLATFORM += $(wildcard $(PRJ_DIR)/platforms/security_counter/PSC3/*.c)
    INCLUDE_DIRS_PLATFORM += $(PRJ_DIR)/platforms/security_counter/PSC3
    PLATFORM_DEFINES_LIBS += CY_RBC_ROLLBACK_COUNTERS_NUM=$(MCUBOOT_IMAGE_NUMBER)
    PLATFORM_DEFINES_LIBS += CY_RBC_CHECKSUM_LENGTH=32
    PLATFORM_DEFINES_LIBS += CY_RBC_ALIGN=$(PLATFORM_MEMORY_ALIGN)
endif
###############################################################################
# Application specific libraries
###############################################################################
# MCUBootApp
###############################################################################
THIS_APP_PATH = $(PRJ_DIR)/libs
CORE ?= CM33
CORE_SUFFIX = m33
CORE_ID = 0

ifeq ($(APP_NAME), MCUBootApp)
# Application dependent definitions
# MCUBootApp default settings
USE_CRYPTO_HW ?= 0
HEADER_SIZE := 0

# Platform dependend utils files
PLATFORM_APP_SOURCES += $(wildcard $(PRJ_DIR)/platforms/utils/$(FAMILY)/*.c)
PLATFORM_INCLUDE_DIRS_UTILS := $(PRJ_DIR)/platforms/utils/$(FAMILY)

# Post build job to execute for platform
post_build: $(OUT_CFG)/$(APP_NAME)_unsigned.hex
ifeq ($(POST_BUILD_ENABLE), 1)
	$(info [POST BUILD] - Executing post build script for $(APP_NAME))
	$(shell cp -f $(OUT_CFG)/$(APP_NAME)_unsigned.hex $(OUT_CFG)/$(APP_NAME).hex)
	$(GCC_PATH)/bin/arm-none-eabi-objdump -s $(OUT_CFG)/$(APP_NAME).hex > $(OUT_CFG)/$(APP_NAME).objdump
else
	$(info Post build is disabled by POST_BUILD_ENABLE parameter)
endif # POST_BUILD_ENABLE
endif ## MCUBootApp

###############################################################################
# BlinkyApp
###############################################################################
ifeq ($(APP_NAME), BlinkyApp)
PLATFORM_DEFAULT_PRIMARY_IMG_START ?= $(PLATFORM_DEFAULT_USER_APP_START)

# We still need this for MCUBoot apps signing
IMGTOOL_PATH ?=	../../scripts/imgtool.py

PLATFORM_DEFAULT_IMG_VER_ARG ?= 1.0.0

KEY_FILE_PATH ?= ../keys
SIGN_KEY_FILE ?= cypress-test-ec-p256
HEADER_SIZE := 0x400

SIGN_COMMAND = image-metadata
ifeq ($(IMG_TYPE), BOOT)
    DEFINES += BOOT_IMAGE
    
    ifneq ($(MCUBOOT_SKIP_BOOT_VALIDATION), 1)
        SIGN_COMMAND = sign-image
        PLATFORM_SIGN_ARGS += --key $(KEY_FILE_PATH)/$(SIGN_KEY_FILE).pem
    endif
    
else ifeq ($(IMG_TYPE), UPGRADE)
    DEFINES += UPGRADE_IMAGE
    DEFINES += MEMORY_ALIGN=$(PLATFORM_MEMORY_ALIGN)
    DEFINES += PLATFORM_MAX_TRAILER_PAGE_SIZE=$(PLATFORM_MAX_TRAILER_PAGE_SIZE)
    
    ifneq ($(MCUBOOT_SKIP_UPGRADE_VALIDATION), 1)
        SIGN_COMMAND = sign-image
        PLATFORM_SIGN_ARGS += --key $(KEY_FILE_PATH)/$(SIGN_KEY_FILE).pem
    else
        DEFINES += MCUBOOT_SKIP_VALIDATE_SECONDARY_SLOT
        ifeq ($(ENC_IMG), 1)
            PLATFORM_SIGN_ARGS += --decrypted $(BINARY_OUT_PATH)/$(APPNAME)_decrypted.hex
        endif
    endif
    
    # ifneq ($(USE_OVERWRITE), 1)
    #     SOURCES+= $(MCUBOOT_CY_PATH)/platforms/img_confirm/$(FAMILY)/set_img_ok.c
    # endif
else
    $(error Set IMG_TYPE to be either BOOT or UPGRADE)
endif

APP_VERSION_MAJOR ?= $(IMG_ID)
ifeq ($(IMG_TYPE), BOOT)    
    APP_VERSION_MINOR ?= 1
else ifeq ($(IMG_TYPE), UPGRADE)
    APP_VERSION_MINOR ?= 2
endif
APP_VERSION_BUILD ?= 0

PLATFORM_ALIGNMENT = 1
ERASED_VALUE ?= 0
PLATFORM_MIN_ERASE_SIZE = 0x200

CY_BUILD_VERSION=$(APP_VERSION_MAJOR).$(APP_VERSION_MINOR).$(APP_VERSION_BUILD)
PLATFORM_SIGN_ARGS += --header-size $(HEADER_SIZE) --align $(PLATFORM_ALIGNMENT) \
            --image-version $(CY_BUILD_VERSION) -S $(SLOT_SIZE) $(UPGRADE_TYPE)\
            -R $(ERASED_VALUE) --min-erase-size $(PLATFORM_MIN_ERASE_SIZE)

ifeq ($(IMG_TYPE), UPGRADE)
    # Set img_ok flag to trigger swap type permanent
    ifeq ($(CONFIRM), 1)
        PLATFORM_SIGN_ARGS += --confirm
    endif
    PLATFORM_SIGN_ARGS += --pad
endif


ifeq ($(BOOT_RECORD_SW_TYPE), )
    ifeq ($(IMG_TYPE), BOOT)
        BOOT_RECORD_IMG_TYPE_STR = B_Blinky$(IMG_ID)
    else
        BOOT_RECORD_IMG_TYPE_STR = U_Blinky$(IMG_ID)
    endif
    
    BOOT_RECORD = --boot-record $(BOOT_RECORD_IMG_TYPE_STR)
else
    BOOT_RECORD = --boot-record $(BOOT_RECORD_SW_TYPE)
endif

PLATFORM_SIGN_ARGS += $(BOOT_RECORD)

# Use key provisioned in device to verify images
ifeq ($(USE_HW_KEY), 1)
    SIGN_ARGS += --public-key-format=full 
endif

# Set parameters needed for signing
ifeq ($(IMG_TYPE), UPGRADE)
# Use encryption and random initial vector for image
	ifeq ($(ENC_IMG), 1)
		PLATFORM_SIGN_ARGS += --encrypt ../../$(ENC_KEY_FILE).pem
		PLATFORM_SIGN_ARGS += --use-random-iv
	endif
endif

# Post build action to execute after main build job
post_build: $(OUT_CFG)/$(APP_NAME).bin
ifeq ($(POST_BUILD_ENABLE), 1)
	$(info [POST BUILD] - Executing post build script for $(APP_NAME))
	$(shell mv -f $(OUT_CFG)/$(APP_NAME).bin $(OUT_CFG)/$(APP_NAME)_unsigned.bin)
	edgeprotecttools sign-image $(PLATFORM_SIGN_ARGS) --key-path $(PRJ_DIR)/keys/cypress-test-ec-p256.pem --image $(OUT_CFG)/$(APP_NAME)_unsigned.bin  --hex-addr=$(HEADER_OFFSET) -o $(OUT_CFG)/$(APP_NAME)$(UPGRADE_SUFFIX).hex
	$(GCC_PATH)/bin/arm-none-eabi-objdump -s $(OUT_CFG)/$(APP_NAME)$(UPGRADE_SUFFIX).hex > $(OUT_CFG)/$(APP_NAME)$(UPGRADE_SUFFIX).objdump
else
	$(info Post build is disabled by POST_BUILD_ENABLE parameter)
endif # POST_BUILD_ENABLE
endif ## BlinkyApp


###############################################################################
# Toolchain
###############################################################################
# Define build flags specific to a certain platform

CFLAGS_PLATFORM := -mcpu=cortex-$(CORE_SUFFIX) -pipe -mcmse
###############################################################################
# Common libraries
###############################################################################
PLATFORM_SOURCES_PDL_STARTUP = startup_cat1b_c$(CORE_SUFFIX).c

PLATFORM_INCLUDE_DIRS_UTILS := $(PRJ_DIR)/platforms/utils/$(FAMILY)
PLATFORM_INCLUDE_DIRS_UTILS += $(CY_GENERATED_SOURCE_PATH)

PLATFORM_APP_SOURCES += $(wildcard $(CY_GENERATED_SOURCE_PATH)/*.c)
PLATFORM_APP_SOURCES += $(wildcard $(CY_BSP)/COMPONENT_CM33/COMPONENT_SECURE_DEVICE/*.c)


PLATFORM_DEFINES_LIBS := -DCY_USING_HAL
PLATFORM_DEFINES_LIBS += -DCOMPONENT_$(CORE)
PLATFORM_DEFINES_LIBS += -DCOMPONENT_$(CORE)_$(CORE_ID)
PLATFORM_DEFINES_LIBS += -DCORE_NAME_$(CORE)_$(CORE_ID)=1
PLATFORM_DEFINES_LIBS += -DPSC3
PLATFORM_DEFINES_LIBS += -DCM33_STACK_LIMIT=0x200 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_CAT1 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_CAT1B 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_CM33 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_CM33_0 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_MBEDTLS 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_MTB_HAL_CAT1 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_MTB_PDL_CAT1 
PLATFORM_DEFINES_LIBS += -DCORE_NAME_CM33_0=1 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_MCUBOOT 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_MTB_HAL_CAT1 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_MTB_PDL_CAT1 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_RECIPE_MAKE_CAT1B 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_RETARGET_IO 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_SOFTFP 
PLATFORM_DEFINES_LIBS += -DCORE_NAME_CM33_0=1  
PLATFORM_DEFINES_LIBS += -DCY_RETARGET_IO_NO_FLOAT 
PLATFORM_DEFINES_LIBS += -DCY_SUPPORTS_DEVICE_VALIDATION 
PLATFORM_DEFINES_LIBS += -DMAX_BOOT_RECORD_SZ=0x1000 
PLATFORM_DEFINES_LIBS += -DMCUBOOT_DATA_SHARING 
PLATFORM_DEFINES_LIBS += -DMCUBOOT_FIH_PROFILE_MEDIUM 
PLATFORM_DEFINES_LIBS += -DMCUBOOT_FIH_PROFILE_ON  
PLATFORM_DEFINES_LIBS += -DMCUBOOT_LOG_LEVEL=MCUBOOT_LOG_LEVEL_DEBUG 
PLATFORM_DEFINES_LIBS += -DPLATFORM_MAX_IMG_SECTORS=320  
PLATFORM_DEFINES_LIBS += -DMCUBOOT_OVERWRITE_ONLY 
PLATFORM_DEFINES_LIBS += -DMCUBOOT_SHARED_DATA_BASE=0x3400f000 
PLATFORM_DEFINES_LIBS += -DMCUBOOT_SHARED_DATA_SIZE=0x1000 
PLATFORM_DEFINES_LIBS += -DMEMORY_ALIGN=512U -DPLATFORM_MAX_TRAILER_PAGE_SIZE=512U 
PLATFORM_DEFINES_LIBS += -DCY_TARGET_BOARD=APP_KIT_PSC3M5_EVK
PLATFORM_DEFINES_LIBS += -DCOMPONENT_APP_KIT_PSC3M5_EVK
PLATFORM_DEFINES_LIBS += -DCOMPONENT_PSC3
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_CY_MBEDTLS_ACCELERATION
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_IFX_MBEDTLS
PLATFORM_DEFINES_LIBS += -DCOMPONENT_GCC_ARM
PLATFORM_DEFINES_LIBS += -DCOMPONENT_Debug  
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_CMSIS 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_CORE_LIB 
PLATFORM_DEFINES_LIBS += -DCOMPONENT_MW_CORE_MAKE   
PLATFORM_DEFINES_LIBS += -DCOMPONENT_SECURE_DEVICE
