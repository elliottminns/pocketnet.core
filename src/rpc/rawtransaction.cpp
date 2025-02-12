// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Pocketcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <coins.h>
#include <compat/byteswap.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <index/txindex.h>
#include <key_io.h>
#include <keystore.h>
#include <merkleblock.h>
#include <net.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <primitives/rtransaction.h>
#include <primitives/transaction.h>
#include <rpc/rawtransaction.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/standard.h>
#include <txmempool.h>
#include <uint256.h>
#include <utilstrencodings.h>
#include <validation.h>
#include <validationinterface.h>

#include <future>
#include <stdint.h>

#include <univalue.h>

#include "antibot/antibot.h"
#include "html.h"
#include "index/addrindex.h"

static void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    // Call into TxToUniv() in pocketcoin-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in pocketcoin-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry, true, RPCSerializationFlags());

    entry.pushKV("pockettx", g_addrindex->IsPocketnetTransaction(tx));

    if (!hashBlock.IsNull()) {
        LOCK(cs_main);

        entry.pushKV("blockhash", hashBlock.GetHex());
        CBlockIndex* pindex = LookupBlockIndex(hashBlock);
        if (pindex) {
            if (chainActive.Contains(pindex)) {
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            } else
                entry.pushKV("confirmations", 0);
        }
    }
}

static UniValue getrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose \"blockhash\" )\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions. If the block which contains the transaction\n"
            "is known, its hash can be provided even for nodes without -txindex. Note that if a blockhash is\n"
            "provided, only that block will be searched and if the transaction is in the mempool or other\n"
            "blocks, or if this node does not have the given block available, the transaction will not be found.\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose     (bool, optional, default=false) If false, return a string, otherwise return a json object\n"
            "3. \"blockhash\" (string, optional) The block in which to look for the transaction\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"in_active_chain\": b, (bool) Whether specified block is in the active chain or not (only present with explicit \"blockhash\" argument)\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"weight\" : n,           (numeric) The transaction's weight (between vsize*4-3 and vsize*4)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT + "\n"
                            "       \"n\" : n,                    (numeric) index\n"
                            "       \"scriptPubKey\" : {          (json object)\n"
                            "         \"asm\" : \"asm\",          (string) the asm\n"
                            "         \"hex\" : \"hex\",          (string) the hex\n"
                            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
                            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
                            "         \"addresses\" : [           (json array of string)\n"
                            "           \"address\"        (string) pocketcoin address\n"
                            "           ,...\n"
                            "         ]\n"
                            "       }\n"
                            "     }\n"
                            "     ,...\n"
                            "  ],\n"
                            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
                            "  \"confirmations\" : n,      (numeric) The confirmations\n"
                            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
                            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
                            "}\n"

                            "\nExamples:\n" +
            HelpExampleCli("getrawtransaction", "\"mytxid\"") + HelpExampleCli("getrawtransaction", "\"mytxid\" true") + HelpExampleRpc("getrawtransaction", "\"mytxid\", true") + HelpExampleCli("getrawtransaction", "\"mytxid\" false \"myblockhash\"") + HelpExampleCli("getrawtransaction", "\"mytxid\" true \"myblockhash\""));

    bool in_active_chain = true;
    uint256 hash = ParseHashV(request.params[0], "parameter 1");
    CBlockIndex* blockindex = nullptr;

    if (hash == Params().GenesisBlock().hashMerkleRoot) {
        // Special exception for the genesis block coinbase transaction
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "The genesis block coinbase is not considered an ordinary transaction and cannot be retrieved");
    }

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        fVerbose = request.params[1].isNum() ? (request.params[1].get_int() != 0) : request.params[1].get_bool();
    }

    if (!request.params[2].isNull()) {
        LOCK(cs_main);

        uint256 blockhash = ParseHashV(request.params[2], "parameter 3");
        blockindex = LookupBlockIndex(blockhash);
        if (!blockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
        }
        in_active_chain = chainActive.Contains(blockindex);
    }

    bool f_txindex_ready = false;
    if (g_txindex && !blockindex) {
        f_txindex_ready = g_txindex->BlockUntilSyncedToCurrentChain();
    }

    CTransactionRef tx;
    uint256 hash_block;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hash_block, true, blockindex)) {
        std::string errmsg;
        if (blockindex) {
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        } else if (!g_txindex) {
            errmsg = "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
        } else if (!f_txindex_ready) {
            errmsg = "No such mempool transaction. Blockchain transactions are still in the process of being indexed";
        } else {
            errmsg = "No such mempool or blockchain transaction";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    if (!fVerbose) {
        return EncodeHexTx(*tx, RPCSerializationFlags());
    }

    UniValue result(UniValue::VOBJ);
    if (blockindex) result.pushKV("in_active_chain", in_active_chain);
    TxToJSON(*tx, hash_block, result);
    return result;
}

static UniValue gettxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included manually (by blockhash).\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n");

    std::set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = request.params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid txid ") + txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated txid: ") + txid.get_str());
        setTxids.insert(hash);
        oneTxid = hash;
    }

    CBlockIndex* pblockindex = nullptr;
    uint256 hashBlock;
    if (!request.params[1].isNull()) {
        LOCK(cs_main);
        hashBlock = uint256S(request.params[1].get_str());
        pblockindex = LookupBlockIndex(hashBlock);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    } else {
        LOCK(cs_main);

        // Loop through txids and try to find which block they're in. Exit loop once a block is found.
        for (const auto& tx : setTxids) {
            const Coin& coin = AccessByTxid(*pcoinsTip, tx);
            if (!coin.IsSpent()) {
                pblockindex = chainActive[coin.nHeight];
                break;
            }
        }
    }


    // Allow txindex to catch up if we need to query it and before we acquire cs_main.
    if (g_txindex && !pblockindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    LOCK(cs_main);

    if (pblockindex == nullptr) {
        CTransactionRef tx;
        if (!GetTransaction(oneTxid, tx, Params().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        pblockindex = LookupBlockIndex(hashBlock);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        }
    }

    CBlock block;
    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto& tx : block.vtx)
        if (setTxids.count(tx->GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not all transactions found in specified or retrieved block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

static UniValue verifytxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof can not be validated.\n");

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);

    const CBlockIndex* pindex = LookupBlockIndex(merkleBlock.header.GetHash());
    if (!pindex || !chainActive.Contains(pindex) || pindex->nTx == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");
    }

    // Check if proof is valid, only add results if so
    if (pindex->nTx == merkleBlock.txn.GetNumTransactions()) {
        for (const uint256& hash : vMatch) {
            res.push_back(hash.GetHex());
        }
    }

    return res;
}

CMutableTransaction ConstructTransaction(const UniValue& inputs_in, const UniValue& outputs_in, const UniValue& locktime, const UniValue& rbf)
{
    if (inputs_in.isNull() || outputs_in.isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = inputs_in.get_array();
    const bool outputs_is_obj = outputs_in.isObject();
    UniValue outputs = outputs_is_obj ? outputs_in.get_obj() : outputs_in.get_array();

    CMutableTransaction rawTx;

    if (!locktime.isNull()) {
        int64_t nLockTime = locktime.get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

    bool rbfOptIn = rbf.isTrue();

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence;
        if (rbfOptIn) {
            nSequence = MAX_BIP125_RBF_SEQUENCE;
        } else if (rawTx.nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set<CTxDestination> destinations;
    if (!outputs_is_obj) {
        // Translate array of key-value pairs into dict
        UniValue outputs_dict = UniValue(UniValue::VOBJ);
        for (size_t i = 0; i < outputs.size(); ++i) {
            const UniValue& output = outputs[i];
            if (!output.isObject()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, key-value pair not an object as expected");
            }
            if (output.size() != 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, key-value pair must contain exactly one key");
            }
            outputs_dict.pushKVs(output);
        }
        outputs = std::move(outputs_dict);
    }
    for (const std::string& name_ : outputs.getKeys()) {
        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(outputs[name_].getValStr(), "Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination = DecodeDestination(name_);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + name_);
            }

            if (!destinations.insert(destination).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }

            CScript scriptPubKey = GetScriptForDestination(destination);
            CAmount nAmount = AmountFromValue(outputs[name_]);

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    if (!rbf.isNull() && rawTx.vin.size() > 0 && rbfOptIn != SignalsOptInRBF(rawTx)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter combination: Sequence number(s) contradict replaceable option");
    }

    return rawTx;
}

static UniValue createrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4) {
        throw std::runtime_error(
            // clang-format off
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] [{\"address\":amount},{\"data\":\"hex\"},...] ( locktime ) ( replaceable )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs can be addresses or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",      (string, required) The transaction id\n"
            "         \"vout\":n,         (numeric, required) The output number\n"
            "         \"sequence\":n      (numeric, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (array, required) a json array with outputs (key-value pairs)\n"
            "   [\n"
            "    {\n"
            "      \"address\": x.xxx,    (obj, optional) A key-value pair. The key (string) is the pocketcoin address, the value (float or string) is the amount in " + CURRENCY_UNIT + "\n"
            "    },\n"
            "    {\n"
            "      \"data\": \"hex\"        (obj, optional) A key-value pair. The key must be \"data\", the value is hex encoded data\n"
            "    }\n"
            "    ,...                     More key-value pairs of the above form. For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
            "                             accepted as second parameter.\n"
            "   ]\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
            "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
            "                             Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible.\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"address\\\":0.01}]\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"[{\\\"address\\\":0.01}]\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
            // clang-format on
        );
    }

    RPCTypeCheck(request.params, {UniValue::VARR,
                                     UniValueType(), // ARR or OBJ, checked later
                                     UniValue::VNUM, UniValue::VBOOL},
        true);

    CMutableTransaction rawTx = ConstructTransaction(request.params[0], request.params[1], request.params[2], request.params[3]);

    return EncodeHexTx(rawTx);
}

static UniValue decoderawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\" ( iswitness )\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"
            "2. iswitness          (boolean, optional) Whether the transaction hex is a serialized witness transaction\n"
            "                         If iswitness is not present, heuristic tests will be used in decoding\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"weight\" : n,           (numeric) The transaction's weight (between vsize*4 - 3 and vsize*4)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT + "\n"
                            "       \"n\" : n,                    (numeric) index\n"
                            "       \"scriptPubKey\" : {          (json object)\n"
                            "         \"asm\" : \"asm\",          (string) the asm\n"
                            "         \"hex\" : \"hex\",          (string) the hex\n"
                            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
                            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
                            "         \"addresses\" : [           (json array of string)\n"
                            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) pocketcoin address\n"
                            "           ,...\n"
                            "         ]\n"
                            "       }\n"
                            "     }\n"
                            "     ,...\n"
                            "  ],\n"
                            "}\n"

                            "\nExamples:\n" +
            HelpExampleCli("decoderawtransaction", "\"hexstring\"") + HelpExampleRpc("decoderawtransaction", "\"hexstring\""));

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    CMutableTransaction mtx;

    bool try_witness = request.params[1].isNull() ? true : request.params[1].get_bool();
    bool try_no_witness = request.params[1].isNull() ? true : !request.params[1].get_bool();

    if (!DecodeHexTx(mtx, request.params[0].get_str(), try_no_witness, try_witness)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    UniValue result(UniValue::VOBJ);
    TxToUniv(CTransaction(std::move(mtx)), uint256(), result, false);

    return result;
}

static UniValue decodescript(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) pocketcoin address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) address of P2SH script wrapping this redeem script (not returned if the script is already a P2SH).\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("decodescript", "\"hexstring\"") + HelpExampleRpc("decodescript", "\"hexstring\""));

    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0) {
        std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToUniv(script, r, false);

    UniValue type;
    type = find_value(r, "type");

    if (type.isStr() && type.get_str() != "scripthash") {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.pushKV("p2sh", EncodeDestination(CScriptID(script)));
        // P2SH and witness programs cannot be wrapped in P2WSH, if this script
        // is a witness program, don't return addresses for a segwit programs.
        if (type.get_str() == "pubkey" || type.get_str() == "pubkeyhash" || type.get_str() == "multisig" || type.get_str() == "nonstandard") {
            std::vector<std::vector<unsigned char>> solutions_data;
            txnouttype which_type = Solver(script, solutions_data);
            // Uncompressed pubkeys cannot be used with segwit checksigs.
            // If the script contains an uncompressed pubkey, skip encoding of a segwit program.
            if ((which_type == TX_PUBKEY) || (which_type == TX_MULTISIG)) {
                for (const auto& solution : solutions_data) {
                    if ((solution.size() != 1) && !CPubKey(solution).IsCompressed()) {
                        return r;
                    }
                }
            }
            UniValue sr(UniValue::VOBJ);
            CScript segwitScr;
            if (which_type == TX_PUBKEY) {
                segwitScr = GetScriptForDestination(WitnessV0KeyHash(Hash160(solutions_data[0].begin(), solutions_data[0].end())));
            } else if (which_type == TX_PUBKEYHASH) {
                segwitScr = GetScriptForDestination(WitnessV0KeyHash(solutions_data[0]));
            } else {
                // Scripts that are not fit for P2WPKH are encoded as P2WSH.
                // Newer segwit program versions should be considered when then become available.
                segwitScr = GetScriptForDestination(WitnessV0ScriptHash(script));
            }
            ScriptPubKeyToUniv(segwitScr, sr, true);
            sr.pushKV("p2sh-segwit", EncodeDestination(CScriptID(segwitScr)));
            r.pushKV("segwit", sr);
        }
    }

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hash.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    UniValue witness(UniValue::VARR);
    for (unsigned int i = 0; i < txin.scriptWitness.stack.size(); i++) {
        witness.push_back(HexStr(txin.scriptWitness.stack[i].begin(), txin.scriptWitness.stack[i].end()));
    }
    entry.pushKV("witness", witness);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

static UniValue combinerawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinerawtransaction [\"hexstring\",...]\n"
            "\nCombine multiple partially signed transactions into one transaction.\n"
            "The combined transaction may be another partially signed transaction or a \n"
            "fully signed transaction."

            "\nArguments:\n"
            "1. \"txs\"         (string) A json array of hex strings of partially signed transactions\n"
            "    [\n"
            "      \"hexstring\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "\"hex\"            (string) The hex-encoded raw transaction with signature(s)\n"

            "\nExamples:\n" +
            HelpExampleCli("combinerawtransaction", "[\"myhex1\", \"myhex2\", \"myhex3\"]"));


    UniValue txs = request.params[0].get_array();
    std::vector<CMutableTransaction> txVariants(txs.size());

    for (unsigned int idx = 0; idx < txs.size(); idx++) {
        if (!DecodeHexTx(txVariants[idx], txs[idx].get_str(), true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed for tx %d", idx));
        }
    }

    if (txVariants.empty()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transactions");
    }

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache& viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mergedTx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Input not found or already spent");
        }
        SignatureData sigdata;

        // ... and merge in other signatures:
        for (const CMutableTransaction& txv : txVariants) {
            if (txv.vin.size() > i) {
                sigdata.MergeSignatureData(DataFromTransaction(txv, i, coin.out));
            }
        }
        ProduceSignature(DUMMY_SIGNING_PROVIDER, MutableTransactionSignatureCreator(&mergedTx, i, coin.out.nValue, 1), coin.out.scriptPubKey, sigdata);

        UpdateInput(txin, sigdata);
    }

    return EncodeHexTx(mergedTx);
}

