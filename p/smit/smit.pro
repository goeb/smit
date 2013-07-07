TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += \
    ../../mongoose/mongoose.c \
    ../../src/hello.c \
    ../../src/db.cpp \
    ../../src/upload.cpp

HEADERS += \
    ../../mongoose/mongoose.h \
    ../../src/db.h

