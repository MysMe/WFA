#include <unordered_map>
#include <string>
#include "curl/curl.h"
#include <algorithm>
#include "uwebsockets/App.h"

namespace //Anonymous namespace to only allow query and body to see/use this function
{
    //Parses values from a "name=value" style to a map
    std::unordered_map<std::string, std::string> parseURLValues(std::string_view source)
    {
        std::unordered_map<std::string, std::string> ret;
        auto it = source.cbegin();
        while (it != source.cend())
        {
            //We implicitly allow this to not find anything, indicating it is the last element
            auto end = std::find(it, source.cend(), '&');

            auto div = std::find(it, end, '=');

            std::string id{ &*it, static_cast<size_t>(std::distance(it, div)) };
            //Empty values are allowed, but empty names are not
            if (div != end && std::distance(div, end) - 1 != 0)
            {
                std::string temp{ div + 1, end };
                std::replace(temp.begin(), temp.end(), '+', ' '); //Spaces are encoded as '+' in queries (but not URLs, because consistency)
                int curl_str_len = 0;
                auto curl_str = curl_easy_unescape(nullptr, temp.data(), temp.size(), &curl_str_len);
                ret[id].assign(curl_str, curl_str_len);
                curl_free(curl_str);
            }
            else
                ret[id] = "";

            it = end;
            if (it != source.cend())
                ++it;
        }
        return ret;
    }

    //HTTP queries and bodies both share similar key/value formats, the major difference is the "source" (the HTTP request or body)
    class queryBase
    {
        std::unordered_map<std::string, std::string> elements;

    public:
        queryBase() = default;
        queryBase(std::unordered_map<std::string, std::string>&& elem) : elements(std::move(elem)) {}

        bool hasElement(const std::string & name, bool allowEmpty = false) const
        {
            if (!allowEmpty)
                return elements.count(name) != 0 && !elements.at(name).empty();
            else
                return elements.count(name) != 0;
        }
        std::string_view getElement(const std::string & name) const
        {
            return elements.at(name);
        }

        bool containsAll(const std::vector<std::string>&strings, bool allowEmpty = false) const
        {
            for (const auto i : strings)
            {
                if (!hasElement(i, allowEmpty))
                    return false;
            }
            return true;
        }

        bool containsAny(const std::vector<std::string>&strings, bool allowEmpty = false) const
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
}

class query : public queryBase
{
    //A query should be unique from a body, so must be kept as a separate class
    //The alternate constructor helps prevent it from being incorrectly created
public:
    query() = default;
    query(uWS::HttpRequest* req) : queryBase(parseURLValues(req->getQuery())) {}
};

class body : public queryBase
{
public:
    body() = default;
    body(std::string_view contents) : queryBase(parseURLValues(contents)) {}
};
