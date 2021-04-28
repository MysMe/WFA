#pragma once
#include <uwebsockets/App.h>
#include <cassert> //Note that curl mistakenly fails to include <cassert>, yet uses assert, do not reorder
#include "Curl.h"
#include "Response.h"
#include <charconv>

//UWebSockets uses a callback on the post body, so we have to put a callback in that callback using a third callback
//Aka, the callback for the socket will call an unnamed callback which itself will call the argument provided to this function
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

//Forwards response data (notably cookies and contents) through the socket
void writeBack(uWS::HttpResponse<true>* res, const APIResponse& API)
{
    res->writeStatus(std::to_string(API.response_code));
    for (const auto& i : API.headers)
    {
        res->writeHeader(i.first, i.second);
    }
    const auto resw = responseWrapper::fromData(API.response);
    if (resw.has_value())
    {
        const auto str = resw.value().toData(true);
        res->end(str);
    }
    else
    {
        res->end(API.response);
    }
}

//Wraps a callback for UWS to call later
void forwardPost(uWS::HttpResponse<true>* res, uWS::HttpRequest* req, requestWrapper&& curl)
{
    extractPostBody(res, req,
        [curl = std::move(curl)](uWS::HttpResponse<true>* res, uWS::HttpRequest* req, std::string_view body) mutable
    {
        writeBack(res, curl.post(body));
    }
    );
}

//Builds and sends a request to a URL, using the URL sent to the server
void forward(uWS::HttpResponse<true>* res, uWS::HttpRequest* req)
{
    //Note that this is dependant on the server configuration
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
    }
}
