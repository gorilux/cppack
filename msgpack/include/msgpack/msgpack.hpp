//
// Created by Mike Loomis on 6/22/2019.
//

#ifndef CPPACK_PACKER_HPP
#define CPPACK_PACKER_HPP

#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <forward_list>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace msgpack {

// Enhanced error codes
enum class unpacker_error {
  out_of_range          = 1,
  invalid_format        = 2,
  type_mismatch         = 3,
  corrupted_data        = 4,
  buffer_overflow       = 5,
  unsupported_extension = 6
};

struct unpacker_err_category : public std::error_category {
public:
  const char* name() const noexcept override {
    return "unpacker";
  };

  std::string message(int ev) const override {
    switch (static_cast<msgpack::unpacker_error>(ev)) {
      case msgpack::unpacker_error::out_of_range:
        return "tried to dereference out of range during deserialization";
      case msgpack::unpacker_error::invalid_format:
        return "invalid format byte encountered";
      case msgpack::unpacker_error::type_mismatch:
        return "type mismatch during deserialization";
      case msgpack::unpacker_error::corrupted_data:
        return "corrupted data detected";
      case msgpack::unpacker_error::buffer_overflow:
        return "buffer overflow during operation";
      case msgpack::unpacker_error::unsupported_extension:
        return "unsupported extension type";
      default:
        return "(unrecognized error)";
    }
  };
};

inline std::error_code make_error_code(msgpack::unpacker_error e) {
  static unpacker_err_category the_unpacker_err_category;
  return {static_cast<int>(e), the_unpacker_err_category};
}
} // namespace msgpack

namespace std {
template <>
struct is_error_code_enum<msgpack::unpacker_error> : public true_type {};
} // namespace std

namespace msgpack {

enum format_constants : uint8_t {
  // positive fixint = 0x00 - 0x7f
  // fixmap = 0x80 - 0x8f
  // fixarray = 0x90 - 0x9f
  // fixstr = 0xa0 - 0xbf
  // negative fixint = 0xe0 - 0xff

  nil        = 0xc0,
  false_bool = 0xc2,
  true_bool  = 0xc3,
  bin8       = 0xc4,
  bin16      = 0xc5,
  bin32      = 0xc6,
  ext8       = 0xc7,
  ext16      = 0xc8,
  ext32      = 0xc9,
  float32    = 0xca,
  float64    = 0xcb,
  uint8      = 0xcc,
  uint16     = 0xcd,
  uint32     = 0xce,
  uint64     = 0xcf,
  int8       = 0xd0,
  int16      = 0xd1,
  int32      = 0xd2,
  int64      = 0xd3,
  fixext1    = 0xd4,
  fixext2    = 0xd5,
  fixext4    = 0xd6,
  fixext8    = 0xd7,
  fixext16   = 0xd8,
  str8       = 0xd9,
  str16      = 0xda,
  str32      = 0xdb,
  array16    = 0xdc,
  array32    = 0xdd,
  map16      = 0xde,
  map32      = 0xdf
};

// Timestamp extension type
constexpr int8_t TIMESTAMP_EXT_TYPE = -1;

// Extension type structure
struct extension {
  int8_t               type;
  std::vector<uint8_t> data;

  extension(int8_t t, std::vector<uint8_t> d)
    : type(t)
    , data(std::move(d)) {
  }
};

// Enhanced container type detection
template <class T>
struct is_container {
  static const bool value = false;
};

template <class T, class Alloc>
struct is_container<std::vector<T, Alloc>> {
  static const bool value = true;
};

template <class T, class Alloc>
struct is_container<std::list<T, Alloc>> {
  static const bool value = true;
};

template <class T, class Alloc>
struct is_container<std::deque<T, Alloc>> {
  static const bool value = true;
};

template <class T, class Alloc>
struct is_container<std::forward_list<T, Alloc>> {
  static const bool value = true;
};

template <class T, class Compare, class Alloc>
struct is_container<std::set<T, Compare, Alloc>> {
  static const bool value = true;
};

template <class T, class Compare, class Alloc>
struct is_container<std::multiset<T, Compare, Alloc>> {
  static const bool value = true;
};

template <class T, class Hash, class Equal, class Alloc>
struct is_container<std::unordered_set<T, Hash, Equal, Alloc>> {
  static const bool value = true;
};

template <class T, class Hash, class Equal, class Alloc>
struct is_container<std::unordered_multiset<T, Hash, Equal, Alloc>> {
  static const bool value = true;
};

template <class T>
struct is_std_array {
  static const bool value = false;
};

template <class T, std::size_t N>
struct is_std_array<std::array<T, N>> {
  static const bool value = true;
};

template <class T>
struct is_map {
  static const bool value = false;
};

template <class K, class V, class Compare, class Alloc>
struct is_map<std::map<K, V, Compare, Alloc>> {
  static const bool value = true;
};

template <class K, class V, class Compare, class Alloc>
struct is_map<std::multimap<K, V, Compare, Alloc>> {
  static const bool value = true;
};

template <class K, class V, class Hash, class Equal, class Alloc>
struct is_map<std::unordered_map<K, V, Hash, Equal, Alloc>> {
  static const bool value = true;
};

template <class K, class V, class Hash, class Equal, class Alloc>
struct is_map<std::unordered_multimap<K, V, Hash, Equal, Alloc>> {
  static const bool value = true;
};

class packer {
public:
  template <class... Types>
  void operator()(const Types&... args) {
    (pack_type(std::forward<const Types&>(args)), ...);
  }

