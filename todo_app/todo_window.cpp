#include "todo_window.h"

#include "floating_focus_widget.h"
#include "schedule_table.h"
#include "study_calendar_widget.h"

#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QSqlError>
#include <QSqlQuery>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindowStateChangeEvent>

#include <algorithm>

namespace {
constexpr int CategoryRole = Qt::UserRole + 10;
constexpr int DueDateRole = Qt::UserRole + 11;
constexpr int EstimateMinutesRole = Qt::UserRole + 12;
}

TodoWindow::TodoWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("待办专注");
    resize(1520, 920);
    setMinimumSize(1180, 700);

    auto *root = new QWidget(this);
    auto *page = new QHBoxLayout(root);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(0);

    page->addWidget(createSidebar());
    page->addWidget(createDashboardPanel(), 1);
    page->addWidget(createDetailPanel());

    setCentralWidget(root);
    applyStyle();
    floatingFocus = new FloatingFocusWidget();
    floatingFocus->setRestoreHandler([this] { restoreFromFloating(); });
    floatingFocus->setToggleHandler([this] { toggleFocusTimer(); });
    initDatabase();
    connectUi();
    loadTasks();
    loadSchedule();
    resetFocusTimer();
    updateStatus();
    updateTaskDetail();
    updateTodayStudy();
    updateStudyCalendar();
    syncFloatingFocus();
}

void TodoWindow::applySoftShadow(QWidget *widget, int blur, int yOffset, int alpha) {
    auto *shadow = new QGraphicsDropShadowEffect(widget);
    shadow->setBlurRadius(blur);
    shadow->setOffset(0, yOffset);
    shadow->setColor(QColor(74, 84, 128, alpha));
    widget->setGraphicsEffect(shadow);
}

void TodoWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized()) {
        QTimer::singleShot(0, this, [this] { showFloatingFocus(); });
    }
}

void TodoWindow::closeEvent(QCloseEvent *event) {
    if (floatingFocus) floatingFocus->close();
    QMainWindow::closeEvent(event);
}

QWidget *TodoWindow::createSidebar() {
    auto *sidebar = new QFrame();
    sidebar->setObjectName("Sidebar");
    sidebar->setFixedWidth(268);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(22, 18, 18, 18);
    layout->setSpacing(10);

    auto *windowDots = new QHBoxLayout();
    for (const QString &color : {"#ff5f57", "#ffbd2e", "#28c840"}) {
        auto *dot = new QLabel("●");
        dot->setStyleSheet(QString("color: %1; font-size: 14px;").arg(color));
        windowDots->addWidget(dot);
    }
    windowDots->addStretch(1);
    layout->addLayout(windowDots);

    auto *brand = new QLabel("✓  TodoList");
    brand->setObjectName("Brand");
    layout->addWidget(brand);
    layout->addSpacing(16);

    auto *newTask = new QPushButton("＋  新建任务");
    newTask->setObjectName("NewTaskButton");
    connect(newTask, &QPushButton::clicked, this, [this] { taskInput->setFocus(); });
    layout->addWidget(newTask);
    layout->addSpacing(12);

    const QStringList navItems = {"◉  今天", "□  明天", "▣  即将到来", "☷  全部任务", "▦  日历打卡"};
    for (int index = 0; index < navItems.size(); ++index) {
        auto *button = new QPushButton(navItems[index]);
        button->setObjectName(index == 0 ? "NavActive" : "NavButton");
        layout->addWidget(button);
        if (index == 0) todayNavButton = button;
        if (index == 1) tomorrowNavButton = button;
        if (index == 2) upcomingNavButton = button;
        if (index == 3) allNavButton = button;
    }

    layout->addSpacing(16);
    auto *listTitle = new QLabel("清单");
    listTitle->setObjectName("SidebarSection");
    layout->addWidget(listTitle);
    const QList<QPair<QString, QLabel **>> categoryRows = {
        {"●  工作", &workCountLabel},
        {"●  学习", &studyCountLabel},
        {"●  个人", &personalCountLabel},
        {"●  购物清单", &shoppingCountLabel},
        {"●  旅行计划", &travelCountLabel}
    };
    for (const auto &row : categoryRows) {
        auto *line = new QHBoxLayout();
        auto *name = new QLabel(row.first);
        name->setObjectName("SidebarList");
        auto *count = new QLabel("0");
        count->setObjectName("SidebarList");
        line->addWidget(name, 1);
        line->addWidget(count);
        layout->addLayout(line);
        *row.second = count;
    }

    layout->addStretch(1);
    auto *stats = new QLabel("⌁  统计");
    stats->setObjectName("SidebarFooter");
    auto *settings = new QLabel("⚙  设置");
    settings->setObjectName("SidebarFooter");
    layout->addWidget(stats);
    layout->addWidget(settings);
    layout->addSpacing(12);
    auto *profile = new QLabel("●  Cynthia                 ⌄");
    profile->setObjectName("Profile");
    layout->addWidget(profile);
    return sidebar;
}

QWidget *TodoWindow::createTasksPanel() {
    auto *panel = new QFrame();
    panel->setObjectName("Panel");
    applySoftShadow(panel, 26, 8, 16);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    auto *panelTitle = new QLabel("任务清单");
    panelTitle->setObjectName("PanelTitle");
    layout->addWidget(panelTitle);

    auto *filters = new QHBoxLayout();
    filters->setSpacing(16);
    allFilterLabel = new QLabel("全部 0");
    urgentFilterLabel = new QLabel("今日 0");
    importantFilterLabel = new QLabel("本周 0");
    otherFilterLabel = new QLabel("已完成 0");
    allFilterLabel->setObjectName("FilterActive");
    urgentFilterLabel->setObjectName("Filter");
    importantFilterLabel->setObjectName("Filter");
    otherFilterLabel->setObjectName("Filter");
    filters->addWidget(allFilterLabel);
    filters->addWidget(urgentFilterLabel);
    filters->addWidget(importantFilterLabel);
    filters->addWidget(otherFilterLabel);
    filters->addStretch(1);
    layout->addLayout(filters);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(10);

    taskInput = new QLineEdit();
    taskInput->setPlaceholderText("输入新的待办事项");
    taskInput->setClearButtonEnabled(true);
    inputRow->addWidget(taskInput, 1);

    addButton = new QPushButton("添加");
    addButton->setDefault(true);
    addButton->setObjectName("PrimaryButton");
    inputRow->addWidget(addButton);
    layout->addLayout(inputRow);

    list = new QListWidget();
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(list, 1);

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(10);

    statusLabel = new QLabel();
    statusLabel->setObjectName("MutedText");
    bottomRow->addWidget(statusLabel, 1);

    deleteButton = new QPushButton("删除");
    clearDoneButton = new QPushButton("清除已完成");
    bottomRow->addWidget(deleteButton);
    bottomRow->addWidget(clearDoneButton);
    layout->addLayout(bottomRow);

    return panel;
}

