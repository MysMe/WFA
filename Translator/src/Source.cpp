#include <iostream>
#include <uwebsockets/App.h>
#include <curl/curl.h>
#include <string_view>
#include <vector>

class curlwrapper
{
    CURL* object = nullptr;
    void tidy()
    {
        if (object != nullptr)
        {
            curl_easy_cleanup(object);
            object = nullptr;
        }
    }
public:
    curlwrapper()
    {
        object = curl_easy_init();
    }
    curlwrapper(const curlwrapper&) = delete;
    curlwrapper(curlwrapper&& other) noexcept
    {
        object = other.object;
        other.object = nullptr;
    }
    curlwrapper& operator=(const curlwrapper&) = delete;
    curlwrapper& operator=(curlwrapper&& other) noexcept
    {
        tidy();
        object = other.object;
        other.object = nullptr;
        return *this;
    }
    ~curlwrapper()
    {
        tidy();
    }

    CURL* data() { return object; }
    const CURL* data() const { return object; }
    operator CURL* () { return object; }
    operator const CURL* () const { return object; }
    bool valid() const { return object != nullptr; }

    void perform()
    {
        curl_easy_perform(object);
    }
};

class multicurlwrapper
{
public:
    CURLM* object = nullptr;
    std::vector<curlwrapper> curls;

    void tidy()
    {
        if (object != nullptr)
        {
            for (auto& i : curls)
            {
                curl_multi_remove_handle(object, i.data());
            }
            curls.clear();
            curl_multi_cleanup(object);
        }
    }
public:
    multicurlwrapper()
    {
        object = curl_multi_init();
    }
    multicurlwrapper(const multicurlwrapper& other) = delete;
    multicurlwrapper(multicurlwrapper&& other) noexcept
    {
        object = other.object;
        other.object = nullptr;
        curls = std::move(other.curls);
        other.curls.clear();
    }
    multicurlwrapper& operator=(const multicurlwrapper&) = delete;
    multicurlwrapper& operator=(multicurlwrapper&& other) noexcept
    {
        tidy();
        object = other.object;
        other.object = nullptr;
        curls = std::move(other.curls);
        other.curls.clear();
    }

    ~multicurlwrapper()
    {
        tidy();
    }

    void perform()
    {
        int connections = 0;
        do
        {
            curl_multi_perform(object, &connections);
        } while (connections != 0);
    }

    void add(curlwrapper&& obj)
    {
        curls.emplace_back(std::move(obj));
        curl_multi_add_handle(object, curls.back().data());
    }
};

struct APIResponse
{
    std::vector<std::pair<std::string, std::string>> headers;
    std::string response;
    long response_code = 0;
    double responseTime = 0;

    void bind(curlwrapper& curl) noexcept
    {
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &writeHeaders);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &responseTime);
    }

    static size_t writeHeaders(char* buffer, size_t size, size_t nitems, decltype(headers)* data)
    {
        const auto div = std::find(buffer, buffer + nitems, ':');
        std::string name{ buffer, div };
        std::string value;
        if (div != buffer + nitems)
        {
            assert(div[1] = ' ');
            assert(buffer[nitems - 1] == '\n' && buffer[nitems - 2] == '\r');
            //Skip the ':' and following space, ignore the trailing \r\n
            value.assign(div + 2, buffer + nitems - 2);
            data->emplace_back(std::move(name), std::move(value));
        }
        return nitems;
    }
};

class requestWrapper
{
    static size_t dataWrite(void* dataptr, size_t size, size_t nmemb, std::string* data)
    {
        data->append((char*)dataptr, size * nmemb);
        return size * nmemb;
    }

    APIResponse response;
    curlwrapper curl;

    static curlwrapper bind(APIResponse& response, std::string_view URL)
    {
        curlwrapper curl;
        if (!curl.valid()) return curl;
        response.bind(curl);
        curl_easy_setopt(curl, CURLOPT_URL, URL.data());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dataWrite);
        return curl;
    }

    void perform()
    {
        curl.perform();
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.response_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &response.responseTime);
    }