  template <class... Types>
  void process(const Types&... args) {
    (pack_type(std::forward<const Types&>(args)), ...);
  }

  const std::vector<uint8_t>& vector() const {
    return serialized_object_;
  }

  void clear() {
    serialized_object_.clear();
  }

private:
  std::vector<uint8_t> serialized_object_;

  template <class T>
  void pack_type(const T& value) {
    if constexpr (is_map<T>::value) {
      pack_map(value);
    } else if constexpr (is_container<T>::value || is_std_array<T>::value) {
      pack_array(value);
    } else {
      auto recursive_packer = packer{};
      const_cast<T&>(value).pack(recursive_packer);
      pack_type(recursive_packer.vector());
    }
  }

  // Timestamp support
  template <class Clock, class Duration>
  void pack_type(const std::chrono::time_point<Clock, Duration>& value) {
    auto epoch_time  = value.time_since_epoch();
    auto seconds     = std::chrono::duration_cast<std::chrono::seconds>(epoch_time).count();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch_time).count() % 1000000000;

    if (nanoseconds == 0 && seconds <= std::numeric_limits<uint32_t>::max() && seconds >= 0) {
      // 32-bit timestamp
      pack_extension(TIMESTAMP_EXT_TYPE, {static_cast<uint8_t>((seconds >> 24) & 0xff), static_cast<uint8_t>((seconds >> 16) & 0xff), static_cast<uint8_t>((seconds >> 8) & 0xff), static_cast<uint8_t>(seconds & 0xff)});
    } else if (seconds <= 0x3FFFFFFFF && seconds >= 0 && nanoseconds >= 0) {
      // 64-bit timestamp
      uint64_t timestamp64 = (static_cast<uint64_t>(nanoseconds) << 34) | static_cast<uint64_t>(seconds);
      pack_extension(TIMESTAMP_EXT_TYPE,
                     {static_cast<uint8_t>((timestamp64 >> 56) & 0xff),
                      static_cast<uint8_t>((timestamp64 >> 48) & 0xff),
                      static_cast<uint8_t>((timestamp64 >> 40) & 0xff),
                      static_cast<uint8_t>((timestamp64 >> 32) & 0xff),
                      static_cast<uint8_t>((timestamp64 >> 24) & 0xff),
                      static_cast<uint8_t>((timestamp64 >> 16) & 0xff),
                      static_cast<uint8_t>((timestamp64 >> 8) & 0xff),
                      static_cast<uint8_t>(timestamp64 & 0xff)});
    } else {
      // 96-bit timestamp
      std::vector<uint8_t> data(12);
      // nanoseconds (4 bytes)
      data[0] = static_cast<uint8_t>((nanoseconds >> 24) & 0xff);
      data[1] = static_cast<uint8_t>((nanoseconds >> 16) & 0xff);
      data[2] = static_cast<uint8_t>((nanoseconds >> 8) & 0xff);
      data[3] = static_cast<uint8_t>(nanoseconds & 0xff);
      // seconds (8 bytes)
      auto useconds = static_cast<uint64_t>(seconds);
      data[4]       = static_cast<uint8_t>((useconds >> 56) & 0xff);
      data[5]       = static_cast<uint8_t>((useconds >> 48) & 0xff);
      data[6]       = static_cast<uint8_t>((useconds >> 40) & 0xff);
      data[7]       = static_cast<uint8_t>((useconds >> 32) & 0xff);
      data[8]       = static_cast<uint8_t>((useconds >> 24) & 0xff);
      data[9]       = static_cast<uint8_t>((useconds >> 16) & 0xff);
      data[10]      = static_cast<uint8_t>((useconds >> 8) & 0xff);
      data[11]      = static_cast<uint8_t>(useconds & 0xff);
      pack_extension(TIMESTAMP_EXT_TYPE, data);
    }
  }

