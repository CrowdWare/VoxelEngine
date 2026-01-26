CXX ?= c++
AR ?= ar

LIB = libVoxelEngine.a
SRCS = src/voxel_engine.cpp src/voxel_renderer.cpp src/stb_image_impl.cpp
OBJS = $(SRCS:.cpp=.o)
CXXFLAGS = -std=c++11 -Iinclude -O2 -Wall -MMD -MP
CXXFLAGS += $(shell pkg-config --cflags vulkan)
DEPS = $(OBJS:.o=.d)

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(DEPS)

clean:
	rm -f $(LIB) $(OBJS) $(DEPS)