public:
    requestWrapper() = delete;
    requestWrapper(std::string_view URL)
    {
        curl = bind(response, URL);
    }

    requestWrapper(requestWrapper&& other) noexcept
    {
        *this = std::move(other);
    }
    requestWrapper& operator=(requestWrapper&& other) noexcept
    {
        curl = std::move(other.curl);
        response.bind(curl);
        return *this;
    }

    void setCookies(const std::string& values)
    {
        curl_easy_setopt(curl, CURLOPT_COOKIE, values.c_str());
    }

    void retarget(const std::string& URL)
    {
        curl_easy_setopt(curl, CURLOPT_URL, URL.data());
    }

    const APIResponse& post(std::string_view data)
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
        perform();
        return response;
    }

    const APIResponse& get()
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, true);
        perform();
        return response;
    }
};

#include <map>
std::string params_string(std::map<std::string, std::string> const& params)
{
    if (params.empty()) return "";
    std::map<std::string, std::string>::const_iterator pb = params.cbegin(), pe = params.cend();
    std::string data = pb->first + "=" + pb->second;
    ++pb; if (pb == pe) return data;
    for (; pb != pe; ++pb)
        data += "&" + pb->first + "=" + pb->second;
    return data;
}

std::string testTranslate();

#include <charconv>

template <class Fn>
void extractPostBody(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, Fn&& callback)
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

void writeBack(uWS::HttpResponse<true>* res, const APIResponse& API)
{
    res->writeStatus(std::to_string(API.response_code));
    for (const auto& i : API.headers)
    {
        res->writeHeader(i.first, i.second);
    }
    res->tryEnd(API.response);
}

void forwardPost(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, requestWrapper&& curl)
{
    extractPostBody(res, req,
        [curl = std::move(curl)](uWS::HttpResponse<true>* res, uWS::HttpRequest* req, std::string_view body) mutable
    {
        writeBack(res, curl.post(body));
        res->end();
    }
    );
}

void forward(uWS::HttpResponse<true>* res, uWS::HttpRequest* req)
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
        forwardPost(res, req, std::move(request));
    }
    else
    {
        writeBack(res, request.get());
        res->end();
    }
}

#include <variant>
#include <any>
#include <optional>

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

struct formattedResponse
{
    std::string field;
    std::optional<std::vector<formattedResponse>> children;

    bool isBranch() const { return children.has_value(); }
    bool isLeaf() const { return !isBranch(); }

    //This node is a branch to a leaf node and therefore only has one child
    bool holdsLeaf() const { return isBranch() && children->size() == 1 && (*children)[0].isLeaf(); }

    bool operator==(const std::string_view name) const { return field == name; }

    static std::pair<formattedResponse, std::string_view::const_iterator> interpret(std::string_view::const_iterator begin, std::string_view::const_iterator end)
    {
        formattedResponse ret;

        const auto stripped = std::find_if(begin, end, [](char v) {return !std::isspace(v); });
        if (stripped == end)
        {
            //Empty field with no children
            return { ret, end };
        }

        if (*stripped != '<')
        {
            ret.field.assign(stripped, end);
            return { ret, end };
        }



        const auto tagEnd = std::find(stripped, end, '>');
        if (tagEnd == end)
        {
            ret.field = "{PARSE ERROR: Tag not closed.}";
            return { ret, end };
        }

        const auto contentStart = tagEnd + 1;

        std::string_view tagName{ &*(stripped + 1), static_cast<size_t>(std::distance(stripped, tagEnd)) - 1 };

        const auto blockEnd = tagSearch(stripped, end, std::string_view(&*stripped, tagName.size()), std::string("/") + std::string(tagName) + ">");

        const auto contentEnd = blockEnd - tagName.size() - 3;

        ret.field = tagName;

        if (contentStart != contentEnd)
            ret.children = std::vector<formattedResponse>{};

        auto it = contentStart;
        while (it != contentEnd)
        {
            auto [res, pos] = formattedResponse::interpret(it, contentEnd);
            if (res.field.empty())
                break;
            it = pos;
            ret.children->emplace_back(std::move(res));
        }
        return { ret, blockEnd };
    }

    static formattedResponse tlInterpret(std::string_view input)
    {
        formattedResponse ret;
        ret.children = std::vector<formattedResponse>();
        auto it = input.cbegin();
        while (it != input.cend())
        {
            auto [res, pos] = interpret(it, input.cend());
            if (res.field.empty())
                break;
            it = pos;
            ret.children->emplace_back(std::move(res));
        }
        return ret;
    }

};



