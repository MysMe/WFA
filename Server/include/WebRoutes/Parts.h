#pragma once
#include "Network.h"
#include "Response.h"

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
        if (!b.hasElement("name") || b.getElement("name").empty())
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
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
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new supplier (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void updateSupplier(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("name") || (b.hasElement("rename") && b.getElement("rename").empty()))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const std::string updateStatement = generateUpdateStatement(b, { {"rename", "NAME"}, {"phone", "PHONE"}, {"email", "EMAIL"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::SUPPLIERS] + " SET " + updateStatement + " WHERE NAME = :NAM", { {":NAM", b.getElement("name")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated supplier (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void searchSuppliers(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("searchterm", true))
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

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") searched for supplier with keyword \"" << q.getElement("searchterm") << "\".\n";

        const auto [status, result] = serverData::database->query("SELECT ID, NAME, PHONE, EMAIL FROM " + serverData::tableNames[serverData::SUPPLIERS] + 
            " WHERE NAME LIKE :VAL OR NAME LIKE :VAL OR PHONE LIKE :VAL OR EMAIL LIKE :VAL",
            { {":VAL", generateLIKEArgument(q.getElement("searchterm"))} });
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
                responseWrapper temp;
                temp.add("ID", result[i][0]);
                temp.add("Name", result[i][1]);
                temp.add("Phone", result[i][2]);
                temp.add("Email", result[i][3]);
                response.add("Suppliers", std::move(temp));
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

    void selectSupplier(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
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

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") selected supplier (\"" << q.getElement("ID") << "\").\n";

        const auto [status, result] = serverData::database->query("SELECT ID, NAME, PHONE, EMAIL FROM " + serverData::tableNames[serverData::SUPPLIERS] + " WHERE ID = :ID",
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
            responseWrapper response;
            response.add("ID", result[0][0]);
            response.add("Name", result[0][1]);
            response.add("Phone", result[0][2]);
            response.add("Email", result[0][3]);
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



    void createPartGroup(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("name") || b.getElement("name").empty())
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::PARTGROUPS] + " (NAME) VALUES (:NAM);", {{":NAM", std::string(b.getElement("name"))}});

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new part group (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void updatePartGroup(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "name", "rename" }) || b.getElement("rename").empty())
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTGROUPS] + " SET NAME = :RNM WHERE NAME = :NAM", 
            { {":NAM", b.getElement("name")}, {":RNM", b.getElement("rename")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated part group (\"" << b.getElement("name") << "\"/\"" << b.getElement("rename") << "\").\n";
        }
        res->end();
    }

    void searchPartGroups(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("name", true))
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

        const auto [status, result] =
            serverData::database->query("SELECT ID, NAME FROM " + serverData::tableNames[serverData::PARTGROUPS] +
                " WHERE NAME LIKE :NAM", { {":NAM", generateLIKEArgument(q.getElement("name"))} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        if (result.rowCount() != 0)
        {
            responseWrapper response;
            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("ID", result[i][0]);
                temp.add("Name", result[i][1]);
                response.add("Groups", std::move(temp), true);
            }

            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " searched part groups for " << q.getElement("name") << ".\n";
            res->tryEnd(response.toData(false));
        }
        else
        {
            //No content
            res->writeStatus(HTTPCodes::NOTFOUND);
            res->end();
        }
    }

    void selectPartGroup(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
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

        const auto [status, result] =
            serverData::database->query("SELECT ID, NAME FROM " + serverData::tableNames[serverData::PARTGROUPS] +
                " WHERE ID = :ID", { {":ID", q.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " selected group " << q.getElement("ID") << ".\n";

        if (result.rowCount() != 0)
        {
            responseWrapper response;
            response.add("ID", result[0][0]);
            response.add("Name", result[0][1]);
            res->tryEnd(response.toData(false));
        }
        else
        {
            res->writeStatus(HTTPCodes::NOTFOUND);
            res->end();
        }
    }


    void createPart(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "name", "quantity", "supplier", "price" }) || 
            b.getElement("name").empty() || b.getElement("quantity").empty() || b.getElement("supplier").empty() || b.getElement("price").empty())
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
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
                res->writeStatus(HTTPCodes::INTERNALERROR);
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
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " created new part (\"" << b.getElement("name") << "\").\n";
        }
        res->end();
    }

    void updatePart(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID") || (b.hasElement("rename") && b.getElement("rename").empty()))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (!serverData::auth->verify(req, authLevel::manager))
        {
            //Forbidden - Insufficient permissions
            res->writeStatus(HTTPCodes::FORBIDDEN);
            res->end();
            return;
        }

        const std::string updateStatement = generateUpdateStatement(b, { {"name", "NAME"}, {"quantity", "QUANTITY"}, {"supplier", "SUPPLIER"}, {"price", "PRICE"}, {"group", "SIMILAR"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTS] + " SET " + updateStatement + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " updated part (\"" << b.getElement("ID") << "\").\n";
        }
        res->end();
    }

    void searchParts(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("name", true) && !q.hasElement("group", true))
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

        std::unordered_map<std::string_view, std::string_view> searchVals;
        std::string likeName;
        if (q.hasElement("name", true))
        {
            likeName = generateLIKEArgument(q.getElement("name"));
            searchVals[":NAM"] = likeName;
        }
        if (q.hasElement("group", true))
            searchVals[":ID"] = q.getElement("group");

        std::string searchTerm;
        if (q.hasElement("name", true))
        {
            searchTerm += " P.NAME LIKE :NAM";
            if (q.hasElement("group", true))
                searchTerm += " AND";
        }
        if (q.hasElement("group", true))
            searchTerm += " G.ID = :ID";

        const auto [status, result] =
            serverData::database->query("SELECT P.ID, P.NAME, P.PRICE, P.QUANTITY, S.NAME, G.ID FROM " + serverData::tableNames[serverData::PARTS] +
                " AS P LEFT JOIN " + serverData::tableNames[serverData::PARTGROUPS] + " AS G ON P.SIMILAR = G.ID " +
                "INNER JOIN " + serverData::tableNames[serverData::SUPPLIERS] +
                " AS S ON P.SUPPLIER = S.ID WHERE " + searchTerm, searchVals);

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        responseWrapper response;
        for (size_t i = 0; i < result.rowCount(); i++)
        {
            responseWrapper temp;
            temp.add("ID", result[i][0]);
            temp.add("Name", result[i][1]);
            temp.add("Price", result[i][2]);
            temp.add("Quantity", result[i][3]);
            temp.add("Supplier", result[i][4]);
            temp.add("GroupID", result[i][5]);
            response.add("Parts", std::move(temp), true);
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " searched parts for " << (q.hasElement("name", true) ? q.getElement("name") : q.getElement("group")) << ".\n";
        res->tryEnd(response.toData(false));
    }

    void selectPart(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
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

        const auto [status, result] =
            serverData::database->query("SELECT P.ID, P.NAME, P.PRICE, P.QUANTITY, S.NAME, G.ID FROM " + serverData::tableNames[serverData::PARTS] +
                " AS P LEFT JOIN " + serverData::tableNames[serverData::PARTGROUPS] + " AS G INNER JOIN " + serverData::tableNames[serverData::SUPPLIERS] +
                " AS S WHERE P.ID = :ID", { {":ID", q.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << " selected part " << q.getElement("ID") << ".\n";

        if (result.rowCount() != 0)
        {
            responseWrapper response;
            response.add("ID", result[0][0]);
            response.add("Name", result[0][1]);
            response.add("Price", result[0][2]);
            response.add("Quantity", result[0][3]);
            response.add("Supplier", result[0][4]);
            response.add("GroupID", result[0][5]);
            res->tryEnd(response.toData(false));
        }
        else
        {
            res->writeStatus(HTTPCodes::NOTFOUND);
            res->end();
        }
    }
}