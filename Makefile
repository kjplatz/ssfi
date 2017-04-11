CXX = g++
CXXFLAGS = --std=gnu++11 -Wall -g3 -O0 -DUSE_MAP
INCLUDES = -I${PWD}/include
DEBUG = -g3 -O0
RELEASE = -O3 -DNDEBUG
LIBS = -lpthread
LDFLAGS = ${LIBS} 

SRCS = ssfi.cpp 
OBJS = $(patsubst %.cpp,%.o, $(SRCS))

all: ssfi

clean:
	rm $(OBJS) ssfi
debug:
	CXXFLAGS := $(CXXFLAGS) $(DEBUG)
	make ssfi-debug

ssfi: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

ssfi-debug: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)