class translation
{
public:

    struct operation
    {
        enum class type
        {
            substitute,
            assign
        };

        type op;
        std::string data;
        std::string perform(const formattedResponse& res) const
        {
            switch (op)
            {
            case(type::substitute):
                return data;
            case(type::assign):
                if (res.isLeaf())
                {
                    if (data == "")
                    {
                        return res.field;
                    }
                    else
                    {
                        return "{HTMT ERROR: Current object (\"" + res.field + "\") did not have any subobjects, attempted to access \"" + data + "\".}";
                    }
                }
                else if (data == "" && res.holdsLeaf())
                {
                    return (*res.children)[0].field;
                }
                else
                {
                    const auto begin = res.children->cbegin(), end = res.children->cend();
                    const std::string_view view(data);
                    const auto obj = std::find(begin, end, view);
                    if (obj == res.children->cend())
                    {
                        return "{HTMT ERROR: Current object (\"" + res.field + "\") did not have subobject \"" + data + "\".}";
                    }
                    else if (!obj->holdsLeaf())
                    {
                        return "{HTMT ERROR: Current object (\"" + res.field + "\") had subobject \"" + data + "\" but it was not a field.}";
                    }
                    else
                    {
                        return (*obj->children)[0].field;
                    }
                }
            default:
                return "";
            }
        }
    };

    std::string search;
    size_t minPos = 0, maxPos = std::numeric_limits<size_t>::max();

    std::vector<std::variant<operation, translation>> structure;

    std::string execute(const formattedResponse& res) const
    {
        std::string ret;
        for (auto& i : structure)
        {
            if (i.index() == 0)
            {
                ret += std::get<operation>(i).perform(res);
            }
            else
            {
                ret += std::get<translation>(i).perform(res);
            }
        }
        return ret;
    }

    void interpretTagArgs(std::string_view tag)
    {
        tag;
    }

    void parseStatement(std::string_view statement)
    {
        size_t pos = 0;
        while (pos < statement.size())
        {
            size_t next = statement.find('[', pos);
            if (next == std::string_view::npos)
            {
                operation t;
                t.op = operation::type::substitute;
                t.data.assign(statement.cbegin() + pos, statement.cend());
                structure.emplace_back(std::move(t));
                break;
            }

            size_t end = statement.find(']', next);
            if (end == std::string_view::npos)
                end = statement.size();

            operation t;
            t.op = operation::type::substitute;
            t.data.assign(statement.cbegin() + pos, statement.cbegin() + next);
            structure.emplace_back(std::move(t));

            t.op = operation::type::assign;
            t.data.assign(statement.cbegin() + next + 1, statement.cbegin() + end);
            structure.emplace_back(std::move(t));

            pos = end + 1;
        }
    }

public:
    
    static translation parse(std::string_view input)
    {
        translation ret;
        
        assert(input.size() > sizeof("<HTMT>"));
        assert(std::strncmp(input.data(), "<HTMT>", sizeof("<HTMT>") - 1) == 0);
        assert(std::strncmp(input.data() + input.size() - sizeof("</HTMT>") + 1, "</HTMT>", sizeof("</HTMT>") - 1) == 0);


        auto openEnd = input.find('>');
        auto closeStart = input.size() - sizeof("</HTMT>");

        ret.interpretTagArgs(std::string_view(input.data(), openEnd));

        input = std::string_view(input.data() + openEnd + 1, closeStart - openEnd);

        size_t pos = 0;
        while (pos < input.size())
        {
            size_t next = input.find("<HTMT", pos);
            if (next == std::string_view::npos)
                next = input.size();

            if (pos != next)
            {
                ret.parseStatement(std::string_view(input.data() + pos, next - pos));
            }
            if (next != input.size())
            {
                auto tagEnd = tagSearch(input.cbegin() + next, input.cend(), "<HTMT", "/HTMT>");
                ret.structure.emplace_back(
                    translation::parse(
                        std::string_view(input.data() + next, std::distance(input.cbegin() + next, tagEnd))));

                pos = std::distance(input.cbegin(), tagEnd);
            }
            else
            {
                pos = next;
            }
        }

        return ret;
    }

