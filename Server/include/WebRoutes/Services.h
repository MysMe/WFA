#pragma once
#include "Network.h"
#include "Response/Response.h"

namespace webRoute
{
	/*
	Create service request (user-side)
	Authorise a request
	Update a request
	Close a request
	Reopen a request (clearing all closure data)
	*/

    void createRequest(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "plate", "request"}))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::manager))
        {
            const auto [status, result] = serverData::database->query("SELECT U.ID FROM " + serverData::tableNames[serverData::VEHICLES] + " AS V INNER JOIN " + serverData::tableNames[serverData::USER] + 
                "AS U WHERE V.PLATE = :PLT", { {":PLT", b.getElement("plate")} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
                res->end();
                return;
            }
            if (!serverData::auth->isSessionUserFromID(req, result[0][0]))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus("403");
                res->end();
                return;
            }
        }

        const auto [sStatus, sResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICESHARED] + "(VEHICLE, REQUESTED, REQUEST) VALUES " + 
            "((SELECT ID FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE PLATE = :PLT), (SELECT date('now')), :REQ)",
            { {":PLT", b.getElement("plate")}, {":REQ", b.getElement("request")} });
        if (!sStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " (SERVICE) VALUES ((SELECT last_insert_rowid()))", { {} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " requested a new service.\n";
        }
        res->end();
    }

    void authoriseRequest(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID") && !b.hasElement("quote"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }

        const auto [sStatus, sResult] = serverData::database->query("SELECT SERVICE FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
        if (!sStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        const auto [dStatus, dResult] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });
        if (!dStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        std::string user = std::to_string(serverData::auth->getSessionUser(req).value());

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEACTIVE] + " (SERVICE, LABOUR, NOTES, AUTHORISER, QUOTE) VALUES " + 
            "(:ID, 0, :NOT, :UID, :QOT)", {
            {":ID", b.getElement("ID")},
            {":NOT", b.hasElement("notes") ? b.getElement("notes") : ""},
            {":UID", user},
            {":QOT", b.getElement("quote")} });

        if (!status)
        {
            //TODO:# Add rollback
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        const auto [oStatus, oResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEOPEN] + " (SERVICE) VALUES ((SELECT last_insert_rowid()))", { {} });

        if (!oStatus)
        {
            //TODO:# Add rollback
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " authorised a service as \"" + user + "\".\n";
        }
        res->end();
    }

    void updateService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }

        const std::string updateStatement = generateUpdateStatement(b, { {"notes", "NOTES"}, {"hours", "LABOUR"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus("200");
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::SERVICEACTIVE] + " SET " + updateStatement + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated a service.\n";
        }
        res->end();
    }

    void closeService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }
        std::string user = std::to_string(serverData::auth->getSessionUser(req).value());

        const auto [dStatus, dResult] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::SERVICEOPEN] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!dStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }


        const auto [sStatus, sResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICECLOSED] + "(SERVICE, COMPLETER, COMPLETED) VALUES (:ID, (SELECT date('now')), :USR)",
            { {":ID", b.getElement("ID")}, {":USR", user} });
        if (!sStatus)
        {
            //TODO:# Add rollback
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " closed a service.\n";
        }
        res->end();
    }

    void reopenService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }
        std::string user = std::to_string(serverData::auth->getSessionUser(req).value());

        const auto [dStatus, dResult] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::SERVICECLOSED] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!dStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }


        const auto [sStatus, sResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEOPEN] + "(SERVICE) VALUES (:ID)",
            { {":ID", b.getElement("ID")} });
        if (!sStatus)
        {
            //TODO:# Add rollback
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " reopened a service.\n";
        }
        res->end();
    }

    /*
    Add part to service
    Remove part from service
    List parts used in service
    */

    void addPartToService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "serviceID", "partID" }))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }
        if (b.hasElement("quantity"))
        {
            uint64_t quantity;
            const auto& val = b.getElement("quantity");
            auto result = std::from_chars(val.data(), val.data() + val.size(), quantity);
            if (result.ec != std::errc() || quantity <= 0)
            {
                //Bad Request - Invalid arguments
                res->writeStatus("400");
                res->end();
                return;
            }
        }

        const auto [searchStatus, searchResult] = serverData::database->query("SELECT QUANTITY, ID FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " INNER JOIN " + serverData::tableNames[serverData::SERVICEOPEN] +
            " AS S WHERE S.ID = :SID AND SERVICE = S.SERVICE AND PART = :PRT", { {":PRT", b.getElement("partID")}, {":SID", b.getElement("serviceID")} });
        if (!searchStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        if (searchResult.rowCount() != 0)
        {
            const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTSINSERVICE] + " SET QUANTITY = QUANTITY + :QNT WHERE ID = :ID",
                { {":ID", searchResult[0][1]}, {":QNT", b.hasElement("quantity") ? b.getElement("quantity") : "1"} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
            }
            else
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " added existing parts to a service.\n";
            }
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::PARTSINSERVICE] + "(PART, QUANITY, PRICE, SERVICE) VALUES " +
            "(:PRT, :QNT, (SELECT PRICE FROM PARTS WHERE ID = :PRT), :SRV)",
            { {":PRT", b.getElement("partID")}, {":QNT", b.hasElement("quantity") ? b.getElement("quantity") : "1"}, {":SRV", b.getElement("serviceID")} });
        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " added new parts to a service.\n";
        }
        res->end();
    }

    void removePartFromService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "serviceID", "partID" }))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }

        const auto [searchStatus, searchResult] = serverData::database->query("SELECT QUANTITY, ID FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " INNER JOIN " + serverData::tableNames[serverData::SERVICEOPEN] +
            " AS S WHERE S.ID = :SID AND SERVICE = S.SERVICE AND PART = :PRT", { {":PRT", b.getElement("partID")}, {":SID", b.getElement("serviceID")} });
        if (!searchStatus)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }
        if (searchResult.rowCount() == 0)
        {
            //Invalid Arguments
            res->writeStatus("400");
            res->end();
            return;
        }

        uint64_t currentQuantity, removedQuantity = 1;
        {
            const auto result = std::from_chars(searchResult[0][0].data(), searchResult[0][0].data() + searchResult[0][0].size(), currentQuantity);
            if (result.ec != std::errc())
            {
                //Internal server error
                res->writeStatus("500");
                res->end();
                return;
            }
        }
        if (b.hasElement("quantity"))
        {
            const auto& val = b.getElement("quantity");
            const auto result = std::from_chars(val.data(), val.data() + val.size(), removedQuantity);
            if (result.ec != std::errc())
            {
                //Invalid Arguments
                res->writeStatus("400");
                res->end();
                return;
            }
        }

        if (removedQuantity <= 0)
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }

        if (removedQuantity > currentQuantity)
        {
            const auto [status, result] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " WHERE ID = :ID", { {":ID", searchResult[0][1]} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
            }
            else
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " removed a set of existing parts from a service.\n";
            }
            res->end();
            return;

        }
        else
        {
            const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTSINSERVICE] + " SET QUANTITY = QUANTITY - :QNT WHERE ID = :ID",
                { {":QNT", b.hasElement("quantity") ? b.getElement("quantity") : "1"}, {":ID", searchResult[0][1]} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
            }
            else
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " removed some existing parts from a service.\n";
            }
            res->end();
        }
    }

    /*
    Get open services by client
    Get closed services by client
    Get unauthorised services by client

    Get services by authoriser
    Get services by closer
    Get service requests by date
    */

    void searchServicesByClient(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.containsAny({ "unauthorised", "open", "closed" }))
        {
            //Bad Request - Invalid arguments
            res->writeStatus("400");
            res->end();
            return;
        }
        if (q.hasElement("username") && !serverData::auth->verify(req, authLevel::employee))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus("403");
            res->end();
            return;
        }
        if (!serverData::auth->hasSession(req))
        {
            //Unauthorised
            res->writeStatus("401");
            res->end();
            return;
        }

        const std::string userSearch = q.hasElement("username") ? ".USERNAME = :USR" : ".ID = :USR";
        const std::string userVal = q.hasElement("username") ? std::string(q.getElement("username")) : std::to_string(serverData::auth->getSessionUser(req).value());

        responseWrapper response;

        if (q.hasElement("unauthorised"))
        {
            const auto [status, result] = serverData::database->query("SELECT V.PLATE, SS.REQUESTED, SS.REQUEST FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] +
            " AS SUA INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS SS INNER JOIN " + serverData::tableNames[serverData::VEHICLES] + 
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS U WHERE U" + userSearch, { {":USR", userVal} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
                res->end();
                return;
            }

            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("Vehicle Plate", result[i][0]);
                temp.add("Date Requested", result[i][1]);
                temp.add("Request", result[i][2]);
                response.add("Unauthorised", std::move(temp), true);
            }
        }

        if (q.hasElement("open"))
        {
            const auto [status, result] = serverData::database->query(
                "SELECT UO.USERNAME, V.PLATE, SS.REQUESTED, SS.REQUEST, UA.USERNAME, SA.QUOTE, SA.LABOUR, SA.NOTES FROM" +
                                 serverData::tableNames[serverData::SERVICEOPEN] +            " AS O" +
                " INNER JOIN " + serverData::tableNames[serverData::SERVICEACTIVE] +    " AS SA ON O.SERVICE = SA.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] +    " AS SS ON SA.SERVICE = SS.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::VEHICLES] +         " AS V ON SS.VEHICLE = V.ID" + 
                " INNER JOIN " + serverData::tableNames[serverData::USER] +             " AS UO ON V.OWNER = UO.ID" + 
                " INNER JOIN " + serverData::tableNames[serverData::USER] +             " AS UA ON SA.AUTHORISER = UA.ID" +
                " WHERE U " + (b.hasElement("username") ? "O" : "A") + userSearch, { {":USR", userVal} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
                res->end();
                return;
            }

            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("Vehicle Owner", result[i][0]);
                temp.add("Vehicle Plate", result[i][1]);
                temp.add("Date Requested", result[i][2]);
                temp.add("Request", result[i][3]);
                temp.add("Authorised By", result[i][4]);
                temp.add("Quoted", result[i][5]);
                temp.add("Labour Hours", result[i][6]);
                temp.add("Notes", result[i][7]);
                response.add("Open", std::move(temp), true);
            }
        }

        if (q.hasElement("closed"))
        {
            const auto [status, result] = serverData::database->query(
                "SELECT UO.USERNAME, V.PLATE, SS.REQUESTED, CS.COMPLETED, SS.REQUEST, UA.USERNAME, UC.USERNAME, SA.QUOTE, SA.LABOUR, SA.NOTES FROM" +
                serverData::tableNames[serverData::SERVICECLOSED] + " AS CS" +
                " INNER JOIN " + serverData::tableNames[serverData::SERVICEACTIVE] + " AS SA ON CS.SERVICE = SA.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS SS ON SA.SERVICE = SS.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::VEHICLES] + " AS V ON SS.VEHICLE = V.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS UO ON V.OWNER = UO.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS UA ON SA.AUTHORISER = UA.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS UC ON CS.COMPLETER = UC.ID" +
                " WHERE U " + (b.hasElement("username") ? "O" : "A") + userSearch, { {":USR", userVal} });
            if (!status)
            {
                //Internal server error
                res->writeStatus("500");
                res->end();
                return;
            }

            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("Vehicle Owner", result[i][0]);
                temp.add("Vehicle Plate", result[i][1]);
                temp.add("Date Requested", result[i][2]);
                temp.add("Date Completed", result[i][3]);
                temp.add("Request", result[i][4]);
                temp.add("Authorised By", result[i][5]);
                temp.add("Completed By", result[i][6]);
                temp.add("Quoted", result[i][7]);
                temp.add("Labour Hours", result[i][8]);
                temp.add("Notes", result[i][9]);
                response.add("Completed", std::move(temp), true);
            }
        }

        res->tryEnd(response.toData(false));
    }
}