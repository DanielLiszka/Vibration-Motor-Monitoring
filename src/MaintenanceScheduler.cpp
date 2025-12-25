#include "MaintenanceScheduler.h"

MaintenanceScheduler::MaintenanceScheduler()
    : currentRiskScore(0.0f)
    , lastUpdateTime(0)
    , totalTasksCompleted(0)
    , totalTasksScheduled(0)
    , rulEstimator(nullptr)
{
}

MaintenanceScheduler::~MaintenanceScheduler() {
    tasks.clear();
}

bool MaintenanceScheduler::begin() {
    DEBUG_PRINTLN("Initializing Maintenance Scheduler...");
    tasks.clear();
    currentRiskScore = 0.0f;
    lastUpdateTime = millis();
    return true;
}

void MaintenanceScheduler::update(const FeatureVector& features,
                                  const FaultResult& fault,
                                  const TrendAnalysis& trends) {
    currentRiskScore = calculateRiskScore(features, fault, trends);

    if (rulEstimator) {
        rulEstimator->updateHealthIndex(features, fault.anomalyScore);
        RULEstimate rul = rulEstimator->estimateRUL();

        if (rul.currentStage == STAGE_NEAR_FAILURE && rul.estimatedHoursRemaining < 24.0f) {
            MaintenanceType type = recommendMaintenanceType(fault);
            scheduleTask(type, PRIORITY_CRITICAL, 0,
                        "RUL critical: " + String(rul.estimatedHoursRemaining, 1) + "h remaining");
        } else if (rul.currentStage == STAGE_ACCELERATED_DEGRADATION) {
            scheduleTask(MAINT_INSPECTION, PRIORITY_HIGH,
                        (uint32_t)(rul.estimatedHoursRemaining * 0.5f),
                        "Accelerated degradation detected - plan maintenance");
        } else if (rul.currentStage == STAGE_EARLY_DEGRADATION &&
                   rul.degradationRate > 0.01f) {
            scheduleTask(MAINT_VIBRATION_ANALYSIS, PRIORITY_MEDIUM, 72,
                        "Early degradation trend - analysis recommended");
        }
    }

    if (currentRiskScore > MAINTENANCE_CRITICAL_THRESHOLD) {
        MaintenanceType type = recommendMaintenanceType(fault);
        scheduleTask(type, PRIORITY_CRITICAL, 0, "Critical fault detected - immediate action required");
    } else if (currentRiskScore > MAINTENANCE_WARNING_THRESHOLD) {
        if (trends.isDeterioration) {
            scheduleTask(MAINT_INSPECTION, PRIORITY_HIGH, 24,
                        "Deteriorating trend detected - inspection recommended");
        }
    }

    cleanupCompletedTasks();
    sortTasksByPriority();
    lastUpdateTime = millis();
}

void MaintenanceScheduler::scheduleTask(MaintenanceType type, MaintenancePriority priority,
                                       uint32_t delayHours, const String& description) {
    if (tasks.size() >= MAX_SCHEDULED_TASKS) {
        DEBUG_PRINTLN("Maximum scheduled tasks reached");
        return;
    }

    MaintenanceTask task;
    task.type = type;
    task.priority = priority;
    task.scheduledTime = millis() + (delayHours * 3600000UL);
    task.estimatedDuration = estimateTaskDuration(type);
    task.description = description;
    task.urgencyScore = currentRiskScore;
    task.completed = false;
    task.completedTime = 0;

    tasks.push_back(task);
    totalTasksScheduled++;

    DEBUG_PRINT("Scheduled maintenance task: ");
    DEBUG_PRINTLN(description);
}

std::vector<MaintenanceTask> MaintenanceScheduler::getScheduledTasks() {
    std::vector<MaintenanceTask> scheduled;
    for (const auto& task : tasks) {
        if (!task.completed) {
            scheduled.push_back(task);
        }
    }
    return scheduled;
}

std::vector<MaintenanceTask> MaintenanceScheduler::getUpcomingTasks(uint32_t hoursAhead) {
    std::vector<MaintenanceTask> upcoming;
    uint32_t currentTime = millis();
    uint32_t futureTime = currentTime + (hoursAhead * 3600000UL);

    for (const auto& task : tasks) {
        if (!task.completed &&
            task.scheduledTime >= currentTime &&
            task.scheduledTime <= futureTime) {
            upcoming.push_back(task);
        }
    }

    return upcoming;
}

