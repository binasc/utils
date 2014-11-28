C_FLAGS= -Werror -Wall -Wpointer-arith -O0 -pipe -g -D_REENTRANT -D_GUN_SOURCE -D_NET_LOG
#CC = gcc
AR = ar
INC= -Isrc/ -I../utils/inc
LIB_PATH= -L../utils/lib -lutils -lstdc++
LIB_FILE=
SRC_DIR= src/
OBJ_DIR= obj/
SOURCES_FILES_CPP=$(wildcard $(SRC_DIR)*.c)
OBJECTS = $(patsubst %.c,$(OBJ_DIR)%.o,$(notdir $(SOURCES_FILES_CPP)))

EXE_DIR= bin/
EXE= $(EXE_DIR)tun

OBJ_EXT= .o
CSRC_EXT= .c

.PHONY: all clean module

all: $(EXE)

$(OBJ_DIR)%$(OBJ_EXT): $(SRC_DIR)%$(CSRC_EXT)
	@echo
	@echo "Compiling $< ==> $@"
	@echo "mkdir -p $(OBJ_DIR)"
	mkdir -p $(OBJ_DIR)
	$(CC) $(INC) $(C_FLAGS) -c $< -o $@

$(EXE): $(OBJECTS)
	mkdir -p $(EXE_DIR)
	$(CC) -o $(EXE) $(INC) $(C_FLAGS) $^ $(LIB_PATH) $(LIB_FILE)

clean:
	rm -f $(EXE) $(OBJECTS)
	rm -rf $(OBJ_DIR)

