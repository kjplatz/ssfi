CXX = g++
DEBUG = -g3 -O0
CXXFLAGS = --std=gnu++11 -Wall $(DEBUG)
INCLUDES = -I${PWD}/include
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

ssfi.o: ssfi.cpp stripedhash.h bdqueue.h
