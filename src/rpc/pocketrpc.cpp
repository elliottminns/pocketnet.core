// Copyright (c) 2019 The Pocketcoin Core developers

#include <rpc/pocketrpc.h>

UniValue getcommentsV2(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getcomments (\"postid\", \"parentid\", [\"commend_id\",\"commend_id\",...])\n"
            "\nGet Pocketnet comments.\n"
        );

    std::string postid = "";
    if (request.params.size() > 0) {
        postid = request.params[0].get_str();
        if (postid.length() == 0 && request.params.size() < 3)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid postid");
    }

    std::string parentid = "";
    if (request.params.size() > 1) {
        parentid = request.params[1].get_str();
    }
    
    vector<string> cmnids;
    if (request.params.size() > 2) {
        if (request.params[2].isArray()) {
            UniValue cmntid = request.params[2].get_array();
            for (unsigned int id = 0; id < cmntid.size(); id++) {
                cmnids.push_back(cmntid[id].get_str());
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid inputs params");
        }
    }
    
    reindexer::QueryResults commRes;
    if (cmnids.size()>0)
        g_pocketdb->Select(
            Query("Comment")
            .Where("otxid", CondSet, cmnids)
            .Where("last", CondEq, true)
            .InnerJoin("otxid", "txid", CondEq, Query("Comment").Where("txid", CondSet, cmnids).Limit(1))
        ,commRes);
    else 
        g_pocketdb->Select(
            Query("Comment")
            .Where("postid", CondEq, postid)
            .Where("parentid", CondEq, parentid)
            .Where("last", CondEq, true)
            .InnerJoin("otxid", "txid", CondEq, Query("Comment").Limit(1))
        ,commRes);

    UniValue aResult(UniValue::VARR);
    for (auto& it : commRes) {
        reindexer::Item cmntItm = it.GetItem();
        reindexer::Item ocmntItm = it.GetJoined()[0][0].GetItem();

        UniValue oCmnt(UniValue::VOBJ);
        oCmnt.pushKV("id", cmntItm["otxid"].As<string>());
        oCmnt.pushKV("postid", cmntItm["postid"].As<string>());
        oCmnt.pushKV("address", cmntItm["address"].As<string>());
        oCmnt.pushKV("time", ocmntItm["time"].As<string>());
        oCmnt.pushKV("timeUpd", cmntItm["time"].As<string>());
        oCmnt.pushKV("block", cmntItm["block"].As<string>());
        oCmnt.pushKV("msg", cmntItm["msg"].As<string>());
        oCmnt.pushKV("parentid", cmntItm["parentid"].As<string>());
        oCmnt.pushKV("answerid", cmntItm["answerid"].As<string>());
        oCmnt.pushKV("scoreSum", cmntItm["scoreSum"].As<string>());
        oCmnt.pushKV("scoreCnt", cmntItm["scoreCnt"].As<string>());
        oCmnt.pushKV("reputation", cmntItm["reputation"].As<string>());
        oCmnt.pushKV("edit", cmntItm["otxid"].As<string>() != cmntItm["txid"].As<string>());

        if (parentid == "")
            oCmnt.pushKV("children", std::to_string(g_pocketdb->SelectCount(Query("Comment").Where("parentid", CondEq, cmntItm["otxid"].As<string>()).Where("last", CondEq, true))));

        aResult.push_back(oCmnt);
    }

    return aResult;
}

UniValue getlastcommentsV2(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getlastcomments (count)\n"
            "\nGet Pocketnet last comments.\n");

    int resultCount = 10;
    if (request.params.size() > 0) {
        ParseInt32(request.params[0].get_str(), &resultCount);
    }

    reindexer::QueryResults commRes;
    g_pocketdb->Select(
        Query("Comment")
        .Where("last", CondEq, true)
        .Sort("time", true)
        .Limit(resultCount)
        .InnerJoin("otxid", "txid", CondEq, Query("Comment").Limit(1))
    ,commRes);

    UniValue aResult(UniValue::VARR);
    for (auto& it : commRes) {
        reindexer::Item cmntItm = it.GetItem();
        reindexer::Item ocmntItm = it.GetJoined()[0][0].GetItem();

        UniValue oCmnt(UniValue::VOBJ);
        oCmnt.pushKV("id", cmntItm["otxid"].As<string>());
        oCmnt.pushKV("postid", cmntItm["postid"].As<string>());
        oCmnt.pushKV("address", cmntItm["address"].As<string>());
        oCmnt.pushKV("time", ocmntItm["time"].As<string>());
        oCmnt.pushKV("timeUpd", cmntItm["time"].As<string>());
        oCmnt.pushKV("block", cmntItm["block"].As<string>());
        oCmnt.pushKV("msg", cmntItm["msg"].As<string>());
        oCmnt.pushKV("parentid", cmntItm["parentid"].As<string>());
        oCmnt.pushKV("answerid", cmntItm["answerid"].As<string>());
        oCmnt.pushKV("scoreSum", cmntItm["scoreSum"].As<string>());
        oCmnt.pushKV("scoreCnt", cmntItm["scoreCnt"].As<string>());
        oCmnt.pushKV("reputation", cmntItm["reputation"].As<string>());
        oCmnt.pushKV("edit", cmntItm["otxid"].As<string>() != cmntItm["txid"].As<string>());

        aResult.push_back(oCmnt);
    }

    return aResult;
}

static const CRPCCommand commands[] =
    {
        {"pocketnetrpc",   "getlastcomments2",    &getlastcommentsV2,      {"count"}},
        {"pocketnetrpc",   "getcomments2",        &getcommentsV2,          {"postid", "parentid"}},
};

void RegisterPocketnetRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
