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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemWatcher>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>
#include <QProcess>

#define NODECOUNT 6
#define CONNPOINTCOUNT 3
#define UI_MODE 0
#define VAULT_MODE 1


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(int a, QWidget *parent = nullptr);
    ~MainWindow();

private slots:

    void on_greenButton_clicked();
    void on_redButton_clicked();
    void on_route1Button_clicked();
    void on_route2Button_clicked();
    void on_route3Button_clicked();
    void on_contact1Button_clicked();
    void on_contact2Button_clicked();
    void on_contact3Button_clicked();
    void on_contact4Button_clicked();
    void on_contact5Button_clicked();
    void on_contact6Button_clicked();
    void fifoChanged(const QString & path);
    void fifoWrite(QString message);
    void readGpioButtons();
    void writeBackLight(QString value);
    void rampUp();
    void rampDown();
    void on_volumeSlider_valueChanged(int value);
    void on_pwrButton_clicked();
    void scanPeers();
    void on_commCheckButton_clicked();
    int msgFifoChanged(const QString & path);
    void on_denyButton_clicked();
    void on_eraseButton_clicked();
    void on_lineEdit_returnPressed();
    void on_pinButton_clear_clicked();
    void on_pinButton_1_clicked();
    void on_pinButton_2_clicked();
    void on_pinButton_3_clicked();
    void on_pinButton_4_clicked();
    void on_pinButton_5_clicked();
    void on_pinButton_6_clicked();
    void on_pinButton_7_clicked();
    void on_pinButton_8_clicked();
    void on_pinButton_9_clicked();
    void on_pinButton_a_clicked();
    void on_pinButton_b_clicked();
    int on_pinButton_c_clicked();
    void on_pinButton_0_clicked();
    void on_pinButton_hash_clicked();
    void setSystemVolume(int volume);
    void connectAsClient(QString nodeIp, QString nodeId);
    void touchLocalFile(QString filename);
    void removeLocalFile(QString filename);
    void disconnectAsClient(QString nodeIp, QString nodeId);
    void updateCallStatusIndicator(QString text, QString fontColor, QString backgroundColor,int logMethod);
    void on_answerButton_clicked();
    void setIndicatorForIncomingConnection(QString peerIp);
    void setContactButtons(bool state);
    void txKeyPresentageChanged();
    void rxKeyPresentageChanged();
    void beepBuzzer(int lenght);
    void beepBuzzerOff();
    void loadConnectionProfile();
    void saveAndActivateConnectionProfile(QString profile);
    void on_pinButton_pwr_clicked();
    void networkLatency();
    void reloadKeyUsage();
    long int get_file_size(QString keyFilename);
    long int get_key_index(QString counterFilename);
    void peerLatency();
    void saveUserPreferencesBeep(QString value);
    void on_exitButton_clicked();
    void scanAvailableWifiNetworks(QString command, QStringList parameters);
    void connectWifiNetwork(QString command, QStringList parameters);
    void getWifiStatus();
    void getKnownWifiNetworks();
    void on_scanWifiButton_clicked();
    void on_saveWifiButton_clicked();
    void on_networksComboBox_activated(int index);
    void on_deleteWifiButton_clicked();
    void on_wifiPasswordText_textChanged(const QString &arg1);
    void on_saveGatewayButton_clicked();
    int isValidIp4(char *str);
    void on_gatewayIpPortInput_textChanged(const QString &arg1);
    void on_autoeraseCheckbox_stateChanged(int arg1);
    void finalCountdown();
    void onVaultProcessReadyReadStdOutput();
    void onVaultProcessFinished();
    void exitVaultOpenProcess();
    void exitVaultOpenProcessWithFail();
    void on_camButton_clicked();
    void on_imageFrameCloseButton_clicked();
    void on_imageFrameTakePictureButton_clicked();
    void on_imageFrameSendPicture_clicked();
    void incomingImageChangeDetected();
    void incomingImageVerifyChange();
    void tearDownLocal();
    int waitForFifoReply();
    int checkFifoReplyTimeout();


    void on_audioDeviceInput_textChanged(const QString &arg1);

