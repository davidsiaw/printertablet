// Native::Http: blocking HTTP requests, for worker VMs only (the main VM must
// never block; see native/worker.cpp).
//
//   request(method, url, timeout) -> [status, body]
//       url: http://host[:port]/path. timeout: seconds to wait for the reply.
//       Raises RuntimeError (message: just the reason, e.g. "Connection
//       refused") if the request fails: network, timeout, or the worker is
//       stopping. Any HTTP status, including errors, is returned.

#include "native/native.hpp"

#include <cstdlib>
#include <string>

#include <mruby/array.h>
#include <mruby/variable.h>

#include "http.hpp"
#include "scripting.hpp"

namespace
{
    RClass* HttpModule(mrb_state* mrb)
    {
        return mrb_module_get_under(mrb, mrb_module_get(mrb, "Native"), "Http");
    }

    // Splits http://host[:port]/path. False if it isn't one.
    bool ParseUrl(const std::string& url, std::string& host, int& port, std::string& path)
    {
        const std::string scheme = "http://";
        if (url.compare(0, scheme.size(), scheme) != 0) return false;
        size_t start = scheme.size();
        size_t slash = url.find('/', start);
        std::string authority = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        path = slash == std::string::npos ? "/" : url.substr(slash);
        size_t colon = authority.rfind(':');
        port = 80;
        if (colon != std::string::npos)
        {
            port = atoi(authority.c_str() + colon + 1);
            authority = authority.substr(0, colon);
        }
        host = authority;
        return !host.empty() && port > 0;
    }

    // Does the request. Returns [status, body], or an exception to raise.
    mrb_value DoRequest(mrb_state* mrb, const std::string& method, const std::string& url, double timeout,
                        SDL_atomic_t* cancel)
    {
        std::string host, path, body, error;
        int port = 0, status = 0;
        if (!ParseUrl(url, host, port, path))
        {
            std::string message = "not an http:// URL: " + url;
            return mrb_exc_new(mrb, E_ARGUMENT_ERROR, message.data(), message.size());
        }
        if (!Http::Request(method.c_str(), host, port, path, status, body, error, cancel, (int)(timeout * 1000)))
        {
            return mrb_exc_new(mrb, E_RUNTIME_ERROR, error.data(), error.size());   // e.g. "Connection refused"
        }
        mrb_value result = mrb_ary_new_capa(mrb, 2);
        mrb_ary_push(mrb, result, mrb_fixnum_value(status));
        mrb_ary_push(mrb, result, mrb_str_new(mrb, body.data(), body.size()));
        return result;
    }

    mrb_value Request(mrb_state* mrb, mrb_value self)
    {
        mrb_value method, url;
        mrb_float timeout;
        mrb_get_args(mrb, "SSf", &method, &url, &timeout);
        mrb_value cancel = mrb_iv_get(mrb, mrb_obj_value(HttpModule(mrb)), mrb_intern_lit(mrb, "@cancel"));
        mrb_value result = DoRequest(mrb,
                                     std::string(RSTRING_PTR(method), RSTRING_LEN(method)),
                                     std::string(RSTRING_PTR(url), RSTRING_LEN(url)),
                                     timeout, static_cast<SDL_atomic_t*>(mrb_cptr(cancel)));
        // Raise only now, with no C++ objects alive in this frame.
        if (mrb_exception_p(result)) mrb_exc_raise(mrb, result);
        return result;
    }
}

void Native::BindHttp(Scripting& scripting, SDL_atomic_t* cancel)
{
    Module(scripting)->create_module("Http");
    mrb_state* mrb = scripting.GetState();
    RClass* http = HttpModule(mrb);
    mrb_iv_set(mrb, mrb_obj_value(http), mrb_intern_lit(mrb, "@cancel"), mrb_cptr_value(mrb, cancel));
    mrb_define_module_function(mrb, http, "request", Request, MRB_ARGS_REQ(3));
}
