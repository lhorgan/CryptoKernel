#include <string>

#include "blockchain.h"

using namespace CryptoKernel;
using namespace std;

class Chain : public Blockchain {
public:
    Chain(Log* log, string blockchainDir);

    virtual uint64_t getBlockReward(const uint64_t height);
    virtual string getCoinbaseOwner(const string& publicKey);
};