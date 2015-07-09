TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    ../../third-party/mongoose.c \
    ../../src/db.cpp \
    ../../src/parseConfig.cpp \
    ../../src/identifiers.cpp \
    ../../test/T_identifiers.cpp \
    ../../test/T_parseConfig.cpp \
    ../../src/main.cpp \
    ../../src/httpdHandlers.cpp \
    ../../src/renderingText.cpp \
    ../../src/renderingHtml.cpp \
    ../../src/cpio.cpp \
    ../../src/stringTools.cpp \
    ../../src/session.cpp \
    ../../src/mutexTools.cpp \
    ../../src/dateTools.cpp \
    ../../src/logging.cpp \
    ../../src/renderingCsv.cpp \
    ../../src/Trigger.cpp \
    ../../src/filesystem.cpp \
    ../../src/clone.cpp \
    ../../src/console.cpp \
    ../../src/HttpContext.cpp \
    ../../src/httpClient.cpp \
    ../../src/localClient.cpp \
    ../../src/Args.cpp \
    ../../src/Entry.cpp \
    ../../src/Project.cpp \
    ../../src/Issue.cpp \
    ../../src/View.cpp \
    ../../src/Tag.cpp \
    ../../src/ProjectConfig.cpp \
    ../../src/Object.cpp \
    ../../src/AuthKrb5.cpp \
    ../../src/AuthSha1.cpp \
    ../../src/AuthLdap.cpp \
    ../../src/third-party/mongoose.c \
    ../../src/third-party/fnmatch.c \
    ../../src/renderingJson.cpp \
    ../../src/json.cpp
    ../../src/ssoKerberos.cpp

HEADERS += \
    ../../src/third-party/mongoose.h \
    ../../src/db.h \
    ../../src/parseConfig.h \
    ../../src/ustring.h \
    ../../src/identifiers.h \
    ../../src/logging.h \
    ../../src/httpdHandlers.h \
    ../../src/renderingText.h \
    ../../src/renderingHtml.h \
    ../../src/cpio.h \
    ../../src/stringTools.h \
    ../../src/session.h \
    ../../src/mutexTools.h \
    ../../src/dateTools.h \
    ../../src/global.h \
    ../../src/renderingCsv.h \
    ../../mongoose/mg_win32.h \
    ../../src/Trigger.h \
    ../../src/filesystem.h \
    ../../src/clone.h \
    ../../src/console.h \
    ../../src/HttpContext.h \
    ../../src/httpClient.h \
    ../../src/localClient.h \
    ../../src/Args.h \
    ../../src/Entry.h \
    ../../src/Project.h \
    ../../src/Issue.h \
    ../../src/View.h \
    ../../src/Object.h \
    ../../src/restApi.h \
    ../../src/ProjectConfig.h \
    ../../src/Auth.h \
    ../../src/AuthKrb5.h \
    ../../src/AuthSha1.h \
    ../../src/AuthLdap.h \
    ../../src/third-party/fnmatch.h \
    ../../src/mg_win32.h \
    ../../src/Tag.h \
    ../../src/renderingJson.h \
    ../../src/json.h
    ../../src/ssoKerberos.h

