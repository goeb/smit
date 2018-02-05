#ifndef _Auth_h
#define _Auth_h

#define AUTH_NONE "none"

struct Auth {
    std::string type;
    std::string username;
    virtual int authenticate(const char *password) = 0;
    virtual std::string serialize() = 0;
    virtual Auth *createCopy() const = 0;
    inline virtual ~Auth() { }
    inline Auth(const std::string &t, const std::string &u) :
        type(t), username(u) {}
    inline virtual std::string getParameter(const char *param) { return ""; }
};

#endif
