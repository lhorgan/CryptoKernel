/*#include <SFML/Network.hpp>

#include "log.h"

class Node {
public:
    CryptoKernel::Log* log;

    Node(CryptoKernel::Log* log) {
        this->log = log;
    }

    void receiveWrapper() {
        log->printf(LOG_LEVEL_INFO, "Noise(): Client (of " + addr + "), receive wrapper starting.");
        bool quitThread = false;

        sf::SocketSelector selector;
        selector.add(*server);

        while(!quitThread && !getHandshakeComplete()) {
            if(selector.wait(sf::seconds(1))) {
                sf::Packet packet;
                const auto status = server->receive(packet);
                if(status == sf::Socket::Done) {
                    receivePacket(packet);
                }
                else {
                    log->printf(LOG_LEVEL_INFO, "Noise(): Client (of " + addr + ") encountered error receiving packet.");
                    quitThread = true;
                }
            }
        }

        selector.remove(*server);
    }

    void receivePacket(sf::Packet& packet) {
    }
};*/