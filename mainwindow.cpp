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

#include <QProcess>
#include <QDebug>
#include <QFile>
#include <QSocketNotifier>
#include <QSettings>
#include <QKeyEvent>
#include <QTimer>
#include <QThread>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <QLocale>
#include <QMessageBox>
#include <QInputDialog>

#define NODECOUNT               10
#define CONNPOINTCOUNT          3
#define TELEMETRY_FIFO_OUT      "/tmp/telemetry_fifo_out"
#define TELEMETRY_FIFO_IN       "/tmp/telemetry_fifo_in"
#define GPIO_INPUT_PATH         "/dev/input/by-path/platform-gpio_keys-event"
#define BACKLIGHT_PATH          "/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0045/backlight/1-0045/brightness"
#define BUZZER_PATH             "/sys/class/leds/usr_buzzer/brightness"
#define USR_LED_GREEN_PATH      "/sys/class/leds/usr_led0/brightness"
#define STA_LED_RED_PATH        "/sys/class/leds/usr_led1/brightness"
#define STA_LED_GREEN_PATH      "/sys/class/leds/usr_led2/brightness"
#define SETTINGS_INI_FILE       "/opt/tunnel/sinm.ini"
#define USER_PREF_INI_FILE      "/opt/tunnel/userpreferences.ini"
#define UI_ELEMENTS_INI_FILE    "/opt/tunnel/userinterface.ini"
#define WG_CONFIGURATION_FILE   "/etc/systemd/network/wg0.netdev"
#define WG_CONFIGURATION_FILE_S "/opt/tunnel/network-configurations/wg0.netdev"
#define IMAGE_TRANSFERRED_FILE  "/tmp/ftp/incoming/image.png"
#define CAMERA_PIC_FILE         "/tmp/image.png"
#define BLACK_OUT_TIME          300000
#define MESSAGE_RECEIVE_FIFO    "/tmp/message_fifo_out"
#define INDICATE_ONLY           0
#define LOG_ONLY                1
#define LOG_AND_INDICATE        2
#define TX_KEY_PRESENTAGE       "/tmp/tx-key-presentage"
#define RX_KEY_PRESENTAGE       "/tmp/rx-key-presentage"
#define SUBSTITUTE_CHAR_CODE    24
#define FIFO_TIMEOUT            1
#define FIFO_REPLY_RECEIVED     0
#define LED_PULSE_MS            500

/* Global fifoIn file handle */
QFile fifoIn(TELEMETRY_FIFO_OUT);
QFile msgFifoIn(MESSAGE_RECEIVE_FIFO);
QFile txKeyFifoIn(TX_KEY_PRESENTAGE);
QFile rxKeyFifoIn(RX_KEY_PRESENTAGE);

MainWindow::MainWindow(int argumentValue, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->settingsFrame->setVisible(0);
    ui->inComingFrame->setVisible(0);
    ui->route1Selected->setVisible(0);
    ui->route2Selected->setVisible(0);
    ui->route3Selected->setVisible(0);
    ui->contact1Selected->setVisible(0);
    ui->contact2Selected->setVisible(0);
    ui->contact3Selected->setVisible(0);
    ui->contact4Selected->setVisible(0);
    ui->contact5Selected->setVisible(0);
    ui->contact6Selected->setVisible(0);
    ui->contact7Selected->setVisible(0);
    ui->contact8Selected->setVisible(0);
    ui->contact9Selected->setVisible(0);
    ui->contact10Selected->setVisible(0);
    ui->countLabel->setVisible(0);
    ui->imageFrame->setVisible(0);
    ui->pinButton_pwr->setVisible(false);
    ui->pwrButton->setVisible(false);

    /* Load user provided logo if it's present */
    QString logoGraphFile("/root/logo.png");
    QFile fileCheck(logoGraphFile);
    if ( fileCheck.exists() )
        ui->logoLabel->setPixmap(QPixmap(logoGraphFile));

    /* Set version string */
    ui->versionLabel->setText("v0.32 (10)");

    if ( argumentValue == VAULT_MODE ) {
        m_startMode = VAULT_MODE;
        loadSettings();
        loadUserPreferences();
        loadUserInterfacePreferences();
        writeBackLight("254");
        backLightOn = true;
        ui->codeFrame->setVisible(true);
        ui->logoLabel->setVisible(true);
        ui->settingsFrame->setVisible(false);
        ui->codeValue->setText("");
        ui->pinEntryTitle->setText(uiElement.pinEntryTitleVault);
        // After PIN -> program should exit
    } else {

        m_startMode = UI_MODE;

        /* Load settings */
        loadSettings();
        loadUserPreferences();
        loadUserInterfacePreferences();

        /* Watcher */
        QFileSystemWatcher *watcher = new QFileSystemWatcher();
        watcher->addPath(TELEMETRY_FIFO_OUT);
        QObject::connect(watcher, SIGNAL(fileChanged(QString)), this, SLOT(fifoChanged(QString)));

        fifoWrite("127.0.0.1,daemon_ping");

        /* Incoming telemetry FIFO */
        if(!fifoIn.open(QIODevice::ReadOnly | QIODevice::Unbuffered | QIODevice::Text)) {
            qDebug() << "TELEMETRY FIFO ERROR:" << fifoIn.errorString();
        }

        /* Message fifo & watcher*/
        fifoWrite(nodes.myNodeIp + ",message,init");
        QFileSystemWatcher *msgWatcher = new QFileSystemWatcher();
        msgWatcher->addPath(MESSAGE_RECEIVE_FIFO);
        QObject::connect(msgWatcher, SIGNAL(fileChanged(QString)), this, SLOT(msgFifoChanged(QString)));

        /* Incoming message FIFO */
        if(!msgFifoIn.open(QIODevice::ReadOnly | QIODevice::Unbuffered | QIODevice::Text)) {
            qDebug() << "MSG FIFO ERROR:" << msgFifoIn.errorString();
        }

        /* Initial volume(s) */
        ui->volumeSlider->setValue(uPref.volumeValue.toInt());
        if ( uPref.volumeValue == "" ) {
            uPref.volumeValue = "50";
            ui->volumeSlider->setValue(uPref.volumeValue.toInt());
            saveUserPreferences();
        }

        /* GPIO Buttons, TODO: m_fd close */
        QByteArray device = QByteArrayLiteral(GPIO_INPUT_PATH);
        m_fd = open(device.constData(), O_RDONLY);
        if (m_fd >= 0) {
            m_notify = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
            connect(m_notify, SIGNAL(activated(int)), this, SLOT(readGpioButtons()));
        } else {
            qErrnoWarning(errno, "Cannot open input device %s", device.constData());
        }

        /* Countdown timer */
        countdownTimer = new QTimer(this);
        connect(countdownTimer, SIGNAL(timeout()), this, SLOT(finalCountdown()) );

        /* Backlight timer */
        screenBlanktimer = new QTimer(this);
        connect(screenBlanktimer, SIGNAL(timeout()), this, SLOT(rampDown()) );
        rampUp();
        backLightOn = true;
        staGreenLedOff();
        staRedLedOff();
        usrLedOff();

        /* Enable route buttons */
        ui->route1Button->setEnabled(true);
        ui->route2Button->setEnabled(true);
        ui->route3Button->setEnabled(false);

        /* Set connection state */
        g_connectState=false;

        /* Key presentage watchers TODO: improve this */
        QFileSystemWatcher *txKeyWatcher = new QFileSystemWatcher();
        txKeyWatcher->addPath(TX_KEY_PRESENTAGE);
        QObject::connect(txKeyWatcher, SIGNAL(fileChanged(QString)), this, SLOT(txKeyPresentageChanged()));

        /* TODO: improve this */
        QFile file(TX_KEY_PRESENTAGE);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            stream << "wait" << Qt::endl;
        }

        /* Incoming txKey FIFO */
        if(!txKeyFifoIn.open(QIODevice::ReadOnly | QIODevice::Unbuffered | QIODevice::Text)) {
            qDebug() << "txKey FIFO error:" << txKeyFifoIn.errorString();
        }

        QFileSystemWatcher *rxKeyWatcher = new QFileSystemWatcher();
        rxKeyWatcher->addPath(RX_KEY_PRESENTAGE);
        QObject::connect(rxKeyWatcher, SIGNAL(fileChanged(QString)), this, SLOT(rxKeyPresentageChanged()));

        /* TODO: Improve this */
        QFile fileRx(RX_KEY_PRESENTAGE);
        if (fileRx.open(QIODevice::ReadWrite)) {
            QTextStream stream(&fileRx);
            stream << "wait" << Qt::endl;
        }

        /* Incoming rxKey FIFO */
        if(!rxKeyFifoIn.open(QIODevice::ReadOnly | QIODevice::Unbuffered | QIODevice::Text)) {
            qDebug() << "rxKey FIFO error:" << rxKeyFifoIn.errorString();
        }

        /* Network latency timer */
        envTimer = new QTimer();
        connect(envTimer, SIGNAL(timeout()), this, SLOT(networkLatency()) );
        envTimer->start(5000);

        /* Disable "Go Secure" */
        ui->greenButton->setEnabled(false);

        /* Set filesystem watcher for Camera (experimental) */
        QFileSystemWatcher *imageWatcher = new QFileSystemWatcher();
        imageWatcher->addPath("/tmp/ftp/incoming/image.png");
        QObject::connect(imageWatcher, SIGNAL(fileChanged(QString)), this, SLOT(incomingImageChangeDetected()));

    }
}

void MainWindow::txKeyPresentageChanged()
{
    QString line = txKeyFifoIn.readLine();
    line.replace(QString(" "), QString(""));
    line.replace(QString("%"), QString(""));
    txKeyRemaining = 100 - line.toDouble();
    txKeyRemainingString = QString::number( txKeyRemaining ,'f', 2);
    if ( txKeyRemainingString != "100.00" )
        ui->keyPrecentage->setText( txKeyRemainingString + " % " + rxKeyRemainingString + " %");
}

