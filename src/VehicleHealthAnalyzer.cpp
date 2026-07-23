#include "VehicleHealthAnalyzer.h"

#include <QtGlobal>
#include <cmath>

namespace {
constexpr quint8 PID_ENGINE_LOAD = 0x04;
constexpr quint8 PID_COOLANT_TEMP = 0x05; 
constexpr quint8 PID_SHORT_TRIM_1 = 0x06;
constexpr quint8 PID_LONG_TRIM_1 = 0x07;
constexpr quint8 PID_ENGINE_RPM = 0x0C;
constexpr quint8 PID_VEHICLE_SPEED = 0x0D;
constexpr quint8 PID_CONTROL_MODULE_VOLTAGE = 0x42;

QString ratingForScore(int score)
{
    if (score >= 90) return "Excellent";
    if (score >= 75) return "Good";
    if (score >= 60) return "Fair";
    if (score >= 40) return "Poor";
    return "Critical";
}

void addUnique(QStringList &list, const QString &message)
{
    if (!list.contains(message)) list.append(message);
}
}

void VehicleHealthAnalyzer::updatePid(quint8 pid, double value)
{
    if (std::isfinite(value)) m_pidValues.insert(pid, value);
}

void VehicleHealthAnalyzer::updateDtcs(quint8 mode, const QStringList &codes)
{
    switch (mode) {
    case 0x03: m_storedDtcs = codes; break;
    case 0x07: m_pendingDtcs = codes; break;
    case 0x0A: m_permanentDtcs = codes; break;
    default: break;
    }
}

void VehicleHealthAnalyzer::clearDtcs()
{
    m_storedDtcs.clear();
    m_pendingDtcs.clear();
    m_permanentDtcs.clear();
}

void VehicleHealthAnalyzer::reset()
{
    m_pidValues.clear();
    clearDtcs();
}

