#pragma once
#include <vector>
#include <string_view>
#include <optional>
#include "uwebsockets/App.h"
#include "Database.h"
#include <fstream>
#include <random>
#include <charconv>
#include "ServerData.h"
#include "Response.h"

namespace HTTPCodes
{
    constexpr auto OK                   = "200";
    constexpr auto NOCONTENT_NOREDIRECT = "204"; //Note that a browser recieving code 204 may choose to not redirect from the page
    constexpr auto BADREQUEST           = "400";
    constexpr auto UNAUTHORISED         = "401";
    constexpr auto FORBIDDEN            = "403";
    constexpr auto NOTFOUND             = "404";
    constexpr auto CONFLICT             = "409";
    constexpr auto INTERNALERROR        = "500";
}

std::unordered_map<std::string, std::string> parseURLValues(std::string_view source);

class query
{
    std::unordered_map<std::string, std::string> elements;

public:
    query() = default;
    query(uWS::HttpRequest* req) : elements(parseURLValues(req->getQuery())) {}

    bool hasElement(const std::string& name, bool allowEmpty = false) const
    {
        if (!allowEmpty)
            return elements.count(name) != 0 && !elements.at(name).empty();
        else
            return elements.count(name) != 0;
    }
    std::string_view getElement(const std::string& name) const
    {
        return elements.at(name);
    }

    bool containsAll(const std::vector<std::string>& strings, bool allowEmpty = false) const
    {
        for (const auto i : strings)
        {
            if (!hasElement(i, allowEmpty))
                return false;
        }
        return true;
    }

    bool containsAny(const std::vector<std::string>& strings, bool allowEmpty = false) const
    {
        for (const auto i : strings)
        {
            if (hasElement(i, allowEmpty))
                return true;
        }
        return false;
    }
    auto begin() const { return elements.cbegin(); }
    auto end() const { return elements.cend(); }
};

class body
{
    std::unordered_map<std::string, std::string> elements;

public:

    body() = default;
    body(std::string_view contents) : elements(parseURLValues(contents)) {}

    bool hasElement(const std::string& name, bool allowEmpty = false) const
    {
        if (allowEmpty)
            return elements.count(name) != 0 && !elements.at(name).empty();
        else
            return elements.count(name) != 0;
    }
    std::string_view getElement(const std::string& name) const
    {
        return elements.at(name);
    }

    bool containsAll(const std::vector<std::string>& strings, bool allowEmpty = false) const
    {
        for (const auto i : strings)
        {
            if (!hasElement(i, allowEmpty))
                return false;
        }
        return true;
    }

    bool containsAny(const std::vector<std::string>& strings, bool allowEmpty = false) const
    {
        for (const auto i : strings)
        {
            if (hasElement(i, allowEmpty))
                return true;
        }
        return false;
    }


    auto begin() const { return elements.cbegin(); }
    auto end() const { return elements.cend(); }
};

class HttpCallWrapper
{
    std::function<void(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, const body&, const query&)> callback;
public:
    template <class Fn>
    HttpCallWrapper(Fn func) : callback(func) {}

    void operator()(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const
    {
        const size_t contentLength = [&]()->size_t
        {
            size_t val;
            const auto header = req->getHeader("content-length");
            const auto res = std::from_chars(header.data(), header.data() + header.size(), val);
            if (res.ec != std::errc())
                return 0;
            return val;
        }();

        query stackQuery{ req };

        if (contentLength == 0)
        {
            callback(res, req, {}, stackQuery);
            return;
        }

        std::string contentBuffer;
        contentBuffer.reserve(contentLength);


        //Note that this is a callback which will be called outside the current function scope, ergo the body and callback must be copied
        //This callback will be called multiple times, so the data it stores must be mutable so it can be accumulated
        res->onData([res, req, lambdaQuery = std::move(stackQuery), callback = callback, buffer = std::move(contentBuffer)](std::string_view data, bool last) mutable
        {
            buffer.append(data.data(), data.size());
            if (last)
            {
                callback(res, req, body(buffer), lambdaQuery);
            }
        });

        res->onAborted([res]() 
            {
                //Internal Server Error
                res->writeStatus(HTTPCodes::INTERNALERROR);
                res->end();
            });
    }
};

class cookieManager
{
    std::unordered_map<std::string, std::string> values;
public:
    static cookieManager getCookies(uWS::HttpRequest* req)
    {
        cookieManager ret;
        const std::string_view mixed = req->getHeader("cookie");
        auto it = mixed.cbegin();
        while (it != mixed.cend())
        {
            auto end = std::find(it, mixed.cend(), ';');
            auto div = std::find(it, end, '=');
            //Empty names/values are ignored
            if (it == div || div == end || *div != '=')
            {
                it = end;
                if (it != mixed.cend())
                    it++;
                continue;
            }
            std::string name(it, div), value(std::next(div), end);
            ret.values[name] = std::move(value);
            it = end;
            if (it != mixed.cend())
                it++;
            while (it != mixed.cend() && std::isspace(*it))
                it++;
        }
        return ret;
    }

    template <bool SSL>
    static void clearCookies(uWS::HttpResponse<SSL>* res)
    {
        res->writeHeader("Clear-Site-Data", "\"cookies\"");
    }

    template <bool SSL>
    static void setCookie(uWS::HttpResponse<SSL>* res, std::string_view name, std::string_view val)
    {
        std::string expr;
        expr.reserve(name.size() + val.size() + 1);
        expr = name;
        expr += "=";
        expr += val;
        res->writeHeader("Set-Cookie", expr);
    }