    std::string perform(const formattedResponse& res) const
    {
        std::string ret;
        if (!res.children)
            return ret;
        size_t pos = 0;
        for (auto i : *res.children)
        {
            if (search == "" || i.field == search)
            {
                if (pos < minPos)
                {
                    pos++;
                    continue;
                }
                if (pos >= maxPos)
                {
                    break;
                }
                ret += execute(i);
                pos++;
            }
        }
        return ret;
    }

};

class translatedPage
{
public:
    std::vector<std::variant<std::string, translation>> source;

public:

    std::string translate(const formattedResponse& res)
    {
        std::string ret;
        assert(res.children.has_value());

        for (const auto& i : source)
        {
            if (i.index() == 0)
            {
                ret += std::get<std::string>(i);
            }
            else
            {
                const auto& val = std::get<translation>(i);
                ret += val.perform(res);
            }
        }
        return ret;
    }

    static translatedPage parseTags(std::string_view input)
    {
        translatedPage ret;

        size_t pos = 0;
        while (pos < input.size())
        {
            const std::string_view substr = std::string_view(input.data() + pos, input.size() - pos);
            size_t next = substr.find("<HTMT");
            if (next == std::string_view::npos)
                next = input.size();

            if (pos != next)
            {
                ret.source.emplace_back(std::string(input.cbegin() + pos, input.cbegin() + next));
            }

            if (next != input.size())
            {
                auto tagEnd = tagSearch(input.cbegin() + next, input.cend(), "<HTMT", "/HTMT>");
                ret.source.emplace_back(
                    translation::parse(
                        std::string_view(input.data() + next, std::distance(input.cbegin() + next, tagEnd))));

                pos = std::distance(input.cbegin(), tagEnd);
            }
            else
            {
                pos = next;
            }
        }

        return ret;
    }
};


formattedResponse gr;


std::string testTranslate()
{
    formattedResponse simAPIRes;
    /*
    Build the response to be equivalent to

    <obj>
        <f1>
            Hello
        </f1>
        <f2>
            There
        </f2>
    </obj>
    <obj>
        <f1>
            Good
        </f1>
        <f2>
            Day
        </f2>
    </obj>
    */
    {
        simAPIRes.children = std::vector<formattedResponse>();
        simAPIRes.children->resize(2);
        auto& vec = *simAPIRes.children;

        vec[0].field = "obj";
        vec[0].children = std::vector<formattedResponse>();
        auto& v1 = *vec[0].children;
        v1.resize(2);
            v1[0].field = "f1";
                v1[0].children = std::vector<formattedResponse>();
                v1[0].children->resize(1);
                (*v1[0].children)[0].field = "Hello";
            v1[1].field = "f2";
                v1[1].children = std::vector<formattedResponse>();
                v1[1].children->resize(1);
                (*v1[1].children)[0].field = "There";

        vec[1].field = "obj";
        vec[1].children = std::vector<formattedResponse>();
        auto& v2 = *vec[1].children;
        v2.resize(2);
            v2[0].field = "f1";
                v2[0].children = std::vector<formattedResponse>();
                v2[0].children->resize(1);
                (*v2[0].children)[0].field = "Good";
            v2[1].field = "f2";
                v2[1].children = std::vector<formattedResponse>();
                v2[1].children->resize(1);
                (*v2[1].children)[0].field = "Day";
    }


    /*
    Build the equivalent of the HTMT code:

    <table><HTMT><tr><HTMT><th>[]</th></HTMT></tr></HTMT></table>

    */
    return translatedPage::
        parseTags("<table><HTMT><tr><HTMT><th>[]</th></HTMT></tr></HTMT></table>")
        .translate(gr);
}

void discard()
{

    uWS::SSLApp app;
    app.listen(9002, [](auto*) {});
    app.any("/*", forward);
    std::cout << "Ready to forward (9002).\n";
    app.run();
    std::cin.ignore();
}

void testConnect()
{
    requestWrapper request("localhost:9001/request");
    const auto r1 = request.post("username=ADMIN&password=ADMIN");
    request.retarget("localhost:9001/ping");
    const auto r2 = request.get();

    std::cin.ignore();
}

int main(int argc, char** argv) 
{
    discard();
    requestWrapper request("localhost:9001/ping");
    auto result = request.get();
    testConnect();
    std::cin.ignore();
}