  // Extension type packing
  void pack_extension(int8_t type, const std::vector<uint8_t>& data) {
    size_t size = data.size();

    if (size == 1) {
      serialized_object_.emplace_back(fixext1);
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size == 2) {
      serialized_object_.emplace_back(fixext2);
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size == 4) {
      serialized_object_.emplace_back(fixext4);
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size == 8) {
      serialized_object_.emplace_back(fixext8);
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size == 16) {
      serialized_object_.emplace_back(fixext16);
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size <= 255) {
      serialized_object_.emplace_back(ext8);
      serialized_object_.emplace_back(static_cast<uint8_t>(size));
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size <= 65535) {
      serialized_object_.emplace_back(ext16);
      for (auto i = sizeof(uint16_t); i > 0; --i) {
        serialized_object_.emplace_back(static_cast<uint8_t>(size >> (8U * (i - 1)) & 0xff));
      }
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    } else if (size <= std::numeric_limits<uint32_t>::max()) {
      serialized_object_.emplace_back(ext32);
      for (auto i = sizeof(uint32_t); i > 0; --i) {
        serialized_object_.emplace_back(static_cast<uint8_t>(size >> (8U * (i - 1)) & 0xff));
      }
      serialized_object_.emplace_back(static_cast<uint8_t>(type));
    }

    for (uint8_t byte : data) {
      serialized_object_.emplace_back(byte);
    }
  }

  template <class T>
  void pack_array(const T& array) {
    if (array.size() <= 15) {
      auto size_mask = uint8_t(0b10010000);
      serialized_object_.emplace_back(uint8_t(array.size() | size_mask));
    } else if (array.size() <= std::numeric_limits<uint16_t>::max()) {
      serialized_object_.emplace_back(array16);
      for (auto i = sizeof(uint16_t); i > 0; --i) {
        serialized_object_.emplace_back(uint8_t(array.size() >> (8U * (i - 1)) & 0xff));
      }
    } else if (array.size() <= std::numeric_limits<uint32_t>::max()) {
      serialized_object_.emplace_back(array32);
      for (auto i = sizeof(uint32_t); i > 0; --i) {
        serialized_object_.emplace_back(uint8_t(array.size() >> (8U * (i - 1)) & 0xff));
      }
    } else {
      return; // Give up if array is too long
    }
    for (const auto& elem : array) {
      pack_type(elem);
    }
  }

  template <class T>
  void pack_map(const T& map) {
    if (map.size() <= 15) {
      auto size_mask = uint8_t(0b10000000);
      serialized_object_.emplace_back(uint8_t(map.size() | size_mask));
    } else if (map.size() <= 65535) {
      serialized_object_.emplace_back(map16);
      for (auto i = sizeof(uint16_t); i > 0; --i) {
        serialized_object_.emplace_back(uint8_t(map.size() >> (8U * (i - 1)) & 0xff));
      }
    } else if (map.size() <= std::numeric_limits<uint32_t>::max()) {
      serialized_object_.emplace_back(map32);
      for (auto i = sizeof(uint32_t); i > 0; --i) {
        serialized_object_.emplace_back(uint8_t(map.size() >> (8U * (i - 1)) & 0xff));
      }
    }
    for (const auto& elem : map) {
      if constexpr (std::is_same_v<T, std::map<typename T::key_type, typename T::mapped_type, typename T::key_compare, typename T::allocator_type>> ||
                    std::is_same_v<T, std::unordered_map<typename T::key_type, typename T::mapped_type, typename T::hasher, typename T::key_equal, typename T::allocator_type>>) {
        pack_type(elem.first);
        pack_type(elem.second);
      } else {
        pack_type(std::get<0>(elem));
        pack_type(std::get<1>(elem));
      }
    }
  }

  // Safe float to bits conversion
  uint32_t float_to_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }

  uint64_t double_to_bits(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }
};

// Fixed integer specializations with proper negative fixint handling
template <>
inline void packer::pack_type(const int8_t& value) {
  if (value >= -32 && value <= 127) {
    // Use fixint format for values in range [-32, 127]
    if (value >= 0) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value));
    } else {
      serialized_object_.emplace_back(static_cast<uint8_t>(value)); // negative fixint
    }
  } else {
    serialized_object_.emplace_back(int8);
    serialized_object_.emplace_back(static_cast<uint8_t>(value));
  }
}

template <>
inline void packer::pack_type(const int16_t& value) {
  if (value >= -32 && value <= 127) {
    pack_type(static_cast<int8_t>(value));
  } else if (value >= std::numeric_limits<int8_t>::min() && value <= std::numeric_limits<int8_t>::max()) {
    pack_type(static_cast<int8_t>(value));
  } else {
    serialized_object_.emplace_back(int16);
    uint16_t bits = static_cast<uint16_t>(value);
    for (auto i = sizeof(value); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
    }
  }
}

