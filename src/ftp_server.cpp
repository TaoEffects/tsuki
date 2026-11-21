#include "ftp_server.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <vector>

namespace ftp {
namespace {

std::atomic<bool> g_running{false};
std::atomic<int> g_clients{0};
std::atomic<uint64_t> g_bytes{0};
std::atomic<bool> g_libDirty{false};
uint16_t g_port = 1337;
int g_listenFd = -1;
pthread_t g_thread{};
bool g_threadStarted = false;

std::mutex g_logMtx;
std::deque<std::string> g_logs;
std::string g_lastFile;

void addLog(const std::string &msg) {
    time_t t = time(nullptr);
    struct tm lt{};
    localtime_r(&t, &lt);
    char ts[16];
    std::snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ", lt.tm_hour, lt.tm_min, lt.tm_sec);
    std::lock_guard<std::mutex> lk(g_logMtx);
    g_logs.push_back(std::string(ts) + msg);
    while (g_logs.size() > 14) g_logs.pop_front();
}

void markDirty(const std::string &path) {
    {
        std::lock_guard<std::mutex> lk(g_logMtx);
        g_lastFile = path;
    }
    g_libDirty.store(true, std::memory_order_release);
}

std::string normalizeRel(const std::string &cwd, const std::string &arg) {
    std::string combined = (!arg.empty() && arg[0] == '/') ? arg : (cwd == "/" ? ("/" + arg) : (cwd + "/" + arg));
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < combined.size()) {
        while (i < combined.size() && combined[i] == '/') ++i;
        size_t j = i;
        while (j < combined.size() && combined[j] != '/') ++j;
        if (j > i) {
            std::string tok = combined.substr(i, j - i);
            if (tok == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (tok != ".") parts.push_back(tok);
        }
        i = j;
    }
    if (parts.empty()) return "/";
    std::string out;
    for (const auto &p : parts) out += "/" + p;
    return out;
}

std::string toSdmc(const std::string &rel) {
    if (rel.empty() || rel == "/") return "sdmc:/";
    if (rel[0] == '/') return "sdmc:" + rel;
    return "sdmc:/" + rel;
}

void sendLine(int fd, const std::string &line) {
    std::string s = line + "\r\n";
    send(fd, s.data(), s.size(), 0);
}

int acceptData(int pasvFd) {
    if (pasvFd < 0) return -1;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(pasvFd, &rfds);
    struct timeval tv{8, 0};
    if (select(pasvFd + 1, &rfds, nullptr, nullptr, &tv) <= 0) return -1;
    return accept(pasvFd, nullptr, nullptr);
}

void handleClient(int cfd) {
    g_clients.fetch_add(1, std::memory_order_relaxed);
    std::string cwd = "/";
    std::string renameFrom;
    int pasvFd = -1;

    sendLine(cfd, "220 Tsuki Switch 2 FTP Ready");

    std::string buf;
    char tmp[1024];

    while (g_running.load(std::memory_order_acquire)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(cfd, &rfds);
        struct timeval tv{0, 250000};
        int sr = select(cfd + 1, &rfds, nullptr, nullptr, &tv);
        if (sr < 0) break;
        if (sr == 0) continue;

        ssize_t n = recv(cfd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, static_cast<size_t>(n));

        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string cmd = line;
            std::string arg;
            size_t sp = line.find(' ');
            if (sp != std::string::npos) {
                cmd = line.substr(0, sp);
                arg = line.substr(sp + 1);
            }
            for (char &c : cmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            if (cmd == "USER") {
                sendLine(cfd, "331 Anonymous login ok");
            } else if (cmd == "PASS") {
                sendLine(cfd, "230 Logged in");
                addLog("Client connecté");
            } else if (cmd == "SYST") {
                sendLine(cfd, "215 UNIX Type: L8");
            } else if (cmd == "FEAT") {
                sendLine(cfd, "211-Features:\r\n UTF8\r\n SIZE\r\n PASV\r\n211 End");
            } else if (cmd == "OPTS" || cmd == "NOOP" || cmd == "TYPE") {
                sendLine(cfd, "200 OK");
            } else if (cmd == "PWD" || cmd == "XPWD") {
                sendLine(cfd, "257 \"" + cwd + "\"");
            } else if (cmd == "CWD" || cmd == "XCWD") {
                std::string next = normalizeRel(cwd, arg);
                struct stat st{};
                if (next == "/" || (stat(toSdmc(next).c_str(), &st) == 0 && S_ISDIR(st.st_mode))) {
                    cwd = next;
                    sendLine(cfd, "250 Directory changed");
                } else {
                    sendLine(cfd, "550 Directory not found");
                }
            } else if (cmd == "CDUP" || cmd == "XCUP") {
                cwd = normalizeRel(cwd, "..");
                sendLine(cfd, "250 OK");
            } else if (cmd == "PASV") {
                if (pasvFd >= 0) { close(pasvFd); pasvFd = -1; }
                pasvFd = socket(AF_INET, SOCK_STREAM, 0);
                if (pasvFd < 0) { sendLine(cfd, "425 Cannot open passive socket"); continue; }
                int one = 1;
                setsockopt(pasvFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
                struct sockaddr_in daddr{};
                daddr.sin_family = AF_INET;
                daddr.sin_addr.s_addr = INADDR_ANY;
                daddr.sin_port = 0;
                if (bind(pasvFd, reinterpret_cast<struct sockaddr *>(&daddr), sizeof(daddr)) < 0 ||
                    listen(pasvFd, 1) < 0) {
                    close(pasvFd); pasvFd = -1;
                    sendLine(cfd, "425 Passive bind failed");
                    continue;
                }
                struct sockaddr_in local{}, bound{};
                socklen_t ll = sizeof(local), bl = sizeof(bound);
                getsockname(cfd, reinterpret_cast<struct sockaddr *>(&local), &ll);
                getsockname(pasvFd, reinterpret_cast<struct sockaddr *>(&bound), &bl);
                uint32_t ip = ntohl(local.sin_addr.s_addr);
                uint16_t dp = ntohs(bound.sin_port);
                char resp[128];
                std::snprintf(resp, sizeof(resp), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
                              (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                              (dp >> 8) & 0xFF, dp & 0xFF);
                sendLine(cfd, resp);
            } else if (cmd == "LIST" || cmd == "NLST") {
                int dfd = acceptData(pasvFd);
                if (pasvFd >= 0) { close(pasvFd); pasvFd = -1; }
                if (dfd < 0) { sendLine(cfd, "425 Data connection failed"); continue; }
                sendLine(cfd, "150 Opening data connection");
                std::string target = (!arg.empty() && arg[0] != '-') ? normalizeRel(cwd, arg) : cwd;
                DIR *d = opendir(toSdmc(target).c_str());
                if (d) {
                    while (struct dirent *de = readdir(d)) {
                        if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
                        std::string full = toSdmc(normalizeRel(target, de->d_name));
                        struct stat st{};
                        bool isDir = false;
                        uint64_t sz = 0;
                        if (stat(full.c_str(), &st) == 0) {
                            isDir = S_ISDIR(st.st_mode);
                            sz = isDir ? 0 : static_cast<uint64_t>(st.st_size);
                        }
                        char entry[512];
                        if (cmd == "NLST") {
                            std::snprintf(entry, sizeof(entry), "%s\r\n", de->d_name);
                        } else {
                            std::snprintf(entry, sizeof(entry), "%crwxrwxrwx 1 switch switch %12llu Jan 01 00:00 %s\r\n",
                                          isDir ? 'd' : '-', static_cast<unsigned long long>(sz), de->d_name);
                        }
                        send(dfd, entry, std::strlen(entry), 0);
                    }
                    closedir(d);
                }
                close(dfd);
                sendLine(cfd, "226 Transfer complete");
            } else if (cmd == "SIZE") {
                std::string full = toSdmc(normalizeRel(cwd, arg));
                struct stat st{};
                if (stat(full.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
                    sendLine(cfd, "213 " + std::to_string(static_cast<uint64_t>(st.st_size)));
                } else {
                    sendLine(cfd, "550 Not found");
                }
            } else if (cmd == "RETR") {
                std::string rel = normalizeRel(cwd, arg);
                FILE *fp = std::fopen(toSdmc(rel).c_str(), "rb");
                int dfd = acceptData(pasvFd);
                if (pasvFd >= 0) { close(pasvFd); pasvFd = -1; }
                if (!fp || dfd < 0) {
                    if (fp) std::fclose(fp);
                    if (dfd >= 0) close(dfd);
                    sendLine(cfd, "550 Cannot open file");
                    continue;
                }
                sendLine(cfd, "150 Sending file");
                std::vector<char> io(65536);
                size_t r;
                while ((r = std::fread(io.data(), 1, io.size(), fp)) > 0) {
                    send(dfd, io.data(), r, 0);
                    g_bytes.fetch_add(r, std::memory_order_relaxed);
                }
                std::fclose(fp);
                close(dfd);
                sendLine(cfd, "226 Transfer complete");
            } else if (cmd == "STOR") {
                std::string rel = normalizeRel(cwd, arg);
                std::string full = toSdmc(rel);
                FILE *fp = std::fopen(full.c_str(), "wb");
                int dfd = acceptData(pasvFd);
                if (pasvFd >= 0) { close(pasvFd); pasvFd = -1; }
                if (!fp || dfd < 0) {
                    if (fp) std::fclose(fp);
                    if (dfd >= 0) close(dfd);
                    sendLine(cfd, "550 Cannot create file");
                    continue;
                }
                sendLine(cfd, "150 Receiving file");
                std::vector<char> io(65536);
                ssize_t r;
                uint64_t total = 0;
                while ((r = recv(dfd, io.data(), io.size(), 0)) > 0) {
                    std::fwrite(io.data(), 1, static_cast<size_t>(r), fp);
                    total += static_cast<uint64_t>(r);
                    g_bytes.fetch_add(static_cast<uint64_t>(r), std::memory_order_relaxed);
                }
                std::fclose(fp);
                close(dfd);
                sendLine(cfd, "226 Transfer complete");
                addLog("STOR " + rel + " (" + std::to_string(total / 1024) + " Ko)");
                if (rel.rfind("/switch", 0) == 0 && rel.find(".tmp") == std::string::npos) markDirty(rel);
            } else if (cmd == "DELE") {
                std::string rel = normalizeRel(cwd, arg);
                if (unlink(toSdmc(rel).c_str()) == 0) {
                    sendLine(cfd, "250 Deleted");
                    addLog("DELE " + rel);
                    if (rel.rfind("/switch", 0) == 0) markDirty(rel);
                } else {
                    sendLine(cfd, "550 Delete failed");
                }
            } else if (cmd == "MKD" || cmd == "XMKD") {
                std::string rel = normalizeRel(cwd, arg);
                if (mkdir(toSdmc(rel).c_str(), 0777) == 0 || errno == EEXIST) {
                    sendLine(cfd, "257 \"" + rel + "\" created");
                } else {
                    sendLine(cfd, "550 MKD failed");
                }
            } else if (cmd == "RMD" || cmd == "XRMD") {
                std::string rel = normalizeRel(cwd, arg);
                if (rmdir(toSdmc(rel).c_str()) == 0) sendLine(cfd, "250 Removed");
                else sendLine(cfd, "550 RMD failed");
            } else if (cmd == "RNFR") {
                renameFrom = normalizeRel(cwd, arg);
                sendLine(cfd, "350 Ready for RNTO");
            } else if (cmd == "RNTO") {
                std::string rel = normalizeRel(cwd, arg);
                if (!renameFrom.empty() && rename(toSdmc(renameFrom).c_str(), toSdmc(rel).c_str()) == 0) {
                    sendLine(cfd, "250 Renamed");
                    addLog("RNTO " + rel);
                    if (rel.rfind("/switch", 0) == 0) markDirty(rel);
                } else {
                    sendLine(cfd, "550 Rename failed");
                }
                renameFrom.clear();
            } else if (cmd == "QUIT") {
                sendLine(cfd, "221 Goodbye");
                break;
            } else {
                sendLine(cfd, "502 Command not implemented");
            }
        }
    }

    if (pasvFd >= 0) close(pasvFd);
    close(cfd);
    g_clients.fetch_sub(1, std::memory_order_relaxed);
}

void *serverLoop(void *) {
    addLog("Serveur FTP prêt sur le port " + std::to_string(g_port));
    while (g_running.load(std::memory_order_acquire)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_listenFd, &rfds);
        struct timeval tv{0, 250000};
        int r = select(g_listenFd + 1, &rfds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(g_listenFd, &rfds)) {
            int cfd = accept(g_listenFd, nullptr, nullptr);
            if (cfd >= 0) {
                handleClient(cfd);
            }
        }
    }
    return nullptr;
}

}  // namespace

bool Server::start(uint16_t port) {
    if (g_running.load(std::memory_order_acquire)) return true;
    g_port = port;
    g_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenFd < 0) return false;
    int one = 1;
    setsockopt(g_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(g_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0 ||
        listen(g_listenFd, 4) < 0) {
        close(g_listenFd);
        g_listenFd = -1;
        return false;
    }
    g_running.store(true, std::memory_order_release);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    if (pthread_create(&g_thread, &attr, serverLoop, nullptr) != 0) {
        pthread_attr_destroy(&attr);
        g_running.store(false, std::memory_order_release);
        close(g_listenFd);
        g_listenFd = -1;
        return false;
    }
    pthread_attr_destroy(&attr);
    g_threadStarted = true;
    return true;
}

void Server::stop() {
    if (!g_threadStarted) return;
    g_running.store(false, std::memory_order_release);
    if (g_listenFd >= 0) {
        shutdown(g_listenFd, SHUT_RDWR);
        close(g_listenFd);
        g_listenFd = -1;
    }
    pthread_join(g_thread, nullptr);
    g_threadStarted = false;
    addLog("Serveur FTP arrêté");
}

bool Server::isRunning() const { return g_running.load(std::memory_order_acquire); }
uint16_t Server::port() const { return g_port; }
int Server::activeClients() const { return g_clients.load(std::memory_order_relaxed); }
uint64_t Server::bytesTransferred() const { return g_bytes.load(std::memory_order_relaxed); }

bool Server::consumeLibraryDirty(std::string &lastFileOut) {
    if (!g_libDirty.exchange(false, std::memory_order_acq_rel)) return false;
    std::lock_guard<std::mutex> lk(g_logMtx);
    lastFileOut = g_lastFile;
    return true;
}

std::vector<std::string> Server::recentLogs() const {
    std::lock_guard<std::mutex> lk(g_logMtx);
    return std::vector<std::string>(g_logs.begin(), g_logs.end());
}

}  // namespace ftp
