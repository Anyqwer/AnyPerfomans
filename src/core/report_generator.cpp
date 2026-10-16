#include "anyperf/report_generator.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <shellapi.h>

#include "anyperf/utils.hpp"

namespace anyperf {

std::wstring ReportGenerator::generate_html_report(
    const BenchmarkRecord& record,
    const std::vector<float>& frametime_samples,
    const GpuMetrics& gpu,
    const std::wstring& output_dir
) {
    CreateDirectoryW(output_dir.c_str(), nullptr);

    // Generate timestamp for filename
    std::time_t now = std::time(nullptr);
    std::tm ltm{};
    localtime_s(&ltm, &now);

    std::wstringstream fname_ss;
    fname_ss << output_dir << L"\\benchmark_"
             << std::setfill(L'0')
             << (ltm.tm_year + 1900)
             << std::setw(2) << (ltm.tm_mon + 1)
             << std::setw(2) << ltm.tm_mday << L"_"
             << std::setw(2) << ltm.tm_hour
             << std::setw(2) << ltm.tm_min
             << std::setw(2) << ltm.tm_sec << L".html";

    std::wstring out_path = fname_ss.str();
    std::ofstream html(out_path);
    if (!html.is_open()) return L"";

    // Format JSON array for frametime points
    std::stringstream ft_json;
    ft_json << "[";
    for (size_t i = 0; i < frametime_samples.size(); ++i) {
        ft_json << std::fixed << std::setprecision(2) << frametime_samples[i];
        if (i + 1 < frametime_samples.size()) ft_json << ",";
    }
    ft_json << "]";

    // Part 1: Head & CSS (Liquid Glass Edition)
    html << R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AnyPerfomans - Benchmark Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/html2canvas@1.4.1/dist/html2canvas.min.js"></script>
    <style>
        :root {
            --bg-dark: #07090e;
            --card-bg: rgba(13, 17, 26, 0.78);
            --card-border: rgba(0, 240, 255, 0.22);
            --neon-cyan: #00f0ff;
            --neon-green: #00e676;
            --neon-red: #ff334b;
            --neon-amber: #ffb300;
            --accent-glow: rgba(0, 240, 255, 0.15);
            --text-main: #f5f6fa;
            --text-muted: #8c92a4;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            background: radial-gradient(circle at 50% 0%, #0d1e2e 0%, var(--bg-dark) 80%);
            color: var(--text-main);
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            min-height: 100vh;
            padding: 30px 20px;
        }
        .container { max-width: 1100px; margin: 0 auto; }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 25px;
            padding-bottom: 15px;
            border-bottom: 1px solid var(--card-border);
        }
        .logo { font-size: 24px; font-weight: 800; color: var(--neon-cyan); letter-spacing: 1.5px; text-shadow: 0 0 16px rgba(0, 240, 255, 0.4); }
        .meta-text { color: var(--text-muted); font-size: 14px; }
        .actions { display: flex; gap: 12px; }
        .btn {
            background: linear-gradient(135deg, #00b4d8 0%, #0077b6 100%);
            color: #fff;
            border: none;
            padding: 10px 18px;
            border-radius: 8px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
            box-shadow: 0 4px 15px var(--accent-glow);
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(0, 240, 255, 0.4); }
        .btn-outline {
            background: transparent;
            border: 1px solid var(--card-border);
            color: var(--text-main);
        }
        .btn-outline:hover { background: rgba(0, 240, 255, 0.1); }

        /* KPI Cards Grid */
        .kpi-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 25px;
        }
        .card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            padding: 18px;
            backdrop-filter: blur(12px);
            box-shadow: 0 8px 30px rgba(0,0,0,0.5);
            transition: transform 0.2s ease, border-color 0.2s ease;
        }
        .card:hover { transform: translateY(-2px); border-color: rgba(0, 240, 255, 0.45); }
        .card-title { font-size: 11px; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1.2px; }
        .card-val { font-size: 32px; font-weight: 800; margin: 6px 0; color: #fff; }
        .val-fps { color: var(--neon-green); text-shadow: 0 0 12px rgba(0, 230, 118, 0.3); }
        .val-low { color: var(--neon-cyan); }
        .card-sub { font-size: 12px; color: var(--text-muted); line-height: 1.4; }

        /* Bottleneck & Chart Section */
        .chart-card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            padding: 24px;
            margin-bottom: 25px;
            backdrop-filter: blur(12px);
        }
        .chart-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }

        /* Table */
        .table-card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            padding: 20px;
            backdrop-filter: blur(12px);
        }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th, td { padding: 12px 14px; text-align: left; border-bottom: 1px solid rgba(255,255,255,0.06); font-size: 14px; }
        th { color: var(--text-muted); font-size: 12px; text-transform: uppercase; letter-spacing: 0.8px; }
        tr:hover td { background: rgba(255,255,255,0.02); }
        .badge {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: 700;
            background: rgba(0, 240, 255, 0.15);
            color: var(--neon-cyan);
            border: 1px solid rgba(0, 240, 255, 0.35);
        }
        .badge-red {
            background: rgba(255, 51, 75, 0.15);
            color: var(--neon-red);
            border-color: rgba(255, 51, 75, 0.35);
        }
        .badge-green {
            background: rgba(0, 230, 118, 0.15);
            color: var(--neon-green);
            border-color: rgba(0, 230, 118, 0.35);
        }
    </style>
