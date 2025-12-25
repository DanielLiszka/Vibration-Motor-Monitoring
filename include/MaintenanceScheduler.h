#ifndef MAINTENANCE_SCHEDULER_H
#define MAINTENANCE_SCHEDULER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"
#include "TrendAnalyzer.h"
#include "EdgeRULEstimator.h"

#define MAINTENANCE_WINDOW_HOURS 24
#define MAINTENANCE_CRITICAL_THRESHOLD 0.8
#define MAINTENANCE_WARNING_THRESHOLD 0.5
#define MAX_SCHEDULED_TASKS 10

enum MaintenanceType {
    MAINT_INSPECTION,
    MAINT_LUBRICATION,
    MAINT_ALIGNMENT,
    MAINT_BALANCE,
    MAINT_BEARING_REPLACEMENT,
    MAINT_COUPLING_CHECK,
    MAINT_VIBRATION_ANALYSIS,
    MAINT_THERMAL_IMAGING
};

enum MaintenancePriority {
    PRIORITY_LOW,
    PRIORITY_MEDIUM,
    PRIORITY_HIGH,
    PRIORITY_CRITICAL
};

struct MaintenanceTask {
    MaintenanceType type;
    MaintenancePriority priority;
    uint32_t scheduledTime;
    uint32_t estimatedDuration;
    String description;
    float urgencyScore;
    bool completed;
    uint32_t completedTime;
};

class MaintenanceScheduler {
public:
    MaintenanceScheduler();
    ~MaintenanceScheduler();

    bool begin();

    void update(const FeatureVector& features, const FaultResult& fault,
                const TrendAnalysis& trends);

    void scheduleTask(MaintenanceType type, MaintenancePriority priority,
                     uint32_t delayHours, const String& description);

    std::vector<MaintenanceTask> getScheduledTasks();
    std::vector<MaintenanceTask> getUpcomingTasks(uint32_t hoursAhead = 24);
    std::vector<MaintenanceTask> getOverdueTasks();

    void completeTask(size_t taskIndex);
    void cancelTask(size_t taskIndex);

    uint32_t getNextMaintenanceTime() const;
    float getRiskScore() const { return currentRiskScore; }

    void setRULEstimator(EdgeRULEstimator* estimator) { rulEstimator = estimator; }
    RULEstimate getRULEstimate() const;
    float getEstimatedHoursRemaining() const;
    DegradationStage getDegradationStage() const;

    String generateMaintenanceReport();
    String generateRULReport() const;

private:
    std::vector<MaintenanceTask> tasks;
    float currentRiskScore;
    uint32_t lastUpdateTime;
    uint32_t totalTasksCompleted;
    uint32_t totalTasksScheduled;
    EdgeRULEstimator* rulEstimator;

    float calculateRiskScore(const FeatureVector& features,
                            const FaultResult& fault,
                            const TrendAnalysis& trends);

    MaintenancePriority determinePriority(float riskScore);
    MaintenanceType recommendMaintenanceType(const FaultResult& fault);
    uint32_t estimateTaskDuration(MaintenanceType type);

    void cleanupCompletedTasks();
    void sortTasksByPriority();
};

#endif
