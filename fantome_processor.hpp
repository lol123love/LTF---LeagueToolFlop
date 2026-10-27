#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <miniz/miniz.h>
#include <zstd.h>

namespace fantome_tool {

    namespace fs = std::filesystem;

    // =========================================================================
    // 1. HASHING (FNV1a32 & XXH64)
    // =========================================================================
    struct FNV1a {
        uint32_t hash_ = 0;

        static constexpr uint32_t compute(std::string_view str) noexcept {
            uint32_t h = 0x811c9dc5u;
            for (char ch : str) {
                uint8_t c = static_cast<uint8_t>(ch);
                c = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
                h = ((h ^ c) * 0x01000193u) & 0xFFFFFFFFu;
            }
            return h;
        }

        constexpr FNV1a() noexcept = default;
        constexpr FNV1a(uint32_t h) noexcept : hash_(h) {}
        constexpr FNV1a(std::string_view str) noexcept : hash_(compute(str)) {}
        constexpr uint32_t hash() const noexcept { return hash_; }
        constexpr bool operator==(FNV1a const& o) const noexcept { return hash_ == o.hash_; }
    };

    struct XXH64 {
        uint64_t hash_ = 0;

        static constexpr uint64_t compute(std::string_view str, uint64_t seed = 0) noexcept {
            auto data = str.data();
            auto const size = str.size();
            auto const end = data + size;
            constexpr uint64_t Prime1 = 11400714785074694791ULL;
            constexpr uint64_t Prime2 = 14029467366897019727ULL;
            constexpr uint64_t Prime3 = 1609587929392839161ULL;
            constexpr uint64_t Prime4 = 9650029242287828579ULL;
            constexpr uint64_t Prime5 = 2870177450012600261ULL;

            auto Char = [](char c) constexpr -> uint64_t {
                return static_cast<uint8_t>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
                };
            auto HalfBlock = [Char](char const* p) constexpr -> uint64_t {
                return Char(*p) | (Char(*(p + 1)) << 8) | (Char(*(p + 2)) << 16) | (Char(*(p + 3)) << 24);
                };
            auto Block = [Char](char const* p) constexpr -> uint64_t {
                return Char(*p) | (Char(*(p + 1)) << 8) | (Char(*(p + 2)) << 16) | (Char(*(p + 3)) << 24) |
                    (Char(*(p + 4)) << 32) | (Char(*(p + 5)) << 40) | (Char(*(p + 6)) << 48) | (Char(*(p + 7)) << 56);
                };
            auto ROL = [](uint64_t val, int amt) constexpr -> uint64_t { return std::rotl(val, amt); };

            uint64_t result = 0;
            if (size >= 32u) {
                uint64_t s1 = seed + Prime1 + Prime2;
                uint64_t s2 = seed + Prime2;
                uint64_t s3 = seed;
                uint64_t s4 = seed - Prime1;
                for (; data + 32 <= end; data += 32) {
                    s1 = ROL(s1 + Block(data) * Prime2, 31) * Prime1;
                    s2 = ROL(s2 + Block(data + 8) * Prime2, 31) * Prime1;
                    s3 = ROL(s3 + Block(data + 16) * Prime2, 31) * Prime1;
                    s4 = ROL(s4 + Block(data + 24) * Prime2, 31) * Prime1;
                }
                result = ROL(s1, 1) + ROL(s2, 7) + ROL(s3, 12) + ROL(s4, 18);
                result ^= ROL(s1 * Prime2, 31) * Prime1;
                result = result * Prime1 + Prime4;
                result ^= ROL(s2 * Prime2, 31) * Prime1;
                result = result * Prime1 + Prime4;
                result ^= ROL(s3 * Prime2, 31) * Prime1;
                result = result * Prime1 + Prime4;
                result ^= ROL(s4 * Prime2, 31) * Prime1;
                result = result * Prime1 + Prime4;
            }
            else {
                result = seed + Prime5;
            }
            result += static_cast<uint64_t>(size);
            for (; data + 8 <= end; data += 8) {
                result ^= ROL(Block(data) * Prime2, 31) * Prime1;
                result = ROL(result, 27) * Prime1 + Prime4;
            }
            for (; data + 4 <= end; data += 4) {
                result ^= HalfBlock(data) * Prime1;
                result = ROL(result, 23) * Prime2 + Prime3;
            }
            for (; data != end; ++data) {
                result ^= Char(*data) * Prime5;
                result = ROL(result, 11) * Prime1;
            }
            result ^= result >> 33;
            result *= Prime2;
            result ^= result >> 29;
            result *= Prime3;
            result ^= result >> 32;
            return result;
        }

        constexpr XXH64() noexcept = default;
        constexpr XXH64(uint64_t h) noexcept : hash_(h) {}
        constexpr XXH64(std::string_view str) noexcept : hash_(compute(str)) {}
        constexpr uint64_t hash() const noexcept { return hash_; }
        constexpr bool operator<(XXH64 const& o) const noexcept { return hash_ < o.hash_; }
        constexpr bool operator==(XXH64 const& o) const noexcept { return hash_ == o.hash_; }
    };

    // =========================================================================
    // 2. EMBEDDED RITOBIN AST & BINARY IO
    // =========================================================================
    namespace bin {
        template <typename K, typename V>
        struct flatmap {
            std::vector<std::pair<K, V>> values_;
            auto begin() { return values_.begin(); }
            auto end() { return values_.end(); }
            auto begin() const { return values_.begin(); }
            auto end() const { return values_.end(); }

            template <typename U>
            auto find(U&& what) {
                auto i = begin();
                while (i != end() && i->first != what) ++i;
                return i;
            }
            template <typename U>
            auto find(U&& what) const {
                auto i = begin();
                while (i != end() && i->first != what) ++i;
                return i;
            }
            template <typename UK, typename UV>
            auto& emplace(UK&& key, UV&& value) {
                auto i = find(key);
                if (i != end()) {
                    i->second = std::forward<UV>(value);
                    return *i;
                }
                return values_.emplace_back(std::forward<UK>(key), std::forward<UV>(value));
            }
            void clear() { values_.clear(); }
        };