void MainWindow::rxKeyPresentageChanged()
{
    QString line = rxKeyFifoIn.readLine();
    line.replace(QString(" "), QString(""));
    line.replace(QString("%"), QString(""));
    rxKeyRemaining = 100 - line.toDouble();
    rxKeyRemainingString = QString::number( rxKeyRemaining, 'f', 2 );
    if ( rxKeyRemainingString != "100.00" )
        ui->keyPrecentage->setText( txKeyRemainingString + " % " + rxKeyRemainingString + " %" );
}
void MainWindow::resetF1State() {
    m_f1WasDown = false;
}
void MainWindow::readGpioButtons()
{
    struct input_event in_ev = { 0 };
    /*  Loop read data  */
    if (sizeof(struct input_event) != read(m_fd, &in_ev, sizeof(struct input_event))) {
        perror("read error");
        exit(-1);
    }

    switch (in_ev.type) {
        case EV_KEY:
        {
            /* F1 key: OTP status display */
            if (KEY_A == in_ev.code && in_ev.value == 1 ) {
                m_f1WasDown = true;
                QTimer::singleShot(2000, this, SLOT( resetF1State()) );
                ui->lineEdit->clearFocus();
                reloadKeyUsage();
                ui->contact1Button->setText( m_keyStatusString[0] );
                ui->contact2Button->setText( m_keyStatusString[1] );
                ui->contact3Button->setText( m_keyStatusString[2] );
                ui->contact4Button->setText( m_keyStatusString[3] );
                ui->contact5Button->setText( m_keyStatusString[4] );
                ui->contact6Button->setText( m_keyStatusString[5] );
                ui->contact7Button->setText( m_keyStatusString[6] );
                ui->contact8Button->setText( m_keyStatusString[7] );
                ui->contact9Button->setText( m_keyStatusString[8] );
                ui->contact10Button->setText( m_keyStatusString[9] );

                ui->contact1Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact2Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact3Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact4Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact5Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact6Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact7Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact8Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact9Button->setStyleSheet(m_otpStatusHighlightStyle);
                ui->contact10Button->setStyleSheet(m_otpStatusHighlightStyle);
                break;
            }
            if (KEY_A == in_ev.code && in_ev.value == 0 ) {
                ui->lineEdit->setFocus();
                ui->contact1Button->setText( nodes.node_name[0] );
                ui->contact2Button->setText( nodes.node_name[1] );
                ui->contact3Button->setText( nodes.node_name[2] );
                ui->contact4Button->setText( nodes.node_name[3] );
                ui->contact5Button->setText( nodes.node_name[4] );
                ui->contact6Button->setText( nodes.node_name[5] );
                ui->contact7Button->setText( nodes.node_name[6] );
                ui->contact8Button->setText( nodes.node_name[7] );
                ui->contact9Button->setText( nodes.node_name[8] );
                ui->contact10Button->setText( nodes.node_name[9] );
                ui->contact1Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact2Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact3Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact4Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact5Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact6Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact7Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact8Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact9Button->setStyleSheet(m_otpStausNormalStyle);
                ui->contact10Button->setStyleSheet(m_otpStausNormalStyle);
                break;
            }
            /* F2 key: beep mute */
            if ( KEY_S == in_ev.code && in_ev.value == 1 ) {
                ui->lineEdit->clearFocus();
                if ( nodes.beepActive == "1") {
                    updateCallStatusIndicator("Beep muted", "green", "transparent",INDICATE_ONLY );
                    saveUserPreferencesBeep("0");
                } else {
                    updateCallStatusIndicator("Beep unmuted", "green", "transparent",INDICATE_ONLY );
                    saveUserPreferencesBeep("1");
                    beepBuzzer(10);
                }
                if( KEY_S == in_ev.code && in_ev.value == 0 ) {
                    ui->lineEdit->setFocus();
                }
            }
            /* F3 key: 'nuke.sh' */
            if (KEY_D == in_ev.code && in_ev.value == 1 ) {
                ui->lineEdit->clearFocus();
                on_eraseButton_clicked();
                /* Override beep */
                nodes.beepActive = "1";
                ui->countLabel->setVisible(true);
                m_finalCountdownValue = 10;
                ui->countLabel->setText(QString::number(m_finalCountdownValue));
                countdownTimer->start(500);
            }
            if (KEY_D == in_ev.code && in_ev.value == 0 ) {
                ui->lineEdit->setFocus();
                /* Read beep preference */
                QSettings settings(USER_PREF_INI_FILE,QSettings::IniFormat);
                nodes.beepActive = settings.value("beep").toString();
                m_finalCountdownValue = 10;
                countdownTimer->stop();
                ui->countLabel->setVisible(false);
                beepBuzzerOff();
            }
            /* Green button: screen lock/unlock  */
            if (KEY_F == in_ev.code && in_ev.value == 1 && backLightOn == false ) {
                ui->lineEdit->clearFocus();
                screenBlanktimer->start(BLACK_OUT_TIME);
                rampUp();
                ui->lineEdit->setFocus();
                break;
            }
            if (KEY_F == in_ev.code && in_ev.value == 1 && backLightOn == true ) {
                ui->lineEdit->clearFocus();
                rampDown();
                screenBlanktimer->stop();
                break;
            }

            /* Power button dialog */
            if (142 == in_ev.code && in_ev.value == 1 ) {
                if ( backLightOn == false ) {
                    rampUp();
                }
                QMessageBox msgBox;
                msgBox.setWindowTitle("Power button");
                msgBox.setText("Do you want to power off?");
                msgBox.setStandardButtons(QMessageBox::Yes);
                msgBox.addButton(QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);
                msgBox.setStyleSheet( m_powerButtonDialogStyle );
                if(msgBox.exec() == QMessageBox::Yes){
                  on_pwrButton_clicked();
                } else {
                }
                break;
            }
        }
    }
}

void MainWindow::usrLedOn()
{
    QFile file(USR_LED_GREEN_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << 1 << Qt::endl;
    }
    file.close();
}
/*  STA led is connection latency indicator
    USR led is unused */
void MainWindow::usrLedOff()
{
    QFile file(USR_LED_GREEN_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << 0 << Qt::endl;
    }
    file.close();
}
void MainWindow::staRedLedOn()
{
    QFile file(STA_LED_RED_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << 1 << Qt::endl;
    }
    file.close();
    QTimer::singleShot(LED_PULSE_MS, this, SLOT( staRedLedOff() ) );
}
void MainWindow::staRedLedOff()
{
    QFile file(STA_LED_RED_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << 0 << Qt::endl;
    }
    file.close();
}
void MainWindow::staGreenLedOn()
{
    QFile file(STA_LED_GREEN_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << 1 << Qt::endl;
    }
    file.close();
    QTimer::singleShot(LED_PULSE_MS, this, SLOT( staGreenLedOff() ) );
}
void MainWindow::staGreenLedOff()
{
    QFile file(STA_LED_GREEN_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << 0 << Qt::endl;
    }
    file.close();
}

void MainWindow::beepBuzzerOff()
{
    if ( nodes.beepActive == "1" ) {
        QFile file(BUZZER_PATH);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            stream << 0 << Qt::endl;
        }
        file.close();
    }
}

void MainWindow::beepBuzzer(int lenght)
{
   if ( nodes.beepActive == "1" ) {
        QFile file(BUZZER_PATH);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            stream << 1 << Qt::endl;
        }
        file.close();
        QTimer::singleShot(lenght, this, SLOT(beepBuzzerOff()));
    }
}

void MainWindow::rampUp()
{
    backLightOn=true;
    for (int x=0; x<255; x++) {
        writeBackLight(QString::number(x));
        /* TODO: Improve this */
        QCoreApplication::processEvents();
    }
    if ( !screenBlanktimer->isActive()) {
        screenBlanktimer->start(BLACK_OUT_TIME);
    }
}
void MainWindow::rampDown()
{
    screenBlanktimer->stop();
    backLightOn=false;
    for (int x=255; x>0; x--) {
        writeBackLight(QString::number(x));
        QCoreApplication::processEvents();
    }
    writeBackLight("0");
    screenBlanktimer->stop();
    ui->pinEntryTitle->setText(uiElement.pinEntryTitleAccessPin);
    ui->codeFrame->setVisible(true);
    ui->logoLabel->setVisible(true);
    ui->settingsFrame->setVisible(false);
    ui->codeValue->setText("");
    ui->imageFramePictureLabel->clear();
    ui->imageFrame->setVisible(0);
}

void MainWindow::writeBackLight(QString value)
{
    QFile file(BACKLIGHT_PATH);
    if (file.open(QIODevice::ReadWrite)) {
        QTextStream stream(&file);
        stream << value << Qt::endl;
    }
    file.close();
}

void MainWindow::fifoWrite(QString message)
{
    /* Dummy read */
    QTextStream in(&fifoIn);
    QString line = in.readAll();
    g_fifoReply = "";
    QFile file(TELEMETRY_FIFO_IN);
    if(!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        qDebug() << "FIFO Write file open error" << file.errorString();
    }
    QTextStream out(&file);
    out << message << Qt::endl;
    file.close();
}

