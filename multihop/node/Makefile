.PHONY: build clean board lib monitor pio-build

BUILD   := build
BIN     := bin
PIO     := .pio
LIB     := ../../lib
TARGET  := $(BIN)/main

LIB_DIRS := $(wildcard $(LIB)/*)
SRC     := src $(foreach dir, $(LIB_DIRS), $(dir)/src)
INC     := include $(foreach dir, $(LIB_DIRS), $(dir)/include)

CXX      := g++ -std=c++23
CXXFLAGS := -Wall \
            -g \
            -Wextra \
            -Wsign-conversion \
            -Wformat \
            -Wmissing-declarations \
            -Wpedantic \
            -D SIMULATE \
            -D NODE_COUNT=$(nodes) \
            -D NEXUS_ROOT=\"$$HOME/nexus\" \
            $(addprefix -I, $(INC))

SOURCES := $(foreach dir,$(SRC),$(wildcard $(dir)/*.cpp))
OBJECTS := $(patsubst $(SRC)/%.cpp,$(BUILD)/%.o,$(SOURCES))

RM := rm -rf

default: build

build: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

$(BUILD)/%.o: $(SRC)/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(BIN) $(BUILD) $(PIO)
	-unlink lib 2>/dev/null

# PlatformIO uses lib_deps = symlink://lib/<name>, which expects a `lib`
# directory beside platformio.ini. Symlink to the shared library tree.
lib:
	@test -e lib || ln -s ../../lib lib

# Compile-only check that the embedded build works. Skips the upload
# step so it can run without a board attached.
pio-build: lib
	pio run -e aura

# Build and flash to a connected Arduino Mega 2560. Caller must supply
# port=/dev/ttyUSBN.
board: lib
	export DEVICE_ID=$(id); \
	pio run -e aura --target upload --upload-port $(port)

monitor:
	pio device monitor -p $(port)