</head>
<body>
    <div class="container" id="report-container">
        <div class="header">
            <div>
                <div class="logo">⚡ ANYPERFOMANS <span style="font-size: 14px; font-weight: 500; color: #8c92a4;">LIQUID GLASS</span></div>
                <div class="meta-text">Target: <strong>)html";

    html << escape_html(wide_to_utf8(record.session_name))
         << "</strong> | Duration: " << record.duration_seconds << "s | High-Precision ETW Present Engine</div>\n"
         << R"html(            </div>
            <div class="actions">
                <button class="btn" onclick="copyCardAsImage()">📸 Copy Card for Discord/TG</button>
                <button class="btn btn-outline" onclick="window.print()">🖨 Print / PDF</button>
            </div>
        </div>

        <div class="kpi-grid">
            <div class="card">
                <div class="card-title">Average Framerate</div>
                <div class="card-val val-fps">)html"
         << std::fixed << std::setprecision(1) << record.avg_fps
         << R"html(</div>
                <div class="card-sub">Min: )html"
         << std::fixed << std::setprecision(1) << record.min_fps
         << " | Max: " << std::fixed << std::setprecision(1) << record.max_fps
         << R"html( FPS</div>
            </div>
            <div class="card">
                <div class="card-title">Low Percentiles</div>
                <div class="card-val val-low">)html"
         << std::fixed << std::setprecision(1) << record.one_percent_low
         << R"html(</div>
                <div class="card-sub">1% Low | 0.1%: )html"
         << std::fixed << std::setprecision(1) << record.zero_one_percent_low
         << " | 0.01%: " << std::fixed << std::setprecision(1) << record.zero_zero_one_percent_low
         << R"html( FPS</div>
            </div>
            <div class="card">
                <div class="card-title">Frametime & Pacing</div>
                <div class="card-val">)html"
         << std::fixed << std::setprecision(2) << record.avg_frametime_ms
         << R"html( ms</div>
                <div class="card-sub">Jitter (&sigma;): )html"
         << std::fixed << std::setprecision(2) << record.pacing_std_dev_ms
         << R"html( ms</div>
            </div>
            <div class="card">
                <div class="card-title">Stutters & Hitches</div>
                <div class="card-val" style="color: )html"
         << (record.stutter_count > 5 ? "var(--neon-red)" : "var(--neon-green)")
         << R"html(;">)html"
         << record.stutter_count
         << R"html(</div>
                <div class="card-sub">)html"
         << std::fixed << std::setprecision(2) << record.stutter_percentage
         << R"html(% of captured frames</div>
            </div>
            <div class="card">
                <div class="card-title">GPU Telemetry</div>
                <div class="card-val">)html"
         << gpu.core_usage_percent
         << R"html(%</div>
                <div class="card-sub">)html";

    html << escape_html(wide_to_utf8(gpu.adapter_name));
    if (gpu.temperature_c > 0) {
        html << "<br>Temp: " << gpu.temperature_c << "&deg;C";
        if (gpu.hotspot_temp_c > 0) html << " (Hotspot: " << gpu.hotspot_temp_c << "&deg;C)";
        if (gpu.vram_temp_c > 0) html << " (VRAM: " << gpu.vram_temp_c << "&deg;C)";
    }
    if (gpu.power_watts > 0) {
        html << " | Power: " << gpu.power_watts << " W";
    }

    // Determine Bottleneck State
    std::string bneck_label = "BALANCED / UNCONSTRAINED";
    std::string bneck_badge_class = "badge-green";
    std::string bneck_desc = "Balanced workload between CPU and GPU pipeline.";
    if (gpu.core_usage_percent >= 95) {
        bneck_label = "GPU BOUND (OPTIMAL)";
        bneck_badge_class = "badge";
        bneck_desc = "Graphics card is 95%+ saturated. System is fully leveraging the GPU.";
    } else if (record.pacing_std_dev_ms < 0.35 && gpu.core_usage_percent < 85) {
        bneck_label = "FRAMERATE CAPPED / VSYNC";
        bneck_badge_class = "badge";
        bneck_desc = "Framerate is constrained by an in-game or external FPS limiter.";
    } else if (gpu.core_usage_percent < 85) {
        bneck_label = "CPU / ENGINE LIMIT";
        bneck_badge_class = "badge-red";
        bneck_desc = "GPU load is below 85%. Rendering throughput is constrained by CPU or RAM bandwidth.";
    }

    html << R"html(</div>
            </div>
            <div class="card">
                <div class="card-title">Bottleneck Analysis</div>
                <div class="card-val" style="font-size: 18px; margin-top: 12px;"><span class="badge )html"
         << bneck_badge_class << R"html(" style="font-size: 13px; padding: 4px 10px;">)html"
         << bneck_label << R"html(</span></div>
                <div class="card-sub" style="margin-top: 8px;">)html"
         << bneck_desc
         << R"html(</div>
            </div>
        </div>

        <div class="chart-card">
            <div class="chart-header">
                <h3>Frametime Latency Timeline (ms)</h3>
                <span class="badge">Kernel DXGI / ETW Trace</span>
            </div>
            <canvas id="frametimeChart" height="95"></canvas>
        </div>

        <div class="table-card">
            <h3>Process Resource Footprint Attribution</h3>
            <table>
                <thead>
                    <tr>
                        <th>Process Name</th>
                        <th>PID</th>
                        <th>Average CPU %</th>
                        <th>Private RAM (Commit)</th>
                        <th>Role</th>
                    </tr>
                </thead>
                <tbody>)html";

    for (size_t i = 0; i < record.process_summaries.size(); ++i) {
        const auto& p = record.process_summaries[i];
        bool is_primary = (i == 0);
        html << "<tr>\n"
             << "<td><strong>" << escape_html(wide_to_utf8(p.name)) << "</strong></td>\n"
             << "<td>" << p.pid << "</td>\n"
             << "<td>" << std::fixed << std::setprecision(2) << p.avg_cpu_percent << "%</td>\n"
             << "<td>" << p.avg_ram_mb << " MB</td>\n"
             << "<td><span class=\"badge " << (is_primary ? "badge-green" : "") << "\">" << (is_primary ? "PRIMARY APP" : "SECONDARY") << "</span></td>\n"
             << "</tr>\n";
    }

    html << R"html(
                </tbody>
            </table>
        </div>
    </div>

    <script>
        const ftData = )html" << ft_json.str() << R"html(;
        const labels = ftData.map((_, i) => i);

        const ctx = document.getElementById('frametimeChart').getContext('2d');
        const gradient = ctx.createLinearGradient(0, 0, 0, 300);
        gradient.addColorStop(0, 'rgba(0, 240, 255, 0.45)');
        gradient.addColorStop(1, 'rgba(0, 240, 255, 0.0)');

        new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{
                    label: 'Frametime (ms)',
                    data: ftData,
                    borderColor: '#00f0ff',
                    backgroundColor: gradient,
                    fill: true,
                    tension: 0.1,
                    pointRadius: 0,
                    borderWidth: 1.5
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    legend: { display: false }
                },
                scales: {
                    x: { display: false },
                    y: {
                        grid: { color: 'rgba(255,255,255,0.06)' },
                        ticks: { color: '#8c92a4' },
                        suggestedMax: 33.3
                    }
                }
            }
        });

        async function copyCardAsImage() {
            try {
                const element = document.getElementById('report-container');
                const canvas = await html2canvas(element, { backgroundColor: '#07090e' });
                canvas.toBlob(async (blob) => {
                    await navigator.clipboard.write([
                        new ClipboardItem({ 'image/png': blob })
                    ]);
                    alert('📸 Benchmark card copied to clipboard! You can now press Ctrl+V in Discord or Telegram.');
                });
            } catch (err) {
                alert('Could not auto-copy to clipboard. Opening image in new tab instead.');
                const element = document.getElementById('report-container');
                const canvas = await html2canvas(element);
                const win = window.open();
                win.document.write('<img src="' + canvas.toDataURL() + '"/>');
            }
        }
    </script>