/* Telemetry FIFO */
void MainWindow::fifoChanged(const QString & path)
{
  int nodeNumber;
  QTextStream in(&fifoIn);
  QString line = in.readAll();

  if(line.compare("telemetryclient_is_alive") == 0) {
      g_fifoReply ="client_alive";
  } else {

    /*  Main logic for telemetry fifo handling
     *  IP:     token[0]
     *  Status: token[1] */
    QStringList token = line.split(',');
    g_fifoReply = token[1];

      if( token[1].compare("available") == 0 )
      {
          for (int x=0; x<NODECOUNT; x++) {
            if(token[0].compare(nodes.node_ip[x]) == 0) {
                nodeNumber=x;
            }
          }
          if( nodeNumber == 0 ) {
                ui->contact1Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact1Selected->setVisible(1);
          }
          if( nodeNumber == 1 ) {
                ui->contact2Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact2Selected->setVisible(1);
          }
          if( nodeNumber == 2 ) {
                ui->contact3Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact3Selected->setVisible(1);
          }
          if( nodeNumber == 3 ) {
                ui->contact4Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact4Selected->setVisible(1);
          }
          if( nodeNumber == 4 ) {
                ui->contact5Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact5Selected->setVisible(1);
          }
          if( nodeNumber == 5 ) {
                ui->contact6Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact6Selected->setVisible(1);
          }
          if( nodeNumber == 6 ) {
                ui->contact7Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact7Selected->setVisible(1);
          }
          if( nodeNumber == 7 ) {
                ui->contact8Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact8Selected->setVisible(1);
          }
          if( nodeNumber == 8 ) {
                ui->contact9Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact9Selected->setVisible(1);
          }
          if( nodeNumber == 9 ) {
                ui->contact10Selected->setStyleSheet("background-color: lightgreen;");
                connectAsClient(nodes.node_ip[nodeNumber], nodes.node_id[nodeNumber]);
                ui->contact10Selected->setVisible(1);
          }

      }
      if( token[1].compare("offline") == 0 )
      {
          for (int x=0; x<NODECOUNT; x++) {
            if(token[0].compare(nodes.node_ip[x]) == 0) {
                nodeNumber=x;
            }
          }
          if( nodeNumber == 0 ) {
            ui->contact1Selected->setVisible(1);
            ui->contact1Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 1 ) {
            ui->contact2Selected->setVisible(1);
            ui->contact2Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 2 ) {
            ui->contact3Selected->setVisible(1);
            ui->contact3Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 3 ) {
            ui->contact4Selected->setVisible(1);
            ui->contact4Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 4 ) {
            ui->contact5Selected->setVisible(1);
            ui->contact5Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 5 ) {
            ui->contact6Selected->setVisible(1);
            ui->contact6Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 6 ) {
            ui->contact7Selected->setVisible(1);
            ui->contact7Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 7 ) {
            ui->contact8Selected->setVisible(1);
            ui->contact8Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 8 ) {
            ui->contact9Selected->setVisible(1);
            ui->contact9Selected->setStyleSheet("background-color: red;");
          }
          if( nodeNumber == 9 ) {
            ui->contact10Selected->setVisible(1);
            ui->contact10Selected->setStyleSheet("background-color: red;");
          }
      }

      if( token[1].compare("terminate_ready") == 0 )
      {
          updateCallStatusIndicator("remote terminated", "green", "transparent",INDICATE_ONLY );
          ui->contact1Selected->setVisible(0);
          ui->contact2Selected->setVisible(0);
          ui->contact3Selected->setVisible(0);
          ui->contact4Selected->setVisible(0);
          ui->contact5Selected->setVisible(0);
          ui->contact6Selected->setVisible(0);
          ui->contact7Selected->setVisible(0);
          ui->contact8Selected->setVisible(0);
          ui->contact9Selected->setVisible(0);
          ui->contact10Selected->setVisible(0);
          setContactButtons(true);
          ui->answerButton->setEnabled(true);
          ui->answerButton->setVisible(true);
          ui->keyPrecentage->setText("");
          ui->redButton->setStyleSheet(s_terminateButtonStyle_normal);
          ui->greenButton->setStyleSheet(s_goSecureButtonStyle_normal);
          ui->greenButton->setEnabled(false);
          on_eraseButton_clicked();
      }

      if( token[1].compare("busy") == 0 )
      {
          updateCallStatusIndicator("Remote busy", "green", "transparent",LOG_ONLY );
      }
      if( token[1].compare("offline") == 0 )
      {          
          updateCallStatusIndicator("Remote offline", "green", "transparent",LOG_ONLY );

          /* Disabled */
          if ( 0 && g_connectState ) {
              /* Tear connection down without remote involvement. */
              updateCallStatusIndicator("Auto disconnect", "green", "transparent",LOG_ONLY );
              ui->contact1Selected->setVisible(0);
              ui->contact2Selected->setVisible(0);
              ui->contact3Selected->setVisible(0);
              ui->contact4Selected->setVisible(0);
              ui->contact5Selected->setVisible(0);
              ui->contact6Selected->setVisible(0);
              QTimer::singleShot(3 * 1000, this, SLOT(tearDownLocal()));
              removeLocalFile("/tmp/CLIENT_CALL_ACTIVE");
              ui->keyPrecentage->setText("");
              ui->redButton->setStyleSheet(s_terminateButtonStyle_normal);
              ui->greenButton->setStyleSheet(s_goSecureButtonStyle_normal);
              ui->greenButton->setEnabled(false);
              ui->inComingFrame->setVisible(false);
              g_connectState = false;
              g_connectedNodeId = "";
              g_connectedNodeIp = "";
              g_remoteOtpPeerIp = "";
          }

      }
  }
    /* TODO: Other status codes
        busy
        terminate_ready
        prepare_ready
        ring_ready
    */
}

/* msg fifo is a way to talk to UI
 * IP:          token[0]
 * msg payload: token[1]
 */
int MainWindow::msgFifoChanged(const QString & path)
{
    QTextStream in(&msgFifoIn);
    QString line = in.readAll();
    QStringList token = line.split(',');
    /* Logic for UI ring indication. Note that ring tone ('sound') is played by telemetry logic */
    if ( token[1] == "ring" )
    {
        if (  backLightOn == false ) {
            rampUp();
        }
        beepBuzzer(500);
        ui->inComingFrame->setVisible(true);
        ui->incomingTitleFrame->setText("Incoming audio");
        ui->answerButton->setText("Accept");
        ui->denyButton->setText("Deny");
        token[1]="";
    }

    /* TODO: Is this obsolete ? */
    if ( token[1] == "answered_ok") {
        updateCallStatusIndicator("Accepted ("+token[0]+")", "lightgreen", "transparent",LOG_AND_INDICATE);
        ui->incomingTitleFrame->setText("Voice active");
        ui->inComingFrame->setVisible(true);
        token[1]="";
    }

    if ( token[1] == "remote_hangup") {
        ui->inComingFrame->setVisible(false);
        ui->messagesView->append("[SYSTEM]: Remote hangup (" + token[0] + ")");
        updateCallStatusIndicator("Remote hangup", "green", "transparent",LOG_AND_INDICATE);
        token[1]="";
        ui->redButton->click();
        on_eraseButton_clicked();
    }
    if ( token[1] == "answer_success") {
        updateCallStatusIndicator("Audio active", "lightgreen", "transparent",INDICATE_ONLY);
        token[1]="";
    }
    /* Remote (who connected us) presses 'terminate', we should do the same.
       TODO: This has some logic errors.
     */
    if ( token[1] == "initiator_disconnect") {
        qDebug() << "initiator_disconnect()";
        ui->inComingFrame->setVisible(false);
        // ui->redButton->click(); // WRONG!! We cannot 'disconnect' as Client if we are 'Server'
        if ( g_connectState )
            on_denyButton_clicked();
        token[1]="";
        on_eraseButton_clicked();
    }

    /* client_connected,[client_id];[client_ip];[client_name] */
    if ( token[1].contains( "client_connected",Qt::CaseInsensitive ) ) {
        QStringList remoteParameters = token[1].split(';');
        g_connectedNodeId = remoteParameters[1];
        g_connectedNodeIp = remoteParameters[2];
        g_connectState = true;
        updateCallStatusIndicator(remoteParameters[3] + " connected" , "lightgreen","transparent",LOG_AND_INDICATE);
        token[1]="";
        setIndicatorForIncomingConnection(remoteParameters[2]);
        if (  backLightOn == false ) {
            rampUp();
        }
        /* Inbound OTP */
        g_remoteOtpPeerIp = "10.10.0.2";
        /* Inbound connection: highlight terminate and disable Go Secure */
        ui->redButton->setStyleSheet(s_terminateButtonStyle_highlight);
        ui->greenButton->setStyleSheet(s_goSecureButtonStyle_normal);
        ui->greenButton->setEnabled(false);
        beepBuzzer(10);
    }

    /* Commcheck TODO: Make alive ping out of this */
    if ( token[1] == "Ping") {
        if ( g_connectState ) {
            QString fifo_command = g_remoteOtpPeerIp + ",message,Commcheck from: " + nodes.myNodeName;
            fifoWrite(fifo_command);
            return 0;
        }
    }
    /* Normal message to be shown */
    if (token[1] != "" )
    {        
        token[1].replace( QChar(SUBSTITUTE_CHAR_CODE), "," );
        ui->messagesView->append(token[1]);
        beepBuzzer(10);
    }
    return 0;
}

/* Alter contact button state */
void MainWindow::setContactButtons(bool state)
{
    ui->contact1Button->setDisabled(!state);
    ui->contact2Button->setDisabled(!state);
    ui->contact3Button->setDisabled(!state);
    ui->contact4Button->setDisabled(!state);
    ui->contact5Button->setDisabled(!state);
    ui->contact6Button->setDisabled(!state);
    ui->contact7Button->setDisabled(!state);
    ui->contact8Button->setDisabled(!state);
    ui->contact9Button->setDisabled(!state);
    ui->contact10Button->setDisabled(!state);

    /* If button's are enabled, disable 'own' button */
    int myOwnNodeId;
    for (int x=0; x < NODECOUNT; x++ ) {
        if ( nodes.node_name[x].compare( nodes.myNodeName ) == 0 )
            myOwnNodeId = x;

        if ( myOwnNodeId == x ) {
         if ( x == 0 )
            ui->contact1Button->setDisabled(true);
         if ( x == 1 )
             ui->contact2Button->setDisabled(true);
         if ( x == 2 )
             ui->contact3Button->setDisabled(true);
         if ( x == 3 )
             ui->contact4Button->setDisabled(true);
         if ( x == 4 )
             ui->contact5Button->setDisabled(true);
         if ( x == 5 )
             ui->contact6Button->setDisabled(true);
         if ( x == 6 )
             ui->contact7Button->setDisabled(true);
         if ( x == 7 )
             ui->contact8Button->setDisabled(true);
         if ( x == 8 )
             ui->contact9Button->setDisabled(true);
         if ( x == 9 )
             ui->contact10Button->setDisabled(true);
        }
    }
}

void MainWindow::setIndicatorForIncomingConnection(QString peerIp)
{
        ui->contact1Selected->setVisible(0);
        ui->contact2Selected->setVisible(0);
        ui->contact3Selected->setVisible(0);
        ui->contact4Selected->setVisible(0);
        ui->contact5Selected->setVisible(0);
        ui->contact6Selected->setVisible(0);
        ui->contact7Selected->setVisible(0);
        ui->contact8Selected->setVisible(0);
        ui->contact9Selected->setVisible(0);
        ui->contact10Selected->setVisible(0);
        /* Disable contact buttons when incoming connection is alive */
        setContactButtons(false);
        /* Light up 'green' for contact, who made connection */
        int nodeNumber;
        for (int x=0; x<NODECOUNT; x++) {
          if(peerIp.compare(nodes.node_ip[x]) == 0) {
              nodeNumber=x;
          }
        }
        if( nodeNumber == 0 ) {
            ui->contact1Selected->setVisible(1);
        }
        if( nodeNumber == 1 ) {
            ui->contact2Selected->setVisible(1);
        }
        if( nodeNumber == 2 ) {
            ui->contact3Selected->setVisible(1);
        }
        if( nodeNumber == 3 ) {
            ui->contact4Selected->setVisible(1);
        }
        if( nodeNumber == 4 ) {
            ui->contact5Selected->setVisible(1);
        }
        if( nodeNumber == 5 ) {
            ui->contact6Selected->setVisible(1);
        }
        if( nodeNumber == 6 ) {
            ui->contact7Selected->setVisible(1);
        }
        if( nodeNumber == 7 ) {
            ui->contact8Selected->setVisible(1);
        }
        if( nodeNumber == 8 ) {
            ui->contact9Selected->setVisible(1);
        }
        if( nodeNumber == 9 ) {
            ui->contact10Selected->setVisible(1);
        }
}