QWidget *TodoWindow::createMetricCard(const QString &title, const QString &value, const QString &caption, const QString &accent) {
    auto *card = new QFrame();
    card->setObjectName("MetricCard");
    card->setMinimumHeight(118);
    applySoftShadow(card, 24, 8, 18);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(7);

    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName("MetricTitle");
    layout->addWidget(titleLabel);

    auto *valueLabel = new QLabel(value);
    valueLabel->setObjectName("MetricValue");
    valueLabel->setStyleSheet(QString("color: %1;").arg(accent));
    layout->addWidget(valueLabel);
    if (title == "今日进度") progressMetricLabel = valueLabel;
    if (title == "今日专注") focusMetricLabel = valueLabel;
    if (title == "连续打卡") streakMetricLabel = valueLabel;
    if (title == "预计用时") estimateMetricLabel = valueLabel;

    auto *captionLabel = new QLabel(caption);
    captionLabel->setObjectName("MutedText");
    layout->addWidget(captionLabel);
    if (title == "今日进度") progressMetricCaptionLabel = captionLabel;
    if (title == "今日专注") focusMetricCaptionLabel = captionLabel;
    if (title == "连续打卡") streakMetricCaptionLabel = captionLabel;
    if (title == "预计用时") estimateMetricCaptionLabel = captionLabel;
    return card;
}

QWidget *TodoWindow::createDashboardPanel() {
    auto *dashboard = new QFrame();
    dashboard->setObjectName("Dashboard");
    auto *layout = new QVBoxLayout(dashboard);
    layout->setContentsMargins(28, 22, 28, 22);
    layout->setSpacing(16);

    auto *header = new QHBoxLayout();
    auto *titleBlock = new QVBoxLayout();
    auto *title = new QLabel("今天  ☀");
    title->setObjectName("AppTitle");
    auto *subtitle = new QLabel(QDate::currentDate().toString("M月d日  dddd"));
    subtitle->setObjectName("Subtitle");
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    header->addLayout(titleBlock, 1);
    searchInput = new QLineEdit();
    searchInput->setPlaceholderText("⌕  搜索任务");
    searchInput->setFixedWidth(230);
    header->addWidget(searchInput);
    summaryLabel = new QLabel();
    summaryLabel->setObjectName("SummaryPill");
    header->addWidget(summaryLabel);
    layout->addLayout(header);

    auto *metrics = new QHBoxLayout();
    metrics->setSpacing(12);
    metrics->addWidget(createMetricCard("今日进度", "0%", "0 / 0 已完成", "#5667f2"), 1);
    metrics->addWidget(createMetricCard("今日专注", "0.0 h", "0 分钟", "#26a47b"), 1);
    metrics->addWidget(createMetricCard("连续打卡", "0 天", "最佳记录 0 天", "#f29d49"), 1);
    metrics->addWidget(createMetricCard("预计用时", "0.0 h", "剩余 0 分钟", "#6574d9"), 1);
    layout->addLayout(metrics);

    layout->addWidget(createTasksPanel(), 3);

    auto *bottomSplitter = new QSplitter(Qt::Horizontal);
    bottomSplitter->setObjectName("BottomSplitter");
    bottomSplitter->addWidget(createCalendarPanel());
    bottomSplitter->addWidget(createSchedulePanel());
    bottomSplitter->setSizes({430, 620});
    bottomSplitter->setChildrenCollapsible(false);
    layout->addWidget(bottomSplitter, 3);
    return dashboard;
}

QWidget *TodoWindow::createDetailPanel() {
    auto *panel = new QFrame();
    panel->setObjectName("DetailPanel");
    panel->setFixedWidth(356);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(22, 24, 22, 20);
    layout->setSpacing(14);

    auto *eyebrow = new QLabel("任务详情");
    eyebrow->setObjectName("MutedText");
    layout->addWidget(eyebrow);

    detailTitleLabel = new QLabel("选择一个任务");
    detailTitleLabel->setObjectName("DetailTitle");
    detailTitleLabel->setWordWrap(true);
    layout->addWidget(detailTitleLabel);

    detailStatusLabel = new QLabel("□  未完成");
    detailStatusLabel->setObjectName("Tag");
    layout->addWidget(detailStatusLabel);

    auto *divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName("Divider");
    layout->addWidget(divider);

    detailTitleInput = new QLineEdit();
    detailTitleInput->setPlaceholderText("任务标题");
    layout->addWidget(detailTitleInput);

    detailDateInput = new QDateEdit(QDate::currentDate());
    detailDateInput->setCalendarPopup(true);
    detailDateInput->setDisplayFormat("yyyy-MM-dd");
    layout->addWidget(detailDateInput);

    detailCategoryInput = new QComboBox();
    detailCategoryInput->addItems({"工作", "学习", "个人", "购物清单", "旅行计划"});
    layout->addWidget(detailCategoryInput);

    detailEstimateInput = new QSpinBox();
    detailEstimateInput->setRange(1, 480);
    detailEstimateInput->setSingleStep(5);
    detailEstimateInput->setSuffix(" 分钟");
    layout->addWidget(detailEstimateInput);

    detailSaveButton = new QPushButton("保存任务详情");
    detailSaveButton->setObjectName("PrimaryButton");
    detailToggleButton = new QPushButton("标记为已完成");
    layout->addWidget(detailSaveButton);
    layout->addWidget(detailToggleButton);

    auto *descriptionTitle = new QLabel("描述");
    descriptionTitle->setObjectName("PanelTitle");
    layout->addWidget(descriptionTitle);
    detailDescriptionLabel = new QLabel("选择任务后，这里会显示任务信息。你也可以双击左侧任务直接修改标题。");
    detailDescriptionLabel->setObjectName("MutedText");
    detailDescriptionLabel->setWordWrap(true);
    layout->addWidget(detailDescriptionLabel);

    layout->addSpacing(10);
    layout->addWidget(createFocusPanel());
    layout->addStretch(1);
    return panel;
}

QWidget *TodoWindow::createFocusPanel() {
    auto *panel = new QFrame();
    panel->setObjectName("FocusPanel");
    applySoftShadow(panel, 22, 6, 14);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto *panelTitle = new QLabel("专注计时");
    panelTitle->setObjectName("PanelTitle");
    layout->addWidget(panelTitle);

    focusTaskLabel = new QLabel("选择一个任务开始专注");
    focusTaskLabel->setObjectName("FocusTask");
    focusTaskLabel->setWordWrap(true);
    layout->addWidget(focusTaskLabel);

    timerLabel = new QLabel("25:00");
    timerLabel->setObjectName("TimerLabel");
    timerLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(timerLabel);

    focusProgress = new QProgressBar();
    focusProgress->setRange(0, 1000);
    focusProgress->setTextVisible(false);
    layout->addWidget(focusProgress);

    auto *durationRow = new QHBoxLayout();
    durationRow->setSpacing(10);
    auto *durationLabel = new QLabel("分钟");
    durationLabel->setObjectName("MutedText");
    durationRow->addWidget(durationLabel);
    minutesInput = new QSpinBox();
    minutesInput->setRange(1, 120);
    minutesInput->setSingleStep(1);
    minutesInput->setValue(25);
    durationRow->addWidget(minutesInput, 1);
    layout->addLayout(durationRow);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(10);
    startPauseButton = new QPushButton("开始");
    startPauseButton->setObjectName("PrimaryButton");
    resetButton = new QPushButton("重置");
    buttonRow->addWidget(startPauseButton, 1);
    buttonRow->addWidget(resetButton, 1);
    layout->addLayout(buttonRow);

    auto *stats = new QFrame();
    stats->setObjectName("StatsBox");
    auto *statsLayout = new QGridLayout(stats);
    statsLayout->setContentsMargins(14, 12, 14, 12);
    statsLayout->setHorizontalSpacing(14);
    statsLayout->setVerticalSpacing(4);

    focusStateLabel = new QLabel("准备中");
    focusStateLabel->setObjectName("StatValue");
    sessionsLabel = new QLabel("0");
    sessionsLabel->setObjectName("StatValue");
    todayStudyLabel = new QLabel("0 分钟");
    todayStudyLabel->setObjectName("StatValue");
    auto *stateCaption = new QLabel("状态");
    auto *sessionsCaption = new QLabel("专注次数");
    auto *todayCaption = new QLabel("今日学习");
    stateCaption->setObjectName("MutedText");
    sessionsCaption->setObjectName("MutedText");
    todayCaption->setObjectName("MutedText");
    statsLayout->addWidget(focusStateLabel, 0, 0);
    statsLayout->addWidget(sessionsLabel, 0, 1);
    statsLayout->addWidget(todayStudyLabel, 0, 2);
    statsLayout->addWidget(stateCaption, 1, 0);
    statsLayout->addWidget(sessionsCaption, 1, 1);
    statsLayout->addWidget(todayCaption, 1, 2);
    layout->addWidget(stats);

    focusTimer = new QTimer(this);
    focusTimer->setInterval(1000);
    return panel;
}

