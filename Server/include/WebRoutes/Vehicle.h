#pragma once
#include "Network.h"
#include "Response.h"

namespace webRoute
{
    void createVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "plate", "make", "model", "owner", "year", "colour" }))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        const auto usr = serverData::auth->getSessionUser(req);
        if (!serverData::auth->verify(req, authLevel::manager) &&
            !serverData::auth->isSessionUser(req, b.getElement("owner")))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const auto [vStatus, vResult] = serverData::database->query("INSERT OR IGNORE INTO " + serverData::tableNames[serverData::VEHICLESHARED] + "(MAKE, MODEL) VALUES (:MAK, :MOD)",
            { {":MAK", b.getElement("make")}, {":MOD", b.getElement("model")} });
        if (!vStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::VEHICLES] + " (PLATE, BASE, OWNER, YEAR, COLOUR) VALUES (:PLT, " + 
            "(SELECT ID FROM " + serverData::tableNames[serverData::VEHICLESHARED] + " WHERE MAKE = :MAK AND MODEL = :MOD), :OWN, :YEA, :COL);", {
                {":PLT", b.getElement("plate")},
                {":MAK", b.getElement("make")},
                {":MOD", b.getElement("model")},
                {":OWN", b.getElement("owner")},
                {":YEA", b.getElement("year")},
                {":COL", b.getElement("colour")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") added new vehicle for (\"" << b.getElement("owner") << "\").\n";
        }
        res->end();
    }

    void updateVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        {
            const auto [status, result] = serverData::database->query("SELECT OWNER FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
            if (!serverData::auth->isSessionUserFromID(req, result[0][0]) && !serverData::auth->verify(req, authLevel::manager))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                    res->end();
                    return;
            }
        }

        //Note the year, make or model of the vehicle may not be updated as they can not logically change
        //Nor may the user, vehicles are not to be transferred, but may be deleted and added anew
        const std::string updateStatement = generateUpdateStatement(b, { {"plate", "PLATE"}, {"colour", "COLOUR"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::VEHICLES] + " SET " + updateStatement + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") updated vehicle (\"" << b.getElement("ID") << "\").\n";
        res->end();
    }

    void deleteVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        {
            const auto [status, result] = serverData::database->query("SELECT OWNER FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
            if (!serverData::auth->isSessionUserFromID(req, result[0][0]) && !serverData::auth->verify(req, authLevel::manager))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }

        const auto [status, result] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
        if (!status)
        {
            //Internal Server Error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") deleted vehicle (\"" << b.getElement("ID") << "\").\n";
        res->end();
    }

    void selectVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        {
            const auto [status, result] = serverData::database->query("SELECT OWNER FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE ID = :ID", { {":ID", q.getElement("ID")} });
            if (!serverData::auth->isSessionUser(req, result[0][0]) && !serverData::auth->verify(req, authLevel::manager))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }

        const auto [vehStatus, vehResult] =
             serverData::database->query(
                    "SELECT V.ID, V.PLATE, VS.MAKE, VS.MODEL, V.YEAR, V.COLOUR FROM VEHICLES AS V INNER JOIN VEHICLESHAREDDATA AS VS ON V.BASE = VS.ID WHERE V.ID = :ID;", { {":ID", q.getElement("ID")} });

            if (!vehStatus)
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            if (vehResult.rowCount() != 0)
            {
                responseWrapper response;
                response.add("ID", vehResult[0][0]);
                response.add("Plate", vehResult[0][1]);
                response.add("Make", vehResult[0][2]);
                response.add("Model", vehResult[0][3]);
                response.add("Year", vehResult[0][4]);
                response.add("Colour", vehResult[0][5]);
                response.add("Owner", q.getElement("ID"));
                res->tryEnd(response.toData(false));
            }
            else
            {
                res->writeStatus(HTTPCodes::NOTFOUND);
                res->end();
            }
            return;

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") selected vehicle (\"" << b.getElement("ID") << "\").\n";
        res->end();
    }

}