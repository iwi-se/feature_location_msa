# Feature Location via Multiple Sequence Alignment

A C++23 research tool for **feature location in software product-line variants**. It
aligns the variants of the same source file with a **Multiple Sequence Alignment (MSA)**
and then derives, for every aligned token, the set of variants (systems) that contain
it. From those sets, feature expressions are computed and reported.

Supported input languages: **C++** and **Java** (parsed with
[tree-sitter](https://tree-sitter.github.io/)).

## How it works

The work is split across two tools that run one after the other:

```
 input variants ──► feature_location_msa ──► intermediate MSA result ──► msa_analyzer ──► HTML report
   (source files)        (msa/)               (output/*.output,          (msa_analyzer/)   or
                                                output/*.txt)                              ArgoUML SPL benchmark results
```

1. **`feature_location_msa`** (`msa/`) parses each variant of a file into an AST, splits it
   into tokens and aligns the variants against each other. It writes an **intermediate result**: the alignment
   of every file family.
2. **`msa_analyzer`** (`msa_analyzer/`) reads that intermediate result, re-parses the
   referenced source files and processes the alignment further. Depending on the mode,
   it produces either
   - a **human-readable HTML report** (`render`, based on `msa_analyzer/template.html`), or
   - results in the **ArgoUML feature location benchmark format** (`analyze`).

Both tools share the tree-sitter parsing code in `shared/`.

## Repository layout

| Path                 | Contents                                                                   |
| -------------------- | -------------------------------------------------------------------------- |
| `msa/`               | The MSA tool (`feature_location_msa`)                                      |
| `msa_analyzer/`      | The analyzer that turns MSA output into reports and benchmark results      |
| `shared/`            | Shared library: tree-sitter parser wrapper and the `node_t` AST type       |
| `atomic_type_files/` | Lists of tree-sitter node types treated as atomic (not split into tokens)  |
| `dashboard/`         | Optional live web dashboard (FastAPI) that follows a running MSA job       |
| `util/`              | Helper scripts, e.g. generating system id maps or diffing benchmark output |

## Requirements

- A C++23 compiler with parallel algorithm support (e.g. recent GCC)
- CMake ≥ 3.16
- [oneTBB](https://github.com/oneapi-src/oneTBB)
- tree-sitter libraries: `libtree-sitter`, `libtree-sitter-cpp`, `libtree-sitter-java`
- Python 3 for `dashboard/` and `util/` (optional)

## Building

`msa/` and `msa_analyzer/` are separate CMake projects with their own build directories.

```bash
# MSA tool
cd msa
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Analyzer
cd ../../msa_analyzer
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Input format

The input directory contains one subdirectory per variant. Files with the same relative
path in different variant directories form a **file family** and are aligned together:

```
<path>/
  <variant1>/<relative/path/to/File.java>
  <variant2>/<relative/path/to/File.java>
  ...
```

## Usage

### Step 1: compute the alignment (`feature_location_msa`)

```bash
./feature_location_msa <lang> <path> [--threads N] [--atomic-types-file FILE]
```

| Argument              | Description                                                    |
| --------------------- | -------------------------------------------------------------- |
| `<lang>`              | `cpp` or `java`                                                |
| `<path>`              | Input directory (see [Input format](#input-format))            |
| `--threads N`         | Number of worker threads (default: all hardware threads)       |
| `--atomic-types-file` | File listing tree-sitter node types to treat as atomic tokens  |

File families are processed in parallel. For each family the pipeline is:

1. **Preprocessing:** load the ASTs, build the token tables
2. **Alignment:** incremental multiple sequence alignment
3. **Refinement:** rare combinations of systems are refined
4. **Postprocessing:** filler (gap) tokens are sized
5. **Output:** the result is written to `output/`

The **intermediate result** in `output/` consists of, per file family:

- `<file_family>.output`: the machine-readable alignment. For each variant it holds the variant
  name, the path of the source file and a bit string with one character per alignment
  column (`1` = token, `0` = gap). **This file is the input for `msa_analyzer`.**
- `<file_family>.txt`: a plain-text version of the alignment for manual inspection.

### Step 2: process the result (`msa_analyzer`)

```bash
./msa_analyzer analyze <msa_outputs> <isolation_exe> <spl_spec> \
    [--threads N] [--atomic-types-file FILE] \
    [--feature-expressions-file FILE] [--system-id-map FILE]

./msa_analyzer render  <msa_outputs> <isolation_exe> <spl_spec> \
    [--atomic-types-file FILE] \
    [--feature-expressions-file FILE] [--system-id-map FILE]
```

| Mode               | Result                                                                                              |
| ------------------ | --------------------------------------------------------------------------------------------------- |
| `analyze`          | Results in the ArgoUML benchmark format, one `.txt` file per feature in `output/`                   |
| `render`           | A human-readable HTML report `output.html` generated from `../template.html` (run from `build/`)    |

| Argument                     | Description                                                                                 |
| ---------------------------- | ------------------------------------------------------------------------------------------- |
| `<msa_outputs>`              | A directory of `.output` files from step 1, or a single `.output` file                      |
| `<isolation_exe>`            | External executable called as `<isolation_exe> expr <spl_spec> <systems>` to obtain the feature expression for a set of systems |
| `<spl_spec>`                 | The SPL feature specification file                                                          |
| `--atomic-types-file`        | Should match the file used in step 1, so tokens line up with the alignment                  |
| `--feature-expressions-file` | Precomputed feature expressions (`<system ids>: <expression>`), used instead of calling the isolation executable |
| `--system-id-map`            | Maps variant (folder) names to numeric system ids (`<variant>,<id>`). Without it, variant names must be numbers |

### Example

```bash
# 1. Align
cd msa/build
./feature_location_msa java ../../input/argouml_traditional \
    --atomic-types-file ../../atomic_type_files/java_atomic_types.txt

# 2. Generate benchmark results
cd ../../msa_analyzer/build
./msa_analyzer analyze ../../msa/build/output <isolation_exe> <spl_spec> \
    --atomic-types-file ../../atomic_type_files/java_atomic_types.txt \
    --system-id-map ../../util/traditional_system_id_map.txt
```

## Utilities

- **Dashboard:** live view of a running MSA job:

  ```bash
  cd dashboard
  pip install -r requirements.txt
  python server.py --events-file ../msa/build/output/events.jsonl
  ```

- **`util/generate_system_id_map.py`:** builds a `--system-id-map` file from an SPL spec
  and a directory of per-variant feature selections.
- **`util/benchmark_difference.py`:** compares two ArgoUML SPL benchmark result files and lists the
  lines unique to each.
