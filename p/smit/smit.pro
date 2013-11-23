TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    ../../mongoose/mongoose.c \
    ../../src/hello.c \
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
    ../../src/dateTools.cpp

HEADERS += \
    ../../mongoose/mongoose.h \
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
    ../../src/global.h

