﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#include <chrono>
#include <thread>
#include <memory>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>
#include "perf_monitor.h"
#include "cpu_info.h"
#include "qsv_osdep.h"
#include "qsv_control.h"
#include "qsv_util.h"
#if defined(_WIN32) || defined(_WIN64)
#include <psapi.h>
#else
#include <sys/time.h>
#include <sys/resource.h>

extern "C" {
extern char _binary_PerfMonitor_perf_monitor_pyw_start;
extern char _binary_PerfMonitor_perf_monitor_pyw_end;
extern char _binary_PerfMonitor_perf_monitor_pyw_size;
}

#endif //#if defined(_WIN32) || defined(_WIN64)

tstring CPerfMonitor::SelectedCounters(int select) {
    if (select == 0) {
        return _T("none");
    }
    tstring str;
    for (uint32_t i = 0; i < _countof(list_pref_monitor); i++) {
        if (list_pref_monitor[i].desc &&
            (select & list_pref_monitor[i].value) == list_pref_monitor[i].value) {
            if (str.length()) {
                str += _T(",");
            }
            str += list_pref_monitor[i].desc;
            select &= ~(list_pref_monitor[i].value);
        }
    }
    return str;
}

CPerfMonitor::CPerfMonitor() {
    memset(m_info, 0, sizeof(m_info));
    memset(&m_pipes, 0, sizeof(m_pipes));

    cpu_info_t cpu_info;
    get_cpu_info(&cpu_info);
    m_nLogicalCPU = cpu_info.logical_cores;
}

CPerfMonitor::~CPerfMonitor() {
    clear();
}

void CPerfMonitor::clear() {
    if (m_thCheck.joinable()) {
        m_bAbort = true;
        m_thCheck.join();
    }
    memset(m_info, 0, sizeof(m_info));
    m_nStep = 0;
    m_thMainThread.reset();
    m_thEncThread = nullptr;
    m_bAbort = false;
    m_bEncStarted = false;
    if (m_fpLog) {
        fprintf(m_fpLog.get(), "\n\n");
    }
    m_fpLog.reset();
    if (m_pipes.f_stdin) {
        fclose(m_pipes.f_stdin);
        m_pipes.f_stdin = NULL;
    }
    m_pProcess.reset();
}

int CPerfMonitor::createPerfMpnitorPyw(const TCHAR *pywPath) {
    //リソースを取り出し
    int ret = 0;
    uint32_t resourceSize = 0;
    FILE *fp = NULL;
    const char *pDataPtr = NULL;
#if defined(_WIN32) || defined(_WIN64)
    HRSRC hResource = NULL;
    HGLOBAL hResourceData = NULL;
    HMODULE hModule = GetModuleHandleA(NULL);
    if (   NULL == hModule
        || NULL == (hResource = FindResource(hModule, _T("PERF_MONITOR_PYW"), _T("PERF_MONITOR_SRC")))
        || NULL == (hResourceData = LoadResource(hModule, hResource))
        || NULL == (pDataPtr = (const char *)LockResource(hResourceData))
        || 0    == (resourceSize = SizeofResource(hModule, hResource))) {
        ret = 1;
    } else
#else
    pDataPtr = &_binary_PerfMonitor_perf_monitor_pyw_start;
    resourceSize = (uint32_t)(size_t)&_binary_PerfMonitor_perf_monitor_pyw_size;
#endif //#if defined(_WIN32) || defined(_WIN64)
    if (_tfopen_s(&fp, pywPath, _T("wb")) || NULL == fp) {
        ret = 1;
    } else if (resourceSize != fwrite(pDataPtr, 1, resourceSize, fp)) {
        ret = 1;
    }
    if (fp)
        fclose(fp);
    return ret;
}

