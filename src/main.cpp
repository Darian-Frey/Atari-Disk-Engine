#include "../ui/MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // High DPI scaling for modern displays
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    
    QApplication a(argc, argv);
    
    MainWindow w;
    w.show();
    
    return a.exec();
}
