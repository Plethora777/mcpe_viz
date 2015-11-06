#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_btnMCPEWorld_clicked();

    void on_btnOutputDirectory_clicked();

    void on_btnOutputName_clicked() {};

    void on_btnGo_clicked();

    void on_btnWebApp_clicked();

    void on_action_About_triggered();

    void on_btnMCPEWorld_2_clicked() {};

    void on_btnOutputDirectory_2_clicked() {};

    void processStandardOutput();
    void processStandardError();
    void processFinished(int exitCode,  QProcess::ExitStatus exitStatus);

private:
    Ui::MainWindow *ui;
    QProcess *mcpeVizProcess;
    QString m_sSettingsFile;

    int getCommandLine(std::string &cmd);
    void saveSettings();
    void loadSettings();
};

#endif // MAINWINDOW_H
