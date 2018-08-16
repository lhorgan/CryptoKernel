#ifndef ERC20CONST
#define ERC20CONST

#include <string>
#include "crypto.h"
#include <json/value.h>

using namespace std;

// https://bitcore.io/playground/#/address
string G_PUBLIC_KEY = "BNEOpZ81Oo42ISQEhwid8hvQogv42vTTP7BonJMaEG00dPPO7qjz6HpcKO7d9dM4UkvpvsSI0SbCk+c73hGjDjs=";

Json::Value PRIV_KEY;
PRIV_KEY["iv"] = "8CRCc182Zfuc1zJw8hJz5Q==";
PRIV_KEY["salt"] = "C+CSus2dnpS/lsV9w6gxJuR7Zc+pOvVJlRBii1zfZrA=";
PRIV_KEY["cipherText"] = "zjt4YZ88FqqTNEjmCNmOsUbqn8Yrf5CM5nEF5RIPia0wAfywcHVupFZ1hPj2/zGJ";

CryptoKernel::AES256 G_PRIVATE_KEY(PRIV_KEY);

#endif