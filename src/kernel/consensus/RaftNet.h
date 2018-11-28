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
                        return;
                    }

        sf::Packet packet;
        packet << message;

        clientMutex.lock();
        auto it = clients.find(addr);
        if(it != clients.end()) {
            if(!it->second) {
                //log->printf(LOG_LEVEL_INFO, "A connection attempt to " + it->first + " is in progress...");
            }
            else {
                sf::Socket::Status res = it->second->send(packet);
                if(res != sf::Socket::Done) {
                    //printf("RAFT: error sending packet to %s\n", addr.c_str());
                    toRemove[addr] = it->second;
                }
                else {
                    //printf("RAFT: Successfully sent message to %s\n", addr.c_str());
                }
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
                //printf("RAFT: Raft connected to %s\n", addr.c_str());
            }
            else {
                //log->printf(LOG_LEVEL_INFO, "RAFT: failed to connect to " + addr);
                clientMutex.lock();
                toRemove[addr] = socket;
                clientMutex.unlock();
            }
        }
    }

    std::vector<std::string> pullMessages() {
        std::vector<std::string> messageQueue;

        messageMutex.lock();
        for(int i = messages.size() - 1; i >= 0; i--) {
            messageQueue.push_back(messages[i]);
        }
        messages.clear();
        messageMutex.unlock();

        return messageQueue;
    }

    ~RaftNet() {
        //printf("RAFT: CLOSING RAFTNET!!!\n");
        running = false;
        listenThread->join();
        receiveThread->join();
        this->clients.clear();
    }

private: 
    std::map <std::string, sf::TcpSocket*> clients;
    std::map <std::string, sf::TcpSocket*> toRemove;

    std::vector<std::string> messages;

    bool running;
    CryptoKernel::Log* log;
    std::unique_ptr<std::thread> listenThread;
    std::unique_ptr<std::thread> receiveThread;

    std::mutex clientMutex;
    std::mutex messageMutex;

    void listen() {
        sf::TcpListener listener;
        listener.listen(1701);

        sf::SocketSelector selector;
        selector.add(listener);

        //printf("RAFT: selector thread started\n");
        while(running) {
            if(selector.wait()) {
                sf::TcpSocket* client = new sf::TcpSocket;
                if(listener.accept(*client) == sf::Socket::Done) {
                    std::string addr = client->getRemoteAddress().toString();
                    //printf("RAFT: Raft received incoming connection from %s\n", addr.c_str());
                    clientMutex.lock();
                    if(clients.find(addr) == clients.end()) {
                        //printf("RAFT: adding %s to client map\n", addr.c_str());
                        clients[addr] = client;
                    }
                    else {
                        //printf("RAFT: %s is an existing address\n", addr.c_str());
                        client->disconnect();
                        delete client;
                    }
                    clientMutex.unlock();
                }
            }
        }

        listener.close();
    }

    void receive() {
        sf::SocketSelector selector;
        std::set<std::string> selectorSet;

        while(running) {
            if(selector.wait(sf::milliseconds(500))) {
                clientMutex.lock();
                for(auto it = clients.begin(); it != clients.end(); it++) {
                    sf::TcpSocket* client = std::get<1>(*it);
                    
                    if(!client || selectorSet.find(it->first) == selectorSet.end()) {
                        continue;
                    }

                    if(selector.isReady(*client)) {
                        // The client has sent some data, we can receive it
                        sf::Packet packet;
                        if(client->receive(packet) == sf::Socket::Done) {
                            std::string message;
                            packet >> message;
                            //log->printf(LOG_LEVEL_INFO, "RAFT: Received packet " + message + " from " + it->first);

                            messageMutex.lock();
                            messages.push_back(message);
                            messageMutex.unlock();
                        }
                        else {
                            //toRemove[it->first] = it->second;
                        }
                    }
                    else {
                        //toRemove[it->first] = it->second; // c1
                    }
                }
                clientMutex.unlock();
            }

            clientMutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++) {
                std::string addr = it->first;
                if(selectorSet.find(addr) == selectorSet.end()) {
                    sf::TcpSocket* socket = it->second;
                    if(socket) {
                        selector.add(*socket);
                        selectorSet.insert(addr);
                    }
                }
            }

            for(auto it = toRemove.begin(); it != toRemove.end(); it++) {
                std::string addr = it->first;
                sf::TcpSocket* client = it->second;

                selectorSet.erase(addr);
                selector.remove(*client);
                clients.erase(addr);
                toRemove.erase(addr);
                
                delete client;
            }
            clientMutex.unlock();
        }
    }
};