UniValue SignTransaction(CMutableTransaction& mtx, const UniValue& prevTxsUnival, CBasicKeyStore* keystore, bool is_temp_keystore, const UniValue& hashType)
{
    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK2(cs_main, mempool.cs);
        CCoinsViewCache& viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Add previous txouts given in the RPC call:
    if (!prevTxsUnival.isNull()) {
        UniValue prevTxs = prevTxsUnival.get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); ++idx) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject()) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");
            }

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");
            }

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin& coin = view.AccessCoin(out);
                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n" +
                          ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = MAX_MONEY;
                if (prevOut.exists("amount")) {
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }

            // if redeemScript and private keys were given, add redeemScript to the keystore so it can be signed
            if (is_temp_keystore && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    keystore->AddCScript(redeemScript);
                    // Automatically also add the P2WSH wrapped version of the script (to deal with P2SH-P2WSH).
                    keystore->AddCScript(GetScriptForWitness(redeemScript));
                }
            }
        }
    }

    int nHashType = ParseSighashString(hashType);

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata = DataFromTransaction(mtx, i, coin.out);
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size())) {
            ProduceSignature(*keystore, MutableTransactionSignatureCreator(&mtx, i, amount, nHashType), prevPubKey, sigdata);
        }

        UpdateInput(txin, sigdata);

        // amount must be specified for valid segwit signature
        if (amount == MAX_MONEY && !txin.scriptWitness.IsNull()) {
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing amount for %s", coin.out.ToString()));
        }

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
            } else {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(mtx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }

    return result;
}

static UniValue signrawtransactionwithkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "signrawtransactionwithkey \"hexstring\" [\"privatekey1\",...] ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second argument is an array of base58-encoded private\n"
            "keys that will be the only keys used to sign the transaction.\n"
            "The third optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"

            "\nArguments:\n"
            "1. \"hexstring\"                      (string, required) The transaction hex string\n"
            "2. \"privkeys\"                       (string, required) A json array of base58-encoded private keys for signing\n"
            "    [                               (json array of strings)\n"
            "      \"privatekey\"                  (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "3. \"prevtxs\"                        (string, optional) An json array of previous dependent transaction outputs\n"
            "     [                              (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",               (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",     (string, required) script key\n"
            "         \"redeemScript\": \"hex\",     (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "4. \"sighashtype\"                    (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",                  (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,          (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                      (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",              (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                   (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",          (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,               (numeric) Script sequence number\n"
            "      \"error\" : \"text\"              (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("signrawtransactionwithkey", "\"myhex\"") + HelpExampleRpc("signrawtransactionwithkey", "\"myhex\""));

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    CBasicKeyStore keystore;
    const UniValue& keys = request.params[1].get_array();
    for (unsigned int idx = 0; idx < keys.size(); ++idx) {
        UniValue k = keys[idx];
        CKey key = DecodeSecret(k.get_str());
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
        }
        keystore.AddKey(key);
    }

    return SignTransaction(mtx, request.params[2], &keystore, true, request.params[3]);
}

UniValue signrawtransaction(const JSONRPCRequest& request)
{
    // This method should be removed entirely in V0.19, along with the entries in the
    // CRPCCommand table and rpc/client.cpp.
    throw JSONRPCError(RPC_METHOD_DEPRECATED, "signrawtransaction was removed in v0.18.\n"
                                              "Clients should transition to using signrawtransactionwithkey and signrawtransactionwithwallet");
}

static UniValue _sendrawtransaction(RTransaction& rtx)
{
    const uint256& hashTx = rtx->GetHash();

    std::promise<void> promise;
    CAmount nMaxRawTxFee = maxTxFee;

    { // cs_main scope
        LOCK(cs_main);
        CCoinsViewCache& view = *pcoinsTip;
        bool fHaveChain = false;
        for (size_t o = 0; !fHaveChain && o < rtx->vout.size(); o++) {
            const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
            fHaveChain = !existingCoin.IsSpent();
        }
        bool fHaveMempool = mempool.exists(hashTx);
        if (!fHaveMempool && !fHaveChain) {
            // push to local node and sync with wallets
            CValidationState state;
            bool fMissingInputs;
            if (!AcceptToMemoryPool(mempool, state, rtx, &fMissingInputs, nullptr /* plTxnReplaced */, false /* bypass_limits */, nMaxRawTxFee)) {
                if (state.IsInvalid()) {
                    throw JSONRPCError(RPC_TRANSACTION_REJECTED, FormatStateMessage(state));
                } else {
                    if (state.GetRejectCode() == RPC_POCKETTX_MATURITY) {
                        throw JSONRPCError(RPC_POCKETTX_MATURITY, FormatStateMessage(state));
                    } else {
                        if (fMissingInputs) {
                            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                        }
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, FormatStateMessage(state));
                    }
                }
            } else {
                // If wallet is enabled, ensure that the wallet has been made aware
                // of the new transaction prior to returning. This prevents a race
                // where a user might call sendrawtransaction with a transaction
                // to/from their wallet, immediately call some wallet RPC, and get
                // a stale result because callbacks have not yet been processed.
                CallFunctionInValidationInterfaceQueue([&promise] {
                    promise.set_value();
                });
            }
        } else if (fHaveChain) {
            throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
        } else {
            // Make sure we don't block forever if re-sending
            // a transaction already in mempool.
            promise.set_value();
        }

    } // cs_main

    promise.get_future().wait();

    if (!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CInv inv(MSG_TX, hashTx);
    g_connman->ForEachNode([&inv](CNode* pnode) {
        pnode->PushInventory(inv);
    });

    return hashTx.GetHex();
}

static UniValue sendrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransactionwithkey calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n" + HelpExampleCli("signrawtransactionwithwallet", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n" + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n" + HelpExampleRpc("sendrawtransaction", "\"signedhex\""));

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    //CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    RTransaction rtx(mtx);

    return _sendrawtransaction(rtx);
}

static UniValue testmempoolaccept(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            // clang-format off
            "testmempoolaccept [\"rawtxs\"] ( allowhighfees )\n"
            "\nReturns if raw transaction (serialized, hex-encoded) would be accepted by mempool.\n"
            "\nThis checks if the transaction violates the consensus or policy rules.\n"
            "\nSee sendrawtransaction call.\n"
            "\nArguments:\n"
            "1. [\"rawtxs\"]       (array, required) An array of hex strings of raw transactions.\n"
            "                                        Length must be one for now.\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "[                   (array) The result of the mempool acceptance test for each raw transaction in the input array.\n"
            "                            Length is exactly one for now.\n"
            " {\n"
            "  \"txid\"           (string) The transaction hash in hex\n"
            "  \"allowed\"        (boolean) If the mempool allows this tx to be inserted\n"
            "  \"reject-reason\"  (string) Rejection string (only present when 'allowed' is false)\n"
            " }\n"
            "]\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransactionwithwallet", "\"myhex\"") +
            "\nTest acceptance of the transaction (signed hex)\n"
            + HelpExampleCli("testmempoolaccept", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("testmempoolaccept", "[\"signedhex\"]")
            // clang-format on
        );
    }

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VBOOL});
    if (request.params[0].get_array().size() != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Array must contain exactly one raw transaction for now");
    }

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_array()[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& tx_hash = tx->GetHash();

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    UniValue result(UniValue::VARR);
    UniValue result_0(UniValue::VOBJ);
    result_0.pushKV("txid", tx_hash.GetHex());

    CValidationState state;
    bool missing_inputs;
    bool test_accept_res;
    {
        LOCK(cs_main);
        test_accept_res = AcceptToMemoryPool(mempool, state, std::move(tx), &missing_inputs,
            nullptr /* plTxnReplaced */, false /* bypass_limits */, max_raw_tx_fee, /* test_accept */ true);
    }
    result_0.pushKV("allowed", test_accept_res);
    if (!test_accept_res) {
        if (state.IsInvalid()) {
            result_0.pushKV("reject-reason", strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else if (missing_inputs) {
            result_0.pushKV("reject-reason", "missing-inputs");
        } else {
            result_0.pushKV("reject-reason", state.GetRejectReason());
        }
    }

    result.push_back(std::move(result_0));
    return result;
}

static std::string WriteHDKeypath(std::vector<uint32_t>& keypath)
{
    std::string keypath_str = "m";
    for (uint32_t num : keypath) {
        keypath_str += "/";
        bool hardened = false;
        if (num & 0x80000000) {
            hardened = true;
            num &= ~0x80000000;
        }

        keypath_str += std::to_string(num);
        if (hardened) {
            keypath_str += "'";
        }
    }
    return keypath_str;
}

UniValue decodepsbt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodepsbt \"psbt\"\n"
            "\nReturn a JSON object representing the serialized, base64-encoded partially signed Pocketcoin transaction.\n"

            "\nArguments:\n"
            "1. \"psbt\"            (string, required) The PSBT base64 string\n"

            "\nResult:\n"
            "{\n"
            "  \"tx\" : {                   (json object) The decoded network-serialized unsigned transaction.\n"
            "    ...                                      The layout is the same as the output of decoderawtransaction.\n"
            "  },\n"
            "  \"unknown\" : {                (json object) The unknown global fields\n"
            "    \"key\" : \"value\"            (key-value pair) An unknown key-value pair\n"
            "     ...\n"
            "  },\n"
            "  \"inputs\" : [                 (array of json objects)\n"
            "    {\n"
            "      \"non_witness_utxo\" : {   (json object, optional) Decoded network transaction for non-witness UTXOs\n"
            "        ...\n"
            "      },\n"
            "      \"witness_utxo\" : {            (json object, optional) Transaction output for witness UTXOs\n"
            "        \"amount\" : x.xxx,           (numeric) The value in " +
            CURRENCY_UNIT + "\n"
                            "        \"scriptPubKey\" : {          (json object)\n"
                            "          \"asm\" : \"asm\",            (string) The asm\n"
                            "          \"hex\" : \"hex\",            (string) The hex\n"
                            "          \"type\" : \"pubkeyhash\",    (string) The type, eg 'pubkeyhash'\n"
                            "          \"address\" : \"address\"     (string) Pocketcoin address if there is one\n"
                            "        }\n"
                            "      },\n"
                            "      \"partial_signatures\" : {             (json object, optional)\n"
                            "        \"pubkey\" : \"signature\",           (string) The public key and signature that corresponds to it.\n"
                            "        ,...\n"
                            "      }\n"
                            "      \"sighash\" : \"type\",                  (string, optional) The sighash type to be used\n"
                            "      \"redeem_script\" : {       (json object, optional)\n"
                            "          \"asm\" : \"asm\",            (string) The asm\n"
                            "          \"hex\" : \"hex\",            (string) The hex\n"
                            "          \"type\" : \"pubkeyhash\",    (string) The type, eg 'pubkeyhash'\n"
                            "        }\n"
                            "      \"witness_script\" : {       (json object, optional)\n"
                            "          \"asm\" : \"asm\",            (string) The asm\n"
                            "          \"hex\" : \"hex\",            (string) The hex\n"
                            "          \"type\" : \"pubkeyhash\",    (string) The type, eg 'pubkeyhash'\n"
                            "        }\n"
                            "      \"bip32_derivs\" : {          (json object, optional)\n"
                            "        \"pubkey\" : {                     (json object, optional) The public key with the derivation path as the value.\n"
                            "          \"master_fingerprint\" : \"fingerprint\"     (string) The fingerprint of the master key\n"
                            "          \"path\" : \"path\",                         (string) The path\n"
                            "        }\n"
                            "        ,...\n"
                            "      }\n"
                            "      \"final_scriptsig\" : {       (json object, optional)\n"
                            "          \"asm\" : \"asm\",            (string) The asm\n"
                            "          \"hex\" : \"hex\",            (string) The hex\n"
                            "        }\n"
                            "       \"final_scriptwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
                            "      \"unknown\" : {                (json object) The unknown global fields\n"
                            "        \"key\" : \"value\"            (key-value pair) An unknown key-value pair\n"
                            "         ...\n"
                            "      },\n"
                            "    }\n"
                            "    ,...\n"
                            "  ]\n"
                            "  \"outputs\" : [                 (array of json objects)\n"
                            "    {\n"
                            "      \"redeem_script\" : {       (json object, optional)\n"
                            "          \"asm\" : \"asm\",            (string) The asm\n"
                            "          \"hex\" : \"hex\",            (string) The hex\n"
                            "          \"type\" : \"pubkeyhash\",    (string) The type, eg 'pubkeyhash'\n"
                            "        }\n"
                            "      \"witness_script\" : {       (json object, optional)\n"
                            "          \"asm\" : \"asm\",            (string) The asm\n"
                            "          \"hex\" : \"hex\",            (string) The hex\n"
                            "          \"type\" : \"pubkeyhash\",    (string) The type, eg 'pubkeyhash'\n"
                            "      }\n"
                            "      \"bip32_derivs\" : [          (array of json objects, optional)\n"
                            "        {\n"
                            "          \"pubkey\" : \"pubkey\",                     (string) The public key this path corresponds to\n"
                            "          \"master_fingerprint\" : \"fingerprint\"     (string) The fingerprint of the master key\n"
                            "          \"path\" : \"path\",                         (string) The path\n"
                            "          }\n"
                            "        }\n"
                            "        ,...\n"
                            "      ],\n"
                            "      \"unknown\" : {                (json object) The unknown global fields\n"
                            "        \"key\" : \"value\"            (key-value pair) An unknown key-value pair\n"
                            "         ...\n"
                            "      },\n"
                            "    }\n"
                            "    ,...\n"
                            "  ]\n"
                            "  \"fee\" : fee                      (numeric, optional) The transaction fee paid if all UTXOs slots in the PSBT have been filled.\n"
                            "}\n"

                            "\nExamples:\n" +
            HelpExampleCli("decodepsbt", "\"psbt\""));

    RPCTypeCheck(request.params, {UniValue::VSTR});

    // Unserialize the transactions
    PartiallySignedTransaction psbtx;
    std::string error;
    if (!DecodePSBT(psbtx, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    UniValue result(UniValue::VOBJ);

    // Add the decoded tx
    UniValue tx_univ(UniValue::VOBJ);
    TxToUniv(CTransaction(*psbtx.tx), uint256(), tx_univ, false);
    result.pushKV("tx", tx_univ);

    // Unknown data
    UniValue unknowns(UniValue::VOBJ);
    for (auto entry : psbtx.unknown) {
        unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
    }
    result.pushKV("unknown", unknowns);

    // inputs
    CAmount total_in = 0;
    bool have_all_utxos = true;
    UniValue inputs(UniValue::VARR);
    for (unsigned int i = 0; i < psbtx.inputs.size(); ++i) {
        const PSBTInput& input = psbtx.inputs[i];
        UniValue in(UniValue::VOBJ);
        // UTXOs
        if (!input.witness_utxo.IsNull()) {
            const CTxOut& txout = input.witness_utxo;

            UniValue out(UniValue::VOBJ);

            out.pushKV("amount", ValueFromAmount(txout.nValue));
            total_in += txout.nValue;

            UniValue o(UniValue::VOBJ);
            ScriptToUniv(txout.scriptPubKey, o, true);
            out.pushKV("scriptPubKey", o);
            in.pushKV("witness_utxo", out);
        } else if (input.non_witness_utxo) {
            UniValue non_wit(UniValue::VOBJ);
            TxToUniv(*input.non_witness_utxo, uint256(), non_wit, false);
            in.pushKV("non_witness_utxo", non_wit);
            total_in += input.non_witness_utxo->vout[psbtx.tx->vin[i].prevout.n].nValue;
        } else {
            have_all_utxos = false;
        }

        // Partial sigs
        if (!input.partial_sigs.empty()) {
            UniValue partial_sigs(UniValue::VOBJ);
            for (const auto& sig : input.partial_sigs) {
                partial_sigs.pushKV(HexStr(sig.second.first), HexStr(sig.second.second));
            }
            in.pushKV("partial_signatures", partial_sigs);
        }

        // Sighash
        if (input.sighash_type > 0) {
            in.pushKV("sighash", SighashToStr((unsigned char)input.sighash_type));
        }

        // Redeem script and witness script
        if (!input.redeem_script.empty()) {
            UniValue r(UniValue::VOBJ);
            ScriptToUniv(input.redeem_script, r, false);
            in.pushKV("redeem_script", r);
        }
        if (!input.witness_script.empty()) {
            UniValue r(UniValue::VOBJ);
            ScriptToUniv(input.witness_script, r, false);
            in.pushKV("witness_script", r);
        }

        // keypaths
        if (!input.hd_keypaths.empty()) {
            UniValue keypaths(UniValue::VARR);
            for (auto entry : input.hd_keypaths) {
                UniValue keypath(UniValue::VOBJ);
                keypath.pushKV("pubkey", HexStr(entry.first));

                keypath.pushKV("master_fingerprint", strprintf("%08x", ReadBE32(entry.second.fingerprint)));
                keypath.pushKV("path", WriteHDKeypath(entry.second.path));
                keypaths.push_back(keypath);
            }
            in.pushKV("bip32_derivs", keypaths);
        }

        // Final scriptSig and scriptwitness
        if (!input.final_script_sig.empty()) {
            UniValue scriptsig(UniValue::VOBJ);
            scriptsig.pushKV("asm", ScriptToAsmStr(input.final_script_sig, true));
            scriptsig.pushKV("hex", HexStr(input.final_script_sig));
            in.pushKV("final_scriptSig", scriptsig);
        }
        if (!input.final_script_witness.IsNull()) {
            UniValue txinwitness(UniValue::VARR);
            for (const auto& item : input.final_script_witness.stack) {
                txinwitness.push_back(HexStr(item.begin(), item.end()));
            }
            in.pushKV("final_scriptwitness", txinwitness);
        }

        // Unknown data
        if (input.unknown.size() > 0) {
            UniValue unknowns(UniValue::VOBJ);
            for (auto entry : input.unknown) {
                unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
            }
            in.pushKV("unknown", unknowns);
        }

        inputs.push_back(in);
    }
    result.pushKV("inputs", inputs);

    // outputs
    CAmount output_value = 0;
    UniValue outputs(UniValue::VARR);
    for (unsigned int i = 0; i < psbtx.outputs.size(); ++i) {
        const PSBTOutput& output = psbtx.outputs[i];
        UniValue out(UniValue::VOBJ);
        // Redeem script and witness script
        if (!output.redeem_script.empty()) {
            UniValue r(UniValue::VOBJ);
            ScriptToUniv(output.redeem_script, r, false);
            out.pushKV("redeem_script", r);
        }
        if (!output.witness_script.empty()) {
            UniValue r(UniValue::VOBJ);
            ScriptToUniv(output.witness_script, r, false);
            out.pushKV("witness_script", r);
        }

        // keypaths
        if (!output.hd_keypaths.empty()) {
            UniValue keypaths(UniValue::VARR);
            for (auto entry : output.hd_keypaths) {
                UniValue keypath(UniValue::VOBJ);
                keypath.pushKV("pubkey", HexStr(entry.first));
                keypath.pushKV("master_fingerprint", strprintf("%08x", ReadBE32(entry.second.fingerprint)));
                keypath.pushKV("path", WriteHDKeypath(entry.second.path));
                keypaths.push_back(keypath);
            }
            out.pushKV("bip32_derivs", keypaths);
        }

        // Unknown data
        if (output.unknown.size() > 0) {
            UniValue unknowns(UniValue::VOBJ);
            for (auto entry : output.unknown) {
                unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
            }
            out.pushKV("unknown", unknowns);
        }

        outputs.push_back(out);

        // Fee calculation
        output_value += psbtx.tx->vout[i].nValue;
    }
    result.pushKV("outputs", outputs);
    if (have_all_utxos) {
        result.pushKV("fee", ValueFromAmount(total_in - output_value));
    }

    return result;
}

UniValue combinepsbt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinepsbt [\"psbt\",...]\n"
            "\nCombine multiple partially signed Pocketcoin transactions into one transaction.\n"
            "Implements the Combiner role.\n"
            "\nArguments:\n"
            "1. \"txs\"                   (string) A json array of base64 strings of partially signed transactions\n"
            "    [\n"
            "      \"psbt\"             (string) A base64 string of a PSBT\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "  \"psbt\"          (string) The base64-encoded partially signed transaction\n"
            "\nExamples:\n" +
            HelpExampleCli("combinepsbt", "[\"mybase64_1\", \"mybase64_2\", \"mybase64_3\"]"));

    RPCTypeCheck(request.params, {UniValue::VARR}, true);

    // Unserialize the transactions
    std::vector<PartiallySignedTransaction> psbtxs;
    UniValue txs = request.params[0].get_array();
    for (unsigned int i = 0; i < txs.size(); ++i) {
        PartiallySignedTransaction psbtx;
        std::string error;
        if (!DecodePSBT(psbtx, txs[i].get_str(), error)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
        }
        psbtxs.push_back(psbtx);
    }

    PartiallySignedTransaction merged_psbt(psbtxs[0]); // Copy the first one

    // Merge
    for (auto it = std::next(psbtxs.begin()); it != psbtxs.end(); ++it) {
        if (*it != merged_psbt) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "PSBTs do not refer to the same transactions.");
        }
        merged_psbt.Merge(*it);
    }
    if (!merged_psbt.IsSane()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Merged PSBT is inconsistent");
    }

    UniValue result(UniValue::VOBJ);
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << merged_psbt;
    return EncodeBase64((unsigned char*)ssTx.data(), ssTx.size());
}

