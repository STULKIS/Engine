// Frontier Engine - PNG/TGA codecs and the small inflate they need.
#include "asset/Image.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace frontier::asset {

namespace {

// ------------------------------------------------------------------ bit input

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    // Reads `count` bits (count <= 24) least-significant bit first.
    bool Read(int count, uint32_t& out) {
        while (m_bits < count) {
            if (m_pos >= m_size) {
                return false;
            }
            m_buffer |= static_cast<uint32_t>(m_data[m_pos++]) << m_bits;
            m_bits += 8;
        }
        out = m_buffer & ((1u << count) - 1u);
        m_buffer >>= count;
        m_bits -= count;
        return true;
    }

    void AlignToByte() {
        m_buffer = 0;
        m_bits = 0;
    }

    bool ReadByte(uint8_t& out) {
        if (m_bits >= 8) {
            out = static_cast<uint8_t>(m_buffer & 0xFFu);
            m_buffer >>= 8;
            m_bits -= 8;
            return true;
        }
        if (m_pos >= m_size) {
            return false;
        }
        out = m_data[m_pos++];
        return true;
    }

    size_t Position() const { return m_pos; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
    uint32_t m_buffer = 0;
    int m_bits = 0;
};

// -------------------------------------------------------------- huffman codes

class Huffman {
public:
    bool Build(const uint8_t* lengths, int count) {
        m_counts.assign(16, 0);
        for (int i = 0; i < count; ++i) {
            m_counts[lengths[i]]++;
        }
        if (m_counts[0] == count) {
            m_empty = true; // no codes at all; any decode attempt fails
            m_symbols.clear();
            return true;
        }
        m_counts[0] = 0;

        std::vector<int> offsets(16, 0);
        for (int length = 1; length < 16; ++length) {
            offsets[length] = offsets[length - 1] + m_counts[length - 1];
        }
        m_symbols.assign(static_cast<size_t>(count), 0);
        for (int i = 0; i < count; ++i) {
            if (lengths[i] != 0) {
                m_symbols[static_cast<size_t>(offsets[lengths[i]]++)] = static_cast<uint16_t>(i);
            }
        }
        return true;
    }

