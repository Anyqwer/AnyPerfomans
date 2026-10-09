#pragma once

#include "anyperf/types.hpp"
#include "anyperf/gpu_monitor.hpp"
#include <string>
#include <vector>

namespace anyperf {

class ReportGenerator {
public:
    // Generate an interactive HTML report with Chart.js and 1-click Discord/TG image export
    static std::wstring generate_html_report(
        const BenchmarkRecord& record,
        const std::vector<float>& frametime_samples,
        const GpuMetrics& gpu,
        const std::wstring& output_dir = L"reports"
    );

    // Open file in default system web browser
    static void open_in_browser(const std::wstring& file_path);
};

} // namespace anyperf