QWidget *TodoWindow::createSchedulePanel() {
    auto *panel = new QFrame();
    panel->setObjectName("Panel");
    applySoftShadow(panel, 24, 8, 14);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *titleRow = new QHBoxLayout();
    auto *panelTitle = new QLabel("一周行程表");
    panelTitle->setObjectName("PanelTitle");
    titleRow->addWidget(panelTitle, 1);
    auto *hint = new QLabel("空白拖动新建；拖已有行程移动；Ctrl 拖复制；长按拖调整；右键删除");
    hint->setObjectName("MutedText");
    titleRow->addWidget(hint);
    layout->addLayout(titleRow);

    scheduleTable = new ScheduleTable();
    scheduleTable->setRowCount(15);
    scheduleTable->setColumnCount(7);
    scheduleTable->setHorizontalHeaderLabels({"周一", "周二", "周三", "周四", "周五", "周六", "周日"});

    QStringList hours;
    for (int hour = 8; hour <= 22; ++hour) {
        hours << QString("%1:00").arg(hour, 2, 10, QLatin1Char('0'));
    }
    scheduleTable->setVerticalHeaderLabels(hours);
    scheduleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    scheduleTable->verticalHeader()->setDefaultSectionSize(34);
    scheduleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    scheduleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scheduleTable->setAlternatingRowColors(false);
    scheduleTable->setContextMenuPolicy(Qt::CustomContextMenu);
    scheduleTable->setDragFinishedHandler([this](const ScheduleSelection &selection) {
        createScheduleItem(selection);
    });
    layout->addWidget(scheduleTable, 1);

    return panel;
}

QWidget *TodoWindow::createCalendarPanel() {
    auto *panel = new QFrame();
    panel->setObjectName("Panel");
    applySoftShadow(panel, 22, 7, 14);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    auto *panelTitle = new QLabel("学习日历");
    panelTitle->setObjectName("PanelTitle");
    titleRow->addWidget(panelTitle, 1);
    layout->addLayout(titleRow);

    studyCalendar = new StudyCalendarWidget();
    studyCalendar->setSelectedDate(QDate::currentDate());
    layout->addWidget(studyCalendar, 1);

    auto *hint = new QLabel("有学习记录的日期会变暗");
    hint->setObjectName("MutedText");
    layout->addWidget(hint);

    return panel;
}

void TodoWindow::connectUi() {
    connect(addButton, &QPushButton::clicked, this, [this] { addTask(); });
    connect(taskInput, &QLineEdit::returnPressed, this, [this] { addTask(); });
    connect(deleteButton, &QPushButton::clicked, this, [this] { deleteSelectedTask(); });
    connect(clearDoneButton, &QPushButton::clicked, this, [this] { clearCompletedTasks(); });
    connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        updateFocusTask();
        updateStatus();
        updateTaskDetail();
    });
    connect(list, &QListWidget::itemChanged, this, [this](QListWidgetItem *changedItem) {
        if (!loading) {
            updateFocusTask();
            updateStatus();
            updateTaskDetail();
            updateTaskRowWidget(changedItem);
            saveTasks();
        }
    });
    connect(minutesInput, &QSpinBox::valueChanged, this, [this](int) {
        if (!focusRunning) {
            resetFocusTimer();
        }
    });
    connect(startPauseButton, &QPushButton::clicked, this, [this] { toggleFocusTimer(); });
    connect(resetButton, &QPushButton::clicked, this, [this] { resetFocusTimer(); });
    connect(focusTimer, &QTimer::timeout, this, [this] { tickFocusTimer(); });
    connect(studyCalendar, &QCalendarWidget::currentPageChanged, this, [this](int, int) { updateStudyCalendar(); });
    connect(scheduleTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        showScheduleContextMenu(position);
    });
    connect(searchInput, &QLineEdit::textChanged, this, [this](const QString &) { applyTaskFilter(); });
    connect(detailSaveButton, &QPushButton::clicked, this, [this] { saveSelectedTaskDetail(); });
    connect(detailToggleButton, &QPushButton::clicked, this, [this] { toggleSelectedTaskCompletion(); });
    connect(todayNavButton, &QPushButton::clicked, this, [this] { taskFilterMode = 0; applyTaskFilter(); });
    connect(tomorrowNavButton, &QPushButton::clicked, this, [this] { taskFilterMode = 1; applyTaskFilter(); });
    connect(upcomingNavButton, &QPushButton::clicked, this, [this] { taskFilterMode = 2; applyTaskFilter(); });
    connect(allNavButton, &QPushButton::clicked, this, [this] { taskFilterMode = 3; applyTaskFilter(); });
}

QString TodoWindow::appDataDirPath() const {
    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dirPath.isEmpty()) {
        dirPath = QCoreApplication::applicationDirPath();
    }

    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dirPath;
}

QString TodoWindow::databaseFilePath() const {
    return QDir(appDataDirPath()).filePath("todo_focus.db");
}

QString TodoWindow::dataFilePath() const {
    return QDir(appDataDirPath()).filePath("tasks.json");
}

void TodoWindow::initDatabase() {
    database = QSqlDatabase::addDatabase("QSQLITE");
    database.setDatabaseName(databaseFilePath());

    if (!database.open()) {
        QMessageBox::warning(this, "数据库错误", database.lastError().text());
        return;
    }

    QSqlQuery query(database);
    query.exec("CREATE TABLE IF NOT EXISTS focus_sessions ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "task_text TEXT NOT NULL,"
               "duration_seconds INTEGER NOT NULL,"
               "finished_at TEXT NOT NULL)");
    query.exec("CREATE TABLE IF NOT EXISTS schedule_items ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "day_of_week INTEGER NOT NULL,"
               "start_hour INTEGER NOT NULL,"
               "end_hour INTEGER NOT NULL,"
               "title TEXT NOT NULL)");
    migrateDatabase();
}

void TodoWindow::migrateDatabase() {
    QSqlQuery query(database);
    query.exec("ALTER TABLE schedule_items ADD COLUMN start_day INTEGER");
    query.exec("ALTER TABLE schedule_items ADD COLUMN end_day INTEGER");
    query.exec("UPDATE schedule_items SET start_day = day_of_week WHERE start_day IS NULL");
    query.exec("UPDATE schedule_items SET end_day = day_of_week WHERE end_day IS NULL");
}

void TodoWindow::addTask() {
    const QString text = taskInput->text().trimmed();
    if (text.isEmpty()) {
        taskInput->setFocus();
        return;
    }

    addTaskItem(text, false);
    taskInput->clear();
    taskInput->setFocus();
    updateStatus();
    saveTasks();
}

