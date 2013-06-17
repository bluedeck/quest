include $(dir $(lastword $(MAKEFILE_LIST)))target-config.mk

INSTALL_CMD = cp
CC = $(TARGET)-gcc
LD = $(TARGET)-ld
AR = $(TARGET)-ar
RANLIB = $(TARGET)-ranlib
TARGET_DEST = $(TOOLCHAIN_INSTALL_DIR)/$(TARGET)
LIB_DEST = $(TARGET_DEST)/lib
INC_DEST = $(TARGET_DEST)/include