UniValue finalizepsbt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "finalizepsbt \"psbt\" ( extract )\n"
            "Finalize the inputs of a PSBT. If the transaction is fully signed, it will produce a\n"
            "network serialized transaction which can be broadcast with sendrawtransaction. Otherwise a PSBT will be\n"
            "created which has the final_scriptSig and final_scriptWitness fields filled for inputs that are complete.\n"
            "Implements the Finalizer and Extractor roles.\n"
            "\nArguments:\n"
            "1. \"psbt\"                 (string) A base64 string of a PSBT\n"
            "2. \"extract\"              (boolean, optional, default=true) If true and the transaction is complete, \n"
            "                             extract and return the complete transaction in normal network serialization instead of the PSBT.\n"

            "\nResult:\n"
            "{\n"
            "  \"psbt\" : \"value\",          (string) The base64-encoded partially signed transaction if not extracted\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded network transaction if extracted\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("finalizepsbt", "\"psbt\""));

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL}, true);

    // Unserialize the transactions
    PartiallySignedTransaction psbtx;
    std::string error;
    if (!DecodePSBT(psbtx, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    // Get all of the previous transactions
    bool complete = true;
    for (unsigned int i = 0; i < psbtx.tx->vin.size(); ++i) {
        PSBTInput& input = psbtx.inputs.at(i);

        complete &= SignPSBTInput(DUMMY_SIGNING_PROVIDER, *psbtx.tx, input, i, 1);
    }

    UniValue result(UniValue::VOBJ);
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    bool extract = request.params[1].isNull() || (!request.params[1].isNull() && request.params[1].get_bool());
    if (complete && extract) {
        CMutableTransaction mtx(*psbtx.tx);
        for (unsigned int i = 0; i < mtx.vin.size(); ++i) {
            mtx.vin[i].scriptSig = psbtx.inputs[i].final_script_sig;
            mtx.vin[i].scriptWitness = psbtx.inputs[i].final_script_witness;
        }
        ssTx << mtx;
        result.pushKV("hex", HexStr(ssTx.begin(), ssTx.end()));
    } else {
        ssTx << psbtx;
        result.pushKV("psbt", EncodeBase64((unsigned char*)ssTx.data(), ssTx.size()));
    }
    result.pushKV("complete", complete);

    return result;
}

UniValue createpsbt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "createpsbt [{\"txid\":\"id\",\"vout\":n},...] [{\"address\":amount},{\"data\":\"hex\"},...] ( locktime ) ( replaceable )\n"
            "\nCreates a transaction in the Partially Signed Transaction format.\n"
            "Implements the Creator role.\n"
            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",      (string, required) The transaction id\n"
            "         \"vout\":n,         (numeric, required) The output number\n"
            "         \"sequence\":n      (numeric, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (array, required) a json array with outputs (key-value pairs)\n"
            "   [\n"
            "    {\n"
            "      \"address\": x.xxx,    (obj, optional) A key-value pair. The key (string) is the pocketcoin address, the value (float or string) is the amount in " +
            CURRENCY_UNIT + "\n"
                            "    },\n"
                            "    {\n"
                            "      \"data\": \"hex\"        (obj, optional) A key-value pair. The key must be \"data\", the value is hex encoded data\n"
                            "    }\n"
                            "    ,...                     More key-value pairs of the above form. For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
                            "                             accepted as second parameter.\n"
                            "   ]\n"
                            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
                            "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
                            "                             Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible.\n"
                            "\nResult:\n"
                            "  \"psbt\"        (string)  The resulting raw transaction (base64-encoded string)\n"
                            "\nExamples:\n" +
            HelpExampleCli("createpsbt", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\""));


    RPCTypeCheck(request.params, {
                                     UniValue::VARR,
                                     UniValueType(), // ARR or OBJ, checked later
                                     UniValue::VNUM,
                                     UniValue::VBOOL,
                                 },
        true);

    CMutableTransaction rawTx = ConstructTransaction(request.params[0], request.params[1], request.params[2], request.params[3]);

    // Make a blank psbt
    PartiallySignedTransaction psbtx;
    psbtx.tx = rawTx;
    for (unsigned int i = 0; i < rawTx.vin.size(); ++i) {
        psbtx.inputs.push_back(PSBTInput());
    }
    for (unsigned int i = 0; i < rawTx.vout.size(); ++i) {
        psbtx.outputs.push_back(PSBTOutput());
    }

    // Serialize the PSBT
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << psbtx;

    return EncodeBase64((unsigned char*)ssTx.data(), ssTx.size());
}

UniValue converttopsbt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "converttopsbt \"hexstring\" ( permitsigdata iswitness )\n"
            "\nConverts a network serialized transaction to a PSBT. This should be used only with createrawtransaction and fundrawtransaction\n"
            "createpsbt and walletcreatefundedpsbt should be used for new applications.\n"
            "\nArguments:\n"
            "1. \"hexstring\"              (string, required) The hex string of a raw transaction\n"
            "2. permitsigdata           (boolean, optional, default=false) If true, any signatures in the input will be discarded and conversion.\n"
            "                              will continue. If false, RPC will fail if any signatures are present.\n"
            "3. iswitness               (boolean, optional) Whether the transaction hex is a serialized witness transaction.\n"
            "                              If iswitness is not present, heuristic tests will be used in decoding. If true, only witness deserializaion\n"
            "                              will be tried. If false, only non-witness deserialization will be tried. Only has an effect if\n"
            "                              permitsigdata is true.\n"
            "\nResult:\n"
            "  \"psbt\"        (string)  The resulting raw transaction (base64-encoded string)\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"") +
            "\nConvert the transaction to a PSBT\n" + HelpExampleCli("converttopsbt", "\"rawtransaction\""));


    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL}, true);

    // parse hex string from parameter
    CMutableTransaction tx;
    bool permitsigdata = request.params[1].isNull() ? false : request.params[1].get_bool();
    bool witness_specified = !request.params[2].isNull();
    bool iswitness = witness_specified ? request.params[2].get_bool() : false;
    bool try_witness = permitsigdata ? (witness_specified ? iswitness : true) : false;
    bool try_no_witness = permitsigdata ? (witness_specified ? !iswitness : true) : true;
    if (!DecodeHexTx(tx, request.params[0].get_str(), try_no_witness, try_witness)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    // Remove all scriptSigs and scriptWitnesses from inputs
    for (CTxIn& input : tx.vin) {
        if ((!input.scriptSig.empty() || !input.scriptWitness.IsNull()) && (request.params[1].isNull() || (!request.params[1].isNull() && request.params[1].get_bool()))) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Inputs must not have scriptSigs and scriptWitnesses");
        }
        input.scriptSig.clear();
        input.scriptWitness.SetNull();
    }

    // Make a blank psbt
    PartiallySignedTransaction psbtx;
    psbtx.tx = tx;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        psbtx.inputs.push_back(PSBTInput());
    }
    for (unsigned int i = 0; i < tx.vout.size(); ++i) {
        psbtx.outputs.push_back(PSBTOutput());
    }

    // Serialize the PSBT
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << psbtx;

    return EncodeBase64((unsigned char*)ssTx.data(), ssTx.size());
}
//----------------------------------------------------------
bool checkValidAddress(std::string address_str)
{
    CTxDestination dest = DecodeDestination(address_str);
    return IsValidDestination(dest);
}

bool GetInputAddress(uint256 txhash, int n, std::string& address)
{
    uint256 hash_block;
    CTransactionRef tx;
    //-------------------------
    if (!GetTransaction(txhash, tx, Params().GetConsensus(), hash_block)) return false;
    const CTxOut& txout = tx->vout[n];
    CTxDestination destAddress;
    const CScript& scriptPubKey = txout.scriptPubKey;
    bool fValidAddress = ExtractDestination(scriptPubKey, destAddress);
    if (!fValidAddress) return false;
    address = EncodeDestination(destAddress);
    //-------------------------
    return true;
}

