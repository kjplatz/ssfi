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

debug: CXXFLAGS += $(DEBUG)
debug: ssfi

release: CXXFLAGS += $(RELEASE)
release: ssfi

map-debug: CXXFLAGS += -DUSE_MAP
map-debug: debug

map-release: CXXFLAGS += -DUSE_MAP
map-release: release


unstriped-release: CXXFLAGS += -DHASH_NO_STRIPE
unstriped-release: release

unstriped-debug: CXXFLAGS += -DHASH_NO_STRIPE
unstriped-debug: debug


ssfi: $(OBJS) 
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

ssfi.o: ssfi.cpp stripedhash.h bdqueue.h