        enum class Type : uint8_t {
            NONE = 0, BOOL = 1, I8 = 2, U8 = 3, I16 = 4, U16 = 5, I32 = 6, U32 = 7,
            I64 = 8, U64 = 9, F32 = 10, VEC2 = 11, VEC3 = 12, VEC4 = 13, MTX44 = 14,
            RGBA = 15, STRING = 16, HASH = 17, FILE = 18,
            LIST = 0x80 | 0, LIST2 = 0x80 | 1, POINTER = 0x80 | 2, EMBED = 0x80 | 3,
            LINK = 0x80 | 4, OPTION = 0x80 | 5, MAP = 0x80 | 6, FLAG = 0x80 | 7,
        };

        struct Element;
        struct Field;
        struct Pair;
        using ElementList = std::vector<Element>;
        using FieldList = std::vector<Field>;
        using PairList = std::vector<Pair>;

        struct None { static constexpr Type type = Type::NONE; };
        struct Bool { static constexpr Type type = Type::BOOL;   bool value{}; };
        struct I8 { static constexpr Type type = Type::I8;     int8_t value{}; };
        struct U8 { static constexpr Type type = Type::U8;     uint8_t value{}; };
        struct I16 { static constexpr Type type = Type::I16;    int16_t value{}; };
        struct U16 { static constexpr Type type = Type::U16;    uint16_t value{}; };
        struct I32 { static constexpr Type type = Type::I32;    int32_t value{}; };
        struct U32 { static constexpr Type type = Type::U32;    uint32_t value{}; };
        struct I64 { static constexpr Type type = Type::I64;    int64_t value{}; };
        struct U64 { static constexpr Type type = Type::U64;    uint64_t value{}; };
        struct F32 { static constexpr Type type = Type::F32;    float value{}; };
        struct Vec2 { static constexpr Type type = Type::VEC2;   std::array<float, 2> value{}; };
        struct Vec3 { static constexpr Type type = Type::VEC3;   std::array<float, 3> value{}; };
        struct Vec4 { static constexpr Type type = Type::VEC4;   std::array<float, 4> value{}; };
        struct Mtx44 { static constexpr Type type = Type::MTX44;  std::array<float, 16> value{}; };
        struct RGBA { static constexpr Type type = Type::RGBA;   std::array<uint8_t, 4> value{}; };
        struct String { static constexpr Type type = Type::STRING; std::string value{}; };
        struct Hash { static constexpr Type type = Type::HASH;   FNV1a value{}; };
        struct File { static constexpr Type type = Type::FILE;   XXH64 value{}; };
        struct List { static constexpr Type type = Type::LIST;   Type valueType{}; ElementList items{}; };
        struct List2 { static constexpr Type type = Type::LIST2;  Type valueType{}; ElementList items{}; };
        struct Link { static constexpr Type type = Type::LINK;   FNV1a value{}; };
        struct Option { static constexpr Type type = Type::OPTION; Type valueType{}; ElementList items{}; };
        struct Map { static constexpr Type type = Type::MAP;    Type keyType{}; Type valueType{}; PairList items{}; };
        struct Flag { static constexpr Type type = Type::FLAG;   bool value{}; };

        struct Pointer {
            static constexpr Type type = Type::POINTER;
            FNV1a name{};
            FieldList items{};
            inline Field* find_field(FNV1a const& key) noexcept;
        };

        struct Embed {
            static constexpr Type type = Type::EMBED;
            FNV1a name{};
            FieldList items{};
            inline Field* find_field(FNV1a const& key) noexcept;
        };

        using Value = std::variant<
            None, Bool, I8, U8, I16, U16, I32, U32, I64, U64, F32,
            Vec2, Vec3, Vec4, Mtx44, RGBA, String, Hash, File,
            List, List2, Pointer, Embed, Link, Option, Map, Flag
        >;

        struct Element { Value value{}; };
        struct Pair { Value key{}; Value value{}; };
        struct Field { FNV1a key{}; Value value{}; };

        inline Field* Pointer::find_field(FNV1a const& key) noexcept {
            for (auto& f : items) if (f.key.hash() == key.hash()) return &f;
            return nullptr;
        }
        inline Field* Embed::find_field(FNV1a const& key) noexcept {
            for (auto& f : items) if (f.key.hash() == key.hash()) return &f;
            return nullptr;
        }

        struct Bin {
            flatmap<std::string, Value> sections;
        };

        template <typename> struct ValueHelperImpl;
        template <typename... T> struct ValueHelperImpl<std::variant<T...>> {
            static Type value_to_type(Value const& v) noexcept {
                return std::visit([](auto&& arg) { return arg.type; }, v);
            }
            static Value type_to_value(Type t) noexcept {
                Value v = None{};
                (void)((T::type == t ? (v = T{}, true) : false) || ...);
                return v;
            }
            static constexpr bool is_primitive(Type t) noexcept {
                return !(static_cast<uint8_t>(t) & 0x80);
            }
            static constexpr bool is_container(Type t) noexcept {
                return t == Type::LIST || t == Type::LIST2 || t == Type::OPTION || t == Type::MAP;
            }
            static bool raw_to_type(uint8_t raw, Type& type, bool legacy = false) noexcept {
                if (legacy) {
                    if (raw >= 18 && raw < 0x80) raw = (raw - 18) | 0x80;
                    if (raw >= 0x81) raw += 1;
                }
                type = static_cast<Type>(raw);
                return is_primitive(type) ? (type <= Type::FILE) : (type <= Type::FLAG);
            }
        };
        using ValueHelper = ValueHelperImpl<Value>;

