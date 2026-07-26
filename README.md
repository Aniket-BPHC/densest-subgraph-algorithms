# Densest Subgraph Algorithms

Implementation of exact and approximate algorithms for the **Densest Subgraph Problem (DSP)** using both flow-based and flowless techniques, evaluated on large-scale real-world graphs.

---

## Research Papers

- Efficient Algorithms for Densest Subgraph Discovery (Flow-based exact methods)  
  https://doi.org/10.14778/3342263.3342645  

- Flowless: Extracting Densest Subgraphs Without Flow Computations  
  https://doi.org/10.1145/3366423.3380140  

---

## Overview

This project implements four algorithms for finding dense subgraphs:

### Approximate (Scalable)
- **Charikar Greedy**
- **Greedy++**

### Exact (Flow-based)
- **Exact Triangle-Density DSP**
- **CoreExact (Optimized exact method)**

The implementations are tested on real-world datasets with up to **millions of nodes and edges**, highlighting trade-offs between scalability and optimality.

---

## Datasets

Download the following datasets:

- Email-Enron  
  https://snap.stanford.edu/data/email-Enron.html  

- AS-Skitter  
  https://snap.stanford.edu/data/as-Skitter.html  

- Wiki-Vote  
  https://snap.stanford.edu/data/wiki-Vote.html  

After downloading, place all `.txt.gz` files **in the same directory as the source code**.

---

## File Structure

```
.
├── algorithm_a.cpp
├── algorithm_b.cpp
├── final_dense-subgraph_algo1.cpp
├── dense-subgraph_4.cpp
├── common.h
├── run.sh
├── run.ps1
├── README.md
├── wiki-Vote.txt.gz
├── email-Enron.txt.gz
├── as-skitter.txt.gz
```

---

## Dependencies

### Linux
- g++ (C++17)
- zlib

Install:
```
sudo apt install build-essential zlib1g-dev
```

### Windows
- MinGW / MSYS2
- zlib (`-lz`)
- psapi (`-lpsapi`)

---

## Compilation

### Linux

```
g++ -O3 -std=c++17 algorithm_a.cpp -o algorithm_a -lz
g++ -O3 -std=c++17 algorithm_b.cpp -o algorithm_b -lz
g++ -O3 -std=c++17 final_dense-subgraph_algo1.cpp -o dense-subgraph_algo1 -lz
g++ -O3 -std=c++17 dense-subgraph_4.cpp -o dense-subgraph_4 -lz
```

---

### Windows

```
g++ -O3 -std=c++17 algorithm_a.cpp -o algorithm_a -lpsapi -lz
g++ -O3 -std=c++17 algorithm_b.cpp -o algorithm_b -lpsapi -lz
g++ -O3 -std=c++17 final_dense-subgraph_algo1.cpp -o dense-subgraph_algo1 -lpsapi -lz
g++ -O3 -std=c++17 dense-subgraph_4.cpp -o dense-subgraph_4 -lpsapi -lz
```

---

## Running the Algorithms

### Greedy Algorithms (run on ALL datasets automatically)

```
./algorithm_a
./algorithm_b
```

---

### Exact Flow-based Algorithm (run per dataset)

```
./dense-subgraph_algo1 wiki-Vote.txt.gz
./dense-subgraph_algo1 email-Enron.txt.gz
./dense-subgraph_algo1 as-skitter.txt.gz
```

---

### CoreExact Algorithm (run on ALL datasets)

```
./dense-subgraph_4
```

---

## Observations

- Greedy algorithms scale efficiently to very large graphs  
- Exact algorithm guarantees optimal solution but is memory-intensive  
- CoreExact improves performance using core-based pruning  
- Large datasets (e.g., AS-Skitter) require significant memory  

---

## Notes

- Exact algorithm on `as-skitter` may fail due to high memory usage (>1GB)  
- CoreExact on large datasets can take ~50 minutes and ~1.5GB RAM  
- Ensure all dataset files are in the same directory before running  
- Outputs are printed to standard output  

---

## Key Learnings

- Implementing research-level algorithms from papers  
- Handling large-scale graph datasets efficiently  
- Understanding trade-offs between exact and approximate solutions  
- Working with memory and runtime constraints in real systems  

