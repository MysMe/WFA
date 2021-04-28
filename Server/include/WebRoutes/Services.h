#pragma once
#include "Network.h"
#include "Response.h"

//Simple string conversion function
std::optional<double> getPartPrice(std::string_view price, std::string_view quantity)
{
    {
        double partPrice;
        size_t partQuantity;
        const auto conv = std::from_chars(price.data(), price.data() + price.size(), partPrice);
        const auto conv_b = std::from_chars(quantity.data(), quantity.data() + quantity.size(), partQuantity);
        if (conv.ec != std::errc() || conv_b.ec != std::errc())
        {
            return {};
        }
        return partPrice * partQuantity;
    }
}

namespace webRoute
{
    void createRequest(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "VID", "request"}))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::manager))
        {
            const auto [status, result] = serverData::database->query("SELECT OWNER FROM " + serverData::tableNames[serverData::VEHICLES] + " WHERE ID = :VID", { {":VID", b.getElement("VID")} });
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }
            if (!serverData::auth->isSessionUserFromID(req, result[0][0]))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }

        const auto [sStatus, sResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICESHARED] + "(VEHICLE, REQUESTED, REQUEST) VALUES " + 
            "(:ID, (SELECT date('now')), :REQ)",
            { {":ID", b.getElement("VID")}, {":REQ", b.getElement("request")} });
        if (!sStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " (SERVICE) VALUES ((SELECT last_insert_rowid()))", {});

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") requested a new service.\n";
        }
        res->end();
    }

    void authoriseRequest(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID") && !b.hasElement("quote"))
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

        const auto [sStatus, sResult] = serverData::database->query("SELECT SERVICE FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " WHERE SERVICE = :ID", { {":ID", b.getElement("ID")} });
        if (!sStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }
        if (sResult.rowCount() != 1)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::NOTFOUND);
            res->end();
            return;
        }


        const auto [dStatus, dResult] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " WHERE SERVICE = :ID", { {":ID", b.getElement("ID")} });
        if (!dStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
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
            //Note that there is no rollback, a real-world system would need to ensure both this operation and the next complete successfully
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        const auto [oStatus, oResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEOPEN] + " (SERVICE) VALUES ((SELECT last_insert_rowid()))", {});

        if (!oStatus)
        {
            //Note that there is no rollback, a real-world system would need to ensure both this operation and the next complete successfully
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") authorised a service as \"" + user + "\".\n";
        }
        res->end();
    }

    void updateService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
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

        const std::string updateStatement = generateUpdateStatement(b, { {"notes", "NOTES"}, {"hours", "LABOUR"} });

        if (updateStatement.empty())
        {
            //OK, nothing to update
            res->writeStatus(HTTPCodes::OK);
            res->end();
            return;
        }

        const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::SERVICEACTIVE] + " SET " + updateStatement + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!status)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") updated a service.\n";
        }
        res->end();
    }

    void closeService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
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
        std::string user = std::to_string(serverData::auth->getSessionUser(req).value());

        const auto [sStatus, sResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICECLOSED] + "(SERVICE, COMPLETED, COMPLETER, PAID) VALUES (:ID, (SELECT date('now')), :USR, :PAD)",
            { {":ID", b.getElement("ID")}, {":USR", user}, {":PAD", b.getElement("paid")} });
        if (!sStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        const auto [dStatus, dResult] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::SERVICEOPEN] + " WHERE SERVICE = :ID", { {":ID", b.getElement("ID")} });

        if (!dStatus)
        {
            //Note that there is no rollback, a real-world system would need to ensure both this operation and the next complete successfully
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }


        std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") closed a service.\n";
        res->end();
    }

    void reopenService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("ID"))
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
        std::string user = std::to_string(serverData::auth->getSessionUser(req).value());

        const auto [dStatus, dResult] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::SERVICECLOSED] + " WHERE ID = :ID", { {":ID", b.getElement("ID")} });

        if (!dStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }


        const auto [sStatus, sResult] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::SERVICEOPEN] + "(SERVICE) VALUES (:ID)",
            { {":ID", b.getElement("ID")} });
        if (!sStatus)
        {
            //Note that there is no rollback, a real-world system would need to ensure both this operation and the next complete successfully
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }
        else
        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") reopened a service.\n";
        }
        res->end();
    }

    void addPartToService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.containsAll({ "serviceID", "partID" }))
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
        if (b.hasElement("quantity"))
        {
            uint64_t quantity;
            const auto& val = b.getElement("quantity");
            auto result = std::from_chars(val.data(), val.data() + val.size(), quantity);
            if (result.ec != std::errc() || quantity <= 0)
            {
                //Bad Request - Invalid arguments
                res->writeStatus(HTTPCodes::BADREQUEST);
                res->end();
                return;
            }
        }

        const auto [searchStatus, searchResult] = serverData::database->query(
            "SELECT ID FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " WHERE SERVICE = :SID AND PART = :PRT", { {":PRT", b.getElement("partID")}, {":SID", b.getElement("serviceID")} });
        if (!searchStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }

        if (searchResult.rowCount() != 0)
        {
            const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTSINSERVICE] + " SET QUANTITY = QUANTITY + :QNT WHERE ID = :ID",
                { {":ID", searchResult[0][0]}, {":QNT", b.hasElement("quantity") ? b.getElement("quantity") : "1"} });
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
            }
            else
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") added existing parts to a service.\n";
            }
            res->end();
            return;
        }
        else
        {
            const auto [status, result] = serverData::database->query("INSERT INTO " + serverData::tableNames[serverData::PARTSINSERVICE] + "(PART, QUANTITY, SERVICE) VALUES " +
                "(:PRT, :QNT, :SRV)",
                { {":PRT", b.getElement("partID")}, {":QNT", b.hasElement("quantity") ? b.getElement("quantity") : "1"}, {":SRV", b.getElement("serviceID")} });
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }
        }

        {
            std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") added new parts to a service.\n";
        }
        res->end();
    }

    void removePartFromService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!b.hasElement("entry"))
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

        const auto [searchStatus, searchResult] = serverData::database->query(
            "SELECT QUANTITY FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " WHERE ID = :ID", { {":ID", b.getElement("entry")} });
        if (!searchStatus)
        {
            //Internal server error
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }
        if (searchResult.rowCount() == 0)
        {
            //Invalid Arguments
            res->writeStatus(HTTPCodes::NOTFOUND);
            res->end();
            return;
        }

        uint64_t currentQuantity, removedQuantity = 1;
        {
            const auto result = std::from_chars(searchResult[0][0].data(), searchResult[0][0].data() + searchResult[0][0].size(), currentQuantity);
            if (result.ec != std::errc())
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
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
                res->writeStatus(HTTPCodes::BADREQUEST);
                res->end();
                return;
            }
        }

        if (removedQuantity <= 0)
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }

        if (removedQuantity >= currentQuantity)
        {
            const auto [status, result] = serverData::database->query("DELETE FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " WHERE ID = :ID", { {":ID", b.getElement("entry")} });
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
            }
            else
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") removed a set of existing parts from a service.\n";
            }
            res->end();
            return;

        }
        else
        {
            const auto [status, result] = serverData::database->query("UPDATE " + serverData::tableNames[serverData::PARTSINSERVICE] + " SET QUANTITY = QUANTITY - :QNT WHERE ID = :ID",
                { {":QNT", b.hasElement("quantity") ? b.getElement("quantity") : "1"}, {":ID", b.getElement("entry")} });
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
            }
            else
            {
                std::cout << "Session (" << serverData::auth->getSessionID(req).value() << ") removed some existing parts from a service.\n";
            }
            res->end();
        }
    }

    void searchServices(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.containsAny({ "unauthorised", "open", "closed" }, true))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::employee))
        {
            const auto UID = serverData::auth->getSessionUser(req);
            //If no user was specified or no user matched this session or the user of this session was not the specified user
            if (!UID.has_value() || std::to_string(UID.value()) != q.getElement("UID"))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }
        if (!serverData::auth->hasSession(req))
        {
            //Unauthorised
            res->writeStatus(HTTPCodes::UNAUTHORISED);
            res->end();
            return;
        }
        responseWrapper response;

        const std::unordered_map<std::string_view, std::string_view> userID =
            q.hasElement("UID") ? std::unordered_map<std::string_view, std::string_view>{ {":ID", q.getElement("UID")}} : std::unordered_map<std::string_view, std::string_view>{};

        if (q.hasElement("unauthorised", true))
        {
            const auto [status, result] = serverData::database->query(
                "SELECT SERVICE, V.ID, V.OWNER, SS.REQUEST, SS.REQUESTED FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + 
                " INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS SS ON SERVICE = SS.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::VEHICLES] + " AS V ON SS.VEHICLE = V.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS U ON U.ID = V.OWNER" + (q.hasElement("UID") ? " WHERE U.ID = :ID" : ""), userID);
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }


            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("service", result[i][0]);
                temp.add("vehicle", result[i][1]);
                temp.add("owner", result[i][2]);
                temp.add("request", result[i][3]);
                temp.add("requested", result[i][4]);
                response.add("Unauthorised", std::move(temp), true);
            }
        }

        if (q.hasElement("open", true))
        {
            const auto [status, result] = serverData::database->query(
                "SELECT S.ID, S.VEHICLE, V.OWNER, S.REQUEST, S.REQUESTED, A.LABOUR, A.NOTES, U.ID, A.QUOTE, S.ID FROM " + serverData::tableNames[serverData::SERVICEOPEN] + " AS SO" +
                " INNER JOIN " + serverData::tableNames[serverData::SERVICEACTIVE] + " AS A ON SO.SERVICE = A.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS S ON A.SERVICE = S.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::VEHICLES] + " AS V ON S.VEHICLE = V.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS U ON A.AUTHORISER = U.ID" +
                " INNER JOIN " + serverData::tableNames[serverData::USER] + " AS O ON V.OWNER = O.ID" + (q.hasElement("UID") ? " WHERE O.ID = :ID" : ""), userID);
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("service", result[i][0]);
                temp.add("vehicle", result[i][1]);
                temp.add("owner", result[i][2]);
                temp.add("request", result[i][3]);
                temp.add("requested", result[i][4]);
                temp.add("labour", result[i][5]);
                temp.add("notes", result[i][6]);
                temp.add("authoriser", result[i][7]);
                temp.add("quote", result[i][8]);

                {
                    const auto [partStatus, partResult] = serverData::database->query(
                        "SELECT PS.ID, P.NAME, PS.PART, PS.QUANTITY, P.PRICE FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " AS PS" +
                        " INNER JOIN " + serverData::tableNames[serverData::PARTS] + " AS P ON PS.PART = P.ID" +
                        " WHERE PS.SERVICE = :ID",
                        { {":ID", result[i][9] } });
                    if (!partStatus)
                    {
                        res->writeStatus(HTTPCodes::INTERNALERROR);
                        res->end();
                        return;
                    }

                    double totalPrice = 0;

                    for (size_t i = 0; i < partResult.rowCount(); i++)
                    {
                        responseWrapper temp2;
                        temp2.add("entry", partResult[i][0]);
                        temp2.add("name", partResult[i][1]);
                        temp2.add("ID", partResult[i][2]);
                        temp2.add("quantity", partResult[i][3]);
                        temp2.add("price", partResult[i][4]);
                        {
                            const auto price = getPartPrice(partResult[i][4], partResult[i][3]);
                            if (!price.has_value())
                            {
                                res->writeStatus(HTTPCodes::INTERNALERROR);
                                res->end();
                                return;
                            }
                            totalPrice += price.value();
                        }
                        temp.add("parts", std::move(temp2), true);
                    }
                    temp.add("total", std::to_string(totalPrice));
                }

                response.add("Open", std::move(temp), true);
            }
        }

        if (q.hasElement("closed", true))
        {
            const auto [status, result] = serverData::database->query(
                "SELECT S.ID, S.VEHICLE, V.OWNER, S.REQUEST, S.REQUESTED, A.LABOUR, A.NOTES, A.AUTHORISER, A.QUOTE, C.COMPLETED, C.COMPLETER, C.PAID FROM " 
                + serverData::tableNames[serverData::SERVICECLOSED] + " AS C "
                "INNER JOIN " + serverData::tableNames[serverData::SERVICEACTIVE] + " AS A ON C.SERVICE = A.ID " +
                "INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS S ON A.SERVICE = S.ID " +
                "INNER JOIN " + serverData::tableNames[serverData::VEHICLES] + " AS V ON S.VEHICLE = V.ID " +
                "INNER JOIN " + serverData::tableNames[serverData::USER] + " AS O ON V.OWNER = O.ID" + (q.hasElement("UID") ? " WHERE O.ID = :ID" : ""), userID);
            if (!status)
            {
                //Internal server error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            for (size_t i = 0; i < result.rowCount(); i++)
            {
                responseWrapper temp;
                temp.add("service", result[i][0]);
                temp.add("vehicle", result[i][1]);
                temp.add("owner", result[i][2]);
                temp.add("request", result[i][3]);
                temp.add("requested", result[i][4]);
                temp.add("labour", result[i][5]);
                temp.add("notes", result[i][6]);
                temp.add("authoriser", result[i][7]);
                temp.add("quote", result[i][8]);
                temp.add("completed", result[i][9]);
                temp.add("completer", result[i][10]);
                temp.add("paid", result[i][11]);

                {
                    const auto [partStatus, partResult] = serverData::database->query(
                        "SELECT P.NAME, PS.PART, PS.QUANTITY, P.PRICE FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " AS PS "
                        "INNER JOIN " + serverData::tableNames[serverData::PARTS] + " AS P ON PS.PART = P.ID WHERE PS.SERVICE = :ID",
                        { {":ID", result[i][0] } });
                    if (!partStatus)
                    {
                        res->writeStatus(HTTPCodes::INTERNALERROR);
                        res->end();
                        return;
                    }
                    double totalPrice = 0;
                    for (size_t i = 0; i < partResult.rowCount(); i++)
                    {
                        responseWrapper temp2;
                        temp2.add("name", partResult[i][0]);
                        temp2.add("ID", partResult[i][1]);
                        temp2.add("quantity", partResult[i][2]);
                        temp2.add("price", partResult[i][3]);
                        {
                            const auto price = getPartPrice(partResult[i][3], partResult[i][2]);
                            if (!price.has_value())
                            {
                                res->writeStatus(HTTPCodes::INTERNALERROR);
                                res->end();
                                return;
                            }
                            totalPrice += price.value();
                        }
                        temp.add("parts", std::move(temp2), true);
                    }
                    temp.add("total", std::to_string(totalPrice));
                }

                response.add("Closed", std::move(temp), true);
            }
        }

        res->tryEnd(response.toData(false));
    }


    void selectService(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("ID"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        if (!serverData::auth->hasSession(req))
        {
            //Unauthorised
            res->writeStatus(HTTPCodes::UNAUTHORISED);
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::employee))
        {
            {

                const auto [status, result] = serverData::database->query("SELECT U.ID FROM " + serverData::tableNames[serverData::VEHICLES] + " AS V " +
                    "INNER JOIN " + serverData::tableNames[serverData::USER] + " AS V ON V.OWNER = U.ID " +
                    "INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS S ON S.VEHICLE = V.ID WHERE S.ID = :ID", { {":ID", q.getElement("ID")} });
                if (!status || result.rowCount() == 0)
                {
                    //Internal server error
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }

                const auto UID = serverData::auth->getSessionUser(req);
                if (!UID.has_value() || std::to_string(UID.value()) != result[0][0])
                {
                    //Forbidden - Insufficient permissions
                    res->writeStatus(HTTPCodes::FORBIDDEN);
                    res->end();
                    return;
                }
            }
        }
        responseWrapper response;

        //Collect all common data first

        //Service Status - This is determined by which tables the request is present in

        //Service ID
        //Vehicle ID
        //Owner ID
        //Original Request
        //Time Requested

        {
            const auto [status, result] = serverData::database->query(
                "SELECT S.ID, V.ID, U.ID, S.REQUEST, S.REQUESTED FROM " + serverData::tableNames[serverData::SERVICESHARED] + " AS S " +
                "INNER JOIN " + serverData::tableNames[serverData::VEHICLES] + " AS V ON V.ID = S.VEHICLE " +
                "INNER JOIN " + serverData::tableNames[serverData::USER] + " AS U ON U.ID = V.OWNER " +
                "WHERE S.ID = :ID", { {":ID", q.getElement("ID")} });

            if (!status)
            {
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }
            if (result.rowCount() == 0)
            {
                res->writeStatus(HTTPCodes::NOTFOUND);
                res->end();
                return;
            }

            response.add("service", result[0][0]);
            response.add("vehicle", result[0][1]);
            response.add("owner", result[0][2]);
            response.add("request", result[0][3]);
            response.add("requested", result[0][4]);
        }

        //Check if in unauthorised list
        {
            const auto [status, result] = serverData::database->query(
                "SELECT U.ID FROM " + serverData::tableNames[serverData::SERVICEUNAUTHORISED] + " AS U "
                "INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS S ON S.ID = U.SERVICE WHERE S.ID = :ID",
                { {":ID", q.getElement("ID")} });
            if (!status)
            {
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }
            if (result.rowCount() != 0)
            {
                response.add("status", "unauthorised");
                res->tryEnd(response.toData(false));
                return;
            }
        }

        //If the service is authorised (including all previous data)

        //The authoriser
        //Current labour hours
        //Current part list
        //Employee notes
        //Quoted Price

        {
            //Find active data
            {
                const auto [status, result] = serverData::database->query(
                    "SELECT A.LABOUR, A.NOTES, A.AUTHORISER, A.QUOTE FROM " + serverData::tableNames[serverData::SERVICEACTIVE] + + " AS A"
                    " WHERE A.SERVICE = :ID", { {":ID", q.getElement("ID")} });
                if (!status || result.rowCount() == 0)
                {
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }
                response.add("labour", result[0][0]);
                response.add("notes", result[0][1]);
                response.add("authoriser", result[0][2]);
                response.add("quote", result[0][3]);
            }

            //Find any parts
            {
                const auto [status, result] = serverData::database->query(
                    "SELECT P.NAME, PS.PART, PS.QUANTITY, P.PRICE FROM " + serverData::tableNames[serverData::PARTSINSERVICE] + " AS PS" +
                    " INNER JOIN " + serverData::tableNames[serverData::PARTS] + " AS P ON PS.PART = P.ID" +
                    " INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS S ON PS.SERVICE = S.ID WHERE S.ID = :ID",
                    { {":ID", q.getElement("ID") } });
                if (!status)
                {
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }

                double totalPrice = 0;

                for (size_t i = 0; i < result.rowCount(); i++)
                {
                    responseWrapper temp;
                    temp.add("name", result[i][0]);
                    temp.add("ID", result[i][1]);
                    temp.add("quantity", result[i][2]);
                    temp.add("price", result[i][3]);
                    {
                        const auto price = getPartPrice(result[i][3], result[i][2]);
                        if (!price.has_value())
                        {
                            res->writeStatus(HTTPCodes::INTERNALERROR);
                            res->end();
                            return;
                        }
                        totalPrice += price.value();
                    }
                    response.add("parts", std::move(temp), true);
                }
                response.add("total", std::to_string(totalPrice));
            }

            //Check if this service is still open
            {
                const auto [status, result] = serverData::database->query(
                    "SELECT ID FROM " + serverData::tableNames[serverData::SERVICEOPEN] + " WHERE SERVICE = :ID",
                    { {":ID", q.getElement("ID")} });
                if (!status)
                {
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }
                if (result.rowCount() != 0)
                {
                    response.add("status", "authorised");
                    res->tryEnd(response.toData(false));
                    return;
                }
            }
        }

        //If the service is closed (including all previous data)

        //Warrantied parts
        //Service Completer
        //Date Completed
        //Price Paid

        {
            const auto [status, result] = serverData::database->query(
                "SELECT C.ID, C.COMPLETED, C.PAID FROM " + serverData::tableNames[serverData::SERVICECLOSED] + " AS C " +
                "INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS S ON SERVICE = S.ID " +
                "WHERE S.ID + :ID", { {":ID", q.getElement("ID")} });

            if (!status || result.rowCount() == 0)
            {
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
                return;
            }

            response.add("completer", result[0][0]);
            response.add("completed", result[0][1]);
            response.add("paid", result[0][2]);

            //Find warrantied parts
            {
                const auto [lds, ldr] = serverData::database->query("SELECT DATE(:DAT, '-6 month')", { {":DAT", result[0][1]} });
                if (!lds)
                {
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }

                const auto [partStatus, partResult] = serverData::database->query(
                    "SELECT P.NAME, PS.PART, PS.QUANTITY FROM " + serverData::tableNames[serverData::SERVICECLOSED] + " AS S " +
                    "INNER JOIN " + serverData::tableNames[serverData::SERVICEACTIVE] + " AS A ON S.SERVICE = A.ID " +
                    "INNER JOIN " + serverData::tableNames[serverData::SERVICESHARED] + " AS SS ON A.SERVICE = SS.ID " +
                    "INNER JOIN " + serverData::tableNames[serverData::PARTSINSERVICE] + " AS PS ON SS.ID = PS.SERVICE " +
                    "INNER JOIN " + serverData::tableNames[serverData::PARTS] + " AS P ON PS.PART = P.ID " +
                    "WHERE S.ID != :ID AND S.COMPLETED >= :LOWDATE AND S.COMPLETED <= :HIGHDATE",
                    { {":ID", q.getElement("ID")}, {":LOWDATE", ldr[0][0]}, {":HIGHDATE", result[0][1]} });

                if (!partStatus)
                {
                    res->writeStatus(HTTPCodes::INTERNALERROR);
                    res->end();
                    return;
                }

                for (size_t i = 0; i < partResult.rowCount(); i++)
                {
                    responseWrapper temp;
                    temp.add("name", partResult[i][0]);
                    temp.add("ID", partResult[i][1]);
                    temp.add("quantity", partResult[i][2]);
                    response.add("warrantied", std::move(temp), true);
                }
            }

            res->tryEnd(response.toData(false));
            return;
        }
    }


    void selectServicePart(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body& b, const query& q)
    {
        if (!q.hasElement("entry"))
        {
            //Bad Request - Invalid arguments
            res->writeStatus(HTTPCodes::BADREQUEST);
            res->end();
            return;
        }
        if (!serverData::auth->verify(req, authLevel::employee))
        {
            const auto UID = serverData::auth->getSessionUser(req);
            //If no user was specified or no user matched this session or the user of this session was not the specified user
            if (!UID.has_value() || std::to_string(UID.value()) != q.getElement("UID"))
            {
                //Forbidden - Insufficient permissions
                res->writeStatus(HTTPCodes::FORBIDDEN);
                res->end();
                return;
            }
        }
        if (!serverData::auth->hasSession(req))
        {
            //Unauthorised
            res->writeStatus(HTTPCodes::UNAUTHORISED);
            res->end();
            return;
        }
        responseWrapper response;

        const auto [status, result] = serverData::database->query(
            "SELECT ID, SERVICE, PART, QUANTITY FROM " + serverData::tableNames[serverData::PARTSINSERVICE] +
            " WHERE ID = :ID", { {":ID", q.getElement("entry")} });

        if (!status)
        {
            res->writeStatus(HTTPCodes::INTERNALERROR);
            res->end();
            return;
        }
        if (result.rowCount() == 0)
        {
            res->writeStatus(HTTPCodes::NOTFOUND);
            res->end();
            return;
        }

        response.add("entry", result[0][0]);
        response.add("serviceID", result[0][1]);
        response.add("partID", result[0][2]);
        response.add("quantity", result[0][3]);

        res->tryEnd(response.toData(false));
    }
}