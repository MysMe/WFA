#pragma once
#include "Network.h"
#include "Response/Response.h"

namespace webRoute
{
	/*
	Add supplier
	Update supplier

	Add part group
	Get parts in group

	Add part
	Update part
	Search parts by supplier
	Search parts by (partial) name
	*/


    void createSupplier(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("name"))
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

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SUPPLIERS] + " (ID, NAME, PHONE, EMAIL) VALUES (NULL, :NAM, :PHO, :EMA);", {
                {":NAM", std::string(b.getElement("name"))},
                {":PHO", b.hasElement("phone") ? std::string(b.getElement("phone")) : "NULL"},
                {":EMA", b.hasElement("email") ? std::string(b.getElement("email")) : "NULL"} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new supplier (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void updateSupplier(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("name"))
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

        const std::string updateStatement = generateUpdateStatement(b, { {"rename", "NAME"}, {"phone", "PHONE"}, {"email", "EMAIL"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus("200");
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::SUPPLIERS] + " SET " + updateStatement + " WHERE NAME = :NAM", { {":NAM", b.getElement("name")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated supplier (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }


    void createPartGroup(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("name"))
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

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::PARTGROUPS] + " (NAME) VALUES (:NAM);", {{":NAM", std::string(b.getElement("name"))}});

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new part group (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void updatePartGroup(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "name", "rename" }))
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

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTGROUPS] + " SET NAME = :RNM WHERE NAME = :NAM", 
            { {":NAM", b.getElement("name")}, {":RNM", b.getElement("rename")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated part group (\"" << b.getElement("name") << "\"/\"" << b.getElement("rename") << "\").\n";
        }
        res->end();
    }


    void createPart(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "name", "quantity", "supplier", "price" }))
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



        const auto [supstatus, supresult] = serverData::database->query("SELECT ID FROM " + serverData::tableNames[serverData::SUPPLIERS] + " WHERE NAME = :SUP", { {":SUP", b.getElement("supplier")} });
        if (!supstatus || supresult.rowCount() != 1)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        std::string_view groupID = "NULL";

        if (b.hasElement("group"))
        {
            const auto [groupstatus, groupresult] = serverData::database->query("SELECT ID FROM " + serverData::tableNames[serverData::PARTGROUPS] + " WHERE NAME = :GRP", { {":GRP", b.getElement("group")} });
            if (!groupstatus || groupresult.rowCount() != 1)
            {
                //Internal server error
                res->writeStatus("500");
                res->end();
                return;
            }
        }


        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::PARTS] + " (ID, NAME, QUANTITY, SUPPLIER, PRICE, SIMILAR) VALUES (NULL, :NAM, :QUA, :SUP, :PRI, :SIM);", {
                {":NAM", b.getElement("name")},
                {":QUA", b.getElement("quantity")},
                {":SUP", b.getElement("supplier")},
                {":PRI", b.getElement("price")},
                {":SIM", groupID} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new part (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void updatePart(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("name"))
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

        const std::string updateStatement = generateUpdateStatement(b, { {"rename", "NAME"}, {"quantity", "QUANTITY"}, {"supplier", "SUPPLIER"}, {"price", "PRICE"}, {"group", "SIMILAR"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus("200");
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::SUPPLIERS] + " SET " + updateStatement + " WHERE NAME = :NAM", { {":NAM", b.getElement("name")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated part (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void searchParts(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("group") && !q.hasElement("supplier") && !q.hasElement("name"))
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

        std::string searchTerm = "WHERE ";
        bool chained = false;
        if (q.hasElement("group"))
        {
            const auto& val = q.getElement("group");
            searchTerm += "WHERE G.NAME LIKE '%";
            searchTerm.append(val.data(), val.size());
            searchTerm += "%'";
            chained = true;
        }
        if (q.hasElement("supplier"))
        {
            if (chained)
                searchTerm += " AND ";
            const auto& val = q.getElement("supplier");
            searchTerm += "S.NAME LIKE '%";
            searchTerm.append(val.data(), val.size());
            searchTerm += "%'";
            chained = true;
        }
        if (q.hasElement("name"))
        {
            if (chained)
                searchTerm += " AND ";
            const auto& val = q.getElement("name");
            searchTerm += "P.NAME LIKE '%";
            searchTerm.append(val.data(), val.size());
            searchTerm += "%'";
        }

        const auto [status, result] =
            serverData::database->query("SELECT P.NAME, P.QUANTITY, P.PRICE, S.NAME, G.NAME FROM " + serverData::tableNames[serverData::PARTS] +
                " AS P LEFT JOIN " + serverData::tableNames[serverData::PARTGROUPS] + " AS G INNER JOIN " + serverData::tableNames[serverData::SUPPLIERS] +
                " AS S " + searchTerm, {});

        if (!status)
        {
            //Internal server error
            res->writeStatus("500");
            res->end();
            return;
        }

        responseWrapper response;
        for (size_t i = 0; i < result.rowCount(); i++)
        {
            responseWrapper temp;
            temp.add("Part name", result[i][0]);
            temp.add("Part price", result[i][2]);
            temp.add("Current stock", result[i][1]);
            temp.add("Supplied by", result[i][3]);
            temp.add("Group", result[i][4]);
            response.add("Parts", std::move(temp), true);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " searched parts.\n";
        res->tryEnd(response.toData(false));
    }
}