void TodoWindow::addTaskItem(const QString &text, bool completed, const QString &category,
                             const QDate &dueDate, int estimateMinutes) {
    auto *item = new QListWidgetItem(text, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
    item->setCheckState(completed ? Qt::Checked : Qt::Unchecked);
    item->setData(CategoryRole, category);
    item->setData(DueDateRole, dueDate.toString(Qt::ISODate));
    item->setData(EstimateMinutesRole, estimateMinutes);
    updateTaskRowWidget(item);
}

void TodoWindow::deleteSelectedTask() {
    auto *item = list->currentItem();
    if (!item) {
        return;
    }

    delete list->takeItem(list->row(item));
    updateFocusTask();
    updateStatus();
    saveTasks();
}

void TodoWindow::clearCompletedTasks() {
    for (int row = list->count() - 1; row >= 0; --row) {
        auto *item = list->item(row);
        if (item->checkState() == Qt::Checked) {
            delete list->takeItem(row);
        }
    }

    updateFocusTask();
    updateStatus();
    saveTasks();
}

void TodoWindow::updateStatus() {
    int completed = 0;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->checkState() == Qt::Checked) {
            ++completed;
        }
    }

    const int total = list->count();
    const int active = total - completed;
    statusLabel->setText(QString("%1 个未完成 / %2 个已完成").arg(active).arg(completed));
    summaryLabel->setText(QString("剩余 %1 项").arg(active));
    deleteButton->setEnabled(list->currentItem() != nullptr);
    clearDoneButton->setEnabled(completed > 0);
    updateDashboardMetrics();
    updateSidebarCounts();
    updateFilterLabels();
}

void TodoWindow::updateFocusTask() {
    const auto *item = list->currentItem();
    if (!item) {
        focusTaskLabel->setText("选择一个任务开始专注");
        return;
    }

    QString text = item->text().trimmed();
    if (text.isEmpty()) {
        text = "未命名任务";
    }
    focusTaskLabel->setText(text);
    syncFloatingFocus();
}

void TodoWindow::updateTaskDetail() {
    if (!detailTitleLabel || !detailStatusLabel || !detailDescriptionLabel) {
        return;
    }

    const auto *item = list->currentItem();
    if (!item) {
        detailTitleLabel->setText("选择一个任务");
        detailStatusLabel->setText("□  未完成");
        detailTitleInput->clear();
        detailDateInput->setDate(QDate::currentDate());
        detailCategoryInput->setCurrentText("学习");
        detailEstimateInput->setValue(25);
        detailSaveButton->setEnabled(false);
        detailToggleButton->setEnabled(false);
        detailDescriptionLabel->setText("选择任务后，这里会显示任务信息。你也可以双击任务直接修改标题。");
        return;
    }

    detailTitleLabel->setText(item->text());
    const bool completed = item->checkState() == Qt::Checked;
    detailStatusLabel->setText(completed ? "✓  已完成" : "□  未完成");
    detailTitleInput->setText(item->text());
    detailDateInput->setDate(QDate::fromString(item->data(DueDateRole).toString(), Qt::ISODate));
    detailCategoryInput->setCurrentText(item->data(CategoryRole).toString());
    detailEstimateInput->setValue(item->data(EstimateMinutesRole).toInt());
    detailSaveButton->setEnabled(true);
    detailToggleButton->setEnabled(true);
    detailToggleButton->setText(completed ? "标记为未完成" : "标记为已完成");
    detailDescriptionLabel->setText(completed
        ? "这个任务已经完成。可以在任务清单中取消勾选，继续处理。"
        : "专注处理这个任务时，可以使用下方计时器；完成后在任务清单中勾选。");
}

void TodoWindow::updateTaskRowWidget(QListWidgetItem *item) {
    if (!item || !list) return;

    auto *row = new QWidget();
    row->setObjectName("TaskRow");
    applySoftShadow(row, 18, 4, 12);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 7, 14, 7);
    layout->setSpacing(12);

    auto *check = new QCheckBox();
    check->setChecked(item->checkState() == Qt::Checked);
    layout->addWidget(check);

    auto *title = new QLabel(item->text());
    title->setObjectName(item->checkState() == Qt::Checked ? "TaskTitleDone" : "TaskTitle");
    title->setMinimumWidth(260);
    layout->addWidget(title, 1);

    const QDate dueDate = QDate::fromString(item->data(DueDateRole).toString(), Qt::ISODate);
    const QString category = item->data(CategoryRole).toString();
    const int estimate = item->data(EstimateMinutesRole).toInt();
    const bool urgent = dueDate.isValid() && dueDate <= QDate::currentDate();

    auto *priority = new QLabel(urgent ? "紧急" : "计划");
    priority->setObjectName(urgent ? "ChipUrgent" : "ChipSoft");
    layout->addWidget(priority);

    auto *categoryChip = new QLabel(category.isEmpty() ? "学习" : category);
    categoryChip->setObjectName(category == "工作" ? "ChipWork" : "ChipStudy");
    layout->addWidget(categoryChip);

    QString dueText = dueDate.isValid() ? dueDate.toString("今天 M月d日") : "未定";
    if (dueDate.isValid() && dueDate != QDate::currentDate()) {
        dueText = dueDate.toString("M月d日");
    }
    auto *time = new QLabel(QString("%1  %2分钟").arg(dueText).arg(estimate));
    time->setObjectName("TaskTime");
    layout->addWidget(time);

    auto *star = new QLabel(urgent ? "★" : "☆");
    star->setObjectName(urgent ? "StarOn" : "StarOff");
    layout->addWidget(star);

    item->setSizeHint(QSize(0, 54));
    list->setItemWidget(item, row);

    connect(check, &QCheckBox::toggled, this, [this, item](bool checked) {
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    });
}

void TodoWindow::refreshTaskRows() {
    for (int row = 0; row < list->count(); ++row) {
        updateTaskRowWidget(list->item(row));
    }
}

void TodoWindow::saveSelectedTaskDetail() {
    auto *item = list->currentItem();
    if (!item) return;

    const QString text = detailTitleInput->text().trimmed();
    if (text.isEmpty()) return;
    item->setText(text);
    item->setData(DueDateRole, detailDateInput->date().toString(Qt::ISODate));
    item->setData(CategoryRole, detailCategoryInput->currentText());
    item->setData(EstimateMinutesRole, detailEstimateInput->value());
    saveTasks();
    updateTaskRowWidget(item);
    applyTaskFilter();
    updateTaskDetail();
    updateStatus();
}

void TodoWindow::toggleSelectedTaskCompletion() {
    auto *item = list->currentItem();
    if (!item) return;
    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void TodoWindow::applyTaskFilter() {
    const QDate today = QDate::currentDate();
    const QString searchText = searchInput ? searchInput->text().trimmed() : QString();
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        const QDate dueDate = QDate::fromString(item->data(DueDateRole).toString(), Qt::ISODate);
        bool visible = true;
        if (taskFilterMode == 0) visible = dueDate == today;
        if (taskFilterMode == 1) visible = dueDate == today.addDays(1);
        if (taskFilterMode == 2) visible = dueDate > today.addDays(1);
        if (!searchText.isEmpty()) visible = visible && item->text().contains(searchText, Qt::CaseInsensitive);
        item->setHidden(!visible);
    }
    const QList<QPushButton *> buttons = {todayNavButton, tomorrowNavButton, upcomingNavButton, allNavButton};
    for (int index = 0; index < buttons.size(); ++index) {
        buttons[index]->setObjectName(index == taskFilterMode ? "NavActive" : "NavButton");
        buttons[index]->style()->unpolish(buttons[index]);
        buttons[index]->style()->polish(buttons[index]);
    }
}