    bool Decode(BitReader& reader, int& symbol) const {
        if (m_empty) {
            return false;
        }
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= 15; ++length) {
            uint32_t bit = 0;
            if (!reader.Read(1, bit)) {
                return false;
            }
            code |= static_cast<int>(bit);
            const int count = m_counts[static_cast<size_t>(length)];
            if (code - first < count) {
                const size_t at = static_cast<size_t>(index + (code - first));
                if (at >= m_symbols.size()) {
                    return false;
                }
                symbol = m_symbols[at];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return false;
    }

private:
    std::vector<uint16_t> m_counts;
    std::vector<uint16_t> m_symbols;
    bool m_empty = false;
};

const int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19,  23, 27,
                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistanceBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
                               33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
                               1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const int kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool InflateBlockData(BitReader& reader, const Huffman& literals, const Huffman& distances,
                      std::vector<uint8_t>& out, std::string& error) {
    for (;;) {
        int symbol = 0;
        if (!literals.Decode(reader, symbol)) {
            error = "deflate: bad literal code";
            return false;
        }
        if (symbol < 256) {
            out.push_back(static_cast<uint8_t>(symbol));
            continue;
        }
        if (symbol == 256) {
            return true;
        }
        symbol -= 257;
        if (symbol >= 29) {
            error = "deflate: invalid length symbol";
            return false;
        }
        uint32_t extra = 0;
        if (kLengthExtra[symbol] > 0 && !reader.Read(kLengthExtra[symbol], extra)) {
            error = "deflate: truncated length";
            return false;
        }
        const size_t length = static_cast<size_t>(kLengthBase[symbol]) + extra;

        int distanceSymbol = 0;
        if (!distances.Decode(reader, distanceSymbol) || distanceSymbol < 0 || distanceSymbol >= 30) {
            error = "deflate: bad distance code";
            return false;
        }
        extra = 0;
        if (kDistanceExtra[distanceSymbol] > 0 && !reader.Read(kDistanceExtra[distanceSymbol], extra)) {
            error = "deflate: truncated distance";
            return false;
        }
        const size_t distance = static_cast<size_t>(kDistanceBase[distanceSymbol]) + extra;
        if (distance == 0 || distance > out.size()) {
            error = "deflate: distance beyond the output buffer";
            return false;
        }
        const size_t start = out.size() - distance;
        for (size_t i = 0; i < length; ++i) {
            out.push_back(out[start + i]);
        }
        if (out.size() > (64u << 20)) {
            error = "deflate: output larger than 64 MB";
            return false;
        }
    }
}

bool BuildFixedTables(Huffman& literals, Huffman& distances) {
    uint8_t literalLengths[288];
    for (int i = 0; i < 144; ++i) literalLengths[i] = 8;
    for (int i = 144; i < 256; ++i) literalLengths[i] = 9;
    for (int i = 256; i < 280; ++i) literalLengths[i] = 7;
    for (int i = 280; i < 288; ++i) literalLengths[i] = 8;
    uint8_t distanceLengths[32];
    std::memset(distanceLengths, 5, sizeof(distanceLengths));
    return literals.Build(literalLengths, 288) && distances.Build(distanceLengths, 32);
}

bool BuildDynamicTables(BitReader& reader, Huffman& literals, Huffman& distances,
                        std::string& error) {
    static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint32_t value = 0;
    if (!reader.Read(5, value)) {
        error = "deflate: truncated block header";
        return false;
    }
    const int literalCount = static_cast<int>(value) + 257;
    if (!reader.Read(5, value)) {
        error = "deflate: truncated block header";
        return false;
    }
    const int distanceCount = static_cast<int>(value) + 1;
    if (!reader.Read(4, value)) {
        error = "deflate: truncated block header";
        return false;
    }
    const int codeLengthCount = static_cast<int>(value) + 4;
    if (literalCount > 286 || distanceCount > 30) {
        error = "deflate: block declares too many codes";
        return false;
    }

    uint8_t codeLengths[19] = {0};
    for (int i = 0; i < codeLengthCount; ++i) {
        if (!reader.Read(3, value)) {
            error = "deflate: truncated code-length table";
            return false;
        }
        codeLengths[kOrder[i]] = static_cast<uint8_t>(value);
    }
    Huffman codeLengthCode;
    if (!codeLengthCode.Build(codeLengths, 19)) {
        error = "deflate: invalid code-length table";
        return false;
    }

    std::vector<uint8_t> lengths(static_cast<size_t>(literalCount + distanceCount), 0);
    size_t index = 0;
    while (index < lengths.size()) {
        int symbol = 0;
        if (!codeLengthCode.Decode(reader, symbol)) {
            error = "deflate: bad code-length symbol";
            return false;
        }
        if (symbol < 16) {
            lengths[index++] = static_cast<uint8_t>(symbol);
        } else if (symbol == 16) {
            if (index == 0) {
                error = "deflate: repeat with no previous length";
                return false;
            }
            if (!reader.Read(2, value)) {
                error = "deflate: truncated repeat";
                return false;
            }
            const uint8_t previous = lengths[index - 1];
            for (int i = 0; i < 3 + static_cast<int>(value) && index < lengths.size(); ++i) {
                lengths[index++] = previous;
            }
        } else if (symbol == 17 || symbol == 18) {
            const int extra = symbol == 17 ? 3 : 11;
            if (!reader.Read(symbol == 17 ? 3 : 7, value)) {
                error = "deflate: truncated zero run";
                return false;
            }
            for (int i = 0; i < extra + static_cast<int>(value) && index < lengths.size(); ++i) {
                lengths[index++] = 0;
            }
        } else {
            error = "deflate: invalid code-length symbol";
            return false;
        }
    }

    if (!literals.Build(lengths.data(), literalCount)) {
        error = "deflate: invalid literal table";
        return false;
    }
    if (!distances.Build(lengths.data() + literalCount, distanceCount)) {
        error = "deflate: invalid distance table";
        return false;
    }
    return true;
}

// ------------------------------------------------------------------- checksums

uint32_t Adler32(const uint8_t* data, size_t size) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint32_t ReadLE16(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}

void AppendBE32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendChunk(std::vector<uint8_t>& out, const char type[5], const std::vector<uint8_t>& data) {
    AppendBE32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = Crc32(out.data() + start, out.size() - start);
    AppendBE32(out, crc);
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open '" + path + "'";
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        error = "'" + path + "' is empty";
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(file);
}

// Filtering, shared by the PNG decoder.
uint8_t PaethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return static_cast<uint8_t>(a);
    }
    if (pb <= pc) {
        return static_cast<uint8_t>(b);
    }
    return static_cast<uint8_t>(c);
}

} // namespace

