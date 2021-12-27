#include "Networking.h"
#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace uS {

namespace TLS {

Context::Context(const Context &other)
{
    if (other.context) {
        context = other.context;
        SSL_CTX_up_ref(context);
    }
}

Context &Context::operator=(const Context &other) {
    if (other.context) {
        context = other.context;
        SSL_CTX_up_ref(context);
    }
    return *this;
}

Context::~Context()
{
    if (context) {
        SSL_CTX_free(context);
    }
}

struct Init {
    Init() {
        SSL_library_init();

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
        signal(SIGPIPE, SIG_IGN);
#endif
    }
    ~Init() {
        /*EVP_cleanup();*/
#ifdef _WIN32
        WSACleanup();
#endif
    }
} init;

Context createContext(std::string certChainFileName, std::string keyFileName, std::string keyFilePassword)
{
    Context context(SSL_CTX_new(SSLv23_server_method()));
    if (!context.context) {
        return nullptr;
    }

    if (keyFilePassword.length()) {
        context.password.reset(new std::string(keyFilePassword));
        SSL_CTX_set_default_passwd_cb_userdata(context.context, context.password.get());
        SSL_CTX_set_default_passwd_cb(context.context, 
            [](char *buf, int size, int rwflag, void *u) -> int {
                std::string *password = (std::string *) u;
                int length = std::min<int>(size, (int)password->length());
                memcpy(buf, password->data(), length);
                buf[length] = '\0';
                return length;
            });
    }

    SSL_CTX_set_options(context.context, SSL_OP_NO_SSLv3);

    if (SSL_CTX_use_certificate_chain_file(context.context, certChainFileName.c_str()) != 1) {
        return nullptr;
    } else if (SSL_CTX_use_PrivateKey_file(context.context, keyFileName.c_str(), SSL_FILETYPE_PEM) != 1) {
        return nullptr;
    }

    return context;
}

}

}