void CPerfMonitor::write_header(FILE *fp, int nSelect) {
    if (fp == NULL || nSelect == 0) {
        return;
    }
    std::string str;
    if (nSelect & PERF_MONITOR_CPU) {
        str += ",cpu (%)";
    }
    if (nSelect & PERF_MONITOR_CPU_KERNEL) {
        str += ",cpu kernel (%)";
    }
    if (nSelect & PERF_MONITOR_THREAD_MAIN) {
        str += ",cpu main thread (%)";
    }
    if (nSelect & PERF_MONITOR_THREAD_ENC) {
        str += ",cpu enc thread (%)";
    }
    if (nSelect & PERF_MONITOR_MEM_PRIVATE) {
        str += ",mem private (MB)";
    }
    if (nSelect & PERF_MONITOR_MEM_VIRTUAL) {
        str += ",mem virtual (MB)";
    }
    if (nSelect & PERF_MONITOR_FRAME_IN) {
        str += ",frame in";
    }
    if (nSelect & PERF_MONITOR_FRAME_OUT) {
        str += ",frame out";
    }
    if (nSelect & PERF_MONITOR_FPS) {
        str += ",enc speed (fps)";
    }
    if (nSelect & PERF_MONITOR_FPS_AVG) {
        str += ",enc speed avg (fps)";
    }
    if (nSelect & PERF_MONITOR_BITRATE) {
        str += ",bitrate (kbps)";
    }
    if (nSelect & PERF_MONITOR_BITRATE_AVG) {
        str += ",bitrate avg (kbps)";
    }
    if (nSelect & PERF_MONITOR_IO_READ) {
        str += ",read (MB/s)";
    }
    if (nSelect & PERF_MONITOR_IO_WRITE) {
        str += ",write (MB/s)";
    }
    str += "\n";
    fwrite(str.c_str(), 1, str.length(), fp);
    fflush(fp);
}