template <>
inline void packer::pack_type(const int32_t& value) {
  if (value >= std::numeric_limits<int16_t>::min() && value <= std::numeric_limits<int16_t>::max()) {
    pack_type(static_cast<int16_t>(value));
  } else {
    serialized_object_.emplace_back(int32);
    uint32_t bits = static_cast<uint32_t>(value);
    for (auto i = sizeof(value); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
    }
  }
}

template <>
inline void packer::pack_type(const int64_t& value) {
  if (value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max()) {
    pack_type(static_cast<int32_t>(value));
  } else {
    serialized_object_.emplace_back(int64);
    uint64_t bits = static_cast<uint64_t>(value);
    for (auto i = sizeof(value); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
    }
  }
}

template <>
inline void packer::pack_type(const uint8_t& value) {
  if (value <= 0x7f) {
    serialized_object_.emplace_back(value);
  } else {
    serialized_object_.emplace_back(uint8);
    serialized_object_.emplace_back(value);
  }
}

template <>
inline void packer::pack_type(const uint16_t& value) {
  if (value <= std::numeric_limits<uint8_t>::max()) {
    pack_type(static_cast<uint8_t>(value));
  } else {
    serialized_object_.emplace_back(uint16);
    for (auto i = sizeof(value); i > 0U; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value >> (8U * (i - 1)) & 0xff));
    }
  }
}

template <>
inline void packer::pack_type(const uint32_t& value) {
  if (value <= std::numeric_limits<uint16_t>::max()) {
    pack_type(static_cast<uint16_t>(value));
  } else {
    serialized_object_.emplace_back(uint32);
    for (auto i = sizeof(value); i > 0U; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value >> (8U * (i - 1)) & 0xff));
    }
  }
}

template <>
inline void packer::pack_type(const uint64_t& value) {
  if (value <= std::numeric_limits<uint32_t>::max()) {
    pack_type(static_cast<uint32_t>(value));
  } else {
    serialized_object_.emplace_back(uint64);
    for (auto i = sizeof(value); i > 0U; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value >> (8U * (i - 1)) & 0xff));
    }
  }
}

template <>
inline void packer::pack_type(const std::nullptr_t& /*value*/) {
  serialized_object_.emplace_back(nil);
}

template <>
inline void packer::pack_type(const bool& value) {
  if (value) {
    serialized_object_.emplace_back(true_bool);
  } else {
    serialized_object_.emplace_back(false_bool);
  }
}

// Fixed float/double with safe bit conversion
template <>
inline void packer::pack_type(const float& value) {
  // Check for special values
  if (std::isnan(value) || std::isinf(value) || std::floor(value) != value) {
    serialized_object_.emplace_back(float32);
    uint32_t bits = float_to_bits(value);
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
    }
  } else {
    // Try to pack as integer if it's a whole number
    auto int_val = static_cast<int64_t>(value);
    if (static_cast<float>(int_val) == value) {
      pack_type(int_val);
    } else {
      serialized_object_.emplace_back(float32);
      uint32_t bits = float_to_bits(value);
      for (auto i = sizeof(uint32_t); i > 0; --i) {
        serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
      }
    }
  }
}

template <>
inline void packer::pack_type(const double& value) {
  // Check for special values
  if (std::isnan(value) || std::isinf(value) || std::floor(value) != value) {
    serialized_object_.emplace_back(float64);
    uint64_t bits = double_to_bits(value);
    for (auto i = sizeof(uint64_t); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
    }
  } else {
    // Try to pack as integer if it's a whole number
    auto int_val = static_cast<int64_t>(value);
    if (static_cast<double>(int_val) == value) {
      pack_type(int_val);
    } else {
      serialized_object_.emplace_back(float64);
      uint64_t bits = double_to_bits(value);
      for (auto i = sizeof(uint64_t); i > 0; --i) {
        serialized_object_.emplace_back(static_cast<uint8_t>(bits >> (8U * (i - 1)) & 0xff));
      }
    }
  }
}

// Fixed string packing with correct boundary handling
template <>
inline void packer::pack_type(const std::string& value) {
  if (value.size() <= 31) {
    serialized_object_.emplace_back(static_cast<uint8_t>(value.size()) | 0b10100000);
  } else if (value.size() <= 255) {
    serialized_object_.emplace_back(str8);
    serialized_object_.emplace_back(static_cast<uint8_t>(value.size()));
  } else if (value.size() <= std::numeric_limits<uint16_t>::max()) {
    serialized_object_.emplace_back(str16);
    for (auto i = sizeof(uint16_t); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value.size() >> (8U * (i - 1)) & 0xff));
    }
  } else if (value.size() <= std::numeric_limits<uint32_t>::max()) {
    serialized_object_.emplace_back(str32);
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value.size() >> (8U * (i - 1)) & 0xff));
    }
  } else {
    return; // Give up if string is too long
  }
  for (char i : value) {
    serialized_object_.emplace_back(static_cast<uint8_t>(i));
  }
}

