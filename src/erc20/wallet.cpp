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

}