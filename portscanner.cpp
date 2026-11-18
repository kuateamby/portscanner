#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")


static const int MAX_SOCKETS_PER_BATCH = 4096;
static const int POLL_TIMEOUT_MS = 40;
static const int MAX_CONNECT_WAIT_MS = 250;
static const int MAX_THREADS = 128;


static std::mutex g_printMutex;

static std::string serviceName(int port)
{
    switch (port) {
    case 21: return "ftp";    case 22: return "ssh";
    case 23: return "telnet"; case 25: return "smtp";
    case 53: return "dns";    case 80: return "http";
    case 110:return "pop3";   case 135:return "msrpc";
    case 139:return "netbios";case 143:return "imap";
    case 443:return "https";  case 445:return "smb";
    case 1433:return "mssql"; case 3306:return "mysql";
    case 3389:return "rdp";   case 5432:return "postgres";
    case 5900:return "vnc";   case 6379:return "redis";
    case 8080:return "http-alt";
    default: return "";
    }
}
static std::string udpServiceName(int port)
{
    switch (port) {
    case 53: return "dns";    case 67: return "dhcp";
    case 69: return "tftp";   case 123: return "ntp";
    case 137:return "netbios-ns"; case 161: return "snmp";
    case 500:return "ike";    case 1900:return "ssdp";
    case 5353:return "mdns";
    default: return "";
    }
}


struct PendingSocket {
    SOCKET    sock;
    int       port;
    ULONGLONG startTick;
};

static void scanTcpChunkFast(const std::string& ip,
    const std::vector<int>& ports,
    size_t from, size_t to,
    std::vector<int>& foundLocal)
{
    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    std::vector<PendingSocket> pending;
    pending.reserve(MAX_SOCKETS_PER_BATCH);

    std::vector<WSAPOLLFD> pollfds;
    pollfds.reserve(MAX_SOCKETS_PER_BATCH);

    size_t nextPortIdx = from;

    while (nextPortIdx < to || !pending.empty())
    {

        while (pending.size() < (size_t)MAX_SOCKETS_PER_BATCH && nextPortIdx < to)
        {
            int port = ports[nextPortIdx++];
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) continue;


            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);


            int nodelay = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

            sockaddr_in a = addr;
            a.sin_port = htons((u_short)port);

            int r = connect(s, (sockaddr*)&a, sizeof(a));
            if (r == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                    closesocket(s);
                    continue;
                }
            }
            PendingSocket ps;
            ps.sock = s;
            ps.port = port;
            ps.startTick = GetTickCount64();
            pending.push_back(ps);
        }

        if (pending.empty()) break;


        pollfds.clear();
        for (size_t i = 0; i < pending.size(); ++i) {
            WSAPOLLFD pfd;
            pfd.fd = pending[i].sock;
            pfd.events = POLLWRNORM;   
            pfd.revents = 0;
            pollfds.push_back(pfd);
        }

        int n = WSAPoll(pollfds.data(), (ULONG)pollfds.size(), POLL_TIMEOUT_MS);
        if (n == SOCKET_ERROR) {
            for (size_t i = 0; i < pending.size(); ++i) closesocket(pending[i].sock);
            pending.clear();
            continue;
        }

        ULONGLONG now = GetTickCount64();


        for (size_t i = 0; i < pending.size(); )
        {
            SOCKET s = pending[i].sock;
            int    port = pending[i].port;
            short  rev = pollfds[i].revents;
            bool   tmo = ((now - pending[i].startTick) > (ULONGLONG)MAX_CONNECT_WAIT_MS);

            if (rev != 0) {

                if (rev & (POLLWRNORM | POLLOUT)) {
                    int e = 0;
                    int len = sizeof(e);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&e, &len);
                    if (e == 0) foundLocal.push_back(port);
                }

                closesocket(s);
                pending.erase(pending.begin() + i);
                pollfds.erase(pollfds.begin() + i);
                continue;
            }
            if (tmo) {
                closesocket(s);
                pending.erase(pending.begin() + i);
                pollfds.erase(pollfds.begin() + i);
                continue;
            }
            ++i;
        }
    }
}