void TodoWindow::updateDashboardMetrics() {
    int total = 0;
    int completed = 0;
    int remainingMinutes = 0;
    const QDate today = QDate::currentDate();
    int tomorrowCount = 0;
    int upcomingCount = 0;
    for (int row = 0; row < list->count(); ++row) {
        const auto *item = list->item(row);
        const QDate dueDate = QDate::fromString(item->data(DueDateRole).toString(), Qt::ISODate);
        if (dueDate == today.addDays(1)) ++tomorrowCount;
        if (dueDate > today.addDays(1)) ++upcomingCount;
        if (dueDate != today) continue;
        ++total;
        if (item->checkState() == Qt::Checked) {
            ++completed;
        } else {
            remainingMinutes += item->data(EstimateMinutesRole).toInt();
        }
    }
    if (progressMetricLabel) {
        progressMetricLabel->setText(total > 0 ? QString("%1%").arg(completed * 100 / total) : "0%");
    }
    if (progressMetricCaptionLabel) {
        progressMetricCaptionLabel->setText(QString("%1 / %2 已完成").arg(completed).arg(total));
    }
    if (estimateMetricLabel) {
        estimateMetricLabel->setText(QString::number(remainingMinutes / 60.0, 'f', 1) + " h");
    }
    if (estimateMetricCaptionLabel) {
        estimateMetricCaptionLabel->setText(QString("剩余 %1 分钟").arg(remainingMinutes));
    }
    if (todayNavButton) todayNavButton->setText(QString("◉  今天                 %1").arg(total));
    if (tomorrowNavButton) tomorrowNavButton->setText(QString("□  明天                 %1").arg(tomorrowCount));
    if (upcomingNavButton) upcomingNavButton->setText(QString("▣  即将到来          %1").arg(upcomingCount));
    if (allNavButton) allNavButton->setText(QString("☷  全部任务          %1").arg(list->count()));

    int totalSeconds = 0;
    QSet<QDate> studiedDates;
    if (database.isOpen()) {
        QSqlQuery query(database);
        query.prepare("SELECT COALESCE(SUM(duration_seconds), 0) FROM focus_sessions WHERE finished_at LIKE :today");
        query.bindValue(":today", today.toString(Qt::ISODate) + "%");
        query.exec();
        if (query.next()) totalSeconds = query.value(0).toInt();

        query.exec("SELECT DISTINCT substr(finished_at, 1, 10) FROM focus_sessions");
        while (query.next()) studiedDates.insert(QDate::fromString(query.value(0).toString(), Qt::ISODate));
    }
    if (focusMetricLabel) focusMetricLabel->setText(QString::number(totalSeconds / 3600.0, 'f', 1) + " h");
    if (focusMetricCaptionLabel) focusMetricCaptionLabel->setText(QString("%1 分钟").arg(totalSeconds / 60));

    int streak = 0;
    for (QDate date = today; studiedDates.contains(date); date = date.addDays(-1)) ++streak;
    int bestStreak = 0;
    int currentRun = 0;
    QList<QDate> sortedDates = studiedDates.values();
    std::sort(sortedDates.begin(), sortedDates.end());
    QDate previous;
    for (const QDate &date : sortedDates) {
        if (!previous.isValid() || previous.daysTo(date) == 1) {
            ++currentRun;
        } else {
            currentRun = 1;
        }
        bestStreak = std::max(bestStreak, currentRun);
        previous = date;
    }
    if (streakMetricLabel) streakMetricLabel->setText(QString("%1 天").arg(streak));
    if (streakMetricCaptionLabel) streakMetricCaptionLabel->setText(QString("最佳记录 %1 天").arg(bestStreak));
}

void TodoWindow::updateSidebarCounts() {
    int work = 0, study = 0, personal = 0, shopping = 0, travel = 0;
    for (int row = 0; row < list->count(); ++row) {
        const QString category = list->item(row)->data(CategoryRole).toString();
        if (category == "工作") ++work;
        else if (category == "学习") ++study;
        else if (category == "个人") ++personal;
        else if (category == "购物清单") ++shopping;
        else if (category == "旅行计划") ++travel;
    }
    if (workCountLabel) workCountLabel->setText(QString::number(work));
    if (studyCountLabel) studyCountLabel->setText(QString::number(study));
    if (personalCountLabel) personalCountLabel->setText(QString::number(personal));
    if (shoppingCountLabel) shoppingCountLabel->setText(QString::number(shopping));
    if (travelCountLabel) travelCountLabel->setText(QString::number(travel));
}

void TodoWindow::updateFilterLabels() {
    int todayCount = 0, weekCount = 0, completedCount = 0;
    const QDate today = QDate::currentDate();
    for (int row = 0; row < list->count(); ++row) {
        const auto *item = list->item(row);
        const QDate dueDate = QDate::fromString(item->data(DueDateRole).toString(), Qt::ISODate);
        if (dueDate == today) ++todayCount;
        if (dueDate >= today && dueDate <= today.addDays(6)) ++weekCount;
        if (item->checkState() == Qt::Checked) ++completedCount;
    }
    if (allFilterLabel) allFilterLabel->setText(QString("全部 %1").arg(list->count()));
    if (urgentFilterLabel) urgentFilterLabel->setText(QString("今日 %1").arg(todayCount));
    if (importantFilterLabel) importantFilterLabel->setText(QString("本周 %1").arg(weekCount));
    if (otherFilterLabel) otherFilterLabel->setText(QString("已完成 %1").arg(completedCount));
}

QString TodoWindow::currentFocusTaskText() const {
    const auto *item = list->currentItem();
    if (item && !item->text().trimmed().isEmpty()) {
        return item->text().trimmed();
    }
    return "未选择任务";
}

void TodoWindow::toggleFocusTimer() {
    if (focusRunning) {
        focusRunning = false;
        focusTimer->stop();
        startPauseButton->setText("继续");
        focusStateLabel->setText("已暂停");
        minutesInput->setEnabled(true);
        syncFloatingFocus();
        return;
    }

    if (focusRemainingSeconds <= 0) {
        resetFocusTimer();
    }

    focusRunning = true;
    focusTimer->start();
    startPauseButton->setText("暂停");
    focusStateLabel->setText("专注中");
    minutesInput->setEnabled(false);
    syncFloatingFocus();
}

void TodoWindow::resetFocusTimer() {
    focusRunning = false;
    focusTimer->stop();
    focusTotalSeconds = minutesInput ? minutesInput->value() * 60 : 25 * 60;
    focusRemainingSeconds = focusTotalSeconds;
    if (minutesInput) {
        minutesInput->setEnabled(true);
    }
    if (startPauseButton) {
        startPauseButton->setText("开始");
    }
    if (focusStateLabel) {
        focusStateLabel->setText("准备中");
    }
    updateTimerDisplay();
    syncFloatingFocus();
}

void TodoWindow::tickFocusTimer() {
    if (focusRemainingSeconds > 0) {
        --focusRemainingSeconds;
        updateTimerDisplay();
    }

    if (focusRemainingSeconds == 0) {
        focusRunning = false;
        focusTimer->stop();
        ++completedSessions;
        sessionsLabel->setText(QString::number(completedSessions));
        logFocusSession();
        startPauseButton->setText("开始");
        focusStateLabel->setText("已完成");
        minutesInput->setEnabled(true);
        syncFloatingFocus();
        QMessageBox::information(this, "专注完成", "做得好，这次专注已经完成。");
    }
}

