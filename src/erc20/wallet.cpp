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

    blockchain->loadChain(consensus, "alice.json");

    const string networkDir = "erc20/network";
    const unsigned int networkPort = 9823;
    network = new Network(log, blockchain, networkPort, networkDir);

    // open the wallet database
    const string walletDir = "erc20/wallet";
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, walletDir + "/stats", &statsDB);
    assert(status.ok());
    status = leveldb::DB::Open(options, walletDir + "/utxos", &utxoDB);
    assert(status.ok());

    tipHeight = 1;
    tipId = blockchain->getBlockByHeight(1).getId();

    monitorThread.reset(new thread(&ERC20Wallet::monitorBlockchain, this));
    sendThread.reset(new thread(&ERC20Wallet::sendFunc, this));

    consensus->start();
}

ERC20Wallet::~ERC20Wallet() {
    log->printf(LOG_LEVEL_INFO, "cleaning up");
    monitorThread->join();
    sendThread->join();

    delete statsDB;
    delete utxoDB;
}

void ERC20Wallet::sendFunc() {
    while(true) {
        log->printf(LOG_LEVEL_INFO, "sending....");
        transfer(G_OTHER_PUBLIC_KEY, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(30000));
    }
}

/**
 * Send 'value' to 'pubKey'
 */
bool ERC20Wallet::transfer(const std::string& pubKey, uint64_t value) {
    const time_t t = std::time(0);
    const uint64_t now = static_cast<uint64_t> (t);;
    default_random_engine generator(now);
    uniform_int_distribution<unsigned int> distribution(0, UINT_MAX);
    const uint64_t nonce = distribution(generator);
    
    std::set<CryptoKernel::Blockchain::output> outputsToSpend;
    uint64_t acc = 0;

    leveldb::Iterator* it = utxoDB->NewIterator(leveldb::ReadOptions());
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        if(acc < value) {
            CryptoKernel::Blockchain::output output(Json::Value(it->value().ToString()));
            acc +=output.getValue();
            outputsToSpend.insert(output);
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    if(acc >= value) {
        Crypto crypto;
        crypto.setPublicKey(pubKey);
        crypto.setPrivateKey(privKey);

        set<CryptoKernel::Blockchain::output> outputs;
        set<CryptoKernel::Blockchain::input> inputs;
        
        Json::Value data;
        data["publicKey"] = pubKey;
        const CryptoKernel::Blockchain::output toThem = CryptoKernel::Blockchain::output(value, distribution(generator), data);
        data.clear();
        data["publicKey"] = this->publicKey;
        const CryptoKernel::Blockchain::output change = CryptoKernel::Blockchain::output(acc - value, distribution(generator), data);
        outputs.insert(toThem);
        outputs.insert(change);

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
        log->printf(LOG_LEVEL_INFO, "Broadcasting transaction...");
        network->broadcastTransactions(transactions);

        return true;
    }

    log->printf(LOG_LEVEL_INFO, "Insufficient funds for transaction.");
    return false;
}

/**
 *  Top level function for watching the blockchain for blocks that might contain transactions
 *  with money for us!
 */
void ERC20Wallet::monitorBlockchain() {
    while(true) {
        // well now, first thing's first, has the chain split?
        if(blockchain->getBlockByHeight(tipHeight).getId() != tipId) {
            log->printf(LOG_LEVEL_INFO, "Woops, looks like there's been a fork.");
            tipHeight = 1;
            tipId = blockchain->getBlockByHeight(1).getId();
            
            leveldb::Iterator* it = utxoDB->NewIterator(leveldb::ReadOptions());
            for(it->SeekToFirst(); it->Valid(); it->Next()) {
                utxoDB->Delete(leveldb::WriteOptions(), it->key().ToString());
            }
        }

        //log->printf(LOG_LEVEL_INFO, "Syncing from block " + std::to_string(tipHeight) + " to " + std::to_string(network->getCurrentHeight()));
        for(int i = tipHeight + 1; i <= network->getCurrentHeight(); i++) {
            CryptoKernel::Blockchain::block block = blockchain->getBlockByHeight(i);
            processBlock(block);
            tipHeight = i;
            tipId = block.getId();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

/**
 * Figure out if a specific block contains money for us
 */
void ERC20Wallet::processBlock(CryptoKernel::Blockchain::block& block) {
    std::set<CryptoKernel::Blockchain::transaction> transactions = block.getTransactions();
    transactions.insert(block.getCoinbaseTx());
    for(auto transaction : transactions) {
        processTransaction(transaction);
    }
}

/**
 * Grr
 */
void ERC20Wallet::processTransaction(CryptoKernel::Blockchain::transaction& transaction) {
    log->printf(LOG_LEVEL_INFO, "Processing transaction...");

    std::set<CryptoKernel::Blockchain::output> outputs = transaction.getOutputs();
    std::set<CryptoKernel::Blockchain::input> inputs = transaction.getInputs();

    for(auto input : inputs) {
        string inputStr;
        leveldb::Status status = utxoDB->Get(leveldb::ReadOptions(), input.getOutputId().toString(), &inputStr);
        if(status.ok()) {
            utxoDB->Delete(leveldb::WriteOptions(), input.getOutputId().toString());
        } 
    }

    for(auto output : outputs) {
        // Check if there is a publicKey that belongs to you
        if(output.getData()["publicKey"].isString()) {
            string outputPubKey = output.getData()["publicKey"].asString();
            if(outputPubKey == publicKey) {
                log->printf(LOG_LEVEL_INFO, "This output belongs to us!");
                utxoDB->Put(leveldb::WriteOptions(), output.getId().toString(), output.toJson().toStyledString());
            }
        }
        else {
            log->printf(LOG_LEVEL_INFO, "nope, not ours (" + output.getData()["publicKey"].asString() + " != " + publicKey + ")");
        }
    }
}