void MainWindow::scanPeers()
{

}

MainWindow::~MainWindow()
{
    /* TODO:
     if (m_fd >= 0)
      close(m_fd);*/
    fifoIn.close();
    msgFifoIn.close();
    delete ui;
}

void MainWindow::loadSettings()
{
    int myOwnNodeId;
    QSettings settings(SETTINGS_INI_FILE,QSettings::IniFormat);
    /* Get own node information */
    nodes.myNodeId = settings.value("my_id").toString();
    nodes.myNodeIp = settings.value("my_ip").toString();
    nodes.myNodeName = settings.value("my_name").toString();
    ui->myNodeName->setText(nodes.myNodeName);
    /* Get nodes */
    for (int x=0; x < NODECOUNT; x++ ) {
        nodes.node_name[x] = settings.value("node_name_"+QString::number(x), "").toString();
        nodes.node_ip[x] = settings.value("node_ip_"+QString::number(x), "").toString();
        nodes.node_id[x] = settings.value("node_id_"+QString::number(x), "").toString();
    }
    /* Change button titles */
    ui->contact1Button->setText( nodes.node_name[0] );
    ui->contact2Button->setText( nodes.node_name[1] );
    ui->contact3Button->setText( nodes.node_name[2] );
    ui->contact4Button->setText( nodes.node_name[3] );
    ui->contact5Button->setText( nodes.node_name[4] );
    ui->contact6Button->setText( nodes.node_name[5] );

    /* TODO THIS After INI files are formed right: */
    ui->contact7Button->setText( nodes.node_name[6] );
    ui->contact8Button->setText( nodes.node_name[7] );
    ui->contact9Button->setText( nodes.node_name[8] );
    ui->contact10Button->setText( nodes.node_name[9] );

    /* Disable my own contact button */
    for (int x=0; x < NODECOUNT; x++ ) {

        if ( nodes.node_name[x].compare( nodes.myNodeName ) == 0 )
            myOwnNodeId = x;

         if ( myOwnNodeId == x ) {
             if ( x == 0 )
                 ui->contact1Button->setDisabled(true);
             if ( x == 1 )
                 ui->contact2Button->setDisabled(true);
             if ( x == 2 )
                 ui->contact3Button->setDisabled(true);
             if ( x == 3 )
                 ui->contact4Button->setDisabled(true);
             if ( x == 4 )
                 ui->contact5Button->setDisabled(true);
             if ( x == 5 )
                 ui->contact6Button->setDisabled(true);
             if ( x == 6 )
                 ui->contact7Button->setDisabled(true);
             if ( x == 7 )
                 ui->contact8Button->setDisabled(true);
             if ( x == 8 )
                 ui->contact9Button->setDisabled(true);
             if ( x == 9 )
                 ui->contact10Button->setDisabled(true);

         }
    }
    /* Get connection profile from INI file */
    loadConnectionProfile();
    /* Get connection points */
    for (int x=0; x < CONNPOINTCOUNT; x++ ) {
        nodes.connectionPointName[x] = settings.value("conn_point_name_"+QString::number(x), "").toString();
        ui->route1Button->setText(nodes.connectionPointName[0]);
        ui->route2Button->setText(nodes.connectionPointName[1]);
        ui->route3Button->setText(nodes.connectionPointName[2]);
    }
    /* Get connection gateway IP and PORT to settings page */
    QSettings connectionSettings(WG_CONFIGURATION_FILE,QSettings::IniFormat);
    QString connectionIp = connectionSettings.value("WireGuardPeer/Endpoint").toString();
    ui->gatewayIpPortInput->setText(connectionIp);
    ui->saveGatewayButton->setStyleSheet(m_buttonNormalStyle);
    ui->saveGatewayButton->setEnabled(false);
}


void MainWindow::loadConnectionProfile()
{
    QSettings settings(SETTINGS_INI_FILE,QSettings::IniFormat);
    nodes.connectionProfile = settings.value("connection_profile","wan").toString();
    if ( nodes.connectionProfile == "wan" )
    {
        ui->route1Selected->setVisible(1);
        ui->route2Selected->setVisible(0);
        ui->route3Selected->setVisible(0);
    }
    if ( nodes.connectionProfile == "lan" )
    {
        ui->route1Selected->setVisible(0);
        ui->route2Selected->setVisible(1);
        ui->route3Selected->setVisible(0);
    }
}

void MainWindow::saveAndActivateConnectionProfile(QString profile)
{
    if ( profile == "wan" || profile == "lan")
    {
        QSettings settings(SETTINGS_INI_FILE,QSettings::IniFormat);
        settings.setValue("connection_profile", profile );
    }
    /* Run Profile change (includes reboot) */
    if ( profile == "wan" )
    {
        qint64 pid;
        QProcess process;
        process.setProgram("/opt/tunnel/wan-config.sh");
        process.setArguments({""});
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.startDetached(&pid);
    }
    if ( profile == "lan")
    {
        qint64 pid;
        QProcess process;
        process.setProgram("/opt/tunnel/lan-config.sh");
        process.setArguments({""});
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.startDetached(&pid);
    }
}

/*
 * Set system volume with external process.
 * Playback volume:     amixer sset [DEVICENAME] Playback 100%
 * Microphone volume:   amixer sset [DEVICENAME] Capture 5%+
 */
void MainWindow::setSystemVolume(int volume)
{
    QString volumePercentString = QString::number(volume) + "%";
    qint64 pid;
    QProcess process;
    process.setProgram("/usr/bin/amixer");
    process.setArguments({"sset","'"+uiElement.audioMixerOutputDevice+"'", "Playback",volumePercentString});
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.startDetached(&pid);
}
void MainWindow::setMicrophoneVolume(int volume)
{
    QString volumePercentString = QString::number(volume) + "%";
    qint64 pid;
    QProcess process;
    process.setProgram("/usr/bin/amixer");
    process.setArguments({"sset","'"+uiElement.audioMixerInputDevice+"'", "Capture", volumePercentString });
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.startDetached(&pid);
}

void MainWindow::loadUserPreferences()
{
    QSettings settings(USER_PREF_INI_FILE,QSettings::IniFormat);
    uPref.volumeValue = settings.value("volume","70").toString();
    nodes.beepActive = settings.value("beep").toString();
    uPref.m_pinCode = settings.value("pincode","1234").toString();
    uPref.m_settingsPinCode = settings.value("settings_pincode","4321").toString();
    uPref.m_autoerase = settings.value("autoerase","true").toString();
    if ( uPref.m_autoerase == "true") {
        ui->autoeraseCheckbox->setChecked(true);
    }
    if ( uPref.m_autoerase == "false") {
        ui->autoeraseCheckbox->setChecked(false);
    }
    uPref.m_micVolume = settings.value("micvolume","90").toString();

}
void MainWindow::saveUserPreferences()
{
    QSettings settings(USER_PREF_INI_FILE,QSettings::IniFormat);
    settings.setValue("volume", uPref.volumeValue);
    setSystemVolume( uPref.volumeValue.toInt() );
    setMicrophoneVolume(uPref.m_micVolume.toInt());
}

void MainWindow::saveUserPreferencesBeep(QString value)
{
    QSettings settings(USER_PREF_INI_FILE,QSettings::IniFormat);
    settings.setValue("beep", value);
    nodes.beepActive = settings.value("beep").toString();
}

/* TODO: Add new elements to INI */
void MainWindow::loadUserInterfacePreferences()
{
    QSettings settings(UI_ELEMENTS_INI_FILE,QSettings::IniFormat);
    uiElement.messagingTitle = settings.value("message_title","Messaging:").toString();
    uiElement.commCheckButton = settings.value("commcheck_button","COMM CHECK").toString();
    uiElement.eraseButton = settings.value("erase_button","ERASE").toString();
    uiElement.secureVoiceInactiveNotify = settings.value("voice_inactive_notify","SECURE VOICE INACTIVE").toString();
    uiElement.secureVoiceActiveNotify = settings.value("voice_active_notify","SECURE VOICE ACTIVE").toString();
    uiElement.goSecureButton = settings.value("go_secure_button","Go Secure").toString();
    uiElement.terminateSecureButton = settings.value("terminate_secure_button","Terminate").toString();
    uiElement.systemName = settings.value("system_name","CommUnit").toString();
    uiElement.pinEntryTitleVault = settings.value("pintitle_vault","Enter vault PIN").toString();
    uiElement.pinEntryTitleVaultChecking = settings.value("pintitle_vault_check","Checking...").toString();
    uiElement.pinEntryTitleAccessPin = settings.value("pintitle_access","Set calibration data:").toString();
    uiElement.cameraButtonVisible = settings.value("cam_enabled",false).toBool();
    uiElement.audioMixerOutputDevice = settings.value("audio_device","Headset").toString();
    uiElement.audioMixerInputDevice = settings.value("audio_mic_device","Headset").toString();
    ui->systemNameLabel->setText(uiElement.systemName);
    ui->messagingTitle->setText(uiElement.messagingTitle);
    ui->commCheckButton->setText(uiElement.commCheckButton);
    ui->eraseButton->setText(uiElement.eraseButton);
    ui->greenButton->setText(uiElement.goSecureButton);
    ui->redButton->setText(uiElement.terminateSecureButton);
    ui->pinEntryTitle->setText(uiElement.pinEntryTitleAccessPin);
    ui->audioDeviceInput->setText(uiElement.audioMixerOutputDevice);
    ui->audioDeviceMicInputName->setText(uiElement.audioMixerInputDevice);
    if ( uiElement.cameraButtonVisible ) {
        ui->camButton->setVisible(1);
    } else {
        ui->camButton->setVisible(0);
    }
}