        // --- Binary Reader ---
        struct BinaryReader {
            char const* const beg_;
            char const* cur_;
            char const* const cap_;
            bool legacy_ = false;

            size_t position() const noexcept { return cur_ - beg_; }

            template <typename T>
            bool read(T& v) noexcept {
                static_assert(std::is_arithmetic_v<T>);
                if (cur_ + sizeof(T) > cap_) return false;
                std::memcpy(&v, cur_, sizeof(T));
                cur_ += sizeof(T);
                return true;
            }
            template <typename T, size_t S>
            bool read(std::array<T, S>& v) noexcept {
                if (cur_ + sizeof(T) * S > cap_) return false;
                std::memcpy(v.data(), cur_, sizeof(T) * S);
                cur_ += sizeof(T) * S;
                return true;
            }
            template <typename T>
            bool read(std::vector<T>& v, size_t count) noexcept {
                if (cur_ + sizeof(T) * count > cap_) return false;
                v.resize(count);
                std::memcpy(v.data(), cur_, sizeof(T) * count);
                cur_ += sizeof(T) * count;
                return true;
            }
            bool read(std::string& v) noexcept {
                uint16_t sz{};
                if (!read(sz) || cur_ + sz > cap_) return false;
                v.assign(cur_, sz);
                cur_ += sz;
                return true;
            }
            bool read(Type& v) noexcept {
                uint8_t raw{};
                if (!read(raw)) return false;
                return ValueHelper::raw_to_type(raw, v, legacy_);
            }
            bool read(FNV1a& v) noexcept { uint32_t h{}; if (!read(h)) return false; v = FNV1a{ h }; return true; }
            bool read(XXH64& v) noexcept { uint64_t h{}; if (!read(h)) return false; v = XXH64{ h }; return true; }

            bool read_value_of(Value& val, Type t) noexcept {
                val = ValueHelper::type_to_value(t);
                return std::visit([this](auto& arg) { return read_visit(arg); }, val);
            }
            bool read_visit(None&) noexcept { return false; }
            bool read_visit(Embed& v) noexcept {
                uint32_t sz{}; uint16_t cnt{};
                if (!read(v.name) || !read(sz)) return false;
                size_t pos = position();
                if (!read(cnt)) return false;
                for (size_t i = 0; i < cnt; ++i) {
                    auto& [k, item] = v.items.emplace_back();
                    Type t{};
                    if (!read(k) || !read(t) || !read_value_of(item, t)) return false;
                }
                return position() == pos + sz;
            }
            bool read_visit(Pointer& v) noexcept {
                uint32_t sz{}; uint16_t cnt{};
                if (!read(v.name)) return false;
                if (v.name.hash() == 0) return true;
                if (!read(sz)) return false;
                size_t pos = position();
                if (!read(cnt)) return false;
                for (size_t i = 0; i < cnt; ++i) {
                    auto& [k, item] = v.items.emplace_back();
                    Type t{};
                    if (!read(k) || !read(t) || !read_value_of(item, t)) return false;
                }
                return position() == pos + sz;
            }
            bool read_visit(Option& v) noexcept {
                uint8_t cnt{};
                if (!read(v.valueType) || ValueHelper::is_container(v.valueType) || !read(cnt)) return false;
                if (cnt != 0) {
                    auto& [item] = v.items.emplace_back();
                    if (!read_value_of(item, v.valueType)) return false;
                }
                return true;
            }
            bool read_visit(List& v) noexcept {
                uint32_t sz{}, cnt{};
                if (!read(v.valueType) || ValueHelper::is_container(v.valueType) || !read(sz)) return false;
                size_t pos = position();
                if (!read(cnt)) return false;
                for (size_t i = 0; i < cnt; ++i) {
                    auto& [item] = v.items.emplace_back();
                    if (!read_value_of(item, v.valueType)) return false;
                }
                return position() == pos + sz;
            }
            bool read_visit(List2& v) noexcept {
                uint32_t sz{}, cnt{};
                if (!read(v.valueType) || ValueHelper::is_container(v.valueType) || !read(sz)) return false;
                size_t pos = position();
                if (!read(cnt)) return false;
                for (size_t i = 0; i < cnt; ++i) {
                    auto& [item] = v.items.emplace_back();
                    if (!read_value_of(item, v.valueType)) return false;
                }
                return position() == pos + sz;
            }
            bool read_visit(Map& v) noexcept {
                uint32_t sz{}, cnt{};
                if (!read(v.keyType) || !ValueHelper::is_primitive(v.keyType) ||
                    !read(v.valueType) || ValueHelper::is_container(v.valueType) || !read(sz)) return false;
                size_t pos = position();
                if (!read(cnt)) return false;
                for (size_t i = 0; i < cnt; ++i) {
                    auto& [k, item] = v.items.emplace_back();
                    if (!read_value_of(k, v.keyType) || !read_value_of(item, v.valueType)) return false;
                }
                return position() == pos + sz;
            }
            template <typename T>
            bool read_visit(T& v) noexcept { return read(v.value); }

