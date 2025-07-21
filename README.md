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

## 📈 Performance Comparison 
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