/* Timeout for FIFO replies */
int MainWindow::waitForFifoReply() {
    g_fifoCheckInProgress = true;
    fifoReplyTimer = new QTimer(this);
    fifoReplyTimer->start(10000); // 10 s
    connect(fifoReplyTimer, SIGNAL(timeout()), this, SLOT(checkFifoReplyTimeout()) );

        while ( g_fifoReply == "" && g_fifoCheckInProgress == true ) {
            QCoreApplication::processEvents();
            if ( g_fifoReply != "" ) {
                fifoReplyTimer->stop();
                return FIFO_REPLY_RECEIVED;
            }
        }

    if ( g_fifoReply == "" && g_fifoCheckInProgress == false )
        return FIFO_TIMEOUT;
}

int MainWindow::checkFifoReplyTimeout() {
    g_fifoCheckInProgress = false;
    fifoReplyTimer->stop();
    return 0;
}


/* 'Go Secure' button */
void MainWindow::on_greenButton_clicked()
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    updateCallStatusIndicator("Waiting remote", "lightgreen","transparent",INDICATE_ONLY);
    /* Send telemetry 'ring' -> ring_ready */
    QString callString = g_connectedNodeIp + ",ring";
    fifoWrite( callString );

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    /* Send 'ring' to UI */
    callString = g_connectedNodeIp + ",message,ring";
    fifoWrite( callString );
}

/* 'Terminate' button */
void MainWindow::on_redButton_clicked()
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    updateCallStatusIndicator("Terminating...", "lightgreen", "transparent",INDICATE_ONLY );
    ui->contact1Selected->setVisible(0);
    ui->contact2Selected->setVisible(0);
    ui->contact3Selected->setVisible(0);
    ui->contact4Selected->setVisible(0);
    ui->contact5Selected->setVisible(0);
    ui->contact6Selected->setVisible(0);
    ui->contact7Selected->setVisible(0);
    ui->contact8Selected->setVisible(0);
    ui->contact9Selected->setVisible(0);
    ui->contact10Selected->setVisible(0);
    // setContactButtons(true);

    if ( g_connectState )
        disconnectAsClient(g_connectedNodeIp, g_connectedNodeId);

    /* Local FIFO commands don't have ACK */
    QTimer::singleShot(6 * 1000, this, SLOT(tearDownLocal()));

    updateCallStatusIndicator("Please wait...", "lightgreen", "transparent",LOG_AND_INDICATE);
    ui->keyPrecentage->setText("");
    ui->redButton->setStyleSheet(s_terminateButtonStyle_normal);
    ui->greenButton->setStyleSheet(s_goSecureButtonStyle_normal);
    ui->greenButton->setEnabled(false);
    if ( uPref.m_autoerase == "true") {
        on_eraseButton_clicked();
    }
    beepBuzzer(10);
}

void MainWindow::tearDownLocal()
{
    qDebug() << "tearDownLocal";
    QString terminateLocalFifoCmd = "127.0.0.1,terminate_local";
    fifoWrite(terminateLocalFifoCmd);
    setContactButtons(true);
    updateCallStatusIndicator(uiElement.secureVoiceInactiveNotify, "green", "transparent",LOG_AND_INDICATE);
}

void MainWindow::on_route1Button_clicked()
{
    if (nodes.connectionProfile == "lan")
    {
        saveAndActivateConnectionProfile("wan");
    }
}

void MainWindow::on_route2Button_clicked()
{
    if (nodes.connectionProfile == "wan")
    {
        saveAndActivateConnectionProfile("lan");
    }
}

void MainWindow::on_route3Button_clicked()
{
    ui->route1Selected->setVisible(0);
    ui->route2Selected->setVisible(0);
    ui->route3Selected->setVisible(1);
}

void MainWindow::changeCallSign(int index)
{
    QInputDialog inputBox;
    inputBox.setLabelText("Change call sign:");
    inputBox.setStyleSheet( m_editCallSignStyle );
    int ret = inputBox.exec();
    if ( ret == QDialog::Accepted ) {
        QString newCallSign = inputBox.textValue();
        if ( newCallSign.length() > 2 && newCallSign.length() < 10 ) {
            QString nodeName="node_name_" + QString::number(index);
            QSettings settings(SETTINGS_INI_FILE,QSettings::IniFormat);
            settings.setValue(nodeName, newCallSign );
            loadSettings();
        }
    }
}

void MainWindow::on_contact1Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(0);
    } else {
        QString scanCmd = nodes.node_ip[0] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact2Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(1);
    } else {
        QString scanCmd = nodes.node_ip[1] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact3Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(2);
    } else {
        QString scanCmd = nodes.node_ip[2] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact4Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(3);
    } else {
        QString scanCmd = nodes.node_ip[3] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact5Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(4);
    } else {
        QString scanCmd = nodes.node_ip[4] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact6Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(5);
    } else {
        QString scanCmd = nodes.node_ip[5] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact7Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(6);
    } else {
        QString scanCmd = nodes.node_ip[6] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact8Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(7);
    } else {
        QString scanCmd = nodes.node_ip[7] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact9Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(8);
    } else {
        QString scanCmd = nodes.node_ip[8] + ",status";
        fifoWrite(scanCmd);
    }
}

void MainWindow::on_contact10Button_clicked()
{
    if ( m_f1WasDown )
    {
        changeCallSign(9);
    } else {
        QString scanCmd = nodes.node_ip[9] + ",status";
        fifoWrite(scanCmd);
    }
}


void MainWindow::on_volumeSlider_valueChanged(int value)
{
    uPref.volumeValue = QString::number(value);
    saveUserPreferences();
}

void MainWindow::on_pwrButton_clicked()
{
    qint64 pid;
    QProcess process;
    process.setProgram("/sbin/poweroff");
    process.setArguments({"-f"});
    process.startDetached(&pid);
}

void MainWindow::on_commCheckButton_clicked()
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    if ( g_connectState ) {
        QString fifo_command = g_remoteOtpPeerIp + ",message,Ping";
        fifoWrite(fifo_command);
    }
}

void MainWindow::on_eraseButton_clicked()
{
    ui->messagesView->clear();
    ui->lineEdit->clear();
}

/* Send Message over OTP Channel */
void MainWindow::on_lineEdit_returnPressed()
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    if ( g_connectState ) {
        QString msg_line = ui->lineEdit->text();
        /* TODO: Check lenght */
        ui->messagesView->append("<font color='white'>" + msg_line + "</font>");
        msg_line.replace( ",", QChar(SUBSTITUTE_CHAR_CODE) );
        QString fifo_command = g_remoteOtpPeerIp + ",message," + msg_line;
        qDebug() << "on_lineEdit_returnPressed(): " << fifo_command;
        fifoWrite(fifo_command);
        ui->lineEdit->clear();
    }
}

/* PIN Entry buttons */
void MainWindow::on_pinButton_clear_clicked()
{
   ui->codeValue->setText("");
   beepBuzzer(20);
}
void MainWindow::on_pinButton_1_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"1");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_2_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"2");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_3_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"3");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_4_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"4");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_5_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"5");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_6_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"6");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_7_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"7");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_8_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"8");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_9_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"9");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_a_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"A");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_b_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"B");
    beepBuzzer(20);
}

int MainWindow::on_pinButton_c_clicked()
{
    if ( m_startMode == UI_MODE ) {
        if ( ui->codeValue->text() == uPref.m_settingsPinCode ) {
            /* Get connection gateway IP and PORT to settings page */
            QSettings connectionSettings(WG_CONFIGURATION_FILE,QSettings::IniFormat);
            QString connectionIp = connectionSettings.value("WireGuardPeer/Endpoint").toString();
            ui->gatewayIpPortInput->setText(connectionIp);
            ui->saveGatewayButton->setStyleSheet(m_buttonNormalStyle);
            ui->saveGatewayButton->setEnabled(false);
            ui->settingsFrame->setVisible(true);
            ui->logoLabel->setVisible(false);
            return 0;
        }
        if ( ui->codeValue->text() == uPref.m_pinCode ) {
             ui->codeFrame->setVisible(false);
             beepBuzzer(20);
             return 0;
        } else {
            ui->codeValue->setText("");
            beepBuzzer(20);
            return 0;
        }
    } else {
        /* VAULT mode */
        QString vaultPinCode = ui->codeValue->text();
        if ( vaultPinCode.length() > 3 ) {
            vaultOpenProcess.connect(&vaultOpenProcess,
                    &QProcess::readyReadStandardOutput,
                    this, &MainWindow::onVaultProcessReadyReadStdOutput);
            vaultOpenProcess.connect(&vaultOpenProcess,
                    (void (QProcess::*)(int,QProcess::ExitStatus))&QProcess::finished,
                    this, &MainWindow::onVaultProcessFinished);

            ui->pinEntryTitle->setText(uiElement.pinEntryTitleVaultChecking);
            ui->codeValue->setText("");
            vaultOpenProcess.setProgram("/bin/vault-pin.sh");
            vaultOpenProcess.setArguments({vaultPinCode});
            vaultOpenProcess.start();
            ui->countLabel->setVisible(true);
            ui->countLabel->setText(QString("<span style=\"font-size:68pt; color:#dddd00;\">• WAIT •</span>"));
        } else {
            ui->codeValue->setText("");
        }
    }
    return 0;
}

void MainWindow::onVaultProcessReadyReadStdOutput()
{
    vaultOpenProcess.setReadChannel(QProcess::StandardOutput);
    QTextStream stream(&vaultOpenProcess);
    while (!stream.atEnd()) {
       QString line = stream.readLine();
       ui->countLabel->setText(QString("<span style=\"font-size:68pt; color:#00dd00;\">★ OK ★</span>"));
       QTimer::singleShot(2 * 1000, this, SLOT(exitVaultOpenProcess()));
    }
}
void MainWindow::exitVaultOpenProcess()
{
    ui->countLabel->setVisible(false);
    ui->countLabel->setText("");
    QCoreApplication::quit();
}
void MainWindow::exitVaultOpenProcessWithFail()
{
    ui->countLabel->setVisible(false);
    ui->countLabel->setText("");
    ui->pinEntryTitle->setText(uiElement.pinEntryTitleVault);
    ui->codeValue->setText("");
}
void MainWindow::onVaultProcessFinished()
{
    QString perr = vaultOpenProcess.readAllStandardError();
    if (perr.length()) {
        ui->countLabel->setVisible(true);
        ui->countLabel->setText(QString("<span style=\"font-size:68pt; color:#dd0000;\">• FAIL •</span>"));
        QTimer::singleShot(5 * 1000, this, SLOT(exitVaultOpenProcessWithFail()));
    }
}

