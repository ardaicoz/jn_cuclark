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
# Install cuCLARK
./bin/arda -i

# Setup database
./bin/arda -d <database_path>

# Classify reads
./bin/arda -c <fastq_file> <result_file> [batch_size]

# Estimate abundance
./bin/arda -a <database_path>

# Generate report
./bin/arda -r
```

## Running ARDA-MPI (Cluster Mode)

### Prerequisites

1. **OpenMPI 4.0+** installed on all nodes
2. **sshpass** installed on master node: `sudo apt install sshpass`
3. **Database** configured identically on all nodes
4. **Reads** present on each node

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
# Run pre-flight checks only
./bin/arda-mpi -c config/cluster.conf --preflight

# Run full cluster classification
./bin/arda-mpi -c config/cluster.conf

# Verbose mode
./bin/arda-mpi -c config/cluster.conf --verbose
```

### What Happens

1. **Pre-flight checks** - Verifies all nodes:
   - SSH connectivity
   - Database presence
   - Read files exist
   - cuCLARK binaries installed
   - Sufficient disk space

2. **Classification** - On each node:
   - Runs cuCLARK-l on local reads
   - Saves results locally

3. **Result collection** - Master node:
   - Copies results from all workers
   - Generates aggregated report

4. **Output** - Results stored in:
   - `results/<hostname>_<sample>.csv` - Per-node results
   - `results/aggregated/` - Combined results
   - `results/aggregated/cluster_report.txt` - Summary report
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
│                        MASTER NODE (jn00)                           │
├─────────────────────────────────────────────────────────────────────┤
│  ./bin/arda-mpi -c config/cluster.conf                              │
│                                                                     │
│  1. Load cluster.conf                                               │
│  2. Pre-flight checks (SSH to all nodes)                            │
│  3. Coordinate classification on all nodes                          │
│  4. Collect results                                                 │
│  5. Generate aggregate report                                       │
└─────────────────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│ jn01        │      │ jn04        │      │ jn10        │
├─────────────┤      ├─────────────┤      ├─────────────┤
│ cuCLARK-l   │      │ cuCLARK-l   │      │ cuCLARK-l   │
│ local reads │      │ local reads │      │ local reads │
│ local result│      │ local result│      │ local result│
└─────────────┘      └─────────────┘      └─────────────┘
```
