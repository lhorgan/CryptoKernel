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

/**
 * Send 'value' to 'pubKey' 
 */
bool Wallet::transfer(const std::string& pubKey, uint64_t value) {
    // set up the uniform random distribution
    const time_t t = std::time(0);
    const uint64_t now = static_cast<uint64_t> (t);;
    default_random_engine generator(now);
    uniform_int_distribution<unsigned int> distribution(0, UINT_MAX);
    const uint64_t nonce = distribution(generator);

    vector<CryptoKernel::Blockchain::dbOutput> outputsToSpend = findUtxosToSpend(value);
    if(outputsToSpend.size() < value) {
        log->printf(LOG_LEVEL_INFO, "Couldn't complete transfer, insufficient funds!");
        return false;
    } 
    
    Crypto crypto;
    crypto.setPublicKey(pubKey);
    crypto.setPrivateKey(privKey);

    set<CryptoKernel::Blockchain::output> outputs;
    set<CryptoKernel::Blockchain::input> inputs;

    for(int i = 0; i < value; i++) {
        Json::Value data;
        data["publicKey"] = pubKey;
        const CryptoKernel::Blockchain::output toThem = CryptoKernel::Blockchain::output(1, distribution(generator), data);
        outputs.insert(toThem);
    }
    const std::string outputHash = CryptoKernel::Blockchain::transaction::getOutputSetId(outputs).toString();

    for(auto output : outputsToSpend) {
        string outputPubKey = output.getData()["publicKey"].asString();

        string signature = crypto.sign(output.getId().toString() + outputHash);
        Json::Value data;
        data["signature"] = signature;

        CryptoKernel::Blockchain::input input(output.getId(), data);
        inputs.insert(input);
    }

    const CryptoKernel::Blockchain::transaction transaction = CryptoKernel::Blockchain::transaction(inputs, outputs, now);
    vector<CryptoKernel::Blockchain::transaction> transactions;
    transactions.push_back(transaction);
    network->broadcastTransactions(transactions);
}

/**
 * Find a set of (our own personal) UTXOs to spend to cover a transfer 
*/
std::vector<CryptoKernel::Blockchain::dbOutput> Wallet::findUtxosToSpend(uint64_t value) {
    std::set<CryptoKernel::Blockchain::dbOutput> outputs = blockchain->getUnspentOutputs(publicKey);

    std::vector<CryptoKernel::Blockchain::dbOutput> outputsToSpend;
    for(auto output : outputs) {
        if(outputsToSpend.size() < value) {
            outputsToSpend.push_back(output);
        }
        else {
            break;
        }
    }

    return outputsToSpend;
}

/**
 *  Top level function for watching the blockchain for blocks that might contain transactions
 *  with money for us!
 */
void Wallet::monitorBlockchain() {

}

/**
 * Figure out if a specific block contains money for us
 */
void Wallet::processBlock() {

}

/**
 * Hmmmm.... wonder what this does.
 * (okay, okay.  fine.  it mines)
 */
void Wallet::mine() {

}