UniValue sendrawtransactionwithmessage(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "sendrawtransactionwithmessage\n"
            "\nCreate new Pocketnet transaction.\n");

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VSTR});
    //-------------------------------------------------
    std::string address = "";
    CMutableTransaction m_new_tx;
    if (!DecodeHexTx(m_new_tx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    if (!GetInputAddress(m_new_tx.vin[0].prevout.hash, m_new_tx.vin[0].prevout.n, address)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid address");
    }

    //CTransactionRef new_tx(MakeTransactionRef(std::move(m_new_tx)));
    RTransaction new_rtx(m_new_tx);
    std::string new_txid = new_rtx->GetHash().GetHex();

    // Antibot check this transaction
    std::string mesType = request.params[2].get_str();
    int64_t txTime = new_rtx->nTime;

    if (mesType == "share") {
        new_rtx.pTable = "Posts";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        // Posts:
        //   txid - txid of original post transaction
        //   txidEdit - txid of post transaction
        std::string _txid_edit = "";
        if (request.params[1].exists("txidEdit")) _txid_edit = request.params[1]["txidEdit"].get_str();
        if (_txid_edit != "") {
            reindexer::Item _itmP;
            reindexer::Error _err = g_pocketdb->SelectOne(reindexer::Query("Posts").Where("txid", CondEq, _txid_edit), _itmP);

            if (!_err.ok()) throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid txidEdit. Post not found.");

            txTime = _itmP["time"].As<int64_t>();
        }

        new_rtx.pTransaction["txid"] = _txid_edit == "" ? new_txid : _txid_edit;
        new_rtx.pTransaction["txidEdit"] = _txid_edit == "" ? "" : new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["lang"] = "en";
        new_rtx.pTransaction["message"] = request.params[1]["m"].get_str();

        if (request.params[1].exists("c")) new_rtx.pTransaction["caption"] = request.params[1]["c"].get_str();
        if (request.params[1].exists("u")) new_rtx.pTransaction["url"] = request.params[1]["u"].get_str();
        if (request.params[1].exists("s")) new_rtx.pTransaction["settings"] = request.params[1]["s"].get_obj().write();

        vector<string> _tags;
        if (request.params[1].exists("t")) {
            UniValue tags = request.params[1]["t"].get_array();
            for (unsigned int idx = 0; idx < tags.size(); idx++) {
                _tags.push_back(tags[idx].get_str());
            }
        }
        new_rtx.pTransaction["tags"] = _tags;


        vector<string> _images;
        if (request.params[1].exists("i")) {
            UniValue images = request.params[1]["i"].get_array();
            for (unsigned int idx = 0; idx < images.size(); idx++) {
                _images.push_back(images[idx].get_str());
            }
        }
        new_rtx.pTransaction["images"] = _images;

        // 0 - simple post (default)
        // 1 - video post
        // 2 - image post
        int share_type = 0;
        if (request.params[1].exists("type") && request.params[1]["type"].isNum()) {
            share_type = request.params[1]["type"].get_int();
        }
        new_rtx.pTransaction["type"] = share_type;

    } else if (mesType == "upvoteShare") {
        new_rtx.pTable = "Scores";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["posttxid"] = request.params[1]["share"].get_str();
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["time"] = txTime;

        int _val;
        ParseInt32(request.params[1]["value"].get_str(), &_val);
        new_rtx.pTransaction["value"] = _val;

    } else if (mesType == "subscribe" || mesType == "subscribePrivate") {
        new_rtx.pTable = "Subscribes";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["address_to"] = request.params[1]["address"].get_str();
        new_rtx.pTransaction["private"] = (mesType == "subscribePrivate");
        new_rtx.pTransaction["unsubscribe"] = false;

    } else if (mesType == "unsubscribe") {
        reindexer::Item _itm;
        reindexer::Error _err = g_pocketdb->SelectOne(
            reindexer::Query("SubscribesView")
                .Where("address", CondEq, address)
                .Where("address_to", CondEq, request.params[1]["address"].get_str()),
            _itm);

        if (!_err.ok()) throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid transaction");
        //----------------------------
        new_rtx.pTable = "Subscribes";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["address_to"] = request.params[1]["address"].get_str();
        new_rtx.pTransaction["private"] = _itm["private"].As<bool>();
        new_rtx.pTransaction["unsubscribe"] = true;

    } else if (mesType == "userInfo") {
        new_rtx.pTable = "Users";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["id"] = -1;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["name"] = request.params[1]["n"].get_str();
        new_rtx.pTransaction["avatar"] = request.params[1]["i"].get_str();
        new_rtx.pTransaction["lang"] = request.params[1]["l"].get_str() == "" ? "en" : request.params[1]["l"].get_str();

        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["regdate"] = txTime;
        reindexer::Item user_cur;
        if (g_pocketdb->SelectOne(reindexer::Query("UsersView").Where("address", CondEq, address), user_cur).ok()) {
            new_rtx.pTransaction["regdate"] = user_cur["regdate"].As<int64_t>();
        }

        if (request.params[1].exists("a")) new_rtx.pTransaction["about"] = request.params[1]["a"].get_str();
        if (request.params[1].exists("s")) new_rtx.pTransaction["url"] = request.params[1]["s"].get_str();
        if (request.params[1].exists("b")) new_rtx.pTransaction["donations"] = request.params[1]["b"].get_str();
        if (request.params[1].exists("k")) new_rtx.pTransaction["pubkey"] = request.params[1]["k"].get_str();

        new_rtx.pTransaction["referrer"] = "";
        if (request.params[1].exists("r")) {
            new_rtx.pTransaction["referrer"] = request.params[1]["r"].get_str();
        }

    } else if (mesType == "complainShare") {
        new_rtx.pTable = "Complains";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["posttxid"] = request.params[1]["share"].get_str();
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["time"] = txTime;

        int _val;
        ParseInt32(request.params[1]["reason"].get_str(), &_val);
        new_rtx.pTransaction["reason"] = _val;

    } else if (mesType == "blocking") {
        new_rtx.pTable = "Blocking";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["address_to"] = request.params[1]["address"].get_str();
        new_rtx.pTransaction["unblocking"] = false;

    } else if (mesType == "unblocking") {
        reindexer::Item _itm;
        reindexer::Error _err = g_pocketdb->SelectOne(
            reindexer::Query("BlockingView")
                .Where("address", CondEq, address)
                .Where("address_to", CondEq, request.params[1]["address"].get_str()),
            _itm);

        if (!_err.ok()) throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid transaction");
        //----------------------------
        new_rtx.pTable = "Blocking";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["address_to"] = request.params[1]["address"].get_str();
        new_rtx.pTransaction["unblocking"] = true;

    } else if (mesType == "comment" || mesType == "commentEdit" || mesType == "commentDelete") {

        bool valid = true;
        if (mesType != "comment") valid = valid & request.params[1].exists("id");
        if (mesType != "commentDelete") valid = valid & request.params[1].exists("msg");
        valid = valid & request.params[1].exists("postid");
        valid = valid & request.params[1].exists("parentid");
        valid = valid & request.params[1].exists("answerid");
        if (!valid) throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameters");

        new_rtx.pTable = "Comment";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        std::string _otxid = new_txid;
        if (request.params[1].exists("id")) _otxid = request.params[1]["id"].get_str();
        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["otxid"] = _otxid;

        new_rtx.pTransaction["block"] = -1;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["last"] = true;

        new_rtx.pTransaction["msg"] = "";
        if (mesType != "commentDelete") 
            new_rtx.pTransaction["msg"] = request.params[1]["msg"].get_str();

        new_rtx.pTransaction["postid"] = request.params[1]["postid"].get_str();
        new_rtx.pTransaction["parentid"] = request.params[1]["parentid"].get_str();
        new_rtx.pTransaction["answerid"] = request.params[1]["answerid"].get_str();

    } else if (mesType == "cScore") {

        bool valid = true;
        valid = valid & request.params[1].exists("commentid");
        valid = valid & request.params[1].exists("value");
        if (!valid) throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameters");

        new_rtx.pTable = "CommentScores";
        new_rtx.pTransaction = g_pocketdb->DB()->NewItem(new_rtx.pTable);

        new_rtx.pTransaction["txid"] = new_txid;
        new_rtx.pTransaction["address"] = address;
        new_rtx.pTransaction["time"] = txTime;
        new_rtx.pTransaction["block"] = -1;

        new_rtx.pTransaction["commentid"] = request.params[1]["commentid"].get_str();

        int _val;
        ParseInt32(request.params[1]["value"].get_str(), &_val);
        new_rtx.pTransaction["value"] = _val;

    } else {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid transaction type");
    }
    //-------------------------------------------------
    // Check transaction with antibot
    ANTIBOTRESULT ab_result;
    g_antibot->CheckTransactionRIItem(g_addrindex->GetUniValue(new_rtx, new_rtx.pTransaction, new_rtx.pTable), ab_result);
    if (ab_result != ANTIBOTRESULT::Success) {
        throw JSONRPCError(ab_result, mesType);
    }

    // Antibot checked -> create transaction in blockchain
    UniValue rt = _sendrawtransaction(new_rtx);
    //-------------------------------------------------
    return rt;
}
//----------------------------------------------------------
UniValue getPostData(reindexer::Item& itm, std::string address, int comments_version = 0)
{
    UniValue entry(UniValue::VOBJ);

    entry.pushKV("txid", itm["txid"].As<string>());
    if (itm["txidEdit"].As<string>() != "") entry.pushKV("edit", "true");
    entry.pushKV("address", itm["address"].As<string>());
    entry.pushKV("time", itm["time"].As<string>());
    entry.pushKV("l", itm["lang"].As<string>());
    entry.pushKV("c", itm["caption"].As<string>());
    entry.pushKV("m", itm["message"].As<string>());
    entry.pushKV("u", itm["url"].As<string>());

    entry.pushKV("scoreSum", itm["scoreSum"].As<string>());
    entry.pushKV("scoreCnt", itm["scoreCnt"].As<string>());

    try {
        UniValue t(UniValue::VARR);
        reindexer::VariantArray va = itm["tags"];
        for (unsigned int idx = 0; idx < va.size(); idx++) {
            t.push_back(va[idx].As<string>());
        }
        entry.pushKV("t", t);
    } catch (...) {
    }

    try {
        UniValue i(UniValue::VARR);
        reindexer::VariantArray va = itm["images"];
        for (unsigned int idx = 0; idx < va.size(); idx++) {
            i.push_back(va[idx].As<string>());
        }
        entry.pushKV("i", i);
    } catch (...) {
    }

    UniValue ss(UniValue::VOBJ);
    ss.read(itm["settings"].As<string>());
    entry.pushKV("s", ss);

    if (address != "") {
        reindexer::Item scoreMyItm;
        reindexer::Error errS = g_pocketdb->SelectOne(
            reindexer::Query("Scores").Where("address", CondEq, address).Where("posttxid", CondEq, itm["txid"].As<string>()),
            scoreMyItm);

        entry.pushKV("myVal", errS.ok() ? scoreMyItm["value"].As<string>() : "0");
    }

	if (comments_version == 0) {
        int totalComments = g_pocketdb->SelectCount(Query("Comments").Where("postid", CondEq, itm["txid"].As<string>()));
        reindexer::Item cmntItm;
        g_pocketdb->SelectOne(Query("Comments").Where("postid", CondEq, itm["txid"].As<string>()).Where("parentid", CondEq, "").Sort("time", true), cmntItm);
        entry.pushKV("comments", totalComments);
        if (totalComments > 0) {
            UniValue oCmnt(UniValue::VOBJ);
            oCmnt.pushKV("id", cmntItm["id"].As<string>());
            oCmnt.pushKV("postid", cmntItm["postid"].As<string>());
            oCmnt.pushKV("address", cmntItm["address"].As<string>());
            oCmnt.pushKV("pubkey", cmntItm["pubkey"].As<string>());
            oCmnt.pushKV("signature", cmntItm["signature"].As<string>());
            oCmnt.pushKV("time", cmntItm["time"].As<string>());
            oCmnt.pushKV("block", cmntItm["block"].As<string>());
            oCmnt.pushKV("msg", cmntItm["msg"].As<string>());
            oCmnt.pushKV("parentid", cmntItm["parentid"].As<string>());
            oCmnt.pushKV("answerid", cmntItm["answerid"].As<string>());
            oCmnt.pushKV("timeupd", cmntItm["timeupd"].As<string>());
            oCmnt.pushKV("children", std::to_string(g_pocketdb->SelectCount(Query("Comments").Where("parentid", CondEq, cmntItm["id"].As<string>()))));

            entry.pushKV("lastComment", oCmnt);
        }
    } else {
        int totalComments = g_pocketdb->SelectCount(Query("Comment").Where("postid", CondEq, itm["txid"].As<string>()).Where("last", CondEq, true));
        entry.pushKV("comments", totalComments);

        reindexer::QueryResults cmntRes;
        g_pocketdb->Select(
            Query("Comment", 0, 1)
                .Where("postid", CondEq, itm["txid"].As<string>())
                .Where("parentid", CondEq, "")
                .Where("last", CondEq, true)
                .Sort("time", true)
                .InnerJoin("otxid", "txid", CondEq, Query("Comment").Limit(1))
                .LeftJoin("otxid", "commentid", CondEq, Query("CommentScores").Where("address", CondSet, address).Limit(1))
            ,cmntRes);
        
        if (totalComments > 0 && cmntRes.Count() > 0) {
            UniValue oCmnt(UniValue::VOBJ);

            reindexer::Item cmntItm = cmntRes[0].GetItem();
            reindexer::Item ocmntItm = cmntRes[0].GetJoined()[0][0].GetItem();
            
            int myScore = 0;
            if (cmntRes[0].GetJoined().size() > 1 && cmntRes[0].GetJoined()[1].Count() > 0) {
                reindexer::Item ocmntScoreItm = cmntRes[0].GetJoined()[1][0].GetItem();
                myScore = ocmntScoreItm["value"].As<int>();
            }

            oCmnt.pushKV("id", cmntItm["otxid"].As<string>());
            oCmnt.pushKV("postid", cmntItm["postid"].As<string>());
            oCmnt.pushKV("address", cmntItm["address"].As<string>());
            oCmnt.pushKV("time", ocmntItm["time"].As<string>());
            oCmnt.pushKV("timeUpd", cmntItm["time"].As<string>());
            oCmnt.pushKV("block", cmntItm["block"].As<string>());
            oCmnt.pushKV("msg", cmntItm["msg"].As<string>());
            oCmnt.pushKV("parentid", cmntItm["parentid"].As<string>());
            oCmnt.pushKV("answerid", cmntItm["answerid"].As<string>());
            oCmnt.pushKV("scoreUp", cmntItm["scoreUp"].As<string>());
            oCmnt.pushKV("scoreDown", cmntItm["scoreDown"].As<string>());
            oCmnt.pushKV("reputation", cmntItm["reputation"].As<string>());
            oCmnt.pushKV("edit", cmntItm["otxid"].As<string>() != cmntItm["txid"].As<string>());
            oCmnt.pushKV("deleted", cmntItm["msg"].As<string>() == "");
            oCmnt.pushKV("myScore", myScore);
            oCmnt.pushKV("children", std::to_string(g_pocketdb->SelectCount(Query("Comment").Where("parentid", CondEq, cmntItm["otxid"].As<string>()).Where("last", CondEq, true))));

            entry.pushKV("lastComment", oCmnt);
        }
	}

    return entry;
}

UniValue getrawtransactionwithmessage(const JSONRPCRequest& request, int version = 0) {
    if (request.fHelp)
        throw std::runtime_error(
            "getrawtransactionwithmessage\n"
            "\nReturn Pocketnet posts.\n");

    UniValue a(UniValue::VARR);

    reindexer::QueryResults queryRes;
    reindexer::Error err;

    int64_t resultStart = 0;
    int resultCount = 50;

    std::string address_from = "";
    if (request.params.size() > 0 && request.params[0].get_str().length() > 0) {
        address_from = request.params[0].get_str();
        if (address_from.length() < 34)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid address in HEX transaction");
    }

    std::string address_to = "";
    if (request.params.size() > 1 && request.params[1].get_str().length() > 0) {
        address_to = request.params[1].get_str();
        if (address_to != "1" && address_to.length() < 34)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid address in HEX transaction");
    }

    if (request.params.size() > 2) {
        reindexer::Item postItm;
        reindexer::Error errT = g_pocketdb->SelectOne(
            reindexer::Query("Posts").Where("txid", CondEq, request.params[2].get_str()),
            postItm);
        if (errT.ok()) resultStart = postItm["time"].As<int64_t>();
    }

    if (request.params.size() > 3) {
        resultCount = request.params[3].get_int();
    }

    vector<string> addrsblock;
    if (address_from != "" && (address_to == "" || address_to == "1")) {
        reindexer::QueryResults queryResBlocking;
        err = g_pocketdb->DB()->Select(reindexer::Query("BlockingView").Where("address", CondEq, address_from), queryResBlocking);

        for (auto it : queryResBlocking) {
            reindexer::Item itm(it.GetItem());
            addrsblock.push_back(itm["address_to"].As<string>());
        }
    }

    // Do not show posts from users with reputation < Limit::bad_reputation
    if (address_to == "") {
        int64_t _bad_reputation_limit = GetActualLimit(Limit::bad_reputation, chainActive.Height());
        reindexer::QueryResults queryResBadReputation;
        err = g_pocketdb->DB()->Select(reindexer::Query("UsersView").Where("reputation", CondLe, _bad_reputation_limit), queryResBadReputation);

        for (auto it : queryResBadReputation) {
            reindexer::Item itm(it.GetItem());
            addrsblock.push_back(itm["address"].As<string>());
        }
    }

    if (address_to != "") {
        vector<string> addrs;
        if (address_to == "1") {
            if (address_from.length() < 34)
                throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid address in HEX transaction");
            reindexer::QueryResults queryResSubscribes;
            err = g_pocketdb->DB()->Select(reindexer::Query("SubscribesView").Where("address", CondEq, address_from), queryResSubscribes);

            for (auto it : queryResSubscribes) {
                reindexer::Item itm(it.GetItem());
                addrs.push_back(itm["address_to"].As<string>());
            }
        } else {
            addrs.push_back(address_to);
        }

        err = g_pocketdb->DB()->Select(
            reindexer::Query("Posts" /*, 0, resultCount*/).Where("address", CondSet, addrs).Not().Where("address", CondSet, addrsblock).Where("time", ((resultCount > 0 && resultStart > 0) ? CondLt : CondGt), resultStart).Where("time", CondLe, GetAdjustedTime()).Sort("time", (resultCount > 0 ? true : false)),
            queryRes);
    } else {
        err = g_pocketdb->DB()->Select(
            reindexer::Query("Posts" /*, 0, resultCount*/).Not().Where("address", CondSet, addrsblock).Where("time", ((resultCount > 0 && resultStart > 0) ? CondLt : CondGt), resultStart).Where("time", CondLe, GetAdjustedTime()).Sort("time", (resultCount > 0 ? true : false)),
            queryRes);
    }

    int iQuery = 0;
    reindexer::QueryResults::Iterator it = queryRes.begin();
    while (resultCount > 0 && it != queryRes.end()) {
        reindexer::Item itm(it.GetItem());

        reindexer::QueryResults queryResComp;
        err = g_pocketdb->DB()->Select(reindexer::Query("Complains").Where("posttxid", CondEq, itm["txid"].As<string>()), queryResComp);
        reindexer::QueryResults queryResUpv;
        err = g_pocketdb->DB()->Select(reindexer::Query("Scores").Where("posttxid", CondEq, itm["txid"].As<string>()).Where("value", CondGt, 3), queryResUpv);

        if (queryResComp.Count() <= 7 || queryResComp.Count() / (queryResUpv.Count() == 0 ? 1 : queryResUpv.Count() == 0 ? 1 : queryResUpv.Count()) <= 0.1) {
            a.push_back(getPostData(itm, address_from, version));
            resultCount -= 1;
        }
        iQuery += 1;
        it = queryRes[iQuery];
    }

    return a;
}

UniValue getrawtransactionwithmessage(const JSONRPCRequest& request)
{
    return getrawtransactionwithmessage(request, 0);
}

UniValue getrawtransactionwithmessage2(const JSONRPCRequest& request)
{
    return getrawtransactionwithmessage(request, 2);
}

UniValue getrawtransactionwithmessagebyid(const JSONRPCRequest& request, int version = 0)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getrawtransactionwithmessagebyid\n"
            "\nReturn Pocketnet posts.\n");

    if (request.params.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "There is no TxId");

    vector<string> TxIds;
    if (request.params[0].isArray()) {
        UniValue txid = request.params[0].get_array();
        for (unsigned int idx = 0; idx < txid.size(); idx++) {
            TxIds.push_back(txid[idx].get_str());
        }
    } else if (request.params[0].isStr()) {
        TxIds.push_back(request.params[0].get_str());
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid inputs params");
    }

    std::string address = "";
    if (request.params.size() > 1 && request.params[1].get_str().length() > 0) {
        address = request.params[1].get_str();
        if (address.length() < 34)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid address in HEX transaction");
    }

    UniValue a(UniValue::VARR);

    reindexer::QueryResults queryRes;
    reindexer::Error err;

    err = g_pocketdb->DB()->Select(
        reindexer::Query("Posts").Where("txid", CondSet, TxIds).Sort("time", true),
        queryRes);

    for (auto it : queryRes) {
        reindexer::Item itm(it.GetItem());
        a.push_back(getPostData(itm, address, version));
    }
    return a;
}

