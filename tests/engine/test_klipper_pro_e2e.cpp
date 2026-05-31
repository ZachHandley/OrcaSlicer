// Phase 6.5 — end-to-end test for the Klipper-Pro flagship plugin.
//
// Spawns moonraker_mock.py (Python 3 stdlib HTTP server, lives next to the
// staged plugin), loads engine/plugins/klipper-pro's compiled .so via
// Session::load_plugin, drives its registered DeviceAgent through
// connect → start_print → cancel_print → disconnect, then asserts the mock
// recorded the expected URL sequence (with the right API key + filename).
//
// The plugin .so + manifest are staged by tests/engine/fixtures/klipper_pro/
// CMakeLists.txt; the test reads ${TEST_FIXTURES_DIR}/klipper_pro/. Subprocess
// orchestration is posix fork/exec — the test SKIPs on non-Linux platforms
// (klipper-pro itself is Linux-first for CI; Windows/macOS coverage lands
// later if/when CI runners exist).

#include <catch2/catch_all.hpp>

#include "orca/Session.hpp"
#include "orca/PrinterAgent.hpp"
#include "orca/Result.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef TEST_FIXTURES_DIR
constexpr const char* kFixturesDir = TEST_FIXTURES_DIR;
#else
constexpr const char* kFixturesDir = "";
#endif

#ifdef TEST_TMP_DIR
constexpr const char* kTmpDir = TEST_TMP_DIR;
#else
constexpr const char* kTmpDir = "";
#endif

namespace fs = std::filesystem;

#if defined(__linux__)

namespace {

// Owns a Python mock subprocess + the captured TCP port. Construction spawns
// the process, reads its first stdout line (`PORT=<n>\n`), and parks the
// child pid for cleanup. Destruction sends SIGTERM and reaps.
class MockServer {
public:
    MockServer(const std::string& python, const std::string& script) {
        // Pipe so we can read the child's PORT= line.
        int pipefd[2];
        REQUIRE(::pipe(pipefd) == 0);

        pid_ = ::fork();
        REQUIRE(pid_ >= 0);

        if (pid_ == 0) {
            // Child: redirect stdout to the write end, exec python3.
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::close(pipefd[0]);
            ::close(pipefd[1]);

            // Keep stderr (the mock logs there) for debug; tests can run
            // with -s to see it.
            const char* argv[] = {
                python.c_str(), script.c_str(), nullptr,
            };
            ::execvp(python.c_str(), const_cast<char* const*>(argv));
            // execvp only returns on failure.
            std::fprintf(stderr, "execvp(%s) failed: %s\n",
                         python.c_str(), std::strerror(errno));
            std::_Exit(127);
        }

        ::close(pipefd[1]);
        read_fd_ = pipefd[0];

        // Read up to 64 bytes — the PORT= line is shorter than that.
        char buf[64] = {};
        ssize_t n = ::read(read_fd_, buf, sizeof(buf) - 1);
        REQUIRE(n > 0);
        std::string first_line(buf, static_cast<std::size_t>(n));
        auto nl = first_line.find('\n');
        if (nl != std::string::npos) first_line.resize(nl);

        // Expect "PORT=<n>".
        REQUIRE(first_line.rfind("PORT=", 0) == 0);
        port_ = std::atoi(first_line.c_str() + 5);
        REQUIRE(port_ > 0);
    }

    ~MockServer() {
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
        if (read_fd_ >= 0) ::close(read_fd_);
    }

    MockServer(const MockServer&) = delete;
    MockServer& operator=(const MockServer&) = delete;

    int port() const { return port_; }

    // Synchronous GET — returns body or empty string on failure. Used by the
    // teardown to read /__test/requests.
    std::string get(const std::string& path) const {
        return raw_http("GET", path, "", "");
    }

private:
    std::string raw_http(const std::string& method,
                         const std::string& path,
                         const std::string& content_type,
                         const std::string& body) const {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return {};

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
        addr.sin_port        = htons(static_cast<std::uint16_t>(port_));
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return {};
        }

        std::string req = method + " " + path + " HTTP/1.0\r\n"
                          "Host: 127.0.0.1\r\n";
        if (!content_type.empty())
            req += "Content-Type: " + content_type + "\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n"
               "Connection: close\r\n\r\n" + body;

        if (::send(fd, req.data(), req.size(), 0) < 0) {
            ::close(fd);
            return {};
        }

        std::string out;
        char buf[4096];
        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
        }
        ::close(fd);

        auto hdr_end = out.find("\r\n\r\n");
        return hdr_end != std::string::npos ? out.substr(hdr_end + 4) : out;
    }

    int   read_fd_{-1};
    pid_t pid_{0};
    int   port_{0};
};

// Locate the Python interpreter. ctest sets PYTHON_EXECUTABLE if it was
// detected at configure time; fall back to "python3".
std::string find_python() {
    if (const char* p = std::getenv("PYTHON_EXECUTABLE"); p && *p)
        return p;
    return "python3";
}

} // namespace