// ---------------------------------------------------------------- format help

ImageFormat ImageFormatFromPath(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return ImageFormat::Unknown;
    }
    std::string extension = path.substr(dot + 1);
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (extension == "png") return ImageFormat::Png;
    if (extension == "tga") return ImageFormat::Tga;
    return ImageFormat::Unknown;
}

const char* ImageFormatName(ImageFormat format) {
    switch (format) {
        case ImageFormat::Png: return "PNG";
        case ImageFormat::Tga: return "TGA";
        default: return "unknown";
    }
}

// --------------------------------------------------------------------- inflate

bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, std::string& error) {
    if (size < 6) {
        error = "zlib: stream too short";
        return false;
    }
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8) {
        error = "zlib: only deflate is supported";
        return false;
    }
    if (((static_cast<uint32_t>(cmf) << 8) | flg) % 31u != 0u) {
        error = "zlib: bad header checksum";
        return false;
    }
    if (flg & 0x20u) {
        error = "zlib: preset dictionaries are not supported";
        return false;
    }

    BitReader reader(data + 2, size - 2);
    out.clear();
    out.reserve(size * 4);

    bool finalBlock = false;
    while (!finalBlock) {
        uint32_t value = 0;
        if (!reader.Read(1, value)) {
            error = "deflate: truncated block header";
            return false;
        }
        finalBlock = value == 1;
        if (!reader.Read(2, value)) {
            error = "deflate: truncated block header";
            return false;
        }
        if (value == 0) {
            reader.AlignToByte();
            uint8_t lengthLow = 0, lengthHigh = 0, checkLow = 0, checkHigh = 0;
            if (!reader.ReadByte(lengthLow) || !reader.ReadByte(lengthHigh) ||
                !reader.ReadByte(checkLow) || !reader.ReadByte(checkHigh)) {
                error = "deflate: truncated stored block";
                return false;
            }
            const int length = static_cast<int>(lengthLow) | (static_cast<int>(lengthHigh) << 8);
            const int check = static_cast<int>(checkLow) | (static_cast<int>(checkHigh) << 8);
            if ((length ^ 0xFFFF) != check) {
                error = "deflate: stored block length mismatch";
                return false;
            }
            for (int i = 0; i < length; ++i) {
                uint8_t byte = 0;
                if (!reader.ReadByte(byte)) {
                    error = "deflate: truncated stored block";
                    return false;
                }
                out.push_back(byte);
            }
        } else if (value == 1) {
            Huffman literals, distances;
            if (!BuildFixedTables(literals, distances)) {
                error = "deflate: could not build the fixed tables";
                return false;
            }
            if (!InflateBlockData(reader, literals, distances, out, error)) {
                return false;
            }
        } else if (value == 2) {
            Huffman literals, distances;
            if (!BuildDynamicTables(reader, literals, distances, error)) {
                return false;
            }
            if (!InflateBlockData(reader, literals, distances, out, error)) {
                return false;
            }
        } else {
            error = "deflate: reserved block type";
            return false;
        }
    }
    return true;
}

