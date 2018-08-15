#include <random>

#include "crypto.h"
#include "wallet.h"
#include "constants.h"
#include "consensus/PoW.h"

ERC20Wallet::ERC20Wallet() {
    log = new Log("erc20.log", true);

    publicKey = G_PUBLIC_KEY;

    const string blockchainDir = "erc20/blockchain";
    blockchain = new Chain(log, blockchainDir);
    
    Consensus* consensus = new Consensus::PoW::KGW_LYRA2REV2(5, blockchain, true, publicKey);

    blockchain->loadChain(consensus, "hello");

    const string networkDir = "erc20/network";
    const unsigned int networkPort = 9823;
    network = new Network(log, blockchain, networkPort, networkDir);

    monitorThread.reset(new thread(&ERC20Wallet::monitorBlockchain, this));
    sendThread.reset(new thread(&ERC20Wallet::sendFunc, this));
}

void ERC20Wallet::sendFunc() {
    while(true) {
        transfer(G_OTHER_PUB_KEY, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    }
}

/**
 * Send 'value' to 'pubKey' 
 */
bool ERC20Wallet::transfer(const std::string& pubKey, uint64_t value) {
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
std::vector<CryptoKernel::Blockchain::dbOutput> ERC20Wallet::findUtxosToSpend(uint64_t value) {
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
void ERC20Wallet::monitorBlockchain() {
    while(true) {
        for(int i = 2; i < network->getCurrentHeight(); i++) {
            CryptoKernel::Blockchain::block block = blockchain->getBlockByHeight(i);
            processBlock(block);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

/**
 * Figure out if a specific block contains money for us
 */
void ERC20Wallet::processBlock(CryptoKernel::Blockchain::block& block) {
    std::set<CryptoKernel::Blockchain::transaction> transactions = block.getTransactions();
    for(auto transaction : transactions) {
        processTransaction(transaction);
    }
}

/**
 * Grr
 */
void ERC20Wallet::processTransaction(CryptoKernel::Blockchain::transaction& transaction) {
    std::set<CryptoKernel::Blockchain::output> outputs = transaction.getOutputs();
    for(auto output : outputs) {
        // Check if there is a publicKey that belongs to you
        if(output.getData()["publicKey"].isString()) {
            string outputPubKey = output.getData()["publicKey"].asString();
            if(outputPubKey == publicKey) {
                log->printf(LOG_LEVEL_INFO, "This output belongs to us!");
            }
        }
    }
}