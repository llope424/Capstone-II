#include <QApplication>
#include <QStyleFactory>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("ObdSuite");
    QApplication::setOrganizationName("ObdSuite");
    QApplication::setApplicationVersion(OBDSUITE_VERSION);

    // Fusion gives a consistent, predictable look across widget states (the
    // native Windows style renders disabled buttons with near-invisible text on
    // a dark desktop) and is the same style the dark-theme toggle switches on.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    MainWindow window;
    window.show();

    return QApplication::exec();
}
