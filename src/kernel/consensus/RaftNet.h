#include <SFML/Network.hpp>

#include "concurrentmap.h"
#include "log.h"

class RaftConnection {
public:
    RaftConnection(sf::TcpSocket* client) {
        std::lock_guard<std::mutex> cml(clientMutex);
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
        std::lock_guard<std::mutex> cml(clientMutex);
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
                        printf("RAFT: Can't send message to self\n");
                        return;
                    }

        sf::Packet packet;
        packet << message;
        auto it = clients.find(addr);
        if(it != clients.end()) {
            sf::Socket::Status res = it->second->send(packet);
            if(res != sf::Socket::Done) {
                printf("RAFT: error sending packet to %s\n", addr.c_str());
                if(res == sf::Socket::Status::Disconnected) {
                    printf("We got disconnected from %s\n", addr.c_str());
                }
                else if(res == sf::Socket::Status::Error) {
                    printf("Some unspecified error %s\n", addr.c_str());
                }
                else if(res == sf::Socket::Status::NotReady) {
                    printf("The address wasn't ready %s\n", addr.c_str());
                }
                else if(res == sf::Socket::Status::Partial) {
                    printf("We got a partial message from %s\n", addr.c_str());
                }
                clients.erase(addr);
            }
            else {
                printf("Successfully sent message to %s\n", addr.c_str());
            }
        }
        else {
            sf::TcpSocket* socket = new sf::TcpSocket();

            if(socket->connect(ipAddr, port, sf::seconds(3)) == sf::Socket::Done) {
                if(!clients.contains(addr)) {
                    printf("RAFT: Raft connected to %s\n", addr.c_str());
                    RaftConnection* connection = new RaftConnection(socket);
                    clients.insert(addr, connection);
                    //this->send(addr, port, message);
                }
                else {
                    printf("RAFT: Raft was already connected to %s\n", addr.c_str());
                    delete socket;
                }
                printf("RAFT: Raft connected to %s\n", addr.c_str());
            }
            else {
                printf("RAFT: Failed to connect to %s\n", addr.c_str());
                delete socket;
            }
        }
    }

    ~RaftNet() {
        printf("RAFT: CLOSING RAFTNET!!!\n");
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

        printf("RAFT: selector thread started\n");

        int i = 0;

        while(running) {
            if(++i % 50 == 0) {
                printf("Still running...\n");
            }
            //printf("Running...\n");
            // Make the selector wait for data on any socket
            if(selector.wait(sf::milliseconds(50))) {
                // Test the listener
                if(selector.isReady(listener)) {
                    //printf("we're ready here\n");
                    // The listener is ready: there is a pending connection
                    sf::TcpSocket* client = new sf::TcpSocket;
                    if (listener.accept(*client) == sf::Socket::Done) {
                        // Add the new client to the clients list
                        std::string addr = client->getRemoteAddress().toString();
                        printf("RAFT: Raft received incoming connection from %s\n", addr.c_str());
                        if(!clients.contains(addr)) {
                            printf("RAFT: adding %s to client map\n", addr.c_str());
                            std::string remoteAddr = client->getRemoteAddress().toString();
                            clients.insert(remoteAddr, new RaftConnection(client));
                            // Add the new client to the selector so that we will
                            // be notified when he sends something
                            selector.add(*client);
                            socketSet.insert(remoteAddr);
                        }
                        else {
                            printf("RAFT: %s is an existing address\n", addr.c_str());
                        }
                    }
                    else {
                        printf("RAFT: Raft didn't accept connection, deleting client\n");
                        // Error, we won't get a new connection, delete the socket
                        delete client;
                    }
                }
                else {
                    // The listener socket is not ready, test all other sockets (the clients)
                    std::vector<std::string> keys = clients.keys();
                    std::random_shuffle(keys.begin(), keys.end());
                    for(std::string key : keys) {
                        //printf("Trying " + key);
                        auto it = clients.find(key);
                        if(it != clients.end()) {
                            if(it->second->acquire()) {
                                sf::TcpSocket* client = it->second->get();
                                if(socketSet.find(key) == socketSet.end()) {
                                    printf("RAFT: We have to add %s to our socket set\n", key);
                                    socketSet.insert(key);
                                    selector.add(*client); // hopefully this doesn't wreak havoc
                                }  
                                else if(selector.isReady(*client)) {
                                    // The client has sent some data, we can receive it
                                    sf::Packet packet;
                                    if(client->receive(packet) == sf::Socket::Done) {
                                        std::string message;
                                        packet >> message;
                                        printf("RAFT: Received packet: %s\n", message.c_str());
                                    }
                                    else {
                                        printf("RAFT: Error receiving packet\n");
                                    }
                                }
                                else {
                                    //printf("RAFT: " + key + " wasn't ready\n");
                                }
                                it->second->release(); 
                            }
                        }
                    }
                }
            }
            else {
                printf("Something is wrong with the selector\n");
            }
        }
    }
};