static int checkUdpPortFast(const std::string& ip, int port, int timeoutMs)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    sockaddr_in a;
    ZeroMemory(&a, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    inet_pton(AF_INET, ip.c_str(), &a.sin_addr);

    const char probe[1] = { 0 };
    sendto(s, probe, 1, 0, (sockaddr*)&a, sizeof(a));

    WSAPOLLFD pfd;
    pfd.fd = s;
    pfd.events = POLLRDNORM;
    pfd.revents = 0;

    int n = WSAPoll(&pfd, 1, timeoutMs);
    int result = 0;
    if (n > 0) {
        char buf[512];
        sockaddr_in fromAddr;
        int fl = sizeof(fromAddr);
        int r = recvfrom(s, buf, sizeof(buf), 0, (sockaddr*)&fromAddr, &fl);
        if (r >= 0) result = 2;
        else if (WSAGetLastError() == WSAECONNRESET) result = 1;
    }
    closesocket(s);
    return result;
}


static std::string grabBanner(const std::string& ip, int port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return "";

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    sockaddr_in a;
    ZeroMemory(&a, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    inet_pton(AF_INET, ip.c_str(), &a.sin_addr);

    connect(s, (sockaddr*)&a, sizeof(a));

    WSAPOLLFD pfd;
    pfd.fd = s;
    pfd.events = POLLWRNORM;
    pfd.revents = 0;
    if (WSAPoll(&pfd, 1, 800) <= 0) { closesocket(s); return ""; }

    if (port == 80 || port == 8080 || port == 443) {
        const char* req = "HEAD / HTTP/1.0\r\n\r\n";
        send(s, req, (int)strlen(req), 0);
    }

    char buf[1024];
    ZeroMemory(buf, sizeof(buf));
    pfd.events = POLLRDNORM;
    pfd.revents = 0;
    int n = 0;
    if (WSAPoll(&pfd, 1, 1000) > 0)
        n = recv(s, buf, sizeof(buf) - 1, 0);

    closesocket(s);
    if (n <= 0) return "";
    std::string b(buf, n);
    size_t eol = b.find("\r\n");
    if (eol != std::string::npos) b = b.substr(0, eol);
    return b;
}

static void analyzeOs(const std::string& ip,
    const std::vector<int>& openPorts,
    std::string& outOs,
    int& outConfidence,
    std::vector<std::string>& outEvidence)
{
    outOs = "Unknown";
    outConfidence = 0;
    outEvidence.clear();

    bool has135 = false, has139 = false, has445 = false, has3389 = false, has1433 = false;
    bool has22 = false, has111 = false, has631 = false, has2049 = false;
    bool has548 = false, has5353 = false;

    for (size_t i = 0; i < openPorts.size(); ++i) {
        int p = openPorts[i];
        if (p == 135)  has135 = true;
        if (p == 139)  has139 = true;
        if (p == 445)  has445 = true;
        if (p == 3389) has3389 = true;
        if (p == 1433) has1433 = true;
        if (p == 22)   has22 = true;
        if (p == 111)  has111 = true;
        if (p == 631)  has631 = true;
        if (p == 2049) has2049 = true;
        if (p == 548)  has548 = true;
        if (p == 5353) has5353 = true;
    }

    std::string sshBanner, smtpBanner, ftpBanner, httpBanner;
    for (size_t i = 0; i < openPorts.size(); ++i) {
        int p = openPorts[i];
        if (p == 22) sshBanner = grabBanner(ip, 22);
        if (p == 25) smtpBanner = grabBanner(ip, 25);
        if (p == 21) ftpBanner = grabBanner(ip, 21);
        if (p == 80) httpBanner = grabBanner(ip, 80);
    }

    if (!sshBanner.empty())  outEvidence.push_back("SSH: " + sshBanner);
    if (!smtpBanner.empty()) outEvidence.push_back("SMTP: " + smtpBanner);
    if (!ftpBanner.empty())  outEvidence.push_back("FTP: " + ftpBanner);
    if (!httpBanner.empty()) outEvidence.push_back("HTTP: " + httpBanner);

    if (has135 || has139 || has445 || has3389 || has1433) {
        int c = 60;
        if (has135 && has445) c += 15;
        if (has3389)          c += 10;
        if (has1433)          c += 5;
        outOs = "Windows";
        outConfidence = (c > 95) ? 95 : c;
    }
    else if (has22 || has111 || has631 || has2049) {
        int c = 55;
        if (has22)   c += 10;
        if (has111)  c += 10;
        if (has2049) c += 5;
        outOs = "Linux/Unix";
        outConfidence = (c > 90) ? 90 : c;
    }
    else if (has548 || has5353) {
        outOs = "macOS";
        outConfidence = 65;
    }

    if (!sshBanner.empty()) {
        std::string b = sshBanner;
        for (size_t i = 0; i < b.size(); ++i) b[i] = (char)tolower((unsigned char)b[i]);
        if (b.find("ubuntu") != std::string::npos) { outOs = "Linux (Ubuntu)"; if (outConfidence < 85) outConfidence = 85; }
        if (b.find("debian") != std::string::npos) { outOs = "Linux (Debian)"; if (outConfidence < 85) outConfidence = 85; }
        if (b.find("centos") != std::string::npos) { outOs = "Linux (CentOS)"; if (outConfidence < 85) outConfidence = 85; }
        if (b.find("freebsd") != std::string::npos) { outOs = "FreeBSD";        if (outConfidence < 85) outConfidence = 85; }
    }

    if (!httpBanner.empty()) {
        std::string b = httpBanner;
        for (size_t i = 0; i < b.size(); ++i) b[i] = (char)tolower((unsigned char)b[i]);
        if (b.find("microsoft-iis") != std::string::npos) { outOs = "Windows (IIS)"; if (outConfidence < 90) outConfidence = 90; }
        if (b.find("apache") != std::string::npos && outOs == "Unknown") { outOs = "Linux/Unix (Apache)"; outConfidence = 60; }
        if (b.find("nginx") != std::string::npos && outOs == "Unknown") { outOs = "Linux/Unix (nginx)";  outConfidence = 60; }
    }
}


static void printUsage(const char* prog)
{
    std::cout <<
        "Usage:\n"
        "  " << prog << " -P <ip> [start] [end]      TCP scan\n"
        "  " << prog << " -P -u <ip> [start] [end]   UDP scan\n"
        "  " << prog << " -O <ip>                    OS detection\n";
}

int main(int argc, char* argv[])
{
    if (argc < 3) { printUsage(argv[0]); return 1; }

    std::string mode = argv[1];
    if (mode != "-P" && mode != "-O") { printUsage(argv[0]); return 1; }

    bool udpMode = false;
    int argStart = 2;
    if (mode == "-P" && argStart < argc && std::string(argv[argStart]) == "-u") {
        udpMode = true;
        argStart++;
    }
    if (argStart >= argc) { printUsage(argv[0]); return 1; }

    std::string ip = argv[argStart];

    sockaddr_in sa;
    ZeroMemory(&sa, sizeof(sa));
    if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        std::cerr << "[-] Bad IPv4: " << ip << "\n";
        return 1;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[-] WSAStartup failed\n";
        return 1;
    }


    if (mode == "-O")
    {
        std::cout << "[*] OS detection for " << ip << "\n";

        std::vector<int> probePorts;
        probePorts.push_back(21);   probePorts.push_back(22);
        probePorts.push_back(25);   probePorts.push_back(80);
        probePorts.push_back(111);  probePorts.push_back(135);
        probePorts.push_back(139);  probePorts.push_back(443);
        probePorts.push_back(445);  probePorts.push_back(548);
        probePorts.push_back(631);  probePorts.push_back(1433);
        probePorts.push_back(2049); probePorts.push_back(3306);
        probePorts.push_back(3389); probePorts.push_back(5353);
        probePorts.push_back(5432); probePorts.push_back(8080);

        std::vector<int> openPorts;

        int threadCount = 4;   
        std::vector<std::thread> threads;
        std::vector< std::vector<int> > localFound(threadCount);
        size_t chunk = (probePorts.size() + threadCount - 1) / threadCount;

        for (int t = 0; t < threadCount; ++t) {
            size_t from = (size_t)t * chunk;
            size_t to = from + chunk;
            if (to > probePorts.size()) to = probePorts.size();
            if (from >= to) break;
            threads.push_back(std::thread([&, t, from, to]() {
                scanTcpChunkFast(ip, probePorts, from, to, localFound[t]);
                }));
        }
        for (size_t i = 0; i < threads.size(); ++i) threads[i].join();
        for (int t = 0; t < threadCount; ++t)
            for (size_t j = 0; j < localFound[t].size(); ++j)
                openPorts.push_back(localFound[t][j]);
        std::sort(openPorts.begin(), openPorts.end());

        std::cout << "[*] Open ports: ";
        if (openPorts.empty()) std::cout << "(none)";
        else for (size_t i = 0; i < openPorts.size(); ++i) std::cout << openPorts[i] << " ";
        std::cout << "\n";

        std::string os;
        int conf = 0;
        std::vector<std::string> evidence;
        analyzeOs(ip, openPorts, os, conf, evidence);

        std::cout << "\n===== OS DETECTION RESULT =====\n";
        std::cout << "OS:         " << os << "\n";
        std::cout << "Confidence: " << conf << "%\n";
        std::cout << "Evidence:\n";
        if (evidence.empty()) std::cout << "  (no evidence collected)\n";
        else for (size_t i = 0; i < evidence.size(); ++i) std::cout << "  - " << evidence[i] << "\n";

        WSACleanup();
        return 0;
    }


    int startPort = 1;
    int endPort = 1024;
    if (argc > argStart + 1) startPort = std::atoi(argv[argStart + 1]);
    if (argc > argStart + 2) endPort = std::atoi(argv[argStart + 2]);
    if (startPort < 1)     startPort = 1;
    if (endPort > 65535) endPort = 65535;
    if (startPort > endPort) { int tmp = startPort; startPort = endPort; endPort = tmp; }

    int totalPorts = endPort - startPort + 1;
    std::vector<int> ports(totalPorts);
    for (int i = 0; i < totalPorts; ++i) ports[i] = startPort + i;


    unsigned hw = std::thread::hardware_concurrency();
    int threadCount = (int)hw * 4;
    if (threadCount < 16) threadCount = 16;
    if (threadCount > MAX_THREADS) threadCount = MAX_THREADS;
    if (totalPorts < 2048 && threadCount > 16) threadCount = 16;

    std::cout << "Scanning " << ip << " ports " << startPort << "-" << endPort
        << " (" << totalPorts << " ports, " << threadCount
        << " threads, batch=" << MAX_SOCKETS_PER_BATCH
        << ", mode=" << (udpMode ? "UDP" : "TCP") << ")\n\n";

    DWORD t0 = GetTickCount();

    std::vector<int> allFound;
    allFound.reserve(64);

    if (!udpMode) {
        size_t chunk = ((size_t)totalPorts + threadCount - 1) / threadCount;
        std::vector<std::thread> threads;
        std::vector< std::vector<int> > localFound(threadCount);

        for (int t = 0; t < threadCount; ++t) {
            size_t from = (size_t)t * chunk;
            size_t to = from + chunk;
            if (to > (size_t)totalPorts) to = (size_t)totalPorts;
            if (from >= to) break;
            threads.push_back(std::thread([&, t, from, to]() {
                scanTcpChunkFast(ip, ports, from, to, localFound[t]);
                }));
        }
        for (size_t i = 0; i < threads.size(); ++i) threads[i].join();
        for (int t = 0; t < threadCount; ++t)
            for (size_t j = 0; j < localFound[t].size(); ++j)
                allFound.push_back(localFound[t][j]);

        std::sort(allFound.begin(), allFound.end());
        for (size_t i = 0; i < allFound.size(); ++i) {
            int p = allFound[i];
            std::string svc = serviceName(p);
            std::cout << "[+] TCP " << p << " OPEN";
            if (!svc.empty()) std::cout << "  (" << svc << ")";
            std::cout << "\n";
        }
    }
    else {
        // UDP
        std::atomic<size_t> idx(0);
        std::vector<std::thread> threads;

        for (int t = 0; t < threadCount; ++t) {
            threads.push_back(std::thread([&]() {
                while (true) {
                    size_t i = idx.fetch_add(1);
                    if (i >= ports.size()) break;
                    int port = ports[i];
                    int st = checkUdpPortFast(ip, port, 300);
                    if (st != 1) {
                        std::lock_guard<std::mutex> lk(g_printMutex);
                        std::string svc = udpServiceName(port);
                        if (st == 2) std::cout << "[+] UDP " << port << " OPEN";
                        else         std::cout << "[?] UDP " << port << " open|filtered";
                        if (!svc.empty()) std::cout << "  (" << svc << ")";
                        std::cout << "\n";
                    }
                }
                }));
        }
        for (size_t i = 0; i < threads.size(); ++i) threads[i].join();
    }

    DWORD ms = GetTickCount() - t0;
    if (ms == 0) ms = 1;

    std::cout << "\nDone. Open ports: " << allFound.size()
        << " | Time: " << ms << " ms"
        << " | Speed: " << (totalPorts * 1000.0 / ms)
        << " ports/sec\n";

    WSACleanup();
    return 0;
}
