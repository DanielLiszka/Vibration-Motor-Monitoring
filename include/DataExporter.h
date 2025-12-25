#ifndef DATA_EXPORTER_H
#define DATA_EXPORTER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "FeatureExtractor.h"
#include "FaultDetector.h"

enum ExportFormat {
    FORMAT_JSON = 0,
    FORMAT_CSV,
    FORMAT_XML,
    FORMAT_BINARY
};

class DataExporter {
public:
    DataExporter();
    ~DataExporter();

    String exportFeatures(const FeatureVector& features, ExportFormat format = FORMAT_JSON);

    String exportFault(const FaultResult& fault, ExportFormat format = FORMAT_JSON);

    String exportSpectrum(const float* spectrum, size_t length,
                         ExportFormat format = FORMAT_JSON);

    String exportSession(const std::vector<FeatureVector>& features,
                        const std::vector<FaultResult>& faults,
                        ExportFormat format = FORMAT_JSON);

    String generateCSVHeader();

    bool exportToFile(const String& filename, const String& data);

private:
    String featuresToJSON(const FeatureVector& features);
    String featuresToCSV(const FeatureVector& features);
    String featuresToXML(const FeatureVector& features);

    String faultToJSON(const FaultResult& fault);
    String faultToCSV(const FaultResult& fault);
    String faultToXML(const FaultResult& fault);

    String spectrumToJSON(const float* spectrum, size_t length);
    String spectrumToCSV(const float* spectrum, size_t length);

    String escapeXML(const String& str);
};

#endif
