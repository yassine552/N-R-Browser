#pragma once

#include <QDialog>

class BrowserApplication;
class QLabel;
class QListWidget;
class QPushButton;

// Downloads list with progress, states, open/cancel/clear actions.
class DownloadsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DownloadsDialog(BrowserApplication *app, QWidget *parent = nullptr);

private slots:
    void refresh();
    void openSelected();
    void cancelSelected();
    void clearFinished();

private:
    BrowserApplication *m_app;
    QListWidget *m_list = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