std::vector<MaintenanceTask> MaintenanceScheduler::getOverdueTasks() {
    std::vector<MaintenanceTask> overdue;
    uint32_t currentTime = millis();

    for (const auto& task : tasks) {
        if (!task.completed && task.scheduledTime < currentTime) {
            overdue.push_back(task);
        }
    }

    return overdue;
}

void MaintenanceScheduler::completeTask(size_t taskIndex) {
    if (taskIndex >= tasks.size()) {
        return;
    }

    tasks[taskIndex].completed = true;
    tasks[taskIndex].completedTime = millis();
    totalTasksCompleted++;

    DEBUG_PRINT("Completed maintenance task: ");
    DEBUG_PRINTLN(tasks[taskIndex].description);
}

void MaintenanceScheduler::cancelTask(size_t taskIndex) {
    if (taskIndex >= tasks.size()) {
        return;
    }

    tasks.erase(tasks.begin() + taskIndex);

    DEBUG_PRINTLN("Cancelled maintenance task");
}

uint32_t MaintenanceScheduler::getNextMaintenanceTime() const {
    if (tasks.empty()) {
        return 0;
    }

    uint32_t nextTime = UINT32_MAX;
    for (const auto& task : tasks) {
        if (!task.completed && task.scheduledTime < nextTime) {
            nextTime = task.scheduledTime;
        }
    }

    return (nextTime == UINT32_MAX) ? 0 : nextTime;
}

String MaintenanceScheduler::generateMaintenanceReport() {
    String report = "=== MAINTENANCE REPORT ===\n\n";

    report += "Current Risk Score: " + String(currentRiskScore, 2) + "\n";
    report += "Total Tasks Scheduled: " + String(totalTasksScheduled) + "\n";
    report += "Total Tasks Completed: " + String(totalTasksCompleted) + "\n";
    report += "Active Tasks: " + String(getScheduledTasks().size()) + "\n\n";

    std::vector<MaintenanceTask> overdue = getOverdueTasks();
    if (!overdue.empty()) {
        report += "OVERDUE TASKS (" + String(overdue.size()) + "):\n";
        for (const auto& task : overdue) {
            report += "  - " + task.description + "\n";
        }
        report += "\n";
    }

    std::vector<MaintenanceTask> upcoming = getUpcomingTasks(MAINTENANCE_WINDOW_HOURS);
    if (!upcoming.empty()) {
        report += "UPCOMING TASKS (Next 24h):\n";
        for (const auto& task : upcoming) {
            uint32_t hoursUntil = (task.scheduledTime - millis()) / 3600000UL;
            report += "  - " + task.description + " (in " + String(hoursUntil) + "h)\n";
        }
        report += "\n";
    }

    report += "=========================\n";

    return report;
}

float MaintenanceScheduler::calculateRiskScore(const FeatureVector& features,
                                              const FaultResult& fault,
                                              const TrendAnalysis& trends) {
    float riskScore = 0.0f;

    riskScore += fault.anomalyScore * 0.4f;

    if (fault.severity == SEVERITY_CRITICAL) {
        riskScore += 0.3f;
    } else if (fault.severity == SEVERITY_WARNING) {
        riskScore += 0.15f;
    }

    if (trends.isDeterioration) {
        riskScore += 0.2f;
    }

    if (features.kurtosis > 5.0f) {
        riskScore += 0.1f;
    }

    return min(riskScore, 1.0f);
}

MaintenancePriority MaintenanceScheduler::determinePriority(float riskScore) {
    if (riskScore > MAINTENANCE_CRITICAL_THRESHOLD) {
        return PRIORITY_CRITICAL;
    } else if (riskScore > MAINTENANCE_WARNING_THRESHOLD) {
        return PRIORITY_HIGH;
    } else if (riskScore > 0.25f) {
        return PRIORITY_MEDIUM;
    } else {
        return PRIORITY_LOW;
    }
}

MaintenanceType MaintenanceScheduler::recommendMaintenanceType(const FaultResult& fault) {
    switch (fault.type) {
        case FAULT_IMBALANCE:
            return MAINT_BALANCE;
        case FAULT_MISALIGNMENT:
            return MAINT_ALIGNMENT;
        case FAULT_BEARING:
            return MAINT_BEARING_REPLACEMENT;
        case FAULT_LOOSENESS:
            return MAINT_COUPLING_CHECK;
        default:
            return MAINT_INSPECTION;
    }
}

