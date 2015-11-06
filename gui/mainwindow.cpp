/*
 * MCPE Viz Helper/GUI
 * (c) Plethora777 -- 2015.11.05
 *
 * GPL'ed code - please see LICENSE
 *
 * todo
 *   auto-check for update on github?
 *
 */

#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QDesktopServices>
#include <QSettings>
#include <QUrl>
#include <vector>
#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    this->setWindowTitle(tr("MCPE Viz Helper by Plethora777"));

    QLabel *label = new QLabel(tr("<html><a href=\"https://github.com/Plethora777/mcpe_viz\">MCPE Viz Helper</a> by Plethora777</html>"));
    label->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(true);
    statusBar()->addPermanentWidget(label);

    mcpeVizProcess = NULL;

#ifdef Q_OS_WIN32
    m_sSettingsFile = QApplication::applicationDirPath() + "\\mcpe_viz_helper.ini";
#else
    m_sSettingsFile = QApplication::applicationDirPath() + "/mcpe_viz_helper.ini";
#endif
    
    loadSettings();
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::loadSettings() {
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    QString sText;

    sText = settings.value("dirMCPEWorld", "").toString();
    ui->txtMCPEWorld->setText(sText);

    sText = settings.value("dirOutput", "").toString();
    ui->txtOutputDirectory->setText(sText);

    sText = settings.value("outputName", "").toString();
    ui->txtOutputName->setText(sText);

    int modeId = settings.value("mode", "0").toInt();
    ui->cbImages->setCurrentIndex(modeId);
}

void MainWindow::saveSettings() {
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.setValue("dirMCPEWorld", ui->txtMCPEWorld->text());
    settings.setValue("dirOutput", ui->txtOutputDirectory->text());
    settings.setValue("outputName", ui->txtOutputName->text());
    settings.setValue("mode", ui->cbImages->currentIndex());
}

int MainWindow::getCommandLine(std::string &cmd) {
    cmd = "";
    typedef std::vector<std::string> StringList;
    StringList cmd_args;
    StringList err_list;
    QFileInfo fi;

    // check that all is well
    fi = QFileInfo(ui->txtMCPEWorld->text());
    QString dirDb = fi.absolutePath();
    if (dirDb.length() > 0) {
        cmd_args.push_back("--db " + dirDb.toStdString());
    } else {
        err_list.push_back("Invalid MCPE World directory");
    }

    fi = QFileInfo(ui->txtOutputDirectory->text());
    if ( ! fi.isDir() ) {
        err_list.push_back("Output Directory is not a directory");
    }
    if ( ! fi.isWritable() ) {
        err_list.push_back("We cannot write to Output Directory");
    }

    QString dirOutput = ui->txtOutputDirectory->text();
    QString outputName = QFileInfo(ui->txtOutputName->text()).baseName();
    if (dirOutput.length() > 0 && outputName.length() > 0) {
        cmd_args.push_back("--out " + dirOutput.toStdString() + "/" + outputName.toStdString());
    } else {
        err_list.push_back("Invalid Output Directory and/or Output Name");
    }

    // make sure that dirOutput and dirDb are different
    if ( dirOutput == dirDb ) {
        err_list.push_back("Output directory cannot be the same as MCPE World directory");
    }

    // todo - super brittle way to do this
    int mode = ui->cbImages->currentIndex();
    switch (mode) {
    case 0:
        // web most
        cmd_args.push_back("--html-most");
        break;
    case 1:
        // web all
        cmd_args.push_back("--html-all");
        break;
    case 2:
        // image most
        cmd_args.push_back("--all-image");
        break;
    case 3:
        //image all
        cmd_args.push_back("--all-image --slices");
        break;
     default:
        err_list.push_back("Invalid comboBox setting");
        break;
    }

    if (err_list.size() > 0) {
        QString msg="";
        for (StringList::iterator it=err_list.begin(); it != err_list.end(); ++it) {
            msg.append(it->c_str());
            msg.append("\n");
        }
        QMessageBox::warning(this, "Error", msg);
        // todo - would be nice to highlight the fields with issues
        return -1;
    }

    // execute mcpe_viz.exe w/ the proper params and put output in txtProgress
#ifdef Q_OS_WIN32
    cmd="mcpe_viz.exe --flush";
#else
    cmd="./mcpe_viz --flush";
#endif

    for (StringList ::iterator it=cmd_args.begin(); it != cmd_args.end(); ++it) {
        cmd += " " + (*it);
    }
    return 0;
}


