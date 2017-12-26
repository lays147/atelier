/* Atelier KDE Printer Host for 3D Printing
    Copyright (C) <2017>
    Author: Lays Rodrigues - laysrodriguessilva@gmail.com

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
#include <AtCore/SerialLayer>
#include <AtCore/GCodeCommands>
#include <KLocalizedString>

AtCoreInstanceWidget::AtCoreInstanceWidget(QWidget *parent):
    QWidget(parent),
    m_connected(false)
{
    ui = new Ui::AtCoreInstanceWidget;
    ui->setupUi(this);
    ui->printPB->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    ui->pausePB->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    ui->stopPB->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    ui->disableMotorsPB->setIcon(style()->standardIcon(QStyle::SP_DockWidgetCloseButton));
    ui->printProgressWidget->setVisible(false);
    connect(ui->axisCK, &QCheckBox::clicked, [=](bool b){
        ui->axisViewWidget->setVisible(b);
    });
    connect(ui->controlsCK, &QCheckBox::clicked, [=](bool b){
        ui->bedExtWidget->setVisible(b);
    });
    connect(ui->graphCK, &QCheckBox::clicked, [=](bool b){
        ui->plotWidget->setVisible(b);
    });
    connect(ui->pausePB, &QPushButton::clicked, this, &AtCoreInstanceWidget::pausePrint);
    connect(ui->stopPB, &QPushButton::clicked, this, &AtCoreInstanceWidget::stopPrint);
    connect(ui->disableMotorsPB, &QPushButton::clicked, this, &AtCoreInstanceWidget::disableMotors);
    connect(ui->disconnectPB, &QPushButton::clicked, [=]{
        m_core.setState(AtCore::DISCONNECTED);
    });
    enableControls(false);
}

AtCoreInstanceWidget::~AtCoreInstanceWidget()
{
    delete ui;
}

void AtCoreInstanceWidget::startConnection(QString serialPort, QMap<QString, QVariant> profiles){
    m_core.initSerial(serialPort, profiles["bps"].toInt());
    initConnectsToAtCore();
}

void AtCoreInstanceWidget::initConnectsToAtCore()
{

    // Handle AtCore status change
    connect(&m_core, &AtCore::stateChanged, this, &AtCoreInstanceWidget::handlePrinterStatusChanged);

    // If the number of extruders from the printer change, we need to update the radiobuttons on the widget
    connect(this, &AtCoreInstanceWidget::extruderCountChanged, ui->bedExtWidget, &BedExtruderWidget::setExtruderCount);

    // Bed and Extruder temperatures management
    connect(ui->bedExtWidget, &BedExtruderWidget::bedTemperatureChanged, &m_core, &AtCore::setBedTemp);
    connect(ui->bedExtWidget, &BedExtruderWidget::extTemperatureChanged, &m_core, &AtCore::setExtruderTemp);

    // Connect AtCore temperatures changes on Atelier Plot
    connect(&m_core.temperature(), &Temperature::bedTemperatureChanged, [ = ](float temp) {
        checkTemperature(0x00, 0, temp);
        ui->plotWidget->appendPoint(i18n("Actual Bed"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateBedTemp(temp);
    });
    connect(&m_core.temperature(), &Temperature::bedTargetTemperatureChanged, [ = ](float temp) {
        checkTemperature(0x01, 0, temp);
        ui->plotWidget->appendPoint(i18n("Target Bed"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateBedTargetTemp(temp);
    });
    connect(&m_core.temperature(), &Temperature::extruderTemperatureChanged, [ = ](float temp) {
        checkTemperature(0x02, 0, temp);
        ui->plotWidget->appendPoint(i18n("Actual Ext.1"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateExtTemp(temp);
    });
    connect(&m_core.temperature(), &Temperature::extruderTargetTemperatureChanged, [ = ](float temp) {
        checkTemperature(0x03, 0, temp);
        ui->plotWidget->appendPoint(i18n("Target Ext.1"), temp);
        ui->plotWidget->update();
        ui->bedExtWidget->updateExtTargetTemp(temp);
    });

    connect(ui->pushGCodeWidget, &PushGCodeWidget::push, [ = ](QString command) {
        ui->logWidget->addLog("Push " + command);
        m_core.pushCommand(command);
    });

    // Fan, Flow and Speed management
    connect(ui->ratesControlWidget, &RatesControlWidget::fanSpeedChanged, &m_core, &AtCore::setFanSpeed);
    connect(ui->ratesControlWidget, &RatesControlWidget::flowRateChanged, &m_core, &AtCore::setFlowRate);
    connect(ui->ratesControlWidget, &RatesControlWidget::printSpeedChanged, &m_core, &AtCore::setPrinterSpeed);
    connect(ui->axisViewWidget, &AxisControl::clicked, this, &AtCoreInstanceWidget::axisControlClicked);

}

void AtCoreInstanceWidget::printFile(QUrl fileName)
{
    if (!fileName.isEmpty() && (m_core.state() == AtCore::IDLE)) {
        QString f = fileName.toLocalFile();
        m_core.print(f);
    }
}

void AtCoreInstanceWidget::pausePrint()
{
    m_core.pause(QString());
}

void AtCoreInstanceWidget::stopPrint()
{
    m_core.stop();
}

void AtCoreInstanceWidget::disableMotors()
{
    m_core.pushCommand(GCode::toCommand(GCode::M18));
}

void AtCoreInstanceWidget::handlePrinterStatusChanged(AtCore::STATES newState)
{
    QString stateString;
    switch (newState) {
        case AtCore::CONNECTING: {
            stateString = i18n("Connecting...");
            connect(&m_core, &AtCore::receivedMessage, this, &AtCoreInstanceWidget::checkReceivedCommand);
            connect(m_core.serial(), &SerialLayer::pushedCommand, this, &AtCoreInstanceWidget::checkPushedCommands);
        } break;
        case AtCore::IDLE: {
            stateString = i18n("Connected to ") + m_core.serial()->portName();
            emit extruderCountChanged(m_core.extruderCount());
            ui->logWidget->addLog(i18n("Serial connected"));
            ui->disconnectPB->setEnabled(true);
            setConnectedStatus(true);
        } break;
        case AtCore::DISCONNECTED: {
            stateString = i18n("Not Connected");
            disconnect(&m_core, &AtCore::receivedMessage, this, &AtCoreInstanceWidget::checkReceivedCommand);
            disconnect(m_core.serial(), &SerialLayer::pushedCommand, this, &AtCoreInstanceWidget::checkPushedCommands);
            ui->logWidget->addLog(i18n("Serial disconnected"));
            setConnectedStatus(false);
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
        } break;
        case AtCore::BUSY: {
            stateString = i18n("Printing");
            ui->disconnectPB->setDisabled(true);
        } break;
        case AtCore::PAUSE: {
            stateString = i18n("Paused");
        } break;
        case AtCore::STOP: {
            stateString = i18n("Stoping Print");
        } break;
        case AtCore::ERRORSTATE: {
            stateString = i18n("Error");
        } break;
    }
     ui->lblState->setText(stateString);
}

void AtCoreInstanceWidget::checkTemperature(uint sensorType, uint number, uint temp)
{
    QString msg;
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

void AtCoreInstanceWidget::checkPushedCommands(QByteArray bmsg)
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
    ui->printPB->setEnabled(b);
    ui->pausePB->setEnabled(b);
    ui->stopPB->setEnabled(b);
    ui->disconnectPB->setEnabled(b);
    ui->disableMotorsPB->setEnabled(b);
}

bool AtCoreInstanceWidget::connected()
{
    return m_connected;
}

void AtCoreInstanceWidget::setConnectedStatus(bool b)
{
    m_connected = b;
    enableControls(b);
}

