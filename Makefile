CXX = g++
CXXFLAGS = --std=gnu++11 -Wall
INCLUDES = -I${PWD}/include
DEBUG = -g3 -O0
RELEASE = -O3 -DNDEBUG
LIBS = -lpthread
LDFLAGS = ${LIBS} 

SRCS = ssfi.cpp
OBJS = $(patsubst %.c,%.o, $(SRCS))

clean:
	rm $(OBJS)
debug:
	CXXFLAGS := $(CXXFLAGS) $(DEBUG)
	make ssfi-debug

ssfi: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

ssfi-debug: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)