void TodoWindow::updateTimerDisplay() {
    const int minutes = focusRemainingSeconds / 60;
    const int seconds = focusRemainingSeconds % 60;
    timerLabel->setText(QString("%1:%2")
                            .arg(minutes, 2, 10, QLatin1Char('0'))
                            .arg(seconds, 2, 10, QLatin1Char('0')));

    const int elapsed = focusTotalSeconds - focusRemainingSeconds;
    const int progress = focusTotalSeconds > 0 ? elapsed * 1000 / focusTotalSeconds : 0;
    focusProgress->setValue(progress);
    syncFloatingFocus();
}

void TodoWindow::showFloatingFocus() {
    if (!floatingFocus) return;
    syncFloatingFocus();
    hide();
    floatingFocus->moveToScreenCorner();
    floatingFocus->show();
    floatingFocus->raise();
}

void TodoWindow::restoreFromFloating() {
    if (!floatingFocus) return;
    floatingFocus->hide();
    showNormal();
    activateWindow();
    raise();
}

void TodoWindow::syncFloatingFocus() {
    if (!floatingFocus || !timerLabel) return;
    floatingFocus->setTaskText(currentFocusTaskText());
    floatingFocus->setTimeText(timerLabel->text());
    floatingFocus->setRunning(focusRunning);
}

void TodoWindow::logFocusSession() {
    if (!database.isOpen()) {
        return;
    }

    QSqlQuery query(database);
    query.prepare("INSERT INTO focus_sessions (task_text, duration_seconds, finished_at) "
                  "VALUES (:task_text, :duration_seconds, :finished_at)");
    query.bindValue(":task_text", currentFocusTaskText());
    query.bindValue(":duration_seconds", focusTotalSeconds);
    query.bindValue(":finished_at", QDateTime::currentDateTime().toString(Qt::ISODate));
    query.exec();
    updateTodayStudy();
    updateStudyCalendar();
    updateDashboardMetrics();
}

void TodoWindow::updateTodayStudy() {
    if (!todayStudyLabel || !database.isOpen()) {
        return;
    }

    QSqlQuery query(database);
    query.prepare("SELECT COALESCE(SUM(duration_seconds), 0) FROM focus_sessions "
                  "WHERE finished_at LIKE :today");
    query.bindValue(":today", QDate::currentDate().toString(Qt::ISODate) + "%");
    query.exec();

    int totalSeconds = 0;
    if (query.next()) {
        totalSeconds = query.value(0).toInt();
    }

    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    if (hours > 0) {
        todayStudyLabel->setText(QString("%1 小时 %2 分钟").arg(hours).arg(minutes));
    } else {
        todayStudyLabel->setText(QString("%1 分钟").arg(minutes));
    }
}

void TodoWindow::updateStudyCalendar() {
    if (!studyCalendar) {
        return;
    }

    QSet<QDate> studiedDates;
    if (database.isOpen()) {
        QSqlQuery query(database);
        query.exec("SELECT substr(finished_at, 1, 10), SUM(duration_seconds) "
                   "FROM focus_sessions GROUP BY substr(finished_at, 1, 10)");
        while (query.next()) {
            const QDate date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
            if (date.isValid() && query.value(1).toInt() > 0) {
                studiedDates.insert(date);
            }
        }
    }

    studyCalendar->setStudiedDates(studiedDates);
}

void TodoWindow::createScheduleItem(const ScheduleSelection &selection) {
    if (!scheduleTable || selection.startColumn < 0 || selection.startRow < 0) {
        return;
    }

    const int startHour = 8 + selection.startRow;
    const int endHour = 8 + selection.endRow + 1;

    if (selection.operation != ScheduleDragOperation::NewItem && selection.sourceItemId <= 0) {
        scheduleTable->clearSelection();
        return;
    }

    ScheduleSelection target = selection;
    if (selection.operation == ScheduleDragOperation::MoveItem ||
        selection.operation == ScheduleDragOperation::CopyItem) {
        int oldLeft = scheduleTable->columnCount();
        int oldRight = -1;
        int oldTop = scheduleTable->rowCount();
        int oldBottom = -1;
        for (int row = 0; row < scheduleTable->rowCount(); ++row) {
            for (int column = 0; column < scheduleTable->columnCount(); ++column) {
                auto *item = scheduleTable->item(row, column);
                if (item && item->data(Qt::UserRole).toInt() == selection.sourceItemId) {
                    oldLeft = std::min(oldLeft, column);
                    oldRight = std::max(oldRight, column);
                    oldTop = std::min(oldTop, row);
                    oldBottom = std::max(oldBottom, row);
                }
            }
        }

        if (oldRight < 0 || selection.dropColumn < 0 || selection.dropRow < 0) {
            scheduleTable->clearSelection();
            return;
        }

        const int width = oldRight - oldLeft;
        const int height = oldBottom - oldTop;
        target.startColumn = std::clamp(selection.dropColumn, 0, scheduleTable->columnCount() - 1);
        target.startRow = std::clamp(selection.dropRow, 0, scheduleTable->rowCount() - 1);
        target.endColumn = std::min(target.startColumn + width, scheduleTable->columnCount() - 1);
        target.endRow = std::min(target.startRow + height, scheduleTable->rowCount() - 1);

        if (selection.operation == ScheduleDragOperation::MoveItem &&
            target.startColumn == oldLeft && target.endColumn == oldRight &&
            target.startRow == oldTop && target.endRow == oldBottom) {
            scheduleTable->clearSelection();
            return;
        }
    }

    const int targetStartHour = 8 + target.startRow;
    const int targetEndHour = 8 + target.endRow + 1;
    QString title = selection.sourceTitle.trimmed();
    if (selection.operation == ScheduleDragOperation::NewItem) {
        title = currentFocusTaskText();
        if (title == "未选择任务") {
            title = "学习";
        }

        const QString startDay = scheduleTable->horizontalHeaderItem(selection.startColumn)->text();
        const QString endDay = scheduleTable->horizontalHeaderItem(selection.endColumn)->text();
        const QString rangeText = selection.startColumn == selection.endColumn
            ? QString("%1 %2:00-%3:00").arg(startDay).arg(startHour).arg(endHour)
            : QString("%1 到 %2，%3:00-%4:00").arg(startDay, endDay).arg(startHour).arg(endHour);

        bool ok = false;
        title = QInputDialog::getText(this, "创建行程", rangeText, QLineEdit::Normal, title, &ok).trimmed();
        if (!ok || title.isEmpty()) {
            scheduleTable->clearSelection();
            return;
        }
    } else {
        QString action = "移动";
        if (selection.operation == ScheduleDragOperation::CopyItem) {
            action = "复制";
        } else if (selection.operation == ScheduleDragOperation::ResizeItem) {
            action = "调整";
        }

        if (!confirmScheduleChange(action, target)) {
            scheduleTable->clearSelection();
            loadSchedule();
            return;
        }
    }

    int itemId = selection.sourceItemId;
    if (database.isOpen()) {
        if (selection.operation == ScheduleDragOperation::NewItem ||
            selection.operation == ScheduleDragOperation::CopyItem) {
        QSqlQuery query(database);
            query.prepare("INSERT INTO schedule_items "
                          "(day_of_week, start_day, end_day, start_hour, end_hour, title) "
                          "VALUES (:day, :start_day, :end_day, :start_hour, :end_hour, :title)");
            query.bindValue(":day", target.startColumn + 1);
            query.bindValue(":start_day", target.startColumn + 1);
            query.bindValue(":end_day", target.endColumn + 1);
            query.bindValue(":start_hour", targetStartHour);
            query.bindValue(":end_hour", targetEndHour);
            query.bindValue(":title", title);
            query.exec();
            itemId = query.lastInsertId().toInt();
        } else {
            QSqlQuery query(database);
            query.prepare("UPDATE schedule_items SET day_of_week = :day, start_day = :start_day, "
                          "end_day = :end_day, start_hour = :start_hour, end_hour = :end_hour "
                          "WHERE id = :id");
            query.bindValue(":day", target.startColumn + 1);
            query.bindValue(":start_day", target.startColumn + 1);
            query.bindValue(":end_day", target.endColumn + 1);
            query.bindValue(":start_hour", targetStartHour);
            query.bindValue(":end_hour", targetEndHour);
            query.bindValue(":id", selection.sourceItemId);
            query.exec();
            clearScheduleItemCells(selection.sourceItemId);
        }
    }

    paintScheduleItem(itemId, target.startColumn, target.endColumn, target.startRow, target.endRow, title);
    scheduleTable->clearSelection();
}

