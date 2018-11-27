#include <SFML/Network.hpp>

#include "log.h"

class RaftNet {
public:
    RaftNet(CryptoKernel::Log* log) {
        this->log = log;
        running = true;
        listenThread.reset(new std::thread(&RaftNet::listen, this));
        receiveThread.reset(new std::thread(&RaftNet::receive, this));
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
                selectorMutex.lock();
                selector.add(*socket);
                selectorMutex.unlock();
                clientMutex.unlock();
                printf("RAFT: Raft connected to %s\n", addr.c_str());
            }
            else {
                log->printf(LOG_LEVEL_INFO, "failed to connct to " + addr);
                clientMutex.lock();
                selectorMutex.lock();
                selector.remove(*socket);
                selectorMutex.unlock();
                clients.erase(addr);
                clientMutex.unlock();
            }
        }
    }

    ~RaftNet() {
        printf("RAFT: CLOSING RAFTNET!!!\n");
        running = false;
        listenThread->join();
        receiveThread->join();
        listener.close();
        this->clients.clear();
    }

private: 
    std::map <std::string, sf::TcpSocket*> clients;
    bool running;
    CryptoKernel::Log* log;
    std::unique_ptr<std::thread> listenThread;
    std::unique_ptr<std::thread> receiveThread;

    sf::TcpListener listener;
    sf::SocketSelector selector;

    std::mutex clientMutex;
    std::mutex selectorMutex;

    void listen() {
        listener.listen(1701);

        selectorMutex.lock();
        selector.add(listener);
        selectorMutex.unlock();

        printf("RAFT: selector thread started\n");
        while(running) {
            sf::TcpSocket* client = new sf::TcpSocket;
            if(listener.accept(*client) == sf::Socket::Done) {
                std::string addr = client->getRemoteAddress().toString();
                printf("RAFT: Raft received incoming connection from %s\n", addr.c_str());
                clientMutex.lock();
                if(clients.find(addr) == clients.end()) {
                    printf("RAFT: adding %s to client map\n", addr.c_str());
                    clients[addr] = client;
                    selectorMutex.lock();
                    selector.add(*client);
                    selectorMutex.unlock();
                }
                else {
                    printf("RAFT: %s is an existing address\n", addr.c_str());
                    client->disconnect();
                    delete client;
                }
                clientMutex.unlock();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void receive() {
        while(running) {
            log->printf(LOG_LEVEL_INFO, "RECEIVE THREAD HUMMING");
            // The listener socket is not ready, test all other sockets (the clients)
            clientMutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++) {
                log->printf(LOG_LEVEL_INFO, "Looking at address " + it->first);
                sf::TcpSocket* client = it->second;
                if(!client) {
                    log->printf(LOG_LEVEL_INFO, "Client for " + it->first + " is null");
                    continue;
                }

                selectorMutex.lock();
                if(selector.isReady(*client)) {
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
                    log->printf(LOG_LEVEL_INFO, "RAFT: Selector wasn't ready for " + it->first);
                }
                selectorMutex.unlock();
            }
            clientMutex.unlock();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};