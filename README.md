# Huffman Compressor

A from-scratch implementation of Huffman coding in C++17 — lossless file compression and decompression with both classic and canonical modes.

Built as a systems programming portfolio project. No external dependencies; only the C++ standard library.

---

## How it works

1. **Frequency count** — scan the input and count occurrences of each byte value (0–255)
2. **Tree construction** — build a binary min-heap, repeatedly merge the two lowest-frequency nodes (greedy, provably optimal among prefix codes)
3. **Code generation** — walk the tree; frequent bytes get shorter bit strings
4. **Encode** — replace each byte with its code, pack bits into bytes, write with a compact header
5. **Decode** — rebuild the same tree from the header, walk it bit-by-bit to recover the original

Deterministic tie-breaking (insertion order + sequence IDs) ensures the compress and decompress runs always produce the same tree shape, even when byte frequencies collide.

### Canonical mode (`--canonical`)

Inspired by gzip/DEFLATE and JPEG. Instead of storing frequencies in the header, only one byte per symbol (its code *length*) is stored. The decoder reconstructs the exact same codes from lengths alone using a fixed sorting rule, shrinking the header significantly for files with many distinct byte values.

---

## Complexity

| Phase            | Time           | Space    |
|------------------|----------------|----------|
| Frequency count  | O(n)           | O(k)     |
| Tree build       | O(k log k)     | O(k)     |
| Code generation  | O(k)           | O(k)     |
| Encode           | O(n)           | O(n)     |
| Decode           | O(n)           | O(n)     |
| **Overall**      | **O(n + k log k)** | **O(n + k)** |

`n` = input size in bytes, `k` = distinct byte values (≤ 256)

---

## Build

**Requirements:** CMake ≥ 3.16, a C++17-capable compiler (GCC ≥ 9, Clang ≥ 10, MSVC 2019+)

```bash
git clone https://github.com/raithu/huffman.git
cd huffman

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Debug build (enables AddressSanitizer + UBSan):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

---

## Usage

```bash
# Compress
./build/huffman compress   input.txt output.huff
./build/huffman compress   input.txt output.huff --canonical

# Decompress
./build/huffman decompress output.huff decoded.txt

# Round-trip self-test (compress -> decompress -> diff, no temp files left behind)
./build/huffman verify     input.txt
./build/huffman verify     input.txt --canonical

# Entropy and code table (no file written)
./build/huffman stats      input.txt
```

### Example output (`stats` mode)

```
File: input.txt (45412 bytes, 74 distinct byte values)

Shannon entropy    : 4.8821 bits/byte
Avg Huffman length : 4.9104 bits/byte
Coding efficiency  : 99.42% of entropy bound

Code table (byte : freq : code, sorted by code length):
  ' '   freq=5842   code=000 (3 bits)
  'e'   freq=5217   code=001 (3 bits)
  't'   freq=3981   code=010 (3 bits)
  ...
```

---

## File format

```
[uint32]  magic "HUFK"
[uint8]   flags (bit 0 = canonical mode)
[uint32]  FNV-based checksum of original data
[uint32]  number of distinct byte values
  classic:    per symbol → [uint8 byte][uint32 frequency]
  canonical:  per symbol → [uint8 byte][uint8 code length]
[uint32]  number of bits in the encoded stream
[bytes]   packed compressed bits (last byte zero-padded)
```

---

## Tests

CTest round-trip tests run automatically after building:

```bash
cd build && ctest --output-on-failure
```
