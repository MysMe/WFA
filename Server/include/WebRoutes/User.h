#pragma once
#include "Network.h"
#include "Response.h"

namespace webRoute
{
	//Create
	//Read (local)
	//Search
	//Delete
	//Update

    void createUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "username", "password", "permission" }))
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
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new user (\"" << b.getElement("username") << "\").\n";
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

        const auto [userStatus, userResult] = serverData::database->query("SELECT USERNAME, PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE ID = :USR",
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
"SELECT VS.MAKE, VS.MODEL, V.YEAR, V.COLOUR FROM VEHICLES AS V INNER JOIN VEHICLESHAREDDATA AS VS ON V.BASE = VS.ID WHERE V.OWNER = :USR;", { {":USR", std::to_string(user.value())} });

        if (!vehStatus)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        responseWrapper response;
        response.add("Username", userResult[0][0]);
        response.add("Permissions", userResult[0][1]);
        for (size_t i = 0; i < vehResult.rowCount(); i++)
        {
            responseWrapper temp;
            temp.add("Vehicle Make", vehResult[i][0]);
            temp.add("Vehicle Model", vehResult[i][1]);
            temp.add("Vehicle Year", vehResult[i][2]);
            temp.add("Vehicle Colour", vehResult[i][3]);
            response.add("Vehicles", std::move(temp), true);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") accessed user data for (\"" << userResult[0][0] << "\").\n";

        res->tryEnd(response.toData(false));
    }

    void searchUsers(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("username"))
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

        const auto [status, result] = serverData::database->query("SELECT ID, USERNAME, PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE USERNAME LIKE :USR", 
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

            const auto [vehStatus, vehResult] =
                serverData::database->query(
                    "SELECT V.PLATE, VS.MAKE, VS.MODEL, V.YEAR, V.COLOUR FROM VEHICLES AS V INNER JOIN VEHICLESHAREDDATA AS VS ON V.BASE = VS.ID WHERE V.OWNER = :USR;", { {":USR", result[0][0]} });

            if (!vehStatus)
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            responseWrapper response;
            response.add("Username", result[0][1]);
            response.add("Permissions", result[0][2]);
            for (size_t i = 0; i < vehResult.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("Vehicle plate", vehResult[i][0]);
                temp.add("Vehicle Make", vehResult[i][1]);
                temp.add("Vehicle Model", vehResult[i][2]);
                temp.add("Vehicle Year", vehResult[i][3]);
                temp.add("Vehicle Colour", vehResult[i][4]);
                response.add("Vehicles", std::move(temp), true);
            }
            res->tryEnd(response.toData(false));
            return;
        }
        else
        {
            //No content
            res->writeStatus(HTTPCodes::NOCONTENT);
        }
        res->end();
    }

    void deleteUser(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("username"))
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

        const auto [pstatus, presult] = serverData::database->query("SELECT PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE USERNAME = :USR", { {":USR", b.getElement("username")} });
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
        if (!b.hasElement("username"))
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
            const auto [pstatus, presult] = serverData::database->query("SELECT PERMISSIONS FROM " + serverData::tableNames[serverData::USER] + " WHERE USERNAME = :USR", { {":USR", b.getElement("username")} });
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

        const std::string updateStatement = generateUpdateStatement(b, { {"rename", "USERNAME"}, {"password", "PASSWORD"}, {"permissions", "PERMISSIONS"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::USER] + " SET " + updateStatement + " WHERE USERNAME = :USR", { {":USR", b.getElement("username")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") updated user (\"" << q.getElement("username") << "\").\n";
        res->end();
    }

}