#ifndef RAFT_H_INCLUDED
#define RAFT_H_INCLUDED

#include <chrono>
#include <thread>

#include "network.h"
#include "RaftNet.h"
#include "../blockchain.h"

namespace CryptoKernel {
    class Consensus::Raft : public Consensus {
    public:
        Raft(CryptoKernel::Blockchain* blockchain, std::string pubKey, CryptoKernel::Log* log);
        ~Raft();

        bool isBlockBetter(Storage::Transaction* transaction,
                               const CryptoKernel::Blockchain::block& block,
                               const CryptoKernel::Blockchain::dbBlock& tip);

        bool checkConsensusRules(Storage::Transaction* transaction,
                                         CryptoKernel::Blockchain::block& block,
                                        const CryptoKernel::Blockchain::dbBlock& previousBlock);

        Json::Value generateConsensusData(Storage::Transaction* transaction,
                const CryptoKernel::BigNum& previousBlockId, const std::string& publicKey);

        // probably always returns true
        bool verifyTransaction(Storage::Transaction* transaction,
                                    const CryptoKernel::Blockchain::transaction& tx);

        // probably always returns true
        bool confirmTransaction(Storage::Transaction* transaction,
                                        const CryptoKernel::Blockchain::transaction& tx);

        // probably always returns true
        bool submitTransaction(Storage::Transaction* transaction,
                                    const CryptoKernel::Blockchain::transaction& tx);

        // probably always returns true
        bool submitBlock(Storage::Transaction* transaction,
                                const CryptoKernel::Blockchain::block& block);
        
        // ha.  get it?  instead of miner.  floater?  ha.
        void floater();
        void start();
        void requestVotes();
        void castVote(std::string candidateId, bool vote);
        void sendAppendEntries();
        void resetValues();
        void handleTermDisparity(int requesterTerm);

        class LifeRaft;
    
    private:
        bool running;
        unsigned long long lastPing;
        unsigned long long electionTimeout;
        CryptoKernel::Blockchain* blockchain;
        std::string pubKey;
        CryptoKernel::Log* log;
        std::unique_ptr<std::thread> floaterThread;

        int networkSize;
        std::set<std::string> supporters;
        bool leader;
        bool candidate;
        unsigned int term;
        CryptoKernel::Network* network;

        std::string votedFor;

        RaftNet* raftNet;

        void sendAll(Json::Value data);
        void processQueue(); // process the incoming message queue

        void handleAppendEntries(Json::Value& data);
        void handleRequestVotes(Json::Value& data);
    };
}

#endif // RAFT_H_INCLUDED