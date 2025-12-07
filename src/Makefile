CXXFLAGS = -O3
NVCC = nvcc #/usr/local/cuda/bin/nvcc
# ARDA: Jetson's cuda comp is 5.3, so changing this from sm_30 to sm_53
# Target multiple architectures to support "many but powerful" GPUs (Pascal to Ada/Hopper).
# This creates a "fat binary" containing optimized code for each listed architecture.
# - sm_60 (Pascal): GTX 1080 Ti, P100
# - sm_70 (Volta): V100
# - sm_75 (Turing): RTX 2080 Ti, T4
# - sm_80 (Ampere): A100, RTX 3090
# - sm_86 (Ampere): RTX 3060/3070/3080
# - sm_89 (Ada): RTX 4090
NVCCFLAGS = -O3 \
	-gencode arch=compute_60,code=sm_60 \
	-gencode arch=compute_70,code=sm_70 \
	-gencode arch=compute_75,code=sm_75 \
	-gencode arch=compute_80,code=sm_80 \
	-gencode arch=compute_86,code=sm_86 \
	-gencode arch=compute_89,code=sm_89 \
	-gencode arch=compute_89,code=compute_89
# Checking for openmp
NB = $(shell echo |cpp -fopenmp -dM |grep -i open | wc -l)
ifeq ($(NB),1)
OPENMP = -Xcompiler -fopenmp
endif

CUCLARKCC = CuClarkDB.cu main.cc analyser.cc file.cc kmersConversion.cc
CUCLARK = $(CUCLARKCC) CuClarkDB.cuh CuCLARK_hh.hh dataType.hh hashTable_hh.hh HashTableStorage_hh.hh analyser.hh dataType.hh file.hh kmersConversion.hh

TPROGS = getTargetsDef getAccssnTaxID getfilesToTaxNodes getAbundance #getGInTaxID
PROGS = cuCLARK cuCLARK-l $(TPROGS)

.PHONY: all clean target_definition debug

all: $(PROGS)

clean:
	rm -f $(PROGS) *.o
	
debug: NVCCFLAGS += -DTIME_DBLOADING -DDEBUG_DMEM #-DDEBUG_HMEM -DDEBUG_DB -DDEBUGQUERY -DDEBUG_KERNEL -DDEBUG_MERGE -DDEBUG_RESULT -DDEBUG_BATCH
debug: cuCLARK cuCLARK-l

cuCLARK: $(CUCLARK) parameters.hh
	$(NVCC) $(NVCCFLAGS) $(OPENMP) -o cuCLARK $(CUCLARKCC)

cuCLARK-l: $(CUCLARK) parameters_light_hh
	@mv parameters.hh parameters_full_hh
	@cp parameters_light_hh parameters.hh
	 $(NVCC) $(NVCCFLAGS) $(OPENMP) -o cuCLARK-l $(CUCLARKCC)
	@mv parameters_full_hh parameters.hh

target_definition: $(TPROGS)

getTargetsDef: getTargetsDef.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o getTargetsDef getTargetsDef.cc file.cc

#getGInTaxID: getGInTaxID.cc file.cc file.hh
#	$(CXX) $(CXXFLAGS) -o getGInTaxID getGInTaxID.cc file.cc

getAccssnTaxID: getAccssnTaxID.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o getAccssnTaxID getAccssnTaxID.cc file.cc

getfilesToTaxNodes: getfilesToTaxNodes.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o getfilesToTaxNodes getfilesToTaxNodes.cc file.cc

getAbundance: getAbundance.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o getAbundance getAbundance.cc file.cc
