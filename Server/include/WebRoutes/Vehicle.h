#pragma once
#include "Network.h"
#include "Response/Response.h"

namespace webRoute
{
	/*
	Add vehicle
	Remove vehicle
	Update vehicle
	*/

    void createVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "plate", "make", "model", "owner", "year", "colour" }))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->isSessionUser(req, b.getElement("owner")) && !serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }

        const auto [vStatus, vResult] = serverData::database->query("INSERT IGNORE INTO " + serverData::tableNames[serverData::VEHICLESHARED] + "(MAKE, MODEL) VALUES (:MAK, :MOD)",
            { {":MAK", b.getElement("make")}, {":MOD", b.getElement("model")} });
        if (!vStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::VEHICLES] + " (PLATE, BASE, OWNER, YEAR, COLOUR) VALUES (:PLT, " + 
            "(SELECT ID FROM " + serverData::tableNames[serverData::VEHICLESHARED] + " WHERE MAKE = :MAK AND MODEL = :MOD), " +
            "(SELECT ID FROM " + serverData::tableNames[serverData::USER] + " WHERE USERNAME = :USR), :YEA, :COL);", {
                {":PLT", b.getElement("plate")},
                {":MAK", b.getElement("make")},
                {":MOD", b.getElement("model")},
                {":USR", b.getElement("username")},
                {":YEA", b.getElement("year")},
                {":COL", b.getElement("colour")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " added new vehicle for (\"" << b.getElement("owner") << "\").\n";
        }
        res->end();
    }

    void updateVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("plate"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }

        {
            const auto [status, result] = serverData::database->query("SELECT U.ID FROM " + serverData::tableNames[serverData::USER] + " AS U INNER JOIN VEHICLES AS V WHERE V.PLATE = :PLT", { {":PLT", b.getElement("plate")} });
            if (!serverData::auth->isSessionUserFromID(req, result[0][0]) && !serverData::auth->verify(req, authLevel::manager))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus("403");
                    res->end();
                    return;
            }
        }

        //Note the year, make or model of the vehicle may not be updated as they can not logically change
        //Nor may the user, vehicles are not to be transferred, but may be deleted and added anew
        const std::string updateStatement = generateUpdateStatement(b, { {"replate", "PLATE"}, {"colour", "COLOUR"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus("200");
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::VEHICLES] + " SET " + updateStatement + " WHERE PLATE = :PLT", { {":PLT", b.getElement("plate")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") updated vehicle (\"" << q.getElement("plate") << "\").\n";
        res->end();
    }

    void deleteVehicle(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("plate"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }

        {
            const auto [status, result] = serverData::database->query("SELECT U.ID FROM " + serverData::tableNames[serverData::USER] + " AS U INNER JOIN VEHICLES AS V WHERE V.PLATE = :PLT", { {":PLT", b.getElement("plate")} });
            if (!serverData::auth->isSessionUserFromID(req, result[0][0]) && !serverData::auth->verify(req, authLevel::manager))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus("403");
                res->end();
                return;
            }
        }

        const auto [status, result] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE PLATE = :PLT", { {":PLT", b.getElement("plate")} });
        if (!status)
        {
            //Internal Server Error
            res->writeStatus("500");
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") deleted vehicle (\"" << b.getElement("plate") << "\").\n";
        res->end();
    }

}