            bool parse_bin(Bin& bin) noexcept {
                bin.sections.clear();
                std::array<char, 4> magic{};
                uint32_t version = 0;
                if (!read(magic)) return false;
                bool is_patch = false;
                if (magic == std::array{ 'P', 'T', 'C', 'H' }) {
                    uint64_t unk{};
                    if (!read(unk) || !read(magic)) return false;
                    bin.sections.emplace("type", String{ "PTCH" });
                    is_patch = true;
                }
                else {
                    bin.sections.emplace("type", String{ "PROP" });
                }
                if (magic != std::array{ 'P', 'R', 'O', 'P' } || !read(version)) return false;
                bin.sections.emplace("version", U32{ version });

                if (version >= 2) {
                    List linked{ Type::STRING, {} };
                    uint32_t cnt{};
                    if (!read(cnt)) return false;
                    for (uint32_t i = 0; i < cnt; ++i) {
                        String s{};
                        if (!read(s.value)) return false;
                        linked.items.emplace_back(Element{ std::move(s) });
                    }
                    bin.sections.emplace("linked", std::move(linked));
                }

                uint32_t entry_cnt{};
                std::vector<uint32_t> entry_hashes;
                if (!read(entry_cnt) || !read(entry_hashes, entry_cnt)) return false;

                Map entries{ Type::HASH, Type::EMBED, {} };
                for (uint32_t eh : entry_hashes) {
                    Hash key_hash{};
                    Embed entry{ {eh}, {} };
                    uint32_t len{};
                    uint16_t fcnt{};
                    if (!read(len)) return false;
                    size_t pos = position();
                    if (!read(key_hash.value) || !read(fcnt)) return false;
                    for (uint16_t i = 0; i < fcnt; ++i) {
                        auto& [k, item] = entry.items.emplace_back();
                        Type t{};
                        if (!read(k) || !read(t) || !read_value_of(item, t)) return false;
                    }
                    if (position() != pos + len) return false;
                    entries.items.emplace_back(Pair{ std::move(key_hash), std::move(entry) });
                }
                bin.sections.emplace("entries", std::move(entries));

                if (is_patch && cur_ < cap_) {
                    uint32_t pcnt{};
                    if (!read(pcnt)) return false;
                    Map patches{ Type::HASH, Type::EMBED, {} };
                    for (uint32_t i = 0; i < pcnt; ++i) {
                        Hash pk{};
                        Embed pe{ {"patch"}, {} };
                        uint32_t plen{};
                        if (!read(pk.value) || !read(plen)) return false;
                        size_t pos = position();
                        Type pt{}; String pname{}; Value pval{};
                        if (!read(pt) || !read(pname.value) || !read_value_of(pval, pt)) return false;
                        if (position() != pos + plen) return false;
                        pe.items.emplace_back(Field{ {"path"}, std::move(pname) });
                        pe.items.emplace_back(Field{ {"value"}, std::move(pval) });
                        patches.items.emplace_back(Pair{ std::move(pk), std::move(pe) });
                    }
                    bin.sections.emplace("patches", std::move(patches));
                }
                return cur_ == cap_;
            }
        };

        inline bool read_binary(Bin& b, std::span<const char> data) noexcept {
            BinaryReader reader{ data.data(), data.data(), data.data() + data.size(), false };
            if (reader.parse_bin(b)) return true;
            BinaryReader legacy_reader{ data.data(), data.data(), data.data() + data.size(), true };
            return legacy_reader.parse_bin(b);
        }

        // --- Binary Writer ---
        struct BinaryWriter {
            std::vector<char>& buf_;

            size_t position() const noexcept { return buf_.size(); }
            void skip(size_t sz) { buf_.insert(buf_.end(), sz, '\x00'); }
            void write_at(size_t off, size_t val) {
                uint32_t tmp = static_cast<uint32_t>(val);
                std::memcpy(buf_.data() + off, &tmp, sizeof(uint32_t));
            }
            template <typename T>
            void write(T v) {
                static_assert(std::is_arithmetic_v<T>);
                char b[sizeof(T)];
                std::memcpy(b, &v, sizeof(T));
                buf_.insert(buf_.end(), b, b + sizeof(T));
            }
            void write(bool v) { write(static_cast<uint8_t>(v)); }
            void write(Type t) { write(static_cast<uint8_t>(t)); }
            template <typename T, size_t S>
            void write(std::array<T, S> const& v) {
                char b[sizeof(T) * S];
                std::memcpy(b, v.data(), sizeof(b));
                buf_.insert(buf_.end(), b, b + sizeof(b));
            }
            void write(std::string const& v) {
                write(static_cast<uint16_t>(v.size()));
                buf_.insert(buf_.end(), v.data(), v.data() + v.size());
            }
            void write(FNV1a const& v) { write(v.hash()); }
            void write(XXH64 const& v) { write(v.hash()); }

            bool write_value(Value const& v) {
                return std::visit([this](auto const& arg) { return write_visit(arg); }, v);
            }
            bool write_visit(None const&) { return true; }
            bool write_visit(Embed const& v) {
                write(v.name);
                size_t pos = position();
                write(uint32_t{ 0 });
                write(static_cast<uint16_t>(v.items.size()));
                for (auto const& [k, item] : v.items) {
                    write(k.hash());
                    write(ValueHelper::value_to_type(item));
                    if (!write_value(item)) return false;
                }
                write_at(pos, position() - pos - 4);
                return true;
            }
            bool write_visit(Pointer const& v) {
                write(v.name);
                if (v.name.hash() == 0) return true;
                size_t pos = position();
                write(uint32_t{ 0 });
                write(static_cast<uint16_t>(v.items.size()));
                for (auto const& [k, item] : v.items) {
                    write(k.hash());
                    write(ValueHelper::value_to_type(item));
                    if (!write_value(item)) return false;
                }
                write_at(pos, position() - pos - 4);
                return true;
            }
            bool write_visit(List const& v) {
                write(v.valueType);
                size_t pos = position();
                write(uint32_t{ 0 });
                write(static_cast<uint32_t>(v.items.size()));
                for (auto const& [item] : v.items) if (!write_value(item)) return false;
                write_at(pos, position() - pos - 4);
                return true;
            }
            bool write_visit(List2 const& v) {
                write(v.valueType);
                size_t pos = position();
                write(uint32_t{ 0 });
                write(static_cast<uint32_t>(v.items.size()));
                for (auto const& [item] : v.items) if (!write_value(item)) return false;
                write_at(pos, position() - pos - 4);
                return true;
            }
            bool write_visit(Option const& v) {
                write(v.valueType);
                write(static_cast<uint8_t>(v.items.size()));
                for (auto const& [item] : v.items) if (!write_value(item)) return false;
                return true;
            }
            bool write_visit(Map const& v) {
                write(v.keyType);
                write(v.valueType);
                size_t pos = position();
                write(uint32_t{ 0 });
                write(static_cast<uint32_t>(v.items.size()));
                for (auto const& [k, item] : v.items) {
                    if (!write_value(k) || !write_value(item)) return false;
                }
                write_at(pos, position() - pos - 4);
                return true;
            }
            template <typename T>
            bool write_visit(T const& v) { write(v.value); return true; }

