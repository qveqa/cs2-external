// ttfdump.cpp - tiny TrueType inspector: prints the name table entries, glyph count
// and the mapped unicode codepoints, so an icon font can be wired up without guessing.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint16_t U16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

uint32_t U32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

const uint8_t* Table(const std::vector<uint8_t>& data, const char* tag, uint32_t& length) {
    const uint16_t num_tables = U16(data, 4);
    for (int i = 0; i < num_tables; ++i) {
        const size_t record = 12 + static_cast<size_t>(i) * 16;
        if (std::memcmp(data.data() + record, tag, 4) == 0) {
            const uint32_t offset = U32(data, record + 8);
            length = U32(data, record + 12);
            return offset < data.size() ? data.data() + offset : nullptr;
        }
    }
    length = 0;
    return nullptr;
}

std::string DecodeName(const uint8_t* table, size_t length) {
    const uint16_t count = U16({table, table + length}, 2);
    const uint16_t string_offset = U16({table, table + length}, 4);
    (void)count;

    std::string result;
    const uint16_t records = U16({table, table + length}, 2);
    for (int i = 0; i < records; ++i) {
        const size_t record = 6 + static_cast<size_t>(i) * 12;
        if (record + 12 > length) break;
        const uint16_t platform = U16({table, table + length}, record);
        const uint16_t name_id = U16({table, table + length}, record + 6);
        const uint16_t str_len = U16({table, table + length}, record + 8);
        const uint16_t str_off = U16({table, table + length}, record + 10);
        if (name_id != 1 && name_id != 4 && name_id != 5 && name_id != 6) continue;

        const size_t at = string_offset + str_off;
        if (at + str_len > length) continue;

        std::string text;
        if (platform == 3) {   // utf-16be
            for (size_t b = at; b + 1 < at + str_len; b += 2) {
                const uint16_t ch = static_cast<uint16_t>((table[b] << 8) | table[b + 1]);
                if (ch < 0x80) text.push_back(static_cast<char>(ch));
            }
        } else {
            text.assign(reinterpret_cast<const char*>(table + at), str_len);
        }

        const char* label = name_id == 1 ? "family" : (name_id == 4 ? "full" : (name_id == 5 ? "version" : "postscript"));
        result += std::string(label) + "=\"" + text + "\" ";
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: ttfdump <font.ttf>\n");
        return 1;
    }

    FILE* file = std::fopen(argv[1], "rb");
    if (!file) {
        std::printf("cannot open %s\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> data;
    std::fseek(file, 0, SEEK_END);
    data.resize(static_cast<size_t>(std::ftell(file)));
    std::fseek(file, 0, SEEK_SET);
    if (std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        std::printf("read failed\n");
        return 1;
    }
    std::fclose(file);

    if (data.size() < 12) {
        std::printf("not a font\n");
        return 1;
    }

    uint32_t length = 0;
    if (const uint8_t* name = Table(data, "name", length))
        std::printf("name: %s\n", DecodeName(name, length).c_str());

    if (const uint8_t* head = Table(data, "head", length))
        std::printf("unitsPerEm: %u\n", U16(data, static_cast<size_t>(head - data.data()) + 18));
    if (const uint8_t* maxp = Table(data, "maxp", length))
        std::printf("glyphs: %u\n", U16(data, static_cast<size_t>(maxp - data.data()) + 4));

    const uint8_t* cmap = Table(data, "cmap", length);
    if (!cmap) {
        std::printf("no cmap\n");
        return 0;
    }
    const size_t cmap_at = static_cast<size_t>(cmap - data.data());
    const uint16_t subtables = U16(data, cmap_at + 2);

    for (int i = 0; i < subtables; ++i) {
        const size_t record = cmap_at + 4 + static_cast<size_t>(i) * 8;
        const uint16_t platform = U16(data, record);
        const uint16_t encoding = U16(data, record + 2);
        const uint32_t sub_offset = U32(data, record + 4);
        const size_t sub = cmap_at + sub_offset;
        const uint16_t format = U16(data, sub);
        std::printf("cmap[%d] plat=%u enc=%u format=%u\n", i, platform, encoding, format);
        if (format != 4) continue;

        const uint16_t seg_count = U16(data, sub + 6) / 2;
        const size_t end_codes = sub + 14;
        const size_t start_codes = end_codes + seg_count * 2 + 2;
        const size_t deltas = start_codes + seg_count * 2;
        const size_t range_offsets = deltas + seg_count * 2;

        std::vector<uint32_t> mapped;
        for (int seg = 0; seg < seg_count; ++seg) {
            const uint16_t end = U16(data, end_codes + seg * 2);
            const uint16_t start = U16(data, start_codes + seg * 2);
            const uint16_t delta = U16(data, deltas + seg * 2);
            const uint16_t range_offset = U16(data, range_offsets + seg * 2);
            for (uint32_t code = start; code <= end && code != 0xFFFF; ++code) {
                uint16_t glyph = static_cast<uint16_t>(code + delta);
                if (range_offset != 0) {
                    const size_t glyph_at = range_offsets + seg * 2 + range_offset + (code - start) * 2;
                    glyph = U16(data, glyph_at);
                    if (glyph) glyph = static_cast<uint16_t>(glyph + delta);
                }
                if (glyph) mapped.push_back(code);
            }
        }

        // compress into ranges
        std::string ranges;
        size_t start = 0;
        for (size_t n = 1; n <= mapped.size(); ++n) {
            const bool break_here = (n == mapped.size()) || (mapped[n] != mapped[n - 1] + 1) || (n - start >= 8);
            if (!break_here) continue;
            char buffer[64];
            if (n - start == 1)
                std::snprintf(buffer, sizeof(buffer), "U+%04X ", mapped[start]);
            else
                std::snprintf(buffer, sizeof(buffer), "U+%04X-U+%04X ", mapped[start], mapped[n - 1]);
            ranges += buffer;
            start = n;
        }
        std::printf("  mapped (%zu): %s\n", mapped.size(), ranges.c_str());
    }
    return 0;
}
