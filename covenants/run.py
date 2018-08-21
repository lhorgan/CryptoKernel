import requests
import json
import base64
import time

recipient = "BItvGO8GMWDHlyCY4PtzpBkuEV8FJiRbyMHmrGcDBmUH4TiCpYrjdgrwYrk3f1Csrvr10uW74hoiRu5aEpRHHyA="
approved = {"BE6sFoO5Ip0zYLDjrOXBJg3OEyhn14hYfM9mgVg3y5cOD6P4wvoxMJO1KbWMGBTKsZSpS1mHTtPw/Jla6iDVUuE=": True}
destinations = {"BKn1tIWFTannpdKOS37F9sdNu3j5eX1SnyChrYfPNJhQ5FtQ+n2uGLsae9KcZ55/IdkciV7FZSEAoWu40tIwyEA=": True}

def request(method, params):
    url = "http://localhost:8383"
    encoded_str = base64.b64encode("ckrpc:J6PvMA77SOLJ/1uAQvEwikygt9EfajTSqFhhkZvm1Zk=")
    headers = {
        "content-type": "application/json",
        "Authorization": "Basic " + encoded_str}
    payload = {
        "method": method,
        "params": params,
        "jsonrpc": "2.0",
        "id": 0,
    }
    response = requests.post(url, data=json.dumps(payload), headers=headers).json()  
    return response

with open('contract.lua', 'r') as myfile:
        data=myfile.read()
        
contract = request("compilecontract", {"code": data})["result"]

def initialSend():
    print "initial send running..."
    inp = request("listunspentoutputs", {"account": "8339392368704015606_change"})["result"]["outputs"].pop()

    tx = {"inputs":     [{"outputId": inp["id"], "data": {}}], 
          "outputs":    [{"value": inp["value"] - 200000, 
                          "nonce": time.time(), 
                          "data": {"publicKey": recipient,
                                   "contract": contract,
                                   "approved": approved,
                                   "destinations": destinations}}], 
          "timestamp":  time.time()
          }
          
    print("OutputId: " + request("calculateoutputid", {"output": tx["outputs"][0]})["result"])
                     
    return request("signtransaction", {"transaction": tx, "password": "mypassword"})["result"]

def approvedSend(outputId, origin, amount):
    tx = {"inputs":     [{"outputId": outputId, "data": {}}], 
          "outputs":    [{"value": amount - 200000, 
                          "nonce": time.time(), 
                          "data": {"publicKey": "BE6sFoO5Ip0zYLDjrOXBJg3OEyhn14hYfM9mgVg3y5cOD6P4wvoxMJO1KbWMGBTKsZSpS1mHTtPw/Jla6iDVUuE=",
                                   "contract": contract,
                                   "origin": origin}}], 
          "timestamp":  time.time()
          }                

    return request("signtransaction", {"transaction": tx, "password": "mypassword"})["result"]
    

print "starting...."

tx = initialSend()


#tx = approvedSend("8e1c92d8d79f17424e1d75b21b4bc014804ea706e2d8ddc36e7fb8daf3e50154", "8e1c92d8d79f17424e1d75b21b4bc014804ea706e2d8ddc36e7fb8daf3e50154", 8447022574)
print(tx)
print(request("sendrawtransaction", {"transaction": tx}))

#print(request("sendrawtransaction", {"transaction": signed}))