UniValue getrawtransactionwithmessagebyid(const JSONRPCRequest& request) {
    return getrawtransactionwithmessagebyid(request, 0);
}

UniValue getrawtransactionwithmessagebyid2(const JSONRPCRequest& request) {
    return getrawtransactionwithmessagebyid(request, 2);
}

UniValue gethotposts(const JSONRPCRequest& request, int version = 0)
{
    if (request.fHelp)
        throw std::runtime_error(
            "gethotposts\n"
            "\nReturn Pocketnet top posts.\n");

    int count = 20;
    if (request.params.size() > 0) {
        ParseInt32(request.params[0].get_str(), &count);
    }

    // Depth in seconds (default 3 days)
    int depth = 24 * 3 * 60 * 60;
    if (request.params.size() > 1) {
        ParseInt32(request.params[1].get_str(), &depth);
    }

    std::string address = "";
    if (request.params.size() > 2 && request.params[2].get_str().length() > 0) {
        address = request.params[2].get_str();
        if (address.length() < 34)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid address in HEX transaction");
    }

    int64_t curTime = GetAdjustedTime();

    // Excluded posts
    vector<string> addrsblock;

    // Exclude posts from blocked authors
    if (address != "") {
        reindexer::QueryResults queryResBlocking;
        g_pocketdb->DB()->Select(reindexer::Query("BlockingView").Where("address", CondEq, address), queryResBlocking);

        for (auto it : queryResBlocking) {
            reindexer::Item itm(it.GetItem());
            addrsblock.push_back(itm["address_to"].As<string>());
        }
    }

    // Do not show posts from users with reputation < Limit::bad_reputation
    int64_t _bad_reputation_limit = GetActualLimit(Limit::bad_reputation, chainActive.Height());
    reindexer::QueryResults queryResBadReputation;
    g_pocketdb->DB()->Select(reindexer::Query("UsersView").Where("reputation", CondLe, _bad_reputation_limit), queryResBadReputation);

    for (auto it : queryResBadReputation) {
        reindexer::Item itm(it.GetItem());
        addrsblock.push_back(itm["address"].As<string>());
    }

    // best posts of last month
    // 60s * 60m * 24h * 30d = 2592000
    reindexer::QueryResults postsRes;
    g_pocketdb->Select(reindexer::Query("Posts", 0, count * 5)
                           .Where("time", CondGt, curTime - depth)
                           .Not()
                           .Where("address", CondSet, addrsblock)
                           .Sort("reputation", true)
                           .Sort("scoreSum", true),
        postsRes);

    UniValue result(UniValue::VARR);

    for (auto& p : postsRes) {
        reindexer::Item postItm = p.GetItem();

        if (postItm["reputation"].As<int>() > 0) {
            result.push_back(getPostData(postItm, address, version));
        }

        if (result.size() >= count) break;
    }

    return result;
}

UniValue gethotposts(const JSONRPCRequest& request) { return gethotposts(request, 0); }
UniValue gethotposts2(const JSONRPCRequest& request) { return gethotposts(request, 2); }
//----------------------------------------------------------
std::map<std::string, UniValue> getUsersProfiles(std::vector<string> addresses, bool shortForm = true, int option = 0)
{
    std::map<std::string, UniValue> result;

    // Get users
    reindexer::QueryResults _users_res;
    g_pocketdb->DB()->Select(reindexer::Query("UsersView").Where("address", CondSet, addresses), _users_res);

    // Get count of posts by addresses
    reindexer::AggregationResult aggRes;
    std::map<std::string, int> _posts_cnt;
    if (g_pocketdb->SelectAggr(reindexer::Query("Posts").Where("address", CondSet, addresses).Aggregate("address", AggFacet), "address", aggRes).ok()) {
        for (const auto& f : aggRes.facets) {
            _posts_cnt.insert_or_assign(f.value, f.count);
        }
    }

    // Build return object array
    for (auto& it : _users_res) {
        UniValue entry(UniValue::VOBJ);
        reindexer::Item itm = it.GetItem();
        std::string _address = itm["address"].As<string>();

        // Minimal fields for short form
        entry.pushKV("address", _address);
        entry.pushKV("name", itm["name"].As<string>());
        entry.pushKV("id", itm["id"].As<int>() + 1);
        entry.pushKV("i", itm["avatar"].As<string>());
        entry.pushKV("b", itm["donations"].As<string>());
        entry.pushKV("r", itm["referrer"].As<string>());
        entry.pushKV("reputation", itm["reputation"].As<string>());

        if (_posts_cnt.find(_address) != _posts_cnt.end()) {
            entry.pushKV("postcnt", _posts_cnt[_address]);
        }

        // Count of referrals
        size_t _referrals_count = g_pocketdb->SelectCount(reindexer::Query("UsersView").Where("referrer", CondEq, _address));
        entry.pushKV("rc", (int)_referrals_count);

        if (option == 1)
            entry.pushKV("a", itm["about"].As<string>());

        // In full form add other fields
        if (!shortForm) {
            entry.pushKV("regdate", itm["regdate"].As<int64_t>());
            if (option != 1)
                entry.pushKV("a", itm["about"].As<string>());
            entry.pushKV("l", itm["lang"].As<string>());
            entry.pushKV("s", itm["url"].As<string>());
            entry.pushKV("update", itm["time"].As<int64_t>());
            entry.pushKV("k", itm["pubkey"].As<string>());
            //entry.pushKV("birthday", itm["birthday"].As<int>());
            //entry.pushKV("gender", itm["gender"].As<int>());

            // Subscribes
            reindexer::QueryResults queryResSubscribes;
            reindexer::Error errS = g_pocketdb->DB()->Select(
                reindexer::Query("SubscribesView")
                    .Where("address", CondEq, _address),
                queryResSubscribes);

            UniValue aS(UniValue::VARR);
            if (errS.ok() && queryResSubscribes.Count() > 0) {
                for (auto itS : queryResSubscribes) {
                    UniValue entryS(UniValue::VOBJ);
                    reindexer::Item curSbscrbItm(itS.GetItem());
                    entryS.pushKV("adddress", curSbscrbItm["address_to"].As<string>());
                    entryS.pushKV("private", curSbscrbItm["private"].As<string>());
                    aS.push_back(entryS);
                }
            }
            entry.pushKV("subscribes", aS);

            // Subscribers
            reindexer::QueryResults queryResSubscribers;
            reindexer::Error errRS = g_pocketdb->DB()->Select(
                reindexer::Query("SubscribesView")
                    .Where("address_to", CondEq, _address)
                    .Where("private", CondEq, false),
                queryResSubscribers);

            UniValue arS(UniValue::VARR);
            if (errRS.ok() && queryResSubscribers.Count() > 0) {
                for (auto itS : queryResSubscribers) {
                    reindexer::Item curSbscrbItm(itS.GetItem());
                    arS.push_back(curSbscrbItm["address"].As<string>());
                }
            }
            entry.pushKV("subscribers", arS);

            // Blockings
            reindexer::QueryResults queryResBlockings;
            reindexer::Error errRB = g_pocketdb->DB()->Select(
                reindexer::Query("BlockingView")
                    .Where("address", CondEq, _address),
                queryResBlockings);

            UniValue arB(UniValue::VARR);
            if (errRB.ok() && queryResBlockings.Count() > 0) {
                for (auto itB : queryResBlockings) {
                    reindexer::Item curBlckItm(itB.GetItem());
                    arB.push_back(curBlckItm["address_to"].As<string>());
                }
            }
            entry.pushKV("blocking", arB);

            // Recommendations subscribtions
            std::vector<string> recomendedSubscriptions;
            g_addrindex->GetRecomendedSubscriptions(_address, 10, recomendedSubscriptions);

            UniValue rs(UniValue::VARR);
            for (std::string r : recomendedSubscriptions) {
                rs.push_back(r);
            }
            entry.pushKV("recomendedSubscribes", rs);
        }

        result.insert_or_assign(_address, entry);
    }

    return result;
}
UniValue getuserprofile(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getuserprofile \"address\" ( shortForm )\n"
            "\nReturn Pocketnet user profile.\n");

    if (request.params.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "There is no address");

    std::vector<string> addresses;
    if (request.params[0].isStr())
        addresses.push_back(request.params[0].get_str());
    else if (request.params[0].isArray()) {
        UniValue addr = request.params[0].get_array();
        for (unsigned int idx = 0; idx < addr.size(); idx++) {
            addresses.push_back(addr[idx].get_str());
        }
    } else
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid inputs params");

    // Short profile form is: address, b, i, name
    bool shortForm = false;
    if (request.params.size() >= 2) {
        shortForm = request.params[1].get_str() == "1";
    }

    UniValue aResult(UniValue::VARR);

    std::map<std::string, UniValue> profiles = getUsersProfiles(addresses, shortForm);
    for (auto& p : profiles) {
        aResult.push_back(p.second);
    }

    return aResult;
}

