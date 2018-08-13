#include "chain.h"

Chain::Chain(Log* log, string blockchainDir) : Blockchain(log, blockchainDir) {};

uint64_t Chain::getBlockReward(const uint64_t height) {
    return 0;
}

string Chain::getCoinbaseOwner(const string& publicKey) {
    return "hello";
}