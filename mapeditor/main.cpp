/*
 * main.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include <QApplication>
#include "mainwindow.h"

#include "../client/CMT.h"
#include "../client/gui/CGuiHandler.cpp"

int main(int argc, char * argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

	QApplication vcmieditor(argc, argv);
	MainWindow mainWindow;
	return vcmieditor.exec();
}

void handleQuit(bool ask)
{
	qApp->exit();
}

[[noreturn]] void handleFatalError(const std::string & message, bool terminate)
{
	qApp->quit();
	std::exit(0);
}