// Fixed binary data packing
template <>
inline void packer::pack_type(const std::vector<uint8_t>& value) {
  if (value.size() <= 255) {
    serialized_object_.emplace_back(bin8);
    serialized_object_.emplace_back(static_cast<uint8_t>(value.size()));
  } else if (value.size() <= std::numeric_limits<uint16_t>::max()) {
    serialized_object_.emplace_back(bin16);
    for (auto i = sizeof(uint16_t); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value.size() >> (8U * (i - 1)) & 0xff));
    }
  } else if (value.size() <= std::numeric_limits<uint32_t>::max()) {
    serialized_object_.emplace_back(bin32);
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      serialized_object_.emplace_back(static_cast<uint8_t>(value.size() >> (8U * (i - 1)) & 0xff));
    }
  } else {
    return; // Give up if vector is too large
  }
  for (const auto& elem : value) {
    serialized_object_.emplace_back(elem);
  }
}

// Extension type packing
template <>
inline void packer::pack_type(const extension& ext) {
  pack_extension(ext.type, ext.data);
}

class unpacker {
public:
  unpacker()
    : data_pointer_(nullptr)
    , data_end_(nullptr) {};

  unpacker(const uint8_t* data_start, std::size_t bytes)
    : data_pointer_(data_start)
    , data_end_(data_start + bytes) {};

  template <class... Types>
  void operator()(Types&... args) {
    (unpack_type(std::forward<Types&>(args)), ...);
  }

  template <class... Types>
  void process(Types&... args) {
    (unpack_type(std::forward<Types&>(args)), ...);
  }

  void set_data(const uint8_t* pointer, std::size_t size) {
    data_pointer_ = pointer;
    data_end_     = data_pointer_ + size;
    ec.clear();
  }

  std::error_code ec{};

private:
  const uint8_t* data_pointer_;
  const uint8_t* data_end_;

  uint8_t safe_data() {
    if (data_pointer_ < data_end_)
      return *data_pointer_;
    ec = unpacker_error::out_of_range;
    return 0;
  }

  void safe_increment(int64_t bytes = 1) {
    if (data_end_ - data_pointer_ >= bytes) {
      data_pointer_ += bytes;
    } else {
      ec = unpacker_error::out_of_range;
    }
  }

  bool check_remaining(size_t needed) {
    if (data_end_ - data_pointer_ >= static_cast<ptrdiff_t>(needed)) {
      return true;
    }
    ec = unpacker_error::out_of_range;
    return false;
  }

  template <class T>
  void unpack_type(T& value) {
    if constexpr (is_map<T>::value) {
      unpack_map(value);
    } else if constexpr (is_container<T>::value) {
      unpack_array(value);
    } else if constexpr (is_std_array<T>::value) {
      unpack_std_array(value);
    } else {
      auto recursive_data = std::vector<uint8_t>{};
      unpack_type(recursive_data);

      auto recursive_unpacker = unpacker{recursive_data.data(), recursive_data.size()};
      value.pack(recursive_unpacker);
      ec = recursive_unpacker.ec;
    }
  }

  // Enhanced timestamp unpacking
  template <class Clock, class Duration>
  void unpack_type(std::chrono::time_point<Clock, Duration>& value) {
    extension ext;
    unpack_type(ext);
    if (ec)
      return;

    if (ext.type != TIMESTAMP_EXT_TYPE) {
      ec = unpacker_error::type_mismatch;
      return;
    }

    int64_t seconds     = 0;
    int64_t nanoseconds = 0;

    if (ext.data.size() == 4) {
      // 32-bit timestamp
      seconds = (static_cast<uint32_t>(ext.data[0]) << 24) | (static_cast<uint32_t>(ext.data[1]) << 16) | (static_cast<uint32_t>(ext.data[2]) << 8) | static_cast<uint32_t>(ext.data[3]);
    } else if (ext.data.size() == 8) {
      // 64-bit timestamp
      uint64_t timestamp64 = 0;
      for (size_t i = 0; i < 8; ++i) {
        timestamp64 = (timestamp64 << 8) | ext.data[i];
      }
      nanoseconds = timestamp64 >> 34;
      seconds     = timestamp64 & 0x3FFFFFFFF;
    } else if (ext.data.size() == 12) {
      // 96-bit timestamp
      nanoseconds = (static_cast<uint32_t>(ext.data[0]) << 24) | (static_cast<uint32_t>(ext.data[1]) << 16) | (static_cast<uint32_t>(ext.data[2]) << 8) | static_cast<uint32_t>(ext.data[3]);

      seconds = 0;
      for (size_t i = 4; i < 12; ++i) {
        seconds = (seconds << 8) | ext.data[i];
      }
    } else {
      ec = unpacker_error::corrupted_data;
      return;
    }

    auto duration = std::chrono::seconds(seconds) + std::chrono::nanoseconds(nanoseconds);
    value         = std::chrono::time_point<Clock, Duration>(std::chrono::duration_cast<Duration>(duration));
  }

