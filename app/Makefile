TPROGS = getTargetsDef getAccssnTaxID getfilesToTaxNodes getAbundance #getGInTaxID
PROGS = cuCLARK cuCLARK-l $(TPROGS)

# Compiler settings
CXX = g++
MPICXX = mpicxx
CXXFLAGS = -std=c++11 -O2 -Wall

ROOT = ..

.PHONY: all clean target_definition kent kent-mpi

# install all programs in $(ROOT)/bin/
all: cuclark kent
	@echo "Build complete. Binaries in $(ROOT)/bin/"

cuclark:
	$(MAKE) -C $(ROOT)/src
	@mkdir -p $(ROOT)/bin
	@cp $(addprefix $(ROOT)/src/,$(PROGS)) $(ROOT)/bin/

kent:
	@mkdir -p $(ROOT)/bin
	$(CXX) $(CXXFLAGS) -o $(ROOT)/bin/kent kent.cpp

kent-mpi:
	@mkdir -p $(ROOT)/bin
	$(MPICXX) $(CXXFLAGS) -o $(ROOT)/bin/kent-mpi kent_mpi.cpp -pthread
	@echo "MPI coordinator built: $(ROOT)/bin/kent-mpi"
	@echo "Usage: ./bin/kent-mpi -c config/cluster.conf"

# Build everything including MPI
full: all kent-mpi

clean:
	rm -rf $(ROOT)/bin exe
	$(MAKE) -C $(ROOT)/src clean

target_definition:
	$(MAKE) -C $(ROOT)/src target_definition
	@mkdir -p $(ROOT)/bin
	@cp $(addprefix $(ROOT)/src/,$(TPROGS)) $(ROOT)/bin/