void MainWindow::processStandardOutput() {
    QTextCursor c = ui->txtProgress->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(mcpeVizProcess->readAllStandardOutput());
    ui->txtProgress->ensureCursorVisible();
}

void MainWindow::processStandardError() {
    QTextCursor c = ui->txtProgress->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(mcpeVizProcess->readAllStandardError());
    ui->txtProgress->ensureCursorVisible();
}

void MainWindow::processFinished(int exitCode,  QProcess::ExitStatus exitStatus) {
  // QApplication::restoreOverrideCursor();

    if (mcpeVizProcess->exitCode() == 0) {
        ui->txtProgress->appendHtml("<div style=\"color:green\">MCPE Viz completed successfully!<br/>You can launch the web app now.</div>");
        ui->btnWebApp->setEnabled(true);
    } else {
        QMessageBox::warning(this,"Error", "MCPE Viz failed.  See Progress window for details.");
    }

    delete mcpeVizProcess;
    mcpeVizProcess = NULL;
    ui->btnGo->setEnabled(true);
}

void MainWindow::on_btnGo_clicked()
{
    std::string cmd;
    ui->txtProgress->clear();
    ui->btnWebApp->setEnabled(false);
    ui->btnGo->setEnabled(false);
    int ret = getCommandLine(cmd);
    if ( ret == 0 ) {

        if (mcpeVizProcess) {
            delete mcpeVizProcess;
        }
        mcpeVizProcess = new QProcess(this);
        connect (mcpeVizProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(processStandardOutput()));  // connect process signals with your code
        connect (mcpeVizProcess, SIGNAL(readyReadStandardError()), this, SLOT(processStandardError()));  // same here
        connect (mcpeVizProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(processFinished(int,QProcess::ExitStatus)));  // same here
        //todo - signal error also?
        //QApplication::setOverrideCursor(Qt::WaitCursor);
        mcpeVizProcess->start(cmd.c_str());
        // todo - wait for start?
    } else {
        ui->btnGo->setEnabled(true);
    }
}

void MainWindow::on_btnWebApp_clicked()
{
    QString dirOutput = ui->txtOutputDirectory->text();
    QString outputName = QFileInfo(ui->txtOutputName->text()).baseName();
    if (dirOutput.length() > 0 && outputName.length() > 0) {
        std::string url = dirOutput.toStdString() + "/" + outputName.toStdString() + ".html";
        QDesktopServices::openUrl(QUrl(url.c_str()));
    } else {
        QMessageBox::warning(this,"Error", "Invalid Output Directory and/or Output Name");
    }
}

void MainWindow::on_action_About_triggered()
{
    QMessageBox::information(this, "About...",
                             "<html>MCPE Viz Helper by Plethora777<br/><br/>"
                             "<a href=\"https://github.com/Plethora777/mcpe_viz\">MCPE Viz on GitHub</a>"
                             "</html>"
                             );
}

void MainWindow::on_btnMCPEWorld_clicked()
{
    QFileDialog dialog(this, tr("Select MCPE World"), "", tr("MCPE World (level.dat)"));
    if (dialog.exec()) {
        QStringList fileNames = dialog.selectedFiles();
        ui->txtMCPEWorld->setText(fileNames[0]);
    }
}

void MainWindow::on_btnOutputDirectory_clicked()
{
    QFileDialog dialog(this, "Select Output Directory", "", "");
    dialog.setFileMode(QFileDialog::Directory);
    if (dialog.exec()) {
        QStringList fileNames = dialog.selectedFiles();
        ui->txtOutputDirectory->setText(fileNames[0]);
    }
}
