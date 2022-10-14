LIB_DIR    ?= lib
BIN_DIR    ?= bin
UTL_DIR    ?= utils
TST_DIR    ?= tests
LIBNMU_DIR ?= libnmu
CXX        ?= g++
CC         ?= gcc
CXXLINK    := $(CXX)
CFLAGS     := -std=gnu11 -fpic -Wall -I. -Ilibnmu -g \
              -ffunction-sections -fdata-sections
CXXFLAGS   := -std=c++11 -fpic -Wall -I. -Ilibnmu -g \
              -ffunction-sections -fdata-sections
LFLAGS     := -lpthread -lm -lz -lnuevomatchup -L$(BIN_DIR) \
              -Wl,-rpath,'$$ORIGIN/' -Wl,-z,origin          \

export BASE_DIR=$(PWD)
export CFLAGS
export LFLAGS

# Make surt that libnmu exists
ifeq "$(wildcard $(LIBNMU_DIR) )" ""
$(shell ./get-nmu.sh)
endif

# Include all user-defined functions
include make-functions.mk

# Create rules for all object files
$(call createmodule_cpp,$(wildcard $(LIB_DIR)/*.cpp),lib)
$(call createmodule_cpp,$(wildcard $(UTL_DIR)/*.cpp),utl)
$(call createmodule_cpp,$(wildcard $(TST_DIR)/*.cpp),tst)

# Search for all objects, executables
LIB_OBJ:=$(call collectobjects,$(LIB_DIR),$(BIN_DIR),cpp)
UTL_OBJ:=$(call collectobjects,$(UTL_DIR),$(BIN_DIR),cpp,util)
TST_OBJ:=$(call collectobjects,$(TST_DIR),$(BIN_DIR),cpp,test)

APPS:=$(call collectexecutables,$(UTL_DIR),$(BIN_DIR),cpp,util-)
APPS+=$(call collectexecutables,$(TST_DIR),$(BIN_DIR),cpp,test-)
LIB:=$(BIN_DIR)/libranger.so

# Copy libnuevomatchup to bin dir
$(shell cp $(LIBNMU_DIR)/libnuevomatchup.so $(BIN_DIR))

release: $(APPS) $(LIB)
	strip --strip-unneeded $(APPS) $(LIB)
debug:   $(APPS) $(LIB)
deepdbg: $(APPS) $(LIB)

$(BIN_DIR)/util-%.exe: $(BIN_DIR)/util-%.o $(UTL_OBJ) $(LIB_OBJ) 
	$(CXXLINK) $(CXXFLAGS) $+ $(LFLAGS) -o $@

$(BIN_DIR)/test-%.exe: $(BIN_DIR)/test-%.o $(TST_OBJ) $(LIB_OBJ)
	$(CXXLINK) $(CXXFLAGS) $+ $(LFLAGS) -o $@

$(LIB): $(LIB_OBJ) 
	$(CXXLINK) $(CXXFLAGS) $+ $(LFLAGS) -shared -o $@

# Include submodule with rules to create objects
include $(BIN_DIR)/*.mk

# Target specific variables
release: CFLAGS   += -O2 -DNDEBUG
release: CXXFLAGS += -O2 -DNDEBUG
release: LFLAGS   +=  -fvisibility=hidden -Wl,--gc-sections
debug:   CFLAGS   += -Og
debug:   CXXFLAGS += -Og
deepdbg: CFLAGS   += -O0
deepdbg: CXXFLAGS += -O0

clean:
	rm -rf $(BIN_DIR)/*.o $(BIN_DIR)/*.exe $(BIN_DIR)/*.so

purge: 
	rm -rf $(BIN_DIR)/

