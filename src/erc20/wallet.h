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
    std::vector<CryptoKernel::Blockchain::dbOutput> findUtxosToSpend(uint64_t value);
    void monitorBlockchain();
    void processBlock();
    void mine();

private:
    Blockchain* blockchain;
    Network* network;
    Log* log;
    string publicKey;
    string privKey;
};