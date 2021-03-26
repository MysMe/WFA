#include <iostream>

#include "Forwarding.h"

#include <variant>
#include <any>
#include <optional>
#include <fstream>
#include <array>


std::string_view::const_iterator tagSearch(std::string_view::const_iterator begin, std::string_view::const_iterator end, std::string_view prefix, std::string_view postfix)
{
    const char prefix_sentinel = prefix.front();
    const char postfix_sentinel = postfix.front();

    assert(std::strncmp(&*begin, prefix.data(), std::min(prefix.size(), static_cast<size_t>(std::distance(begin, end)))) == 0);

    size_t depth = std::numeric_limits<size_t>::max();
    for (auto it = begin; it != end; ++it)
    {
        if (*it == prefix_sentinel &&
            std::strncmp(&*it, prefix.data(), std::min(prefix.size(), static_cast<size_t>(std::distance(it, end)))) == 0)
        {
            depth += 1;
            it += prefix.size();
            continue;
        }
        if (*it == postfix_sentinel &&
            std::strncmp(&*it, postfix.data(), std::min(postfix.size(), static_cast<size_t>(std::distance(it, end)))) == 0)
        {
            if (depth == 0)
            {
                return it + postfix.size();
            }
            else
            {
                depth--;
            }
        }
    }
    return end;
}

/*
Possible tags:
    <HTMT:XXX></HTMT>, Iterate over named field values
    <HTMTVAL:XXX>, Get the value of a name field, which may only have one value
    <HTMTCODE:XXX>, Only includes the translation content if the HTTP code matches XXX


Example HTMT:

<HTML>
<HTMT>Your username is <HTMTVAL:USERNAME><HTMT>
</HTML>
*/

class translation;

class translationValue
{
public:
    enum class state
    {
        access,
        HTML,
        translation
    };

    using value_type = std::variant<std::string, std::unique_ptr<translation>>;
    value_type value;
    state valueType;
public:

    translationValue() = default;
    translationValue(std::string_view str, state type) : value(std::string(str)), valueType(type) {}
    translationValue(translation&& tran) : value(std::make_unique<translation>(std::move(tran))), valueType(state::translation) {}

    translationValue(const translationValue&) = delete;
    translationValue(translationValue&&) = default;
    translationValue& operator=(const translationValue&) = delete;
    translationValue& operator=(translationValue&&) = default;

    static translationValue asHTML(std::string_view str) { return translationValue(str, state::HTML); }
    static translationValue asAccess(std::string_view str) { return translationValue(str, state::access); }
    static translationValue asTranslation(translation&& tran) { return translationValue(std::move(tran)); }

    void apply(std::string& source, const responseWrapper& data, long httpCode) const;
};

class translationCondition
{
public:
    bool check(const responseWrapper& data) const
    {
        return true;
    }
};

class translation
{ 
    enum class matchType
    {
        always,
        tag,
        code,
        condition
    };

    matchType match = matchType::always;
    std::variant<std::string, unsigned long, translationCondition> tag;

    std::vector<translationValue> values;


    static void parseSubHTML(translation& obj, std::string_view HTML)
    {
        //Number of characters in "<HTMTVAL"
        constexpr auto tagSize = 9;

        auto left = HTML.cbegin();
        while (left != HTML.cend())
        {
            //Find any value requests
            const auto fpos = std::string_view(&*left, HTML.cend() - left).find("<HTMTVAL:");
            auto it = fpos != std::string_view::npos ? left + fpos : HTML.cend();

            //Add any text before the value request as raw HTML
            if (left != it)
                obj.values.emplace_back(translationValue::asHTML(std::string_view(&*left, it - left)));

            //There may be no value reference
            if (it == HTML.cend())
                break;

            const auto nameStart = it + tagSize;
            const auto nameEnd = std::find(nameStart, HTML.cend(), '>');

            //Extract the value name
            if (left != it)
                obj.values.emplace_back(translationValue::asAccess(std::string_view(&*nameStart, nameEnd - nameStart)));
            //Skip the closing '>'
            left = nameEnd + 1;
        }
    }

    bool matches(const responseWrapper& data, unsigned long httpCode) const
    {
        switch (match)
        {
        default:
            return true;
        case(matchType::code):
            return httpCode == std::get<unsigned long>(tag);
        case(matchType::condition):
            return std::get<translationCondition>(tag).check(data);
        }
    }

