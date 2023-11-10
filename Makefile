# 输出文件名(不包含后缀)
TARGET       = mqtt
# 输出文件路径
OBJ_DIR      = output

# C 源文件
SRCS         =

# 汇编源文件
ASMS         =

# 动态链接库
DLLS         =

# 包含路径
INCLUDES     = -I./inc

-include ./src/Makefile
-include ./res/Makefile

# 宏定义
DEFINES      =
# 子系统
SUB_SYS      = -mconsole
# 编译优化等级
OPTIMIZATION = -O0

# 编译参数
CCFLAGS := -c -Wall -g -fexec-charset=GB2312 -fwide-exec-charset=EUC-CN \
           $(OPTIMIZATION) $(INCLUDES) $(DEFINES)

# 链接参数
LDFLAGS := $(SUB_SYS) -Wall -lwsock32 -lws2_32

OBJS    := $(SRCS:%.c=$(OBJ_DIR)/%.o) $(ASMS:%.s=$(OBJ_DIR)/%.o)
OBJS    += $(DLLS)

# 编译器
AR      = ar
CC      = gcc
CXX     = g++
NM      = nm
CPP     = cpp
OBJCOPY = objcopy
OBJDUMP = objdump
SIZE    = size

.PHONY: clean

$(OBJ_DIR)/$(TARGET).exe:$(OBJS)
	@$(CC) -o $@ $^ $(LDFLAGS)
	@echo LD $@
#	@strip $(OBJ_DIR)/$(TARGET).exe
	@COPY output\$(TARGET).exe . > nul

%.d:%.c

INCLUDE_FILES := $(SRCS:%.c=$(OBJ_DIR)/%.d)
-include $(INCLUDE_FILES)

${OBJ_DIR}/%.o:%.c Makefile
	@if not exist $(subst /,\, $(@D)) (md $(subst /,\, $(@D)))
	@$(CC) $(CCFLAGS) -MMD -c $< -o $@
	@echo CC $@

${OBJ_DIR}/%.o:%.s Makefile
	@if not exist $(subst /,\, $(@D)) (md $(subst /,\, $(@D)))
	@$(CC) $(CCFLAGS) -MMD -c $< -o $@
	@echo CC $@

clean:
	@rd /s /q $(subst /,\,${OBJ_DIR})
	@md $(subst /,\,${OBJ_DIR})
	@echo rm -rf ${OBJ_DIR}/*
