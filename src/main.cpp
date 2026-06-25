/*
 * Hufk
 * ---------------------------------------------
 * USAGE:
 *   ./huffman compress   input.txt   output.huff   [--canonical]
 *   ./huffman decompress output.huff decoded.txt
 *   ./huffman verify     input.txt              (round-trip self-test)
 *   ./huffman stats      input.txt              (entropy / code table info)
 *
 *   --canonical : use canonical Huffman codes, which let the header store
 *                 just a code LENGTH per symbol (1 byte) instead of a full
 *                 32-bit frequency. Decoder rebuilds the exact same codes
 *                 from (symbol, length) pairs alone -- no tree needed to
 *                 serialize, smaller file header. See buildCanonicalCodes().
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <map>
#include <queue>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iomanip>

using namespace std;

// ---------------------------------------------------------------------
// 1. Tree node
// ---------------------------------------------------------------------
// NOTE: byte is `unsigned char`, not `char`. `char` has implementation-
// defined signedness; on platforms where it's signed (most x86 Linux/Mac
// toolchains), bytes >= 0x80 become negative values, which then collide
// or misbehave as unordered_map keys and break ordering in the sort
// comparator below. Using unsigned char makes every byte 0-255 a valid,
// well-ordered key, so the tool works on arbitrary binary input, not
// just 7-bit ASCII text.
struct Node {
    unsigned char ch;  // byte value (only meaningful in leaf nodes)
    long long freq;    // frequency count (or sum of children's freq)
    int seq;           // tie-break id, assigned in deterministic order
    Node* left;
    Node* right;

    Node(unsigned char c, long long f, int s, Node* l = nullptr, Node* r = nullptr)
        : ch(c), freq(f), seq(s), left(l), right(r) {}

    bool isLeaf() const { return !left && !right; }
};

// Comparator for the min-heap: smallest frequency has highest priority.
//
// when two nodes have equal frequency, priority_queue's order
// between them is unspecified -- it depends on insertion order. Since we
// insert from an unordered_map, insertion order can differ between the
// compress run (map built by scanning the file) and the decompress run
// (map built by reading a serialized table), even though the frequency
// VALUES are identical. That non-determinism would silently build a
// different-shaped tree (hence different codes) during decompression,
// corrupting the output.
//
// Fix: break ties using a stable, content-derived key. We use a
// monotonically increasing "sequence id" assigned in a fixed, sorted
// order (see buildHuffmanTree) so the same multiset of (byte, freq)
// pairs always produces the same tree, regardless of map iteration order.
struct Compare {
    bool operator()(Node* a, Node* b) {
        if (a->freq != b->freq) return a->freq > b->freq; // min-heap
        return a->seq > b->seq; // tie-break deterministically
    }
};

// ---------------------------------------------------------------------
// 2. Build the Huffman Tree from a frequency map
//    Classic greedy algorithm using a priority queue (min-heap)
//    Optimality: Huffman's algorithm is provably optimal among prefix
//    codes (exchange argument / matroid greedy proof) -- no other
//    prefix-free code achieves a shorter expected length for this
//    symbol distribution. It does NOT generally hit the Shannon entropy
//    bound exactly, because codes are constrained to integer bit
//    lengths (see computeStats()).
// ---------------------------------------------------------------------
Node* buildHuffmanTree(const unordered_map<unsigned char, long long>& freqMap) {
    priority_queue<Node*, vector<Node*>, Compare> pq;

    // Copy into a vector and sort by byte value first. This guarantees
    // leaves are inserted into the heap in the SAME order every time,
    // regardless of how the unordered_map happened to iterate -- which
    // is what makes tie-breaking (and therefore the resulting tree)
    // deterministic between the compress and decompress runs.
    vector<pair<unsigned char, long long>> sortedFreqs(freqMap.begin(), freqMap.end());
    sort(sortedFreqs.begin(), sortedFreqs.end(),
         [](const pair<unsigned char, long long>& a, const pair<unsigned char, long long>& b) {
             return a.first < b.first;
         });

    int nextSeq = 0;
    for (auto& p : sortedFreqs) {
        pq.push(new Node(p.first, p.second, nextSeq++));
    }

    // Edge case: only one unique byte value in the file
    if (pq.size() == 1) {
        Node* only = pq.top();
        return new Node(0, only->freq, nextSeq++, only, nullptr);
    }

    // Repeatedly merge the two smallest nodes until one tree remains
    while (pq.size() > 1) {
        Node* left = pq.top(); pq.pop();
        Node* right = pq.top(); pq.pop();

        Node* merged = new Node(0, left->freq + right->freq, nextSeq++, left, right);
        pq.push(merged);
    }

    return pq.top(); // root of the Huffman tree
}

// ---------------------------------------------------------------------
// 3. Walk the tree to generate binary codes for each byte
//    left edge = '0', right edge = '1'
// ---------------------------------------------------------------------
void generateCodes(Node* root, const string& path,
                    unordered_map<unsigned char, string>& codes) {
    if (!root) return;

    if (root->isLeaf()) {
        // Handle the edge case of a single unique byte value:
        // give it code "0" so encoding isn't empty.
        codes[root->ch] = path.empty() ? "0" : path;
        return;
    }

    generateCodes(root->left, path + "0", codes);
    generateCodes(root->right, path + "1", codes);
}

// ---------------------------------------------------------------------
// 3b. Canonical Huffman codes
// ---------------------------------------------------------------------
// Why this exists (good interview talking point): the *shape* of a
// Huffman tree is not unique for a given set of code lengths -- many
// trees share the same length for each symbol. Canonical Huffman fixes
// a single deterministic assignment from (symbol, code length) pairs
// alone, with a simple rule:
//   1. Sort symbols by (code length asc, symbol value asc).
//   2. Assign codes in that order, starting at 0, incrementing by 1
//      each time, and left-shifting whenever code length increases.
// Benefit: the encoder only needs to ship ONE byte per symbol (its code
// length, 1-255) instead of a 4-byte frequency -- the decoder derives
// the exact same codes from lengths alone. This is what gzip/DEFLATE
// and JPEG actually do in practice, instead of shipping the full tree.
struct CanonicalEntry { unsigned char symbol; int length; };

unordered_map<unsigned char, string> buildCanonicalCodes(
    const unordered_map<unsigned char, string>& originalCodes) {

    vector<CanonicalEntry> entries;
    for (auto& kv : originalCodes) {
        entries.push_back({kv.first, (int)kv.second.size()});
    }
    sort(entries.begin(), entries.end(), [](const CanonicalEntry& a, const CanonicalEntry& b) {
        if (a.length != b.length) return a.length < b.length;
        return a.symbol < b.symbol;
    });

    unordered_map<unsigned char, string> canonical;
    long long code = 0;
    int prevLen = entries.empty() ? 0 : entries[0].length;

    for (auto& e : entries) {
        code <<= (e.length - prevLen);
        string bits;
        bits.reserve(e.length);
        for (int i = e.length - 1; i >= 0; i--) bits += ((code >> i) & 1) ? '1' : '0';
        canonical[e.symbol] = bits;
        code++;
        prevLen = e.length;
    }
    return canonical;
}

// Rebuild a decode tree purely from (symbol, length) pairs, using the
// same canonical assignment rule -- no frequencies needed at all.
Node* buildTreeFromLengths(const vector<CanonicalEntry>& entries) {
    auto sorted = entries;
    sort(sorted.begin(), sorted.end(), [](const CanonicalEntry& a, const CanonicalEntry& b) {
        if (a.length != b.length) return a.length < b.length;
        return a.symbol < b.symbol;
    });

    Node* root = new Node(0, 0, 0);
    long long code = 0;
    int prevLen = sorted.empty() ? 0 : sorted[0].length;

    int seq = 1;
    for (auto& e : sorted) {
        code <<= (e.length - prevLen);
        Node* curr = root;
        for (int i = e.length - 1; i >= 1; i--) {
            bool bit = (code >> i) & 1;
            Node*& next = bit ? curr->right : curr->left;
            if (!next) next = new Node(0, 0, seq++);
            curr = next;
        }
        bool lastBit = e.length >= 1 ? ((code >> 0) & 1) : 0;
        Node*& leaf = lastBit ? curr->right : curr->left;
        if (!leaf) leaf = new Node(e.symbol, 0, seq++);
        leaf->ch = e.symbol;
        code++;
        prevLen = e.length;
    }
    return root;
}

// ---------------------------------------------------------------------
// 4. Free the tree memory (avoid leaks)
// ---------------------------------------------------------------------
void freeTree(Node* root) {
    if (!root) return;
    freeTree(root->left);
    freeTree(root->right);
    delete root;
}

// ---------------------------------------------------------------------
// 4b. Stats: Shannon entropy vs. actual average Huffman code length
// ---------------------------------------------------------------------
// Entropy H = -sum(p_i * log2(p_i)) is the information-theoretic lower
// bound on average bits/symbol for ANY uniquely-decodable code. Huffman
// is optimal among codes restricted to integer-length codewords, so it
// typically lands within ~1 bit of H, exactly matching it only when
// every probability is a power of 1/2.
struct Stats {
    double entropyBitsPerByte;
    double avgCodeLenBitsPerByte;
    double efficiencyPercent; // entropy / avgLen * 100
};

Stats computeStats(const unordered_map<unsigned char, long long>& freqMap,
                    const unordered_map<unsigned char, string>& codes,
                    long long totalBytes) {
    double entropy = 0.0, avgLen = 0.0;
    for (auto& kv : freqMap) {
        double p = (double)kv.second / totalBytes;
        entropy += -p * log2(p);
        avgLen += p * codes.at(kv.first).size();
    }
    Stats s;
    s.entropyBitsPerByte = entropy;
    s.avgCodeLenBitsPerByte = avgLen;
    s.efficiencyPercent = (avgLen > 0) ? (entropy / avgLen) * 100.0 : 100.0;
    return s;
}

// Simple additive checksum (not cryptographic -- just enough to catch
// truncation/corruption and fail loudly instead of decoding garbage
// or walking off the end of the tree).
uint32_t simpleChecksum(const string& data) {
    uint32_t sum = 2166136261u; // FNV-ish mixing, good enough for a sanity check
    for (unsigned char c : data) {
        sum ^= c;
        sum *= 16777619u;
    }
    return sum;
}

// ---------------------------------------------------------------------
// 5. Compression
// ---------------------------------------------------------------------
const uint32_t MAGIC = 0x4855464Bu; // "HUFK"

void compress(const string& inputPath, const string& outputPath, bool canonical, bool quiet = false) {
    ifstream inFile(inputPath, ios::binary);
    if (!inFile) {
        cerr << "Error: cannot open input file " << inputPath << "\n";
        return;
    }
    stringstream buffer;
    buffer << inFile.rdbuf();
    string text = buffer.str();
    inFile.close();

    if (text.empty()) {
        cerr << "Error: input file is empty.\n";
        return;
    }

    // --- Step 1: frequency count ---
    unordered_map<unsigned char, long long> freqMap;
    for (unsigned char c : text) freqMap[c]++;

    // --- Step 2: build tree, Step 3: generate codes ---
    Node* root = buildHuffmanTree(freqMap);
    unordered_map<unsigned char, string> codes;
    generateCodes(root, "", codes);

    unordered_map<unsigned char, string> outputCodes = canonical
        ? buildCanonicalCodes(codes)
        : codes;

    // --- Step 4: build the encoded bit string ---
    string bitString;
    bitString.reserve(text.size() * 2);
    for (unsigned char c : text) bitString += outputCodes[c];

    // --- Step 5: write output file ---
    // File format:
    //   [uint32] magic "HUFK"
    //   [uint8]  flags (bit 0 = canonical mode)
    //   [uint32] checksum of original data
    //   [uint32] number of distinct byte values
    //   canonical mode: for each symbol -> [uint8 symbol][uint8 code length]
    //   classic mode:   for each symbol -> [uint8 symbol][uint32 frequency]
    //   [uint32] number of bits in the encoded stream
    //   [packed bytes] the actual compressed bits
    ofstream outFile(outputPath, ios::binary);
    if (!outFile) {
        cerr << "Error: cannot open output file " << outputPath << "\n";
        freeTree(root);
        return;
    }

    outFile.write(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC));
    uint8_t flags = canonical ? 1 : 0;
    outFile.write(reinterpret_cast<char*>(&flags), sizeof(flags));
    uint32_t checksum = simpleChecksum(text);
    outFile.write(reinterpret_cast<char*>(&checksum), sizeof(checksum));

    uint32_t numChars = freqMap.size();
    outFile.write(reinterpret_cast<char*>(&numChars), sizeof(numChars));

    if (canonical) {
        vector<CanonicalEntry> entries;
        for (auto& kv : outputCodes) entries.push_back({kv.first, (int)kv.second.size()});
        sort(entries.begin(), entries.end(), [](const CanonicalEntry& a, const CanonicalEntry& b) {
            return a.symbol < b.symbol;
        });
        for (auto& e : entries) {
            outFile.write(reinterpret_cast<char*>(&e.symbol), sizeof(unsigned char));
            uint8_t len = (uint8_t)e.length;
            outFile.write(reinterpret_cast<char*>(&len), sizeof(len));
        }
    } else {
        vector<pair<unsigned char, long long>> sortedFreqs(freqMap.begin(), freqMap.end());
        sort(sortedFreqs.begin(), sortedFreqs.end());
        for (auto& p : sortedFreqs) {
            unsigned char ch = p.first;
            outFile.write(reinterpret_cast<char*>(&ch), sizeof(ch));
            uint32_t f = (uint32_t)p.second;
            outFile.write(reinterpret_cast<char*>(&f), sizeof(f));
        }
    }

    uint32_t numBits = bitString.size();
    outFile.write(reinterpret_cast<char*>(&numBits), sizeof(numBits));

    unsigned char currentByte = 0;
    int bitCount = 0;
    for (char bit : bitString) {
        currentByte = (currentByte << 1) | (bit - '0');
        bitCount++;
        if (bitCount == 8) {
            outFile.write(reinterpret_cast<char*>(&currentByte), 1);
            currentByte = 0;
            bitCount = 0;
        }
    }
    if (bitCount > 0) {
        currentByte <<= (8 - bitCount);
        outFile.write(reinterpret_cast<char*>(&currentByte), 1);
    }
    streampos endPos = outFile.tellp();
    outFile.close();

    if (!quiet) {
        size_t actualCompressedSize = (size_t)endPos;
        double ratio = 100.0 * (1.0 - (double)actualCompressedSize / text.size());
        Stats s = computeStats(freqMap, outputCodes, text.size());

        cout << "Compression complete (" << (canonical ? "canonical" : "classic") << " mode).\n";
        cout << "Original size     : " << text.size() << " bytes\n";
        cout << "Compressed size   : " << actualCompressedSize << " bytes (file, including header)\n";
        cout << "Space saved       : " << fixed << setprecision(2) << ratio << "%\n";
        cout << "Distinct bytes    : " << freqMap.size() << "\n";
        cout << "Shannon entropy   : " << setprecision(4) << s.entropyBitsPerByte << " bits/byte (theoretical min)\n";
        cout << "Avg Huffman length: " << s.avgCodeLenBitsPerByte << " bits/byte (actual)\n";
        cout << "Coding efficiency : " << setprecision(2) << s.efficiencyPercent << "% of entropy bound\n";
    }

    freeTree(root);
}

// ---------------------------------------------------------------------
// 6. Decompression
// ---------------------------------------------------------------------
bool decompress(const string& inputPath, const string& outputPath, bool quiet = false) {
    ifstream inFile(inputPath, ios::binary);
    if (!inFile) {
        cerr << "Error: cannot open input file " << inputPath << "\n";
        return false;
    }

    uint32_t magic;
    inFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != MAGIC) {
        cerr << "Error: not a valid .huff file (bad magic number). Refusing to decode garbage.\n";
        return false;
    }

    uint8_t flags;
    inFile.read(reinterpret_cast<char*>(&flags), sizeof(flags));
    bool canonical = flags & 1;

    uint32_t storedChecksum;
    inFile.read(reinterpret_cast<char*>(&storedChecksum), sizeof(storedChecksum));

    uint32_t numChars;
    inFile.read(reinterpret_cast<char*>(&numChars), sizeof(numChars));

    Node* root = nullptr;
    unordered_map<unsigned char, long long> freqMap; // only populated in classic mode

    if (canonical) {
        vector<CanonicalEntry> entries;
        for (uint32_t i = 0; i < numChars; i++) {
            unsigned char sym; uint8_t len;
            inFile.read(reinterpret_cast<char*>(&sym), sizeof(sym));
            inFile.read(reinterpret_cast<char*>(&len), sizeof(len));
            entries.push_back({sym, len});
        }
        root = buildTreeFromLengths(entries);
    } else {
        for (uint32_t i = 0; i < numChars; i++) {
            unsigned char c; uint32_t f;
            inFile.read(reinterpret_cast<char*>(&c), sizeof(c));
            inFile.read(reinterpret_cast<char*>(&f), sizeof(f));
            freqMap[c] = f;
        }
        root = buildHuffmanTree(freqMap);
    }

    uint32_t numBits;
    inFile.read(reinterpret_cast<char*>(&numBits), sizeof(numBits));

    vector<unsigned char> bytes((istreambuf_iterator<char>(inFile)),
                                  istreambuf_iterator<char>());
    inFile.close();

    string result;
    result.reserve(numBits / 8 + 1);

    Node* curr = root;
    uint32_t bitsDecoded = 0;
    bool singleChar = root->left && !root->right && root->left->isLeaf();

    for (unsigned char byte : bytes) {
        for (int i = 7; i >= 0 && bitsDecoded < numBits; i--, bitsDecoded++) {
            int bit = (byte >> i) & 1;

            if (singleChar) {
                result += (char)root->left->ch;
                continue;
            }

            curr = (bit == 0) ? curr->left : curr->right;
            if (!curr) {
                cerr << "Error: corrupt stream -- walked off the tree.\n";
                freeTree(root);
                return false;
            }
            if (curr->isLeaf()) {
                result += (char)curr->ch;
                curr = root;
            }
        }
    }

    uint32_t actualChecksum = simpleChecksum(result);
    if (actualChecksum != storedChecksum) {
        cerr << "Warning: checksum mismatch -- decoded output may not match the original.\n";
    }

    ofstream outFile(outputPath, ios::binary);
    outFile << result;
    outFile.close();

    if (!quiet) {
        cout << "Decompression complete (" << (canonical ? "canonical" : "classic") << " mode). "
             << "Output written to " << outputPath << "\n";
        cout << "Checksum          : " << (actualChecksum == storedChecksum ? "OK" : "MISMATCH") << "\n";
    }

    freeTree(root);
    return true;
}

// ---------------------------------------------------------------------
// 7. Verify: round-trip self-test (compress -> decompress -> diff),
//    entirely via temp files, reporting pass/fail. Useful to demo
//    correctness live without juggling two terminal commands.
// ---------------------------------------------------------------------
void verify(const string& inputPath, bool canonical) {
    string tmpHuff = inputPath + ".verify.huff";
    string tmpOut = inputPath + ".verify.out";

    compress(inputPath, tmpHuff, canonical, /*quiet=*/true);
    bool ok = decompress(tmpHuff, tmpOut, /*quiet=*/true);

    ifstream a(inputPath, ios::binary), b(tmpOut, ios::binary);
    stringstream sa, sb;
    sa << a.rdbuf();
    sb << b.rdbuf();
    bool identical = ok && (sa.str() == sb.str());

    cout << "Round-trip verification: " << (identical ? "PASS \xE2\x9C\x93" : "FAIL \xE2\x9C\x97") << "\n";
    if (!identical) {
        cout << "  Original size : " << sa.str().size() << " bytes\n";
        cout << "  Decoded size  : " << sb.str().size() << " bytes\n";
    }

    remove(tmpHuff.c_str());
    remove(tmpOut.c_str());
}

