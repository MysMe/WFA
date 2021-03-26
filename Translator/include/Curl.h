#pragma once
#include <curl/curl.h>
#include <vector>
#include <string>

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

    //Header fields that will be copied to the response
    static constexpr auto allowedHeaders =
    {
        "Set-Cookie",
        "cookie"
    };

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

        bool flag = false;

        for (const auto& i : allowedHeaders)
        {
            if (i == name)
            {
                flag = true;
                break;
            }
        }

        if (!flag)
            return nitems;

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