UniValue getmissedinfo(const JSONRPCRequest& request, int version = 0)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getmissedinfo \"address\" block_number\n"
            "\nGet missed info.\n");

    if (request.params.size() < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "There is no address");
    if (request.params.size() < 2)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "There is no block number");

    std::string address = "";
    int blockNumber = 0;
    int cntResult = 30;

    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        CTxDestination dest = DecodeDestination(request.params[0].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + request.params[0].get_str());
        }

        address = request.params[0].get_str();
    }

    if (!request.params[1].isNull()) {
        blockNumber = request.params[1].isNum() ? request.params[1].get_int() : std::stoi(request.params[1].get_str());
    }

    if (!request.params[2].isNull()) {
        cntResult = request.params[2].isNum() ? request.params[2].get_int() : std::stoi(request.params[2].get_str());
    }

    UniValue a(UniValue::VARR);

    reindexer::QueryResults posts;
    g_pocketdb->DB()->Select(reindexer::Query("Posts").Where("block", CondGt, blockNumber), posts);

    UniValue msg(UniValue::VOBJ);
    msg.pushKV("block", (int)chainActive.Height());
    msg.pushKV("cntposts", (int)posts.Count());
    a.push_back(msg);

    /*std::string txidpocketnet = "";
    std::string addrespocketnet = "PEj7QNjKdDPqE9kMDRboKoCtp8V6vZeZPd";

    reindexer::QueryResults postspocketnet;
    g_pocketdb->DB()->Select(reindexer::Query("Posts").Where("block", CondGt, blockNumber).Where("address", CondEq, addrespocketnet).Where("txidEdit", CondEq, ""), postspocketnet);
    for (auto it : postspocketnet) {
        reindexer::Item itm(it.GetItem());
        if (txidpocketnet.find(itm["txid"].As<string>()) == std::string::npos)
            txidpocketnet = txidpocketnet + itm["txid"].As<string>() + ",";
    }
    reindexer::QueryResults postshistpocketnet;
    g_pocketdb->DB()->Select(reindexer::Query("PostsHistory").Where("block", CondGt, blockNumber).Where("address", CondEq, addrespocketnet).Where("txidEdit", CondEq, ""), postshistpocketnet);
    for (auto it : postshistpocketnet) {
        reindexer::Item itm(it.GetItem());
        if (txidpocketnet.find(itm["txid"].As<string>()) == std::string::npos)
            txidpocketnet = txidpocketnet + itm["txid"].As<string>() + ",";
    }
    if (txidpocketnet != "") {
        UniValue msg(UniValue::VOBJ);
        msg.pushKV("msg", "sharepocketnet");
        msg.pushKV("txids", txidpocketnet.substr(0, txidpocketnet.size() - 1));
        a.push_back(msg);
    }*/

    std::string addrespocketnet = "PEj7QNjKdDPqE9kMDRboKoCtp8V6vZeZPd";
    reindexer::QueryResults postspocketnet;
    g_pocketdb->DB()->Select(reindexer::Query("Posts").Where("block", CondGt, blockNumber).Where("address", CondEq, addrespocketnet), postspocketnet);
    for (auto it : postspocketnet) {
        reindexer::Item itm(it.GetItem());
        UniValue msg(UniValue::VOBJ);
        msg.pushKV("msg", "sharepocketnet");
        msg.pushKV("txid", itm["txid"].As<string>());
        msg.pushKV("time", itm["time"].As<string>());
        msg.pushKV("nblock", itm["block"].As<int>());
        a.push_back(msg);
    }

    reindexer::QueryResults subscribers;
    g_pocketdb->DB()->Select(reindexer::Query("SubscribesView").Where("address_to", CondEq, address).Where("block", CondGt, blockNumber).Sort("time", true).Limit(cntResult), subscribers);
    for (auto it : subscribers) {
        reindexer::Item itm(it.GetItem());
        UniValue msg(UniValue::VOBJ);
        msg.pushKV("addr", itm["address_to"].As<string>());
        msg.pushKV("addrFrom", itm["address"].As<string>());
        msg.pushKV("msg", "event");
        msg.pushKV("txid", itm["txid"].As<string>());
        msg.pushKV("time", itm["time"].As<string>());
        msg.pushKV("mesType", "subscribe");
        msg.pushKV("nblock", itm["block"].As<int>());

        a.push_back(msg);
    }

    reindexer::QueryResults scores;
    g_pocketdb->DB()->Select(reindexer::Query("Scores").Where("block", CondGt, blockNumber).InnerJoin("posttxid", "txid", CondEq, reindexer::Query("Posts").Where("address", CondEq, address)).Sort("time", true).Limit(cntResult), scores);
    for (auto it : scores) {
        reindexer::Item itm(it.GetItem());
        UniValue msg(UniValue::VOBJ);
        msg.pushKV("addr", address);
        msg.pushKV("addrFrom", itm["address"].As<string>());
        msg.pushKV("msg", "event");
        msg.pushKV("txid", itm["txid"].As<string>());
        msg.pushKV("time", itm["time"].As<string>());
        msg.pushKV("posttxid", itm["posttxid"].As<string>());
        msg.pushKV("upvoteVal", itm["value"].As<int>());
        msg.pushKV("mesType", "upvoteShare");
        msg.pushKV("nblock", itm["block"].As<int>());
        a.push_back(msg);
    }

    reindexer::QueryResults commentScores;
    g_pocketdb->DB()->Select(
        reindexer::Query("CommentScores")
            .Where("block", CondGt, blockNumber)
            .InnerJoin("commentid", "txid", CondEq, reindexer::Query("Comment").Where("address", CondEq, address))
            .Sort("time", true)
            .Limit(cntResult)
    ,commentScores);
    for (auto it : commentScores) {
        reindexer::Item itm(it.GetItem());
        UniValue msg(UniValue::VOBJ);
        msg.pushKV("addr", address);
        msg.pushKV("addrFrom", itm["address"].As<string>());
        msg.pushKV("msg", "event");
        msg.pushKV("txid", itm["txid"].As<string>());
        msg.pushKV("time", itm["time"].As<string>());
        msg.pushKV("commentid", itm["commentid"].As<string>());
        msg.pushKV("upvoteVal", itm["value"].As<int>());
        msg.pushKV("mesType", "cScore");
        msg.pushKV("nblock", itm["block"].As<int>());
        a.push_back(msg);
    }

    std::vector<std::string> txSent;
    reindexer::QueryResults transactions;
    g_pocketdb->DB()->Select(reindexer::Query("UTXO").Where("address", CondEq, address).Where("block", CondGt, blockNumber).Sort("time", true).Limit(cntResult), transactions);
    for (auto it : transactions) {
        reindexer::Item itm(it.GetItem());
        
        // Double transaction notify not allowed
        if (std::find(txSent.begin(), txSent.end(), itm["txid"].As<string>()) != txSent.end()) continue;

        UniValue msg(UniValue::VOBJ);
        msg.pushKV("addr", itm["address"].As<string>());
        msg.pushKV("msg", "transaction");
        msg.pushKV("txid", itm["txid"].As<string>());
        msg.pushKV("time", itm["time"].As<string>());
        msg.pushKV("amount", itm["amount"].As<int64_t>());
        msg.pushKV("nblock", itm["block"].As<int>());

        uint256 hash = ParseHashV(itm["txid"].As<string>(), "txid");
        CTransactionRef tx;
        uint256 hash_block;
        CBlockIndex* blockindex = nullptr;
        if (GetTransaction(hash, tx, Params().GetConsensus(), hash_block, true, blockindex)) {
            const CTxOut& txout = tx->vout[itm["txout"].As<int>()];
            std::string optype = "";
            if (txout.scriptPubKey[0] == OP_RETURN) {
                std::string asmstr = ScriptToAsmStr(txout.scriptPubKey);
                std::vector<std::string> spl;
                boost::split(spl, asmstr, boost::is_any_of("\t "));
                if (spl.size() == 3) {
                    if (spl[1] == OR_POST || spl[1] == OR_POSTEDIT)
                        optype = "share";
                    else if (spl[1] == OR_SCORE)
                        optype = "upvoteShare";
                    else if (spl[1] == OR_SUBSCRIBE)
                        optype = "subscribe";
                    else if (spl[1] == OR_SUBSCRIBEPRIVATE)
                        optype = "subscribePrivate";
                    else if (spl[1] == OR_USERINFO)
                        optype = "userInfo";
                    else if (spl[1] == OR_UNSUBSCRIBE)
                        optype = "unsubscribe";
                }
            }
            if (optype != "") msg.pushKV("type", optype);

            UniValue txinfo(UniValue::VOBJ);
            TxToJSON(*tx, hash_block, txinfo);
            msg.pushKV("txinfo", txinfo);
        }

        a.push_back(msg);
    }

    if (version == 0) {
        vector<string> answerpostids;
        reindexer::QueryResults commentsAnswer;
        g_pocketdb->DB()->Select(reindexer::Query("Comments").Where("block", CondGt, blockNumber).InnerJoin("answerid", "id", CondEq, reindexer::Query("Comments").Where("address", CondEq, address)).Sort("time", true).Limit(cntResult), commentsAnswer);
        for (auto it : commentsAnswer) {
            reindexer::Item itm(it.GetItem());
            if (address != itm["address"].As<string>()) {
                UniValue msg(UniValue::VOBJ);
                msg.pushKV("addr", address);
                msg.pushKV("addrFrom", itm["address"].As<string>());
                msg.pushKV("nblock", itm["block"].As<int>());
                msg.pushKV("msg", "comment");
                msg.pushKV("mesType", "answer");
                msg.pushKV("commentid", itm["id"].As<string>());
                msg.pushKV("posttxid", itm["postid"].As<string>());
                msg.pushKV("time", itm["time"].As<string>());
                if (itm["parentid"].As<string>() != "") msg.pushKV("parentid", itm["parentid"].As<string>());
                if (itm["answerid"].As<string>() != "") msg.pushKV("answerid", itm["answerid"].As<string>());

                a.push_back(msg);

                answerpostids.push_back(itm["postid"].As<string>());
            }
        }

        reindexer::QueryResults commentsPost;
        g_pocketdb->DB()->Select(reindexer::Query("Comments").Where("block", CondGt, blockNumber).InnerJoin("postid", "txid", CondEq, reindexer::Query("Posts").Where("address", CondEq, address).Not().Where("txid", CondSet, answerpostids)).Sort("time", true).Limit(cntResult), commentsPost);
        for (auto it : commentsPost) {
            reindexer::Item itm(it.GetItem());
            if (address != itm["address"].As<string>()) {
                UniValue msg(UniValue::VOBJ);
                msg.pushKV("addr", address);
                msg.pushKV("addrFrom", itm["address"].As<string>());
                msg.pushKV("nblock", itm["block"].As<int>());
                msg.pushKV("msg", "comment");
                msg.pushKV("mesType", "post");
                msg.pushKV("commentid", itm["id"].As<string>());
                msg.pushKV("posttxid", itm["postid"].As<string>());
                msg.pushKV("time", itm["time"].As<string>());
                if (itm["parentid"].As<string>() != "") msg.pushKV("parentid", itm["parentid"].As<string>());
                if (itm["answerid"].As<string>() != "") msg.pushKV("answerid", itm["answerid"].As<string>());

                a.push_back(msg);
            }
        }
    } else {

        vector<string> answerpostids;
        reindexer::QueryResults commentsAnswer;
        g_pocketdb->DB()->Select(
            reindexer::Query("Comment")
                .Where("block", CondGt, blockNumber)
                .Where("last", CondEq, true)
                .InnerJoin("answerid", "otxid", CondEq, reindexer::Query("Comment").Where("address", CondEq, address).Where("last", CondEq, true))
                .Sort("time", true)
                .Limit(cntResult)
        ,commentsAnswer);

        for (auto it : commentsAnswer) {
            reindexer::Item itm(it.GetItem());
            if (address != itm["address"].As<string>()) {
                if (itm["msg"].As<string>() == "") continue;
                if (itm["otxid"].As<string>() != itm["txid"].As<string>()) continue;

                UniValue msg(UniValue::VOBJ);
                msg.pushKV("addr", address);
                msg.pushKV("addrFrom", itm["address"].As<string>());
                msg.pushKV("nblock", itm["block"].As<int>());
                msg.pushKV("msg", "comment");
                msg.pushKV("mesType", "answer");
                msg.pushKV("txid", itm["otxid"].As<string>());
                msg.pushKV("posttxid", itm["postid"].As<string>());
                msg.pushKV("reason", "answer");
                msg.pushKV("time", itm["time"].As<string>());
                if (itm["parentid"].As<string>() != "") msg.pushKV("parentid", itm["parentid"].As<string>());
                if (itm["answerid"].As<string>() != "") msg.pushKV("answerid", itm["answerid"].As<string>());

                a.push_back(msg);

                answerpostids.push_back(itm["postid"].As<string>());
            }
        }

        reindexer::QueryResults commentsPost;
        g_pocketdb->DB()->Select(reindexer::Query("Comment").Where("block", CondGt, blockNumber).Where("last", CondEq, true)
            .InnerJoin("postid", "txid", CondEq, reindexer::Query("Posts").Where("address", CondEq, address).Not().Where("txid", CondSet, answerpostids)).Sort("time", true).Limit(cntResult), commentsPost);

        for (auto it : commentsPost) {
            reindexer::Item itm(it.GetItem());
            if (address != itm["address"].As<string>()) {
                if (itm["msg"].As<string>() == "") continue;
                if (itm["otxid"].As<string>() != itm["txid"].As<string>()) continue;

                UniValue msg(UniValue::VOBJ);
                msg.pushKV("addr", address);
                msg.pushKV("addrFrom", itm["address"].As<string>());
                msg.pushKV("nblock", itm["block"].As<int>());
                msg.pushKV("msg", "comment");
                msg.pushKV("mesType", "post");
                msg.pushKV("txid", itm["otxid"].As<string>());
                msg.pushKV("posttxid", itm["postid"].As<string>());
                msg.pushKV("reason", "post");
                msg.pushKV("time", itm["time"].As<string>());
                if (itm["parentid"].As<string>() != "") msg.pushKV("parentid", itm["parentid"].As<string>());
                if (itm["answerid"].As<string>() != "") msg.pushKV("answerid", itm["answerid"].As<string>());

                a.push_back(msg);
            }
        }
    }

    return a;
}

UniValue getmissedinfo(const JSONRPCRequest& request) { return getmissedinfo(request, 0); }
UniValue getmissedinfo2(const JSONRPCRequest& request) { return getmissedinfo(request, 2); }

