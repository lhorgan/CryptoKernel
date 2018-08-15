#include "chain.h"

Chain::Chain(Log* log, string blockchainDir) : Blockchain(log, blockchainDir) {};

uint64_t Chain::getBlockReward(const uint64_t height) {
    return 1;
}

string Chain::getCoinbaseOwner(const string& publicKey) {
    return "BDgkCUSogr3PrCJtY5P0wNsxfMF47I4ZGnJp/nn/CT4SerSibE0MKptXciR4zZCrpNB85xychJB4EURSR2TpYUs=";
}