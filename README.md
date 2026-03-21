# jn_cuclark

`jn_cuclark` is a GPU-accelerated metagenomic classification package for Jetson Nano-class systems. It combines the CUDA-enabled `cuCLARK` core with two C++ front ends:

- `kent`: single-node orchestration for database setup, classification, abundance estimation, and report generation
- `kent-mpi`: MPI-based coordination for multi-node runs

This repository contains the software stack. Reference databases and sequencing reads are not bundled here.

## Components

- `src/`: CUDA/C++ implementation of `cuCLARK`, `cuCLARK-l`, and helper binaries
- `app/kent.cpp`: single-node CLI wrapper around the shell scripts in `scripts/`
- `app/kent_mpi.cpp`: MPI coordinator that launches `kent` across multiple nodes
- `scripts/`: database setup, classification, abundance estimation, taxonomy update, and download helpers
- `config/cluster.conf.example`: sanitized template for cluster execution

## Requirements

- Linux with a C++11-capable compiler and GNU Make
- NVIDIA CUDA Toolkit for the full `cuCLARK` build
- OpenMPI for `kent-mpi`

Full replication requires a CUDA-enabled system. Building `kent` alone without CUDA is useful for orchestration development, but it is not a substitute for the full GPU workflow.

## Quick Start

```bash
./install.sh
./bin/kent --verify
```

Then configure a database and run classification:

```bash
./bin/kent -d /path/to/database
./bin/kent -c -O /path/to/reads.fastq -R results/sample.csv
./bin/kent -a /path/to/database results/sample.csv
./bin/kent -r
```

## Single-Node Workflow

```bash
./bin/kent -h
./bin/kent --verify
./bin/kent -d /path/to/database
./bin/kent -c -O /path/to/reads.fastq -R results/sample.csv
./bin/kent -a /path/to/database results/sample.csv
./bin/kent -m results/run1.csv results/run2.csv -o combined.csv
./bin/kent -r
```

Notes:

- `kent -d` must be run before classification so that `scripts/set_targets.sh` can generate `scripts/.settings`.
- The shell scripts in `scripts/` expect to run from the `scripts/` directory. `kent` and `kent-mpi` handle that automatically.
- Generated results go to `results/`; logs go to `logs/`.

## MPI Workflow

1. Copy the cluster template:

```bash
cp config/cluster.conf.example config/cluster.conf
```

2. Edit `config/cluster.conf` for your environment.

3. Run preflight checks, then launch:

```bash
./bin/kent-mpi -c config/cluster.conf -p
./bin/kent-mpi -c config/cluster.conf
```

The MPI configuration file uses INI syntax with `[section]` headers and `key = value` pairs.

## Repository Layout

See [STRUCTURE.md](STRUCTURE.md) for an accurate file-by-file overview of the public tree.

## License

This repository is distributed under the GNU General Public License, version 3 or later. See [LICENSE](LICENSE).
