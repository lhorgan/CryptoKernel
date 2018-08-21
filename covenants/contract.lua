--[[ Get the output associated with the input we're verifying ]]
local output = Blockchain.getOutput(thisInput["outputId"])

--[[ Check the public key is valid ]]
local crypto = Crypto.new()
if not crypto:setPublicKey(output["data"]["publicKey"]) then
    return "Invalid public key"
end

--[[ Verify the signature is correct ]]
if not crypto:verify(thisInput["outputId"] .. outputSetId, thisInput["data"]["signature"]) then
    return "Invalid signature"
end

local dataOutput = nil
local dataOutputId = nil
--[[ this is the first tx in the chain, data output is us ]]
if output["data"]["origin"] == nil then
    dataOutputId = thisInput["outputId"]
    dataOutput = output
else
    dataOutputId = output["data"]["origin"]
    dataOutput = Blockchain.getOutput(output["data"]["origin"])
end

--[[ Ensure each of the outputs are sending to approved pubkeys,
     carry over the origin and contract ]]
for _, out in ipairs(thisTransaction["outputs"]) do
    if dataOutput["data"]["destinations"][out["data"]["publicKey"]] ~= true then
        --[[ originID ]]
        if out["data"]["origin"] ~= dataOutputId then
            return "Invalid origin ID"
        end
        
        --[[ contract propagation ]]
        if out["data"]["contract"] ~= output["data"]["contract"] then
            return "Contract not propagated"
        end
        
        --[[ approved pubkey ]]
        if dataOutput["data"]["approved"][out["data"]["publicKey"]] ~= true then
            return "Recipient is not approved"
        end
    end
end

return true
