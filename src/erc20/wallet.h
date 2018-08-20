#ifndef ERC20WALLET
#define ERC20WALLET

#include <string>
#include <cstdint>

#include "chain.h"
#include "network.h"

using namespace CryptoKernel;
using namespace std;

class ERC20Wallet {
public:
    ERC20Wallet();
    ~ERC20Wallet();
    bool transfer(const string& pubKey, uint64_t value);
    std::vector<CryptoKernel::Blockchain::output> findUtxosToSpend(uint64_t value);
    void monitorBlockchain();
    void processBlock(CryptoKernel::Blockchain::block& block);
    void processTransaction(CryptoKernel::Blockchain::transaction& transaction);
    void sendFunc();
    
private:
    Blockchain* blockchain;
    Network* network;
    Log* log;
    string publicKey;
    string privKey;

    uint64_t tipHeight;
    BigNum tipId;
    ConcurrentMap<string, CryptoKernel::Blockchain::output> utxos;

    unique_ptr<thread> monitorThread;
    unique_ptr<thread> sendThread;

    leveldb::DB* statsDB;
    leveldb::DB* utxoDB;
};

#endif