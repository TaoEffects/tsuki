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
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <vector>

namespace ftp {
namespace {

constexpr int kMaxClients = 8;

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

std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// Strip leading Unix flags like "-la", "-a", "-l" from LIST/NLST arguments
std::string stripListFlags(const std::string &arg) {
    std::string s = trim(arg);
    while (!s.empty() && s[0] == '-') {
        size_t sp = s.find(' ');
        if (sp == std::string::npos) return "";
        s = trim(s.substr(sp + 1));
    }
    return s;
}

std::string normalizeRel(const std::string &cwd, const std::string &arg) {
    if (arg.empty()) return cwd.empty() ? "/" : cwd;
    std::string combined = (arg[0] == '/') ? arg : (cwd == "/" ? ("/" + arg) : (cwd + "/" + arg));
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < combined.size()) {
        while (i < combined.size() && combined[i] == '/') ++i;
        size_t j = i;
        while (j < combined.size() && combined[j] != '/') ++j;
        if (j > i) {
            std::string tok = combined.substr(i, j - i);
            if (tok == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (tok != ".") {
                parts.push_back(tok);
            }
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

bool sendAll(int fd, const void *data, size_t len) {
    const char *p = static_cast<const char *>(data);
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void sendLine(int fd, const std::string &line) {
    std::string s = line + "\r\n";
    sendAll(fd, s.data(), s.size());
}

std::string formatMdtm(time_t t) {
    struct tm gt{};
    gmtime_r(&t, &gt);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                  gt.tm_year + 1900, gt.tm_mon + 1, gt.tm_mday,
                  gt.tm_hour, gt.tm_min, gt.tm_sec);
    return buf;
}

std::string formatListLine(const std::string &name, const struct stat &st) {
    static const char *kMonths[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    bool isDir = S_ISDIR(st.st_mode);
    uint64_t sz = isDir ? 0 : static_cast<uint64_t>(st.st_size);
    struct tm lt{};
    time_t mt = st.st_mtime;
    localtime_r(&mt, &lt);
    int mon = std::clamp(lt.tm_mon, 0, 11);
    char entry[640];
    std::snprintf(entry, sizeof(entry), "%crwxrwxrwx 1 switch switch %12llu %s %02d %02d:%02d %s\r\n",
                  isDir ? 'd' : '-',
                  static_cast<unsigned long long>(sz),
                  kMonths[mon], lt.tm_mday, lt.tm_hour, lt.tm_min,
                  name.c_str());
    return entry;
}

std::string formatMlsEntry(const std::string &name, const struct stat &st, bool isDirOverride = false) {
    bool isDir = isDirOverride || S_ISDIR(st.st_mode);
    uint64_t sz = isDir ? 0 : static_cast<uint64_t>(st.st_size);
    std::string mod = formatMdtm(st.st_mtime);
    char entry[640];
    if (isDir) {
        std::snprintf(entry, sizeof(entry), "type=dir;modify=%s;perm=elcmp; %s\r\n",
                      mod.c_str(), name.c_str());
    } else {
        std::snprintf(entry, sizeof(entry), "type=file;size=%llu;modify=%s;perm=adfrw; %s\r\n",
                      static_cast<unsigned long long>(sz), mod.c_str(), name.c_str());
    }
    return entry;
}

struct DataChannel {
    int pasvFd = -1;
    bool hasPort = false;
    struct sockaddr_in portAddr{};

    void reset() {
        if (pasvFd >= 0) {
            close(pasvFd);
            pasvFd = -1;
        }
        hasPort = false;
    }

    int openPassive(int cfd, uint32_t &outIp, uint16_t &outPort) {
        reset();
        pasvFd = socket(AF_INET, SOCK_STREAM, 0);
        if (pasvFd < 0) return -1;
        int one = 1;
        setsockopt(pasvFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in daddr{};
        daddr.sin_family = AF_INET;
        daddr.sin_addr.s_addr = INADDR_ANY;
        daddr.sin_port = 0;
        if (bind(pasvFd, reinterpret_cast<struct sockaddr *>(&daddr), sizeof(daddr)) < 0 ||
            listen(pasvFd, 2) < 0) {
            reset();
            return -1;
        }
        struct sockaddr_in local{}, bound{};
        socklen_t ll = sizeof(local), bl = sizeof(bound);
        getsockname(cfd, reinterpret_cast<struct sockaddr *>(&local), &ll);
        getsockname(pasvFd, reinterpret_cast<struct sockaddr *>(&bound), &bl);
        outIp = ntohl(local.sin_addr.s_addr);
        outPort = ntohs(bound.sin_port);
        return 0;
    }

    int connectData() {
        int dfd = -1;
        if (pasvFd >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(pasvFd, &rfds);
            struct timeval tv{10, 0};
            if (select(pasvFd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
                dfd = accept(pasvFd, nullptr, nullptr);
            }
            reset();
        } else if (hasPort) {
            dfd = socket(AF_INET, SOCK_STREAM, 0);
            if (dfd >= 0) {
                if (connect(dfd, reinterpret_cast<struct sockaddr *>(&portAddr), sizeof(portAddr)) < 0) {
                    close(dfd);
                    dfd = -1;
                }
            }
            hasPort = false;
        }
        return dfd;
    }
};

void handleClient(int cfd) {
    g_clients.fetch_add(1, std::memory_order_relaxed);
    std::string cwd = "/";
    std::string renameFrom;
    uint64_t restOffset = 0;
    DataChannel data;

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
            } else if (cmd == "PASS" || cmd == "ACCT") {
                sendLine(cfd, "230 Logged in");
                addLog("Client connecté (" + std::to_string(g_clients.load()) + ")");
            } else if (cmd == "SYST") {
                sendLine(cfd, "215 UNIX Type: L8");
            } else if (cmd == "FEAT") {
                sendLine(cfd,
                         "211-Features:\r\n"
                         " UTF8\r\n"
                         " SIZE\r\n"
                         " MDTM\r\n"
                         " MFMT\r\n"
                         " MLST type*;size*;modify*;perm*;\r\n"
                         " MLSD\r\n"
                         " REST STREAM\r\n"
                         " PASV\r\n"
                         " EPSV\r\n"
                         " TVFS\r\n"
                         "211 End");
            } else if (cmd == "OPTS" || cmd == "NOOP" || cmd == "TYPE" || cmd == "MODE" ||
                       cmd == "STRU" || cmd == "ALLO" || cmd == "CLNT") {
                sendLine(cfd, "200 OK");
            } else if (cmd == "ABOR") {
                data.reset();
                restOffset = 0;
                sendLine(cfd, "226 Abort successful");
            } else if (cmd == "REIN") {
                cwd = "/";
                renameFrom.clear();
                restOffset = 0;
                data.reset();
                sendLine(cfd, "220 Service ready for new user");
            } else if (cmd == "HELP") {
                sendLine(cfd, "214 Tsuki FTP: USER PASS SYST FEAT OPTS PWD CWD CDUP PASV EPSV PORT LIST NLST MLSD MLST SIZE MDTM MFMT REST RETR STOR APPE DELE MKD RMD RNFR RNTO STAT ABOR QUIT");
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
                uint32_t ip = 0;
                uint16_t dp = 0;
                if (data.openPassive(cfd, ip, dp) < 0) {
                    sendLine(cfd, "425 Cannot open passive socket");
                    continue;
                }
                char resp[128];
                std::snprintf(resp, sizeof(resp), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
                              (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                              (dp >> 8) & 0xFF, dp & 0xFF);
                sendLine(cfd, resp);
            } else if (cmd == "EPSV") {
                uint32_t ip = 0;
                uint16_t dp = 0;
                if (data.openPassive(cfd, ip, dp) < 0) {
                    sendLine(cfd, "425 Cannot open extended passive socket");
                    continue;
                }
                char resp[96];
                std::snprintf(resp, sizeof(resp), "229 Entering Extended Passive Mode (|||%u|)", dp);
                sendLine(cfd, resp);
            } else if (cmd == "PORT") {
                unsigned h1 = 0, h2 = 0, h3 = 0, h4 = 0, p1 = 0, p2 = 0;
                if (std::sscanf(arg.c_str(), "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) == 6) {
                    data.reset();
                    data.portAddr = {};
                    data.portAddr.sin_family = AF_INET;
                    uint32_t ip = ((h1 & 0xFF) << 24) | ((h2 & 0xFF) << 16) | ((h3 & 0xFF) << 8) | (h4 & 0xFF);
                    data.portAddr.sin_addr.s_addr = htonl(ip);
                    data.portAddr.sin_port = htons(static_cast<uint16_t>(((p1 & 0xFF) << 8) | (p2 & 0xFF)));
                    data.hasPort = true;
                    sendLine(cfd, "200 PORT command successful");
                } else {
                    sendLine(cfd, "501 Invalid PORT syntax");
                }
            } else if (cmd == "REST") {
                char *endp = nullptr;
                unsigned long long off = std::strtoull(arg.c_str(), &endp, 10);
                if (endp != arg.c_str()) {
                    restOffset = static_cast<uint64_t>(off);
                    sendLine(cfd, "350 Restarting at " + std::to_string(restOffset));
                } else {
                    sendLine(cfd, "501 Invalid REST offset");
                }
            } else if (cmd == "LIST" || cmd == "NLST" || cmd == "MLSD") {
                restOffset = 0;
                int dfd = data.connectData();
                if (dfd < 0) {
                    sendLine(cfd, "425 Data connection failed");
                    continue;
                }
                sendLine(cfd, "150 Opening data connection");
                std::string cleanArg = (cmd == "MLSD") ? trim(arg) : stripListFlags(arg);
                std::string target = cleanArg.empty() ? cwd : normalizeRel(cwd, cleanArg);
                std::string sdmcTarget = toSdmc(target);

                struct stat tst{};
                if (target != "/" && stat(sdmcTarget.c_str(), &tst) == 0 && !S_ISDIR(tst.st_mode)) {
                    // Single-file LIST / NLST / MLSD
                    size_t sl = target.rfind('/');
                    std::string base = (sl == std::string::npos) ? target : target.substr(sl + 1);
                    std::string out;
                    if (cmd == "NLST") out = base + "\r\n";
                    else if (cmd == "MLSD") out = formatMlsEntry(base, tst);
                    else out = formatListLine(base, tst);
                    sendAll(dfd, out.data(), out.size());
                } else {
                    DIR *d = opendir(sdmcTarget.c_str());
                    if (d) {
                        while (struct dirent *de = readdir(d)) {
                            if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
                            std::string full = toSdmc(normalizeRel(target, de->d_name));
                            struct stat st{};
                            if (stat(full.c_str(), &st) != 0) {
                                st = {};
                                if (de->d_type == DT_DIR) st.st_mode = S_IFDIR;
                            }
                            std::string out;
                            if (cmd == "NLST") out = std::string(de->d_name) + "\r\n";
                            else if (cmd == "MLSD") out = formatMlsEntry(de->d_name, st);
                            else out = formatListLine(de->d_name, st);
                            if (!sendAll(dfd, out.data(), out.size())) break;
                        }
                        closedir(d);
                    }
                }
                close(dfd);
                sendLine(cfd, "226 Transfer complete");
            } else if (cmd == "MLST") {
                std::string target = trim(arg).empty() ? cwd : normalizeRel(cwd, trim(arg));
                struct stat st{};
                if (target == "/") {
                    st.st_mode = S_IFDIR;
                    st.st_mtime = time(nullptr);
                    sendLine(cfd, "250-Listing /\r\n " + formatMlsEntry("/", st, true) + "250 End");
                } else if (stat(toSdmc(target).c_str(), &st) == 0) {
                    sendLine(cfd, "250-Listing " + target + "\r\n " + formatMlsEntry(target, st) + "250 End");
                } else {
                    sendLine(cfd, "550 Not found");
                }
            } else if (cmd == "STAT") {
                std::string cleanArg = stripListFlags(arg);
                if (cleanArg.empty()) {
                    sendLine(cfd, "211 Tsuki FTP Server ready (" + std::to_string(g_clients.load()) + " clients)");
                } else {
                    std::string target = normalizeRel(cwd, cleanArg);
                    struct stat st{};
                    if (stat(toSdmc(target).c_str(), &st) == 0) {
                        sendLine(cfd, "213-Status of " + target + "\r\n " + formatListLine(target, st) + "213 End");
                    } else {
                        sendLine(cfd, "550 Not found");
                    }
                }
            } else if (cmd == "SIZE") {
                std::string full = toSdmc(normalizeRel(cwd, arg));
                struct stat st{};
                if (stat(full.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
                    sendLine(cfd, "213 " + std::to_string(static_cast<uint64_t>(st.st_size)));
                } else {
                    sendLine(cfd, "550 Not found");
                }
            } else if (cmd == "MDTM") {
                std::string full = toSdmc(normalizeRel(cwd, arg));
                struct stat st{};
                if (stat(full.c_str(), &st) == 0) {
                    sendLine(cfd, "213 " + formatMdtm(st.st_mtime));
                } else {
                    sendLine(cfd, "550 Not found");
                }
            } else if (cmd == "MFMT") {
                size_t sp2 = arg.find(' ');
                if (sp2 != std::string::npos && sp2 >= 14) {
                    std::string ts = arg.substr(0, 14);
                    std::string rel = normalizeRel(cwd, trim(arg.substr(sp2 + 1)));
                    struct tm tmVal{};
                    if (std::sscanf(ts.c_str(), "%4d%2d%2d%2d%2d%2d",
                                    &tmVal.tm_year, &tmVal.tm_mon, &tmVal.tm_mday,
                                    &tmVal.tm_hour, &tmVal.tm_min, &tmVal.tm_sec) == 6) {
                        tmVal.tm_year -= 1900;
                        tmVal.tm_mon -= 1;
                        time_t mt = mktime(&tmVal);
                        struct timeval times[2]{{mt, 0}, {mt, 0}};
                        if (utimes(toSdmc(rel).c_str(), times) == 0) {
                            sendLine(cfd, "213 Modify=" + ts + "; " + rel);
                            continue;
                        }
                    }
                }
                sendLine(cfd, "550 MFMT failed");
            } else if (cmd == "RETR") {
                std::string rel = normalizeRel(cwd, arg);
                FILE *fp = std::fopen(toSdmc(rel).c_str(), "rb");
                if (fp && restOffset > 0) {
                    if (fseeko(fp, static_cast<off_t>(restOffset), SEEK_SET) != 0) {
                        std::fclose(fp);
                        fp = nullptr;
                    }
                }
                restOffset = 0;
                int dfd = data.connectData();
                if (!fp || dfd < 0) {
                    if (fp) std::fclose(fp);
                    if (dfd >= 0) close(dfd);
                    sendLine(cfd, "550 Cannot open file");
                    continue;
                }
                sendLine(cfd, "150 Sending file");
                std::vector<char> io(65536);
                size_t r;
                bool ok = true;
                while ((r = std::fread(io.data(), 1, io.size(), fp)) > 0) {
                    if (!sendAll(dfd, io.data(), r)) {
                        ok = false;
                        break;
                    }
                    g_bytes.fetch_add(r, std::memory_order_relaxed);
                }
                std::fclose(fp);
                close(dfd);
                sendLine(cfd, ok ? "226 Transfer complete" : "426 Connection closed; transfer aborted");
            } else if (cmd == "STOR" || cmd == "APPE") {
                std::string rel = normalizeRel(cwd, arg);
                std::string full = toSdmc(rel);
                FILE *fp = nullptr;
                if (cmd == "APPE") {
                    fp = std::fopen(full.c_str(), "ab");
                } else if (restOffset > 0) {
                    fp = std::fopen(full.c_str(), "r+b");
                    if (!fp) fp = std::fopen(full.c_str(), "wb");
                    if (fp && fseeko(fp, static_cast<off_t>(restOffset), SEEK_SET) != 0) {
                        std::fclose(fp);
                        fp = nullptr;
                    }
                } else {
                    fp = std::fopen(full.c_str(), "wb");
                }
                restOffset = 0;
                int dfd = data.connectData();
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
                bool writeOk = true;
                while ((r = recv(dfd, io.data(), io.size(), 0)) > 0) {
                    if (std::fwrite(io.data(), 1, static_cast<size_t>(r), fp) != static_cast<size_t>(r)) {
                        writeOk = false;
                        break;
                    }
                    total += static_cast<uint64_t>(r);
                    g_bytes.fetch_add(static_cast<uint64_t>(r), std::memory_order_relaxed);
                }
                std::fclose(fp);
                close(dfd);
                sendLine(cfd, writeOk ? "226 Transfer complete" : "451 Local write error");
                addLog(cmd + " " + rel + " (" + std::to_string(total / 1024) + " Ko)");
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
                std::string rel = normalizeRel(cwd, arg);
                struct stat st{};
                if (stat(toSdmc(rel).c_str(), &st) == 0) {
                    renameFrom = rel;
                    sendLine(cfd, "350 Ready for RNTO");
                } else {
                    renameFrom.clear();
                    sendLine(cfd, "550 File not found");
                }
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

    data.reset();
    close(cfd);
    g_clients.fetch_sub(1, std::memory_order_relaxed);
}

void *clientThreadEntry(void *arg) {
    int cfd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    handleClient(cfd);
    return nullptr;
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
                if (g_clients.load(std::memory_order_relaxed) >= kMaxClients) {
                    sendLine(cfd, "421 Too many connections");
                    close(cfd);
                    continue;
                }
                pthread_t ct{};
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                pthread_attr_setstacksize(&attr, 128 * 1024);
                if (pthread_create(&ct, &attr, clientThreadEntry,
                                   reinterpret_cast<void *>(static_cast<intptr_t>(cfd))) != 0) {
                    close(cfd);
                }
                pthread_attr_destroy(&attr);
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
        listen(g_listenFd, kMaxClients) < 0) {
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
