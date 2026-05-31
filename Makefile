CXX ?= g++
TARGET := modmove.so
SRC := src/main.cpp

PKGS := hyprland pixman-1 libdrm wayland-server xkbcommon
CXXFLAGS += -std=c++23 -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers -fPIC -shared
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)