void TodoWindow::paintScheduleItem(int id, int startColumn, int endColumn, int top, int bottom, const QString &title) {
    for (int column = startColumn; column <= endColumn; ++column) {
        for (int row = top; row <= bottom; ++row) {
            auto *item = scheduleTable->item(row, column);
            if (!item) {
                item = new QTableWidgetItem();
                scheduleTable->setItem(row, column, item);
            }

            item->setText((row == top && column == startColumn) ? title : "");
            item->setToolTip(title);
            item->setData(Qt::UserRole, id);
            item->setBackground(QColor("#dbeafe"));
            item->setForeground(QColor("#17345f"));
        }
    }
}

void TodoWindow::clearScheduleItemCells(int id) {
    for (int row = 0; row < scheduleTable->rowCount(); ++row) {
        for (int column = 0; column < scheduleTable->columnCount(); ++column) {
            auto *item = scheduleTable->item(row, column);
            if (item && item->data(Qt::UserRole).toInt() == id) {
                delete scheduleTable->takeItem(row, column);
            }
        }
    }
}

bool TodoWindow::confirmScheduleChange(const QString &action, const ScheduleSelection &selection) const {
    const int startHour = 8 + selection.startRow;
    const int endHour = 8 + selection.endRow + 1;
    const QString startDay = scheduleTable->horizontalHeaderItem(selection.startColumn)->text();
    const QString endDay = scheduleTable->horizontalHeaderItem(selection.endColumn)->text();
    const QString rangeText = selection.startColumn == selection.endColumn
        ? QString("%1 %2:00-%3:00").arg(startDay).arg(startHour).arg(endHour)
        : QString("%1 到 %2，%3:00-%4:00").arg(startDay, endDay).arg(startHour).arg(endHour);

    return QMessageBox::question(
               const_cast<TodoWindow *>(this),
               action + "行程",
               QString("确认%1“%2”到：\n%3？").arg(action, selection.sourceTitle, rangeText),
               QMessageBox::Ok | QMessageBox::Cancel,
               QMessageBox::Cancel) == QMessageBox::Ok;
}

void TodoWindow::showScheduleContextMenu(const QPoint &position) {
    auto *item = scheduleTable->itemAt(position);
    if (!item || item->data(Qt::UserRole).toInt() <= 0) {
        return;
    }

    const int id = item->data(Qt::UserRole).toInt();
    const QString title = item->toolTip().isEmpty() ? item->text() : item->toolTip();

    QMenu menu(this);
    QAction *deleteAction = menu.addAction("删除行程");
    QAction *selected = menu.exec(scheduleTable->viewport()->mapToGlobal(position));
    if (selected == deleteAction) {
        deleteScheduleItem(id, title);
    }
}

void TodoWindow::deleteScheduleItem(int id, const QString &title) {
    if (QMessageBox::question(
            this,
            "删除行程",
            QString("确认删除“%1”？").arg(title),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Ok) {
        return;
    }

    if (database.isOpen()) {
        QSqlQuery query(database);
        query.prepare("DELETE FROM schedule_items WHERE id = :id");
        query.bindValue(":id", id);
        query.exec();
    }
    clearScheduleItemCells(id);
}

void TodoWindow::loadSchedule() {
    if (!scheduleTable || !database.isOpen()) {
        return;
    }

    scheduleTable->clearContents();
    QSqlQuery query(database);
    query.exec("SELECT id, COALESCE(start_day, day_of_week), COALESCE(end_day, day_of_week), "
               "start_hour, end_hour, title FROM schedule_items "
               "ORDER BY COALESCE(start_day, day_of_week), start_hour");
    while (query.next()) {
        const int id = query.value(0).toInt();
        const int startColumn = query.value(1).toInt() - 1;
        const int endColumn = query.value(2).toInt() - 1;
        const int top = query.value(3).toInt() - 8;
        const int bottom = query.value(4).toInt() - 9;
        const QString title = query.value(5).toString();
        if (startColumn >= 0 && endColumn < 7 && top >= 0 && bottom < scheduleTable->rowCount()) {
            paintScheduleItem(id, startColumn, endColumn, top, bottom, title);
        }
    }
}

void TodoWindow::loadTasks() {
    loading = true;

    QFile file(dataFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(file.readAll());
        const auto tasks = document.array();
        for (const auto &value : tasks) {
            const auto object = value.toObject();
            const QString text = object.value("text").toString().trimmed();
            if (!text.isEmpty()) {
                const QString category = object.value("category").toString("学习");
                const QDate dueDate = QDate::fromString(object.value("dueDate").toString(), Qt::ISODate);
                const int estimateMinutes = object.value("estimateMinutes").toInt(25);
                addTaskItem(text, object.value("completed").toBool(false), category,
                            dueDate.isValid() ? dueDate : QDate::currentDate(), estimateMinutes);
            }
        }
    }

    if (list->count() == 0) {
        addTaskItem("完成第一个 Qt 待办应用", false);
        addTaskItem("尝试一次 25 分钟专注", false);
        saveTasks();
    }

    loading = false;
    saveTasks();
    applyTaskFilter();
}

void TodoWindow::saveTasks() {
    QJsonArray tasks;
    for (int row = 0; row < list->count(); ++row) {
        const auto *item = list->item(row);
        QJsonObject object;
        object["text"] = item->text();
        object["completed"] = item->checkState() == Qt::Checked;
        object["category"] = item->data(CategoryRole).toString();
        object["dueDate"] = item->data(DueDateRole).toString();
        object["estimateMinutes"] = item->data(EstimateMinutesRole).toInt();
        tasks.append(object);
    }

    QFile file(dataFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "保存失败", "无法保存你的待办事项。");
        return;
    }

    file.write(QJsonDocument(tasks).toJson(QJsonDocument::Indented));
}

