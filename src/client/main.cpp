/*  CryptoCurrency - An example crypto-currency using the CryptoKernel library as a base
    Copyright (C) 2016  James Lovejoy

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <thread>
#include <iostream>
#include <stdlib.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <csignal>

#include <jsonrpccpp/client/connectors/httpclient.h>

#include <openssl/rand.h>

#include "../erc20/wallet.h"

bool running;

int main(int argc, char* argv[]) {
    ERC20Wallet* wallet = new ERC20Wallet;
}
