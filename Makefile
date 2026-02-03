TPROGS = getTargetsDef getAccssnTaxID getfilesToTaxNodes getAbundance #getGInTaxID
PROGS = cuCLARK cuCLARK-l $(TPROGS)

.PHONY: all clean target_definition arda

# install all programs in folder ./bin/
all:
	$(MAKE) -C src
	@mkdir -p bin
	@cp $(addprefix src/,$(PROGS)) bin/

arda:
	@mkdir -p bin
	g++ -std=c++11 -o bin/arda arda.cpp

clean:
	rm -rf bin exe
	$(MAKE) -C src clean

target_definition:
	$(MAKE) -C src target_definition
	@mkdir -p bin
	@cp  $(addprefix src/,$(TPROGS)) bin/
