C_FLAGS= -Wall -Wpointer-arith -O0 -pipe -g -D_REENTRANT -D_GUN_SOURCE -D_NET_LOG
CXX = g++
AR = ar
COMMON_LIB_PATH=/opt/common/
MYSQL_PATH=/usr/local/mysql/
INC= -Iinc
SRC_DIR= src/
OBJ_DIR= obj/

CXXOBJ_EXT= .oo
CXXSRC_EXT= .cpp
SOURCES_FILES_CPP=$(wildcard $(SRC_DIR)*$(CXXSRC_EXT))
CXXOBJECTS = $(patsubst %$(CXXSRC_EXT), $(OBJ_DIR)%$(CXXOBJ_EXT), $(notdir $(SOURCES_FILES_CPP)))

COBJ_EXT= .o
CSRC_EXT= .c
SOURCES_FILES_C=$(wildcard $(SRC_DIR)*$(CSRC_EXT))
COBJECTS = $(patsubst %$(CSRC_EXT), $(OBJ_DIR)%$(COBJ_EXT), $(notdir $(SOURCES_FILES_C)))

LIB_DIR= lib/
LIB= libutils.a

.PHONY: all clean module

all: $(LIB_DIR)$(LIB)

$(OBJ_DIR)%$(CXXOBJ_EXT): $(SRC_DIR)%$(CXXSRC_EXT)
	@echo
	@echo "Compiling $< ==> $@"
	@echo "mkdir -p $(OBJ_DIR)"
	mkdir -p $(OBJ_DIR)
	$(CXX) $(INC) $(C_FLAGS) -c $< -o $@

$(OBJ_DIR)%$(COBJ_EXT): $(SRC_DIR)%$(CSRC_EXT)
	@echo
	@echo "Compiling $< ==> $@"
	@echo "mkdir -p $(OBJ_DIR)"
	mkdir -p $(OBJ_DIR)
	$(CXX) $(INC) $(C_FLAGS) -c $< -o $@

$(LIB_DIR)$(LIB): $(CXXOBJECTS) $(COBJECTS)
	mkdir -p $(LIB_DIR)
	$(AR) -rcs $@ $^

clean:
	rm -f $(LIB_DIR)$(LIB) $(CXXOBJECTS) $(COBJECTS)
	@echo "rm -rf  $(OBJ_DIR)"
	rm -rf $(OBJ_DIR)