UniValue txunspent(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            "txunspent ( minconf maxconf  [\"addresses\",...] [include_unsafe] [query_options])\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "\nArguments:\n"
            "1. \"addresses\"      (string) A json array of pocketcoin addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) pocketcoin address\n"
            "      ,...\n"
            "    ]\n"
            "2. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "3. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "4. include_unsafe (bool, optional, default=true) Include outputs that are not safe to spend\n"
            "                  See description of \"safe\" attribute below.\n"
            "5. query_options    (json, optional) JSON with query options\n"
            "    {\n"
            "      \"minimumAmount\"    (numeric or string, default=0) Minimum value of each UTXO in " +
            CURRENCY_UNIT + "\n"
                            "      \"maximumAmount\"    (numeric or string, default=unlimited) Maximum value of each UTXO in " +
            CURRENCY_UNIT + "\n"
                            "      \"maximumCount\"     (numeric or string, default=unlimited) Maximum number of UTXOs\n"
                            "      \"minimumSumAmount\" (numeric or string, default=unlimited) Minimum sum value of all UTXOs in " +
            CURRENCY_UNIT + "\n"
                            "    }\n"
                            "\nResult\n"
                            "[                   (array of json object)\n"
                            "  {\n"
                            "    \"txid\" : \"txid\",          (string) the transaction id \n"
                            "    \"vout\" : n,               (numeric) the vout value\n"
                            "    \"address\" : \"address\",    (string) the pocketcoin address\n"
                            "    \"label\" : \"label\",        (string) The associated label, or \"\" for the default label\n"
                            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
                            "    \"amount\" : x.xxx,         (numeric) the transaction output amount in " +
            CURRENCY_UNIT + "\n"
                            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
                            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
                            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
                            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
                            "    \"safe\" : xxx              (bool) Whether this output is considered safe to spend. Unconfirmed transactions\n"
                            "                              from outside keys and unconfirmed replacement transactions are considered unsafe\n"
                            "                              and are not eligible for spending by fundrawtransaction and sendtoaddress.\n"
                            "  }\n"
                            "  ,...\n"
                            "]\n"

                            "\nExamples\n" +
            HelpExampleCli("txunspent", "") +
            HelpExampleCli("txunspent", "\"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\" 6 9999999") +
            HelpExampleRpc("txunspent", "\"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\" 6 9999999") +
            HelpExampleCli("txunspent", "'[]' 6 9999999 true '{ \"minimumAmount\": 0.005 }'") +
            HelpExampleRpc("txunspent", "[], 6, 9999999, true, { \"minimumAmount\": 0.005 } "));
    }

    std::vector<std::string> destinations;
    if (request.params.size() > 0) {
        RPCTypeCheckArgument(request.params[0], UniValue::VARR);
        UniValue inputs = request.params[0].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CTxDestination dest = DecodeDestination(input.get_str());

            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + input.get_str());
            }

            if (std::find(destinations.begin(), destinations.end(), input.get_str()) == destinations.end()) {
                destinations.push_back(input.get_str());
            }
        }
    }

    int nMinDepth = 1;
    if (request.params.size() > 1) {
        RPCTypeCheckArgument(request.params[1], UniValue::VNUM);
        nMinDepth = request.params[1].get_int();
    }

    int nMaxDepth = 9999999;
    if (request.params.size() > 2) {
        RPCTypeCheckArgument(request.params[2], UniValue::VNUM);
        nMaxDepth = request.params[2].get_int();
    }

    bool include_unsafe = true;
    if (request.params.size() > 3) {
        RPCTypeCheckArgument(request.params[3], UniValue::VBOOL);
        include_unsafe = request.params[3].get_bool();
    }

    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = UINTMAX_MAX;

    if (request.params.size() > 4) {
        const UniValue& options = request.params[4].get_obj();

        if (options.exists("minimumAmount"))
            nMinimumAmount = AmountFromValue(options["minimumAmount"]);

        if (options.exists("maximumAmount"))
            nMaximumAmount = AmountFromValue(options["maximumAmount"]);

        if (options.exists("minimumSumAmount"))
            nMinimumSumAmount = AmountFromValue(options["minimumSumAmount"]);

        if (options.exists("maximumCount"))
            nMaximumCount = options["maximumCount"].get_int64();
    }
    // TODO: check txindex and sync

    UniValue results(UniValue::VARR);

    // Get transaction ids from UTXO index
    std::vector<AddressUnspentTransactionItem> unspentTransactions;
    if (!g_addrindex->GetUnspentTransactions(destinations, unspentTransactions)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error get from address index");
    }

    // Check exists TX in mempool
    // TODO: LOCK(mempool.cs);
    for (const auto& e : mempool.mapTx) {
        const CTransaction& tx = e.GetTx();
        for (const CTxIn& txin : tx.vin) {
            AddressUnspentTransactionItem mempoolItm = {"", txin.prevout.hash.ToString(), (int)txin.prevout.n};

            if (std::find_if(
                    unspentTransactions.begin(),
                    unspentTransactions.end(),
                    [&](const AddressUnspentTransactionItem& itm) { return itm == mempoolItm; }) != unspentTransactions.end()) {
                unspentTransactions.erase(
                    remove(unspentTransactions.begin(), unspentTransactions.end(), mempoolItm),
                    unspentTransactions.end());
            }
        }
    }

    for (const auto& unsTx : unspentTransactions) {
        CBlockIndex* blockindex = nullptr;
        uint256 hash_block;
        CTransactionRef tx;
        uint256 txhash;
        txhash.SetHex(unsTx.txid);

        if (!GetTransaction(txhash, tx, Params().GetConsensus(), hash_block, true, blockindex)) {
            continue;
        }

        if (!blockindex) {
            LOCK(cs_main);
            blockindex = LookupBlockIndex(hash_block);
            if (!blockindex) {
                continue;
            }
        }

        const CTxOut& txout = tx->vout[unsTx.txout];

        // TODO: filter by amount

        CTxDestination destAddress;
        const CScript& scriptPubKey = txout.scriptPubKey;
        bool fValidAddress = ExtractDestination(scriptPubKey, destAddress);
        std::string encoded_address = EncodeDestination(destAddress);

        // TODO: filter by depth

        int confirmations = chainActive.Height() - blockindex->nHeight + 1;
        if (confirmations < nMinDepth || confirmations > nMaxDepth) continue;

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", unsTx.txid);
        entry.pushKV("vout", unsTx.txout);
        entry.pushKV("address", unsTx.address);
        entry.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));
        entry.pushKV("amount", ValueFromAmount(txout.nValue));
        entry.pushKV("confirmations", confirmations);
        entry.pushKV("coinbase", tx->IsCoinBase() || tx->IsCoinStake());
        entry.pushKV("pockettx", g_addrindex->IsPocketnetTransaction(tx));
        results.push_back(entry);
    }

    return results;
}

UniValue getaddressregistration(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "getaddressregistration [\"addresses\",...]\n"
            "\nReturns array of registration dates.\n"
            "\nArguments:\n"
            "1. \"addresses\"      (string) A json array of pocketcoin addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) pocketcoin address\n"
            "      ,...\n"
            "    ]\n"
            "\nResult\n"
            "[                             (array of json objects)\n"
            "  {\n"
            "    \"address\" : \"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\",     (string) the pocketcoin address\n"
            "    \"date\" : \"1544596205\",                                (int64) date in Unix time format\n"
            "    \"date\" : \"2378659...\"                                 (string) id of first transaction with this address\n"
            "  },\n"
            "  ,...\n"
            "]");
    }

    std::vector<std::string> addresses;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VARR);
        UniValue inputs = request.params[0].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CTxDestination dest = DecodeDestination(input.get_str());

            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + input.get_str());
            }

            if (std::find(addresses.begin(), addresses.end(), input.get_str()) == addresses.end()) {
                addresses.push_back(input.get_str());
            }
        }
    }
    //-------------------------
    UniValue results(UniValue::VARR);
    //-------------------------
    // Get transaction ids from UTXO index
    std::vector<AddressRegistrationItem> addrRegItems;
    if (!g_addrindex->GetAddressRegistrationDate(addresses, addrRegItems)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error get from address index");
    }
    //-------------------------
    for (const auto& addrReg : addrRegItems) {
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("address", addrReg.address);
        entry.pushKV("date", addrReg.time);
        entry.pushKV("txid", addrReg.txid);
        results.push_back(entry);
    }
    //-------------------------
    return results;
}

UniValue getuserstate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            "getuserstate [\"addresses\",...]\n"
            "\nReturns array of limits.\n"
            "\nArguments:\n"
            "1. \"address\"        (string) A pocketcoin addresses to filter\n"
            "\nResult\n"
            "[                             (array of json objects)\n"
            "  {\n"
            "    \"address\"       : \"1PGFqE..\",     (string) the pocketcoin address\n"
            "    \"reputation\"    : \"205\",          (int) reputation of user\n"
            "    \"balance\"       : \"20500000\",     (int64) sum of unspent transactions\n"
            "    \"trial\"         : \"true\",         (bool) trial mode?\n"
            "    \"post_unspent\"  : \"4\",            (int) unspent posts count\n"
            "    \"post_spent\"    : \"3\",            (int) spent posts count\n"
            "    \"score_unspent\" : \"3\",            (int) unspent scores count\n"
            "    \"score_spent\"   : \"3\",            (int) spent scores count\n"
            "  },\n"
            "  ,...\n"
            "]");
    }

    std::string address;
    if (request.params.size() > 0) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        CTxDestination dest = DecodeDestination(request.params[0].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + request.params[0].get_str());
        }

        address = request.params[0].get_str();
    }

    int64_t time = GetAdjustedTime();
    if (request.params.size() > 1 && request.params[1].isNum()) {
        time = request.params[1].get_int64();
    }

    // Get transaction ids from UTXO index
    UserStateItem userStateItm(address);
    if (!g_antibot->GetUserState(address, time, userStateItm)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error get from address index");
    }
    
    return userStateItm.Serialize();
}

UniValue gettime(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "gettime\n"
            "\nReturn node time.\n");

    UniValue entry(UniValue::VOBJ);
    entry.pushKV("time", GetAdjustedTime());

    return entry;
}

UniValue getrecommendedposts(const JSONRPCRequest& request, int version = 0)
{
    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            "getrecommendedposts address count\n"
            "\nReturns array of recommended posts.\n"
            "\nArguments:\n"
            "1. address            (string) A pocketcoin addresses to filter\n"
            "2. count              (int) Max count of posts\n"
            "\nResult\n"
            "[                     (array of posts)\n"
            "  ...\n"
            "]");
    }

    std::string address;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        CTxDestination dest = DecodeDestination(request.params[0].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + request.params[0].get_str());
        }

        address = request.params[0].get_str();
    }

    int count = 30;
    if (request.params.size() >= 2) {
        ParseInt32(request.params[1].get_str(), &count);
    }

    std::set<string> recommendedPosts;
    g_addrindex->GetRecommendedPostsByScores(address, count, recommendedPosts);
    g_addrindex->GetRecommendedPostsBySubscriptions(address, count, recommendedPosts);

    UniValue a(UniValue::VARR);
    for (string p : recommendedPosts) {
        a.push_back(p);
    }

    JSONRPCRequest jreq;
    jreq.params = UniValue(UniValue::VARR);
    jreq.params.push_back(a);
    return getrawtransactionwithmessagebyid(jreq, version);
}

UniValue getrecommendedposts(const JSONRPCRequest& request) { return getrecommendedposts(request, 0); }
UniValue getrecommendedposts2(const JSONRPCRequest& request) { return getrecommendedposts(request, 2); }

UniValue searchtags(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            "searchtags search_string count\n"
            "\nReturns array of found tags.\n"
            "\nArguments:\n"
            "1. search_string      (string) Symbols for search (minimum 3 symbols)\n"
            "2. count              (int) Max count results\n"
            "\nResult\n"
            "[                     (array of tags with frequency usage)\n"
            "  ...\n"
            "]");
    }

    std::string search_string;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        search_string = request.params[0].get_str();
    }

    int count = 10;
    if (request.params.size() >= 2) {
        ParseInt32(request.params[1].get_str(), &count);
    }

    int totalCount = 0;
    std::map<std::string, int> foundTags;
    g_pocketdb->SearchTags(search_string, count, foundTags, totalCount);

    UniValue a(UniValue::VOBJ);
    for (auto& p : foundTags) {
        a.pushKV(p.first, p.second);
    }

    return a;
}

void getFastSearchString(std::string search, std::string str, std::map<std::string, int>& mFastSearch)
{
    std::size_t found = str.find(search);
    int cntfound = 0;
    if (found != std::string::npos && found + search.length() < str.length()) {
        std::string subst = str.substr(found + search.length());
        std::string runningstr = "";
        for (char& c : subst) {
            if (c == ' ' || c == ',' || c == '.' || c == '!' || c == ')' || c == '(' || c == '"') {
                if (runningstr != "") {
                    if (mFastSearch.find(runningstr) == mFastSearch.end()) {
                        mFastSearch.insert_or_assign(runningstr, 1);
                    } else {
                        mFastSearch[runningstr]++;
                    }
                }
                cntfound++;
                if (cntfound == 2) {
                    runningstr = "";
                    break;
                }
            }
            runningstr = runningstr + c;
        }
        if (runningstr != "") {
            if (mFastSearch.find(runningstr) == mFastSearch.end()) {
                mFastSearch.insert_or_assign(runningstr, 1);
            } else {
                mFastSearch[runningstr]++;
            }
        }
    }
}