void MainWindow::on_pinButton_0_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"0");
    beepBuzzer(20);
}
void MainWindow::on_pinButton_hash_clicked()
{
    ui->codeValue->setText(ui->codeValue->text()+"#");
    beepBuzzer(20);
}

void MainWindow::updateCallStatusIndicator(QString text, QString fontColor, QString backgroundColor, int logMethod )
{
    if ( logMethod == LOG_AND_INDICATE || logMethod == INDICATE_ONLY ) {
    ui->voiceActive->setText(text);
    ui->voiceActive->setStyleSheet("QLabel#voiceActive { \
                                   background-color: "+backgroundColor+"; \
                                   border-style: outset; \
                                   border-width: 0px; \
                                   border-radius: 0px; \
                                   border-color: green; \
                                   color: " + fontColor + "; \
                                   font: bold 30px; \
                                   min-width: 5em; \
                                   padding: 6px; \
                               }");
    }
    if ( logMethod == LOG_ONLY ) {
        ui->messagesView->append("[SYSTEM]: " + text );
    }
}

/* Outbound connection */
void MainWindow::connectAsClient(QString nodeIp, QString nodeId)
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    /* 1. Send 'prepare' to recipient via FIFO */
    QString prepareFifoCmd = nodeIp + ",prepare";
    fifoWrite(prepareFifoCmd);

        // Wait FIFO with timeout
        if ( waitForFifoReply() == FIFO_TIMEOUT ) {
            updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
            return;
        }

    /* 2. Start local service for targeted node as client */
    QString serviceNameAsClient = "connect-with-"+nodeId+"-c.service";
    qDebug() << "Starting service: " << serviceNameAsClient;
    qint64 pid;
    QProcess process;
    process.setProgram("systemctl");
    process.setArguments({"start",serviceNameAsClient});
    process.startDetached(&pid);

    /* 3. Touch local file */
    touchLocalFile("/tmp/CLIENT_CALL_ACTIVE");

    /* Now we should have OTP connectivity ready */
    g_connectState = true;
    g_connectedNodeId = nodeId;
    g_connectedNodeIp = nodeIp;

    /* 4. Indicate yellow state on status */
    updateCallStatusIndicator("OTP connected", "lightgreen", "transparent",INDICATE_ONLY);

    /* We should highlight Go Secure and Terminate at initiator end (TODO) */
    ui->redButton->setStyleSheet(s_terminateButtonStyle_highlight);
    ui->greenButton->setStyleSheet(s_goSecureButtonStyle_highlight);
    ui->greenButton->setEnabled(true);

    /* We should disable Peer keys when connected */
    setContactButtons(false);

    /* 5. Set fixed remote IP based on client role of connection */
    g_remoteOtpPeerIp = "10.10.0.1";

    /* 6. Indicate remote peer UI that we're connected WORK IN PROGRESS!! */
    QString informRemoteUi = nodeIp + ",message,client_connected;"+nodes.myNodeId+";"+nodes.myNodeIp+";"+nodes.myNodeName;
    fifoWrite(informRemoteUi);

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    // 7. Audio gets established by remote end sending 'answer'
    // 8. After termination => terminate_ready
}

void MainWindow::disconnectAsClient(QString nodeIp, QString nodeId)
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    /* 1. Terminate to FIFO */
    QString terminateFifoCmd = nodeIp + ",terminate";
    fifoWrite(terminateFifoCmd);

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    /* 2. Terminate to UI (so remote can tear down indications) */
    terminateFifoCmd = nodeIp + ",message,initiator_disconnect";
    fifoWrite(terminateFifoCmd);

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    /* 2. Stop local service for targeted node (as client) */
    QString serviceNameAsClient = "connect-with-"+nodeId+"-c.service";
    qint64 pid;
    QProcess process;
    process.setProgram("systemctl");
    process.setArguments({"stop",serviceNameAsClient});
    process.startDetached(&pid);

    /* 3. Remove local status file */
    removeLocalFile("/tmp/CLIENT_CALL_ACTIVE");

    /*  4. Terminate Audio
        'telemetryclient' knows how to terminate audio, based on how it's established (client or server)
    */
    QString terminateAudioFifoCmd = "127.0.0.1,disconnect_audio";
    fifoWrite(terminateAudioFifoCmd);
    g_connectState = false;
    g_connectedNodeId = "";
    g_connectedNodeIp = "";
    g_remoteOtpPeerIp = "";

    /* Erase msg history */
    if ( uPref.m_autoerase == "true") {
        on_eraseButton_clicked();
    }
}

void MainWindow::touchLocalFile(QString filename)
{
    QFile touchFile(filename);
    touchFile.open( QIODevice::WriteOnly);
    touchFile.close();
}
void MainWindow::removeLocalFile(QString filename)
{
    QFile file (filename);
    file.remove();
}

/* Accept incoming call on "call in" popup */
void MainWindow::on_answerButton_clicked()
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    updateCallStatusIndicator("Accepted", "green","transparent",LOG_AND_INDICATE);

    /* Send indication that we answered succesfully */
    QString answerString = g_connectedNodeIp + ",message,answer_success";
    fifoWrite( answerString );

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    /* Send answer to telemetry server */
    answerString = g_connectedNodeIp + ",answer";
    fifoWrite( answerString );

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    /* Connect audio as Server */
    answerString = "127.0.0.1,connect_audio_as_server";
    fifoWrite( answerString );

    updateCallStatusIndicator("Audio connected", "green","transparent",INDICATE_ONLY);
    ui->incomingTitleFrame->setText("Voice active!");
    ui->answerButton->setEnabled(false);
    ui->answerButton->setVisible(false);
    ui->denyButton->setText("Hangup");
}

/* Deny button ('Hangup') on popup ( 'Client' terminates ) */
void MainWindow::on_denyButton_clicked()
{
    screenBlanktimer->start(BLACK_OUT_TIME);
    /* Hangup to FIFO */
    QString hangupCommandString = g_connectedNodeIp + ",hangup";
    fifoWrite( hangupCommandString );

    // Wait FIFO with timeout
    if ( waitForFifoReply() == FIFO_TIMEOUT ) {
        updateCallStatusIndicator("Timeout. Aborting.", "green", "transparent",LOG_ONLY );
        return;
    }

    /* Turn off local audio */
    hangupCommandString = "127.0.0.1,disconnect_audio";
    fifoWrite( hangupCommandString );
    updateCallStatusIndicator("Incoming Terminated", "green","transparent",LOG_AND_INDICATE);
    ui->inComingFrame->setVisible(false);

    /* Erase green status */
    ui->contact1Selected->setVisible(0);
    ui->contact2Selected->setVisible(0);
    ui->contact3Selected->setVisible(0);
    ui->contact4Selected->setVisible(0);
    ui->contact5Selected->setVisible(0);
    ui->contact6Selected->setVisible(0);

    /* Activate 'contacts' again */
    setContactButtons(true);
    ui->answerButton->setEnabled(true);
    ui->answerButton->setVisible(true);
    ui->keyPrecentage->setText("");
    ui->redButton->setStyleSheet(s_terminateButtonStyle_normal);
    ui->greenButton->setStyleSheet(s_goSecureButtonStyle_normal);
    ui->greenButton->setEnabled(false);
    if ( uPref.m_autoerase == "true") {
        on_eraseButton_clicked();
    }

    g_connectState = false;
    g_connectedNodeId = "";
    g_connectedNodeIp = "";
    g_remoteOtpPeerIp = "";

    /* Erase msg history */
    if ( uPref.m_autoerase == "true") {
        on_eraseButton_clicked();
    }
}

/* Power button on PIN dialog */
void MainWindow::on_pinButton_pwr_clicked()
{
    on_pwrButton_clicked();
}

/* Read dpinger service output file with timer */
void MainWindow::networkLatency()
{
   QString networkStatusFile="/tmp/network";
    QFile networkFile(networkStatusFile);
    if(!networkFile.exists()){
        qDebug() << "Error, no file: " << networkStatusFile;
    } else {
        QString networkMeasurementValues;
        if (networkFile.open(QIODevice::ReadOnly | QIODevice::Text)){
            QTextStream stream(&networkFile);
            while (!stream.atEnd()){
                networkMeasurementValues = stream.readLine();
            }
        }
        networkFile.close();
        QStringList networkElements = networkMeasurementValues.split(' ');
        int latencyIntms = networkElements[0].toInt()/1000;
        QString indicateValue = QString::number(latencyIntms) + " ms";
        ui->networkLatencyLabel->setText(indicateValue);
        /* Set UI color & STA led */
        if ( latencyIntms < 200 ) {
            ui->networkLatencyLabel->setStyleSheet("color: lightgreen;");
            staGreenLedOn();
        }
        if ( latencyIntms == 0 ) {
            ui->networkLatencyLabel->setStyleSheet("color: '#FF5555';");
            staRedLedOn();
        }
        if ( latencyIntms > 200 && latencyIntms < 1000) {
            ui->networkLatencyLabel->setStyleSheet("color: yellow;");
            staGreenLedOn();
            staRedLedOn();
        }
        if ( latencyIntms > 1000 ) {
            ui->networkLatencyLabel->setStyleSheet("color: '#FF5555';");
            staRedLedOn();
        }
    }
    peerLatency();
    /* Keep screen on while connected */
    if ( g_connectState ) {
        screenBlanktimer->start(BLACK_OUT_TIME);
    }
}

