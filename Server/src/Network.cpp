#include "Network.h"
#include "WebRoutes/Auth.h"
#include "WebRoutes/User.h"
#include "WebRoutes/Parts.h"
#include "WebRoutes/Vehicle.h"
#include "WebRoutes/Services.h"

void net()
{
    uWS::SSLApp app;
    app.listen(9001, [&](auto*){});

    app.post("/request", HttpCallWrapper(webRoute::authenticate));
    app.get("/release", webRoute::deauthenticate);

    app.post("/user/create", HttpCallWrapper(webRoute::createUser));
    app.get("/user/me", HttpCallWrapper(webRoute::getLocalUserData));
    app.get("/user/search", HttpCallWrapper(webRoute::searchUsers));
    app.post("/user/delete", HttpCallWrapper(webRoute::deleteUser));
    app.post("/user/update", HttpCallWrapper(webRoute::updateUser));

    app.post("/part/supplier/create", HttpCallWrapper(webRoute::createSupplier));
    app.post("/part/supplier/update", HttpCallWrapper(webRoute::updateSupplier));
    app.post("/part/group/create", HttpCallWrapper(webRoute::createPartGroup));
    app.post("/part/group/update", HttpCallWrapper(webRoute::updatePartGroup));
    app.post("/part/create", HttpCallWrapper(webRoute::createPart));
    app.post("/part/update", HttpCallWrapper(webRoute::updatePart));
    app.get("/part/search", HttpCallWrapper(webRoute::searchParts));


    app.post("/vehicle/create", HttpCallWrapper(webRoute::createVehicle));
    app.post("/vehicle/update", HttpCallWrapper(webRoute::updateVehicle));
    app.post("/vehicle/delete", HttpCallWrapper(webRoute::deleteVehicle));

    app.any("/ping", [](auto* res, auto* req) 
        { 
            if (serverData::auth->hasSession(req))
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") pinged server.\n";
            }
            else
            {
                std::cout << "Unknown user pinged server.\n";
            }
            res->end();
        });

    app.any("/*", [](auto* res, auto* req) 
        {
            res->writeStatus("400");
            res->end("Bad request."); 
        });

    std::cout << "Network ready.\n";
    app.run();
    std::cin.ignore();
}
