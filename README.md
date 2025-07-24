<p align="center">
  <img src="./docs/_static/veriside_logo.png" alt="VeriSide Logo" width="70"/>
</p>

#  

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

### Complete Example

Here's a complete example of using VeriSide for side-channel analysis:

```bash
verilator --trace-side \
          --side-trigger "data_valid_q" \
          --side-modules "ariane_core,aes_unit,mem_controller" \
          -cc --exe \
          design.sv testbench.cpp
```

This command will:
- Enable side-channel tracing (`--trace-side`)
- Start data collection when `data_valid_q` signal triggers (`--side-trigger`)
- Monitor switching activity in the specified modules (`--side-modules`)
- Generate `.side` files with HD/HW data for leakage assessment

### Important Notes

- **Both `--side-trigger` and `--side-modules` are required** when using `--trace-side`
- Module names should match the instance names in your RTL hierarchy
- The trigger signal should be a valid signal name accessible in your design
- Generated `.side` files will be created in the same directory as your simulation executable

---

## �📈 Performance Comparison 
Case studies on CVA6 RISC-V core with a cryptographic accelerator via the CV-X-IF interface demonstrate VeriSide’s efficacy.

| Metric                | VeriSide | Verilator + VCD |
|-----------------------|----------|-----------------|
| Disk Usage            | ~3-4 MB  | ~5-6 GB         |
| RAM for Extraction    | None     | Up to 46 GB     |
| Extraction Time       | Immediate| ~600-750 seconds|
| CPU Time              | ~86-110s | ~93-121s        |

---

## 📖 Citation

If you use VeriSide in your research or projects, please **cite the following publication**:

```bibtex
B. Farnaghinejad, A. Porsia, A. Ruospo, A. Savino, S. Di Carlo, and E. Sanchez, “Late Contribution: VeriSide: A Modified Verilator for Leakage Assessment at the RTL Level,” in 2025 IEEE 26th Latin American Test Symposium (LATS), Mar. 2025, pp. 1–2. doi: 10.1109/LATS65346.2025.10963943.

```

You can also access the paper here:  
👉 [https://ieeexplore.ieee.org/document/10963943](https://ieeexplore.ieee.org/document/10963943)

---

## 📬 Contributions & Support

We welcome contributions, feature requests, and bug reports!  
Please open an **issue** on this repository.

---

## 📜 License
VeriSide inherits Verilator’s licensing.
