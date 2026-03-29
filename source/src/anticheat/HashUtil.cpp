#include "cube.h"
#include "HashUtil.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace PhantiCheat {

  class FastHash {
  public:
    FastHash() : hash(2166136261u) { }

    void update(const unsigned char* data, size_t length) {
      for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 16777619u;
      }
    }

    void update(std::istream& is) {
      char buffer[4096];
      while (is.read(buffer, sizeof(buffer))) {
        update(reinterpret_cast<const unsigned char*>(buffer), is.gcount());
      }
      update(reinterpret_cast<const unsigned char*>(buffer), is.gcount());
    }

    std::string final() {
      std::ostringstream result;
      result << std::hex << std::setw(8) << std::setfill('0') << hash;
      return result.str();
    }
  private:
    uint32_t hash;
  };

  std::string HashUtil::calculateHash(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
      throw std::runtime_error("Unable to open file: " + filePath);
    }
    FastHash hasher;
    hasher.update(file);
    return hasher.final();
  }

  std::string HashUtil::calculateHash(const unsigned char* data, size_t length) {
    FastHash hasher;
    hasher.update(data, length);
    return hasher.final();
  }
}
