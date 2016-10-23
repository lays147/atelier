#ifndef LEFTWIDGET_H
#define LEFTWIDGET_H
#include <QWidget>
#include <QPushButton>
#include "connectsettings.h"

class LeftWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LeftWidget(QWidget *parent = 0);

private:
    QPushButton *connectSettingsPB;
    QPushButton *gcodeEditorPB;
    QPushButton *maintanceMenuPB;
    void initButtons();

signals:
    void loadConnectContainer();
    void loadGCodeContainer();
    void loadMaintanceContainer();
    void hideContainers();
};

#endif // LEFTWIDGET_H