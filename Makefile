# blue whale external overlay - build with w64devkit (gcc + make)
# output: build/bluewhale.exe; intermediate objects are removed after a successful link
CXX      ?= g++
IMGUI    := dependencies/imgui
BUILD    := build
OBJDIR   := $(BUILD)/obj

CXXFLAGS := -std=c++20 -O2 -DNDEBUG -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -I src -I dependencies -I $(IMGUI) -I $(IMGUI)/backends
LDFLAGS  := -municode -mwindows -static -static-libgcc -static-libstdc++ -s
LIBS     := -ld3d11 -ldxgi -ldxguid -ld3dcompiler -ldcomp -ldwmapi -ldbghelp -luser32 -lgdi32 -limm32

ifeq ($(ESP_DEBUG),1)
CXXFLAGS += -DBLUE_WHALE_ESP_DEBUG
endif
ifeq ($(PERF_DIAG),1)
CXXFLAGS += -DBLUE_WHALE_PERF_DIAG
endif

SRC := src/core/main.cpp src/core/overlay.cpp src/core/menu.cpp src/core/memory.cpp src/core/game.cpp src/core/bind.cpp \
       src/core/config.cpp src/func/esp/esp.cpp src/func/aim/aim.cpp src/func/aim/fov.cpp \
       src/func/aim/trigger.cpp src/func/misc/input.cpp src/func/misc/bhop.cpp \
       src/func/misc/quickstop.cpp \
       $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp \
       $(IMGUI)/backends/imgui_impl_win32.cpp $(IMGUI)/backends/imgui_impl_dx11.cpp
OBJ := $(addprefix $(OBJDIR)/,$(notdir $(SRC:.cpp=.o)))

vpath %.cpp src/core src/func/esp src/func/aim src/func/misc $(IMGUI) $(IMGUI)/backends

all: $(BUILD)/bluewhale.exe

$(BUILD)/bluewhale.exe: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)
	rm -rf $(OBJDIR)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)/bluewhale.exe $(OBJDIR)

.PHONY: all clean
