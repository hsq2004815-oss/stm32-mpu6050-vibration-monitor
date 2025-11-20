QT += core gui serialport widgets printsupport network multimedia

CONFIG += c++11
TARGET = job
TEMPLATE = app
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

message($$QT_VERSION)

SOURCES += \
    main.cpp \
    widget.cpp \
    qcustomplot.cpp \
    deepseekanalyzer.cpp

HEADERS += \
    widget.h \
    qcustomplot.h \
    deepseekanalyzer.h

FORMS += \
    widget.ui

# 添加这一行确保链接器能找到所有符号
LIBS += -lQt5PrintSupport