            bool serialize_bin(Bin const& b) {
                buf_.clear();
                auto type_sec = b.sections.find("type");
                bool is_patch = (type_sec != b.sections.end() && std::get<String>(type_sec->second).value == "PTCH");
                if (is_patch) {
                    write(std::array{ 'P', 'T', 'C', 'H' });
                    write(uint32_t{ 1 });
                    write(uint32_t{ 0 });
                }
                write(std::array{ 'P', 'R', 'O', 'P' });
                uint32_t version = 3;
                if (auto ver_sec = b.sections.find("version"); ver_sec != b.sections.end()) {
                    version = std::get<U32>(ver_sec->second).value;
                }
                write(version);

                if (version >= 2) {
                    if (auto l_sec = b.sections.find("linked"); l_sec != b.sections.end()) {
                        auto const& l = std::get<List>(l_sec->second);
                        write(static_cast<uint32_t>(l.items.size()));
                        for (auto const& [item] : l.items) write(std::get<String>(item).value);
                    }
                    else {
                        write(uint32_t{ 0 });
                    }
                }

                if (auto e_sec = b.sections.find("entries"); e_sec != b.sections.end()) {
                    auto const& entries = std::get<Map>(e_sec->second);
                    write(static_cast<uint32_t>(entries.items.size()));
                    size_t hashes_off = position();
                    skip(sizeof(uint32_t) * entries.items.size());
                    std::vector<uint32_t> type_hashes;
                    for (auto const& [k, v] : entries.items) {
                        auto const& key = std::get<Hash>(k);
                        auto const& emb = std::get<Embed>(v);
                        type_hashes.push_back(emb.name.hash());

                        size_t pos = position();
                        write(uint32_t{ 0 });
                        write(key.value.hash());
                        write(static_cast<uint16_t>(emb.items.size()));
                        for (auto const& [fk, fitem] : emb.items) {
                            write(fk.hash());
                            write(ValueHelper::value_to_type(fitem));
                            if (!write_value(fitem)) return false;
                        }
                        write_at(pos, position() - pos - 4);
                    }
                    std::memcpy(buf_.data() + hashes_off, type_hashes.data(), type_hashes.size() * sizeof(uint32_t));
                }
                else {
                    write(uint32_t{ 0 });
                }
                return true;
            }
        };
    } // namespace bin

    // =========================================================================
    // 3. SELF-CONTAINED WAD V3 READER & WRITER
    // =========================================================================
    namespace wad {
#pragma pack(push, 1)
        struct RawEntryV3 {
            uint64_t path_hash;
            uint32_t offset;
            uint32_t compressed_size;
            uint32_t uncompressed_size;
            uint8_t  type;
            uint8_t  subchunk_hi;
            uint16_t subchunk_lo;
            uint64_t checksum;
        };
#pragma pack(pop)
        static_assert(sizeof(RawEntryV3) == 32);

        struct ChunkData {
            uint8_t compression_type = 0; 
            std::vector<char> uncompressed_bytes;
        };

        inline bool read_wad_toc_hashes(fs::path const& path, std::vector<RawEntryV3>& out_toc) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            char header[272];
            if (!f.read(header, 272)) return false;
            if (header[0] != 'R' || header[1] != 'W' || header[2] != 3) return false;

            uint32_t count = 0;
            std::memcpy(&count, header + 268, sizeof(uint32_t));
            out_toc.resize(count);
            if (count > 0 && !f.read(reinterpret_cast<char*>(out_toc.data()), count * sizeof(RawEntryV3))) {
                return false;
            }
            return true;
        }

        inline std::vector<char> decompress_wad_chunk(const char* comp_data, size_t comp_size, size_t uncomp_size, uint8_t type) {
            uint8_t ctype = type & 0x0F;
            if (ctype == 0 || ctype == 1) {
                return std::vector<char>(comp_data, comp_data + comp_size);
            }
            if (ctype == 3 || ctype == 4) {
                static constexpr uint8_t zstd_magic[4] = { 0x28, 0xB5, 0x2F, 0xFD };
                const char* magic_pos = std::search(comp_data, comp_data + comp_size,
                    reinterpret_cast<const char*>(zstd_magic),
                    reinterpret_cast<const char*>(zstd_magic) + 4);
                size_t prefix_len = (magic_pos != comp_data + comp_size) ? (magic_pos - comp_data) : 0;

                std::vector<char> out(uncomp_size);
                if (prefix_len > 0 && prefix_len <= uncomp_size) {
                    std::memcpy(out.data(), comp_data, prefix_len);
                }
                size_t res = ZSTD_decompress(out.data() + prefix_len, uncomp_size - prefix_len,
                    comp_data + prefix_len, comp_size - prefix_len);
                if (ZSTD_isError(res)) {
                    throw std::runtime_error(std::string("ZSTD decompression error: ") + ZSTD_getErrorName(res));
                }
                return out;
            }
            throw std::runtime_error("Unsupported WAD chunk compression type");
        }

