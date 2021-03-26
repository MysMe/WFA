#pragma once
#include "Network.h"

namespace webRoute
{
    void authenticate(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "username", "password" }))
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