    template <typename... T>
    static std::array<size_t, sizeof...(T)> findNextSet(std::string_view source, T... strings)
    {
        return std::array<size_t, sizeof...(T)>
        {
            source.find(strings.data()), ...
        };
    }

public:

    translation() = default;
    translation(const translation&) = delete;
    translation(translation&&) = default;
    translation& operator=(const translation&) = delete;
    translation& operator=(translation&&) = default;

    void apply(std::string& source, const responseWrapper& data, long httpCode) const
    {
        if (match == matchType::always)
        {
            //Special case for empty <HTMT> tags and/or default translations
            for (const auto& i : values)
                i.apply(source, data, httpCode);
        }
        else if (matches(data, httpCode))
        {
            if (match == matchType::tag)
            {
                const auto maybeSubField = data.search(std::get<std::string>(tag));
                //Unmatched tags are repeated 0 times
                if (!maybeSubField.has_value())
                    return;

                //All subobjects must be objects, not value (arrays)
                assert(maybeSubField.value().get().index() == responseWrapper::index::objects);
                for (const auto& i : std::get<responseWrapper::objectContainer>(maybeSubField.value().get()))
                {
                    for (const auto& u : values)
                        u.apply(source, i, httpCode);
                }
            }
            else
            {
                for (const auto& i : values)
                {
                    i.apply(source, data, httpCode);
                }
            }
        }
    }

    std::string apply(const responseWrapper& data, long httpCode) const
    {
        std::string ret;
        apply(ret, data, httpCode);
        return ret;
    }

    static translation parse(std::string_view source)
    {
        //Divide the string by ever instance of "<HTMT:", "<HTMTVAL:" and "<HTMTCOND:" and their respective closing tags
        std::vector<std::string_view> subsections;
        {
            size_t left = 0;
            auto found = findNextSet(std::string_view(source.data() + left, source.size() - left), "<HTMT:", "<HTMTVAL:", "<HTMTCOND:");
        }






        translation ret;
        auto left = source.cbegin();
        while (left != source.cend())
        {
            constexpr std::string_view objectTag = "<HTMT:";
            constexpr std::string_view objectClose = "</HTMT>";
            constexpr std::string_view codeTag = "<HTMTCODE:";
            constexpr std::string_view codeClose = "</HTMTCODE>";

            //Iterator to the first <HTMT:> or <HTMTCODE:> tag
            std::string_view::const_iterator it;
            //Determines the type of tag it refers to
            matchType type;
            {
                const std::string_view str{ &*left, size_t(source.cend() - left) };
                auto tagPos = str.find(objectTag);
                if (tagPos == std::string_view::npos)
                    tagPos = str.size();
                const auto codePos = std::string_view{ &*left, tagPos }.find(codeTag);
                if (codePos < tagPos)
                {
                    it = left + codePos;
                    type = matchType::code;
                }
                else
                {
                    it = left + tagPos;
                    type = matchType::tag;
                }
            }

            //Leading text may be empty
            if (left != it)
                parseSubHTML(ret, std::string_view(&*left, it - left));

            //There may not be any tags
            if (it == source.cend())
                break;

            //Find the end of the tag
            const auto tagEnd = (type == matchType::code)
                ? tagSearch(it, source.cend(), codeTag, codeClose)
                : tagSearch(it, source.cend(), objectTag, objectClose);

            //Find the end minus the closing tag
            const auto subEnd = tagEnd - ((type == matchType::code) ? codeClose.size() : objectClose.size());

            //Find the closing bracket of the opening <HTMT:XXX>
            const auto tagValueEnd = std::find(it, source.cend(), '>');

            //The next character is the first in the sub-translation
            const auto subBegin = std::next(tagValueEnd);

            //Parse the internal text as a translation
            translation sub = translation::parse(std::string_view(&*subBegin, subEnd - subBegin));
            //sub.tagName.assign(it + ((type == matchType::code) ? codeTag.size() : objectTag.size()), tagValueEnd);
            sub.match = type;
            ret.values.emplace_back(std::move(sub));

            left = tagEnd;
        }
        return ret;
    }
};

