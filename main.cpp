/*
 * Out Of Band (OOB-Comm) user interface for reTerminal
 *
 * (C) 2022 Resilience Theatre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include "mainwindow.h"
#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_FONTDIR", "/usr/share/fonts/dejavu/");
    qputenv("QT_QPA_EGLFS_ROTATION", "-90");
    // Development:
    qputenv("QT_ASSUME_STDERR_HAS_CONSOLE", "1");

    QApplication a(argc, argv);
    a.setOrganizationName("resiliencetheatre");
    a.setOrganizationDomain("rd");
    a.setApplicationName("sinm");      
    QStringList args = a.arguments();
    if (args.count() == 2)
    {
        if ( args.at(1).contains("vault") )
        {
            MainWindow w(VAULT_MODE);
            w.show();
            return a.exec();
        } else {
            MainWindow w(UI_MODE);
            w.show();
            return a.exec();
        }
    } else {
        MainWindow w(UI_MODE);
        w.show();
        return a.exec();
    }
    return a.exec();
}
