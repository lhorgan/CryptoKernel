#include "chain.h"
#include "constants.h"

Chain::Chain(Log* log, string blockchainDir) : Blockchain(log, blockchainDir) {};

uint64_t Chain::getBlockReward(const uint64_t height) {
    return 10000;
}

string Chain::getCoinbaseOwner(const string& publicKey) {
    return G_PUBLIC_KEY;
}