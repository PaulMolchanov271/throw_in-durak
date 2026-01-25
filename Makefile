CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -pthread -g -O0 -fsanitize=address -fno-omit-frame-pointer 
LDFLAGS := -lfltk -lfltk_images -lpng -fsanitize=address

SRC_DIR := ui
OBJ_DIR := obj
TARGET := throw_in_durak

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@..."
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	clear

rebuild: clean all

.PHONY: all clean rebuild

