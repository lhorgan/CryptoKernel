#include <SFML/Network.hpp>

#include "log.h"

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

        clientMutex.lock();
        auto it = clients.find(addr);
        if(it != clients.end()) {
            sf::Socket::Status res = it->second->send(packet);
            if(res != sf::Socket::Done) {
                printf("RAFT: error sending packet to %s\n", addr.c_str());
                clients.erase(addr);
            }
            else {
                printf("Successfully sent message to %s\n", addr.c_str());
            }

            clientMutex.unlock();
        }
        else {
            sf::TcpSocket* socket = new sf::TcpSocket();
            clients[addr] = NULL;
            clientMutex.unlock();

            if(socket->connect(ipAddr, port, sf::seconds(3)) == sf::Socket::Done) {
                clientMutex.lock();
                clients[addr] = socket;
                clientMutex.unlock();
                printf("RAFT: Raft connected to %s\n", addr.c_str());
            }
            else {
                log->printf(LOG_LEVEL_INFO, "failed to connct to " + addr);
                clientMutex.lock();
                clients.erase(addr);
                clientMutex.unlock();
            }
        }
    }

    ~RaftNet() {
        printf("RAFT: CLOSING RAFTNET!!!\n");
        running = false;
        listenThread->join();
        listener.close();
        this->clients.clear();
    }

private: 
    std::map <std::string, sf::TcpSocket*> clients;
    bool running;
    CryptoKernel::Log* log;
    std::unique_ptr<std::thread> listenThread;
    sf::TcpListener listener;

    std::set<std::string> socketSet;
    std::mutex clientMutex;

    void listen() {
        listener.listen(1701);
        sf::SocketSelector selector;
        selector.add(listener);

        printf("RAFT: selector thread started\n");
        while(running) {
            if(selector.wait(sf::milliseconds(300))) {
                // Test the listener
                if(selector.isReady(listener)) {
                    sf::TcpSocket* client = new sf::TcpSocket;
                    if(listener.accept(*client) == sf::Socket::Done) {
                        std::string addr = client->getRemoteAddress().toString();
                        printf("RAFT: Raft received incoming connection from %s\n", addr.c_str());
                        clientMutex.lock();
                        if(clients.find(addr) == clients.end()) {
                            printf("RAFT: adding %s to client map\n", addr.c_str());
                            clients[addr] = client;
                            selector.add(*client);
                            socketSet.insert(addr);
                        }
                        else {
                            printf("RAFT: %s is an existing address\n", addr.c_str());
                        }
                        clientMutex.unlock();
                    }
                    else {
                        printf("RAFT: Raft didn't accept connection, deleting client\n");
                        // Error, we won't get a new connection, delete the socket
                        delete client;
                    }
                }
                else {
                    // The listener socket is not ready, test all other sockets (the clients)
                    clientMutex.lock();
                    for(auto it = clients.begin(); it != clients.end(); it++) {
                        sf::TcpSocket* client = it->second;
                        if(socketSet.find(it->first) == socketSet.end()) {
                            printf("RAFT: We have to add %s to our socket set\n", it->first);
                            socketSet.insert(it->first);
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
                    }
                    clientMutex.unlock();
                }
            }
        }
    }
};