TEST_CASE("Phase 6.5 — Klipper-Pro round-trips through Moonraker mock",
          "[plugin][klipper-pro][e2e]") {
    if (std::string(kFixturesDir).empty()) {
        SKIP("TEST_FIXTURES_DIR not defined at compile time");
    }

    const fs::path staged = fs::path(kFixturesDir) / "klipper_pro";
    if (!fs::exists(staged / "manifest.json")) {
        SKIP("klipper-pro fixture not staged at " + staged.string());
    }
    const fs::path mock_script = staged / "moonraker_mock.py";
    if (!fs::exists(mock_script)) {
        SKIP("moonraker_mock.py missing at " + mock_script.string());
    }

    // 1. Spawn the mock and capture its port.
    MockServer mock(find_python(), mock_script.string());
    REQUIRE(mock.port() > 0);

    // 2. Load klipper-pro via the engine's plugin host.
    auto session = orca::Session::create();
    REQUIRE(session);

    auto load_r = session->load_plugin(staged);
    if (!load_r.ok()) {
        FAIL("load_plugin failed: " + load_r.error().message);
    }
    REQUIRE(session->is_plugin_loaded("com.orcaslicer.klipper-pro"));

    // 3. The plugin's on_register registers the KlipperAgent under id
    // "klipper-pro" (KlipperAgent::AGENT_ID).
    REQUIRE(session->has_printer_agent("klipper-pro"));

    auto agent_or = session->create_printer_agent("klipper-pro");
    REQUIRE(agent_or.ok());
    auto agent = std::move(agent_or).value();
    REQUIRE(agent != nullptr);

    // 4. Wire up callbacks so we can observe the agent's host emits.
    std::mutex                            cb_mu;
    std::vector<orca::PrinterAgentStatus> statuses;
    std::vector<std::string>              messages;

    agent->on_status([&](const orca::PrinterAgentStatus& s) {
        std::lock_guard g(cb_mu);
        statuses.push_back(s);
    });
    agent->on_message([&](std::string_view payload) {
        std::lock_guard g(cb_mu);
        messages.emplace_back(payload);
    });
    agent->on_is_cancelled([] { return false; });

    // 5. Connect.
    orca::PrinterConnection conn;
    conn.device_id = "mock-dev";
    conn.host      = "127.0.0.1";
    conn.port      = mock.port();
    conn.username  = "";
    conn.password  = "fake-api-key";   // doubles as Moonraker X-Api-Key
    conn.use_tls   = false;

    auto cr = agent->connect(conn);
    REQUIRE(cr.ok());
    CHECK(agent->state() == orca::PrinterAgentState::Connected);

    // The connect path emits one status update — make sure it arrived.
    {
        std::lock_guard g(cb_mu);
        REQUIRE_FALSE(statuses.empty());
        CHECK(statuses.front().state == orca::PrinterAgentState::Connected);
    }

    // 6. Stage a .gcode file in TEST_TMP_DIR, then start the print.
    fs::path tmp_dir = fs::path(kTmpDir);
    fs::create_directories(tmp_dir);
    const fs::path gcode_path = tmp_dir / "klipper_pro_e2e.gcode";
    {
        std::ofstream ofs(gcode_path);
        ofs << "; orca klipper-pro e2e test\n";
        ofs << "G28\n";
        ofs << "G1 X10 Y10 Z0.2 F600\n";
    }

    orca::PrintJob job;
    job.gcode_path        = gcode_path.string();
    job.job_name          = "klipper-pro-e2e-job";
    job.start_immediately = true;
    REQUIRE(agent->start_print(job).ok());

    // klipper-pro fires a {event:job_submitted,...} message after upload+start.
    bool saw_job_submitted = false;
    for (int i = 0; i < 50 && !saw_job_submitted; ++i) {
        {
            std::lock_guard g(cb_mu);
            for (const auto& m : messages) {
                if (m.find("job_submitted") != std::string::npos) {
                    saw_job_submitted = true;
                    break;
                }
            }
        }
        if (!saw_job_submitted)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(saw_job_submitted);

    // 7. Cancel + disconnect.
    REQUIRE(agent->cancel_print().ok());
    REQUIRE(agent->disconnect().ok());
    CHECK(agent->state() == orca::PrinterAgentState::Disconnected);

    // 8. Read the mock's request log and assert the URL sequence.
    std::string log = mock.get("/__test/requests");
    REQUIRE_FALSE(log.empty());

    auto contains = [&](const std::string& needle) {
        return log.find(needle) != std::string::npos;
    };

    CHECK(contains("\"path\": \"/printer/info\""));
    CHECK(contains("\"path\": \"/server/files/upload?root=gcodes\""));
    CHECK(contains("\"path\": \"/printer/print/start?filename=klipper-pro-e2e-job.gcode\""));
    CHECK(contains("\"path\": \"/printer/print/cancel\""));
    CHECK(contains("\"X-Api-Key\": \"fake-api-key\""));
    CHECK(contains("\"filename\": \"klipper-pro-e2e-job.gcode\""));

    // 9. Tear down before unload — agent dtor first, then plugin.
    agent.reset();
    auto unl = session->unload_plugin("com.orcaslicer.klipper-pro");
    CHECK(unl.ok());
}

#else // !__linux__

TEST_CASE("Phase 6.5 — Klipper-Pro round-trips through Moonraker mock",
          "[plugin][klipper-pro][e2e]") {
    SKIP("Klipper-Pro E2E test is Linux-only for now (posix subprocess).");
}

#endif // __linux__