int CPerfMonitor::init(tstring filename, const TCHAR *pPythonPath,
    int interval, int nSelectOutputLog, int nSelectOutputMatplot,
    std::unique_ptr<void, handle_deleter> thMainThread,
    std::shared_ptr<CQSVLog> pQSVLog) {
    clear();

    m_nCreateTime100ns = (int64_t)(clock() * (1e7 / CLOCKS_PER_SEC) + 0.5);
    m_sMonitorFilename = filename;
    m_nInterval = interval;
    m_nSelectOutputMatplot = nSelectOutputMatplot;
    m_nSelectOutputLog = nSelectOutputLog;
    m_nSelectCheck = m_nSelectOutputLog | m_nSelectOutputMatplot;
    m_thMainThread = std::move(thMainThread);

    if (!m_fpLog) {
        m_fpLog = std::unique_ptr<FILE, fp_deleter>(_tfopen(m_sMonitorFilename.c_str(), _T("a")));
        if (!m_fpLog) {
            (*pQSVLog)(QSV_LOG_WARN, _T("Failed to open performance monitor log file: %s\n"), m_sMonitorFilename.c_str());
            (*pQSVLog)(QSV_LOG_WARN, _T("performance monitoring disabled.\n"));
            return 1;
        }
    }

    if (m_nSelectOutputMatplot) {
#if defined(_WIN32) || defined(_WIN64)
        m_pProcess = std::unique_ptr<CPipeProcess>(new CPipeProcessWin());
        m_pipes.stdIn.mode = PIPE_MODE_ENABLE;
        TCHAR tempDir[1024] = { 0 };
        TCHAR tempPath[1024] = { 0 };
        GetModuleFileName(NULL, tempDir, _countof(tempDir));
        PathRemoveFileSpec(tempDir);
        PathCombine(tempPath, tempDir, strsprintf(_T("qsvencc_perf_monitor.pyw"), GetProcessId(GetCurrentProcess())).c_str());
        m_sPywPath = tempPath;
        uint32_t priority = NORMAL_PRIORITY_CLASS;
#else
        m_pProcess = std::unique_ptr<CPipeProcess>(new CPipeProcessLinux());
        m_pipes.stdIn.mode = PIPE_MODE_ENABLE;
        m_sPywPath = tstring(_T("/tmp/")) + strsprintf(_T("qsvencc_perf_monitor_%d.pyw"), (int)getpid());
        uint32_t priority = 0;
#endif
        if (createPerfMpnitorPyw(m_sPywPath.c_str())) {
            (*pQSVLog)(QSV_LOG_WARN, _T("Failed to create file qsvencc_perf_monitor.pyw for performance monitor plot.\n"));
            (*pQSVLog)(QSV_LOG_WARN, _T("performance monitor plot disabled.\n"));
            m_nSelectOutputMatplot = 0;
        } else {
            tstring sInterval = strsprintf(_T("%d"), interval);
            tstring sPythonPath = (pPythonPath) ? pPythonPath : _T("python");
            sPythonPath = tstring(_T("\"")) + sPythonPath + tstring(_T("\""));
            m_sPywPath = tstring(_T("\"")) + m_sPywPath + tstring(_T("\""));
            std::vector<const TCHAR *> args;
            args.push_back(sPythonPath.c_str());
            args.push_back(m_sPywPath.c_str());
            args.push_back(_T("-i"));
            args.push_back(sInterval.c_str());
            args.push_back(nullptr);
            if (m_pProcess->run(args, nullptr, &m_pipes, priority, false, false)) {
                (*pQSVLog)(QSV_LOG_WARN, _T("Failed to run performance monitor plot.\n"));
                (*pQSVLog)(QSV_LOG_WARN, _T("performance monitor plot disabled.\n"));
                m_nSelectOutputMatplot = 0;
#if defined(_WIN32) || defined(_WIN64)
            } else {
                WaitForInputIdle(dynamic_cast<CPipeProcessWin *>(m_pProcess.get())->getProcessInfo().hProcess, INFINITE);
#endif
            }
        }
    }

    //未実装
    m_nSelectCheck &= (~PERF_MONITOR_FRAME_IN);

    //未実装
#if !(defined(_WIN32) || defined(_WIN64))
    m_nSelectCheck &= (~PERF_MONITOR_THREAD_MAIN);
    m_nSelectCheck &= (~PERF_MONITOR_THREAD_ENC);
    m_nSelectOutputMatplot = 0;
#endif //#if defined(_WIN32) || defined(_WIN64)

    m_nSelectOutputLog &= m_nSelectCheck;
    m_nSelectOutputMatplot &= m_nSelectCheck;

    (*pQSVLog)(QSV_LOG_DEBUG, _T("Performace Monitor: %s\n"), CPerfMonitor::SelectedCounters(m_nSelectOutputLog).c_str());
    (*pQSVLog)(QSV_LOG_DEBUG, _T("Performace Plot   : %s\n"), CPerfMonitor::SelectedCounters(m_nSelectOutputMatplot).c_str());

    write_header(m_fpLog.get(),   m_nSelectOutputLog);
    write_header(m_pipes.f_stdin, m_nSelectOutputMatplot);

    if (m_nSelectOutputLog || m_nSelectOutputMatplot) {
        m_thCheck = std::thread(loader, this);
    }
    return 0;
}

void CPerfMonitor::SetEncStatus(std::shared_ptr<CEncodeStatusInfo> encStatus) {
    m_pEncStatus = encStatus;
    m_nOutputFPSScale = encStatus->m_nOutputFPSScale;
    m_nOutputFPSRate = encStatus->m_nOutputFPSRate;
}

void CPerfMonitor::SetEncThread(HANDLE thEncThread) {
    m_thEncThread = thEncThread;
}

