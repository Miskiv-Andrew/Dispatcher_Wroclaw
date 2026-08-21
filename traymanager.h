#ifndef TRAYMANAGER_H
#define TRAYMANAGER_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

/**
 * @brief Клас для керування іконкою в системному треї
 */
class TrayManager : public QObject
{
    Q_OBJECT

public:
    explicit TrayManager(QObject *parent = nullptr);
    ~TrayManager();

    /**
     * @brief Встановлює колір іконки залежно від стану
     * @param connectedToPLC Підключення до ПЛК
     * @param hasClient Наявність підключеного Python-клієнта
     */
    void updateStatus(bool connectedToPLC, bool hasClient);

    /**
     * @brief Показує повідомлення в треї
     * @param title Заголовок
     * @param message Текст
     */
    void showMessage(const QString &title, const QString &message);

signals:
    void exitRequested();

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onExitAction();

private:
    QSystemTrayIcon *m_trayIcon;   ///< Іконка в треї
    QMenu *m_trayMenu;             ///< Контекстне меню
    QAction *m_exitAction;         ///< Пункт "Вихід"
};

#endif // TRAYMANAGER_H
