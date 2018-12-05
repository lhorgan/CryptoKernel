#include "Raft.h"

CryptoKernel::Consensus::Raft::Raft(CryptoKernel::Blockchain* blockchain, std::string pubKey, CryptoKernel::Log* log) {
    this->blockchain = blockchain;
    this->pubKey = pubKey;
    this->log = log;

    this->generateEntryLog(); // set up the entry log

    this->raftNet = new RaftNet(log);

    running = true;
    lastPing = 0;
    electionTimeout = 1000 + rand() % 500;
    networkSize = 3;
    leader = false;
    candidate = false;
    term = 0;
    votedFor = "";
}

void CryptoKernel::Consensus::Raft::generateEntryLog() {
    uint64_t currentHeight = blockchain->getBlockDB("tip").getHeight();
    for(int i = 0; i <= currentHeight; i++) {
        int term = blockchain->getBlockByHeight(i).getConsensusData()["term"].isInt();
        entryLog.push_back(term);
    }
}

CryptoKernel::Consensus::Raft::~Raft() {
    hostMutex.lock();
    hosts.clear(); // clear out the hosts
    hostMutex.unlock();
    running = false;
    floaterThread->join();
}

void CryptoKernel::Consensus::Raft::start() {
    floaterThread.reset(new std::thread(&CryptoKernel::Consensus::Raft::floater, this));
}

bool CryptoKernel::Consensus::Raft::checkConsensusRules(Storage::Transaction* transaction,
                                                        CryptoKernel::Blockchain::block& block,
                                                        const CryptoKernel::Blockchain::dbBlock& previousBlock) {
    // leader has this block and the previous block
    // block.index = prevBlockIndex + 1
    Json::Value blockData = block.getConsensusData();
    int blockTerm = blockData["term"].asInt();
    int blockIndex = blockData["index"].asInt();

    Json::Value prevBlockData = previousBlock.getConsensusData();
    int prevBlockTerm = prevBlockData["term"].asInt();
    int prevBlockIndex = prevBlockData["index"].asInt();

    bool result = false;
    logEntryMutex.lock();
    if(blockIndex == prevBlockIndex + 1) {
        if(blockIndex < entryLog.size()) {
            if(entryLog[blockIndex] == blockTerm && entryLog[prevBlockIndex] == prevBlockTerm) {
                result = true;
            }
        }
    }
    logEntryMutex.unlock();
    
    return result;
}

void CryptoKernel::Consensus::Raft::processQueue() {
    std::vector<std::string> queue = this->raftNet->pullMessages();
    //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Queue length: " + std::to_string(queue.size()));

    for(int i = 0; i < queue.size(); i++) {
        //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " THE DECODED MESSAGE: " + queue[i]);
        Json::Value dataArr = CryptoKernel::Storage::toJson(queue[i]);
        for(Json::Value::ArrayIndex j = 0; j < dataArr.size(); j++) {
            Json::Value data = CryptoKernel::Storage::toJson(dataArr[j].asString());
            // we accept votes from nodes with out of date term... for now**
            if(data["rpc"] && data["sender"].asString() != pubKey) {
                if(data["rpc"].asString() == "append_entries") {
                    handleAppendEntries(data);
                }
                else if(data["rpc"].asString() == "request_votes") {
                    handleRequestVotes(data);
                }
            }
        }
    }
}

void CryptoKernel::Consensus::Raft::handleRequestVotes(Json::Value& data) {
    //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Handling vote request , " + data["direction"].asString());

    int requesterTerm = data["term"].asInt();

    if(data["direction"].asString() == "sending") { // we have RECEIVED a request for a vote
        if(requesterTerm >= term) { // only vote for candidates with a greater term
            handleTermDisparity(requesterTerm);
            if(votedFor == "" || votedFor == data["sender"].asString()) {
                // cast a vote for this node
                castVote(data["sender"].asString(), true);
            }
            else {
                //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " I am not voting for " + data["sender"].asString() + " because I already voted for " + votedFor);
                castVote(data["sender"].asString(), false);
            }
        }
        else { // their term is too small, don't vote for them
            //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " I am not voting for " + data["sender"].asString() + " because their term is smaller than mine.");
            castVote(data["sender"].asString(), false);
        }
    }
    else if(data["direction"].asString() == "responding") { // someone else has voted for us
        // am I a candidate?
        if(candidate && term >= requesterTerm) { // we don't care about a voter's term status, anyone can vote for us (but we can't be elected if our term is out of date)
            if(data["vote"].asBool()) { // the vote was a yes
                supporters.insert(data["sender"].asString());
                if(supporters.size() > networkSize / 2) { // we have a simple majority of voters
                    //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " I have been elected leader");
                    leader = true; // I am the captain now!
                }
            }
            handleTermDisparity(requesterTerm);   
        }
    }
}

