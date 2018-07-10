#include "network.h"
#include "networkpeer.h"
#include "version.h"

#include <list>
#include <ctime>

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
    //connectionThread.reset(new std::thread(&CryptoKernel::Network::connectionFuncWrapper, this));

    // Start management thread
    //networkThread.reset(new std::thread(&CryptoKernel::Network::networkFuncWrapper, this));

    // Start peer thread
   	makeOutgoingConnectionsThread.reset(new std::thread(&CryptoKernel::Network::makeOutgoingConnectionsWrapper, this));

    // Start peer thread
    infoOutgoingConnectionsThread.reset(new std::thread(&CryptoKernel::Network::infoOutgoingConnectionsWrapper, this));
}

CryptoKernel::Network::~Network() {
    running = false;
    connectionThread->join();
    networkThread->join();
    makeOutgoingConnectionsThread->join();
    infoOutgoingConnectionsThread->join();
    listener.close();
}

bool CryptoKernel::Network::cacheConnections(std::map<std::string, std::shared_ptr<PeerInfo>>& connectedCache,
											 std::chrono::high_resolution_clock::time_point lastUpdate,
											 double cacheInterval) {

	std::chrono::high_resolution_clock::time_point currTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(currTime - lastUpdate);

	if(elapsedTime.count() > cacheInterval || connectedCache.size() == 0) {
		std::lock_guard<std::recursive_mutex> lock(connectedMutex);

		for(auto& entry: connectedCache) { // todo, handle deleted connections
			if(connections.find(entry.first) == connections.end()) {
				//PeerInfo* peerInfo = new PeerInfo;
				//peerInfo->info = entry.second->info;
				//peerInfo->peer.reset(new Peer(entry.second->peer.get()));
				//peerInfo->peer.reset(entry.second->peer.get());
				//connections[entry.first].reset(peerInfo);
				//connections.insert(std::pair<std::string, std::shared_ptr<PeerInfo>>(entry.first, entry.second));
				connections.insert(entry);
			}
		}
		connectedCache.clear();

		for(auto& entry : connections) {
			//PeerInfo* peerInfo = new PeerInfo;
			//peerInfo->info = entry.second->info;
			//peerInfo->peer.reset(new Peer(entry.second->peer.get()));
			//peerInfo->peer.reset(entry.second->peer.get());
			//connectedCache[entry.first].reset(peerInfo);

			//connectedCache.insert(std::pair<std::string, std::shared_ptr<PeerInfo>>(entry.first, entry.second));
			connections.insert(entry);
		}
		return true;
	}
	return false;
}