  // Extension type unpacking
  void unpack_extension(extension& ext) {
    uint8_t format = safe_data();
    if (ec)
      return;

    size_t data_size = 0;

    switch (format) {
      case fixext1:
        data_size = 1;
        safe_increment();
        break;
      case fixext2:
        data_size = 2;
        safe_increment();
        break;
      case fixext4:
        data_size = 4;
        safe_increment();
        break;
      case fixext8:
        data_size = 8;
        safe_increment();
        break;
      case fixext16:
        data_size = 16;
        safe_increment();
        break;
      case ext8:
        safe_increment();
        data_size = safe_data();
        safe_increment();
        break;
      case ext16:
        safe_increment();
        if (!check_remaining(2))
          return;
        data_size = (static_cast<uint16_t>(safe_data()) << 8);
        safe_increment();
        data_size |= safe_data();
        safe_increment();
        break;
      case ext32:
        safe_increment();
        if (!check_remaining(4))
          return;
        data_size = (static_cast<uint32_t>(safe_data()) << 24);
        safe_increment();
        data_size |= (static_cast<uint32_t>(safe_data()) << 16);
        safe_increment();
        data_size |= (static_cast<uint32_t>(safe_data()) << 8);
        safe_increment();
        data_size |= safe_data();
        safe_increment();
        break;
      default:
        ec = unpacker_error::invalid_format;
        return;
    }

    if (!check_remaining(1 + data_size))
      return;

    ext.type = static_cast<int8_t>(safe_data());
    safe_increment();

    ext.data.resize(data_size);
    for (size_t i = 0; i < data_size; ++i) {
      ext.data[i] = safe_data();
      safe_increment();
    }
  }

  template <class T>
  void unpack_array(T& array) {
    using value_type   = typename T::value_type;
    uint8_t format     = safe_data();
    size_t  array_size = 0;

    if (format == array32) {
      safe_increment();
      if (!check_remaining(4))
        return;
      for (auto i = sizeof(uint32_t); i > 0; --i) {
        array_size |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
        safe_increment();
      }
    } else if (format == array16) {
      safe_increment();
      if (!check_remaining(2))
        return;
      for (auto i = sizeof(uint16_t); i > 0; --i) {
        array_size |= static_cast<uint16_t>(safe_data()) << (8 * (i - 1));
        safe_increment();
      }
    } else if ((format & 0xf0) == 0x90) {
      array_size = format & 0x0f;
      safe_increment();
    } else {
      ec = unpacker_error::type_mismatch;
      return;
    }

    array.clear();
    for (size_t i = 0; i < array_size && !ec; ++i) {
      value_type val{};
      unpack_type(val);
      if constexpr (std::is_same_v<T, std::forward_list<value_type>>) {
        array.push_front(std::move(val));
      } else {
        array.emplace_back(std::move(val));
      }
    }

    // Reverse forward_list since we pushed to front
    if constexpr (std::is_same_v<T, std::forward_list<value_type>>) {
      array.reverse();
    }
  }

  template <class T>
  void unpack_std_array(T& array) {
    using value_type = typename T::value_type;
    auto vec         = std::vector<value_type>{};
    unpack_array(vec);
    if (ec)
      return;

    if (vec.size() != array.size()) {
      ec = unpacker_error::type_mismatch;
      return;
    }

    std::copy(vec.begin(), vec.end(), array.begin());
  }

  template <class T>
  void unpack_map(T& map) {
    using key_type    = typename T::key_type;
    using mapped_type = typename T::mapped_type;

    uint8_t format   = safe_data();
    size_t  map_size = 0;

    if (format == map32) {
      safe_increment();
      if (!check_remaining(4))
        return;
      for (auto i = sizeof(uint32_t); i > 0; --i) {
        map_size |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
        safe_increment();
      }
    } else if (format == map16) {
      safe_increment();
      if (!check_remaining(2))
        return;
      for (auto i = sizeof(uint16_t); i > 0; --i) {
        map_size |= static_cast<uint16_t>(safe_data()) << (8 * (i - 1));
        safe_increment();
      }
    } else if ((format & 0xf0) == 0x80) {
      map_size = format & 0x0f;
      safe_increment();
    } else {
      ec = unpacker_error::type_mismatch;
      return;
    }

    map.clear();
    for (size_t i = 0; i < map_size && !ec; ++i) {
      key_type    key{};
      mapped_type value{};
      unpack_type(key);
      unpack_type(value);
      if constexpr (std::is_same_v<T, std::map<key_type, mapped_type>> || std::is_same_v<T, std::unordered_map<key_type, mapped_type>>) {
        map.insert_or_assign(std::move(key), std::move(value));
      } else {
        map.insert({std::move(key), std::move(value)});
      }
    }
  }

