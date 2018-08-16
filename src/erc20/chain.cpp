#include "chain.h"

Chain::Chain(Log* log, string blockchainDir) : Blockchain(log, blockchainDir) {};

uint64_t Chain::getBlockReward(const uint64_t height) {
    return 100;
}

string Chain::getCoinbaseOwner(const string& publicKey) {
    return "033c74108bd7a1395c362ee8cf9829bef26c616b2a3bcb5e7b0fb351f5dc22f071";
}