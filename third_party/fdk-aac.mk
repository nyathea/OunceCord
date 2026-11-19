# SPDX-License-Identifier: MIT
# Builds the AAC decoder parts of fdk-aac (third_party/fdk-aac) as a static
# library for OunceCord's video sound. Included by the main Makefile's
# outer pass: make -f third_party/fdk-aac.mk OUT=build/fdk-aac
include $(DEVKITPRO)/libounce/switch2_rules

FDK	:=	third_party/fdk-aac
OUT	?=	build/fdk-aac
LIBS_DIRS := libAACdec libArithCoding libDRCdec libFDK libMpegTPDec libPCMutils libSACdec libSBRdec libSYS
SRCS	:=	$(foreach d,$(LIBS_DIRS),$(wildcard $(FDK)/$(d)/src/*.cpp))
OBJS	:=	$(patsubst $(FDK)/%.cpp,$(OUT)/%.o,$(SRCS))
INCS	:=	$(foreach d,$(LIBS_DIRS),-I$(FDK)/$(d)/include)

FDK_FLAGS := -O2 -g -ffunction-sections -fdata-sections $(SWITCH2_CPU) -mtp=soft -fPIE \
	-fno-exceptions -fno-rtti -w -D__SWITCH__ $(INCS)

$(OUT)/libfdk-aac.a: $(OBJS)
	@rm -f $@
	@$(AR) rcs $@ $^
	@echo built ... $(notdir $@)

$(OUT)/%.o: $(FDK)/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(FDK_FLAGS) -c $< -o $@
