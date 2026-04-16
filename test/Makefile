.PHONY: test clean

BUILD   := build
BIN     := bin
LIB     := ../lib
TARGET  := $(BIN)/test

# Only compile the source files we need (no link_layer, multihop, or lora)
LIB_SRCS := $(LIB)/net/src/net.cpp \
            $(LIB)/net/src/multihop_messages.cpp \
            $(LIB)/net/src/multihop_state.cpp \
            $(LIB)/net/src/time_helpers.cpp

TEST_SRCS := test_main.cpp mocks.cpp

INC := . \
       $(LIB)/net/include \
       $(LIB)/arduino_stubs/include \
       $(LIB)/lora/include

CXX      := g++ -std=c++23
CXXFLAGS := -Wall \
            -Wextra \
            -g \
            -D SIMULATE \
            -D 'NEXUS_ROOT="/tmp/test_nexus"' \
            $(addprefix -I, $(INC))

SOURCES := $(TEST_SRCS) $(LIB_SRCS)
OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(SOURCES)))

RM := rm -rf

# Tell make where to find .cpp files
vpath %.cpp . $(LIB)/net/src

default: test

test: $(TARGET)
	@./$(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(BIN) $(BUILD)
