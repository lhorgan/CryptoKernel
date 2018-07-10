///*
// * Connection.cpp
// *
// *  Created on: Jul 10, 2018
// *      Author: root
// */
//
//
//#include <cstddef>
//
//#include "connection.h"
//
//CryptoKernel::Network::Connection::Connection() {
//}
//
//bool CryptoKernel::Network::Connection::acquire() {
//	return peerMutex.try_lock();
//}
//
//bool CryptoKernel::Network::Connection::isFree() {
//	return peer.get() != nullptr;
//}
//
//void CryptoKernel::Network::Connection::release() {
//	peerMutex.unlock();
//}
//
//
//CryptoKernel::Network::Connection::~Connection() {
//}
//
