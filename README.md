<p align="center">
  <img src="./docs/_static/veriside_logo.png" alt="VeriSide Logo" width="300"/>
</p>

# VeriSide

**VeriSide** is a customized extension of [Verilator](https://verilator.org/), the fastest open-source Verilog/SystemVerilog simulator, designed specifically for **Power Side-Channel (PSC) Leakage Assessment at the RTL Level**.

For Verilator's official documentation, tutorials, and usage, please refer to:
- https://verilator.org/

---

## 🚀 What is VeriSide?

VeriSide is based on **Verilator v5.008** and introduces enhancements to enable efficient, scalable, and direct leakage assessment of RTL designs, particularly suited for pre-silicon security evaluation of:
- Cryptographic accelerators
- AI models
- SoCs handling sensitive data

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

- **Validated Use-Case:**  
  Case studies on CVA6 RISC-V core with a cryptographic accelerator via the **CV-X-IF interface** demonstrate VeriSide’s efficacy.

---

## 📈 Performance Comparison

| Metric                | VeriSide | Verilator + VCD |
|-----------------------|----------|-----------------|
| Disk Usage            | ~3-4 MB  | ~5-6 GB         |
| RAM for Extraction    | None     | Up to 46 GB     |
| Extraction Time       | Immediate| ~600-750 seconds|
| CPU Time              | ~86-110s | ~93-121s        |

---

## 📖 Citation

If you use VeriSide in your research or projects, please **cite the following publication**:

> B. Farnaghinejad, A. Porsia, A. Ruospo, E. Sanchez, and S. Di Carlo,  
> "VeriSide: A Modified Verilator for Leakage Assessment at the RTL Level,"  
> *2024 IEEE 33rd Asian Test Symposium (ATS)*, 2024, pp. 1-6.  
> doi: [10.1109/ATS60064.2024.00012](https://ieeexplore.ieee.org/document/10963943)

**BibTeX:**
```bibtex
@inproceedings{farnaghinejad2024veriside,
  title={VeriSide: A Modified Verilator for Leakage Assessment at the RTL Level},
  author={Farnaghinejad, Behnam and Porsia, Antonio and Ruospo, Annachiara and Sanchez, Ernesto and Di Carlo, Stefano},
  booktitle={2024 IEEE 33rd Asian Test Symposium (ATS)},
  pages={1--6},
  year={2024},
  organization={IEEE},
  doi={10.1109/ATS60064.2024.00012}
}
```

You can also access the paper here:  
👉 [https://ieeexplore.ieee.org/document/10963943](https://ieeexplore.ieee.org/document/10963943)

---

## 📬 Contributions & Support

We welcome contributions, feature requests, and bug reports!  
Please open an **issue** on this repository.

---

## 📜 License
VeriSide inherits Verilator’s licensing:
- [LGPL v3 License](https://www.gnu.org/licenses/lgpl-3.0)
- [Perl Artistic License 2.0](https://opensource.org/licenses/Artistic-2.0)
