#include <iostream>
#include "Forwarding.h"

#include <variant>
#include <any>
#include <optional>
#include <fstream>
#include <array>

#include "Query.h"

//Finds a "tag" (a word followed by a symbol), tracking opening and closing pairs to ensure that the tag "depth" remains consistent
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

class translation;

//A single (non-tag-pair) HTMT statement
class translationValue
{
public:
    enum class state
    {
        access, //HTMTVAL
        HTML, //Raw HTML
        translation, //Sub-level HTMT
        query //HTMTQUERY
    };

    //The contents are either a string (the name of a variable for state::access, the HTML for state::HTML or the query name for state::query) or a sub-translation.
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
    static translationValue asQuery(std::string_view str) { return translationValue(str, state::query); }

    void apply(std::string& source, const responseWrapper& data, long httpCode, const query& q) const;
};

//A conditional section of HTMT
class translationCondition
{
    //Given that everything is a string, it doesn't make sense to have numeric comparisons
    //As such, only equal and not-equal are currently provided

    struct subCondition
    {
        enum class condition
        {
            equal,
            notEqual,
            always
            //The parse function relies on the ordering of this enum
        };

        condition action = condition::always;
        std::string constant, variable;

        bool check(const responseWrapper& data) const
        {
            auto isEqual = [&]()
            {
                const auto val = data.search(variable);
                //Data must be present
                if (!val.has_value())
                    return false;
                //Data must be strings (not subobjects)
                if (val.value().get().index() != 0)
                    return false;
                //Data must be exactly one string
                if (std::get<responseWrapper::stringContainer>(val.value().get()).size() != 1)
                    return false;
                //Check equality
                return std::get<responseWrapper::stringContainer>(val.value().get()).front() == constant;
            };

            switch (action)
            {
            default:
                return true;
            case(condition::equal):
                return isEqual();
            case(condition::notEqual):
                return !isEqual();
            }
        }
    };

    //A conjunction is a method of combining multiple conditions (e.g. A && B, A || B)
    enum class conjunction
    {
        and,
        or
        //The parse function relies on the ordering of this enum
    };

    //There must be one fewer conjunction than there are conditions, each condition is parsed left to right
    std::vector<subCondition> conditions;
    std::vector<conjunction> conjunctions;

    static bool applyConjunction(bool left, conjunction val, bool right)
    {
        switch (val)
        {
        default:
            return true;
        case(conjunction::and):
            return left && right;
        case(conjunction::or):
            return left || right;
        }
    }

    static subCondition parseSubCondition(std::string_view source)
    {
        static const std::vector<char> operators{ '=', '!' };

        subCondition::condition type = subCondition::condition::equal;
        size_t div = std::string_view::npos;
        for (size_t i = 0; i < operators.size(); i++)
        {
            div = source.find(operators[i]);
            if (div != std::string_view::npos)
            {
                type = static_cast<subCondition::condition>(i);
                break;
            }
        }

        //If nothing was found, return a default condition (always true)
        if (div == std::string_view::npos)
            return subCondition();

        subCondition ret;
        ret.action = type;
        ret.variable.assign(source.cbegin(), source.cbegin() + div);
        ret.constant.assign(source.cbegin() + div + 1, source.cend());
        return ret;
    }

public:
    bool check(const responseWrapper& data) const
    {
        if (conditions.empty())
            return true;
        bool currentState = conditions[0].check(data);
        for (size_t i = 1; i < conditions.size(); i++)
        {
            currentState = applyConjunction(currentState, conjunctions[i - 1], conditions[i].check(data));
        }
        return currentState;
    }

