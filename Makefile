# Acquire compile environment from sst-config utility
CXX=$(shell sst-config --CXX)
CXXFLAGS=$(shell sst-config --ELEMENT_CXXFLAGS)
LDFLAGS=$(shell sst-config --ELEMENT_LDFLAGS)

SRCS=instruction.cc cpu.cc pcie.cc hbm.cc compute_unit.cc context.cc
OBJS=$(SRCS:.cc=.o)
HEADERS=instruction.h accel_event.h context.h cpu.h pcie.h hbm.h compute_unit.h

all: libtutorial.so install

libtutorial.so: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: libtutorial.so
	sst-register tutorial tutorial_LIBDIR=$(CURDIR)

clean:
	rm -f $(OBJS) libtutorial.so