void CPerfMonitor::check() {
    PerfInfo *pInfoNew = &m_info[(m_nStep + 1) & 1];
    PerfInfo *pInfoOld = &m_info[ m_nStep      & 1];
    memcpy(pInfoNew, pInfoOld, sizeof(pInfoNew[0]));

#if defined(_WIN32) || defined(_WIN64)
    const auto hProcess = GetCurrentProcess();
    auto getThreadTime = [](HANDLE hThread, PROCESS_TIME *time) {
        GetThreadTimes(hThread, (FILETIME *)&time->creation, (FILETIME *)&time->exit, (FILETIME *)&time->kernel, (FILETIME *)&time->user);
    };

    //メモリ情報
    PROCESS_MEMORY_COUNTERS mem_counters = { 0 };
    mem_counters.cb = sizeof(PROCESS_MEMORY_COUNTERS);
    GetProcessMemoryInfo(hProcess, &mem_counters, sizeof(mem_counters));
    pInfoNew->mem_private = mem_counters.WorkingSetSize;
    pInfoNew->mem_virtual = mem_counters.PagefileUsage;

    //IO情報
    IO_COUNTERS io_counters = { 0 };
    GetProcessIoCounters(hProcess, &io_counters);
    pInfoNew->io_total_read = io_counters.ReadTransferCount;
    pInfoNew->io_total_write = io_counters.WriteTransferCount;

    //現在時刻
    uint64_t current_time = 0;
    SYSTEMTIME systime = { 0 };
    GetSystemTime(&systime);
    SystemTimeToFileTime(&systime, (FILETIME *)&current_time);

    //CPU情報
    PROCESS_TIME pt = { 0 };
    GetProcessTimes(hProcess, (FILETIME *)&pt.creation, (FILETIME *)&pt.exit, (FILETIME *)&pt.kernel, (FILETIME *)&pt.user);
    pInfoNew->time_us = (current_time - pt.creation) / 10;
    const double time_diff_inv = 1.0 / (pInfoNew->time_us - pInfoOld->time_us);
#else
    struct rusage usage = { 0 };
    getrusage(RUSAGE_SELF, &usage);

    //現在時間
    uint64_t current_time = clock() * (1e7 / CLOCKS_PER_SEC);

    std::string proc_dir = strsprintf("/proc/%d/", (int)getpid());
    //メモリ情報
    FILE *fp_mem = popen((std::string("cat ") + proc_dir + std::string("status")).c_str(), "r");
    if (fp_mem) {
        char buffer[2048] = { 0 };
        while (NULL != fgets(buffer, _countof(buffer), fp_mem)) {
            if (nullptr != strstr(buffer, "VmSize")) {
                long long i = 0;
                if (1 == sscanf(buffer, "VmSize: %lld kB", &i)) {
                    pInfoNew->mem_virtual = i << 10;
                }
            } else if (nullptr != strstr(buffer, "VmRSS")) {
                long long i = 0;
                if (1 == sscanf(buffer, "VmRSS: %lld kB", &i)) {
                    pInfoNew->mem_private = i << 10;
                }
            }
        }
        fclose(fp_mem);
    }
    //IO情報
    FILE *fp_io = popen((std::string("cat ") + proc_dir + std::string("io")).c_str(), "r");
    if (fp_io) {
        char buffer[2048] = { 0 };
        while (NULL != fgets(buffer, _countof(buffer), fp_io)) {
            if (nullptr != strstr(buffer, "rchar:")) {
                long long i = 0;
                if (1 == sscanf(buffer, "rchar: %lld", &i)) {
                    pInfoNew->io_total_read = i;
                }
            } else if (nullptr != strstr(buffer, "wchar")) {
                long long i = 0;
                if (1 == sscanf(buffer, "wchar: %lld", &i)) {
                    pInfoNew->io_total_write = i;
                }
            }
        }
        fclose(fp_io);
    }

    //CPU情報
    pInfoNew->time_us = (current_time - m_nCreateTime100ns) / 10;
    const double time_diff_inv = 1.0 / (pInfoNew->time_us - pInfoOld->time_us);
#endif

    if (pInfoNew->time_us > pInfoOld->time_us) {
#if defined(_WIN32) || defined(_WIN64)
        pInfoNew->cpu_total_us = (pt.user + pt.kernel) / 10;
        pInfoNew->cpu_total_kernel_us = pt.kernel / 10;
#else
        int64_t cpu_user_us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
        int64_t cpu_kernel_us = usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
        pInfoNew->cpu_total_us = cpu_user_us + cpu_kernel_us;
        pInfoNew->cpu_total_kernel_us = cpu_kernel_us;
#endif //#if defined(_WIN32) || defined(_WIN64)

        //CPU使用率
        const double logical_cpu_inv       = 1.0 / m_nLogicalCPU;
        pInfoNew->cpu_percent        = (pInfoNew->cpu_total_us        - pInfoOld->cpu_total_us) * 100.0 * logical_cpu_inv * time_diff_inv;
        pInfoNew->cpu_kernel_percent = (pInfoNew->cpu_total_kernel_us - pInfoOld->cpu_total_kernel_us) * 100.0 * logical_cpu_inv * time_diff_inv;

        //IO情報
        pInfoNew->io_read_per_sec = (pInfoNew->io_total_read - pInfoOld->io_total_read) * time_diff_inv * 1e6;
        pInfoNew->io_write_per_sec = (pInfoNew->io_total_write - pInfoOld->io_total_write) * time_diff_inv * 1e6;

#if defined(_WIN32) || defined(_WIN64)
        //スレッドCPU使用率
        if (m_thMainThread) {
            getThreadTime(m_thMainThread.get(), &pt);
            pInfoNew->main_thread_total_active_us = (pt.user + pt.kernel) / 10;
            pInfoNew->main_thread_percent = (pInfoNew->main_thread_total_active_us - pInfoOld->main_thread_total_active_us) * 100.0 * logical_cpu_inv * time_diff_inv;
        }

        if (m_thEncThread) {
            DWORD exit_code = 0;
            if (0 != GetExitCodeThread(m_thEncThread, &exit_code) && exit_code == STILL_ACTIVE) {
                getThreadTime(m_thEncThread, &pt);
                pInfoNew->enc_thread_total_active_us = (pt.user + pt.kernel) / 10;
                pInfoNew->enc_thread_percent  = (pInfoNew->enc_thread_total_active_us  - pInfoOld->enc_thread_total_active_us) * 100.0 * logical_cpu_inv * time_diff_inv;
            } else {
                pInfoNew->enc_thread_percent = 0.0;
            }
        }

#endif //defined(_WIN32) || defined(_WIN64)
    }

    if (!m_bEncStarted && m_pEncStatus) {
        m_bEncStarted = m_pEncStatus->getEncStarted();
        if (m_bEncStarted) {
            m_nEncStartTime = m_pEncStatus->getStartTimeMicroSec();
        }
    }

    pInfoNew->bitrate_kbps = 0;
    pInfoNew->frames_out_byte = 0;
    pInfoNew->fps = 0.0;
    if (m_bEncStarted && m_pEncStatus) {
        sEncodeStatusData data = { 0 };
        m_pEncStatus->GetEncodeData(&data);

        //fps情報
        pInfoNew->frames_out = data.nProcessedFramesNum;
        if (pInfoNew->frames_out > pInfoOld->frames_out) {
            pInfoNew->fps_avg = pInfoNew->frames_out / (double)(current_time / 10 - m_nEncStartTime) * 1e6;
            if (pInfoNew->time_us > pInfoOld->time_us) {
                pInfoNew->fps     = (pInfoNew->frames_out - pInfoOld->frames_out) * time_diff_inv * 1e6;
            }

            //ビットレート情報
            double videoSec     = pInfoNew->frames_out * m_nOutputFPSScale / (double)m_nOutputFPSRate;
            double videoSecDiff = (pInfoNew->frames_out - pInfoOld->frames_out) * m_nOutputFPSScale / (double)m_nOutputFPSRate;

            pInfoNew->frames_out_byte = data.nWrittenBytes;
            pInfoNew->bitrate_kbps_avg =  pInfoNew->frames_out_byte * 8.0 / videoSec * 1e-3;
            if (pInfoNew->time_us > pInfoOld->time_us) {
                pInfoNew->bitrate_kbps     = (pInfoNew->frames_out_byte - pInfoOld->frames_out_byte) * 8.0 / videoSecDiff * 1e-3;
            }
        }
    }

    m_nStep++;
}

