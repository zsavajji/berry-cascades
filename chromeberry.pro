# A plain executable, as the entry point; cascades10.prf would make a
# release build a shared library for the Cascades launcher instead.
TEMPLATE = app
TARGET = chromeberry
CONFIG += qt warn_on release
QT += declarative network
QT -= gui
LIBS += -lbbcascades -lbbdata -lbbsystem -lbb -lbbutility -lscreen -lbps
QMAKE_LFLAGS += -Wl,-rpath-link,$${QNX_DIR}/$${QNX_CPUDIR}/usr/lib/qt4/lib
QMAKE_CXXFLAGS += -Wno-psabi -fstack-protector-strong
DEFINES += _FORTIFY_SOURCE=2

SOURCES += src/main.cpp src/browser.cpp src/devtools.cpp src/downloads.cpp src/history.cpp
HEADERS += src/browser.h src/devtools.h src/downloads.h src/history.h

DESTDIR = app
OBJECTS_DIR = obj
MOC_DIR = moc
