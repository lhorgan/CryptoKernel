#include "Raft.h"

CryptoKernel::Consensus::Raft::Raft(CryptoKernel::Blockchain* blockchain, std::string pubKey, CryptoKernel::Log* log) {
    this->blockchain = blockchain;
    this->pubKey = pubKey;
    this->log = log;

    running = true;
    lastPing = 0;
    electionTimeout = 500 + rand() % 500;
    networkSize = 3;
    leader = false;
    candidate = false;
    term = 0;
}

CryptoKernel::Consensus::Raft::~Raft() {
    floaterThread->join();
}

void CryptoKernel::Consensus::Raft::start() {
    floaterThread.reset(new std::thread(&CryptoKernel::Consensus::Raft::floater, this));
}

bool CryptoKernel::Consensus::Raft::checkConsensusRules(Storage::Transaction* transaction,
                                                        CryptoKernel::Blockchain::block& block,
                                                        const CryptoKernel::Blockchain::dbBlock& previousBlock) {
    /*
    if leader {
        if block is log proposal {
            add to log queue
        }
        else if block is append_entries rpc or request_vote rpc from someone with a higher term {
            step down as leader (just stop sending heartbeats)
        }
    }
    else {
        if block comes from leader and term/index of prevBlock match our prevBlock {
            broadcast confirmation of receipt block
            return true
        }
        else {
            broadcast failure of receipt block
            return false
        }
    }
    */

   /*if(this->leader) {

   }
   else {

   }*/
    bool result = false;

    Json::Value data = block.getConsensusData();
    if(data["rpc"]) {
        if(data["rpc"].asString() == "request_votes" && data["direction"].asString() == "sending") {
            int requesterTerm = data["term"].asInt();
            if(requesterTerm >= term) {
                // cast a vote for this node
                term = requesterTerm;
                castVote(data["sender"].asString());
            }
        }
        else if(data["rpc"].asString() == "request_votes" && data["direction"].asString() == "responding") {
            // am I a candidate?
            if(candidate) {
                supporters.insert(data["sender"].asString());
                if(supporters.size() > networkSize / 2) { // we have a simple majority of voters
                    leader = true; // I am the captain now!
                }
            }
            else {
                log->printf(LOG_LEVEL_INFO, "Thanks for the vote, but someone else is already leader.");
            }
        }
        else if(data["rpc"].asString() == "heartbeat") {
            // update last ping
            if(data["sender"].asString() != pubKey) { // I guess it doesn't really matter
                log->printf(LOG_LEVEL_INFO, "I have received a heartbeat.  I have received a heartbeat!");
                lastPing = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
            }
        }
    }

    return result;
}

void CryptoKernel::Consensus::Raft::floater() {
    time_t t = std::time(0);
    uint64_t now = static_cast<uint64_t> (t);

    while(running) {
        // this node is the leader
        if(leader) {
            log->printf(LOG_LEVEL_INFO, "I am the leader.");
            sendHeartbeat();
        }
        else {
            unsigned long long currTime = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
            if(currTime - lastPing < electionTimeout) {
                // everything is fine
                resetValues();
                log->printf(LOG_LEVEL_INFO, "I am a follower.  I got a heartbeat recently.");
            }
            else {
                // time to elect a new leader
                log->printf(LOG_LEVEL_INFO, "I haven't got a leader.  We need to elect a leader!");
                resetValues();
                leader = true;
                supporters.insert(pubKey);
                requestVotes();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

void CryptoKernel::Consensus::Raft::resetValues() {
    supporters.clear();
    leader = false;
    candidate = false;
}

void CryptoKernel::Consensus::Raft::requestVotes() {
    CryptoKernel::Blockchain::block dummyBlock = blockchain->generateVerifyingBlock(pubKey);
    Json::Value dummyData;
    dummyData["rpc"] = "request_votes";
    dummyData["direction"] = "sending";
    dummyData["sender"] = pubKey;
    dummyData["term"] = ++term;
    dummyBlock.setConsensusData(dummyData);
    blockchain->submitBlock(dummyBlock);
}

void CryptoKernel::Consensus::Raft::castVote(std::string candidateId) {
    CryptoKernel::Blockchain::block dummyBlock = blockchain->generateVerifyingBlock(pubKey);
    Json::Value dummyData;
    dummyData["rpc"] = "request_votes";
    dummyData["direction"] = "responding";
    dummyData["sender"] = pubKey;
    dummyData["term"] = term;
    dummyData["candidate"] = candidateId;
    dummyBlock.setConsensusData(dummyData);
    blockchain->submitBlock(dummyBlock);
}

void CryptoKernel::Consensus::Raft::sendHeartbeat() {
    CryptoKernel::Blockchain::block dummyBlock = blockchain->generateVerifyingBlock(pubKey);
    Json::Value dummyData;
    dummyData["rpc"] = "heartbeat"; // paper uses an empty append_entries, but this is easier
    dummyData["sender"] = pubKey;
    blockchain->submitBlock(dummyBlock);
}








bool CryptoKernel::Consensus::Raft::isBlockBetter(Storage::Transaction* transaction,
                               const CryptoKernel::Blockchain::block& block,
                               const CryptoKernel::Blockchain::dbBlock& tip) {
                                   return true;
                               }

Json::Value CryptoKernel::Consensus::Raft::generateConsensusData(Storage::Transaction* transaction,
        const CryptoKernel::BigNum& previousBlockId, const std::string& publicKey) {
            Json::Value val;
            return val;
        }

// probably always returns true
bool CryptoKernel::Consensus::Raft::verifyTransaction(Storage::Transaction* transaction,
                            const CryptoKernel::Blockchain::transaction& tx) {
                                return true;
                            }

// probably always returns true
bool CryptoKernel::Consensus::Raft::confirmTransaction(Storage::Transaction* transaction,
                                const CryptoKernel::Blockchain::transaction& tx) {
                                    return true;
                                }

// probably always returns true
bool CryptoKernel::Consensus::Raft::submitTransaction(Storage::Transaction* transaction,
                            const CryptoKernel::Blockchain::transaction& tx) {
                                return true;
                            }

// probably always returns true
bool CryptoKernel::Consensus::Raft::submitBlock(Storage::Transaction* transaction,
                        const CryptoKernel::Blockchain::block& block) {
                            return true;
                        }