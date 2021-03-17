#pragma once
#include "Network.h"

namespace webRoute
{
    void authenticate(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "username", "password" }))
        {
            //Bad Request - Invalid arguments.
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->request(res, req, b))
        {
            //Unauthorised - authentication failed
            res->writeStatus("401");
        }
        res->end();
    }

    void deauthenticate(uWS::HttpResponse<true>* res, uWS::HttpRequest* req)
    {
        if (!serverData::auth->release(res, req))
        {
            //Conflict - Can't deauth an unauthorised user
            res->writeStatus("409");
        }
        res->end();
        return;
    }

}