UniValue search(const JSONRPCRequest& request, int version = 0)
{
    if (request.fHelp)
        throw std::runtime_error(
            "search ...\n"
            "\nSearch in Pocketnet DB.\n");

    std::string fulltext_search_string;
    std::string fulltext_search_string_strict;

    std::string search_string = "";
    if (request.params.size() > 0) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        search_string = UrlDecode(request.params[0].get_str());
    }

    std::string type = "";
    if (request.params.size() > 1) {
        RPCTypeCheckArgument(request.params[1], UniValue::VSTR);
        type = lower(request.params[1].get_str());
    }

    if (type != "all" && type != "posts" && type != "tags" && type != "users") {
        type = "fs";
    }

    bool fs = (type == "fs");
    bool all = (type == "all");

    int blockNumber = 0;
    if (request.params.size() > 2) {
        ParseInt32(request.params[2].get_str(), &blockNumber);
    }

    int resultStart = 0;
    if (request.params.size() > 3) {
        ParseInt32(request.params[3].get_str(), &resultStart);
    }

    int resulCount = 10;
    if (request.params.size() > 4) {
        ParseInt32(request.params[4].get_str(), &resulCount);
    }

    std::string address = "";
    if (request.params.size() > 5) {
        RPCTypeCheckArgument(request.params[5], UniValue::VSTR);
        CTxDestination dest = DecodeDestination(request.params[5].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + request.params[5].get_str());
        }

        address = request.params[5].get_str();
    }

    int fsresultCount = 10;

    // --- Return object -----------------------------------------------
    UniValue result(UniValue::VOBJ);

    // --- First search tags -----------------------------------------------
    int totalCount;
    std::map<std::string, int> foundTagsRatings;
    std::vector<std::string> foundTags;
    std::map<std::string, std::set<std::string>> mTagsUsers;
    std::map<std::string, int> mFastSearch;

    // --- Search posts by Search String -----------------------------------------------
    if (fs || all || type == "posts") {
        //LogPrintf("--- Search: %s\n", fulltext_search_string);
        reindexer::QueryResults resPostsBySearchString;
        if (g_pocketdb->Select(
                          reindexer::Query("Posts", resultStart, resulCount)
                              .Where("block", blockNumber ? CondLe : CondGe, blockNumber)
                              .Where(search_string.at(0) == '#' ? "tags" : "caption+message", CondEq, search_string.at(0) == '#' ? search_string.substr(1) : "\"" + search_string + "\"")
                              .Where("address", address == "" ? CondGt : CondEq, address)
                              .Sort("time", true)
                              .ReqTotal(),
                          resPostsBySearchString)
                .ok()) {
            UniValue aPosts(UniValue::VARR);

            for (auto& it : resPostsBySearchString) {
                Item _itm = it.GetItem();
                std::string _txid = _itm["txid"].As<string>();
                std::string _caption = _itm["caption_"].As<string>();
                std::string _message = _itm["message_"].As<string>();

                if (fs) getFastSearchString(search_string, _caption, mFastSearch);
                if (fs) getFastSearchString(search_string, _message, mFastSearch);

                if (all || type == "posts") aPosts.push_back(getPostData(_itm, "", version));
            }

            if (all || type == "posts") {
                UniValue oPosts(UniValue::VOBJ);
                oPosts.pushKV("count", resPostsBySearchString.totalCount);
                oPosts.pushKV("data", aPosts);
                result.pushKV("posts", oPosts);
            }
        }
    }

    // --- Search Users by Search String -----------------------------------------------
    if (all || type == "users") {
        reindexer::QueryResults resUsersBySearchString;
        if (g_pocketdb->Select(
                          reindexer::Query("UsersView", resultStart, resulCount)
                              .Where("block", blockNumber ? CondLe : CondGe, blockNumber)
                              .Where("name_text", CondEq, "*" + UrlEncode(search_string) + "*")
                              .Sort("time", false)
                              .ReqTotal(),
                          resUsersBySearchString)
                .ok()) {
            std::vector<std::string> vUserAdresses;

            for (auto& it : resUsersBySearchString) {
                Item _itm = it.GetItem();
                std::string _address = _itm["address"].As<string>();
                vUserAdresses.push_back(_address);
            }

            auto mUsers = getUsersProfiles(vUserAdresses, true, 1);

            UniValue aUsers(UniValue::VARR);
            for (auto& u : mUsers) {
                aUsers.push_back(u.second);
            }

            UniValue oUsers(UniValue::VOBJ);
            oUsers.pushKV("count", resUsersBySearchString.totalCount);
            oUsers.pushKV("data", aUsers);

            result.pushKV("users", oUsers);
        }

        fsresultCount = resUsersBySearchString.Count() < fsresultCount ? fsresultCount - resUsersBySearchString.Count() : 0;
    }

    // --- Autocomplete for search string
    if (fs) {
        struct IntCmp {
            bool operator()(const std::pair<std::string, int>& lhs, const std::pair<std::string, int>& rhs)
            {
                return lhs.second > rhs.second; // Changed  < to > since we need DESC order
            }
        };

        UniValue fastsearch(UniValue::VARR);
        std::vector<std::pair<std::string, int>> vFastSearch;
        for (const auto& f : mFastSearch) {
            vFastSearch.push_back(std::pair<std::string, int>(f.first, f.second));
        }

        std::sort(vFastSearch.begin(), vFastSearch.end(), IntCmp());
        int _count = fsresultCount;
        for (auto& t : vFastSearch) {
            fastsearch.push_back(t.first);
            _count -= 1;
            if (_count <= 0) break;
        }
        result.pushKV("fastsearch", fastsearch);
    }

    return result;
}

UniValue search(const JSONRPCRequest& request) { return search(request, 0); }
UniValue search2(const JSONRPCRequest& request) { return search(request, 0); }

UniValue getuseraddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getuseraddress \"user_name\" ( count )\n"
            "\nGet list addresses of user.\n");

    std::string userName;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        userName = request.params[0].get_str();
    }

    int count = 7;
    if (request.params.size() >= 2) {
        ParseInt32(request.params[1].get_str(), &count);
    }

    reindexer::QueryResults users;
    g_pocketdb->Select(reindexer::Query("UsersView", 0, count).Where("name", CondEq, userName), users);

    UniValue aResult(UniValue::VARR);
    for (auto& u : users) {
        reindexer::Item userItm = u.GetItem();

        UniValue oUser(UniValue::VOBJ);
        oUser.pushKV("name", userItm["name"].As<string>());
        oUser.pushKV("address", userItm["address"].As<string>());

        aResult.push_back(oUser);
    }

    return aResult;
}

UniValue getreputations(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getreputations\n"
            "\nGet list repuatations of users.\n");

    reindexer::QueryResults users;
    g_pocketdb->Select(reindexer::Query("UsersView"), users);

    UniValue aResult(UniValue::VARR);
    for (auto& u : users) {
        reindexer::Item userItm = u.GetItem();

        UniValue oUser(UniValue::VOBJ);
        oUser.pushKV("address", userItm["address"].As<string>());
        oUser.pushKV("referrer", userItm["referrer"].As<string>());
        oUser.pushKV("reputation", userItm["reputation"].As<string>());

        aResult.push_back(oUser);
    }

    return aResult;
}

UniValue getcontents(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            "getcontents address\n"
            "\nReturns contents for address.\n"
            "\nArguments:\n"
            "1. address            (string) A pocketcoin addresses to filter\n"
            "\nResult\n"
            "[                     (array of contents)\n"
            "  ...\n"
            "]");
    }

    std::string address;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        CTxDestination dest = DecodeDestination(request.params[0].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + request.params[0].get_str());
        }

        address = request.params[0].get_str();
    }

    reindexer::QueryResults posts;
    g_pocketdb->Select(reindexer::Query("Posts").Where("address", CondEq, address), posts);

    UniValue aResult(UniValue::VARR);
    for (auto& p : posts) {
        reindexer::Item postItm = p.GetItem();

        UniValue oPost(UniValue::VOBJ);
        oPost.pushKV("content", UrlDecode(postItm["caption"].As<string>()) == "" ? UrlDecode(postItm["message"].As<string>()).substr(0, 100) : UrlDecode(postItm["caption"].As<string>()));
        oPost.pushKV("txid", postItm["txid"].As<string>());
        oPost.pushKV("time", postItm["time"].As<string>());
        oPost.pushKV("reputation", postItm["reputation"].As<string>());
        oPost.pushKV("settings", postItm["settings"].As<string>());
        oPost.pushKV("scoreSum", postItm["scoreSum"].As<string>());
        oPost.pushKV("scoreCnt", postItm["scoreCnt"].As<string>());

        aResult.push_back(oPost);
    }
    return aResult;
}

UniValue gettags(const JSONRPCRequest& request)
{
    std::string address = "";
    if (!request.params[0].isNull() && request.params[0].get_str().length() > 0) {
        RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
        CTxDestination dest = DecodeDestination(request.params[0].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Pocketcoin address: ") + request.params[0].get_str());
        }

        address = request.params[0].get_str();
    }

    int count = 50;
    if (request.params.size() >= 2) {
        ParseInt32(request.params[1].get_str(), &count);
    }

	int from = 0;
    if (request.params.size() >= 3) {
        ParseInt32(request.params[2].get_str(), &from);
    }

    std::map<std::string, int> mapTags;
    reindexer::QueryResults posts;
    g_pocketdb->Select(reindexer::Query("Posts").Where("block", CondGe, from).Where("address", address == "" ? CondGt : CondEq, address), posts);
    for (auto& p : posts) {
        reindexer::Item postItm = p.GetItem();

        UniValue t(UniValue::VARR);
        reindexer::VariantArray va = postItm["tags"];
        for (unsigned int idx = 0; idx < va.size(); idx++) {
            std::string sTag = lower(va[idx].As<string>());
            if (std::all_of(sTag.begin(), sTag.end(), [](unsigned char ch) { return ::isdigit(ch) || ::isalpha(ch); })) {
                if (mapTags.count(sTag) == 0)
                    mapTags[sTag] = 1;
                else
                    mapTags[sTag] = mapTags[sTag] + 1;
            }
        }
    }

    typedef std::function<bool(std::pair<std::string, int>, std::pair<std::string, int>)> Comparator;
    Comparator compFunctor = [](std::pair<std::string, int> elem1, std::pair<std::string, int> elem2) { return elem1.second > elem2.second; };
    std::set<std::pair<std::string, int>, Comparator> setTags(mapTags.begin(), mapTags.end(), compFunctor);

    UniValue aResult(UniValue::VARR);
    for (std::set<std::pair<std::string, int>, Comparator>::iterator it = setTags.begin(); it != setTags.end() && count; ++it, --count) {
        UniValue oTag(UniValue::VOBJ);
        oTag.pushKV("tag", it->first);
        oTag.pushKV("count", std::to_string(it->second));

        aResult.push_back(oTag);
    }

    return aResult;
}

UniValue debug(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "debug\n"
            "\nFor debugging purposes.\n");

    UniValue result(UniValue::VOBJ);
    return result;
}


static UniValue getaddressbalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "getaddressbalance address\n"
            "\nGet address balance.\n"
            "\nArguments:\n"
            "1. \"address\"   (string) Public address\n");

    std::string address;
    if (request.params.size() > 0 && request.params[0].isStr()) {
        CTxDestination dest = DecodeDestination(request.params[0].get_str());

        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + request.params[0].get_str());
        }

        address = request.params[0].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address.");
    }

    UniValue result(UniValue::VOBJ);
    int64_t balance = 0;

    CBlockIndex* pindex = chainActive.Tip();
    while (pindex) {
        CBlock block;
        ReadBlockFromDisk(block, pindex, Params().GetConsensus());

        for (const auto& tx : block.vtx) {
            // OUTs add to balance
            for (int i = 0; i < tx->vout.size(); i++) {
                const CTxOut& txout = tx->vout[i];

                CTxDestination destAddress;
                if (!ExtractDestination(txout.scriptPubKey, destAddress)) continue;
                std::string out_address = EncodeDestination(destAddress);
                if (out_address != address) continue;

                balance += txout.nValue;
            }

            // INs remove from balance
            if (!tx->IsCoinBase()) {
                for (int i = 0; i < tx->vin.size(); i++) {
                    const CTxIn& txin = tx->vin[i];

                    uint256 hash_block;
                    CTransactionRef tx;
                    //-------------------------
                    if (!GetTransaction(txin.prevout.hash, tx, Params().GetConsensus(), hash_block)) continue;
                    const CTxOut& txout = tx->vout[txin.prevout.n];
                    CTxDestination destAddress;
                    const CScript& scriptPubKey = txout.scriptPubKey;
                    bool fValidAddress = ExtractDestination(scriptPubKey, destAddress);
                    if (!fValidAddress) continue;
                    std::string in_address = EncodeDestination(destAddress);
                    if (in_address != address) continue;

                    balance -= txout.nValue;
                }
            }
        }

        pindex = pindex->pprev;
    }

    result.pushKV("balance", balance);
    return result;
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                                    actor (function)                    argNames
  //  --------------------- ------------------------                -----------------------             ----------
    { "rawtransactions",    "getrawtransaction",                    &getrawtransaction,                 {"txid","verbose","blockhash"} },
    { "rawtransactions",    "createrawtransaction",                 &createrawtransaction,              {"inputs","outputs","locktime","replaceable"} },
    { "rawtransactions",    "decoderawtransaction",                 &decoderawtransaction,              {"hexstring","iswitness"} },
    { "rawtransactions",    "decodescript",                         &decodescript,                      {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",                   &sendrawtransaction,                {"hexstring","allowhighfees"} },
    { "rawtransactions",    "combinerawtransaction",                &combinerawtransaction,             {"txs"} },
    { "hidden",             "signrawtransaction",                   &signrawtransaction,                {"hexstring","prevtxs","privkeys","sighashtype"} },
    { "rawtransactions",    "signrawtransactionwithkey",            &signrawtransactionwithkey,         {"hexstring","privkeys","prevtxs","sighashtype"} },
    { "rawtransactions",    "testmempoolaccept",                    &testmempoolaccept,                 {"rawtxs","allowhighfees"} },
    { "rawtransactions",    "decodepsbt",                           &decodepsbt,                        {"psbt"} },
    { "rawtransactions",    "combinepsbt",                          &combinepsbt,                       {"txs"} },
    { "rawtransactions",    "finalizepsbt",                         &finalizepsbt,                      {"psbt", "extract"} },
    { "rawtransactions",    "createpsbt",                           &createpsbt,                        {"inputs","outputs","locktime","replaceable"} },
    { "rawtransactions",    "converttopsbt",                        &converttopsbt,                     {"hexstring","permitsigdata","iswitness"} },
    { "rawtransactions",    "sendrawtransactionwithmessage",        &sendrawtransactionwithmessage,     {"hexstring", "message", "type"} },
    { "rawtransactions",    "getrawtransactionwithmessage",         &getrawtransactionwithmessage,      { "address_from", "address_to", "start_txid", "count" } },
    { "rawtransactions",    "getrawtransactionwithmessage2",        &getrawtransactionwithmessage2,     { "address_from", "address_to", "start_txid", "count" } },
    { "rawtransactions",    "getrawtransactionwithmessagebyid",     &getrawtransactionwithmessagebyid,  { "txs","address" } },
    { "rawtransactions",    "getrawtransactionwithmessagebyid2",    &getrawtransactionwithmessagebyid2, { "txs","address" } },
    { "rawtransactions",    "getuserprofile",                       &getuserprofile,                    { "addresses", "short" } },
    { "rawtransactions",    "getmissedinfo",                        &getmissedinfo,                     { "address", "blocknumber" } },
    { "rawtransactions",    "getmissedinfo2",                       &getmissedinfo2,                    { "address", "blocknumber" } },
    { "rawtransactions",    "txunspent",                            &txunspent,                         { "addresses","minconf","maxconf","include_unsafe","query_options" } },
    { "rawtransactions",    "getaddressregistration",               &getaddressregistration,            { "addresses" } },
    { "rawtransactions",    "getuserstate",                         &getuserstate,                      { "address", "time" } },
    { "rawtransactions",    "gettime",                              &gettime,                           {} },
    { "rawtransactions",    "getrecommendedposts",                  &getrecommendedposts,               { "address", "count" } },
    { "rawtransactions",    "getrecommendedposts2",                 &getrecommendedposts2,              { "address", "count" } },
    { "rawtransactions",    "searchtags",                           &searchtags,                        { "search_string", "count" } },
    { "rawtransactions",    "search",                               &search,                            { "search_string", "type", "count" } },
    { "rawtransactions",    "search2",                              &search2,                           { "search_string", "type", "count" } },
    { "rawtransactions",    "gethotposts",                          &gethotposts,                       { "count", "depth" } },
    { "rawtransactions",    "gethotposts2",                         &gethotposts2,                      { "count", "depth" } },
    { "rawtransactions",    "getuseraddress",                       &getuseraddress,                    { "name", "count" } },
	{ "rawtransactions",    "getreputations",                       &getreputations,                    {} },
	{ "rawtransactions",    "getcontents",                          &getcontents,                       { "address" } },
	{ "rawtransactions",    "gettags",                              &gettags,                           { "address", "count" } },

    { "blockchain",         "gettxoutproof",                        &gettxoutproof,                     {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",                     &verifytxoutproof,                  {"proof"} },

    { "rawtransactions",    "debug",                                &debug,                             {} },
    { "rawtransactions",    "getaddressbalance",                    &getaddressbalance,                 { "address" } },
};
// clang-format on

void RegisterRawTransactionRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
