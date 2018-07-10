#include "network.h"
#include "networkpeer.h"
#include "version.h"

#include <list>

CryptoKernel::Network::Network(CryptoKernel::Log* log,
                               CryptoKernel::Blockchain* blockchain,
                               const unsigned int port,
                               const std::string& dbDir) {
    this->log = log;
    this->blockchain = blockchain;
    this->port = port;
    bestHeight = 0;
    currentHeight = 0;

    myAddress = sf::IpAddress::getPublicAddress();

    networkdb.reset(new CryptoKernel::Storage(dbDir, false, 8, false));
    peers.reset(new Storage::Table("peers"));

    std::unique_ptr<Storage::Transaction> dbTx(networkdb->begin());

    std::ifstream infile("peers.txt");
    if(!infile.is_open()) {
        log->printf(LOG_LEVEL_ERR, "Network(): Could not open peers file");
    }

    std::string line;
    while(std::getline(infile, line)) {
        if(!peers->get(dbTx.get(), line).isObject()) {
            Json::Value newSeed;
            newSeed["lastseen"] = 0;
            newSeed["height"] = 1;
            newSeed["score"] = 0;
            peers->put(dbTx.get(), line, newSeed);
        }
    }

    infile.close();

    dbTx->commit();

    if(listener.listen(port) != sf::Socket::Done) {
        log->printf(LOG_LEVEL_ERR, "Network(): Could not bind to port " + std::to_string(port));
    }

    running = true;

    listener.setBlocking(false);

    // Start connection thread
    //connectionThread.reset(new std::thread(&CryptoKernel::Network::connectionFunc, this));

    // Start management thread
    //networkThread.reset(new std::thread(&CryptoKernel::Network::networkFunc, this));

    // Start peer thread
   	makeOutgoingConnectionsThread.reset(new std::thread(&CryptoKernel::Network::makeOutgoingConnectionsWrapper, this));

    // Start peer thread
    //infoOutgoingConnectionsThread.reset(new std::thread(&CryptoKernel::Network::infoOutgoingConnectionsWrapper, this));
}

CryptoKernel::Network::~Network() {
    running = false;
    connectionThread->join();
    networkThread->join();
    makeOutgoingConnectionsThread->join();
    infoOutgoingConnectionsThread->join();
    listener.close();
}

