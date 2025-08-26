<p align="center">
  <img src="docs/_static/veriside_logo.png" alt="VeriSide Logo" width="100"/>
</p>

## 🛠️ Installation

To install VeriSide from source, follow these steps:

### Prerequisites
- GCC/Clang compiler
- Make
- Autotools (autoconf, automake)
- Perl
- Python3 (for some tests)

### Installation Commands

```bash
# Clone the repository
git clone https://github.com/smilies-polito/VeriSide.git

# Navigate to the VeriSide directory
cd VeriSide

# Set installation directory (optional)
export VERILATOR_INSTALL_DIR=/usr/local

# Configure and build
autoconf && ./configure --prefix="$VERILATOR_INSTALL_DIR"

# Compile (adjust NUM_JOBS based on your CPU cores)
make -j${NUM_JOBS:-4}

# Run tests to verify installation
make test

# Install VeriSide
make install

```

---

## 🔍 Usage


**VeriSide** is a customized extension of [Verilator](https://verilator.org/), the fastest open-source Verilog/SystemVerilog simulator, designed specifically for **Power Side-Channel (PSC) Leakage Assessment at the RTL Level**.

For Verilator's official documentation, tutorials, and usage, please refer to:
- https://verilator.org/

---

## 🚀 What is VeriSide?

VeriSide v1.0 is based on **Verilator v5.008** and introduces enhancements to enable efficient, scalable, and direct leakage assessment of RTL designs, particularly suited for pre-silicon security evaluation.

VeriSide enables **direct generation of Hamming Distance (HD)** and **Hamming Weight (HW)** data during simulation, avoiding the need for post-simulation VCD or SAIF parsing, which is resource-heavy and slow for large designs.

---

## 🔍 Key Features in VeriSide V1.0
- **Inline Leakage Tracing:**  
  Direct generation of `.side` files capturing HD/HW data per simulation, eliminating VCD generation and parsing.

- **Trigger and Instance Specification:**  
  Target specific RTL instances and trigger signals for focused analysis.

- **Resource Efficiency:**  
  - 99% reduction in disk usage compared to traditional VCD-based approaches.
  - Zero post-simulation RAM overhead for leakage data extraction.
  - Immediate trace availability post simulation.

- **Parallel Trace Collection:**  
  Retains Verilator's multi-threaded simulation capabilities while embedding side-channel analysis.

---

## �️ Usage

VeriSide extends Verilator with additional command line options specifically designed for side-channel analysis. To enable side-channel tracing in your RTL simulation, use the following options:

### Side-Channel Analysis Options

#### `--trace-side`
Enables side-channel analysis and generation of `.side` files containing Hamming Distance (HD) and Hamming Weight (HW) data.

**Usage:**
```bash
verilator --trace-side [other options] design.sv
```

#### `--side-trigger <signal_name>`
Specifies the trigger signal name that controls when side-channel data collection begins. The analysis will start capturing data when this signal transitions.

**Usage:**
```bash
verilator --trace-side --side-trigger "trigger_data_q" design.sv
```

#### `--side-modules <module1,module2,...>`
Defines a comma-separated list of module instances to monitor for side-channel analysis. Only switching activity within these specified modules will be captured in the `.side` files.

**Usage:**
```bash
verilator --trace-side --side-trigger "trigger_signal" --side-modules "cpu_core,crypto_unit,cache_controller" design.sv
```

#### `--side-hw`
When specified, VeriSide calculates **Hamming Weight (HW)** instead of **Hamming Distance (HD)**. By default, VeriSide computes Hamming Distance (bit transitions between old and new values). With this option, it counts the number of '1' bits in the new value only.

**Usage:**
```bash
# Calculate Hamming Weight instead of Hamming Distance
verilator --trace-side --side-hw --side-trigger "trigger_signal" --side-modules "cpu_core" design.sv

# Default behavior (Hamming Distance)
verilator --trace-side --side-trigger "trigger_signal" --side-modules "cpu_core" design.sv
```

### Complete Example

Here's a complete example of using VeriSide for side-channel analysis:

```bash
verilator --trace-side \
          --side-trigger "data_valid_q" \
          --side-modules "ariane_core,aes_unit,mem_controller" \
          --side-hw \
          -cc --exe \
          design.sv testbench.cpp
```

This command will:
- Enable side-channel tracing (`--trace-side`)
- Start data collection when `data_valid_q` signal triggers (`--side-trigger`)
- Monitor switching activity in the specified modules (`--side-modules`)
- Calculate Hamming Weight instead of Hamming Distance (`--side-hw`)
- Generate `.side` files with HW data for leakage assessment

### Example: Ariane RISC-V Core

VeriSide includes a modified testbench example based on the Ariane RISC-V core. You can find the example in `ariane_tb.cpp`, which demonstrates how to:

- Set up side-channel tracing with `VerilatedSide`
- Integrate VeriSide tracing into your existing testbench

**Key modifications in the example:**
```cpp
// Create side-channel trace file
VerilatedSideFile* sidep = new VerilatedSideFile;
VerilatedSide* tracep = new VerilatedSide(sidep);

// Enable tracing and open file
dut->trace(tracep, 99);
tracep->open("ariane_trace.side");
```

The `VerilatedSideFile` will contain:
1. **VCD-like Header**: Module hierarchy and signal definitions for the entire design
2. **Validation Information**: List of all filtered signals for each module specified by `--side-modules`
3. **Signal Mapping**: Shows exactly which signals are being monitored in each target module

**Example output structure:**
```
$version Generated by VerilatedSide $end
$timescale 1ps $end
$scope module TOP $end
$scope module ariane_testharness $end
...
$enddefinitions $end

Side channel configuration validated successfully:
  - Trigger signal: trigger_data_q ✓
  - Module: ariane_core ✓ (127 signals)
    * TOP.ariane_testharness.dut.i_ariane.clk_i
    * TOP.ariane_testharness.dut.i_ariane.rst_ni
    * TOP.ariane_testharness.dut.i_ariane.i_frontend.flush_i
    ...
  - Module: cache_subsystem ✓ (89 signals)
    * TOP.ariane_testharness.dut.i_ariane.i_cache_subsystem.clk_i
    * TOP.ariane_testharness.dut.i_ariane.i_cache_subsystem.icache_en_i
    ...
```

See `ariane_tb.cpp` for the complete implementation example.

### Important Notes

- **Both `--side-trigger` and `--side-modules` are required** when using `--trace-side`
- Module names should match the instance names in your RTL hierarchy
- The trigger signal should be a valid signal name accessible in your design
- Use `--side-hw` for Hamming Weight calculation, otherwise Hamming Distance is computed by default
- Generated `.side` files will be created in the same directory as your simulation executable
- VeriSide will validate that all specified modules and trigger signals exist in your design

### Output File Structure

VeriSide generates two types of files during simulation:

#### 1. **Main Trace File** (e.g., `ariane_trace.side`)
Contains a VCD-like header with module hierarchy and signal definitions, followed by validation information showing which signals were filtered for each specified module.

#### 2. **Module-Specific JSON Files** (e.g., `module_name.side`)
Individual JSON files for each module specified in `--side-modules`, containing switching activity data segmented into time windows.  
Each time window is defined by the trigger signal: a new window starts when the trigger becomes non-zero, and the window ends when the trigger returns

**JSON Format Example:**
```json
{
  "TW_0": {
    "34428": 0,
    "34429": 10,
    "34430": 25
  },
  "TW_1": {
    "35000": 2,
    "35001": 0,
    "35002": 7
  }
}
```

The JSON file contains time-windowed switching activity data. Each top-level key (e.g., `"TW_0"`, `"TW_1"`) represents a time window. Inside each window, the keys are simulation time points, and the values are the measured switching activity (e.g., Hamming Distance or Hamming Weight) at that time. This structure allows you to analyze detailed switching activity per time window and per simulation cycle.

The JSON format makes it easy to parse and analyze the data using Python, MATLAB, or any other tool that supports JSON:

```python
import json

# Load switching activity data
with open('ariane_core.side', 'r') as f:
    data = json.load(f)

# Extract switching activity per time window
for window, metrics in data.items():
    print(f"{window}: {metrics['switching_activity']} transitions")
```

---

## 📖 Citation

If you use VeriSide in your research or projects, please **cite the following publication**:

```bibtex
B. Farnaghinejad, A. Porsia, A. Ruospo, A. Savino, S. Di Carlo, and E. Sanchez, "Late Contribution: VeriSide: A Modified Verilator for Leakage Assessment at the RTL Level," in 2025 IEEE 26th Latin American Test Symposium (LATS), Mar. 2025, pp. 1–2. doi: 10.1109/LATS65346.2025.10963943.

```

You can also access the paper here:  
👉 [https://ieeexplore.ieee.org/document/10963943](https://ieeexplore.ieee.org/document/10963943)

**Performance details** and comprehensive evaluation results comparing VeriSide with traditional VCD-based approaches can be found in the cited paper.

---

## 📬 Contributions & Support

We welcome contributions, feature requests, and bug reports!  
Please open an **issue** on this repository.

---

## 📜 License
VeriSide inherits Verilator’s licensing.