void ZlibStore(const std::vector<uint8_t>& input, std::vector<uint8_t>& out) {
    out.clear();
    out.push_back(0x78); // deflate, 32k window
    out.push_back(0x01); // no dictionary, fastest level; (0x78<<8|0x01) % 31 == 0

    size_t offset = 0;
    do {
        const size_t chunk = std::min<size_t>(65535, input.size() - offset);
        const bool final = offset + chunk >= input.size();
        out.push_back(final ? 1 : 0);
        out.push_back(static_cast<uint8_t>(chunk & 0xFF));
        out.push_back(static_cast<uint8_t>((chunk >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((~chunk) & 0xFF));
        out.push_back(static_cast<uint8_t>(((~chunk) >> 8) & 0xFF));
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                   input.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    } while (offset < input.size());

    AppendBE32(out, Adler32(input.data(), input.size()));
}

// ----------------------------------------------------------------- PNG decode

bool DecodePng(const uint8_t* data, size_t size, Image& out, std::string& error) {
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || std::memcmp(data, kSignature, 8) != 0) {
        error = "not a PNG file";
        return false;
    }

    int width = 0;
    int height = 0;
    int bitDepth = 0;
    int colorType = 0;
    int interlace = 0;
    std::vector<uint8_t> compressed;

    size_t offset = 8;
    bool sawHeader = false;
    bool sawEnd = false;
    while (offset + 8 <= size) {
        const uint32_t length = ReadBE32(data + offset);
        if (offset + 12 + length > size) {
            error = "PNG: truncated chunk";
            return false;
        }
        const char type[5] = {static_cast<char>(data[offset + 4]), static_cast<char>(data[offset + 5]),
                              static_cast<char>(data[offset + 6]), static_cast<char>(data[offset + 7]), 0};
        const uint8_t* chunk = data + offset + 8;
        const std::string name(type);
        if (name == "IHDR") {
            if (length < 13) {
                error = "PNG: short header";
                return false;
            }
            width = static_cast<int>(ReadBE32(chunk));
            height = static_cast<int>(ReadBE32(chunk + 4));
            bitDepth = chunk[8];
            colorType = chunk[9];
            interlace = chunk[12];
            sawHeader = true;
        } else if (name == "IDAT") {
            compressed.insert(compressed.end(), chunk, chunk + length);
        } else if (name == "IEND") {
            sawEnd = true;
            break;
        }
        offset += 12 + length;
    }

    if (!sawHeader || !sawEnd) {
        error = "PNG: missing header or end chunk";
        return false;
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        error = "PNG: unsupported dimensions";
        return false;
    }
    if (bitDepth != 8) {
        error = "PNG: only 8 bits per channel is supported";
        return false;
    }
    if (interlace != 0) {
        error = "PNG: Adam7 interlacing is not supported";
        return false;
    }

    int channels = 0;
    switch (colorType) {
        case 0: channels = 1; break; // greyscale
        case 2: channels = 3; break; // RGB
        case 4: channels = 2; break; // grey + alpha
        case 6: channels = 4; break; // RGBA
        case 3:
            error = "PNG: palette images are not supported (re-export as RGB/RGBA)";
            return false;
        default:
            error = "PNG: unknown colour type";
            return false;
    }

    if (compressed.empty()) {
        error = "PNG: no image data";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!ZlibInflate(compressed.data(), compressed.size(), raw, error)) {
        error = "PNG: " + error;
        return false;
    }

    const size_t stride = static_cast<size_t>(width) * static_cast<size_t>(channels);
    const size_t expected = (stride + 1) * static_cast<size_t>(height);
    if (raw.size() < expected) {
        error = "PNG: image data is shorter than the header claims";
        return false;
    }

    // Undo the per-scanline filters in place, writing RGBA as we go.
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255);

    std::vector<uint8_t> previous(stride, 0);
    std::vector<uint8_t> current(stride, 0);
    for (int y = 0; y < height; ++y) {
        const uint8_t* line = raw.data() + static_cast<size_t>(y) * (stride + 1);
        const uint8_t filter = line[0];
        const uint8_t* source = line + 1;
        for (size_t x = 0; x < stride; ++x) {
            const int a = x >= static_cast<size_t>(channels) ? current[x - channels] : 0;
            const int b = previous[x];
            const int c = x >= static_cast<size_t>(channels) ? previous[x - channels] : 0;
            int value = source[x];
            switch (filter) {
                case 0: break;
                case 1: value += a; break;
                case 2: value += b; break;
                case 3: value += (a + b) / 2; break;
                case 4: value += PaethPredictor(a, b, c); break;
                default:
                    error = "PNG: unknown scanline filter";
                    return false;
            }
            current[x] = static_cast<uint8_t>(value & 0xFF);
        }

        uint8_t* destination = out.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
        for (int x = 0; x < width; ++x) {
            const uint8_t* pixel = current.data() + static_cast<size_t>(x) * static_cast<size_t>(channels);
            uint8_t* target = destination + static_cast<size_t>(x) * 4u;
            switch (channels) {
                case 1:
                    target[0] = target[1] = target[2] = pixel[0];
                    target[3] = 255;
                    break;
                case 2:
                    target[0] = target[1] = target[2] = pixel[0];
                    target[3] = pixel[1];
                    break;
                case 3:
                    target[0] = pixel[0];
                    target[1] = pixel[1];
                    target[2] = pixel[2];
                    target[3] = 255;
                    break;
                default:
                    target[0] = pixel[0];
                    target[1] = pixel[1];
                    target[2] = pixel[2];
                    target[3] = pixel[3];
                    break;
            }
        }
        std::swap(previous, current);
        std::fill(current.begin(), current.end(), 0);
    }
    return true;
}

