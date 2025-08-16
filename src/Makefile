# ---- Toolchains ----
ROCM_PATH ?= /opt/rocm

HIPCC      ?= hipcc
HIP_ARCHS  ?= gfx1150
HIPCCFLAGS  = -O3 $(addprefix --offload-arch=,$(HIP_ARCHS))

# Host C++ compiler (plain C++; not HIP)
CXX       ?= g++
CXXFLAGS  ?= -O3
# Add HIP headers so host TUs can include <hip/hip_runtime.h>
CXXFLAGS  += -I$(ROCM_PATH)/include

# Tell HIP headers which platform we target for host builds
# (hipcc sets this automatically, but g++ doesn't)
HIP_PLATFORM ?= AMD
ifeq ($(HIP_PLATFORM),AMD)
  CXXFLAGS += -D__HIP_PLATFORM_AMD__
else ifeq ($(HIP_PLATFORM),NVIDIA)
  CXXFLAGS += -D__HIP_PLATFORM_NVIDIA__
else
  $(error HIP_PLATFORM must be AMD or NVIDIA)
endif

# Detect OpenMP support for host compiler
NB = $(shell echo | cpp -fopenmp -dM | grep -i open | wc -l)
ifeq ($(NB),1)
  OPENMP = -fopenmp
endif

# ---- Sources ----
HIP_SRCS := CuClarkDB.cu.hip
CPP_SRCS := main.cc analyser.cc file.cc kmersConversion.cc
HIP_OBJS := $(HIP_SRCS:.cu.hip=.o)
CPP_OBJS := $(CPP_SRCS:.cc=.o)
OBJS     := $(HIP_OBJS) $(CPP_OBJS)

CUCLARK_HEADERS = CuClarkDB.cuh.hip CuCLARK_hh.hh dataType.hh hashTable_hh.hh \
                  HashTableStorage_hh.hh analyser.hh file.hh kmersConversion.hh parameters.hh

TPROGS = getTargetsDef getAccssnTaxID getfilesToTaxNodes getAbundance
PROGS  = cuCLARK cuCLARK-l $(TPROGS)

.PHONY: all clean clean-objs debug target_definition

all: $(PROGS)

# ---- Rules ----

# Compile HIP kernels (device compile)
%.o: %.cu.hip $(CUCLARK_HEADERS)
	$(HIPCC) $(HIPCCFLAGS) -c $< -o $@

# Compile host C++ (plain, with OpenMP + HIP headers + platform macro)
%.o: %.cc $(CUCLARK_HEADERS)
	$(CXX) $(CXXFLAGS) $(OPENMP) -c $< -o $@

# Link final binary with hipcc (so HIP runtime is linked)
cuCLARK: $(OBJS) parameters.hh
	$(HIPCC) $(HIPCCFLAGS) $(OPENMP) $(OBJS) -o $@

# "Light" build toggles parameters and rebuilds objects
cuCLARK-l: parameters_light_hh
	@mv parameters.hh parameters_full_hh
	@cp parameters_light_hh parameters.hh
	$(MAKE) clean-objs
	$(MAKE) cuCLARK
	@cp cuCLARK cuCLARK-l
	@mv parameters_full_hh parameters.hh

# Utilities (host-only)
getTargetsDef: getTargetsDef.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o $@ getTargetsDef.cc file.cc

getAccssnTaxID: getAccssnTaxID.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o $@ getAccssnTaxID.cc file.cc

getfilesToTaxNodes: getfilesToTaxNodes.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o $@ getfilesToTaxNodes.cc file.cc

getAbundance: getAbundance.cc file.cc file.hh
	$(CXX) $(CXXFLAGS) -o $@ getAbundance.cc file.cc

target_definition: $(TPROGS)

# Debug toggles (affect HIP compilation)
debug: HIPCCFLAGS += -DTIME_DBLOADING -DDEBUG_DMEM
debug: cuCLARK cuCLARK-l

clean:
	rm -f $(PROGS) *.o

clean-objs:
	rm -f $(OBJS)
