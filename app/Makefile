TPROGS = getTargetsDef getAccssnTaxID getfilesToTaxNodes getAbundance #getGInTaxID
PROGS = cuCLARK cuCLARK-l $(TPROGS)

# Compiler settings
CXX = g++
MPICXX = mpicxx
CXXFLAGS = -std=c++11 -O2 -Wall

ROOT = ..

.PHONY: all clean target_definition arda arda-mpi

# install all programs in $(ROOT)/bin/
all: cuclark arda
	@echo "Build complete. Binaries in $(ROOT)/bin/"

cuclark:
	$(MAKE) -C $(ROOT)/src
	@mkdir -p $(ROOT)/bin
	@cp $(addprefix $(ROOT)/src/,$(PROGS)) $(ROOT)/bin/

arda:
	@mkdir -p $(ROOT)/bin
	$(CXX) $(CXXFLAGS) -o $(ROOT)/bin/arda arda.cpp

arda-mpi:
	@mkdir -p $(ROOT)/bin
	$(MPICXX) $(CXXFLAGS) -o $(ROOT)/bin/arda-mpi arda_mpi.cpp -pthread
	@echo "MPI coordinator built: $(ROOT)/bin/arda-mpi"
	@echo "Usage: ./bin/arda-mpi -c config/cluster.conf"

# Build everything including MPI
full: all arda-mpi

clean:
	rm -rf $(ROOT)/bin exe
	$(MAKE) -C $(ROOT)/src clean

target_definition:
	$(MAKE) -C $(ROOT)/src target_definition
	@mkdir -p $(ROOT)/bin
	@cp $(addprefix $(ROOT)/src/,$(TPROGS)) $(ROOT)/bin/
