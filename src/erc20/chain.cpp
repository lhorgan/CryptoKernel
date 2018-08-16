#include "chain.h"

Chain::Chain(Log* log, string blockchainDir) : Blockchain(log, blockchainDir) {};

uint64_t Chain::getBlockReward(const uint64_t height) {
    return 1;
}

string Chain::getCoinbaseOwner(const string& publicKey) {
    return "BNEOpZ81Oo42ISQEhwid8hvQogv42vTTP7BonJMaEG00dPPO7qjz6HpcKO7d9dM4UkvpvsSI0SbCk+c73hGjDjs=";
}