void CPerfMonitor::write(FILE *fp, int nSelect) {
    if (fp == NULL) {
        return;
    }
    const PerfInfo *pInfo = &m_info[m_nStep & 1];
    std::string str = strsprintf("%lf", pInfo->time_us * 1e-6);
    if (nSelect & PERF_MONITOR_CPU) {
        str += strsprintf(",%lf", pInfo->cpu_percent);
    }
    if (nSelect & PERF_MONITOR_CPU_KERNEL) {
        str += strsprintf(",%lf", pInfo->cpu_kernel_percent);
    }
    if (nSelect & PERF_MONITOR_THREAD_MAIN) {
        str += strsprintf(",%lf", pInfo->main_thread_percent);
    }
    if (nSelect & PERF_MONITOR_THREAD_ENC) {
        str += strsprintf(",%lf", pInfo->enc_thread_percent);
    }
    if (nSelect & PERF_MONITOR_MEM_PRIVATE) {
        str += strsprintf(",%.2lf", pInfo->mem_private / (double)(1024 * 1024));
    }
    if (nSelect & PERF_MONITOR_MEM_VIRTUAL) {
        str += strsprintf(",%.2lf", pInfo->mem_virtual / (double)(1024 * 1024));
    }
    if (nSelect & PERF_MONITOR_FRAME_IN) {
        str += strsprintf(",%d", pInfo->frames_in);
    }
    if (nSelect & PERF_MONITOR_FRAME_OUT) {
        str += strsprintf(",%d", pInfo->frames_out);
    }
    if (nSelect & PERF_MONITOR_FPS) {
        str += strsprintf(",%lf", pInfo->fps);
    }
    if (nSelect & PERF_MONITOR_FPS_AVG) {
        str += strsprintf(",%lf", pInfo->fps_avg);
    }
    if (nSelect & PERF_MONITOR_BITRATE) {
        str += strsprintf(",%lf", pInfo->bitrate_kbps);
    }
    if (nSelect & PERF_MONITOR_BITRATE_AVG) {
        str += strsprintf(",%lf", pInfo->bitrate_kbps_avg);
    }
    if (nSelect & PERF_MONITOR_IO_READ) {
        str += strsprintf(",%lf", pInfo->io_read_per_sec / (double)(1024 * 1024));
    }
    if (nSelect & PERF_MONITOR_IO_WRITE) {
        str += strsprintf(",%lf", pInfo->io_write_per_sec / (double)(1024 * 1024));
    }
    str += "\n";
    fwrite(str.c_str(), 1, str.length(), fp);
    if (fp == m_pipes.f_stdin) {
        fflush(fp);
    }
}

void CPerfMonitor::loader(void *prm) {
    reinterpret_cast<CPerfMonitor*>(prm)->run();
}

void CPerfMonitor::run() {
    while (!m_bAbort) {
        check();
        write(m_fpLog.get(),   m_nSelectOutputLog);
        write(m_pipes.f_stdin, m_nSelectOutputMatplot);
        std::this_thread::sleep_for(std::chrono::milliseconds(m_nInterval));
    }
    check();
    write(m_fpLog.get(),   m_nSelectOutputLog);
    write(m_pipes.f_stdin, m_nSelectOutputMatplot);
}