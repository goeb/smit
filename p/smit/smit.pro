TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    ../../mongoose/mongoose.c \
    ../../src/hello.c \
    ../../src/db.cpp \
    ../../src/upload.cpp \
    ../../src/parseConfig.cpp \
    ../../src/identifiers.cpp \
    ../../test/T_identifiers.cpp \
    ../../test/T_parseConfig.cpp \
    ../../src/main.cpp \
    ../../src/httpdHandlers.cpp \
    ../../src/renderingText.cpp

HEADERS += \
    ../../mongoose/mongoose.h \
    ../../src/db.h \
    ../../src/parseConfig.h \
    ../../src/ustring.h \
    ../../src/identifiers.h \
    ../../src/logging.h \
    ../../src/httpdHandlers.h \
    ../../src/renderingText.h