/* Read dpinger service output file for peers */
void MainWindow::peerLatency()
{
    QString normalStyle = " QPushButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
           color: green; \
            font:  30px; \
            min-width: 1em; \
            padding: 6px; \
        } \
        QPushButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";

    QString highlightStyle = " QPushButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
            color: lightgreen; \
            font:  30px; \
            min-width: 1em; \
            padding: 6px; \
        } \
        QPushButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";

    QStringList peerStatusFileNames = { "/tmp/peer0","/tmp/peer1","/tmp/peer2","/tmp/peer3","/tmp/peer4", "/tmp/peer5", "/tmp/peer6", "/tmp/peer7", "/tmp/peer8", "/tmp/peer9" };

    for (int i = 0; i < peerStatusFileNames.size(); i++) {
        QString fileName=peerStatusFileNames.at(i).toLocal8Bit().constData();
        QString entryLatency;
        QFile peerLatencyFile( fileName );
        if(!peerLatencyFile.exists()) {
            qDebug() << "No latency file: " << fileName;
        } else {
            QString peerLatencyValue;
            if (peerLatencyFile.open(QIODevice::ReadOnly | QIODevice::Text)){
                QTextStream stream(&peerLatencyFile);
                while (!stream.atEnd()){
                    entryLatency = stream.readLine();
                }
            }
            peerLatencyFile.close();
            QStringList latencyElements = entryLatency.split(' ');
            int latencyIntms = latencyElements[0].toInt()/1000;
            m_peerLatencyValue[i] = QString::number(latencyIntms);
        }
    }
    /* Peer latency */
    if ( m_peerLatencyValue[0].toInt() > 0 ) {
        ui->contact1Button->setStyleSheet(highlightStyle);
    } else {
        ui->contact1Button->setStyleSheet(normalStyle);
    }
    if ( m_peerLatencyValue[1].toInt() > 0 ) {
        ui->contact2Button->setStyleSheet(highlightStyle);
    } else {
        ui->contact2Button->setStyleSheet(normalStyle);
    }
    if ( m_peerLatencyValue[2].toInt() > 0 ) {
        ui->contact3Button->setStyleSheet(highlightStyle);
    } else {
        ui->contact3Button->setStyleSheet(normalStyle);
    }
    if ( m_peerLatencyValue[3].toInt() > 0 ) {
        ui->contact4Button->setStyleSheet(highlightStyle);
    } else {
        ui->contact4Button->setStyleSheet(normalStyle);
    }
    if ( m_peerLatencyValue[4].toInt() > 0 ) {
        ui->contact5Button->setStyleSheet(highlightStyle);
    } else {
        ui->contact5Button->setStyleSheet(normalStyle);
    }
    if ( m_peerLatencyValue[5].toInt() > 0 ) {
        ui->contact6Button->setStyleSheet(highlightStyle);
    } else {
        ui->contact6Button->setStyleSheet(normalStyle);
    }

        if ( m_peerLatencyValue[6].toInt() > 0 ) {
            ui->contact7Button->setStyleSheet(highlightStyle);
        } else {
            ui->contact7Button->setStyleSheet(normalStyle);
        }
        if ( m_peerLatencyValue[7].toInt() > 0 ) {
            ui->contact8Button->setStyleSheet(highlightStyle);
        } else {
            ui->contact8Button->setStyleSheet(normalStyle);
        }
        if ( m_peerLatencyValue[8].toInt() > 0 ) {
            ui->contact9Button->setStyleSheet(highlightStyle);
        } else {
            ui->contact9Button->setStyleSheet(normalStyle);
        }
        if ( m_peerLatencyValue[9].toInt() > 0 ) {
            ui->contact10Button->setStyleSheet(highlightStyle);
        } else {
            ui->contact10Button->setStyleSheet(normalStyle);
        }
}

/* This function will read key file lenghts and counter values of each key.
   Way keys are named as files, depends on index unit has. Therefore we
   need 'tipping point' - which gives index order change location while
   checking files. See key creation code to get better picture of this.
*/
void MainWindow::reloadKeyUsage()
{
    int tippingPoint=0;
    QString keyfile;
    QString keyCountfile;

    for (int x=0; x < NODECOUNT; x++ ) {
        m_keyPersentage_incount[x] = "";
        m_keyPersentage_outcount[x] = "";
    }
    /* Find a tipping point in keyfile naming. */
    for (int x=0; x < NODECOUNT; x++ ) {
        if ( nodes.node_id[x].compare( nodes.myNodeId ) == 0 ){
            tippingPoint = x;
        }
    }
    /* Loop for inkey with tipping point evaluation */
    for (int x=0; x < NODECOUNT; x++ ) {
        if ( nodes.node_id[x].compare( nodes.myNodeId ) != 0 )
        {
            if ( x < tippingPoint ) {
                keyfile = "/opt/tunnel/" + nodes.node_id[x] + nodes.myNodeId +".inkey";
                keyCountfile = "/opt/tunnel/" + nodes.node_id[x] + nodes.myNodeId +".incount";
            } else {
                keyfile = "/opt/tunnel/" + nodes.myNodeId + nodes.node_id[x] +".inkey";
                keyCountfile = "/opt/tunnel/" + nodes.myNodeId + nodes.node_id[x] +".incount";
            }
            /* Read actual information */
            long int key_file_size = get_file_size(keyfile);
            long int rx_key_used = get_key_index(keyCountfile);
            float key_presentage = (100.0*rx_key_used)/key_file_size;
            QString fullPresentage=QString::number(100-key_presentage,'f',0);
            m_keyPersentage_incount[x] = fullPresentage;
        }
    }
    /* Loop for outkey with tipping point evaluation */
    for (int x=0; x < NODECOUNT; x++ ) {
        if ( nodes.node_id[x].compare( nodes.myNodeId ) != 0 )
        {
            if ( x < tippingPoint ) {
                keyfile = "/opt/tunnel/" + nodes.node_id[x] + nodes.myNodeId +".outkey";
                keyCountfile = "/opt/tunnel/" + nodes.node_id[x] + nodes.myNodeId +".outcount";
            } else {
                keyfile = "/opt/tunnel/" + nodes.myNodeId + nodes.node_id[x] +".outkey";
                keyCountfile = "/opt/tunnel/" + nodes.myNodeId + nodes.node_id[x] +".outcount";
            }
            // Read actual information
            long int key_file_size = get_file_size(keyfile);
            long int rx_key_used = get_key_index(keyCountfile);
            float key_presentage = (100.0*rx_key_used)/key_file_size;
            QString fullPresentage=QString::number(100-key_presentage,'f',0);
            m_keyPersentage_outcount[x] = fullPresentage;
        }
    }
    for (int x=0; x < NODECOUNT; x++){
        if ( m_keyPersentage_incount[x] != "" ) {
            m_keyStatusString[x] = m_keyPersentage_incount[x] + "/" + m_keyPersentage_outcount[x];
        }
    }
}

long int MainWindow::get_file_size(QString keyFilename)
{
    long int size = 0;
    QFile myFile(keyFilename);
    if (myFile.open(QIODevice::ReadOnly)){
        size = myFile.size();
        myFile.close();
        return size;
    }
    return -1;
}

long int MainWindow::get_key_index(QString counterFilename)
{
    long int index=0;
    FILE *keyindex_file;
    std::string str = counterFilename.toStdString();
    const char* filename = str.c_str();
    keyindex_file = fopen(filename, "rb");
    fread(&index, sizeof(long int),1,keyindex_file);
    fclose(keyindex_file);
    return index;
}

void MainWindow::on_exitButton_clicked()
{
    ui->codeValue->setText("");
    ui->settingsFrame->setVisible(false);
    ui->logoLabel->setVisible(true);
}

/* WIFI Network connect with iwd

    1. Connect
    iwctl --passphrase [password] station wlan0 connect [ssid]

    2. Status
    ./wifi_status.sh
    connected [SSID] [IP]

    3. Interface IP's
    ./wifi_interfaceips.sh
    wlan0,[IP],eth0,[IP]

    4. Get known networks
    ./wifi_getknownnetworks.sh

    5. Forget
    iwctl known-networks [SSID] forget
*/

void MainWindow::scanAvailableWifiNetworks(QString command, QStringList parameters)
{
    qint64 pid;
    QProcess process;
    process.setProgram(command);
    process.setArguments(parameters);
    process.start();
    process.waitForFinished();
    QString result=process.readAllStandardOutput();
    ui->WifistatusLabel->setText(result);
    QString trimmedList = result.trimmed();
    QStringList networks=trimmedList.split(" ");
    ui->networksComboBox->addItems(networks);
}

void MainWindow::on_scanWifiButton_clicked()
{
    ui->networksComboBox->clear();
    ui->saveWifiButton->setStyleSheet(m_buttonNormalStyle);
    ui->WifistatusLabel->setStyleSheet(m_wifiConnectStatusNormalStyle);
    ui->saveWifiButton->setEnabled(false);
    ui->wifiPasswordText->setText("");
    scanAvailableWifiNetworks("/opt/tunnel/wifi_getnetworks.sh",{""});
    getKnownWifiNetworks();
}

void MainWindow::on_saveWifiButton_clicked()
{
    QString networkSsid=ui->networksComboBox->currentText();
    QString networkPassword=ui->wifiPasswordText->text();    
    QStringList parameters={"--passphrase",networkPassword,"station","wlan0","connect",networkSsid};
    connectWifiNetwork("iwctl", parameters);
    QTimer::singleShot(10000, this, SLOT( getWifiStatus() ));
}

void MainWindow::connectWifiNetwork(QString command, QStringList parameters)
{
    qint64 pid;
    QProcess process;
    process.setProgram(command);
    process.setArguments(parameters);
    process.startDetached(&pid);
}

void MainWindow::getWifiStatus()
{
    qint64 pid;
    QProcess process;
    process.setProgram("/opt/tunnel/wifi_status.sh");
    process.setArguments({""});
    process.start();
    process.waitForFinished();
    QString result=process.readAllStandardOutput();
    ui->WifistatusLabel->setText("Connect status: " + result);
    ui->WifistatusLabel->setStyleSheet(m_wifiConnectStatusHighlightStyle);
    ui->saveWifiButton->setStyleSheet(m_buttonNormalStyle);
    ui->saveWifiButton->setEnabled(false);
    ui->wifiPasswordText->setText("");
}

void MainWindow::getKnownWifiNetworks()
{
    qint64 pid;
    QProcess process;
    process.setProgram("/opt/tunnel/wifi_getknownnetworks.sh");
    process.setArguments({""});
    process.start();
    process.waitForFinished();
    QString result=process.readAllStandardOutput();
    /* Add also known networks to combo box. Index is used
       to change color of 'Forget' button when already known
       network is selected from dropdown.*/
    ui->networksComboBox->addItem("Known networks:");
    m_knownNetworkIndex = ui->networksComboBox->count();
    QString trimmedList = result.trimmed();
    QStringList networks=trimmedList.split(" ");
    ui->networksComboBox->addItems(networks);
}

void MainWindow::on_networksComboBox_activated(int index)
{
    if ( index >= m_knownNetworkIndex ) {
        ui->deleteWifiButton->setStyleSheet(m_buttonHighlightStyle);
        ui->saveWifiButton->setStyleSheet(m_buttonNormalStyle);
        ui->saveWifiButton->setEnabled(false);
        ui->wifiPasswordText->setText("");
    } else {
        ui->deleteWifiButton->setStyleSheet(m_buttonNormalStyle);
    }
}