void CryptoKernel::Network::makeOutgoingConnectionsWrapper() {
	std::chrono::high_resolution_clock::time_point lastUpdate = std::chrono::high_resolution_clock::now();
	std::map<std::string, std::shared_ptr<PeerInfo>> connected; // rename cachedConnected?
	while(running) {
		if(cacheConnections(connected, lastUpdate, 1.0)) {
			lastUpdate = std::chrono::high_resolution_clock::now();
		}

		makeOutgoingConnections(connected);

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void CryptoKernel::Network::infoOutgoingConnectionsWrapper() {
	std::chrono::high_resolution_clock::time_point lastUpdate = std::chrono::high_resolution_clock::now();
	std::map<std::string, std::shared_ptr<PeerInfo>> connected; // rename cachedConnected?
	while(running) {
		if(cacheConnections(connected, lastUpdate, 1.0)) {
			lastUpdate = std::chrono::high_resolution_clock::now();
		}

		infoOutgoingConnections(connected);

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void CryptoKernel::Network::networkFuncWrapper() {
	std::unique_ptr<std::thread> blockProcessor;
	bool failure = false;
	uint64_t currentHeight = blockchain->getBlockDB("tip").getHeight();

	heightMutex.lock();
	this->currentHeight = currentHeight;
	heightMutex.unlock();
	uint64_t startHeight = currentHeight;

	std::chrono::high_resolution_clock::time_point lastUpdate = std::chrono::high_resolution_clock::now();
	std::map<std::string, std::shared_ptr<PeerInfo>> connected; // rename cachedConnected?
	while(running) {
		if(cacheConnections(connected, lastUpdate, 1.0)) {
			lastUpdate = std::chrono::high_resolution_clock::now();
		}

		networkFunc(connected, failure, blockProcessor, currentHeight, startHeight);
	}

	if(blockProcessor) {
		blockProcessor->join();
	}
}

void CryptoKernel::Network::connectionFuncWrapper() {
	std::chrono::high_resolution_clock::time_point lastUpdate = std::chrono::high_resolution_clock::now();
	std::map<std::string, std::shared_ptr<PeerInfo>> connected; // rename cachedConnected?
	while(running) {
		if(cacheConnections(connected, lastUpdate, 1.0)) {
			lastUpdate = std::chrono::high_resolution_clock::now();
		}

		connectionFunc(connected);
	}
}

void CryptoKernel::Network::makeOutgoingConnections(std::map<std::string, std::shared_ptr<PeerInfo>>& connected) {
	std::map<std::string, Json::Value> peersToTry;
	//{
	log->printf(LOG_LEVEL_INFO, "hey, it's my turn!");
	//std::lock_guard<std::recursive_mutex> lock(connectedMutex);
	CryptoKernel::Storage::Table::Iterator* it = new CryptoKernel::Storage::Table::Iterator(
			peers.get(), networkdb.get());

	for(it->SeekToFirst(); it->Valid(); it->Next()) {
		if(connected.size() >= 8) { // honestly, this is enough
			std::this_thread::sleep_for(std::chrono::seconds(20)); // so stop looking for a while
		}

		Json::Value peerInfo = it->value();

		if(connected.find(it->key()) != connected.end()) {
			continue;
		}

		std::time_t result = std::time(nullptr);

		{
			std::lock_guard<std::mutex> lock(bannedMutex);
			const auto banIt = banned.find(it->key());
			if(banIt != banned.end()) { // todo, cache banned
				if(banIt->second > static_cast<uint64_t>(result)) {
					continue;
				}
			}
		}

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

		log->printf(LOG_LEVEL_INFO, "Network(): Attempting to connect to " + it->key());
		peersToTry.insert(std::pair<std::string, Json::Value>(it->key(), peerInfo));
	}
	delete it;
	//}

	// here, we only access local data (except where there are more locks)
	for(std::map<std::string, Json::Value>::iterator entry = peersToTry.begin(); entry != peersToTry.end(); ++entry) {
		std::string peerIp = entry->first;
		Json::Value peerData = entry->second;

		sf::TcpSocket* socket = new sf::TcpSocket();
		if(socket->connect(peerIp, port, sf::seconds(3)) == sf::Socket::Done) {
			log->printf(LOG_LEVEL_INFO, "Network(): Successfully connected to " + peerIp);
			//std::lock_guard<std::recursive_mutex> lock(connectedMutex);
			PeerInfo* peerInfo = new PeerInfo;
			peerInfo->peer.reset(new Peer(socket, blockchain, this, false));

			peerData["lastseen"] = static_cast<uint64_t>(std::time(nullptr));
			peerData["score"] = 0;

			peerInfo->info = peerData;

			connected[peerIp].reset(peerInfo);
		}
		else {
			log->printf(LOG_LEVEL_WARN, "Network(): Failed to connect to " + peerIp);
			delete socket;
		}
	}
}

void CryptoKernel::Network::infoOutgoingConnections(std::map<std::string, std::shared_ptr<PeerInfo>>& connected) {
	//std::lock_guard<std::recursive_mutex> lock(connectedMutex);
	log->printf(LOG_LEVEL_INFO, "getting block info");

	std::unique_ptr<Storage::Transaction> dbTx(networkdb->begin());

	std::set<std::string> removals;

	for(std::map<std::string, std::shared_ptr<PeerInfo>>::iterator it = connected.begin();
				it != connected.end(); it++) {
		try {

			const Json::Value info = it->second->peer->getInfo();
			try {
				const std::string peerVersion = info["version"].asString();
				if(peerVersion.substr(0, peerVersion.find(".")) != version.substr(0, version.find("."))) {
					log->printf(LOG_LEVEL_WARN,
								"Network(): " + it->first + " has a different major version than us");
					throw Peer::NetworkError();
				}

				{
					std::lock_guard<std::mutex> lock(bannedMutex);
					const auto banIt = banned.find(it->first);
					if(banIt != banned.end()) {
						if(banIt->second > static_cast<uint64_t>(std::time(nullptr))) {
							log->printf(LOG_LEVEL_WARN,
										"Network(): Disconnecting " + it->first + " for being banned");
							throw Peer::NetworkError();
						}
					}
				}

				it->second->info["height"] = info["tipHeight"].asUInt64();

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
						log->printf(LOG_LEVEL_INFO, "Network(): Some random network error");
						changeScore(it->first, 10);
						throw Peer::NetworkError();
					}
				}
			} catch(const Json::Exception& e) {
				changeScore(it->first, 50);
				log->printf(LOG_LEVEL_INFO, "Network(): Some other random network error");
				throw Peer::NetworkError();
			}

			const std::time_t result = std::time(nullptr);
			it->second->info["lastseen"] = static_cast<uint64_t>(result);
		} catch(const Peer::NetworkError& e) {
			log->printf(LOG_LEVEL_WARN, e.what());
			log->printf(LOG_LEVEL_WARN,
						"Network(): Failed to contact " + it->first + ", disconnecting it");
			removals.insert(it->first);
		}
	}

	/*for(const auto& peer : removals) {
		const auto it = connected.find(peer);
		peers->put(dbTx.get(), peer, it->second->info);
		if(it != connected.end()) {
			connected.erase(it);
		}
	}*/

	dbTx->commit();
}

void CryptoKernel::Network::networkFunc(std::map<std::string, std::shared_ptr<PeerInfo>>& connected,
										bool& failure,
										std::unique_ptr<std::thread>& blockProcessor,
										uint64_t& currentHeight,
										uint64_t& startHeight) {
	//Determine best chain
	//connectedMutex.lock();
	uint64_t bestHeight = currentHeight;
	for(std::map<std::string, std::shared_ptr<PeerInfo>>::iterator it = connected.begin();
			it != connected.end(); it++) {
		if(it->second->info["height"].asUInt64() > bestHeight) {
			bestHeight = it->second->info["height"].asUInt64();
		}
	}

	{
		std::lock_guard<std::mutex> lock(heightMutex);
		if(this->currentHeight > bestHeight) {
			bestHeight = this->currentHeight;
		}
		this->bestHeight = bestHeight;
	}

	//connectedMutex.unlock();

	log->printf(LOG_LEVEL_INFO,
				"Network(): Current height: " + std::to_string(currentHeight) + ", best height: " +
				std::to_string(bestHeight) + ", start height: " + std::to_string(startHeight));

	bool madeProgress = false;

	//Detect if we are behind
	if(bestHeight > currentHeight) {
		//connectedMutex.lock();

		for(std::map<std::string, std::shared_ptr<PeerInfo>>::iterator it = connected.begin();
				it != connected.end() && running; ) {
			if(it->second->info["height"].asUInt64() > currentHeight) {
				std::list<CryptoKernel::Blockchain::block> blocks;

				const std::string peerUrl = it->first;

				if(currentHeight == startHeight) {
					auto nBlocks = 0;
					do {
						log->printf(LOG_LEVEL_INFO,
									"Network(): Downloading blocks " + std::to_string(currentHeight + 1) + " to " +
									std::to_string(currentHeight + 6));
						try {
							const auto newBlocks = it->second->peer->getBlocks(currentHeight + 1, currentHeight + 6);
							nBlocks = newBlocks.size();
							blocks.insert(blocks.end(), newBlocks.rbegin(), newBlocks.rend());
							if(nBlocks > 0) {
								madeProgress = true;
							} else {
								log->printf(LOG_LEVEL_WARN, "Network(): Peer responded with no blocks");
							}
						} catch(Peer::NetworkError& e) {
							log->printf(LOG_LEVEL_WARN,
										"Network(): Failed to contact " + it->first + " " + e.what() +
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
								changeScore(it->first, 250);
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
						const auto newBlocks = it->second->peer->getBlocks(currentHeight + 1, currentHeight + 6);
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
									"Network(): Failed to contact " + it->first + " " + e.what() +
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

						heightMutex.lock();
						this->currentHeight = currentHeight;
						heightMutex.unlock();

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

		//connectedMutex.unlock();
	}

	if(bestHeight <= currentHeight || connected.size() == 0 || !madeProgress) {
		std::this_thread::sleep_for(std::chrono::milliseconds(20000));
		currentHeight = blockchain->getBlockDB("tip").getHeight();
		startHeight = currentHeight;

		heightMutex.lock();
		this->currentHeight = currentHeight;
		heightMutex.unlock();
	}
}

void CryptoKernel::Network::connectionFunc(std::map<std::string, std::shared_ptr<PeerInfo>>& connected) {
	sf::TcpSocket* client = new sf::TcpSocket();
	if(listener.accept(*client) == sf::Socket::Done) {
		//std::lock_guard<std::recursive_mutex> lock(connectedMutex);
		if(connected.find(client->getRemoteAddress().toString()) != connected.end()) {
			log->printf(LOG_LEVEL_INFO,
						"Network(): Incoming connection duplicates existing connection for " +
						client->getRemoteAddress().toString());
			client->disconnect();
			delete client;
			//continue;
			return;
		}

		{
			std::lock_guard<std::mutex> lock(bannedMutex);
			const auto it = banned.find(client->getRemoteAddress().toString());
			if(it != banned.end()) {
				if(it->second > static_cast<uint64_t>(std::time(nullptr))) {
					log->printf(LOG_LEVEL_INFO,
								"Network(): Incoming connection " + client->getRemoteAddress().toString() + " is banned");
					client->disconnect();
					delete client;
					//continue;
					return;
				}
			}
		}

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
			//continue;
			return;
		}


		log->printf(LOG_LEVEL_INFO,
					"Network(): Peer connected from " + client->getRemoteAddress().toString() + ":" +
					std::to_string(client->getRemotePort()));
		PeerInfo* peerInfo = new PeerInfo();
		peerInfo->peer.reset(new Peer(client, blockchain, this, true));

		Json::Value info;

		try {
			info = peerInfo->peer->getInfo();
		} catch(const Peer::NetworkError& e) {
			log->printf(LOG_LEVEL_WARN, "Network(): Failed to get information from connecting peer");
			delete peerInfo;
			//continue;
			return;
		}

		try {
			peerInfo->info["height"] = info["tipHeight"].asUInt64();
			peerInfo->info["version"] = info["version"].asString();
		} catch(const Json::Exception& e) {
			log->printf(LOG_LEVEL_WARN, "Network(): Incoming peer sent invalid info message");
			delete peerInfo;
			//continue;
			return;
		}

		const std::time_t result = std::time(nullptr);
		peerInfo->info["lastseen"] = static_cast<uint64_t>(result);

		peerInfo->info["score"] = 0;

		connected[client->getRemoteAddress().toString()].reset(peerInfo);

		std::unique_ptr<Storage::Transaction> dbTx(networkdb->begin());
		peers->put(dbTx.get(), client->getRemoteAddress().toString(), peerInfo->info);
		dbTx->commit();
	} else {
		delete client;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

unsigned int CryptoKernel::Network::getConnections() {
    return connections.size();
}

void CryptoKernel::Network::broadcastTransactions(const
        std::vector<CryptoKernel::Blockchain::transaction> transactions) {
    for(std::map<std::string, std::shared_ptr<PeerInfo>>::iterator it = connections.begin();
            it != connections.end(); it++) {
        try {
            it->second->peer->sendTransactions(transactions);
        } catch(CryptoKernel::Network::Peer::NetworkError& err) {
            log->printf(LOG_LEVEL_WARN, "Network::broadcastTransactions(): Failed to contact peer");
        }
    }
}

void CryptoKernel::Network::broadcastBlock(const CryptoKernel::Blockchain::block block) {
    for(std::map<std::string, std::shared_ptr<PeerInfo>>::iterator it = connections.begin();
            it != connections.end(); it++) {
        try {
            it->second->peer->sendBlock(block);
        } catch(CryptoKernel::Network::Peer::NetworkError& err) {
            log->printf(LOG_LEVEL_WARN, "Network::broadcastBlock(): Failed to contact peer");
        }
    }
}

double CryptoKernel::Network::syncProgress() {
    return (double)(currentHeight)/(double)(bestHeight);
}

void CryptoKernel::Network::changeScore(const std::string& url, const uint64_t score) {
    if(connections[url]) {
        connections[url]->info["score"] = connections[url]->info["score"].asUInt64() + score;
        log->printf(LOG_LEVEL_WARN,
                    "Network(): " + url + " misbehaving, increasing ban score by " + std::to_string(
                        score) + " to " + connections[url]->info["score"].asString());
        if(connections[url]->info["score"].asUInt64() > 200) {
            log->printf(LOG_LEVEL_WARN,
                        "Network(): Banning " + url + " for being above the ban score threshold");
            // Ban for 24 hours
            bannedMutex.lock();
            banned[url] = static_cast<uint64_t>(std::time(nullptr)) + 24 * 60 * 60;
            bannedMutex.unlock();
        }
        connections[url]->info["disconnect"] = true;
    }
}

std::set<std::string> CryptoKernel::Network::getConnectedPeers() {
    std::set<std::string> peerUrls;
    for(const auto& peer : connections) {
        peerUrls.insert(peer.first);
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
