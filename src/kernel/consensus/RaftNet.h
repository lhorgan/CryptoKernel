#include <SFML/Network.hpp>

#include "log.h"

class Sender {
public:
    sf::IpAddress dest;
    unsigned int port;
    bool running;
    bool poisoned;
    bool connected;
    sf::TcpSocket* client;
    std::vector<std::string> messages;
    CryptoKernel::Log* log;

    std::unique_ptr<std::thread> sendThread;
    std::mutex messagesMutex;
    std::mutex poisonedMutex;
    std::mutex connectedMutex;

    Sender(sf::TcpSocket* client, std::string addr, unsigned int port, bool connected, CryptoKernel::Log* log) {
        dest = sf::IpAddress(addr);
        this->port = port;
        poisoned = false;
        running = false;
        this->connected = connected;
        this->log = log;
        this->client = client;

        sendThread.reset(new std::thread(&Sender::sendFunc, this));
    }

    void sendFunc() {
        if(!connected) {
            if(client->connect(dest, port, sf::seconds(3)) == sf::Socket::Done) {
                log->printf(LOG_LEVEL_INFO, "RAFT: Raft connected to " + dest.toString());
                running = true;
                connectedMutex.lock();
                connected = true;
                connectedMutex.unlock();
            }
            else {
                poisonedMutex.lock();
                poisoned = true;
                poisonedMutex.unlock();
            }
        }
        else {
            running = true; // we start off running since we assume we're connected to begin with
        }

        while(running && !poisoned) {
            std::vector<std::string> toSend;

            messagesMutex.lock();
            for(int i = 0; i < messages.size(); i++) {
                toSend.push_back(messages[i]);
            }
            messages.clear();
            messagesMutex.unlock();

            if(toSend.size() > 0) {
                log->printf(LOG_LEVEL_INFO, "Messages in queue for  " + dest.toString());
                Json::Value batched = batchMessages(toSend);
                std::string data = CryptoKernel::Storage::toString(batched);
                sf::Packet packet;
                packet << data;
                if(client->send(packet) != sf::Socket::Done) {
                    log->printf(LOG_LEVEL_INFO, "Error, poisoning " + dest.toString());
                    running = false;
                    poisonedMutex.lock();
                    poisoned = true;
                    poisonedMutex.unlock();
                }
                else {
                    log->printf(LOG_LEVEL_INFO, "Successfully sent message " + data.substr(0, 10) + " to " + dest.toString());
                }
            }
        }
    }

    bool isPoisoned() {
        bool val;
        poisonedMutex.lock();
        val = poisoned;
        poisonedMutex.unlock();
        return val;
    }

    bool isConnected() {
        bool val;
        connectedMutex.lock();
        val = connected;
        connectedMutex.unlock();
        return val;
    }

    void pushMessage(std::string message) {
        messagesMutex.lock();
        messages.push_back(message);
        messagesMutex.unlock();
    }

    Json::Value batchMessages(std::vector<std::string>& messageList) {
        Json::Value batched = Json::arrayValue;
        for(int i = 0; i < messageList.size(); i++) {
            batched.append(messageList[i]);
        }
        return batched;
    }

    ~Sender() {
        running = false;
        sendThread->join();
        delete client;
    }
};

class RaftNet {
public:
    RaftNet(CryptoKernel::Log* log) {
        this->log = log;
        this->publicAddress = sf::IpAddress::getPublicAddress();
        running = true;
        listenThread.reset(new std::thread(&RaftNet::listen, this));
        receiveThread.reset(new std::thread(&RaftNet::receive, this));
        
    }

    void send(std::string addr, unsigned short port, std::string message) {
        sf::IpAddress ipAddr(addr);

        if(ipAddr == sf::IpAddress::getLocalAddress()
                    || ipAddr == sf::IpAddress::LocalHost
                    || ipAddr == sf::IpAddress::None
                    || ipAddr == publicAddress) {
                        return;
                    }

        clientMutex.lock();
        auto it = clients.find(addr);
        if(it == clients.end()) {
            sf::TcpSocket* socket = new sf::TcpSocket();
            Sender* sender = new Sender(socket, addr, port, false, log);
            log->printf(LOG_LEVEL_INFO, "a) Pushing message to " + addr + ": " + message.substr(0, 10));
            sender->pushMessage(message);
            clients[addr] = sender;
        }
        else {
            log->printf(LOG_LEVEL_INFO, "b) Pushing message to " + addr + ": " + message.substr(0, 10));
            it->second->pushMessage(message);
        }
        clientMutex.unlock();
    }

    std::vector<std::string> pullMessages() {
        std::vector<std::string> messageQueue;

        messageMutex.lock();
        for(int i = 0; i < messages.size(); i++) {
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
    std::map <std::string, Sender*> clients;

    std::vector<std::string> messages;
    std::vector<std::string> toSend;

    bool running;
    CryptoKernel::Log* log;
    std::unique_ptr<std::thread> listenThread;
    std::unique_ptr<std::thread> receiveThread;

    std::mutex clientMutex;
    std::mutex messageMutex;
    sf::IpAddress publicAddress;

    void listen() {
        int port = 1701;

        sf::TcpListener listener;
        listener.listen(port);

        sf::SocketSelector selector;
        selector.add(listener);

        while(running) {
            if(selector.wait()) {
                sf::TcpSocket* client = new sf::TcpSocket;
                if(listener.accept(*client) == sf::Socket::Done) {
                    std::string addr = client->getRemoteAddress().toString();
                    printf("RAFT: Raft received incoming connection from %s\n", addr.c_str());
                    clientMutex.lock();
                    if(clients.find(addr) == clients.end()) {
                        printf("RAFT: adding %s to client map\n", addr.c_str());
                        clients[addr] = new Sender(client, addr, port, true, log);
                    }
                    else {
                        printf("RAFT: %s is an existing address\n", addr.c_str());
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
                    sf::TcpSocket* client = std::get<1>(*it)->client;
                    
                    if(selectorSet.find(it->first) == selectorSet.end()) {
                        continue;
                    }

                    if(selector.isReady(*client)) {
                        // The client has sent some data, we can receive it
                        sf::Packet packet;
                        if(client->receive(packet) == sf::Socket::Done) {
                            std::string message;
                            packet >> message;
                            log->printf(LOG_LEVEL_INFO, "RAFT: Received packet " + message + " from " + it->first);

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
            std::vector<std::string> toRemove;
            for(auto it = clients.begin(); it != clients.end(); it++) {
                if(it->second->isPoisoned()) {
                    log->printf(LOG_LEVEL_INFO, "RAFT: marking " + it->first + " for removal");
                    toRemove.push_back(it->first);
                }
            }

            for(int i = 0; i < toRemove.size(); i++) {
                auto it = clients.find(toRemove[i]);
                std::string addr = it->first;
                sf::TcpSocket* client = it->second->client;

                selector.remove(*client);
                selectorSet.erase(addr);
                clients.erase(addr);
                delete it->second;
                //delete client;
            }

            for(auto it = clients.begin(); it != clients.end(); it++) {
                if(selectorSet.find(it->first) == selectorSet.end()) {
                    if(it->second->isConnected()) {
                        log->printf(LOG_LEVEL_INFO, "Adding " + it->first + " to selector");
                        selector.add(*(it->second->client));
                        selectorSet.insert(it->first);
                    }
                }
            }
            clientMutex.unlock();
        }
    }
};