#include <random>

#include "crypto.h"
#include "wallet.h"

Wallet::Wallet() {
    log = new Log("erc20.log", true);

    const string blockchainDir = "erc20/blockchain";
    blockchain = new Chain(log, blockchainDir);

    const string networkDir = "erc20/network";
    const unsigned int networkPort = 9823;
    network = new Network(log, blockchain, networkPort, networkDir);
}

bool Wallet::transfer(const std::string& pubKey, uint64_t value) {
    // set up the uniform random distribution
    const time_t t = std::time(0);
    const uint64_t now = static_cast<uint64_t> (t);;
    default_random_engine generator(now);
    uniform_int_distribution<unsigned int> distribution(0, UINT_MAX);
    const uint64_t nonce = distribution(generator);

    Json::Value data;
    data["publicKey"] = pubKey;

    const CryptoKernel::Blockchain::output toThem = CryptoKernel::Blockchain::output(50, distribution(generator), data);
    vector<CryptoKernel::Blockchain::transaction> transactions;
    //transactions.push_back(output);
    network->broadcastTransactions(transactions);
}