// ---------------------------------------------------------------------
// 8. Stats-only mode: print entropy / code table without writing a file
// ---------------------------------------------------------------------
void printStats(const string& inputPath) {
    ifstream inFile(inputPath, ios::binary);
    if (!inFile) {
        cerr << "Error: cannot open input file " << inputPath << "\n";
        return;
    }
    stringstream buffer;
    buffer << inFile.rdbuf();
    string text = buffer.str();
    inFile.close();

    if (text.empty()) {
        cerr << "Error: input file is empty.\n";
        return;
    }

    unordered_map<unsigned char, long long> freqMap;
    for (unsigned char c : text) freqMap[c]++;

    Node* root = buildHuffmanTree(freqMap);
    unordered_map<unsigned char, string> codes;
    generateCodes(root, "", codes);

    Stats s = computeStats(freqMap, codes, text.size());

    cout << "File: " << inputPath << " (" << text.size() << " bytes, "
         << freqMap.size() << " distinct byte values)\n\n";
    cout << "Shannon entropy    : " << fixed << setprecision(4) << s.entropyBitsPerByte << " bits/byte\n";
    cout << "Avg Huffman length : " << s.avgCodeLenBitsPerByte << " bits/byte\n";
    cout << "Coding efficiency  : " << setprecision(2) << s.efficiencyPercent << "% of entropy bound\n\n";

    cout << "Code table (byte : freq : code, sorted by code length):\n";
    vector<pair<unsigned char, long long>> sortedFreqs(freqMap.begin(), freqMap.end());
    sort(sortedFreqs.begin(), sortedFreqs.end(),
         [&](const pair<unsigned char, long long>& a, const pair<unsigned char, long long>& b) {
             if (codes.at(a.first).size() != codes.at(b.first).size())
                 return codes.at(a.first).size() < codes.at(b.first).size();
             return a.first < b.first;
         });

    for (auto& p : sortedFreqs) {
        unsigned char c = p.first;
        string display;
        if (c == '\n') display = "\\n";
        else if (c == '\t') display = "\\t";
        else if (c == ' ') display = "' '";
        else if (c >= 32 && c < 127) display = string(1, (char)c);
        else { char buf[8]; snprintf(buf, sizeof(buf), "0x%02X", c); display = buf; }

        cout << "  '" << display << "'\t freq=" << p.second
             << "\t code=" << codes.at(c) << " (" << codes.at(c).size() << " bits)\n";
    }

    freeTree(root);
}

// ---------------------------------------------------------------------
// main: simple CLI
// ---------------------------------------------------------------------
void printUsage(const char* prog) {
    cout << "Usage:\n";
    cout << "  " << prog << " compress   <input> <output.huff> [--canonical]\n";
    cout << "  " << prog << " decompress <input.huff> <output>\n";
    cout << "  " << prog << " verify     <input> [--canonical]   (round-trip self-test)\n";
    cout << "  " << prog << " stats      <input>                  (entropy / code table)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    string mode = argv[1];
    bool canonical = false;
    for (int i = 2; i < argc; i++) {
        if (string(argv[i]) == "--canonical") canonical = true;
    }

    if (mode == "compress") {
        if (argc < 4) { printUsage(argv[0]); return 1; }
        compress(argv[2], argv[3], canonical);
    } else if (mode == "decompress") {
        if (argc < 4) { printUsage(argv[0]); return 1; }
        if (!decompress(argv[2], argv[3])) return 1;
    } else if (mode == "verify") {
        verify(argv[2], canonical);
    } else if (mode == "stats") {
        printStats(argv[2]);
    } else {
        cerr << "Unknown mode: " << mode << "\n";
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
