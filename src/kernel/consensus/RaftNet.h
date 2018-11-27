#include <SFML/Network.hpp>

#include "concurrentmap.h"
#include "log.h"

class RaftConnection {
public:
    RaftConnection(sf::TcpSocket* client) {
        this->client = client;
    }

    bool acquire() {
        return clientMutex.try_lock();
    }

    void release() {
        clientMutex.unlock();
    }

    sf::TcpSocket* get() {
        return this->client;
    }

    sf::Socket::Status send(sf::Packet& packet) {
        std::lock_guard<std::mutex> cml(clientMutex);
        sf::Socket::Status res = client->send(packet);
        return res;
    }

    ~RaftConnection() {
        delete client;
    }

private:
    sf::TcpSocket* client;
    std::mutex clientMutex;
};

class RaftNet {
public:
    RaftNet(CryptoKernel::Log* log) {
        this->log = log;
        running = true;
        listenThread.reset(new std::thread(&RaftNet::listen, this));
    }

    void send(std::string addr, unsigned short port, std::string message) {
        sf::IpAddress ipAddr(addr);

        if(ipAddr == sf::IpAddress::getLocalAddress()
                    || ipAddr == sf::IpAddress::LocalHost
                    || ipAddr == sf::IpAddress::None
                    || ipAddr == sf::IpAddress::getPublicAddress()) {
                        log->printf(LOG_LEVEL_INFO, "RAFT: Can't send message to self");
                        return;
                    }

        sf::Packet packet;
        packet << message;
        auto it = clients.find(addr);
        if(it != clients.end()) {
            sf::Socket::Status res = it->second->send(packet);
            if(res != sf::Socket::Done) {
                log->printf(LOG_LEVEL_INFO, "RAFT: error sending packet to " + addr);
                if(res == sf::Socket::Status::Disconnected) {
                    log->printf(LOG_LEVEL_INFO, "We got disconnected from " + addr);
                }
                else if(res == sf::Socket::Status::Error) {
                    log->printf(LOG_LEVEL_INFO, "Some unspecified error " + addr);
                }
                else if(res == sf::Socket::Status::NotReady) {
                    log->printf(LOG_LEVEL_INFO, "The address wasn't ready " + addr);
                }
                else if(res == sf::Socket::Status::Partial) {
                    log->printf(LOG_LEVEL_INFO, "We got a partial message from " + addr);
                }
                clients.erase(addr);
            }
            else {
                log->printf(LOG_LEVEL_INFO, "Successfully sent message to " + addr);
            }
        }
        else {
            sf::TcpSocket* socket = new sf::TcpSocket();

            if(socket->connect(ipAddr, port, sf::seconds(3)) == sf::Socket::Done) {
                if(!clients.contains(addr)) {
                    log->printf(LOG_LEVEL_INFO, "RAFT: Raft connected to " + addr);
                    RaftConnection* connection = new RaftConnection(socket);
                    clients.insert(addr, connection);
                    //this->send(addr, port, message);
                }
                else {
                    log->printf(LOG_LEVEL_INFO, "RAFT: Raft was already connected to " + addr);
                    delete socket;
                }
                log->printf(LOG_LEVEL_INFO, "RAFT: Raft connected to " + addr);
            }
            else {
                log->printf(LOG_LEVEL_INFO, "RAFT: Failed to connect to " + addr);
                delete socket;
            }
        }
    }

    ~RaftNet() {
        log->printf(LOG_LEVEL_INFO, "RAFT: CLOSING RAFTNET!!!");
        running = false;
        listener.close();
        listenThread->join();
        this->clients.clear();
    }

private:
    ConcurrentMap <std::string, RaftConnection*> clients;
    bool running;
    CryptoKernel::Log* log;
    std::unique_ptr<std::thread> listenThread;
    sf::TcpListener listener;

    std::set<std::string> socketSet;

    void listen() {
        listener.listen(1701);
        // Create a socket to listen to new connections
        // Create a selector
        sf::SocketSelector selector;
        // Add the listener to the selector
        selector.add(listener);
        // Endless loop that waits for new connections

        log->printf(LOG_LEVEL_INFO, "RAFT: selector thread started");

        int i = 0;

        while(running) {
            if(++i % 10000 == 0) {
                log->printf(LOG_LEVEL_INFO, "Still running...");
            }
            //log->printf(LOG_LEVEL_INFO, "Running...");
            // Make the selector wait for data on any socket
            if(selector.wait()) {
                // Test the listener
                if(selector.isReady(listener)) {
                    //log->printf(LOG_LEVEL_INFO, "we're ready here");
                    // The listener is ready: there is a pending connection
                    sf::TcpSocket* client = new sf::TcpSocket;
                    if (listener.accept(*client) == sf::Socket::Done) {
                        // Add the new client to the clients list
                        std::string addr = client->getRemoteAddress().toString();
                        log->printf(LOG_LEVEL_INFO, "RAFT: Raft received incoming connection from " + addr);
                        if(!clients.contains(addr)) {
                            log->printf(LOG_LEVEL_INFO, "RAFT: adding " + addr + " to client map");
                            std::string remoteAddr = client->getRemoteAddress().toString();
                            clients.insert(remoteAddr, new RaftConnection(client));
                            // Add the new client to the selector so that we will
                            // be notified when he sends something
                            selector.add(*client);
                            socketSet.insert(remoteAddr);
                        }
                        else {
                            log->printf(LOG_LEVEL_INFO, "RAFT: " + addr + " is an existing address");
                        }
                    }
                    else {
                        log->printf(LOG_LEVEL_INFO, "RAFT: Raft didn't accept connection, deleting client");
                        // Error, we won't get a new connection, delete the socket
                        delete client;
                    }
                }
                else {
                    // The listener socket is not ready, test all other sockets (the clients)
                    std::vector<std::string> keys = clients.keys();
                    std::random_shuffle(keys.begin(), keys.end());
                    for(std::string key : keys) {
                        //log->printf(LOG_LEVEL_INFO, "Trying " + key);
                        auto it = clients.find(key);
                        if(it != clients.end()) {
                            if(it->second->acquire()) {
                                sf::TcpSocket* client = it->second->get();
                                if(socketSet.find(key) == socketSet.end()) {
                                    log->printf(LOG_LEVEL_INFO, "RAFT: We have to add " + key  + " to our socket set");
                                    socketSet.insert(key);
                                    selector.add(*client); // hopefully this doesn't wreak havoc
                                }  
                                else if(selector.isReady(*client)) {
                                    // The client has sent some data, we can receive it
                                    sf::Packet packet;
                                    if(client->receive(packet) == sf::Socket::Done) {
                                        std::string message;
                                        packet >> message;
                                        log->printf(LOG_LEVEL_INFO, "RAFT: Received packet: " + message);
                                    }
                                    else {
                                        log->printf(LOG_LEVEL_INFO, "RAFT: Error receiving packet");
                                    }
                                }
                                else {
                                    //log->printf(LOG_LEVEL_INFO, "RAFT: " + key + " wasn't ready");
                                }
                                it->second->release(); 
                            }
                        }
                    }
                }
            }
            else {
                log->printf(LOG_LEVEL_INFO, "Something is wrong with the selector");
            }
        }
    }
};