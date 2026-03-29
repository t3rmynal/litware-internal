CXX = gcc
CXXFLAGS = -std=c++23 -fPIC -Wall -Wextra -I./litware-dll/src -I./litware-dll/external/imgui -I./litware-dll/external/imgui/backends -I./vendor/omath/include
LDFLAGS = -shared -ldl -lpthread -lSDL2 $(shell pkg-config --libs vulkan 2>/dev/null || echo "-lvulkan")
CXXFLAGS += $(shell pkg-config --cflags vulkan 2>/dev/null)

SOURCES = \
	litware-dll/src/linux_init.cpp \
	litware-dll/src/entry.cpp \
	litware-dll/src/bypass.cpp \
	litware-dll/src/core/interfaces.cpp \
	litware-dll/src/core/globals.cpp \
	litware-dll/src/debug.cpp \
	litware-dll/src/Fonts.cpp \
	litware-dll/src/hooks/render_hook_vk.cpp \
	litware-dll/external/imgui/imgui.cpp \
	litware-dll/external/imgui/imgui_draw.cpp \
	litware-dll/external/imgui/imgui_tables.cpp \
	litware-dll/external/imgui/imgui_widgets.cpp \
	litware-dll/external/imgui/backends/imgui_impl_vulkan.cpp \
	litware-dll/external/imgui/backends/imgui_impl_sdl2.cpp

OBJECTS = $(SOURCES:.cpp=.o)
OUTPUT = litware.so

all: $(OUTPUT)

$(OUTPUT): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(OUTPUT) $(LDFLAGS)
	@echo "Build complete: $(OUTPUT)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(OUTPUT)
	@echo "Clean complete"

.PHONY: all clean