  // Safe float from bits conversion
  float bits_to_float(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  double bits_to_double(uint64_t bits) {
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
};

// Enhanced integer unpacking with better error handling
template <>
inline void unpacker::unpack_type(int8_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == int8) {
    safe_increment();
    if (!check_remaining(1))
      return;
    value = static_cast<int8_t>(safe_data());
    safe_increment();
  } else if ((format & 0x80) == 0x00) {
    // positive fixint
    value = static_cast<int8_t>(format);
    safe_increment();
  } else if ((format & 0xe0) == 0xe0) {
    // negative fixint
    value = static_cast<int8_t>(format);
    safe_increment();
  } else {
    ec = unpacker_error::type_mismatch;
  }
}

template <>
inline void unpacker::unpack_type(int16_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == int16) {
    safe_increment();
    if (!check_remaining(2))
      return;
    uint16_t bits = 0;
    for (auto i = sizeof(uint16_t); i > 0; --i) {
      bits |= static_cast<uint16_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
    value = static_cast<int16_t>(bits);
  } else {
    int8_t val;
    unpack_type(val);
    value = val;
  }
}

template <>
inline void unpacker::unpack_type(int32_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == int32) {
    safe_increment();
    if (!check_remaining(4))
      return;
    uint32_t bits = 0;
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      bits |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
    value = static_cast<int32_t>(bits);
  } else {
    int16_t val;
    unpack_type(val);
    value = val;
  }
}

template <>
inline void unpacker::unpack_type(int64_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == int64) {
    safe_increment();
    if (!check_remaining(8))
      return;
    uint64_t bits = 0;
    for (auto i = sizeof(uint64_t); i > 0; --i) {
      bits |= static_cast<uint64_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
    value = static_cast<int64_t>(bits);
  } else {
    int32_t val;
    unpack_type(val);
    value = val;
  }
}

template <>
inline void unpacker::unpack_type(uint8_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == uint8) {
    safe_increment();
    if (!check_remaining(1))
      return;
    value = safe_data();
    safe_increment();
  } else if ((format & 0x80) == 0x00) {
    // positive fixint
    value = format;
    safe_increment();
  } else {
    ec = unpacker_error::type_mismatch;
  }
}