    template <bool SSL>
    static void clearCookie(uWS::HttpResponse<SSL>* res, std::string_view name)
    {
        //HTTP standard dictates any cookie with the same name must update the previous value
        //and that recieving a cookie that has already expired must delete it
        setCookie(res, name, "deleted; expires=Thu, 01 Jan 1970 00:00:00 GMT;");
    }

    bool empty() const { return values.empty(); }

    std::string_view get(const std::string& name) const
    {
        if (values.count(name) != 0)
            return values.at(name);
        else
            return {};
    }
};

enum class authLevel
{
    client,
    employee,
    manager,
    superuser
};

class authenticator
{
    //Note that nearly all functions in this class are gross simplifications of real functions
    //As such, they should _never_ be used in a real-world application, prefer actual encryption algorithms

    using sessionID = uint64_t;
    static constexpr auto authCookie = "auth";

    static constexpr auto userTag = "u", passwordTag = "p";

    struct session
    {
        authLevel authLevel;
        uint64_t userID = 0;
    };

    std::unordered_map<sessionID, session> sessions;

public:

    std::optional<sessionID> getSessionID(uWS::HttpRequest* req) const
    {
        const auto cookies = cookieManager::getCookies(req);
        if (cookies.empty() || cookies.get(authCookie).empty())
            return std::nullopt;

        const auto auth = cookies.get(authCookie);

        sessionID ret;

        auto status = std::from_chars(auth.data(), auth.data() + auth.size(), ret);
        if (status.ec != std::errc())
        {
            return std::nullopt;
        }
        return ret;
    }

    static std::string hash(std::string_view val)
    {
        return std::string(val);
    }

    template <bool SSL>
    bool request(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req, const body& b)
    {
        if (!b.containsAll({ "username", "password" }))
        {
            return false;
        }

        if (hasSession(req))
            release(res, req);

        const std::string SQL = std::string("SELECT ID, PERMISSIONS FROM ") + serverData::tableNames[serverData::USER] + " WHERE USERNAME = :USR AND PASSWORD = :PWD";

        const auto [status, matches] = 
            serverData::database->query(SQL, { 
                {":USR", std::string(b.getElement("username"))},
                {":PWD", hash(b.getElement("password"))} 
                });

        if (!status || matches.rowCount() == 0)
        {
            return false;
        }
        assert(matches.rowCount() == 1);

        std::random_device rd;
        std::default_random_engine random(rd());
        std::uniform_int_distribution<sessionID> dist(1, std::numeric_limits<sessionID>::max());

        sessionID val;
        do
        {
            val = dist(random);
        } while (sessions.count(val) != 0);

        session ses;
        {
            auto convres = std::from_chars(matches[0][0].data(), matches[0][0].data() + matches[0][0].size(), ses.userID);
            if (convres.ec != std::errc())
                return false;
        }
        {
            unsigned int temp;
            auto convres = std::from_chars(matches[0][1].data(), matches[0][1].data() + matches[0][1].size(), temp);
            if (convres.ec != std::errc())
                return false;
            ses.authLevel = static_cast<authLevel>(temp);
        }

        sessions[val] = std::move(ses);
        cookieManager::setCookie(res, authCookie, std::to_string(val));
        std::cout << "Authenticated \"" << b.getElement("username") << "\" with session ID: " << std::to_string(val) << ".\n";
        return true;
    }

    //Can safely be called on any request, regardless of whether it is authenticated
    template <bool SSL>
    bool release(uWS::HttpResponse<SSL>* res, uWS::HttpRequest* req)
    {
        auto ID = getSessionID(req);
        if (!ID)
            return false;

        sessions.erase(ID.value());
        cookieManager::clearCookie(res, authCookie);
        std::cout << "Released session: " << ID.value() << ".\n";
        return true;
    }

    bool verify(uWS::HttpRequest* req, authLevel checkLevel) const
    {
        const auto ID = getSessionID(req);
        if (!ID)
            return false;
        return sessions.count(ID.value()) == 1 && sessions.at(ID.value()).authLevel >= checkLevel;
    }

    bool hasSession(uWS::HttpRequest* req) const
    {
        return getSessionID(req).has_value();
    }

    std::optional<uint64_t> getSessionUser(uWS::HttpRequest* req) const
    {
        const auto ID = getSessionID(req);
        if (!ID || sessions.count(ID.value()) == 0)
            return std::nullopt;
        return sessions.at(ID.value()).userID;
    }

    std::optional<authLevel> getSessionAuthLevel(uWS::HttpRequest* req) const
    {
        const auto ID = getSessionID(req);
        if (!ID || sessions.count(ID.value()) == 0)
            return std::nullopt;
        return sessions.at(ID.value()).authLevel;
    }

    bool isSessionUser(uWS::HttpRequest* req, std::string_view UID) const
    {
        const auto ID = getSessionUser(req);
        if (!ID)
            return false;
        return std::to_string(ID.value()) == UID;
    }
    bool isSessionUserFromID(uWS::HttpRequest* req, std::string_view userIndex) const
    {
        const auto ID = getSessionUser(req);
        if (!ID)
            return false;
        uint64_t userID;
        auto idResult = std::from_chars(userIndex.data(), userIndex.data() + userIndex.size(), userID);
        if (idResult.ec != std::errc())
            return false;
        return ID.value() == userID;
    }
};

void net();