void CryptoKernel::Consensus::Raft::handleAppendEntries(Json::Value& data) {
    int requesterTerm = data["term"].asInt();

    if(data["direction"] == "sending") { // we have received a heartbeat from a node
        bool success = false;
        if(requesterTerm >= term) {
            // update last ping
            resetValues();
            //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " I have received a heartbeat from " + data["sender"].asString());
            currentLeader = data["sender"].asString();
            lastPing = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();

            
            int prevTerm = data["prevTerm"].asInt();
            int prevIndex = data["prevIndex"].asInt();
            
            // bring our logs in sync
            logEntryMutex.lock();
            if(prevIndex < entryLog.size()) {
                if(entryLog[prevIndex] == prevTerm) {
                    log->printf(LOG_LEVEL_INFO, "Leader says term at index " + std::to_string(prevTerm) + ", and I agree.  Appending new log entries.");
                    entryLog.resize(prevIndex + 1); // trim off any entries that don't belong
                    Json::Value logArr = data["log"];
                    for(Json::Value::ArrayIndex i = 0; i < logArr.size(); i++) {
                        entryLog.push_back(logArr[i].asInt());
                    }
                    success = true;
                }
            }
            logEntryMutex.unlock();
        }
        handleTermDisparity(requesterTerm);

        Json::Value response;
        response["rpc"] = "append_entries";
        response["direction"] = "responding";
        response["sender"] = pubKey;
        response["term"] = term;
        response["success"] = success;

        sendToLeader(response);
    }
    else if(data["direction"] == "responding") { // someone responded to our heartbeat
        if(leader && !data["success"].asBool()) {
            log->printf(LOG_LEVEL_INFO, "Follower responded that it couldn't accept log, decrementing lastIndex");
            hostMutex.lock();
            std::string sender = data["sender"].asString();
            hosts[sender]->lastIndex--;
            hostMutex.unlock();
        }
        handleTermDisparity(requesterTerm);
    }
}

void CryptoKernel::Consensus::Raft::sendToLeader(Json::Value value) {
    hostMutex.lock();
    auto it = hosts.find(currentLeader);
    if(it != hosts.end()) {
        raftNet->send(it->second->ip, 1701, CryptoKernel::Storage::toString(value));
    }
    hostMutex.unlock();
}

void CryptoKernel::Consensus::Raft::handleTermDisparity(int requesterTerm) {
    if(requesterTerm > term) {
        //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Received ping from server with GREATER term " + std::to_string(requesterTerm) + ", " + std::to_string(term));
        term = requesterTerm;
        votedFor = "";
        candidate = false;
        leader = false;
    }
}

void CryptoKernel::Consensus::Raft::createBlock() {
    CryptoKernel::Blockchain::block Block = blockchain->generateVerifyingBlock(pubKey);
    const std::set<CryptoKernel::Blockchain::transaction> blockTransactions = Block.getTransactions();
    
    int notFoundNum = 0;
    for(auto it = blockTransactions.begin(); it != blockTransactions.end(); it++) {
        if(queuedTransactions.find(*it) == queuedTransactions.end()) { // this transaction is not "queued"
            notFoundNum += 1;
        }
    }
    
    /*for(auto it = queuedTransactions.begin(); it != queuedTransactions.end(); it++) {
        if(blockTransactions.find(*it) == blockTransactions.end()) { // this transaction has evidently been confirmed
            queuedTransactions.erase(it);
        }
    }*/

    CryptoKernel::Blockchain::dbBlock previousBlock = blockchain->getBlockDB(Block.getPreviousBlockId().toString());
    Json::Value consensusData = Block.getConsensusData();

    logEntryMutex.lock();
    entryLog.push_back(term);
    consensusData["nonce"] = rand() % 10000000; // make this random**
    consensusData["term"] = term;
    consensusData["index"] = entryLog.size() - 1;
    logEntryMutex.unlock();
    Block.setConsensusData(consensusData);
    blockchain->submitBlock(Block);
}

