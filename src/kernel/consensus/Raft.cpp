#include "Raft.h"

CryptoKernel::Consensus::Raft::Raft() {
    lastPing = 0;
}

void CryptoKernel::Consensus::Raft::floater() {
    time_t t = std::time(0);
    uint64_t now = static_cast<uint64_t> (t);

    while(running) {
        if(leader) {
            
        }
        else {
            unsigned long long currTime = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
            if(currTime - lastPing < electionTimeout){ 
                // everything is fine
            }
            else {
                // time to elect a new leader
            }
        }
    }
}