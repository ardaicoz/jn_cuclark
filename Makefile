TPROGS = getTargetsDef getAccssnTaxID getfilesToTaxNodes getAbundance #getGInTaxID
PROGS = cuCLARK cuCLARK-l $(TPROGS)

# Compiler settings
CXX = g++
MPICXX = mpicxx
CXXFLAGS = -std=c++11 -O2 -Wall

.PHONY: all clean target_definition arda arda-mpi

# install all programs in folder ./bin/
all: cuclark arda
	@echo "Build complete. Binaries in ./bin/"

cuclark:
	$(MAKE) -C src
	@mkdir -p bin
	@cp $(addprefix src/,$(PROGS)) bin/

arda:
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o bin/arda arda.cpp

arda-mpi:
	@mkdir -p bin
	$(MPICXX) $(CXXFLAGS) -o bin/arda-mpi arda_mpi.cpp -pthread
	@echo "MPI coordinator built: bin/arda-mpi"
	@echo "Usage: ./bin/arda-mpi -c config/cluster.conf"

# Build everything including MPI
full: all arda-mpi

clean:
	rm -rf bin exe
	$(MAKE) -C src clean

target_definition:
	$(MAKE) -C src target_definition
	@mkdir -p bin
	@cp  $(addprefix src/,$(TPROGS)) bin/