    static translationCondition parse(std::string_view source)
    {
        static const std::vector<char> divisors{ '&', '|' };

        size_t left = 0;
        translationCondition ret;
        while (left != std::string_view::npos)
        {
            size_t div = std::string_view::npos;
            conjunction type;
            for (size_t i = 0; i < divisors.size(); i++)
            {
                auto temp = source.find(divisors[i], left);
                if (temp < div)
                {
                    div = temp;
                    type = static_cast<conjunction>(i);
                }
            }

            std::string_view contents(source.data() + left,
                ((div == std::string::npos) ? source.size() : div) - left);

            ret.conditions.emplace_back(parseSubCondition(contents));
            if (div != std::string_view::npos)
            {
                ret.conjunctions.push_back(type);
                left = div + 1;
            }
            else
            {
                break;
            }
        }
        assert(ret.conditions.size() == ret.conjunctions.size() + 1);

        return ret;
    }
};

//A collection of HTMT conditions, sections, accesses etc.
class translation
{ 
    enum class matchType
    {
        always, //This translation always applies
        tag, //This translation iterates over a collection of HTMT values
        code, //This translation applies if the HTML code matches a given value
        condition //This translation applies if a condition is met
        //The parse function relies on the ordering of this enum
    };

    matchType match = matchType::always;
    std::variant<std::string, long, translationCondition> tag;

    std::vector<translationValue> values;


    static void parseSubHTML(translation& obj, std::string_view HTML)
    {
        static const std::vector<std::string> tags{ "<HTMTVAL", "<HTMTQUERY" };

        auto left = HTML.cbegin();
        while (left != HTML.cend())
        {
            //Find any HTMT requests
            size_t fpos = std::string::npos;
            size_t tagType = 0;
            for (size_t i = 0; i < tags.size(); i++)
            {
                const auto temp = std::string_view(&*left, HTML.cend() - left).find(tags[i]);
                if (temp != std::string::npos && temp < fpos)
                {
                    fpos = temp;
                    tagType = i;
                }
            }

            auto it = fpos != std::string_view::npos ? left + fpos : HTML.cend();

            //Add any text before the value request as raw HTML
            if (left != it)
                obj.values.emplace_back(translationValue::asHTML(std::string_view(&*left, it - left)));

            //There may be no value reference
            if (it == HTML.cend())
                break;

            const auto nameStart = it + tags[tagType].size() + 1;
            const auto nameEnd = std::find(nameStart, HTML.cend(), '>');

            const std::string_view contents = std::string_view(&*nameStart, nameEnd - nameStart);

            switch (tagType)
            {
            default:
                throw(std::logic_error("Invalid tag type."));
            case(0): //HTMTVAL
                obj.values.emplace_back(translationValue::asAccess(contents));
                break;
            case(1): //HTMTQUERY
                obj.values.emplace_back(translationValue::asQuery(contents));
                break;
            }
            left = nameEnd;
            //Skip the closing '>', if present
            if (left != HTML.cend())
                ++left;
        }
    }

    bool matches(const responseWrapper& data, long httpCode) const
    {
        switch (match)
        {
        default:
            return true;
        case(matchType::code):
            return httpCode == std::get<long>(tag);
        case(matchType::condition):
            return std::get<translationCondition>(tag).check(data);
        }
    }

    
    static std::vector<size_t> findNextSet(std::string_view source, const std::vector<std::string_view>& strings)
    {
        std::vector<size_t> ret(strings.size());
        for (size_t i = 0; i < strings.size(); i++)
        {
            ret[i] = source.find(strings[i].data());
        }
        return ret;
    }


public:

    translation() = default;
    translation(const translation&) = delete;
    translation(translation&&) = default;
    translation& operator=(const translation&) = delete;
    translation& operator=(translation&&) = default;

