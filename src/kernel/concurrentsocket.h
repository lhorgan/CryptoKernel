#ifndef SRC_KERNEL_CONCURRENTSOCKETH_H_
#define SRC_KERNEL_CONCURRENTSOCKETH_H_

#include <SFML/Network.hpp>
#include <mutex>

class ConcurrentSocket {
public:
    ConcurrentSocket(sf::TcpSocket* socket) {
        std::lock_guard<std::mutex> sm(socketMutex); // don't need this, but eh
        this->socket = socket;
    }

    sf::Socket::Status send(sf::Packet& packet) {
        std::lock_guard<std::mutex> sm(socketMutex);
        socket->send(packet);
    }

    sf::Socket::Status receive(sf::Packet& packet) {
        std::lock_guard<std::mutex> sm(socketMutex);
        socket->receive(packet);
    }

    sf::IpAddress getRemoteAddress() {
        std::lock_guard<std::mutex> sm(socketMutex);
        return socket->getRemoteAddress();
    }

    void setSocket(sf::TcpSocket* socket) {
        std::lock_guard<std::mutex> sm(socketMutex);
        oldSockets.push_back(this->socket);
        this->socket = socket;
    }
    
    void acquire() {
        socketMutex.lock();
    }

    void release() {
        socketMutex.unlock();
    }

    sf::TcpSocket* getSocket() {
        return socket;
    }

    void disconnect() {
        std::lock_guard<std::mutex> sm(socketMutex);
        socket->disconnect();
    }

    ~ConcurrentSocket() {
        std::lock_guard<std::mutex> sm(socketMutex);
        for(unsigned int i = 0; i < oldSockets.size(); i++) {
            if(oldSockets[i]) {
                oldSockets[i]->disconnect();
                delete oldSockets[i];
            }
        }
        delete socket;
    }

private:
    std::mutex socketMutex;
    sf::TcpSocket* socket;
    std::vector<sf::TcpSocket*> oldSockets;
};

#endif