void CryptoKernel::Network::makeOutgoingConnectionsWrapper() {
	while(running) {
		makeOutgoingConnections();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void CryptoKernel::Network::infoOutgoingConnectionsWrapper() {
	while(running) {
		bool wait = false;

		infoOutgoingConnections();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void CryptoKernel::Network::makeOutgoingConnections() {
	std::map<std::string, Json::Value> peersToTry;
	auto it = new CryptoKernel::Storage::Table::Iterator(
			peers.get(), networkdb.get());

	for(it->SeekToFirst(); it->Valid(); it->Next()) {
		/*if(connected.size() >= 8) { // honestly, this is enough
			std::this_thread::sleep_for(std::chrono::seconds(20)); // so stop looking for a while
		}*/

		Json::Value peerInfo = it->value();

		if(isConnected(it->key())) {
			continue;
		}

		std::time_t result = std::time(nullptr);

		/*const auto banIt = banned.find(it->key());
		if(banIt != banned.end()) {
			if(banIt->second > static_cast<uint64_t>(result)) {
				continue;
			}
		}*/

		if(peerInfo["lastattempt"].asUInt64() + 5 * 60 > static_cast<unsigned long long int>
				(result) && peerInfo["lastattempt"].asUInt64() != peerInfo["lastseen"].asUInt64()) {
			continue;
		}

		sf::IpAddress addr(it->key());

		if(addr == sf::IpAddress::getLocalAddress()
				|| addr == myAddress
				|| addr == sf::IpAddress::LocalHost
				|| addr == sf::IpAddress::None) {
			continue;
		}

		sf::TcpSocket* socket = new sf::TcpSocket();
		if(socket->connect(it->key(), port, sf::seconds(3)) == sf::Socket::Done) {
			log->printf(LOG_LEVEL_INFO, "Network(): Successfully connected to " + it->key());
			addConnection(it->key(), socket, peerInfo);
		}
		else {
			log->printf(LOG_LEVEL_WARN, "Network(): Failed to connect to " + it->key());
			delete socket;
		}
	}
	delete it;
}

void CryptoKernel::Network::addConnection(std::string ip, sf::TcpSocket* socket, Json::Value info) {
	for(auto& it : connected) {
		if(it->acquire() && it->isFree()) {
			it->peer.reset(new Peer(socket, blockchain, this, false));
			it->info["lastseen"] = static_cast<uint64_t>(std::time(nullptr));

			info["lastseen"] = static_cast<uint64_t>(std::time(nullptr));
			info["score"] = 0;

			it->info = info;
			it->release();
			break;
		}
	}
	log->printf(LOG_LEVEL_WARN, "Network(): No connection slots avaialble :(");
}

bool CryptoKernel::Network::isConnected(std::string ip) {
	for(auto& it : connected) {
		if(it->acquire()) {
			if(it->ip == ip) {
				it->release();
				return true;
			}
			it->release();
		}
	}
	return false;
}


void CryptoKernel::Network::infoOutgoingConnections() {
	std::lock_guard<std::recursive_mutex> lock(connectedMutex);

	std::unique_ptr<Storage::Transaction> dbTx(networkdb->begin());

	std::set<std::string> removals;

	for(auto& it : connected) {
		try {
			const Json::Value info = it->peer->getInfo();
			try {
				const std::string peerVersion = info["version"].asString();
				if(peerVersion.substr(0, peerVersion.find(".")) != version.substr(0, version.find("."))) {
					log->printf(LOG_LEVEL_WARN,
								"Network(): " + it->ip + " has a different major version than us");
					throw Peer::NetworkError();
				}

				/*const auto banIt = banned.find(it->ip);
				if(banIt != banned.end()) {
					if(banIt->second > static_cast<uint64_t>(std::time(nullptr))) {
						log->printf(LOG_LEVEL_WARN,
									"Network(): Disconnecting " + it->ip + " for being banned");
						throw Peer::NetworkError();
					}
				}*/

				it->info["height"] = info["tipHeight"].asUInt64();

				for(const Json::Value& peer : info["peers"]) {
					sf::IpAddress addr(peer.asString());
					if(addr != sf::IpAddress::None) {
						if(!peers->get(dbTx.get(), addr.toString()).isObject()) {
							log->printf(LOG_LEVEL_INFO, "Network(): Discovered new peer: " + addr.toString());
							Json::Value newSeed;
							newSeed["lastseen"] = 0;
							newSeed["height"] = 1;
							newSeed["score"] = 0;
							peers->put(dbTx.get(), addr.toString(), newSeed);
						}
					} else {
						changeScore(it->ip, 10);
						throw Peer::NetworkError();
					}
				}
			} catch(const Json::Exception& e) {
				changeScore(it->ip, 50);
				throw Peer::NetworkError();
			}

			const std::time_t result = std::time(nullptr);
			it->info["lastseen"] = static_cast<uint64_t>(result);
		} catch(const Peer::NetworkError& e) {
			log->printf(LOG_LEVEL_WARN,
						"Network(): Failed to contact " + it->ip + ", disconnecting it");
			removals.insert(it->ip);
		}
	}

	/*for(const auto& peer : removals) {
		const auto it = connected.find(peer);
		peers->put(dbTx.get(), peer, it->info);
		if(it != connected.end()) {
			connected.erase(it);
		}
	}*/

	dbTx->commit();
}

void CryptoKernel::Network::networkFunc() {
    std::unique_ptr<std::thread> blockProcessor;
    bool failure = false;
    uint64_t currentHeight = blockchain->getBlockDB("tip").getHeight();
    this->currentHeight = currentHeight;
    uint64_t startHeight = currentHeight;

    while(running) {
        //Determine best chain
        connectedMutex.lock();
        uint64_t bestHeight = currentHeight;
        for(auto& it : connected) {
            if(it->info["height"].asUInt64() > bestHeight) {
                bestHeight = it->info["height"].asUInt64();
            }
        }

        if(this->currentHeight > bestHeight) {
            bestHeight = this->currentHeight;
        }
        this->bestHeight = bestHeight;
        connectedMutex.unlock();

        log->printf(LOG_LEVEL_INFO,
                    "Network(): Current height: " + std::to_string(currentHeight) + ", best height: " +
                    std::to_string(bestHeight) + ", start height: " + std::to_string(startHeight));

        bool madeProgress = false;

        //Detect if we are behind
        if(bestHeight > currentHeight) {
            connectedMutex.lock();

            for(auto& it: connected) {
            	if(!running) {
            		break;
            	}

                if(it->info["height"].asUInt64() > currentHeight) {
                    std::list<CryptoKernel::Blockchain::block> blocks;

                    const std::string peerUrl = it->ip;

                    if(currentHeight == startHeight) {
                        auto nBlocks = 0;
                        do {
                            log->printf(LOG_LEVEL_INFO,
                                        "Network(): Downloading blocks " + std::to_string(currentHeight + 1) + " to " +
                                        std::to_string(currentHeight + 6));
                            try {
                                const auto newBlocks = it->peer->getBlocks(currentHeight + 1, currentHeight + 6);
                                nBlocks = newBlocks.size();
                                blocks.insert(blocks.end(), newBlocks.rbegin(), newBlocks.rend());
                                if(nBlocks > 0) {
                                    madeProgress = true;
                                } else {
                                    log->printf(LOG_LEVEL_WARN, "Network(): Peer responded with no blocks");
                                }
                            } catch(Peer::NetworkError& e) {
                                log->printf(LOG_LEVEL_WARN,
                                            "Network(): Failed to contact " + it->ip + " " + e.what() +
                                            " while downloading blocks");
                                it++;
                                break;
                            }

                            log->printf(LOG_LEVEL_INFO, "Network(): Testing if we have block " + std::to_string(blocks.rbegin()->getHeight() - 1));

                            try {
                                blockchain->getBlockDB(blocks.rbegin()->getPreviousBlockId().toString());
                            } catch(const CryptoKernel::Blockchain::NotFoundException& e) {
                                if(currentHeight == 1) {
                                    // This peer has a different genesis block to us
                                    changeScore(it->ip, 250);
                                    it++;
                                    break;
                                } else {
                                    log->printf(LOG_LEVEL_INFO, "Network(): got block h: " + std::to_string(blocks.rbegin()->getHeight()) + " with prevBlock: " + blocks.rbegin()->getPreviousBlockId().toString() + " prev not found");


                                    currentHeight = std::max(1, (int)currentHeight - nBlocks);
                                    continue;
                                }
                            }

                            break;
                        } while(running);

                        currentHeight += nBlocks;
                    }

                    log->printf(LOG_LEVEL_INFO, "Network(): Found common block " + std::to_string(currentHeight-1) + " with peer, starting block download");

                    while(blocks.size() < 2000 && running && !failure && currentHeight < bestHeight) {
                        log->printf(LOG_LEVEL_INFO,
                                    "Network(): Downloading blocks " + std::to_string(currentHeight + 1) + " to " +
                                    std::to_string(currentHeight + 6));

                        auto nBlocks = 0;
                        try {
                            const auto newBlocks = it->peer->getBlocks(currentHeight + 1, currentHeight + 6);
                            nBlocks = newBlocks.size();
                            blocks.insert(blocks.begin(), newBlocks.rbegin(), newBlocks.rend());
                            if(nBlocks > 0) {
                                madeProgress = true;
                            } else {
                                log->printf(LOG_LEVEL_WARN, "Network(): Peer responded with no blocks");
                                break;
                            }
                        } catch(Peer::NetworkError& e) {
                            log->printf(LOG_LEVEL_WARN,
                                        "Network(): Failed to contact " + it->ip + " " + e.what() +
                                        " while downloading blocks");
                            it++;
                            break;
                        }

                        currentHeight = std::min(currentHeight + std::max(nBlocks, 1), bestHeight);
                    }

                    if(blockProcessor) {
                        log->printf(LOG_LEVEL_INFO, "Network(): Waiting for previous block processor thread to finish");
                        blockProcessor->join();
                        blockProcessor.reset();

                        if(failure) {
                            log->printf(LOG_LEVEL_WARN, "Network(): Failure processing blocks");
                            blocks.clear();
                            currentHeight = blockchain->getBlockDB("tip").getHeight();
                            this->currentHeight = currentHeight;
                            startHeight = currentHeight;
                            bestHeight = currentHeight;
                            it++;
                            failure = false;
                            break;
                        }
                    }

                    blockProcessor.reset(new std::thread([&, blocks](const std::string& peer){
                        failure = false;

                        log->printf(LOG_LEVEL_INFO, "Network(): Submitting " + std::to_string(blocks.size()) + " blocks to blockchain");

                        for(auto rit = blocks.rbegin(); rit != blocks.rend() && running; ++rit) {
                            const auto blockResult = blockchain->submitBlock(*rit);

                            if(std::get<1>(blockResult)) {
                                changeScore(peer, 50);
                            }

                            if(!std::get<0>(blockResult)) {
                                failure = true;
                                changeScore(peer, 25);
                                log->printf(LOG_LEVEL_WARN, "Network(): offending block: " + rit->toJson().toStyledString());
                                break;
                            }
                        }
                    }, peerUrl));
                } else {
                    it++;
                }
            }

            connectedMutex.unlock();
        }

        if(bestHeight <= currentHeight || connected.size() == 0 || !madeProgress) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20000));
            currentHeight = blockchain->getBlockDB("tip").getHeight();
            startHeight = currentHeight;
            this->currentHeight = currentHeight;
        }
    }

    if(blockProcessor) {
        blockProcessor->join();
    }
}

void CryptoKernel::Network::connectionFunc() {
    while(running) {
        sf::TcpSocket* client = new sf::TcpSocket();
        if(listener.accept(*client) == sf::Socket::Done) {
            std::lock_guard<std::recursive_mutex> lock(connectedMutex);
        	//connectedMutex.lock();
            if(isConnected(client->getRemoteAddress().toString())) {
                log->printf(LOG_LEVEL_INFO,
                            "Network(): Incoming connection duplicates existing connection for " +
                            client->getRemoteAddress().toString());
                client->disconnect();
                delete client;
                continue;
            }

            /*const auto it = banned.find(client->getRemoteAddress().toString());
            if(it != banned.end()) {
                if(it->second > static_cast<uint64_t>(std::time(nullptr))) {
                    log->printf(LOG_LEVEL_INFO,
                                "Network(): Incoming connection " + client->getRemoteAddress().toString() + " is banned");
                    client->disconnect();
                    delete client;
                    continue;
                }
            }*/

            sf::IpAddress addr(client->getRemoteAddress());

            if(addr == sf::IpAddress::getLocalAddress()
                    || addr == myAddress
                    || addr == sf::IpAddress::LocalHost
                    || addr == sf::IpAddress::None) {
                log->printf(LOG_LEVEL_INFO,
                            "Network(): Incoming connection " + client->getRemoteAddress().toString() +
                            " is connecting to self");
                client->disconnect();
                delete client;
                continue;
            }


            log->printf(LOG_LEVEL_INFO,
                        "Network(): Peer connected from " + client->getRemoteAddress().toString() + ":" +
                        std::to_string(client->getRemotePort()));

            /// PUT THIS BACK
//            PeerInfo* peerInfo = new PeerInfo();
//            peerInfo->peer.reset(new Peer(client, blockchain, this, true));
//
//            Json::Value info;
//
//            try {
//                info = peerInfo->peer->getInfo();
//            } catch(const Peer::NetworkError& e) {
//                log->printf(LOG_LEVEL_WARN, "Network(): Failed to get information from connecting peer");
//                delete peerInfo;
//                continue;
//            }
//
//            try {
//                peerInfo->info["height"] = info["tipHeight"].asUInt64();
//                peerInfo->info["version"] = info["version"].asString();
//            } catch(const Json::Exception& e) {
//                log->printf(LOG_LEVEL_WARN, "Network(): Incoming peer sent invalid info message");
//                delete peerInfo;
//                continue;
//            }
//
//            const std::time_t result = std::time(nullptr);
//            peerInfo->info["lastseen"] = static_cast<uint64_t>(result);
//
//            peerInfo->info["score"] = 0;
//
//            connected[client->getRemoteAddress().toString()].reset(peerInfo);
//
//            std::unique_ptr<Storage::Transaction> dbTx(networkdb->begin());
//            peers->put(dbTx.get(), client->getRemoteAddress().toString(), peerInfo->info);
//            dbTx->commit();
        } else {
            delete client;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

unsigned int CryptoKernel::Network::getConnections() {
    return connected.size();
}

void CryptoKernel::Network::broadcastTransactions(const
        std::vector<CryptoKernel::Blockchain::transaction> transactions) {
    for(auto& it : connected) {
        try {
            it->peer->sendTransactions(transactions);
        } catch(CryptoKernel::Network::Peer::NetworkError& err) {
            log->printf(LOG_LEVEL_WARN, "Network::broadcastTransactions(): Failed to contact peer");
        }
    }
}

void CryptoKernel::Network::broadcastBlock(const CryptoKernel::Blockchain::block block) {
    for(auto& it : connected) {
        try {
            it->peer->sendBlock(block);
        } catch(CryptoKernel::Network::Peer::NetworkError& err) {
            log->printf(LOG_LEVEL_WARN, "Network::broadcastBlock(): Failed to contact peer");
        }
    }
}

double CryptoKernel::Network::syncProgress() {
    return (double)(currentHeight)/(double)(bestHeight);
}

void CryptoKernel::Network::changeScore(const std::string& url, const uint64_t score) {
	// PUT THIS BACK TOO
//    if(isCoconnected[url]) {
//        connected[url]->info["score"] = connected[url]->info["score"].asUInt64() + score;
//        log->printf(LOG_LEVEL_WARN,
//                    "Network(): " + url + " misbehaving, increasing ban score by " + std::to_string(
//                        score) + " to " + connected[url]->info["score"].asString());
//        if(connected[url]->info["score"].asUInt64() > 200) {
//            log->printf(LOG_LEVEL_WARN,
//                        "Network(): Banning " + url + " for being above the ban score threshold");
//            // Ban for 24 hours
//            banned[url] = static_cast<uint64_t>(std::time(nullptr)) + 24 * 60 * 60;
//        }
//        connected[url]->info["disconnect"] = true;
//    }
}

std::set<std::string> CryptoKernel::Network::getConnectedPeers() {
    std::set<std::string> peerUrls;
    for(const auto& it : connected) {
        peerUrls.insert(it->ip);
    }

    return peerUrls;
}

uint64_t CryptoKernel::Network::getCurrentHeight() {
    return currentHeight;
}

std::map<std::string, CryptoKernel::Network::peerStats>
CryptoKernel::Network::getPeerStats() {
    std::lock_guard<std::mutex> lock(connectedStatsMutex);

    return connectedStats;
}
