#pragma once
#include "Network.h"
#include "Response.h"

namespace webRoute
{
    void createUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "username", "password", "permission" }) || 
            b.getElement("username").empty() || b.getElement("password").empty() || b.getElement("permission").empty())
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        unsigned int requestedPermLevel;
        {
            const auto str = b.getElement("permission");
            auto result = std::from_chars(str.data(), str.data() + str.size(), requestedPermLevel);
            if (result.ec != std::errc())
            {
                //Bad Request - Invalid arguments
                res->writeStatus(HTTPCodes::BADREQUEST);
                res->end();
                return;
            }
        }
        if (!serverData::auth->verify(req, authLevel::employee) || 
            !serverData::auth->verify(req, static_cast<authLevel>(requestedPermLevel)))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::USER] + " (ID, USERNAME, PASSWORD, PERMISSIONS) VALUES (NULL, :USR, :PAS, :PER);", {
                {":USR", std::string(b.getElement("username"))},
                {":PAS", std::string(b.getElement("password"))},
                {":PER", std::string(b.getElement("permission"))} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") created new user (\"" << b.getElement("username") << "\").\n";
        }
        res->end();
    }

    void getLocalUserData(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        const auto user = serverData::auth->getSessionUser(req);
        if (!user.has_value())
        {
            //Either an invalid session or invalid user, regardless this is an authentication error

            //Unauthorised
            res->writeStatus(HTTPCodes::UNAUTHORISED);
            res->end();
            return;
        }

        const auto [userStatus, userResult] = serverData::database->query("SELECT ID, USERNAME, PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE ID = :USR",
            { {":USR", std::to_string(user.value())} });
        if (!userStatus)
        {          
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        const auto [vehStatus, vehResult] = 
            serverData::database->query(
                "SELECT V.ID, V.PLATE, VS.MAKE, VS.MODEL, V.YEAR, V.COLOUR FROM " + serverData::tableNames[serverData::VEHICLES] + " AS V INNER JOIN " +
                 serverData::tableNames[serverData::VEHICLESHARED] + " AS VS ON V.BASE = VS.ID WHERE V.OWNER = :USR;", { {":USR", std::to_string(user.value())} });

        if (!vehStatus)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        responseWrapper response;
        response.add("ID", userResult[0][0]);
        response.add("Username", userResult[0][1]);
        response.add("Permissions", userResult[0][2]);
        for (size_t i = 0; i < vehResult.rowCount(); i++)
        {
            responseWrapper temp;
            temp.add("ID", vehResult[i][0]);
            temp.add("Plate", vehResult[i][1]);
            temp.add("Make", vehResult[i][2]);
            temp.add("Model", vehResult[i][3]);
            temp.add("Year", vehResult[i][4]);
            temp.add("Colour", vehResult[i][5]);
            response.add("Vehicles", std::move(temp), true);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") accessed user data for (\"" << userResult[0][1] << "\").\n";

        res->tryEnd(response.toData(false));
    }

    void searchUsers(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("username", true))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") searched for user (\"" << q.getElement("username") << "\").\n";

        const auto [status, result] = serverData::database->query("SELECT ID, USERNAME, PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE USERNAME LIKE :USR ORDER BY USERNAME", 
            { {":USR", generateLIKEArgument(q.getElement("username"))} });
        if (!status)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        if (result.rowCount() != 0)
        {
            responseWrapper response;
            for (size_t i = 0; i < result.rowCount(); i++)
            {
                const auto [vehStatus, vehResult] =
                    serverData::database->query(
                        "SELECT V.PLATE, VS.MAKE, VS.MODEL, V.YEAR, V.COLOUR FROM VEHICLES AS V INNER JOIN VEHICLESHAREDDATA AS VS ON V.BASE = VS.ID WHERE V.OWNER = :USR;", { {":USR", result[i][0]} });

                if (!vehStatus)
                {
                    //Internal Server Error
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }

                responseWrapper temp;
                temp.add("ID", result[i][0]);
                temp.add("Username", result[i][1]);
                temp.add("Permissions", result[i][2]);
                for (size_t u = 0; u < vehResult.rowCount(); u++)
                {
                    responseWrapper vehicleResponse;
                    vehicleResponse.add("Vehicle plate", vehResult[u][0]);
                    vehicleResponse.add("Vehicle Make", vehResult[u][1]);
                    vehicleResponse.add("Vehicle Model", vehResult[u][2]);
                    vehicleResponse.add("Vehicle Year", vehResult[u][3]);
                    vehicleResponse.add("Vehicle Colour", vehResult[u][4]);
                    temp.add("Vehicles", std::move(vehicleResponse), true);
                }
                response.add("Users", std::move(temp));
            }
            res->tryEnd(response.toData(false));
            return;
        }
        else
        {
            //No content
            res->writeStatus(HTTPCodes::NOTFOUND);
        }
        res->end();
    }

    void selectUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") selected user (\"" << q.getElement("ID") << "\").\n";

        const auto [status, result] = serverData::database->query("SELECT ID, USERNAME, PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE ID = :ID",
            { {":ID", q.getElement("ID")} });
        if (!status)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        if (result.rowCount() != 0)
        {

            const auto [vehStatus, vehResult] =
                serverData::database->query(
                    "SELECT V.ID, V.PLATE, VS.MAKE, VS.MODEL, V.YEAR, V.COLOUR FROM VEHICLES AS V INNER JOIN VEHICLESHAREDDATA AS VS ON V.BASE = VS.ID WHERE V.OWNER = :USR;", { {":USR", result[0][0]} });

            if (!vehStatus)
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            responseWrapper response;
            response.add("ID", result[0][0]);
            response.add("Username", result[0][1]);
            response.add("Permissions", result[0][2]);
            for (size_t i = 0; i < vehResult.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("ID", vehResult[i][0]);
                temp.add("Plate", vehResult[i][1]);
                temp.add("Make", vehResult[i][2]);
                temp.add("Model", vehResult[i][3]);
                temp.add("Year", vehResult[i][4]);
                temp.add("Colour", vehResult[i][5]);
                response.add("Vehicles", std::move(temp), true);
            }
            res->tryEnd(response.toData(false));
            return;
        }
        else
        {
            //No content
            res->writeStatus(HTTPCodes::NOTFOUND);
        }
        res->end();
    }


    void deleteUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const auto [pstatus, presult] = serverData::database->query("SELECT PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
        if (!pstatus)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        if (presult.rowCount() == 0)
        {
            //OK, nothing to delete
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        {
            unsigned int deletedPermissions;

            auto result = std::from_chars(presult[0][0].data(), presult[0][0].data() + presult[0][0].size(), deletedPermissions);
            if (result.ec != std::errc())
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            if (!serverData::auth->verify(req, static_cast<authLevel>(deletedPermissions)))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }


        const auto [status, result] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::USER] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
        if (!status)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") deleted user (\"" << b.getElement("ID") << "\").\n";
        res->end();
    }

    void updateUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        if (b.hasElement("permission"))
        {
            unsigned int requestedPermLevel;
            const auto str = b.getElement("permission");
            auto result = std::from_chars(str.data(), str.data() + str.size(), requestedPermLevel);
            if (result.ec != std::errc())
            {
                //Bad Request - Invalid arguments
                res->writeStatus(HTTPCodes::BADREQUEST);
                res->end();
                return;
            }
            if (!serverData::auth->verify(req, static_cast<authLevel>(requestedPermLevel)))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }

        {
            const auto [pstatus, presult] = serverData::database->query("SELECT PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
            if (!pstatus)
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            if (presult.rowCount() == 0)
            {

                res->writeStatus(HTTPCodes::BADREQUEST);
                res->end();
                return;
            }

            unsigned int modifiedPermissions;

            auto result = std::from_chars(presult[0][0].data(), presult[0][0].data() + presult[0][0].size(), modifiedPermissions);
            if (result.ec != std::errc())
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            if (!serverData::auth->verify(req, static_cast<authLevel>(modifiedPermissions)))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }

        }

        const std::string updateStatement = generateUpdateStatement(b, { {"username", "USERNAME"}, {"password", "PASSWORD"}, {"permissions", "PERMISSIONS"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::USER] + " SET " + updateStatement + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") updated user (\"" << b.getElement("ID") << "\").\n";
        res->end();
    }

}