        inline std::map<uint64_t, std::vector<char>> parse_wad_from_memory(std::span<const char> wad_bytes) {
            if (wad_bytes.size() < 272 || wad_bytes[0] != 'R' || wad_bytes[1] != 'W') {
                throw std::runtime_error("Invalid WAD header in Fantome");
            }
            uint32_t count = 0;
            std::memcpy(&count, wad_bytes.data() + 268, sizeof(uint32_t));
            if (wad_bytes.size() < 272 + count * sizeof(RawEntryV3)) {
                throw std::runtime_error("Truncated WAD TOC in Fantome");
            }

            std::map<uint64_t, std::vector<char>> entries;
            auto const* toc = reinterpret_cast<const RawEntryV3*>(wad_bytes.data() + 272);
            for (uint32_t i = 0; i < count; ++i) {
                auto const& e = toc[i];
                if (e.offset + e.compressed_size > wad_bytes.size()) continue;
                entries[e.path_hash] = decompress_wad_chunk(
                    wad_bytes.data() + e.offset, e.compressed_size, e.uncompressed_size, e.type);
            }
            return entries;
        }

        
        inline std::vector<char> build_wad_in_memory(std::map<uint64_t, std::vector<char>> const& entries) {
            uint32_t count = static_cast<uint32_t>(entries.size());
            std::vector<char> out(272 + count * sizeof(RawEntryV3), '\x00');

            
            out[0] = 'R';
            out[1] = 'W';
            out[2] = 3;
            out[3] = 4;
            std::memcpy(out.data() + 268, &count, sizeof(uint32_t));

            std::vector<RawEntryV3> toc;
            toc.reserve(count);

            for (auto const& [hash, raw_data] : entries) {
                
                bool is_audio_bank = (raw_data.size() >= 4 &&
                    (std::memcmp(raw_data.data(), "BKHD", 4) == 0 ||
                        std::memcmp(raw_data.data(), "r3d2", 4) == 0));

                uint32_t offset = static_cast<uint32_t>(out.size());
                uint32_t uncomp_size = static_cast<uint32_t>(raw_data.size());
                uint32_t comp_size = 0;
                uint8_t  type = 0;

                if (is_audio_bank || raw_data.empty()) {
                    comp_size = uncomp_size;
                    type = 0; 
                    out.insert(out.end(), raw_data.begin(), raw_data.end());
                }
                else {
                    size_t bound = ZSTD_compressBound(raw_data.size());
                    std::vector<char> comp_buf(bound);
                    size_t csize = ZSTD_compress(comp_buf.data(), bound, raw_data.data(), raw_data.size(), 3);
                    if (ZSTD_isError(csize)) {
                        throw std::runtime_error("ZSTD compression failed while building WAD");
                    }
                    comp_size = static_cast<uint32_t>(csize);
                    type = 3;
                    out.insert(out.end(), comp_buf.data(), comp_buf.data() + comp_size);
                }

                
                uint64_t checksum = XXH64::compute({ out.data() + offset, comp_size });

                toc.push_back(RawEntryV3{
                    .path_hash = hash,
                    .offset = offset,
                    .compressed_size = comp_size,
                    .uncompressed_size = uncomp_size,
                    .type = type,
                    .subchunk_hi = 0,
                    .subchunk_lo = 0,
                    .checksum = checksum
                    });
            }

            std::memcpy(out.data() + 272, toc.data(), toc.size() * sizeof(RawEntryV3));
            return out;
        }

        
        inline std::vector<char> extract_hash_from_disk_wad(fs::path const& wad_path, uint64_t target_hash) {
            std::vector<RawEntryV3> toc;
            if (!read_wad_toc_hashes(wad_path, toc)) {
                throw std::runtime_error("Failed to read game WAD: " + wad_path.string());
            }
            for (auto const& e : toc) {
                if (e.path_hash == target_hash) {
                    std::ifstream f(wad_path, std::ios::binary);
                    f.seekg(e.offset, std::ios::beg);
                    std::vector<char> comp_buf(e.compressed_size);
                    if (!f.read(comp_buf.data(), e.compressed_size)) {
                        throw std::runtime_error("Failed to read chunk from game WAD");
                    }
                    return decompress_wad_chunk(comp_buf.data(), e.compressed_size, e.uncompressed_size, e.type);
                }
            }
            throw std::runtime_error("Hash not found in target game WAD: " + wad_path.string());
        }
    } // namespace wad

    // =========================================================================
    // 4. HELPERS (DEAD BYTES PADDING & CHAMPION NAME EXTRACTION)
    // =========================================================================
    inline void add_safe_dead_bytes(std::vector<char>& data, bool is_skl, size_t pad_size = 16) {
        if (data.size() < 12) return;
        data.insert(data.end(), pad_size, '\x00');

        if (is_skl) {
           
            uint32_t signature = 0;
            std::memcpy(&signature, data.data() + 4, sizeof(uint32_t));
            if (signature == 0x22FD4FC3) {
                uint32_t new_file_size = static_cast<uint32_t>(data.size());
                std::memcpy(data.data(), &new_file_size, sizeof(uint32_t));
            }
        }
    }

    inline std::string extract_champ_from_path(std::string_view path) {
        std::string lower(path);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::string_view marker = "characters/";
        auto pos = lower.find(marker);
        if (pos != std::string::npos) {
            pos += marker.size();
            auto end = path.find_first_of("/\\", pos);
            if (end != std::string_view::npos) {
                return std::string(path.substr(pos, end - pos));
            }
        }
        return "";
    }

    // =========================================================================
    // 5. MAIN IN-MEMORY FANTOME PROCESSOR
    // =========================================================================
    inline std::vector<char> process_fantome_in_memory(std::span<const char> fantome_zip_bytes, fs::path const& league_game_dir) {
       
        mz_zip_archive zip_in{};
        if (!mz_zip_reader_init_mem(&zip_in, fantome_zip_bytes.data(), fantome_zip_bytes.size(), 0)) {
            throw std::runtime_error("Failed to open Fantome ZIP from memory");
        }

        struct ZipFileEntry {
            std::string name;
            std::vector<char> bytes;
        };
        std::vector<ZipFileEntry> non_wad_files;
        std::string wad_internal_zip_path;
        std::map<uint64_t, std::vector<char>> mod_wad_entries;
        bool found_packed_wad = false;

        mz_uint num_files = mz_zip_reader_get_num_files(&zip_in);
        for (mz_uint i = 0; i < num_files; ++i) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&zip_in, i, &stat) || stat.m_is_directory) continue;

            std::string fname(stat.m_filename);
            std::vector<char> buf(stat.m_uncomp_size);
            if (!mz_zip_reader_extract_to_mem(&zip_in, i, buf.data(), buf.size(), 0)) {
                mz_zip_reader_end(&zip_in);
                throw std::runtime_error("Failed to extract file from Fantome ZIP: " + fname);
            }

            std::string flLC = fname;
            std::transform(flLC.begin(), flLC.end(), flLC.begin(), ::tolower);

            if (!found_packed_wad && flLC.starts_with("wad/") && flLC.ends_with(".wad.client") &&
                buf.size() >= 4 && buf[0] == 'R' && buf[1] == 'W') {
                wad_internal_zip_path = fname;
                mod_wad_entries = wad::parse_wad_from_memory(buf);
                found_packed_wad = true;
            }
            else if (flLC.starts_with("wad/")) {
               
                auto s1 = fname.find('/');
                auto s2 = fname.find('/', s1 + 1);
                if (s2 != std::string::npos) {
                    if (wad_internal_zip_path.empty()) wad_internal_zip_path = fname.substr(0, s2);
                    std::string rel_path = fname.substr(s2 + 1);
                    mod_wad_entries[XXH64::compute(rel_path)] = std::move(buf);
                }
                else {
                    non_wad_files.push_back({ fname, std::move(buf) });
                }
            }
            else {
                non_wad_files.push_back({ fname, std::move(buf) });
            }
        }
        mz_zip_reader_end(&zip_in);

        if (mod_wad_entries.empty()) {
            throw std::runtime_error("No WAD entries found in Fantome archive");
        }

        
        uint64_t skin_bin_hash = 0;
        std::vector<char>* skin_bin_bytes = nullptr;

        for (auto& [h, data] : mod_wad_entries) {
            if (data.size() >= 4 && (std::memcmp(data.data(), "PROP", 4) == 0 || std::memcmp(data.data(), "PTCH", 4) == 0)) {
                skin_bin_hash = h;
                skin_bin_bytes = &data;
                break;
            }
        }
        if (!skin_bin_bytes) {
            throw std::runtime_error("No PROP/PTCH .bin file found inside the Fantome WAD");
        }

       
        bin::Bin parsed_bin{};
        if (!bin::read_binary(parsed_bin, *skin_bin_bytes)) {
            throw std::runtime_error("Failed to parse skin0.bin binary structure");
        }

        auto entries_it = parsed_bin.sections.find("entries");
        if (entries_it == parsed_bin.sections.end()) {
            throw std::runtime_error("skin0.bin is missing the 'entries' section");
        }
        auto* entries_map = std::get_if<bin::Map>(&entries_it->second);
        if (!entries_map) {
            throw std::runtime_error("skin0.bin 'entries' section is not a Map");
        }

        std::string champ_name;
        std::string skl_path;
        std::string skn_path;

        constexpr uint32_t H_SKIN_CHAR_DATA_PROPS = FNV1a::compute("SkinCharacterDataProperties");
        constexpr uint32_t H_CHAMP_SKIN_NAME = FNV1a::compute("championSkinName");
        constexpr uint32_t H_SKIN_AUDIO_PROPS = FNV1a::compute("SkinAudioProperties");
        constexpr uint32_t H_TAG_EVENT_LIST = FNV1a::compute("tagEventList");
        constexpr uint32_t H_BANK_UNITS = FNV1a::compute("bankUnits");
        constexpr uint32_t H_NAME = FNV1a::compute("name");
        constexpr uint32_t H_SKIN_MESH_PROPS = FNV1a::compute("skinMeshProperties");
        constexpr uint32_t H_SKELETON = FNV1a::compute("skeleton");
        constexpr uint32_t H_SIMPLE_SKIN = FNV1a::compute("simpleSkin");

        for (auto& pair : entries_map->items) {
            auto* embed = std::get_if<bin::Embed>(&pair.value);
            if (!embed || embed->name.hash() != H_SKIN_CHAR_DATA_PROPS) continue;

            
            if (auto* mesh_f = embed->find_field(H_SKIN_MESH_PROPS)) {
                if (auto* mesh_e = std::get_if<bin::Embed>(&mesh_f->value)) {
                    if (auto* skl_f = mesh_e->find_field(H_SKELETON)) {
                        if (auto* s = std::get_if<bin::String>(&skl_f->value)) {
                            skl_path = s->value;
                            champ_name = extract_champ_from_path(skl_path);
                        }
                    }
                    if (auto* skn_f = mesh_e->find_field(H_SIMPLE_SKIN)) {
                        if (auto* s = std::get_if<bin::String>(&skn_f->value)) {
                            skn_path = s->value;
                            if (champ_name.empty()) champ_name = extract_champ_from_path(skn_path);
                        }
                    }
                }
            }

           
            if (auto* audio_f = embed->find_field(H_SKIN_AUDIO_PROPS)) {
                if (auto* audio_e = std::get_if<bin::Embed>(&audio_f->value)) {
                    if (auto* tags_f = audio_e->find_field(H_TAG_EVENT_LIST)) {
                        if (auto* tags_l = std::get_if<bin::List>(&tags_f->value)) {
                            for (auto const& el : tags_l->items) {
                                if (auto const* s = std::get_if<bin::String>(&el.value)) {
                                    if (champ_name.empty() || (!s->value.empty() && s->value.size() < champ_name.size())) {
                                        champ_name = s->value;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (champ_name.empty()) {
                throw std::runtime_error("Could not determine Champion Name from skin0.bin");
            }

          
            if (auto* csn_f = embed->find_field(H_CHAMP_SKIN_NAME)) {
                if (auto* s = std::get_if<bin::String>(&csn_f->value)) {
                    s->value = champ_name;
                }
            }

            
            if (auto* audio_f = embed->find_field(H_SKIN_AUDIO_PROPS)) {
                if (auto* audio_e = std::get_if<bin::Embed>(&audio_f->value)) {
                   
                    if (auto* tags_f = audio_e->find_field(H_TAG_EVENT_LIST)) {
                        if (auto* tags_l = std::get_if<bin::List>(&tags_f->value)) {
                            tags_l->items.clear();
                            tags_l->items.push_back(bin::Element{ bin::String{champ_name} });
                        }
                    }

              
                    if (auto* units_f = audio_e->find_field(H_BANK_UNITS)) {
                        auto update_units = [&](std::vector<bin::Element>& items) {
                            for (auto& item : items) {
                                if (auto* bu_e = std::get_if<bin::Embed>(&item.value)) {
                                    if (auto* name_f = bu_e->find_field(H_NAME)) {
                                        if (auto* s = std::get_if<bin::String>(&name_f->value)) {
                                            s->value = champ_name;
                                        }
                                    }
                                }
                            }
                            };
                        if (auto* l2 = std::get_if<bin::List2>(&units_f->value)) update_units(l2->items);
                        else if (auto* l1 = std::get_if<bin::List>(&units_f->value)) update_units(l1->items);
                    }
                }
            }
        }

        std::vector<char> new_bin_bytes;
        bin::BinaryWriter writer{ new_bin_bytes };
        if (!writer.serialize_bin(parsed_bin)) {
            throw std::runtime_error("Failed to serialize modified skin0.bin");
        }
        *skin_bin_bytes = std::move(new_bin_bytes);

        fs::path final_dir = league_game_dir / "DATA" / "FINAL";
        if (!fs::exists(final_dir)) {
            if (fs::exists(league_game_dir / "Game" / "DATA" / "FINAL")) {
                final_dir = league_game_dir / "Game" / "DATA" / "FINAL";
            }
            else {
                throw std::runtime_error("Invalid League Game path (DATA/FINAL not found): " + league_game_dir.string());
            }
        }

        fs::path best_wad_path;
        size_t best_overlap_score = 0;

        for (auto const& dirent : fs::recursive_directory_iterator(final_dir)) {
            if (!dirent.is_regular_file()) continue;
            auto p = dirent.path();
            auto fname = p.filename().generic_string();
            if (!fname.ends_with(".wad.client")) continue;

            std::vector<wad::RawEntryV3> toc;
            if (!wad::read_wad_toc_hashes(p, toc)) continue;

            size_t score = 0;
            bool has_exact_bin = false;
            for (auto const& e : toc) {
                if (e.path_hash == skin_bin_hash) {
                    has_exact_bin = true;
                    score += 10000; 
                }
                if (mod_wad_entries.contains(e.path_hash)) {
                    score += 1;
                }
            }

            if (score > best_overlap_score) {
                best_overlap_score = score;
                best_wad_path = p;
                if (has_exact_bin) break;
            }
        }

        if (best_wad_path.empty()) {
            throw std::runtime_error("Could not find matching original WAD in Game folder via overlap");
        }

        
        if (!skl_path.empty()) {
            uint64_t skl_hash = XXH64::compute(skl_path);
            auto skl_bytes = wad::extract_hash_from_disk_wad(best_wad_path, skl_hash);
            add_safe_dead_bytes(skl_bytes, /*is_skl=*/true, 16);
            mod_wad_entries[skl_hash] = std::move(skl_bytes);
        }

        if (!skn_path.empty()) {
            uint64_t skn_hash = XXH64::compute(skn_path);
            auto skn_bytes = wad::extract_hash_from_disk_wad(best_wad_path, skn_hash);
            add_safe_dead_bytes(skn_bytes, /*is_skl=*/false, 16);
            mod_wad_entries[skn_hash] = std::move(skn_bytes);
        }

        
        if (wad_internal_zip_path.empty()) {
            wad_internal_zip_path = "WAD/" + best_wad_path.filename().generic_string();
        }
        std::vector<char> rebuilt_wad_bytes = wad::build_wad_in_memory(mod_wad_entries);

        mz_zip_archive zip_out{};
        if (!mz_zip_writer_init_heap(&zip_out, 0, rebuilt_wad_bytes.size() + 65536)) {
            throw std::runtime_error("Failed to initialize in-memory ZIP writer");
        }

        for (auto const& file : non_wad_files) {
            if (!mz_zip_writer_add_mem(&zip_out, file.name.c_str(), file.bytes.data(), file.bytes.size(), MZ_DEFAULT_COMPRESSION)) {
                mz_zip_writer_end(&zip_out);
                throw std::runtime_error("Failed to write file to output Fantome ZIP: " + file.name);
            }
        }

        if (!mz_zip_writer_add_mem(&zip_out, wad_internal_zip_path.c_str(), rebuilt_wad_bytes.data(), rebuilt_wad_bytes.size(), MZ_NO_COMPRESSION)) {
            mz_zip_writer_end(&zip_out);
            throw std::runtime_error("Failed to write rebuilt WAD to output Fantome ZIP");
        }

        void* final_zip_buf = nullptr;
        size_t final_zip_size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zip_out, &final_zip_buf, &final_zip_size)) {
            mz_zip_writer_end(&zip_out);
            throw std::runtime_error("Failed to finalize output Fantome ZIP in memory");
        }

        std::vector<char> result(static_cast<char*>(final_zip_buf), static_cast<char*>(final_zip_buf) + final_zip_size);
        mz_free(final_zip_buf);
        mz_zip_writer_end(&zip_out);

        return result;
    }

} // namespace fantome_tool
