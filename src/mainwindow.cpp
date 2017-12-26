/* Atelier KDE Printer Host for 3D Printing
    Copyright (C) <2016>
    Author: Lays Rodrigues - laysrodrigues@gmail.com
            Chris Rizzitello - rizzitello@kde.org

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <KLocalizedString>
#include <KStandardAction>
#include <KActionCollection>
#include <KXMLGUIFactory>


MainWindow::MainWindow(QWidget *parent) :
    KXmlGuiWindow(parent),
    ui(new Ui::MainWindow),
    profilesDialog(new ProfilesDialog(this)),
    connectSettingsDialog(new ConnectSettingsDialog(this))
{
    ui->setupUi(this);
    setupActions();
    initWidgets();
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::initWidgets()
{
    // When a new profile is added on the Profile Dialog it needs to update the profiles on connection dialog
    connect(profilesDialog, &ProfilesDialog::updateProfiles,
            connectSettingsDialog, &ConnectSettingsDialog::updateProfiles);

//     connectSettingsDialog->setFirmwareList(core.availableFirmwarePlugins());
//     profilesDialog->setBaudRates(core.serial()->validBaudRates());
}

void MainWindow::setupActions()
{
    // Actions for the Toolbar
    QAction *action;
    action = actionCollection()->addAction(QStringLiteral("open_gcode"));
    action->setText(i18n("&Open GCode"));
    connect(action, &QAction::triggered, this, &MainWindow::openFile);

    _connect = actionCollection()->addAction(QStringLiteral("connect"));
    _connect->setText(i18n("&Connect"));

    action = actionCollection()->addAction(QStringLiteral("profiles"));
    action->setText(i18n("&Profiles"));
    connect(action, &QAction::triggered, profilesDialog, &ProfilesDialog::show);


    #ifdef Q_OS_LINUX
    //only set icons from theme on linux
        actionCollection()->action(QStringLiteral("profiles"))->setIcon(QIcon::fromTheme("emblem-favorite"));
    #endif
    //use style's standardIcon for the icons we can.
    actionCollection()->action(QStringLiteral("open_gcode"))->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));

    action = KStandardAction::quit(qApp, SLOT(quit()), actionCollection());

    setupGUI(Default, "atelierui.rc");
}

void MainWindow::openFile()
{
    QUrl fileNameFromDialog = QFileDialog::getOpenFileUrl(this, i18n("Open GCode"),
                              QDir::homePath(), i18n("GCode (*.gco *.gcode)"));
    if (!fileNameFromDialog.isEmpty()) {
        fileName = fileNameFromDialog;
        ui->gcodeEditorWidget->loadFile(fileName);
        guiFactory()->addClient(ui->gcodeEditorWidget->gcodeView());
        ui->view3DWidget->drawModel(fileName.toString());
    }
}
