#include "anyperf/report_generator.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <shellapi.h>

namespace anyperf {

static std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

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

    // Part 1: Head & CSS
    html << R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AnyPerfomans Benchmark Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/html2canvas@1.4.1/dist/html2canvas.min.js"></script>
    <style>
        :root {
            --bg-dark: #0a0b0f;
            --card-bg: rgba(18, 20, 28, 0.82);
            --border-red: rgba(230, 46, 67, 0.35);
            --neon-red: #ff334b;
            --accent-glow: rgba(255, 51, 75, 0.15);
            --text-main: #f5f6fa;
            --text-muted: #8c92a4;
            --green: #00e676;
            --yellow: #ffb300;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            background: radial-gradient(circle at 50% 0%, #20080d 0%, var(--bg-dark) 75%);
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
            border-bottom: 1px solid var(--border-red);
        }
        .logo { font-size: 24px; font-weight: 800; color: var(--neon-red); letter-spacing: 1.5px; }
        .meta-text { color: var(--text-muted); font-size: 14px; }
        .actions { display: flex; gap: 12px; }
        .btn {
            background: linear-gradient(135deg, #e62e43 0%, #aa1426 100%);
            color: #fff;
            border: none;
            padding: 10px 18px;
            border-radius: 8px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
            box-shadow: 0 4px 15px var(--accent-glow);
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(255, 51, 75, 0.4); }
        .btn-outline {
            background: transparent;
            border: 1px solid var(--border-red);
            color: var(--text-main);
        }
        .btn-outline:hover { background: rgba(230, 46, 67, 0.1); }

        /* KPI Cards Grid */
        .kpi-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 25px;
        }
        .card {
            background: var(--card-bg);
            border: 1px solid var(--border-red);
            border-radius: 12px;
            padding: 18px;
            backdrop-filter: blur(12px);
            box-shadow: 0 8px 30px rgba(0,0,0,0.5);
        }
        .card-title { font-size: 12px; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1px; }
        .card-val { font-size: 32px; font-weight: 800; margin: 6px 0; color: #fff; }
        .val-fps { color: var(--green); }
        .val-low { color: var(--neon-red); }
        .card-sub { font-size: 13px; color: var(--text-muted); }

        /* Chart Section */
        .chart-card {
            background: var(--card-bg);
            border: 1px solid var(--border-red);
            border-radius: 12px;
            padding: 24px;
            margin-bottom: 25px;
            backdrop-filter: blur(12px);
        }
        .chart-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }

        /* Table */
        .table-card {
            background: var(--card-bg);
            border: 1px solid var(--border-red);
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
            background: rgba(230, 46, 67, 0.2);
            color: var(--neon-red);
            border: 1px solid rgba(230, 46, 67, 0.4);
        }
    </style>
</head>
<body>
    <div class="container" id="report-container">
        <div class="header">
            <div>
                <div class="logo">⚡ ANYPERFOMANS</div>
                <div class="meta-text">Target: <strong>)html";

    html << wide_to_utf8(record.session_name)
         << "</strong> | Duration: " << record.duration_seconds << "s | Generated via Kernel ETW Engine</div>\n"
         << R"html(            </div>
            <div class="actions">
                <button class="btn" onclick="copyCardAsImage()">📸 Copy Card for Discord/TG</button>
                <button class="btn btn-outline" onclick="window.print()">🖨 Print / PDF</button>
            </div>
        </div>

        <div class="kpi-grid">
            <div class="card">
                <div class="card-title">Average FPS</div>
                <div class="card-val val-fps">)html"
         << std::fixed << std::setprecision(1) << record.avg_fps
         << R"html(</div>
                <div class="card-sub">Avg Frametime: )html"
         << std::fixed << std::setprecision(2) << record.avg_frametime_ms
         << R"html( ms</div>
            </div>
            <div class="card">
                <div class="card-title">1% Low FPS</div>
                <div class="card-val val-low">)html"
         << std::fixed << std::setprecision(1) << record.one_percent_low
         << R"html(</div>
                <div class="card-sub">Frame Delivery Consistency</div>
            </div>
            <div class="card">
                <div class="card-title">GPU Telemetry</div>
                <div class="card-val">)html"
         << gpu.core_usage_percent
         << R"html(%</div>
                <div class="card-sub">)html"
         << wide_to_utf8(gpu.adapter_name) << (gpu.temperature_c > 0 ? (" | " + std::to_string(gpu.temperature_c) + " C") : "")
         << R"html(</div>
            </div>
            <div class="card">
                <div class="card-title">Monitored Apps</div>
                <div class="card-val">)html"
         << record.process_summaries.size()
         << R"html(</div>
                <div class="card-sub">Concurrent Process Overhead</div>
            </div>
        </div>

        <div class="chart-card">
            <div class="chart-header">
                <h3>Frametime Latency Timeline (ms)</h3>
                <span class="badge">Kernel DXGI Trace</span>
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
             << "<td><strong>" << wide_to_utf8(p.name) << "</strong></td>\n"
             << "<td>" << p.pid << "</td>\n"
             << "<td>" << std::fixed << std::setprecision(2) << p.avg_cpu_percent << "%</td>\n"
             << "<td>" << p.avg_ram_mb << " MB</td>\n"
             << "<td><span class=\"badge\">" << (is_primary ? "PRIMARY APP" : "SECONDARY") << "</span></td>\n"
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
        gradient.addColorStop(0, 'rgba(255, 51, 75, 0.4)');
        gradient.addColorStop(1, 'rgba(255, 51, 75, 0.0)');

        new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{
                    label: 'Frametime (ms)',
                    data: ftData,
                    borderColor: '#ff334b',
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
                const canvas = await html2canvas(element, { backgroundColor: '#0a0b0f' });
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

void ReportGenerator::open_in_browser(const std::wstring& file_path) {
    ShellExecuteW(nullptr, L"open", file_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace anyperf
