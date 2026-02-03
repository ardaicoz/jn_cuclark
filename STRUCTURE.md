# Project Structure (Refactored)

## Directory Layout

```
jn_cuclark/
├── README.md                   # Main documentation
├── LICENSE_GNU_GPL.txt         # License
├── CHANGELOG.md                # Version history
├── Makefile                    # Build system
├── arda.cpp                    # Main orchestrator source
│
├── bin/                        # Compiled executables (generated)
│   ├── arda                    # Main orchestrator binary
│   ├── cuCLARK                 # GPU classifier (full)
│   ├── cuCLARK-l               # GPU classifier (light, for Jetson)
│   ├── getAbundance            # Abundance estimation tool
│   ├── getAccssnTaxID          # Taxonomy ID mapping
│   ├── getfilesToTaxNodes      # Taxonomy node mapping
│   └── getTargetsDef           # Target definition tool
│
├── config/                     # Configuration files (future use)
│   └── (empty - for MPI config)
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
│   ├── result.csv              # Raw classification output
│   ├── abundance_result.txt    # Abundance estimates
│   └── report.txt              # Human-readable report
│
├── logs/                       # Log files (generated)
│   └── ardacpp_log.txt         # Installation & execution logs
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
# Build all cuCLARK components
make

# Build only arda orchestrator
make arda

# Clean build artifacts
make clean
```

## Running ARDA

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

## File Locations

| File Type | Location | Description |
|-----------|----------|-------------|
| Executables | `bin/` | All compiled binaries |
| Scripts | `scripts/` | Shell scripts for various operations |
| Results | `results/` | Classification outputs, reports |
| Logs | `logs/` | Execution and error logs |
| Source | `src/` | C++/CUDA source files |
| Config | `config/` | Configuration files (future) |

## Notes for MPI Integration

The new structure is ready for MPI implementation:

1. **config/** - Add MPI cluster configuration
2. **logs/** - Per-node log files
3. **results/** - Per-node result directories
4. **bin/arda** - Ready to become MPI coordinator

Next steps:
- Add MPI initialization to arda.cpp
- Create node-specific result directories (results/node_001/, etc.)
- Implement work distribution across cluster
- Aggregate results from all nodes
