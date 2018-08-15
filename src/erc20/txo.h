#ifndef ERC20TXO
#define ERC20TXO

#include <string>

#include "network.h"

class Txo {
public:
    Txo(const std::string& id, const uint64_t value);
    Txo(const Json::Value& txoJson);

    Json::Value toJson() const;

    bool isSpent() const;
    void spend();

    uint64_t getValue() const;

private:
    std::string id;
    bool spent;
    uint64_t value;
};

#endif