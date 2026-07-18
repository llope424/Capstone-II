#include <QApplication>

#include "AppSettings.h"
#include "AppStyle.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("ObdSuite");
    QApplication::setOrganizationName("ObdSuite");
    QApplication::setApplicationVersion(OBDSUITE_VERSION);

    // Fusion style + the user's saved color preset (AppStyle also selects
    // Fusion, which renders predictably across widget states and platforms).
    AppStyle::apply(AppSettings::styleName());

    MainWindow window;
    window.show();

    return QApplication::exec();
}
