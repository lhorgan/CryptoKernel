#include <string>
#include <cstdint>

#include "chain.h"
#include "network.h"

using namespace CryptoKernel;
using namespace std;

class Wallet {
public:
    Wallet();
    bool transfer(const string& pubKey, uint64_t value);

private:
    Blockchain* blockchain;
    Network* network;
    Log* log;
    string publicKey;
};