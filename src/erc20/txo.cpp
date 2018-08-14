#include "txo.h"

Txo::Txo(const std::string& id, const uint64_t value) {
    this->id = id;
    this->spent = false;
    this->value = value;
}

Txo::Txo(const Json::Value& txoJson) {
    id = txoJson["id"].asString();
    spent = txoJson["spent"].asBool();
    value = txoJson["value"].asUInt64();
}

Json::Value Txo::toJson() const {
    Json::Value returning;

    returning["id"] = id;
    returning["spent"] = spent;
    returning["value"] = value;

    return returning;
}

void Txo::spend() {
    spent = true;
}

bool Txo::isSpent() const {
    return spent;
}

uint64_t Txo::getValue() const {
    return value;
}