void TodoWindow::applyStyle() {
    setStyleSheet(R"(
        QMainWindow {
            background: #f7f8fc;
        }
        QLabel {
            color: #1f2337;
        }
        #Sidebar {
            background: #f5f7ff;
            border-right: 1px solid #edf1fb;
        }
        #Dashboard {
            background: #fcfdff;
        }
        #DetailPanel {
            background: #ffffff;
            border-left: 1px solid #edf1f7;
        }
        #Brand {
            color: #252844;
            font-size: 21px;
            font-weight: 800;
        }
        #NewTaskButton {
            background: #5262f5;
            border: 1px solid #5262f5;
            border-radius: 9px;
            color: #ffffff;
            font-weight: 700;
            padding: 14px 14px;
            text-align: left;
        }
        #NewTaskButton:hover {
            background: #4957e8;
        }
        #NavButton, #NavActive {
            background: transparent;
            border: none;
            border-radius: 10px;
            color: #586075;
            padding: 10px 11px;
            text-align: left;
        }
        #NavButton:hover {
            background: #eef1ff;
        }
        #NavActive {
            background: #eef2ff;
            color: #5262f5;
            font-weight: 700;
        }
        #SidebarSection, #SidebarList, #SidebarFooter, #Profile {
            color: #636983;
            padding: 4px 6px;
        }
        #SidebarSection {
            color: #8b91a7;
            font-size: 12px;
            font-weight: 700;
        }
        #AppTitle {
            color: #20233c;
            font-size: 27px;
            font-weight: 800;
        }
        #Subtitle, #MutedText {
            color: #969caf;
            font-size: 13px;
        }
        #SummaryPill {
            background: #f5f7ff;
            border: 1px solid #e4e8fb;
            border-radius: 14px;
            color: #5964ca;
            font-weight: 700;
            padding: 6px 11px;
        }
        #Panel, #FocusPanel {
            background: #ffffff;
            border: 1px solid rgba(232, 236, 247, 170);
            border-radius: 14px;
        }
        #FocusPanel {
            background: #ffffff;
        }
        #MetricCard {
            background: #ffffff;
            border: 1px solid rgba(232, 236, 247, 160);
            border-radius: 16px;
        }
        #MetricTitle, #Filter {
            color: #8d94aa;
            font-size: 12px;
        }
        #MetricValue {
            font-size: 24px;
            font-weight: 800;
        }
        #FilterActive {
            color: #5361e6;
            font-size: 12px;
            font-weight: 700;
        }
        #PanelTitle {
            color: #2c304d;
            font-size: 16px;
            font-weight: 800;
        }
        #DetailTitle {
            color: #20233c;
            font-size: 19px;
            font-weight: 800;
        }
        #DetailMeta {
            color: #67708a;
            font-size: 13px;
        }
        #Tag {
            background: #eef8f3;
            border: 1px solid #d8f0e4;
            border-radius: 11px;
            color: #2d9d70;
            font-weight: 700;
            padding: 5px 9px;
        }
        #Divider {
            color: #edf0f5;
        }
        #FocusTask {
            background: #f6f8ff;
            border: 1px solid #e3e7ff;
            border-radius: 12px;
            color: #5361c7;
            font-size: 13px;
            font-weight: 700;
            padding: 9px;
        }
        #TimerLabel {
            color: #343958;
            font-size: 42px;
            font-weight: 800;
        }
        #StatsBox {
            background: #f8f9fd;
            border: 1px solid #edf0f5;
            border-radius: 8px;
        }
        #StatValue {
            color: #454b70;
            font-size: 14px;
            font-weight: 800;
        }
        QSplitter::handle {
            background: #edf0f5;
        }
        QSplitter::handle:horizontal {
            width: 7px;
        }
        QSplitter::handle:vertical {
            height: 7px;
        }
        QLineEdit, QSpinBox {
            background: #ffffff;
            border: 1px solid #e7ebf4;
            border-radius: 9px;
            color: #343958;
            font-size: 13px;
            padding: 8px 10px;
        }
        QLineEdit:focus, QSpinBox:focus {
            border-color: #8792f7;
        }
        QPushButton {
            background: #f8f9fe;
            border: 1px solid #e7ebf4;
            border-radius: 9px;
            color: #596078;
            font-size: 13px;
            font-weight: 700;
            padding: 8px 11px;
        }
        QPushButton:hover {
            background: #eef1ff;
        }
        QPushButton#PrimaryButton {
            background: #5262f5;
            border-color: #5262f5;
            color: #ffffff;
        }
        QPushButton#PrimaryButton:hover {
            background: #4657ea;
        }
        QPushButton:disabled {
            background: #f7f8fb;
            border-color: #edf0f5;
            color: #b6bbca;
        }
        QListWidget, QTableWidget, QCalendarWidget {
            background: #ffffff;
            border: 1px solid #edf0f5;
            border-radius: 8px;
            color: #343958;
            font-size: 14px;
        }
        QListWidget {
            font-size: 15px;
            padding: 6px;
            border: none;
            background: transparent;
        }
        QListWidget::item {
            border-radius: 10px;
            min-height: 54px;
            margin: 3px 0;
        }
        QListWidget::item:selected {
            background: transparent;
        }
        QWidget#TaskRow {
            background: #ffffff;
            border: 1px solid rgba(236, 239, 248, 160);
            border-radius: 14px;
        }
        #TaskTitle {
            color: #272b43;
            font-size: 14px;
            font-weight: 700;
        }
        #TaskTitleDone {
            color: #9aa1b6;
            font-size: 14px;
            font-weight: 700;
            text-decoration: line-through;
        }
        #TaskTime {
            color: #8b93a8;
            font-size: 12px;
        }
        #ChipUrgent, #ChipSoft, #ChipWork, #ChipStudy {
            border-radius: 8px;
            padding: 3px 8px;
            font-size: 12px;
            font-weight: 700;
        }
        #ChipUrgent {
            background: #fff1f2;
            color: #ef476f;
        }
        #ChipSoft {
            background: #f5f7ff;
            color: #7b83a5;
        }
        #ChipWork {
            background: #eef3ff;
            color: #5865f2;
        }
        #ChipStudy {
            background: #ecfbf4;
            color: #25a36d;
        }
        #StarOn {
            color: #ffbd2e;
            font-size: 18px;
        }
        #StarOff {
            color: #b7bdcc;
            font-size: 18px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 5px;
            border: 1px solid #d9deea;
            background: #ffffff;
        }
        QCheckBox::indicator:checked {
            background: #5262f5;
            border: 1px solid #5262f5;
        }
        QTableWidget::item:selected {
            background: #eef1ff;
            color: #4d59ca;
        }
        QCalendarWidget QWidget#qt_calendar_navigationbar {
            background: #ffffff;
            border: none;
        }
        QCalendarWidget QToolButton {
            background: #f7f8fc;
            border: 1px solid #e6e9f1;
            border-radius: 7px;
            color: #596078;
            font-weight: 700;
            margin: 3px;
            padding: 5px 8px;
        }
        QCalendarWidget QToolButton:hover {
            background: #eef1ff;
        }
        QCalendarWidget QSpinBox {
            min-width: 80px;
            padding: 4px 8px;
        }
        QCalendarWidget QAbstractItemView {
            background: #ffffff;
            border: none;
            selection-background-color: transparent;
            outline: 0;
        }
        QHeaderView::section {
            background: #f8f9fd;
            border: 0;
            border-bottom: 1px solid #edf0f5;
            color: #7b839c;
            font-weight: 700;
            padding: 6px;
        }
        QProgressBar {
            background: #eef0f7;
            border: none;
            border-radius: 5px;
            height: 12px;
        }
        QProgressBar::chunk {
            background: #5865f2;
            border-radius: 5px;
        }
    )");
}
