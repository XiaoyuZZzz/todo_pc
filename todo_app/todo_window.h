#pragma once

#include <QDate>
#include <QMainWindow>
#include <QSqlDatabase>

class QLabel;
class QCloseEvent;
class QEvent;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QComboBox;
class QDateEdit;
class QSpinBox;
class QTimer;
class QWidget;

class FloatingFocusWidget;
class ScheduleTable;
class StudyCalendarWidget;
struct ScheduleSelection;

class TodoWindow : public QMainWindow {
public:
    explicit TodoWindow(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createTasksPanel();
    QWidget *createFocusPanel();
    QWidget *createSchedulePanel();
    QWidget *createCalendarPanel();
    QWidget *createSidebar();
    QWidget *createDashboardPanel();
    QWidget *createDetailPanel();
    QWidget *createMetricCard(const QString &title, const QString &value, const QString &caption, const QString &accent);
    void applySoftShadow(QWidget *widget, int blur = 22, int yOffset = 8, int alpha = 22);

    void connectUi();
    void initDatabase();
    void migrateDatabase();
    QString appDataDirPath() const;
    QString databaseFilePath() const;
    QString dataFilePath() const;

    void addTask();
    void addTaskItem(const QString &text, bool completed, const QString &category = "学习",
                     const QDate &dueDate = QDate::currentDate(), int estimateMinutes = 25);
    void deleteSelectedTask();
    void clearCompletedTasks();
    void updateStatus();
    void updateFocusTask();
    void updateTaskDetail();
    void updateTaskRowWidget(QListWidgetItem *item);
    void refreshTaskRows();
    void saveSelectedTaskDetail();
    void toggleSelectedTaskCompletion();
    void applyTaskFilter();
    void updateDashboardMetrics();
    void updateSidebarCounts();
    void updateFilterLabels();
    QString currentFocusTaskText() const;

    void toggleFocusTimer();
    void resetFocusTimer();
    void tickFocusTimer();
    void updateTimerDisplay();
    void showFloatingFocus();
    void restoreFromFloating();
    void syncFloatingFocus();
    void logFocusSession();
    void updateTodayStudy();
    void updateStudyCalendar();

    void createScheduleItem(const ScheduleSelection &selection);
    void paintScheduleItem(int id, int startColumn, int endColumn, int top, int bottom, const QString &title);
    void clearScheduleItemCells(int id);
    bool confirmScheduleChange(const QString &action, const ScheduleSelection &selection) const;
    void showScheduleContextMenu(const QPoint &position);
    void deleteScheduleItem(int id, const QString &title);
    void loadSchedule();

    void loadTasks();
    void saveTasks();
    void applyStyle();

    QLineEdit *taskInput = nullptr;
    QLineEdit *searchInput = nullptr;
    QLineEdit *detailTitleInput = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *deleteButton = nullptr;
    QPushButton *clearDoneButton = nullptr;
    QListWidget *list = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *summaryLabel = nullptr;
    QLabel *detailTitleLabel = nullptr;
    QLabel *detailStatusLabel = nullptr;
    QLabel *detailDescriptionLabel = nullptr;
    QLabel *progressMetricLabel = nullptr;
    QLabel *focusMetricLabel = nullptr;
    QLabel *streakMetricLabel = nullptr;
    QLabel *estimateMetricLabel = nullptr;
    QLabel *progressMetricCaptionLabel = nullptr;
    QLabel *focusMetricCaptionLabel = nullptr;
    QLabel *streakMetricCaptionLabel = nullptr;
    QLabel *estimateMetricCaptionLabel = nullptr;
    QLabel *allFilterLabel = nullptr;
    QLabel *urgentFilterLabel = nullptr;
    QLabel *importantFilterLabel = nullptr;
    QLabel *otherFilterLabel = nullptr;
    QLabel *workCountLabel = nullptr;
    QLabel *studyCountLabel = nullptr;
    QLabel *personalCountLabel = nullptr;
    QLabel *shoppingCountLabel = nullptr;
    QLabel *travelCountLabel = nullptr;
    QLabel *focusTaskLabel = nullptr;
    QLabel *timerLabel = nullptr;
    QLabel *focusStateLabel = nullptr;
    QLabel *sessionsLabel = nullptr;
    QLabel *todayStudyLabel = nullptr;
    QProgressBar *focusProgress = nullptr;
    QSpinBox *minutesInput = nullptr;
    QSpinBox *detailEstimateInput = nullptr;
    QDateEdit *detailDateInput = nullptr;
    QComboBox *detailCategoryInput = nullptr;
    QPushButton *detailSaveButton = nullptr;
    QPushButton *detailToggleButton = nullptr;
    QPushButton *todayNavButton = nullptr;
    QPushButton *tomorrowNavButton = nullptr;
    QPushButton *upcomingNavButton = nullptr;
    QPushButton *allNavButton = nullptr;
    QPushButton *startPauseButton = nullptr;
    QPushButton *resetButton = nullptr;
    ScheduleTable *scheduleTable = nullptr;
    StudyCalendarWidget *studyCalendar = nullptr;
    QTimer *focusTimer = nullptr;
    FloatingFocusWidget *floatingFocus = nullptr;
    QSqlDatabase database;

    int focusTotalSeconds = 25 * 60;
    int focusRemainingSeconds = 25 * 60;
    int completedSessions = 0;
    bool focusRunning = false;
    bool loading = false;
    int taskFilterMode = 0;
};
