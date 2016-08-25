#ifndef _Auth_h
#define _Auth_h

struct Auth {
    std::string type;
    std::string username;
    virtual int authenticate(char *password) = 0;
    virtual std::string serialize() = 0;
    virtual Auth *createCopy() const = 0;
    inline virtual ~Auth() { }
    inline Auth(const std::string &t, const std::string &u) :
        type(t), username(u) {}
};

#endif