template <>
inline void unpacker::unpack_type(uint16_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == uint16) {
    safe_increment();
    if (!check_remaining(2))
      return;
    value = 0;
    for (auto i = sizeof(uint16_t); i > 0; --i) {
      value |= static_cast<uint16_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else {
    uint8_t val;
    unpack_type(val);
    value = val;
  }
}

template <>
inline void unpacker::unpack_type(uint32_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == uint32) {
    safe_increment();
    if (!check_remaining(4))
      return;
    value = 0;
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      value |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else {
    uint16_t val;
    unpack_type(val);
    value = val;
  }
}

template <>
inline void unpacker::unpack_type(uint64_t& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == uint64) {
    safe_increment();
    if (!check_remaining(8))
      return;
    value = 0;
    for (auto i = sizeof(uint64_t); i > 0; --i) {
      value |= static_cast<uint64_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else {
    uint32_t val;
    unpack_type(val);
    value = val;
  }
}

template <>
inline void unpacker::unpack_type(std::nullptr_t& /*value*/) {
  uint8_t format = safe_data();
  if (format == nil) {
    safe_increment();
  } else {
    ec = unpacker_error::type_mismatch;
  }
}

template <>
inline void unpacker::unpack_type(bool& value) {
  uint8_t format = safe_data();
  if (format == true_bool) {
    value = true;
    safe_increment();
  } else if (format == false_bool) {
    value = false;
    safe_increment();
  } else {
    ec = unpacker_error::type_mismatch;
  }
}

// Fixed float/double unpacking with safe bit conversion
template <>
inline void unpacker::unpack_type(float& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == float32) {
    safe_increment();
    if (!check_remaining(4))
      return;
    uint32_t data = 0;
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      data |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
    value = bits_to_float(data);
  } else {
    // Try to unpack as integer first
    if ((format & 0x80) == 0x00 || (format & 0xe0) == 0xe0 || format == int8 || format == int16 || format == int32 || format == int64) {
      int64_t val = 0;
      unpack_type(val);
      value = static_cast<float>(val);
    } else if (format == uint8 || format == uint16 || format == uint32 || format == uint64) {
      uint64_t val = 0;
      unpack_type(val);
      value = static_cast<float>(val);
    } else {
      ec = unpacker_error::type_mismatch;
    }
  }
}

template <>
inline void unpacker::unpack_type(double& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  if (format == float64) {
    safe_increment();
    if (!check_remaining(8))
      return;
    uint64_t data = 0;
    for (auto i = sizeof(uint64_t); i > 0; --i) {
      data |= static_cast<uint64_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
    value = bits_to_double(data);
  } else if (format == float32) {
    float val;
    unpack_type(val);
    value = static_cast<double>(val);
  } else {
    // Try to unpack as integer first
    if ((format & 0x80) == 0x00 || (format & 0xe0) == 0xe0 || format == int8 || format == int16 || format == int32 || format == int64) {
      int64_t val = 0;
      unpack_type(val);
      value = static_cast<double>(val);
    } else if (format == uint8 || format == uint16 || format == uint32 || format == uint64) {
      uint64_t val = 0;
      unpack_type(val);
      value = static_cast<double>(val);
    } else {
      ec = unpacker_error::type_mismatch;
    }
  }
}

template <>
inline void unpacker::unpack_type(std::string& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  size_t str_size = 0;

  if (format == str32) {
    safe_increment();
    if (!check_remaining(4))
      return;
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      str_size |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else if (format == str16) {
    safe_increment();
    if (!check_remaining(2))
      return;
    for (auto i = sizeof(uint16_t); i > 0; --i) {
      str_size |= static_cast<uint16_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else if (format == str8) {
    safe_increment();
    if (!check_remaining(1))
      return;
    str_size = safe_data();
    safe_increment();
  } else if ((format & 0xe0) == 0xa0) {
    str_size = format & 0x1f;
    safe_increment();
  } else {
    ec = unpacker_error::type_mismatch;
    return;
  }

  if (!check_remaining(str_size))
    return;

  value = std::string{reinterpret_cast<const char*>(data_pointer_), str_size};
  safe_increment(str_size);
}

template <>
inline void unpacker::unpack_type(std::vector<uint8_t>& value) {
  uint8_t format = safe_data();
  if (ec)
    return;

  size_t bin_size = 0;

  if (format == bin32) {
    safe_increment();
    if (!check_remaining(4))
      return;
    for (auto i = sizeof(uint32_t); i > 0; --i) {
      bin_size |= static_cast<uint32_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else if (format == bin16) {
    safe_increment();
    if (!check_remaining(2))
      return;
    for (auto i = sizeof(uint16_t); i > 0; --i) {
      bin_size |= static_cast<uint16_t>(safe_data()) << (8 * (i - 1));
      safe_increment();
    }
  } else if (format == bin8) {
    safe_increment();
    if (!check_remaining(1))
      return;
    bin_size = safe_data();
    safe_increment();
  } else {
    ec = unpacker_error::type_mismatch;
    return;
  }

  if (!check_remaining(bin_size))
    return;

  value = std::vector<uint8_t>{data_pointer_, data_pointer_ + bin_size};
  safe_increment(bin_size);
}

template <>
inline void unpacker::unpack_type(extension& ext) {
  unpack_extension(ext);
}

template <class packable_object>
std::vector<uint8_t> pack(packable_object& obj) {
  auto packer_instance = packer{};
  obj.pack(packer_instance);
  return packer_instance.vector();
}

template <class packable_object>
std::vector<uint8_t> pack(packable_object&& obj) {
  auto packer_instance = packer{};
  obj.pack(packer_instance);
  return packer_instance.vector();
}

template <class unpackable_object>
unpackable_object unpack(const uint8_t* data_start, const std::size_t size, std::error_code& ec) {
  auto obj               = unpackable_object{};
  auto unpacker_instance = unpacker(data_start, size);
  obj.pack(unpacker_instance);
  ec = unpacker_instance.ec;
  return obj;
}

template <class unpackable_object>
unpackable_object unpack(const uint8_t* data_start, const std::size_t size) {
  std::error_code ec{};
  return unpack<unpackable_object>(data_start, size, ec);
}

template <class unpackable_object>
unpackable_object unpack(const std::vector<uint8_t>& data, std::error_code& ec) {
  return unpack<unpackable_object>(data.data(), data.size(), ec);
}

template <class unpackable_object>
unpackable_object unpack(const std::vector<uint8_t>& data) {
  std::error_code ec;
  return unpack<unpackable_object>(data.data(), data.size(), ec);
}
} // namespace msgpack

#endif // CPPACK_PACKER_HPP
