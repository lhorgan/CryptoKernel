#ifndef RAFT_H_INCLUDED
#define RAFT_H_INCLUDED

#include <chrono>

#include "../blockchain.h"

namespace CryptoKernel {
    class Consensus::Raft : public Raft {
    public:
        Raft();

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
    
    private:
        bool running;
        bool leader;
        unsigned long long lastPing;
        unsigned long long electionTimeout;
    };
}

#endif // RAFT_H_INCLUDED