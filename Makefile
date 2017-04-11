CXX = g++
CXXFLAGS = --std=gnu++11 -Wall 
INCLUDES = -I${PWD}/include
DEBUG = -g3 -O0
RELEASE = -O3 -DNDEBUG
LIBS = -lpthread
LDFLAGS = ${LIBS} 

SRCS = ssfi.cpp 
OBJS = $(patsubst %.cpp,%.o, $(SRCS))

all: debug

clean:
	rm $(OBJS) ssfi

map-debug: CXXFLAGS += -DUSE_MAP
map-debug: debug

debug: CXXFLAGS += $(DEBUG)
debug: ssfi

map-release: CXXFLAGS += -DUSE_MAP
map-release: release

release: CXXFLAGS += $(RELEASE)
release: ssfi

striped-debug: CXXFLAGS += -DUSE_STRIPED
striped-debug: striped

striped:       CXXFLAGS += -DRELEASE
striped: ssfi

ssfi: $(OBJS) 
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

ssfi.o: ssfi.cpp stripedhash.h bdqueue.h
