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
        return client->send(packet);
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
        listener.listen(1701);
        listenThread.reset(new std::thread(&RaftNet::listen, this));
    }

    void send(std::string addr, unsigned short port, std::string message) {
        sf::Packet packet;
        packet << message;
        auto it = clients.find(addr);
        if(it != clients.end()) {
            if(it->second->send(packet) != sf::Socket::Done) {
                log->printf(LOG_LEVEL_INFO, "RAFT: error sending packet to " + addr);
                clients.erase(addr);
            }
        }
        else {
            sf::TcpSocket* socket = new sf::TcpSocket();
            sf::IpAddress ipAddr(addr);
            if(socket->connect(ipAddr, port, sf::seconds(3))) {
                if(clients.find(addr) == clients.end()) {
                    log->printf(LOG_LEVEL_INFO, "RAFT: Raft connected to " + addr);
                    RaftConnection* connection = new RaftConnection(socket);
                    clients.insert(addr, connection);
                }
                else {
                    log->printf(LOG_LEVEL_INFO, "RAFT: Raft was already connected to " + addr);
                    delete socket;
                }
            }
            else {
                log->printf(LOG_LEVEL_INFO, "RAFT: Failed to connect to " + addr);
                delete socket;
            }
        }
    }

    ~RaftNet() {
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

    void listen() {
        // Create a socket to listen to new connections
        // Create a selector
        sf::SocketSelector selector;
        // Add the listener to the selector
        selector.add(listener);
        // Endless loop that waits for new connections
        while(running) {
            // Make the selector wait for data on any socket
            if (selector.wait()) {
                // Test the listener
                if (selector.isReady(listener)) {
                    // The listener is ready: there is a pending connection
                    sf::TcpSocket* client = new sf::TcpSocket;
                    if (listener.accept(*client) == sf::Socket::Done) {
                        // Add the new client to the clients list
                        std::string addr = client->getRemoteAddress().toString();
                        log->printf(LOG_LEVEL_INFO, "RAFT: Raft received incoming connection from " + addr);
                        if(clients.find(addr) == clients.end()) {
                            clients.insert(client->getRemoteAddress().toString(), new RaftConnection(client));
                            // Add the new client to the selector so that we will
                            // be notified when he sends something
                            selector.add(*client);
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
                        auto it = clients.find(key);
                        if(it != clients.end() && it->second->acquire()) {
                            sf::TcpSocket* client = it->second->get();
                            if(selector.isReady(*client)) {
                                // The client has sent some data, we can receive it
                                sf::Packet packet;
                                if(client->receive(packet) == sf::Socket::Done) {
                                    std::string message;
                                    packet >> message;
                                    log->printf(LOG_LEVEL_INFO, "RAFT: Received packet: " + message);
                                }
                            }   
                        }
                    }
                }
            }
        }
    }
};