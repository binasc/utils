INC := -Isrc -I../utils/inc -I../nl/src
CFLAGS = -Werror -Wall $(CMOREFLAGS) -MMD -MP $(INC)
LDLIBS := -L../nl/bin -lnl
SRC_DIR := src/
OBJ_DIR := obj/
BIN_DIR := bin/

TARGET := $(BIN_DIR)dns

SRCS := $(wildcard $(SRC_DIR)*.c)
OBJS := $(patsubst %.c, $(OBJ_DIR)%.o, $(notdir $(SRCS)))
DEPS := $(patsubst %.c, $(OBJ_DIR)%.d, $(notdir $(SRCS)))

.PHONY: all clean

ver ?= debug

ifeq ($(ver), debug)
CMOREFLAGS := -O0 -g 
else
CMOREFLAGS := -O3 -DLOG_LEVEL=4
endif

all:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@$(MAKE) $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

$(OBJ_DIR)%.o: $(SRC_DIR)%.c
	$(CC) -o $@ -c $(CFLAGS) $<

-include $(DEPS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)
	rm -rf $(OBJ_DIR)

