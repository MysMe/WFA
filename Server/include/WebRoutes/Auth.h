#pragma once
#include "Network.h"

namespace webRoute
{
    void authenticate(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "username", "password" }) || b.getElement("username").empty() || b.getElement("password").empty())
        {
            //Bad Request - Invalid arguments.
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        if (!serverData::auth->request(res, req, b))
        {
            //Unauthorised - authentication failed
            res->writeStatus(HTTPCodes::UNAUTHORISED);
        }
        res->end();
    }

    //Both adds an account and authenticates it in a single transaction
    void registerUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "username", "password" }))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::USER] + " (ID, USERNAME, PASSWORD, PERMISSIONS) VALUES (NULL, :USR, :PAS, 1);", {
                {":USR", std::string(b.getElement("username"))},
                {":PAS", std::string(b.getElement("password"))} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Created new client (\"" << b.getElement("username") << "\").\n";
        }
        authenticate(res, req, b, q);
    }


    void deauthenticate(uWS::HttpResponse<true>* res, uWS::HttpRequest* req)
    {
        if (!serverData::auth->release(res, req))
        {
            //Conflict - Can't deauth an unauthorised user
            res->writeStatus(HTTPCodes::CONFLICT);
        }
        res->end();
        return;
    }

    void checkSession(uWS::HttpResponse<true>* res, uWS::HttpRequest* req)
    {
        if (serverData::auth->hasSession(req))
        {
            res->writeStatus(HTTPCodes::OK);
            responseWrapper wrap;
            wrap.add("Permissions", std::to_string(static_cast<int>(serverData::auth->getSessionAuthLevel(req).value())));
            res->end(wrap.toData(false));
        }
        else
        {
            //Clear invalid state (e.g. client-server mismatch)
            serverData::auth->release(res, req);
            //Unauthorised
            res->writeStatus(HTTPCodes::UNAUTHORISED);
            res->end();
        }
        return;
    }

}