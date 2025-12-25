#include "DataExporter.h"

DataExporter::DataExporter() {
}

DataExporter::~DataExporter() {
}

String DataExporter::exportFeatures(const FeatureVector& features, ExportFormat format) {
    switch (format) {
        case FORMAT_JSON:
            return featuresToJSON(features);
        case FORMAT_CSV:
            return featuresToCSV(features);
        case FORMAT_XML:
            return featuresToXML(features);
        default:
            return featuresToJSON(features);
    }
}

String DataExporter::exportFault(const FaultResult& fault, ExportFormat format) {
    switch (format) {
        case FORMAT_JSON:
            return faultToJSON(fault);
        case FORMAT_CSV:
            return faultToCSV(fault);
        case FORMAT_XML:
            return faultToXML(fault);
        default:
            return faultToJSON(fault);
    }
}

String DataExporter::exportSpectrum(const float* spectrum, size_t length, ExportFormat format) {
    switch (format) {
        case FORMAT_JSON:
            return spectrumToJSON(spectrum, length);
        case FORMAT_CSV:
            return spectrumToCSV(spectrum, length);
        default:
            return spectrumToJSON(spectrum, length);
    }
}

String DataExporter::generateCSVHeader() {
    return "timestamp,rms,peakToPeak,kurtosis,skewness,crestFactor,variance,"
           "spectralCentroid,spectralSpread,bandPowerRatio,dominantFreq\n";
}

String DataExporter::featuresToJSON(const FeatureVector& features) {
    String json = "{";
    json += "\"timestamp\":" + String(features.timestamp) + ",";
    json += "\"rms\":" + String(features.rms, 4) + ",";
    json += "\"peakToPeak\":" + String(features.peakToPeak, 4) + ",";
    json += "\"kurtosis\":" + String(features.kurtosis, 4) + ",";
    json += "\"skewness\":" + String(features.skewness, 4) + ",";
    json += "\"crestFactor\":" + String(features.crestFactor, 4) + ",";
    json += "\"variance\":" + String(features.variance, 4) + ",";
    json += "\"spectralCentroid\":" + String(features.spectralCentroid, 2) + ",";
    json += "\"spectralSpread\":" + String(features.spectralSpread, 2) + ",";
    json += "\"bandPowerRatio\":" + String(features.bandPowerRatio, 4) + ",";
    json += "\"dominantFreq\":" + String(features.dominantFrequency, 2);
    json += "}";
    return json;
}

String DataExporter::featuresToCSV(const FeatureVector& features) {
    String csv = String(features.timestamp) + ",";
    csv += String(features.rms, 4) + ",";
    csv += String(features.peakToPeak, 4) + ",";
    csv += String(features.kurtosis, 4) + ",";
    csv += String(features.skewness, 4) + ",";
    csv += String(features.crestFactor, 4) + ",";
    csv += String(features.variance, 4) + ",";
    csv += String(features.spectralCentroid, 2) + ",";
    csv += String(features.spectralSpread, 2) + ",";
    csv += String(features.bandPowerRatio, 4) + ",";
    csv += String(features.dominantFrequency, 2) + "\n";
    return csv;
}

String DataExporter::featuresToXML(const FeatureVector& features) {
    String xml = "<features>\n";
    xml += "  <timestamp>" + String(features.timestamp) + "</timestamp>\n";
    xml += "  <rms>" + String(features.rms, 4) + "</rms>\n";
    xml += "  <peakToPeak>" + String(features.peakToPeak, 4) + "</peakToPeak>\n";
    xml += "  <kurtosis>" + String(features.kurtosis, 4) + "</kurtosis>\n";
    xml += "  <skewness>" + String(features.skewness, 4) + "</skewness>\n";
    xml += "  <crestFactor>" + String(features.crestFactor, 4) + "</crestFactor>\n";
    xml += "  <variance>" + String(features.variance, 4) + "</variance>\n";
    xml += "  <spectralCentroid>" + String(features.spectralCentroid, 2) + "</spectralCentroid>\n";
    xml += "  <spectralSpread>" + String(features.spectralSpread, 2) + "</spectralSpread>\n";
    xml += "  <bandPowerRatio>" + String(features.bandPowerRatio, 4) + "</bandPowerRatio>\n";
    xml += "  <dominantFreq>" + String(features.dominantFrequency, 2) + "</dominantFreq>\n";
    xml += "</features>\n";
    return xml;
}

String DataExporter::faultToJSON(const FaultResult& fault) {
    String json = "{";
    json += "\"timestamp\":" + String(fault.timestamp) + ",";
    json += "\"type\":\"" + String(fault.getFaultTypeName()) + "\",";
    json += "\"severity\":\"" + String(fault.getSeverityName()) + "\",";
    json += "\"confidence\":" + String(fault.confidence, 2) + ",";
    json += "\"anomalyScore\":" + String(fault.anomalyScore, 4) + ",";
    json += "\"description\":\"" + fault.description + "\"";
    json += "}";
    return json;
}

String DataExporter::faultToCSV(const FaultResult& fault) {
    String csv = String(fault.timestamp) + ",";
    csv += String(fault.getFaultTypeName()) + ",";
    csv += String(fault.getSeverityName()) + ",";
    csv += String(fault.confidence, 2) + ",";
    csv += String(fault.anomalyScore, 4) + ",";
    csv += fault.description + "\n";
    return csv;
}

String DataExporter::faultToXML(const FaultResult& fault) {
    String xml = "<fault>\n";
    xml += "  <timestamp>" + String(fault.timestamp) + "</timestamp>\n";
    xml += "  <type>" + String(fault.getFaultTypeName()) + "</type>\n";
    xml += "  <severity>" + String(fault.getSeverityName()) + "</severity>\n";
    xml += "  <confidence>" + String(fault.confidence, 2) + "</confidence>\n";
    xml += "  <anomalyScore>" + String(fault.anomalyScore, 4) + "</anomalyScore>\n";
    xml += "  <description>" + escapeXML(fault.description) + "</description>\n";
    xml += "</fault>\n";
    return xml;
}

String DataExporter::spectrumToJSON(const float* spectrum, size_t length) {
    String json = "{\"spectrum\":[";
    for (size_t i = 0; i < length; i++) {
        json += String(spectrum[i], 2);
        if (i < length - 1) json += ",";
    }
    json += "]}";
    return json;
}

String DataExporter::spectrumToCSV(const float* spectrum, size_t length) {
    String csv = "";
    for (size_t i = 0; i < length; i++) {
        csv += String(spectrum[i], 2);
        if (i < length - 1) csv += ",";
    }
    csv += "\n";
    return csv;
}

String DataExporter::escapeXML(const String& str) {
    String escaped = str;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    escaped.replace("'", "&apos;");
    return escaped;
}