// ----------------------------------------------------------------- PNG encode

bool SavePng(const Image& image, const std::string& path, std::string& error) {
    if (image.Empty()) {
        error = "cannot save an empty image";
        return false;
    }

    // Filter 0 scanlines, then a stored zlib stream.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u +
                static_cast<size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const uint8_t* row = image.pixels.data() +
                             static_cast<size_t>(y) * static_cast<size_t>(image.width) * 4u;
        raw.insert(raw.end(), row, row + static_cast<size_t>(image.width) * 4u);
    }
    std::vector<uint8_t> compressed;
    ZlibStore(raw, compressed);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    AppendBE32(header, static_cast<uint32_t>(image.width));
    AppendBE32(header, static_cast<uint32_t>(image.height));
    header.push_back(8); // bit depth
    header.push_back(6); // RGBA
    header.push_back(0); // deflate
    header.push_back(0); // adaptive filtering
    header.push_back(0); // no interlace
    AppendChunk(png, "IHDR", header);
    AppendChunk(png, "IDAT", compressed);
    AppendChunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "could not write '" + path + "'";
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        error = "failed while writing '" + path + "'";
        return false;
    }
    return true;
}

// ----------------------------------------------------------------- TGA decode

bool DecodeTga(const uint8_t* data, size_t size, Image& out, std::string& error) {
    if (size < 18) {
        error = "TGA: file too short";
        return false;
    }
    const int idLength = data[0];
    const int colorMapType = data[1];
    const int imageType = data[2];
    const int width = static_cast<int>(ReadLE16(data + 12));
    const int height = static_cast<int>(ReadLE16(data + 14));
    const int bitsPerPixel = data[16];
    const int descriptor = data[17];

    if (colorMapType != 0) {
        error = "TGA: colour-mapped files are not supported";
        return false;
    }
    const bool rle = imageType == 10 || imageType == 11;
    if (imageType != 2 && imageType != 3 && imageType != 10 && imageType != 11) {
        error = "TGA: unsupported image type";
        return false;
    }
    if (width <= 0 || height <= 0) {
        error = "TGA: invalid dimensions";
        return false;
    }
    const bool greyscale = imageType == 3 || imageType == 11;
    const int bytesPerPixel = bitsPerPixel / 8;
    if ((greyscale && bytesPerPixel != 1) || (!greyscale && bytesPerPixel != 3 && bytesPerPixel != 4)) {
        error = "TGA: unsupported pixel depth";
        return false;
    }

    size_t offset = 18 + static_cast<size_t>(idLength);
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> raw(pixelCount * static_cast<size_t>(bytesPerPixel));
    if (!rle) {
        if (offset + raw.size() > size) {
            error = "TGA: pixel data truncated";
            return false;
        }
        std::memcpy(raw.data(), data + offset, raw.size());
    } else {
        size_t written = 0;
        while (written < pixelCount) {
            if (offset >= size) {
                error = "TGA: RLE data truncated";
                return false;
            }
            const uint8_t packet = data[offset++];
            const int count = (packet & 0x7F) + 1;
            if ((packet & 0x80) != 0) {
                if (offset + static_cast<size_t>(bytesPerPixel) > size) {
                    error = "TGA: RLE packet truncated";
                    return false;
                }
                const uint8_t* pixel = data + offset;
                offset += static_cast<size_t>(bytesPerPixel);
                for (int i = 0; i < count && written < pixelCount; ++i) {
                    std::memcpy(raw.data() + written * static_cast<size_t>(bytesPerPixel), pixel,
                                static_cast<size_t>(bytesPerPixel));
                    ++written;
                }
            } else {
                const size_t bytes = static_cast<size_t>(count) * static_cast<size_t>(bytesPerPixel);
                if (offset + bytes > size) {
                    error = "TGA: RLE run truncated";
                    return false;
                }
                const size_t remaining = (pixelCount - written) * static_cast<size_t>(bytesPerPixel);
                std::memcpy(raw.data() + written * static_cast<size_t>(bytesPerPixel), data + offset,
                            std::min(bytes, remaining));
                offset += bytes;
                written += static_cast<size_t>(count);
            }
        }
    }

    out.width = width;
    out.height = height;
    out.pixels.assign(pixelCount * 4u, 255);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* source = raw.data() + i * static_cast<size_t>(bytesPerPixel);
        uint8_t* target = out.pixels.data() + i * 4u;
        if (greyscale) {
            target[0] = target[1] = target[2] = source[0];
            target[3] = 255;
        } else {
            target[0] = source[2];
            target[1] = source[1];
            target[2] = source[0];
            target[3] = bytesPerPixel == 4 ? source[3] : 255;
        }
    }

    // Bit 5 of the descriptor means the rows are stored top-to-bottom.
    if ((descriptor & 0x20) == 0) {
        for (int y = 0; y < height / 2; ++y) {
            uint8_t* a = out.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
            uint8_t* b = out.pixels.data() + static_cast<size_t>(height - 1 - y) *
                                                 static_cast<size_t>(width) * 4u;
            for (int x = 0; x < width * 4; ++x) {
                std::swap(a[x], b[x]);
            }
        }
    }
    return true;
}