void translationValue::apply(std::string& source, const responseWrapper& data, long httpCode) const
{
    switch (valueType)
    {
    case(state::access):
    {
        const auto maybeValues = data.search(std::get<std::string>(value));
        assert(maybeValues.has_value());
        const auto values = std::get<responseWrapper::stringContainer>(maybeValues.value().get());
        assert(values.size() == 1);
        source += values.front();
        return;
    }
    case(state::HTML):
        source += std::get<std::string>(value);
        return;
    case(state::translation):
        std::get<std::unique_ptr<translation>>(value)->apply(source, data, httpCode);
        return;
    }
}


class webpageWrapper
{
protected:
    static std::string readEntireFile(const std::string& filename)
    {
        std::ifstream input{ filename, std::ios::binary | std::ios::ate };
        if (!input.is_open())
        {
            return "";
        }
        const auto end = input.tellg();
        input.seekg(0);
        std::string ret;
        ret.resize(end);
        input.read(ret.data(), end);
        return ret;
    }
public:

    virtual void operator()(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const = 0;
    virtual void apply(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const = 0;

};

class forwardingWrapper final : public webpageWrapper
{
    translation data;

    static void applyTranslation(uWS::HttpResponse<true>* res, const APIResponse& API, const translation& tran)
    {
        res->writeStatus(std::to_string(API.response_code));
        for (const auto& i : API.headers)
        {
            res->writeHeader(i.first, i.second);
        }
        const auto resw = responseWrapper::fromData(API.response);
        if (resw.has_value())
        {
            res->tryEnd(tran.apply(resw.value(), API.response_code));
        }
        else
        {
            res->tryEnd(API.response);
        }
    }

    template <class Fn>
    static void extractPostBody(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, Fn&& callback)
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

        if (contentLength == 0)
        {
            callback(res, req, {});
            return;
        }

        std::string contentBuffer;
        contentBuffer.reserve(contentLength);


        //Note that this is a callback which will be called outside the current function scope, ergo the body and callback must be copied
        //This callback will be called multiple times, so the data it stores must be mutable so it can be accumulated
        res->onData([res, req, callback = std::move(callback), buffer = std::move(contentBuffer)](std::string_view data, bool last) mutable
        {
            buffer.append(data.data(), data.size());
            if (last)
            {
                callback(res, req, buffer);
            }
        });

        res->onAborted([res]()
            {
                //Internal Server Error
                res->writeStatus("500");
                res->end();
            });
    }


    static void forwardPost(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, requestWrapper&& curl, const translation& tran)
    {
        extractPostBody(res, req,
            [curl = std::move(curl), &tran](uWS::HttpResponse<true>* res, uWS::HttpRequest* req, std::string_view body) mutable
        {
            applyTranslation(res, curl.post(body), tran);
        }
        );
    }
public:

    forwardingWrapper() = default;
    forwardingWrapper(const std::string& path) : data(translation::parse(readEntireFile(path))) {};
    forwardingWrapper(translation&& tran) : data(std::move(tran)) {}

    void operator()(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const final { apply(res, req); }

    void apply(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const final
    {
        std::string url = "localhost:9001";
        const auto path = req->getUrl();
        url.append(path.data(), path.size());
        const auto query = req->getQuery();
        url.append(query.data(), query.size());
        requestWrapper request(url);
        request.setCookies(std::string{ req->getHeader("cookie") });
        if (req->getMethod() == "post")
        {
            forwardPost(res, req, std::move(request), data);
        }
        else
        {
            applyTranslation(res, request.get(), data);
        }
    }
};

class staticWrapper final : public webpageWrapper
{
    std::string data;
public:
    staticWrapper() = default;
    staticWrapper(const std::string& path) : data(readEntireFile(path)) {}

    void operator()(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const final { apply(res, req); }

    void apply(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const final
    {
        res->tryEnd(data);
    }
};

/*
<table><tr><th>Owner</th><th>Plate</th><th>Year</th></tr><HTMT:Vehicles><tr><td><HTMTVAL:Owner><tr><td><HTMTVAL:Plate><tr><td><HTMTVAL:Year></td></tr></HTMT></table>
*/

//std::string testTranslate()
//{
//    const auto expr = 
//        translation::parse("<table><tr><th>Owner</th><th>Plate</th><th>Year</th></tr><HTMT:Vehicles><tr><td><HTMTVAL:Owner></td><td><HTMTVAL:Plate></td><td><HTMTVAL:Year></td></tr></HTMT></table>");
//
//    responseWrapper res;
//    {
//        {
//            responseWrapper temp;
//            temp.add("Owner", "A");
//            temp.add("Plate", "ABC");
//            temp.add("Year", "2000");
//            res.add("Vehicles", std::move(temp));
//        }
//        {
//            responseWrapper temp;
//            temp.add("Owner", "B");
//            temp.add("Plate", "DEF");
//            temp.add("Year", "2005");
//            res.add("Vehicles", std::move(temp));
//        }
//        {
//            responseWrapper temp;
//            temp.add("Owner", "C");
//            temp.add("Plate", "GHI");
//            temp.add("Year", "2010");
//            res.add("Vehicles", std::move(temp));
//        }
//    }
//
//    translation populate;
//    populate.tagName = "Vehicles";
//    {
//        populate.values.emplace_back(translationValue::asHTML("<tr><td>"));
//        populate.values.emplace_back(translationValue::asAccess("Owner"));
//        populate.values.emplace_back(translationValue::asHTML("</td><td>"));
//        populate.values.emplace_back(translationValue::asAccess("Plate"));
//        populate.values.emplace_back(translationValue::asHTML("</td><td>"));
//        populate.values.emplace_back(translationValue::asAccess("Year"));
//        populate.values.emplace_back(translationValue::asHTML("</td></tr>"));
//    }
//
//    translation test;
//    {
//        test.values.emplace_back(translationValue::asHTML("<table><tr><th>Owner</th><th>Plate</th><th>Year</th></tr>"));
//        test.values.emplace_back(translationValue::asTranslation(std::move(populate)));
//        test.values.emplace_back(translationValue::asHTML("</table>"));
//    }
//    std::string out;
//    //test.apply(out, res);
//    expr.apply(out, res);
//    return out;
//}

void discard()
{
    uWS::SSLApp app;
    app.listen(9002, [](auto*) {});
    app.any("/*", forward);
    //app.get("/t", [](auto* res, auto* req) {res->end(testTranslate()); });
    app.get("/user/me", forwardingWrapper(
        translation::parse("<table><tr><th>Username</th><th>Permissions</th></tr><HTMT:><tr><td><HTMTVAL:Username></td><td><HTMTVAL:Permissions></td></tr></HTMT></table>")));
    std::cout << "Ready to forward (9002).\n";
    app.run();
    std::cin.ignore();
}


void testConnect()
{
    requestWrapper request("localhost:9001/request");
    const auto r1 = request.post("username=ADMIN&password=ADMIN");
    request.retarget("localhost:9001/user/me");
    const auto r2 = request.get();
    const auto pr = responseWrapper::fromData(r2.response);
    std::cout << pr.value().toData(true);

    std::cin.ignore();
}

bool linkPages(uWS::SSLApp& app, const std::string& linkFile)
{
    std::ifstream input{ linkFile };
    if (!input.is_open())
    {
        std::cout << "Unable to find link file.\n";
        return false;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (line.size() < 4 ||
            (line[0] != 'P' && line[0] != 'G') ||
            (line[1] != 'S' && line[1] != 'F') ||
            line[2] != ':')
        {
            std::cout << "Invalid linkfile entry \"" + line + "\".\n";
            return false;
        }
        auto div = std::find(line.cbegin() + 4, line.cend(), ':');
        if (div == line.cend())
        {
            std::cout << "Invalid linkfile entry \"" + line + "\".\n";
            return false;
        }

        const std::string webDirectory{ line.cbegin() + 3, div };
        const std::string fileDirectory{ &*(div + 1), size_t(line.cend() - div - 1) };

        if (line[0] == 'P')
        {
            if (line[1] == 'S')
            {
                app.post(webDirectory, staticWrapper(fileDirectory));
            }
            else
            {
                app.post(webDirectory, forwardingWrapper(fileDirectory));
            }
        }
        else
        {
            if (line[1] == 'S')
            {
                app.get(webDirectory, staticWrapper(fileDirectory));
            }
            else
            {
                app.get(webDirectory, forwardingWrapper(fileDirectory));
            }
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    //testTranslate();
    //discard();
    uWS::SSLApp app;
    app.listen(9002, [](auto*) {});
    app.any("/*", forward);
    linkPages(app, "../Pages/Link.txt");
    app.run();
    std::cin.ignore();
}