private:
    Ui::MainWindow *ui;
    QFileSystemWatcher * watcher;
    QFileSystemWatcher * msgWatcher;
    QFileSystemWatcher * txKeyWatcher;
    QFileSystemWatcher * rxKeyWatcher;

    QFileSystemWatcher * imageWatcher;

    /* System preferences */
    struct SPreferences
    {
        QString node_name[NODECOUNT];
        QString node_ip[NODECOUNT];
        QString node_id[NODECOUNT];
        QString myNodeId;
        QString myNodeIp;
        QString myNodeName;
        QString connectionPointName[CONNPOINTCOUNT];
        QString systemName;
        QString beepActive;
        QString connectionProfile;

    };
    SPreferences nodes;
    void loadSettings();


    /* User preferences */
    struct UserPreferences
    {
        QString volumeValue;
        QString m_pinCode;
        QString m_settingsPinCode;
        QString m_autoerase;
    };
    UserPreferences uPref;
    void loadUserPreferences();
    void saveUserPreferences();

    /* GPIO Notifier */
    QSocketNotifier * m_notify;
    int m_fd;
    bool backLightOn;

    struct uiStrings
    {
        QString messagingTitle;
        QString commCheckButton;
        QString eraseButton;
        QString secureVoiceInactiveNotify;
        QString secureVoiceActiveNotify;
        QString goSecureButton;
        QString terminateSecureButton;
        QString systemName;
        QString pinEntryTitleVault;
        QString pinEntryTitleVaultChecking;
        QString pinEntryTitleAccessPin;
        bool cameraButtonVisible;
        QString audioMixerOutputDevice;
    };
    uiStrings uiElement;
    void loadUserInterfacePreferences();
    QTimer *screenBlanktimer;
    QTimer *countdownTimer;
    QTimer *fifoReplyTimer;
    bool g_connectState;
    QString g_connectedNodeId;
    QString g_connectedNodeIp;
    QString g_remoteOtpPeerIp;
    QString g_fifoReply;
    bool g_fifoCheckInProgress;
    double rxKeyRemaining;
    double txKeyRemaining;
    QString txKeyRemainingString;
    QString rxKeyRemainingString;
    QTimer *envTimer;
    QString m_keyPersentage_incount[NODECOUNT];
    QString m_keyPersentage_outcount[NODECOUNT];
    QString m_keyStatusString[NODECOUNT];
    QString m_peerLatencyValue[NODECOUNT];



    /* Button Styles */
    QString s_goSecureButtonStyle_highlight = "QPushButton#greenButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 4px; \
            border-radius: 10px; \
            border-color: green; \
            color: lightgreen; \
            font:  bold 30px; \
            min-width: 5em; \
            padding: 6px; \
        } \
        QPushButton#greenButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";

    QString s_goSecureButtonStyle_normal = "QPushButton#greenButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
            color: green; \
            font:  bold 30px; \
            min-width: 5em; \
            padding: 6px; \
        } \
        QPushButton#greenButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";

    QString s_terminateButtonStyle_highlight="QPushButton#redButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 4px; \
            border-radius: 10px; \
            border-color: green; \
            color: lightgreen; \
            font:  bold 30px; \
            min-width: 5em; \
            padding: 6px; \
        } \
        QPushButton#redButton:pressed { \
            background-color: red; \
            border-style: inset; \
        }";


    QString s_terminateButtonStyle_normal="QPushButton#redButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
            color: green; \
            font:  bold 30px; \
            min-width: 5em; \
            padding: 6px; \
        } \
        QPushButton#redButton:pressed { \
            background-color: red; \
            border-style: inset; \
        }";




    int m_knownNetworkIndex;
    int m_startMode;

    QString m_buttonHighlightStyle = "QPushButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
            color: rgb(0,224, 0); \
            font: bold 30px; \
            min-width: 1em; \
            padding: 6px; \
        } \
        QPushButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";

    QString m_buttonNormalStyle = "QPushButton { \
        background-color: transparent; \
        border-style: outset; \
        border-width: 2px; \
        border-radius: 10px; \
        border-color: green; \
       color: green; \
        font: bold 30px; \
        min-width: 1em; \
        padding: 6px; \
    } \
    QPushButton:pressed { \
        background-color: rgb(0,224, 0); \
        border-style: inset; \
    }";

    QString m_wifiConnectStatusNormalStyle="background-color: transparent; \
        color: green; \
        font:   32px; \
        min-width: 1em; \
        padding: 3px;";
    QString m_wifiConnectStatusHighlightStyle="background-color: transparent; \
        color: lightgreen; \
        font:   32px; \
        min-width: 1em; \
        padding: 3px;";
    int m_finalCountdownValue=10;
    QProcess vaultOpenProcess;
    int m_imageFileSize;
    bool m_timerBlock=false;
    QString m_otpStausNormalStyle = " \
        QPushButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
           color: green; \
            font: bold 30px; \
            min-width: 1em; \
            padding: 6px; \
        } \
        QPushButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";

    QString m_otpStatusHighlightStyle = " \
        QPushButton { \
            background-color: transparent; \
            border-style: outset; \
            border-width: 2px; \
            border-radius: 10px; \
            border-color: green; \
           color: lightgreen; \
            font: bold 30px; \
            min-width: 1em; \
            padding: 6px; \
        } \
        QPushButton:pressed { \
            background-color: rgb(0,224, 0); \
            border-style: inset; \
        }";
        QString m_powerButtonDialogStyle = " \
            QLabel{width:450 px; font-size: 30px; color: lightgreen; background-color: rgb(0, 0, 0); } \
            QMessageBox { background-color: rgb(0, 0, 0); border: 5px solid green; } \
            QPushButton { \
                background-color: transparent; \
                border-style: outset; \
                border-width: 2px; \
                border-radius: 10px; \
                border-color: green; \
                color: green; \
                font: bold 32px; \
                min-width: 5em; \
                min-height: 2em; \
                padding: 6px; \
            } \
            QPushButton:pressed { \
                background-color: rgb(0,224, 0); \
                border-style: inset; \
            }";


};
#endif // MAINWINDOW_H
