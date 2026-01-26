CXX ?= c++
AR ?= ar

LIB = libVoxelEngine.a
SRCS = src/voxel_engine.cpp src/voxel_renderer.cpp
OBJS = $(SRCS:.cpp=.o)
CXXFLAGS = -std=c++11 -Iinclude -O2 -Wall
CXXFLAGS += $(shell pkg-config --cflags vulkan)

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(LIB) $(OBJS)