</body>
</html>)html";

    html.close();
    return out_path;
}

std::wstring ReportGenerator::generate_csv_report(
    const BenchmarkRecord& record,
    const std::vector<float>& frametime_samples,
    const GpuMetrics& gpu,
    const std::wstring& output_dir
) {
    CreateDirectoryW(output_dir.c_str(), nullptr);

    std::time_t now = std::time(nullptr);
    std::tm ltm{};
    localtime_s(&ltm, &now);

    std::wstringstream fname_ss;
    fname_ss << output_dir << L"\\benchmark_"
             << std::setfill(L'0')
             << (ltm.tm_year + 1900)
             << std::setw(2) << (ltm.tm_mon + 1)
             << std::setw(2) << ltm.tm_mday << L"_"
             << std::setw(2) << ltm.tm_hour
             << std::setw(2) << ltm.tm_min
             << std::setw(2) << ltm.tm_sec << L".csv";

    std::wstring out_path = fname_ss.str();
    std::ofstream csv(out_path);
    if (!csv.is_open()) return L"";

    // Metadata header comments (CapFrameX / PresentMon standard compatible)
    csv << "# AnyPerfomans Benchmark Run Log\n";
    csv << "# Session Target: " << wide_to_utf8(record.session_name) << "\n";
    csv << "# Duration (sec): " << std::fixed << std::setprecision(2) << record.duration_seconds << "\n";
    csv << "# Average FPS: " << std::fixed << std::setprecision(2) << record.avg_fps << "\n";
    csv << "# Min FPS: " << std::fixed << std::setprecision(2) << record.min_fps << "\n";
    csv << "# Max FPS: " << std::fixed << std::setprecision(2) << record.max_fps << "\n";
    csv << "# 1% Low FPS: " << std::fixed << std::setprecision(2) << record.one_percent_low << "\n";
    csv << "# 0.1% Low FPS: " << std::fixed << std::setprecision(2) << record.zero_one_percent_low << "\n";
    csv << "# 0.01% Low FPS: " << std::fixed << std::setprecision(2) << record.zero_zero_one_percent_low << "\n";
    csv << "# Pacing Jitter Std Dev (ms): " << std::fixed << std::setprecision(3) << record.pacing_std_dev_ms << "\n";
    csv << "# Stutter Count: " << record.stutter_count << "\n";
    csv << "# Stutter Percentage (%): " << std::fixed << std::setprecision(2) << record.stutter_percentage << "%\n";
    csv << "# GPU Name: " << wide_to_utf8(gpu.adapter_name) << "\n";
    csv << "# GPU Core Usage: " << gpu.core_usage_percent << "%\n";
    if (gpu.temperature_c > 0) csv << "# GPU Temp: " << gpu.temperature_c << " C\n";
    if (gpu.core_clock_mhz > 0) csv << "# GPU Clock: " << gpu.core_clock_mhz << " MHz\n";

    csv << "# Process Attribution:\n";
    for (size_t i = 0; i < record.process_summaries.size(); ++i) {
        const auto& p = record.process_summaries[i];
        csv << "#   - " << wide_to_utf8(p.name) << " [PID " << p.pid << "]: "
            << std::fixed << std::setprecision(2) << p.avg_cpu_percent << "% CPU, "
            << p.avg_ram_mb << " MB RAM (" << (i == 0 ? "PRIMARY" : "SECONDARY") << ")\n";
    }

    // CSV Data Table
    csv << "\nFrameIndex,Frametime_ms,FPS\n";
    for (size_t i = 0; i < frametime_samples.size(); ++i) {
        float ft = frametime_samples[i];
        float fps = (ft > 0.0f) ? (1000.0f / ft) : 0.0f;
        csv << i << "," << std::fixed << std::setprecision(3) << ft << ","
            << std::fixed << std::setprecision(1) << fps << "\n";
    }

    csv.close();
    return out_path;
}

void ReportGenerator::open_in_browser(const std::wstring& file_path) {
    ShellExecuteW(nullptr, L"open", file_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace anyperf