bool SaveTga(const Image& image, const std::string& path, std::string& error) {
    if (image.Empty()) {
        error = "cannot save an empty image";
        return false;
    }
    std::vector<uint8_t> header(18, 0);
    header[2] = 2; // uncompressed true colour
    header[12] = static_cast<uint8_t>(image.width & 0xFF);
    header[13] = static_cast<uint8_t>((image.width >> 8) & 0xFF);
    header[14] = static_cast<uint8_t>(image.height & 0xFF);
    header[15] = static_cast<uint8_t>((image.height >> 8) & 0xFF);
    header[16] = 32;
    header[17] = 0x28; // 8 alpha bits, top-left origin

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "could not write '" + path + "'";
        return false;
    }
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        const uint8_t bgra[4] = {image.pixels[i + 2], image.pixels[i + 1], image.pixels[i + 0],
                                 image.pixels[i + 3]};
        file.write(reinterpret_cast<const char*>(bgra), 4);
    }
    return static_cast<bool>(file);
}

bool LoadImage(const std::string& path, Image& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, error)) {
        return false;
    }
    switch (ImageFormatFromPath(path)) {
        case ImageFormat::Png:
            return DecodePng(bytes.data(), bytes.size(), out, error);
        case ImageFormat::Tga:
            return DecodeTga(bytes.data(), bytes.size(), out, error);
        default:
            error = "unsupported image type for '" + path + "' (PNG and TGA are supported)";
            return false;
    }
}

} // namespace frontier::asset