VehicleHealthReport VehicleHealthAnalyzer::analyze() const
{
    VehicleHealthReport report;
    report.pidValues = m_pidValues;
    report.storedDtcs = m_storedDtcs;
    report.pendingDtcs = m_pendingDtcs;
    report.permanentDtcs = m_permanentDtcs;

    int score = 100;

    // DTC scoring.
    score -= qMin(45, m_storedDtcs.size() * 15);
    score -= qMin(24, m_pendingDtcs.size() * 8);
    score -= qMin(24, m_permanentDtcs.size() * 12);

    if (!m_storedDtcs.isEmpty()) {
        report.systemStatus.insert("Diagnostics", "Needs attention");
        addUnique(report.warnings, QString("%1 stored diagnostic trouble code(s) detected.").arg(m_storedDtcs.size()));
        addUnique(report.recommendations, "Review and repair stored trouble codes before clearing them.");
    } else if (!m_pendingDtcs.isEmpty() || !m_permanentDtcs.isEmpty()) {
        report.systemStatus.insert("Diagnostics", "Monitor");
        addUnique(report.warnings, "Pending or permanent diagnostic trouble codes were detected.");
        addUnique(report.recommendations, "Re-scan after a complete drive cycle and investigate recurring codes.");
    } else {
        report.systemStatus.insert("Diagnostics", "Healthy");
    }

   
    if (m_pidValues.contains(PID_CONTROL_MODULE_VOLTAGE)) {
        const double voltage = m_pidValues.value(PID_CONTROL_MODULE_VOLTAGE);
        if (voltage < 11.8) {
            score -= 18;
            report.systemStatus.insert("Electrical", "Critical");
            addUnique(report.warnings, QString("Very low module voltage: %1 V.").arg(voltage, 0, 'f', 1));
            addUnique(report.recommendations, "Test the battery, alternator, terminals, and ground connections.");
        } else if (voltage < 12.2) {
            score -= 10;
            report.systemStatus.insert("Electrical", "Needs attention");
            addUnique(report.warnings, QString("Low module voltage: %1 V.").arg(voltage, 0, 'f', 1));
            addUnique(report.recommendations, "Perform a battery and charging-system test.");
        } else if (voltage > 15.2) {
            score -= 12;
            report.systemStatus.insert("Electrical", "Needs attention");
            addUnique(report.warnings, QString("Charging voltage is unusually high: %1 V.").arg(voltage, 0, 'f', 1));
            addUnique(report.recommendations, "Inspect the voltage regulator and charging system.");
        } else {
            report.systemStatus.insert("Electrical", "Healthy");
        }
    } else {
        report.systemStatus.insert("Electrical", "No data");
    }

    
    if (m_pidValues.contains(PID_COOLANT_TEMP)) {
        const double coolant = m_pidValues.value(PID_COOLANT_TEMP);
        if (coolant >= 115.0) {
            score -= 20;
            report.systemStatus.insert("Cooling", "Critical");
            addUnique(report.warnings, QString("Engine coolant temperature is critical: %1 C.").arg(coolant, 0, 'f', 1));
            addUnique(report.recommendations, "Stop the engine safely and inspect the cooling system before driving further.");
        } else if (coolant >= 105.0) {
            score -= 10;
            report.systemStatus.insert("Cooling", "Needs attention");
            addUnique(report.warnings, QString("Engine coolant temperature is high: %1 C.").arg(coolant, 0, 'f', 1));
            addUnique(report.recommendations, "Check coolant level, radiator airflow, fans, and thermostat operation.");
        } else if (coolant < 60.0) {
            report.systemStatus.insert("Cooling", "Warming up");
        } else {
            report.systemStatus.insert("Cooling", "Healthy");
        }
    } else {
        report.systemStatus.insert("Cooling", "No data");
    }

    
    bool hasFuelTrim = false;
    double worstFuelTrim = 0.0;
    for (quint8 pid : {PID_SHORT_TRIM_1, PID_LONG_TRIM_1}) {
        if (m_pidValues.contains(pid)) {
            hasFuelTrim = true;
            const double value = m_pidValues.value(pid);
            if (std::abs(value) > std::abs(worstFuelTrim)) worstFuelTrim = value;
        }
    }
    if (hasFuelTrim) {
        if (std::abs(worstFuelTrim) > 20.0) {
            score -= 12;
            report.systemStatus.insert("Fuel system", "Needs attention");
            addUnique(report.warnings, QString("Fuel trim correction is excessive: %1%.").arg(worstFuelTrim, 0, 'f', 1));
            addUnique(report.recommendations, "Inspect for intake leaks, fuel-delivery problems, exhaust leaks, or sensor faults.");
        } else if (std::abs(worstFuelTrim) > 10.0) {
            score -= 5;
            report.systemStatus.insert("Fuel system", "Monitor");
            addUnique(report.warnings, QString("Fuel trim is outside the preferred range: %1%.").arg(worstFuelTrim, 0, 'f', 1));
            addUnique(report.recommendations, "Monitor fuel trims and inspect the intake and fuel systems if the value persists.");
        } else {
            report.systemStatus.insert("Fuel system", "Healthy");
        }
    } else {
        report.systemStatus.insert("Fuel system", "No data");
    }

    // Idle checks only apply when vehicle speed is approximately zero.
    if (m_pidValues.contains(PID_ENGINE_RPM)) {
        const double rpm = m_pidValues.value(PID_ENGINE_RPM);
        const double speed = m_pidValues.value(PID_VEHICLE_SPEED, 0.0);
        const bool appearsIdle = speed < 2.0 && rpm > 0.0;
        if (appearsIdle && (rpm < 500.0 || rpm > 1200.0)) {
            score -= 6;
            report.systemStatus.insert("Engine", "Monitor");
            addUnique(report.warnings, QString("Idle speed appears abnormal: %1 RPM.").arg(rpm, 0, 'f', 0));
            addUnique(report.recommendations, "Check for vacuum leaks, throttle-body deposits, misfires, or idle-control issues.");
        } else {
            report.systemStatus.insert("Engine", "Healthy");
        }
    } else {
        report.systemStatus.insert("Engine", "No data");
    }

    if (m_pidValues.contains(PID_ENGINE_LOAD) && m_pidValues.contains(PID_ENGINE_RPM)) {
        const double load = m_pidValues.value(PID_ENGINE_LOAD);
        const double rpm = m_pidValues.value(PID_ENGINE_RPM);
        const double speed = m_pidValues.value(PID_VEHICLE_SPEED, 0.0);
        if (speed < 2.0 && rpm > 0.0 && load > 60.0) {
            score -= 5;
            addUnique(report.warnings, QString("Engine load is high at idle: %1%.").arg(load, 0, 'f', 1));
            addUnique(report.recommendations, "Inspect for accessory load, airflow restrictions, or engine performance problems.");
        }
    }

    report.score = qBound(0, score, 100);
    report.rating = ratingForScore(report.score);

    if (report.warnings.isEmpty()) {
        report.summary = "No major problems were detected in the available OBD-II data.";
        report.recommendations << "Continue routine maintenance and compare future scans for changes.";
    } else {
        report.summary = QString("The scan found %1 item(s) that may require attention.").arg(report.warnings.size());
    }

    return report;
}