    void apply(std::string& source, const responseWrapper& data, long httpCode, const query& q) const
    {
        if (match == matchType::always)
        {
            //Special case for empty <HTMT> tags and/or default translations
            for (const auto& i : values)
                i.apply(source, data, httpCode, q);
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
                        u.apply(source, i, httpCode, q);
                }
            }
            else
            {
                for (const auto& i : values)
                {
                    i.apply(source, data, httpCode, q);
                }
            }
        }
    }

    std::string apply(const responseWrapper& data, long httpCode, const query& q) const
    {
        std::string ret;
        apply(ret, data, httpCode, q);
        return ret;
    }

    static translation parse(std::string_view source)
    {
        //Divide the string by every instance of "<HTMT:", "<HTMTCOND:" and "<HTMTCOND:" and their respective closing tags
        {
            //Order must match order of matchType
            static const std::vector<std::string_view> tagOpens{ "<HTMT:", "<HTMTCODE:", "<HTMTCOND:" };
            //Order must match order of tagOpens
            static const std::vector<std::string_view> tagCloses{ "</HTMT>", "</HTMTCODE>", "</HTMTCOND>" };

            translation ret;
            {
                size_t left = 0;
                while (left != std::string_view::npos)
                {
                    matchType type;
                    auto found = findNextSet(std::string_view(source.data() + left, source.size() - left), tagOpens);

                    //This is relative to left, not to source.data()
                    auto min = std::min_element(found.cbegin(), found.cend());
                    const auto tagIndex = std::distance(found.cbegin(), min);

                    //That the order of the search values must match the order of matchType declarations
                    type = static_cast<matchType>(tagIndex + 1);

                    if (*min == std::string_view::npos)
                    {
                        //No tag found, everything else is plain HTML
                        parseSubHTML(ret, std::string_view(source.data() + left, source.size() - left));
                        //Nothing left to parse, end loop
                        break;
                    }
                    else if (*min != 0)
                    {
                        //A tag was found and had some HTML before it
                        parseSubHTML(ret, std::string_view(source.data() + left, *min));
                    }
                    const auto tagStart = left + *min;
                    const auto tagClose = std::find(source.cbegin() + tagStart + tagOpens[tagIndex].size(), source.cend(), '>');
                    const auto end = tagSearch(source.cbegin() + tagStart, source.cend(), tagOpens[tagIndex], tagCloses[tagIndex]);

                    translation temp = parse(std::string_view{ &*(tagClose + 1), size_t(std::distance(tagClose + 1, end) - tagCloses[tagIndex].size()) });
                    temp.match = type;


                    const std::string_view tagContents(source.data() + tagStart + tagOpens[tagIndex].size(),
                        std::distance(source.cbegin(), tagClose) - tagStart - tagOpens[tagIndex].size());

                    switch (type)
                    {
                    default:
                        assert("Invalid match type provided.");
                    case(matchType::tag):
                        if (tagContents.empty())
                        {
                            //Special case for empty <HTMT:> tags
                            temp.match = matchType::always;
                        }
                        else
                        {
                            temp.tag = std::string(tagContents);
                        }
                        break;
                    case(matchType::code):
                        temp.tag = std::stol(tagContents.data());
                        break;
                    case(matchType::condition):
                        temp.tag = translationCondition::parse(tagContents);
                        break;
                    }
                    ret.values.emplace_back(std::move(temp));
                    left = std::distance(source.cbegin(), end);
                }
            }
            return ret;
        }
    }
};

void translationValue::apply(std::string& source, const responseWrapper& data, long httpCode, const query& q) const
{
    switch (valueType)
    {
    default:
        throw(std::logic_error("Invalid Enum State"));
    case(state::access):
    {
        const auto maybeValues = data.search(std::get<std::string>(value));
        if (!maybeValues.has_value())
        {
            std::cout << "Warning: Value \"" + std::get<std::string>(value) + "\" was not present in reponse.\n";
            return;
        }
        const auto values = std::get<responseWrapper::stringContainer>(maybeValues.value().get());
        assert(values.size() == 1);
        source += values.front();
        return;
    }
    case(state::HTML):
        source += std::get<std::string>(value);
        return;
    case(state::translation):
        std::get<std::unique_ptr<translation>>(value)->apply(source, data, httpCode, q);
        return;
    case(state::query):
        if (q.hasElement(std::get<std::string>(value)))
            source += q.getElement(std::get<std::string>(value));
        else
            std::cout << "Warning: Query element " << std::get<std::string>(value) << " was not present in request.\n";
        //Note the implicit "else" case, where nothing is added
    }
}