void MainWindow::on_deleteWifiButton_clicked()
{
    QString deleteNetworkName=ui->networksComboBox->currentText();
    QStringList parameters={"known-networks",deleteNetworkName,"forget"};
    QProcess process;
    process.setProgram("iwctl");
    process.setArguments(parameters);
    process.start();
    process.waitForFinished();
    QString result=process.readAllStandardOutput();
    ui->networksComboBox->clear();
    ui->deleteWifiButton->setStyleSheet(m_buttonNormalStyle);
}

void MainWindow::on_wifiPasswordText_textChanged(const QString &arg1)
{
    int passwordEntryLen=ui->wifiPasswordText->text().length();
    if ( passwordEntryLen >= 8 ) {
        ui->saveWifiButton->setStyleSheet(m_buttonHighlightStyle);
        ui->saveWifiButton->setEnabled(true);
    } else {
        ui->saveWifiButton->setStyleSheet(m_buttonNormalStyle);
        ui->saveWifiButton->setEnabled(false);
    }
}

void MainWindow::on_saveGatewayButton_clicked()
{
    int lineCount=0;
    QStringList iniFileLines;
    /* Take new IP and PORT */
    QString connectionPointIpAndPort=ui->gatewayIpPortInput->text();
    /* Read tunnel configuration file and replace 'Endpoint' with given value*/
    QFile inputFile(WG_CONFIGURATION_FILE);
    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);
       while (!in.atEnd())
       {
          QString line = in.readLine();
              if ( line.startsWith("Endpoint") ) {
                iniFileLines.append("Endpoint="+connectionPointIpAndPort);
              } else {
                iniFileLines.append(line);
              }
       }
       inputFile.close();
       lineCount = iniFileLines.size();
    }

    /* Write conf for networkd */
    QFile networkdFile( WG_CONFIGURATION_FILE );
    if ( networkdFile.open(QIODevice::ReadWrite) )
    {
        QTextStream stream( &networkdFile );
        for (lineCount = 0; lineCount < iniFileLines.size(); lineCount++)
        {
            stream << iniFileLines.at(lineCount) << Qt::endl;
        }
    }
    networkdFile.close();
    /* Write conf to persisted location */
    QFile persistedFile( WG_CONFIGURATION_FILE_S );
    if ( persistedFile.open(QIODevice::ReadWrite) )
    {
        QTextStream stream( &persistedFile );
        for (lineCount = 0; lineCount < iniFileLines.size(); lineCount++)
        {
            stream << iniFileLines.at(lineCount) << Qt::endl;
        }
    }
    persistedFile.close();
    /* Notify user */
    ui->WifistatusLabel->setText("New connection point saved. \nReboot device to activate!");
    ui->WifistatusLabel->setStyleSheet(m_wifiConnectStatusHighlightStyle);
}

/* Check IPv4 validity
 * [1] https://stackoverflow.com/questions/791982/determine-if-a-string-is-a-valid-ipv4-address-in-c
 */
int MainWindow::isValidIp4 (char *str) {
    int segs = 0;   /* Segment count. */
    int chcnt = 0;  /* Character count within segment. */
    int accum = 0;  /* Accumulator for segment. */

    /* Catch NULL pointer. */
    if (str == NULL)
        return 0;
    /* Process every character in string. */
    while (*str != '\0') {
        /* Segment changeover. */
        if (*str == '.') {
            /* Must have some digits in segment. */
            if (chcnt == 0)
                return 0;
            /* Limit number of segments. */
            if (++segs == 4)
                return 0;
            /* Reset segment values and restart loop. */
            chcnt = accum = 0;
            str++;
            continue;
        }
        /* Check numeric. */
        if ((*str < '0') || (*str > '9'))
            return 0;
        /* Accumulate and check segment. */
        if ((accum = accum * 10 + *str - '0') > 255)
            return 0;
        /* Advance other segment specific stuff and continue loop. */
        chcnt++;
        str++;
    }
    /* Check enough segments and enough characters in last segment. */
    if (segs != 3)
        return 0;
    if (chcnt == 0)
        return 0;
    /* Address okay. */
    return 1;
}

/* Check validity as we type & enable Save button based on that */
void MainWindow::on_gatewayIpPortInput_textChanged(const QString &arg1)
{
    QString connectionPointIpAndPort=ui->gatewayIpPortInput->text();
    QStringList gwParts=connectionPointIpAndPort.split(":");
    /* Check input validity */
    if ( gwParts.count() == 2 ) {
        std::string str = gwParts[0].toStdString();
        char*p = (char*)str.c_str();
        if ( isValidIp4(p) && gwParts[1].toUInt() > 1024 && gwParts[1].toUInt() <= 65535 ) {
            ui->saveGatewayButton->setStyleSheet(m_buttonHighlightStyle);
            ui->saveGatewayButton->setEnabled(true);
        } else {
            ui->saveGatewayButton->setStyleSheet(m_buttonNormalStyle);
            ui->saveGatewayButton->setEnabled(false);
        }
    } else {
        ui->saveGatewayButton->setStyleSheet(m_buttonNormalStyle);
        ui->saveGatewayButton->setEnabled(false);
    }
}

void MainWindow::on_autoeraseCheckbox_stateChanged(int arg1)
{
    if ( arg1 == 2 ) {
        uPref.m_autoerase = "true";
    }
    if ( arg1 == 0 ) {
        uPref.m_autoerase = "false";
    }
    QSettings settings(USER_PREF_INI_FILE,QSettings::IniFormat);
    settings.setValue("autoerase", uPref.m_autoerase);
}

void MainWindow::finalCountdown()
{
    if ( m_finalCountdownValue >= 0 ) {
        ui->countLabel->setText(QString::number(m_finalCountdownValue));

        if ( m_finalCountdownValue == 0 ) {
            qint64 pid;
            QProcess process;
            process.setProgram("/bin/nuke.sh");
            process.setArguments({""});
            process.setStandardOutputFile(QProcess::nullDevice());
            process.setStandardErrorFile(QProcess::nullDevice());
            process.startDetached(&pid);
            ui->countLabel->setText("☹");
            beepBuzzer(500);
        }
        else {
            beepBuzzer(10);
        }
        m_finalCountdownValue--;
    }
}

void MainWindow::on_camButton_clicked()
{
    ui->imageFrame->setVisible(1);
    ui->imageFrameSendPicture->setVisible(0);
    ui->imageFramePictureLabel->clear();
    ui->imageFrameTakePictureButton->setVisible(1);
}

void MainWindow::on_imageFrameCloseButton_clicked()
{
    ui->imageFramePictureLabel->clear();
    ui->imageFrame->setVisible(0);
}

void MainWindow::on_imageFrameTakePictureButton_clicked()
{
    qint64 pid;
    QProcess process;
    process.setProgram("/bin/takepicture.sh");
    process.setArguments({""});
    process.start();
    process.waitForFinished();
    /* TODO: Timeout */
    QString camPictureFile(CAMERA_PIC_FILE);
    QFile fileCheck(camPictureFile);
    if ( fileCheck.exists() ) {
        ui->imageFramePictureLabel->setPixmap(QPixmap(camPictureFile));
        if (g_connectState)
            ui->imageFrameSendPicture->setVisible(1);
    }

}

void MainWindow::on_imageFrameSendPicture_clicked()
{
    qint64 pid;
    QProcess process;
    process.setProgram("/bin/sendpicture.sh");
    process.setArguments({g_remoteOtpPeerIp});
    process.start();
    process.waitForFinished();
    /* TODO: Timeout */
}

void MainWindow::incomingImageChangeDetected()
{
    QFile myFile("/tmp/ftp/incoming/image.png");
    if (myFile.open(QIODevice::ReadOnly)){
        m_imageFileSize = myFile.size();
        myFile.close();
    }
    if ( m_timerBlock == false )  {
        ui->imageFrame->setVisible(1);
        QTimer::singleShot(5000, this, SLOT(incomingImageVerifyChange()));
        m_timerBlock = true;
    }
    ui->imageFramePictureLabel->setText("Received " + QString::number(m_imageFileSize) + " bytes");
}

void MainWindow::incomingImageVerifyChange()
{
    m_timerBlock = false;
    QFile myFile("/tmp/ftp/incoming/image.png");
    int size=0;
    if (myFile.open(QIODevice::ReadOnly)){
        size = myFile.size();
        myFile.close();
    }
    if ( size == m_imageFileSize ) {
        ui->imageFramePictureLabel->setText("");
        QString camPictureFile("/tmp/ftp/incoming/image.png");
        QFile fileCheck(camPictureFile);
        if ( fileCheck.exists() ) {
            ui->imageFramePictureLabel->setPixmap(QPixmap(camPictureFile));
        }
        ui->imageFrameTakePictureButton->setVisible(0);
        ui->imageFrameSendPicture->setVisible(0);
        ui->imageFrame->setVisible(1);
    }

}

void MainWindow::on_audioDeviceInput_textChanged(const QString &arg1)
{
    QSettings settings(UI_ELEMENTS_INI_FILE,QSettings::IniFormat);
    settings.setValue("audio_device", arg1 );
    uiElement.audioMixerOutputDevice = arg1;
}


void MainWindow::on_micVolButton_clicked()
{
    QInputDialog inputBox;
    inputBox.setLabelText("Mic capture value (0-100):");
    inputBox.setStyleSheet( m_editCallSignStyle );
    inputBox.setTextValue(uPref.m_micVolume);
    int ret = inputBox.exec();
    if ( ret == QDialog::Accepted ) {
        int volumeValue = inputBox.textValue().toInt();
        if ( volumeValue > 0 && volumeValue <= 100 ) {
            setMicrophoneVolume(volumeValue);
            uPref.m_micVolume = QString::number(volumeValue);
            QSettings settings(USER_PREF_INI_FILE,QSettings::IniFormat);
            settings.setValue("micvolume", uPref.m_micVolume);
            ui->WifistatusLabel->setText("Capture volume set & saved: " + uPref.m_micVolume + " %" );
        }
    }
}


void MainWindow::on_audioDeviceMicInputName_textChanged(const QString &arg1)
{
    QSettings settings(UI_ELEMENTS_INI_FILE,QSettings::IniFormat);
    settings.setValue("audio_mic_device", arg1 );
    uiElement.audioMixerInputDevice = arg1;
}