void CryptoKernel::Consensus::Raft::floater() {
    time_t t = std::time(0);
    uint64_t now = static_cast<uint64_t> (t);

    int iteration = 0;
    while(running) {
        // this node is the leader
        if(leader) {
            sendAppendEntries(); // hearbeat
            //log->printf(LOG_LEVEL_INFO, "Leader here, sending heartbeat " + std::to_string(iteration));
            //if(iteration == 40) { // about once every 2 seconds
            //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " I am the leader, attempting block creation.\n\n");
            createBlock();
            //iteration = 0;
            //}
        }
        else {
            unsigned long long currTime = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
            if(currTime - lastPing < electionTimeout && !candidate) {
                // everything is fine
                resetValues();
            }
            else {
                //log->printf(LOG_LEVEL_INFO, "\n~~~~~~~\n\n");

                // time to elect a new leader
                currentLeader = "";
                lastPing = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
                electionTimeout = 150 + rand() % 150;
                //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " I haven't got a leader.  We need to elect a leader!");
                resetValues();
                candidate = true;
                supporters.insert(pubKey);
                votedFor = pubKey;
                ++term;
                requestVotes();
            }
        }

        processQueue();
        iteration++;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void CryptoKernel::Consensus::Raft::resetValues() {
    supporters.clear();
    leader = false;
    candidate = false;
}

void CryptoKernel::Consensus::Raft::requestVotes() {
    //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Requesting votes...");
    Json::Value dummyData;
    dummyData["rpc"] = "request_votes";
    dummyData["direction"] = "sending";
    dummyData["sender"] = pubKey;
    dummyData["term"] = term;
    sendAll(dummyData);
}

void CryptoKernel::Consensus::Raft::castVote(std::string candidateId, bool vote) {
    if(vote) {
        //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Casting YES vote for " + candidateId);
        votedFor = candidateId;
    }
    else {
        //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Casting NO vote for " + candidateId);
    }
    
    Json::Value dummyData;
    dummyData["rpc"] = "request_votes";
    dummyData["direction"] = "responding";
    dummyData["sender"] = pubKey;
    dummyData["term"] = term;
    dummyData["candidate"] = candidateId;
    dummyData["vote"] = vote;
    sendAll(dummyData);
}

void CryptoKernel::Consensus::Raft::sendAppendEntries() {
    //log->printf(LOG_LEVEL_INFO, std::to_string(term) + " Sending heartbeat...");
    Json::Value dummyData;
    dummyData["rpc"] = "append_entries";
    dummyData["direction"] = "sending";
    dummyData["sender"] = pubKey;
    
    dummyData["term"] = term;
    dummyData["commitIndex"] = commitIndex;

    std::map<std::string, Host> hostsCopy = cacheHosts();

    for(auto it = hostsCopy.begin(); it != hostsCopy.end(); it++) {
        dummyData["log"] = {};

        logEntryMutex.lock();
        for(int i = 0; i < it->second.lastIndex; i++) {
            dummyData["log"].append(entryLog[i]);
        }
        logEntryMutex.unlock();

        dummyData["prevLogIndex"] = it->second.lastIndex;
        logEntryMutex.lock();
        dummyData["prevLogTerm"] = entryLog[it->second.lastIndex];
        logEntryMutex.unlock();
        this->raftNet->send(it->second.ip, 1701, CryptoKernel::Storage::toString(dummyData));
    }
}

std::map<std::string, CryptoKernel::Host> CryptoKernel::Consensus::Raft::cacheHosts() {
    std::map<std::string, Host> hostsCopy;

    hostMutex.lock();
    for(auto it = hosts.begin(); it != hosts.end(); it++) {
        hostsCopy[it->first] = it->second->copy();
    }
    hostMutex.unlock();

    return hostsCopy;
}

void CryptoKernel::Consensus::Raft::sendAll(Json::Value data) {
    std::string addrs[] = {"100.24.202.21", "100.24.228.94", "34.195.150.28"};

    for(int i =  0; i < 3; i++) {
        this->raftNet->send(addrs[i], 1701, CryptoKernel::Storage::toString(data));
    }
}

bool CryptoKernel::Consensus::Raft::isBlockBetter(Storage::Transaction* transaction,
                               const CryptoKernel::Blockchain::block& block,
                               const CryptoKernel::Blockchain::dbBlock& tip) {
                                   // case: leader has our tip
                                      // leader also has the block --> block is better
                                      // leader does not have the block --> block is not better
                                   // case: leader does not have our itp
                                      // leader has the block --> block is better
                                      // leader does not have the block --> block is not better
                                   
                                   // So, regardless of whether or not the leader has our tip, the block is better if the leader has it,
                                   // under the assumption that anything coming into this function represents A BLOCK THAT WE DO NOT YET HAVE
                                   int blockIndex = block.getConsensusData()["index"].asInt();
                                   int blockTerm = block.getConsensusData()["term"].asInt();
                                   if(entryLog[blockIndex] == blockTerm) {
                                       return true;
                                   }
                                   return false;
                               }


// hrrrrrm*****
Json::Value CryptoKernel::Consensus::Raft::generateConsensusData(Storage::Transaction* transaction,
        const CryptoKernel::BigNum& previousBlockId, const std::string& publicKey) {
            std::string blockId = previousBlockId.toString();
            CryptoKernel::Blockchain::block block = blockchain->getBlock(blockId);
            Json::Value consensusData = block.getConsensusData();
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

