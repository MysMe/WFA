#include "Network.h"
#include "WebRoutes/Auth.h"
#include "WebRoutes/User.h"
#include "WebRoutes/Parts.h"
#include "WebRoutes/Vehicle.h"
#include "WebRoutes/Services.h"
#include "curl/curl.h"

void net()
{
    uWS::SSLApp app;
    app.listen(9001, [&](auto*){});

    app.post("/request", HttpCallWrapper(webRoute::authenticate));
    app.post("/register", HttpCallWrapper(webRoute::registerUser));
    app.get("/release", webRoute::deauthenticate);
    app.get("/checkSession", webRoute::checkSession);

    app.post("/user/create", HttpCallWrapper(webRoute::createUser));
    app.get("/user/me", HttpCallWrapper(webRoute::getLocalUserData));
    app.get("/user/search", HttpCallWrapper(webRoute::searchUsers));
    app.get("/user/select", HttpCallWrapper(webRoute::selectUser));
    app.post("/user/delete", HttpCallWrapper(webRoute::deleteUser));
    app.post("/user/update", HttpCallWrapper(webRoute::updateUser));

    app.post("/part/supplier/create", HttpCallWrapper(webRoute::createSupplier));
    app.post("/part/supplier/update", HttpCallWrapper(webRoute::updateSupplier));
    app.get("/part/supplier/search", HttpCallWrapper(webRoute::searchSuppliers));
    app.get("/part/supplier/select", HttpCallWrapper(webRoute::selectSupplier));

    app.post("/part/group/create", HttpCallWrapper(webRoute::createPartGroup));
    app.post("/part/group/update", HttpCallWrapper(webRoute::updatePartGroup));
    app.get("/part/group/search", HttpCallWrapper(webRoute::searchPartGroups));
    app.get("/part/group/select", HttpCallWrapper(webRoute::selectPartGroup));

    app.post("/part/create", HttpCallWrapper(webRoute::createPart));
    app.post("/part/update", HttpCallWrapper(webRoute::updatePart));
    app.get("/part/search", HttpCallWrapper(webRoute::searchParts));
    app.get("/part/select", HttpCallWrapper(webRoute::selectPart));


    app.post("/vehicle/create", HttpCallWrapper(webRoute::createVehicle));
    app.post("/vehicle/update", HttpCallWrapper(webRoute::updateVehicle));
    app.post("/vehicle/delete", HttpCallWrapper(webRoute::deleteVehicle));
    app.get("/vehicle/select", HttpCallWrapper(webRoute::selectVehicle));
    //Search vehicles by owner - Done by select user

    app.post("/service/create", HttpCallWrapper(webRoute::createRequest));
    app.post("/service/authorise", HttpCallWrapper(webRoute::authoriseRequest));
    app.post("/service/update", HttpCallWrapper(webRoute::updateService));
    app.post("/service/close", HttpCallWrapper(webRoute::closeService));
    app.post("/service/part/add", HttpCallWrapper(webRoute::addPartToService));
    app.post("/service/part/remove", HttpCallWrapper(webRoute::removePartFromService));
    app.get("/service/part/select", HttpCallWrapper(webRoute::selectServicePart));
    app.get("/service/search", HttpCallWrapper(webRoute::searchServices));
    app.get("/service/select", HttpCallWrapper(webRoute::selectService));

    app.get("/debug/displayTables", [](auto* res, auto* req)
        {
            std::cout << "Displaying tables:\n";
            const auto [tableCode, tables] = serverData::database->query(std::string_view("SELECT name FROM sqlite_schema WHERE type='table' ORDER BY name"), {});
            if (!tableCode)
            {
                std::cout << "Error accessing database metadata.\n";
            }
            else
            {
                for (size_t x = 0; x < tables.rowCount(); x++)
                {
                    std::cout << tables[x][0] << ":";
                    const auto [code, result] = serverData::database->query("SELECT * FROM " + std::string(tables[x][0]), {});
                    if (!code)
                    {
                        std::cout << "Error displaying table rows.\n";
                    }
                    else
                    {
                        if (result.columnCount() == 0 || result.rowCount() == 0)
                        {
                            std::cout << " empty.\n";
                            continue;
                        }
                        std::cout << "\n";

                        std::vector<size_t> sizes(result.columnCount(), 0);
                        for (size_t r = 0; r < result.rowCount(); r++)
                        {
                            for (size_t c = 0; c < result.columnCount(); c++)
                            {
                                if (result[r][c].size() > sizes[c])
                                    sizes[c] = result[r][c].size();
                            }
                        }

                        std::cout << '|';
                        for (size_t c = 0; c < result.columnCount(); c++)
                        {
                            if (result.getColName(c).size() > sizes[c])
                                sizes[c] = result.getColName(c).size();

                            std::cout << result.getColName(c);
                            for (size_t i = result.getColName(c).size(); i < sizes[c]; i++)
                            {
                                std::cout << ' ';
                            }
                            std::cout << '|';
                        }
                        std::cout << "\n";

                        for (size_t row = 0; row < result.rowCount(); row++)
                        {
                            std::cout << '|';
                            for (size_t col = 0; col < result.columnCount(); col++)
                            {
                                std::cout << result[row][col];
                                for (size_t i = result[row][col].size(); i < sizes[col]; i++)
                                {
                                    std::cout << ' ';
                                }
                                std::cout << '|';
                            }
                            std::cout << "\n";
                        }
                    }
                }
            }
            res->end();
        });

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
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end("Bad request."); 
        });

    std::cout << "Network ready.\n";
    app.run();
    std::cin.ignore();
}
