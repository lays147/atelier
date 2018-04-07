/* Atelier KDE Printer Host for 3D Printing
    Copyright (C) <2017>
    Author: Lays Rodrigues - laysrodriguessilva@gmail.com
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

#include "atcoreinstancewidget.h"
#include "ui_atcoreinstancewidget.h"
#include <QToolBar>
#include <AtCore/SerialLayer>
#include <AtCore/GCodeCommands>
#include <KLocalizedString>
#include "choosefiledialog.h"

AtCoreInstanceWidget::AtCoreInstanceWidget(QWidget *parent):
    QWidget(parent),
    m_toolBar(nullptr),
    m_printAction(nullptr),
    m_stopAction(nullptr)
{
    m_theme = palette().text().color().value() >= QColor(Qt::lightGray).value() ? QString("dark") : QString("light") ;
    ui = new Ui::AtCoreInstanceWidget;
    ui->setupUi(this);
    ui->printProgressWidget->setVisible(false);
    buildToolbar();
    buildConnectionToolbar();
    enableControls(false);
    updateProfileData();
    initConnectsToAtCore();
    m_toolBar->setHidden(true);
}

AtCoreInstanceWidget::~AtCoreInstanceWidget()
{
    m_core.closeConnection();
    delete ui;
}

void AtCoreInstanceWidget::buildToolbar()
{
    m_toolBar = new QToolBar();
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto lb = new QLabel;
    QIcon icon = QIcon::fromTheme("go-home", QIcon(QString(":/%1/home").arg(m_theme)));
    lb->setPixmap(icon.pixmap(fontMetrics().height()));
    m_toolBar->addWidget(lb);
    lb = new QLabel(i18n("Home:"));
    m_toolBar->addWidget(lb);

    auto homeAll = new QAction("All");
    connect(homeAll, &QAction::triggered, [this]{
       m_core.home();
    });
    m_toolBar->addAction(homeAll);

    for(auto homes : std::map<QString, int>{{"X", AtCore::X}, {"Y", AtCore::Y}, {"Z", AtCore::Z}}) {
        auto home = new QAction(homes.first);
        connect(home, &QAction::triggered, [this, homes] {
            m_core.home(homes.second);
        });
        m_toolBar->addAction(home);
    }

    m_toolBar->addSeparator();

    m_printAction = new QAction(QIcon::fromTheme("media-playback-start", style()->standardIcon(QStyle::SP_MediaPlay)),i18n("Print"));
    connect(m_printAction, &QAction::triggered, [ this ](){
        if(m_core.state() == AtCore::BUSY) {
            pausePrint();
            return;
        }
        if (m_core.state() == AtCore::IDLE){
            print();
        } else if (m_core.state() == AtCore::PAUSE) {
            m_core.resume();
        }
    });
    m_toolBar->addAction(m_printAction);

    m_stopAction = new QAction(QIcon::fromTheme("media-playback-stop", QIcon(QString(":/%1/stop").arg(m_theme))), i18n("Stop"));
    connect(m_stopAction, &QAction::triggered, this, &AtCoreInstanceWidget::stopPrint);
    connect(m_stopAction, &QAction::triggered, [this](){
        m_printAction->setText(i18n("Print"));
        m_printAction->setIcon(QIcon::fromTheme("media-playback-start", QIcon(QString(":/%1/start").arg(m_theme))));
    });
    m_toolBar->addAction(m_stopAction);

    auto disableMotorsAction = new QAction(style()->standardIcon(QStyle::SP_MediaStop),i18n("Disable Motors"));
    connect(disableMotorsAction, &QAction::triggered, this, &AtCoreInstanceWidget::disableMotors);
    m_toolBar->addAction(disableMotorsAction);

    ui->toolBarLayout->addWidget(m_toolBar);
    togglePrintButtons(m_files.size());
}

void AtCoreInstanceWidget::buildConnectionToolbar()
{
    m_connectToolBar = new QToolBar();
    m_comboPort = new QComboBox;
    m_comboPort->setEditable(true);
    QLabel *deviceLabel = new QLabel(i18n("Device"));
    QHBoxLayout *deviceLayout = new QHBoxLayout;
    deviceLayout->addWidget(deviceLabel);
    deviceLayout->addWidget(m_comboPort,100);

    m_comboProfile = new QComboBox;
    m_comboProfile->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    QHBoxLayout *profileLayout = new QHBoxLayout;
    QLabel *profileLabel = new QLabel(i18n("Profile"));
    profileLayout->addWidget(profileLabel);
    profileLayout->addWidget(m_comboProfile,100);

    QHBoxLayout *connectLayout = new QHBoxLayout;
    connectLayout->addLayout(deviceLayout,50);
    connectLayout->addLayout(profileLayout,50);

    m_connectWidget = new QWidget();
    m_connectWidget->setLayout(connectLayout);
    m_connectToolBar->addWidget(m_connectWidget);

    m_connectButton = new QPushButton(QIcon::fromTheme("network-connect", QIcon(QString(":/%1/connect").arg(m_theme))), i18n("Connect"));
    m_connectButton->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    connect(this, &AtCoreInstanceWidget::disableDisconnect, m_connectButton, &QPushButton::setDisabled);
    connect(m_connectButton, &QPushButton::clicked, this, &AtCoreInstanceWidget::connectButtonClicked);
    ui->toolBarLayout->addWidget(m_connectToolBar);
    ui->toolBarLayout->addWidget(m_connectButton);
}

void AtCoreInstanceWidget::connectButtonClicked()
{
    if(m_core.state() == AtCore::DISCONNECTED) {
        if (m_comboProfile->currentText().isEmpty()) {
            QMessageBox::information(this, i18n("No Profiles!"), i18n("Connecting Requires creating a profile for your printer. Create a profile in the next dialog then try again."));
            emit(requestProfileDialog());
            return;
        }

        if (m_comboPort->currentText().isEmpty()) {
            QMessageBox::critical(this, i18n("Error"), i18n("Please, connect a serial device to continue!"));
            return;
        }

        //Get profile data before connecting.
        QString profile = m_comboProfile->currentText();
        m_settings.beginGroup("GeneralSettings");
        m_settings.beginGroup(profile);
        QMap<QString, QVariant> data;
        data["bps"] = m_settings.value(QStringLiteral("bps"), QStringLiteral("115200"));
        data["bedTemp"] = m_settings.value(QStringLiteral("maximumTemperatureBed"), QStringLiteral("0"));
        data["hotendTemp"] = m_settings.value(QStringLiteral("maximumTemperatureExtruder"), QStringLiteral("0"));
        data["firmware"] = m_settings.value(QStringLiteral("firmware"),QStringLiteral("Auto-Detect"));
        data["postPause"] = m_settings.value(QStringLiteral("postPause"),QStringLiteral(""));
        data["name"] = profile;
        m_settings.endGroup();
        m_settings.endGroup();

        //then connect
        m_core.initSerial(m_comboPort->currentText(), data["bps"].toInt());
        if(m_core.state() == AtCore::CONNECTING){
            profileData = data;
            QString fw = profileData["firmware"].toString();
            if( fw != QString("Auto-Detect")){
                m_core.loadFirmwarePlugin(fw);
            }
            emit(connectionChanged(profileData["name"].toString()));
        }
    } else {
        m_core.closeConnection();
        emit(connectionChanged(i18n("Connect a Printer")));
    }
}

void AtCoreInstanceWidget::initConnectsToAtCore()
{
    m_core.setSerialTimerInterval(100);
    // Handle device changes
    connect(&m_core, &AtCore::portsChanged, this, &AtCoreInstanceWidget::updateSerialPort);

    // Handle AtCore status change
    connect(&m_core, &AtCore::stateChanged, this, &AtCoreInstanceWidget::handlePrinterStatusChanged);

    // If the number of extruders from the printer change, we need to update the radiobuttons on the widget
    connect(this, &AtCoreInstanceWidget::extruderCountChanged, ui->bedExtWidget, &BedExtruderWidget::setExtruderCount);

    // Bed and Extruder temperatures management
    connect(ui->bedExtWidget, &BedExtruderWidget::bedTemperatureChanged, &m_core, &AtCore::setBedTemp);
    connect(ui->bedExtWidget, &BedExtruderWidget::extTemperatureChanged, &m_core, &AtCore::setExtruderTemp);

    // Connect AtCore temperatures changes on Atelier Plot
    connect(&m_core.temperature(), &Temperature::bedTemperatureChanged, [ this ](const float& temp) {
        checkTemperature(0x00, 0, temp);
        ui->plotWidget->appendPoint(i18n("Actual Bed"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateBedTemp(temp);
    });
    connect(&m_core.temperature(), &Temperature::bedTargetTemperatureChanged, [ this ](const float& temp) {
        checkTemperature(0x01, 0, temp);
        ui->plotWidget->appendPoint(i18n("Target Bed"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateBedTargetTemp(temp);
    });
    connect(&m_core.temperature(), &Temperature::extruderTemperatureChanged, [ this ](const float& temp) {
        checkTemperature(0x02, 0, temp);
        ui->plotWidget->appendPoint(i18n("Actual Ext.1"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateExtTemp(temp);
    });
    connect(&m_core.temperature(), &Temperature::extruderTargetTemperatureChanged, [ this ](const float& temp) {
        checkTemperature(0x03, 0, temp);
        ui->plotWidget->appendPoint(i18n("Target Ext.1"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateExtTargetTemp(temp);
    });

    connect(ui->pushGCodeWidget, &PushGCodeWidget::push, [ this ](QString command) {
        ui->logWidget->addLog("Push " + command);
        m_core.pushCommand(command);
    });

    // Fan, Flow and Speed management
    connect(ui->ratesControlWidget, &RatesControlWidget::fanSpeedChanged, &m_core, &AtCore::setFanSpeed);
    connect(ui->ratesControlWidget, &RatesControlWidget::flowRateChanged, &m_core, &AtCore::setFlowRate);
    connect(ui->ratesControlWidget, &RatesControlWidget::printSpeedChanged, &m_core, &AtCore::setPrinterSpeed);
    connect(ui->axisViewWidget, &AxisControl::clicked, this, &AtCoreInstanceWidget::axisControlClicked);
}

void AtCoreInstanceWidget::printFile(const QUrl& fileName)
{
    if (!fileName.isEmpty() && (m_core.state() == AtCore::IDLE)) {
        m_core.print(fileName.toLocalFile());
    }
}

void AtCoreInstanceWidget::print(){
    switch (m_files.size()){
        case 0:
            QMessageBox::warning(this, i18n("Error"),
                                 i18n("There's no GCode File open. \n Please select a file and try again."),
                                 QMessageBox::Ok);
            break;
        case 1:
            printFile(m_files.at(0));
            break;
        default:
            auto dialog = new ChooseFileDialog(this, m_files);
            if(dialog->exec() == QDialog::Accepted){
                    printFile(dialog->choosenFile());
            }
    }
}
void AtCoreInstanceWidget::pausePrint()
{
    if(m_core.state() == AtCore::BUSY) {
        m_core.pause(profileData["postPause"].toString());
    } else if (m_core.state() == AtCore::PAUSE) {
        m_core.resume();
    }
}

void AtCoreInstanceWidget::stopPrint()
{
    m_core.stop();
}

void AtCoreInstanceWidget::disableMotors()
{
    m_core.disableMotors(0);
}

void AtCoreInstanceWidget::handlePrinterStatusChanged(AtCore::STATES newState)
{
    static QString stateString;
    switch (newState) {
        case AtCore::CONNECTING: {
            m_core.setSerialTimerInterval(0);
            m_connectButton->setText(i18n("Disconnect"));
            m_connectButton->setIcon(QIcon::fromTheme("network-disconnect", QIcon(QString(":/%1/disconnect").arg(m_theme))));
            m_connectToolBar->setHidden(true);
            m_toolBar->setHidden(false);
            stateString = i18n("Connecting...");
            connect(&m_core, &AtCore::receivedMessage, this, &AtCoreInstanceWidget::checkReceivedCommand);
            connect(m_core.serial(), &SerialLayer::pushedCommand, this, &AtCoreInstanceWidget::checkPushedCommands);
        } break;
        case AtCore::IDLE: {
            stateString = i18n("Connected to ") + m_core.serial()->portName();
            emit extruderCountChanged(m_core.extruderCount());
            ui->logWidget->addLog(i18n("Serial connected"));
            emit disableDisconnect(false);
            enableControls(true);
        } break;
        case AtCore::DISCONNECTED: {
            stateString = i18n("Not Connected");
            disconnect(&m_core, &AtCore::receivedMessage, this, &AtCoreInstanceWidget::checkReceivedCommand);
            disconnect(m_core.serial(), &SerialLayer::pushedCommand, this, &AtCoreInstanceWidget::checkPushedCommands);
            ui->logWidget->addLog(i18n("Serial disconnected"));
            m_core.setSerialTimerInterval(100);
            m_connectButton->setText(i18n("Connect"));
            m_connectButton->setIcon(QIcon::fromTheme("network-connect",QIcon(QString(":/%1/connect").arg(m_theme))));
            m_connectToolBar->setHidden(false);
            m_toolBar->setHidden(true);
            enableControls(false);
        } break;
        case AtCore::STARTPRINT: {
            stateString = i18n("Starting Print");
            ui->printProgressWidget->setVisible(true);
            connect(&m_core, &AtCore::printProgressChanged, ui->printProgressWidget, &PrintProgressWidget::updateProgressBar);
        } break;
        case AtCore::FINISHEDPRINT: {
            stateString = i18n("Finished Print");
            ui->printProgressWidget->setVisible(false);
            disconnect(&m_core, &AtCore::printProgressChanged, ui->printProgressWidget, &PrintProgressWidget::updateProgressBar);
            m_printAction->setText(i18n("Print"));
            m_printAction->setIcon(QIcon::fromTheme("media-playback-start", QIcon(QString(":/%1/start").arg(m_theme))));
        } break;
        case AtCore::BUSY: {
            stateString = i18n("Printing");
            emit disableDisconnect(true);
            m_printAction->setText(i18n("Pause"));
            m_printAction->setIcon(QIcon::fromTheme("media-playback-pause", QIcon(QString(":/%1/pause").arg(m_theme))));
        } break;
        case AtCore::PAUSE: {
            stateString = i18n("Paused");
            m_printAction->setText(i18n("Resume"));
            m_printAction->setIcon(QIcon::fromTheme("media-playback-start", QIcon(QString(":/%1/start").arg(m_theme))));
        } break;
        case AtCore::STOP: {
            stateString = i18n("Stoping Print");
        } break;
        case AtCore::ERRORSTATE: {
            stateString = i18n("Error");
        } break;
        default:
          qWarning("AtCore State not Recognized.");
          break;
    }
     ui->lblState->setText(stateString);
}

void AtCoreInstanceWidget::checkTemperature(uint sensorType, uint number, uint temp)
{
    static QString msg;
    switch (sensorType) {
    case 0x00: // bed
        msg = QString::fromLatin1("Bed Temperature ");
        break;

    case 0x01: // bed target
        msg = QString::fromLatin1("Bed Target Temperature ");
        break;

    case 0x02: // extruder
        msg = QString::fromLatin1("Extruder Temperature ");
        break;

    case 0x03: // extruder target
        msg = QString::fromLatin1("Extruder Target Temperature ");
        break;

    case 0x04: // enclosure
        msg = QString::fromLatin1("Enclosure Temperature ");
        break;

    case 0x05: // enclosure target
        msg = QString::fromLatin1("Enclosure Target Temperature ");
        break;
    }

    msg.append(QString::fromLatin1("[%1] : %2"));
    msg = msg.arg(QString::number(number))
          .arg(QString::number(temp));

    ui->logWidget->addRLog(msg);
}

void AtCoreInstanceWidget::checkReceivedCommand(const QByteArray &message)
{
    ui->logWidget->addRLog(QString::fromUtf8(message));
}

void AtCoreInstanceWidget::checkPushedCommands(const QByteArray &bmsg)
{
    QString msg = QString::fromUtf8(bmsg);
    QRegExp _newLine(QChar::fromLatin1('\n'));
    QRegExp _return(QChar::fromLatin1('\r'));
    msg.replace(_newLine, QStringLiteral("\\n"));
    msg.replace(_return, QStringLiteral("\\r"));
    ui->logWidget->addSLog(msg);
}

void AtCoreInstanceWidget::axisControlClicked(QChar axis, int value)
{
  m_core.setRelativePosition();
  m_core.pushCommand(GCode::toCommand(GCode::G1, QStringLiteral("%1%2").arg(axis, QString::number(value))));
  m_core.setAbsolutePosition();
}

void AtCoreInstanceWidget::enableControls(bool b)
{
    ui->mainTab->setEnabled(b);
    m_toolBar->setEnabled(b);
}

bool AtCoreInstanceWidget::connected()
{
    return (m_core.state() != AtCore::DISCONNECTED);
}

void AtCoreInstanceWidget::setOpenFiles(const QList<QUrl>& files)
{
    m_files = files;
    togglePrintButtons(m_files.size());
}

void AtCoreInstanceWidget::updateSerialPort(const QStringList &ports)
{
    m_comboPort->clear();
    m_comboPort->addItems(ports);
}

void AtCoreInstanceWidget::updateProfileData()
{
    m_settings.beginGroup("GeneralSettings");
    QStringList profiles = m_settings.childGroups();
    m_settings.endGroup();
    m_comboProfile->clear();
    m_comboProfile->addItems(profiles);
}

void AtCoreInstanceWidget::togglePrintButtons(bool shown)
{
    m_printAction->setVisible(shown);
    m_stopAction->setVisible(shown);
}
