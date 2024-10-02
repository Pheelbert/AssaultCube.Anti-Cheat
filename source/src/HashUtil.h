#ifndef HASHUTIL_H
#define HASHUTIL_H

#include <string>

namespace PhantiCheat {

    class HashUtil {
    public:
        static std::string calculateHash(const std::string& filePath);
        static std::string calculateHash(const unsigned char* data, size_t length);
    };

}

#endif // HASHUTIL_H
