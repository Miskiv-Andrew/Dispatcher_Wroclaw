#include "traymanager.h"
#include <QApplication>
#include <QStyle>
#include <QPainter>
#include <QPixmap>

TrayManager::TrayManager(QObject *parent)
    : QObject(parent)
    , m_trayIcon(nullptr)
    , m_trayMenu(nullptr)
    , m_exitAction(nullptr)
{

    // Проверяем, что QApplication существует
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
        qWarning() << "TrayManager requires QApplication";
        return;
    }

    // Проверяем наличие системного трея
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "[TRAY] System tray is NOT available";
        return;
    }

    qDebug() << "[TRAY] System tray is available";

    // Створюємо іконку
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip("ModBusBridgeService");

    qDebug() << "[TRAY] QSystemTrayIcon created";

    // Створюємо контекстне меню
    m_trayMenu = new QMenu();

    // Пункт меню "Вихід"
    m_exitAction = new QAction("Вихід", this);
    connect(m_exitAction, &QAction::triggered, this, &TrayManager::onExitAction);
    m_trayMenu->addAction(m_exitAction);

    m_trayIcon->setContextMenu(m_trayMenu);

    // Підключаємо сигнал активації (клік по іконці)
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayManager::onTrayActivated);

    // Початковий стан — червоний (немає підключень)
    updateStatus(false, false);

    // Показуємо іконку
    // m_trayIcon->show();

    // Диагностика перед показом иконки
    qDebug() << "[TRAY] calling show()";

    // Показываем иконку в системном трее
    m_trayIcon->show();

    // Проверяем, считает ли Qt иконку видимой
    qDebug() << "[TRAY] isVisible =" << m_trayIcon->isVisible();
}

TrayManager::~TrayManager()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
        // m_trayIcon->deleteLater();
        // m_trayIcon = nullptr;
    }
}


void TrayManager::updateStatus(bool connectedToPLC, bool hasClient)
{
    // ---------------------------------------------------------
    // Проверяем, что объект QSystemTrayIcon уже создан.
    // ---------------------------------------------------------
    if (!m_trayIcon)
        return;


    // ---------------------------------------------------------
    // Создаём стандартное полотно 32x32.
    //
    // Сам индикатор будет прямоугольным,
    // но системная tray-иконка всё равно занимает
    // квадратную область.
    // ---------------------------------------------------------
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);


    // ---------------------------------------------------------
    // Создаём объект рисования.
    // ---------------------------------------------------------
    QPainter painter(&pixmap);

    painter.setRenderHint(QPainter::Antialiasing, true);


    // ---------------------------------------------------------
    // Цвета состояний.
    //
    // Зелёный  = подключено.
    // Красный  = не подключено.
    // ---------------------------------------------------------
    const QColor connectedColor(40, 200, 70);
    const QColor disconnectedColor(220, 50, 50);


    // ---------------------------------------------------------
    // Определяем цвет левой половины.
    //
    // Левая половина показывает состояние ПЛК.
    // ---------------------------------------------------------
    QColor plcColor;

    if (connectedToPLC)
        plcColor = connectedColor;
    else
        plcColor = disconnectedColor;


    // ---------------------------------------------------------
    // Определяем цвет правой половины.
    //
    // Правая половина показывает состояние Python-клиента.
    // ---------------------------------------------------------
    QColor pythonColor;

    if (hasClient)
        pythonColor = connectedColor;
    else
        pythonColor = disconnectedColor;


    // ---------------------------------------------------------
    // Размер всего индикатора.
    //
    // Он практически занимает всю ширину иконки,
    // поэтому будет заметнее предыдущего круглого варианта.
    // ---------------------------------------------------------
    const QRect indicatorRect(1, 8, 30, 16);


    // ---------------------------------------------------------
    // Левая половина — ПЛК.
    // ---------------------------------------------------------
    const QRect plcRect(
        indicatorRect.x(),
        indicatorRect.y(),
        indicatorRect.width() / 2,
        indicatorRect.height()
        );


    // ---------------------------------------------------------
    // Правая половина — Python.
    // ---------------------------------------------------------
    const QRect pythonRect(
        indicatorRect.x() + indicatorRect.width() / 2,
        indicatorRect.y(),
        indicatorRect.width() / 2,
        indicatorRect.height()
        );


    // ---------------------------------------------------------
    // Сначала рисуем левую половину.
    // ---------------------------------------------------------
    painter.setPen(Qt::NoPen);
    painter.setBrush(plcColor);

    painter.drawRect(plcRect);


    // ---------------------------------------------------------
    // Затем рисуем правую половину.
    // ---------------------------------------------------------
    painter.setBrush(pythonColor);

    painter.drawRect(pythonRect);


    // ---------------------------------------------------------
    // Рисуем белую рамку вокруг всего индикатора.
    //
    // Она делает значок заметным как на тёмной,
    // так и на светлой панели Windows.
    // ---------------------------------------------------------
    painter.setBrush(Qt::NoBrush);

    painter.setPen(
        QPen(
            Qt::white,
            2
            )
        );

    painter.drawRoundedRect(
        indicatorRect,
        3,
        3
        );


    // ---------------------------------------------------------
    // Рисуем разделительную линию между
    // состоянием ПЛК и Python.
    // ---------------------------------------------------------
    painter.setPen(
        QPen(
            Qt::white,
            2
            )
        );

    painter.drawLine(
        16,
        indicatorRect.top() + 1,
        16,
        indicatorRect.bottom() - 1
        );


    // ---------------------------------------------------------
    // Завершаем рисование.
    // ---------------------------------------------------------
    painter.end();


    // ---------------------------------------------------------
    // Передаём сформированную иконку в QSystemTrayIcon.
    // ---------------------------------------------------------
    m_trayIcon->setIcon(QIcon(pixmap));


    // ---------------------------------------------------------
    // Формируем подробную подсказку.
    //
    // При наведении пользователь сразу увидит,
    // что означает левая и правая части индикатора.
    // ---------------------------------------------------------
    QString plcStatus;

    if (connectedToPLC)
        plcStatus = "connected";
    else
        plcStatus = "no connection";


    QString pythonStatus;

    if (hasClient)
        pythonStatus = "connected";
    else
        pythonStatus = "no connectionя";


    // ---------------------------------------------------------
    // Устанавливаем tooltip.
    // ---------------------------------------------------------
    m_trayIcon->setToolTip(
        QString(
            "ModBusBridgeService\n"
            "ПЛК: %1\n"
            "Python: %2"
            )
            .arg(plcStatus)
            .arg(pythonStatus)
        );
}




void TrayManager::showMessage(const QString &title, const QString &message)
{
    if (m_trayIcon) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void TrayManager::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    // При подвійному кліку — нічого не робимо (можна додати показ логу)
    Q_UNUSED(reason);
}

void TrayManager::onExitAction()
{
    emit exitRequested();
    QCoreApplication::quit();
}