//Generic base class for the other wrappers
//Wraps around HTMT source code, allowing it to be "applied" (translated)
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

//A special wrapper that forwards the request to the back-end server and translates using the response.
class forwardingWrapper final : public webpageWrapper
{
    translation data;
    std::string destination;

    static void applyTranslation(uWS::HttpResponse<true>* res, const APIResponse& API, const translation& tran, const query &q)
    {
        res->writeStatus(std::to_string(API.response_code));
        for (const auto& i : API.headers)
        {
            res->writeHeader(i.first, i.second);
        }
        const auto resw = responseWrapper::fromData(API.response);
        if (resw.has_value())
        {
            res->tryEnd(tran.apply(resw.value(), API.response_code, q));
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
            applyTranslation(res, curl.post(body), tran, query(req));
        }
        );
    }
public:

    forwardingWrapper() = default;
    forwardingWrapper(const std::string& path, const std::string& dest) : data(translation::parse(readEntireFile(path))), destination(dest) {};

    void operator()(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const final { apply(res, req); }

    void apply(uWS::HttpResponse<true>* res, uWS::HttpRequest* req) const final
    {
        std::string url = "localhost:9001";
        url += destination;
        const auto urlQuery = req->getQuery();
        if (!urlQuery.empty())
        {
            url += '?';
            url.append(urlQuery.data(), urlQuery.size());
        }
        requestWrapper request(url);
        request.setCookies(std::string{ req->getHeader("cookie") });
        if (req->getMethod() == "post")
        {
            forwardPost(res, req, std::move(request), data);
        }
        else
        {
            applyTranslation(res, request.get(), data, query(req));
        }
    }
};

//A wrapper that only uses static data, only accepts raw HTML (HTMT doesn't apply as no values can be accessed and there is no HTTP code to read)
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

//Loads pages and adds appropriate links
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
        //The line must start with at least two letters and a ':'
        //If the line starts with '#' then it is a comment and can be skipped
        //Empty lines are skipped
        if (line.empty() || line[0] == '#')
            continue;
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

        //If this is a [S]tatic link (it does not forward to the back-end)
        if (line[1] == 'S')
        {
            const std::string fileDirectory{ div + 1, line.cend() };
            std::cout << "Linked page \"" + webDirectory + "\" to local page \"" + fileDirectory + "\".\n";
            //If this is a [P]OST request
            if (line[0] == 'P')
            {
                app.post(webDirectory, staticWrapper(fileDirectory));
            }
            else //This is a [G]ET request
            {
                app.get(webDirectory, staticWrapper(fileDirectory));
            }
        }
        else //This is a [F]orwarding link
        {
            const auto secDiv = std::find(div + 1, line.cend(), ':');
            const std::string fileDirectory{ div + 1, secDiv };
            const std::string target = 
            secDiv == line.cend() ? webDirectory : std::string{ secDiv + 1, line.cend() };

            std::cout << "Linked page \"" + webDirectory + "\" to local page \"" + fileDirectory + "\" which forwards to \"" + target + "\".\n";

            //If this is a [P]OST request
            if (line[0] == 'P')
            {
                app.post(webDirectory, forwardingWrapper(fileDirectory, target));
            }
            else //This is a [G]ET request
            {
                app.get(webDirectory, forwardingWrapper(fileDirectory, target));
            }
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    uWS::SSLApp app;
    app.listen(9002, [](auto*) {});
    //Default response is simply the text "Bad translation" - not to be confused the with the server response "Bad request".
    app.any("/*", [](auto* req, auto* res) {req->end("Bad translation."); });
    linkPages(app, "../Pages/Link.txt");
    std::cout << "Linking complete.\n";
    //App will run until program termination
    app.run();
}