uint32_t MaintenanceScheduler::estimateTaskDuration(MaintenanceType type) {
    switch (type) {
        case MAINT_INSPECTION:
            return 30;
        case MAINT_LUBRICATION:
            return 15;
        case MAINT_ALIGNMENT:
            return 120;
        case MAINT_BALANCE:
            return 180;
        case MAINT_BEARING_REPLACEMENT:
            return 240;
        case MAINT_COUPLING_CHECK:
            return 60;
        case MAINT_VIBRATION_ANALYSIS:
            return 45;
        case MAINT_THERMAL_IMAGING:
            return 30;
        default:
            return 60;
    }
}

void MaintenanceScheduler::cleanupCompletedTasks() {
    uint32_t currentTime = millis();
    uint32_t retentionTime = 7 * 24 * 3600000UL;

    tasks.erase(
        std::remove_if(tasks.begin(), tasks.end(),
            [currentTime, retentionTime](const MaintenanceTask& task) {
                return task.completed &&
                       (currentTime - task.completedTime) > retentionTime;
            }),
        tasks.end()
    );
}

void MaintenanceScheduler::sortTasksByPriority() {
    std::sort(tasks.begin(), tasks.end(),
        [](const MaintenanceTask& a, const MaintenanceTask& b) {
            if (a.completed != b.completed) {
                return !a.completed;
            }
            if (a.priority != b.priority) {
                return a.priority > b.priority;
            }
            return a.scheduledTime < b.scheduledTime;
        });
}

RULEstimate MaintenanceScheduler::getRULEstimate() const {
    if (rulEstimator) {
        return rulEstimator->estimateRUL();
    }
    RULEstimate empty = {};
    empty.estimatedHoursRemaining = -1.0f;
    empty.currentStage = STAGE_HEALTHY;
    return empty;
}

float MaintenanceScheduler::getEstimatedHoursRemaining() const {
    if (rulEstimator) {
        RULEstimate rul = rulEstimator->estimateRUL();
        return rul.estimatedHoursRemaining;
    }
    return -1.0f;
}

DegradationStage MaintenanceScheduler::getDegradationStage() const {
    if (rulEstimator) {
        RULEstimate rul = rulEstimator->estimateRUL();
        return rul.currentStage;
    }
    return STAGE_HEALTHY;
}

String MaintenanceScheduler::generateRULReport() const {
    String report = "=== RUL ANALYSIS REPORT ===\n\n";

    if (!rulEstimator) {
        report += "RUL Estimator not configured.\n";
        return report;
    }

    RULEstimate rul = rulEstimator->estimateRUL();

    report += "Health Index: " + String(rul.healthIndex, 3) + "\n";
    report += "Estimated Hours Remaining: ";
    if (rul.estimatedHoursRemaining >= 0) {
        report += String(rul.estimatedHoursRemaining, 1) + "h\n";
    } else {
        report += "Unknown\n";
    }

    report += "Confidence Interval: +/- " + String(rul.confidenceInterval, 1) + "h\n";
    report += "Degradation Rate: " + String(rul.degradationRate * 100.0f, 4) + "%/h\n\n";

    report += "Degradation Stage: ";
    switch (rul.currentStage) {
        case STAGE_HEALTHY:
            report += "HEALTHY - Normal operation\n";
            break;
        case STAGE_EARLY_DEGRADATION:
            report += "EARLY DEGRADATION - Monitor closely\n";
            break;
        case STAGE_ACCELERATED_DEGRADATION:
            report += "ACCELERATED DEGRADATION - Plan maintenance\n";
            break;
        case STAGE_NEAR_FAILURE:
            report += "NEAR FAILURE - Immediate action required\n";
            break;
    }

    report += "\nHealth Predictions:\n";
    float h24 = rulEstimator->predictHealthAt(24.0f);
    float h72 = rulEstimator->predictHealthAt(72.0f);
    float h168 = rulEstimator->predictHealthAt(168.0f);

    report += "  In 24h:  " + String(h24, 3) + "\n";
    report += "  In 72h:  " + String(h72, 3) + "\n";
    report += "  In 168h: " + String(h168, 3) + "\n";

    report += "\n===========================\n";

    return report;
}
