.PHONY: dev, production, monitor

RM		:= rm -rf
BUILD   := build
BIN     := bin
PIO	    := .pio
LIB	    := ../../lib
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


# Upload to device
board: upload monitor

upload: lib
	pio run -e aura --target upload --upload-port $(port)

lib :
	ln -s ../../lib lib

monitor:
	pio device monitor -p $(port)

# Simulate on host PC
simulate: $(TARGET)
	./$(TARGET)

build: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

$(BUILD)/%.o: $(SRC)/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(BIN) $(BUILD)
	unlink lib
