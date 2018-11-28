#include "Raft.h"

CryptoKernel::Consensus::Raft::Raft(CryptoKernel::Blockchain* blockchain, std::string pubKey, CryptoKernel::Log* log) {
    this->blockchain = blockchain;
    this->pubKey = pubKey;
    this->log = log;

    //this->network = network;

    this->raftNet = new RaftNet(log);

    running = true;
    lastPing = 0;
    electionTimeout = 3000 + rand() % 1000;
    networkSize = 3;
    leader = false;
    candidate = false;
    term = 0;
}

CryptoKernel::Consensus::Raft::~Raft() {
    running = false;
    floaterThread->join();
}

void CryptoKernel::Consensus::Raft::start() {
    floaterThread.reset(new std::thread(&CryptoKernel::Consensus::Raft::floater, this));
}

bool CryptoKernel::Consensus::Raft::checkConsensusRules(Storage::Transaction* transaction,
                                                        CryptoKernel::Blockchain::block& block,
                                                        const CryptoKernel::Blockchain::dbBlock& previousBlock) {
}

void CryptoKernel::Consensus::Raft::processQueue() {
    log->printf(LOG_LEVEL_INFO, "Checking consenus rules");

    std::vector<std::string> queue = this->raftNet->pullMessages();

    for(auto it = queue.begin(); it != queue.end(); it++) {
        Json::Value data = CryptoKernel::Storage::toJson(*it);

        if(data["rpc"] && data["sender"].asString() != pubKey) {
            log->printf(LOG_LEVEL_INFO, "Okay, there IS an RPC call being made");
            if(data["rpc"].asString() == "request_votes" && data["direction"].asString() == "sender") {
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
                        log->printf(LOG_LEVEL_INFO, "I have been elected leader.");
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
    }
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
            if(currTime - lastPing < electionTimeout && !candidate) {
                // everything is fine
                resetValues();
                log->printf(LOG_LEVEL_INFO, "I am a follower.  I got a heartbeat recently.");
            }
            else {
                // time to elect a new leader
                lastPing = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
                log->printf(LOG_LEVEL_INFO, "I haven't got a leader.  We need to elect a leader!");
                resetValues();
                candidate = true;
                supporters.insert(pubKey);
                ++term;
                requestVotes();
            }
        }
        processQueue();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void CryptoKernel::Consensus::Raft::resetValues() {
    supporters.clear();
    leader = false;
    candidate = false;
}

void CryptoKernel::Consensus::Raft::requestVotes() {
    log->printf(LOG_LEVEL_INFO, "Requesting votes...");
    Json::Value dummyData;
    dummyData["rpc"] = "request_votes";
    dummyData["direction"] = "sender";
    dummyData["sender"] = pubKey;
    dummyData["term"] = term;
    sendAll(dummyData);
}

void CryptoKernel::Consensus::Raft::castVote(std::string candidateId) {
    log->printf(LOG_LEVEL_INFO, "Casting vote for " + candidateId);
    candidate = false;
    Json::Value dummyData;
    dummyData["rpc"] = "request_votes";
    dummyData["direction"] = "responding";
    dummyData["sender"] = pubKey;
    dummyData["term"] = term;
    dummyData["candidate"] = candidateId;
    sendAll(dummyData);
}

void CryptoKernel::Consensus::Raft::sendHeartbeat() {
    log->printf(LOG_LEVEL_INFO, "Sending heartbeat...");
    Json::Value dummyData;
    dummyData["rpc"] = "heartbeat"; // paper uses an empty append_entries, but this is easier
    dummyData["sender"] = pubKey;
    sendAll(dummyData);
}

void CryptoKernel::Consensus::Raft::sendAll(Json::Value data) {
    std::string addrs[] = {"100.24.202.21", "100.24.228.94", "34.195.150.28"};

    log->printf(LOG_LEVEL_INFO, "Sending messages (requesting votes");
    for(int i =  0; i < 3; i++) {
        this->raftNet->send(addrs[i], 1701, CryptoKernel::Storage::toString(data));
    }
}

bool CryptoKernel::Consensus::Raft::isBlockBetter(Storage::Transaction* transaction,
                               const CryptoKernel::Blockchain::block& block,
                               const CryptoKernel::Blockchain::dbBlock& tip) {
                                   return true;
                               }


// hrrrrrm*****
Json::Value CryptoKernel::Consensus::Raft::generateConsensusData(Storage::Transaction* transaction,
        const CryptoKernel::BigNum& previousBlockId, const std::string& publicKey) {
            //log->printf(LOG_LEVEL_INFO, "Uh-oh... generating consensus data");
            std::string blockId = previousBlockId.toString();
            CryptoKernel::Blockchain::block block = blockchain->getBlock(blockId);
            Json::Value consensusData = block.getConsensusData();
            //log->printf(LOG_LEVEL_INFO, "Consenus data: " + consensusData.toStyledString());
            return consensusData;
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

