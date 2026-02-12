# Project Structure (Refactored)

## Directory Layout

```
jn_cuclark/
├── README.md                   # Main documentation
├── LICENSE_GNU_GPL.txt         # License
├── CHANGELOG.md                # Version history
├── Makefile                    # Build system
├── arda.cpp                    # Single-node orchestrator source
├── arda_mpi.cpp                # MPI cluster coordinator source
│
├── bin/                        # Compiled executables (generated)
│   ├── arda                    # Single-node orchestrator binary
│   ├── arda-mpi                # MPI cluster coordinator binary
│   ├── cuCLARK                 # GPU classifier (full)
│   ├── cuCLARK-l               # GPU classifier (light, for Jetson)
│   ├── getAbundance            # Abundance estimation tool
│   ├── getAccssnTaxID          # Taxonomy ID mapping
│   ├── getfilesToTaxNodes      # Taxonomy node mapping
│   └── getTargetsDef           # Target definition tool
│
├── config/                     # Configuration files
│   ├── cluster.conf.example    # Example cluster configuration (YAML)
│   └── cluster.conf            # Your cluster configuration (create from example)
│
├── src/                        # Source code
│   ├── main.cc                 # CuCLARK main
│   ├── CuClarkDB.cu            # CUDA database implementation
│   ├── analyser.cc             # Classification analyzer
│   └── ...                     # Other source files
│
├── scripts/                    # Shell scripts
│   ├── install.sh              # Installation script
│   ├── classify_metagenome.sh  # Classification wrapper
│   ├── estimate_abundance.sh   # Abundance estimation wrapper
│   ├── set_targets.sh          # Database target setup
│   ├── make_metadata.sh        # Metadata generation
│   ├── resetCustomDB.sh        # Database reset
│   ├── updateTaxonomy.sh       # Taxonomy update
│   ├── download/               # Download scripts
│   │   ├── download_data.sh
│   │   ├── download_data_newest.sh
│   │   ├── download_data_release.sh
│   │   └── download_taxondata.sh
│   └── maintenance/            # Maintenance scripts
│
├── data/                       # Input data directory
│   └── README.md
│
├── results/                    # Classification results (generated)
│   ├── <hostname>_<sample>.csv       # Per-node raw results
│   ├── <hostname>_<sample>_abundance.txt  # Per-node abundance
│   ├── aggregated/             # Combined results from all nodes
│   │   └── cluster_report.txt  # Final aggregated report
│   └── report.txt              # Human-readable report
│
├── logs/                       # Log files (generated)
│   ├── ardacpp_log.txt         # Single-node execution logs
│   └── cluster_run.log         # MPI cluster run logs
│
└── .gitignore                  # Git ignore rules

```

## Key Changes from Original Structure

### Before Refactoring
- Executables mixed in root and `exe/` directory
- Scripts scattered in root directory
- Results and logs in root directory
- No organized configuration management
- Build outputs mixed with source code

### After Refactoring
- **bin/** - All executables in one place
- **scripts/** - Organized shell scripts with subdirectories
- **results/** - Dedicated output directory
- **logs/** - Dedicated logging directory
- **config/** - Ready for configuration files (MPI, cluster, etc.)
- Clean separation between source and build artifacts

## Building the Project

```bash
# Build all cuCLARK components + single-node arda
make

# Build only single-node arda orchestrator
make arda

# Build MPI cluster coordinator
make arda-mpi

# Build everything (cuCLARK + arda + arda-mpi)
make full

# Clean build artifacts
make clean
```

## Running ARDA (Single Node)

```bash
# Install cuCLARK (first-time setup)
./install.sh

# Verify installation
./bin/arda --verify

# Setup database
./bin/arda -d <database_path>

# Classify reads
./bin/arda -c <fastq_file> <result_file> [batch_size]

# Estimate abundance
./bin/arda -a <database_path> <result_file>

# Generate report
./bin/arda -r
```

**Note:** The legacy `-i` flag now performs verification only (no builds). Use `./install.sh` for initial installation or rebuilds.

## Running ARDA-MPI (Cluster Mode)

### Prerequisites

1. **OpenMPI 4.0+** installed on all nodes
2. **Passwordless SSH** set up from master to all workers
3. **Database** configured identically on all nodes
4. **Reads** present on each node
5. **Same binary path** - arda-mpi must be at the same path on all nodes

### Setup Passwordless SSH

On master node (jn00):
```bash
# Generate SSH key if you don't have one
ssh-keygen -t rsa -b 4096

# Copy to all worker nodes
ssh-copy-id jn01
ssh-copy-id jn03
# ... repeat for all workers
```

### Setup

1. Copy the example config:
   ```bash
   cp config/cluster.conf.example config/cluster.conf
   ```

2. Edit `config/cluster.conf` with your cluster settings:
   - Set master and worker hostnames
   - Configure paths (must be same on all nodes)
   - Specify read files for each node

### Execution

```bash
# Run pre-flight checks only (tests MPI connectivity)
./bin/arda-mpi -c config/cluster.conf -p

# Run full cluster classification
./bin/arda-mpi -c config/cluster.conf

# Verbose mode
./bin/arda-mpi -c config/cluster.conf -v
```

### What Happens

1. **arda-mpi loads config** and generates hostfile
2. **Automatically calls mpirun** with itself as the worker
3. **All nodes run in parallel** via MPI:
   - Master broadcasts config to all workers
   - Each node runs cuCLARK-l on its local reads
   - Workers send results back to master
4. **Master aggregates results** and generates report

### Output

- `results/<hostname>_<sample>.csv` - Per-node classification results
- `results/<hostname>_<sample>_abundance.txt` - Per-node abundance
- `results/cluster_report.txt` - Summary report with timing
- `logs/cluster_run.log` - Execution log

## File Locations

| File Type | Location | Description |
|-----------|----------|-------------|
| Executables | `bin/` | All compiled binaries |
| Scripts | `scripts/` | Shell scripts for various operations |
| Results | `results/` | Classification outputs, reports |
| Logs | `logs/` | Execution and error logs |
| Source | `src/` | C++/CUDA source files |
| Config | `config/` | Cluster configuration (YAML) |

## MPI Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                    USER RUNS: ./bin/arda-mpi -c cluster.conf        │
│                                                                     │
│  1. Load config, generate hostfile                                  │
│  2. Call: mpirun --hostfile hostfile -np N arda-mpi --mpi-worker    │
└─────────────────────────────────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐      ┌───────────────┐      ┌───────────────┐
│ MPI Rank 0    │      │ MPI Rank 1    │      │ MPI Rank N    │
│ (jn00)        │      │ (jn01)        │      │ (jn10)        │
├───────────────┤      ├───────────────┤      ├───────────────┤
│ 1. Bcast cfg  │      │ 1. Recv cfg   │      │ 1. Recv cfg   │
│ 2. Run local  │ ───► │ 2. Run local  │ ◄─── │ 2. Run local  │
│    cuCLARK-l  │  MPI │    cuCLARK-l  │  MPI │    cuCLARK-l  │
│ 3. Recv all   │      │ 3. Send result│      │ 3. Send result│
│    results    │ ◄─── │    to rank 0  │ ───► │    to rank 0  │
│ 4. Aggregate  │      └───────────────┘      └───────────────┘
│    & report   │
└───────────────┘

All nodes process in PARALLEL - total time = slowest node time
```
