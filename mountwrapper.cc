/**
 * @file mountwrapper.cc
 * @author André Lucas (andre.lucas@storageos.com)
 * @brief Wrap a system binary (e.g. mount(8)) with some additional logging.
 * @version 0.1
 * @date 2021-05-18
 *
 * @copyright Copyright (c) 2021
 *
 * There are some extra considerations at work here as this program is intended
 * to find a subtle race. We don't touch the log file until after the fork() and
 * exec(), otherwise we risk serialising access to the log file and losing our
 * chance to expose the race we're looking for.
 *
 * It's configured using the environment, because we want to leave the command
 * line completely untouched. Use WRAPPER_OUTPUT to change the log file
 * location, and WRAPPER_BINARY to change the target binary being wrapped.
 *
 * We will leave our invocation environment untouched, but will execv(2) the
 * wrapped binary. This means that the program will start with argv[0] equal to
 * the wrapper's path. This is intentional; some programs react differently
 * based on their invocation path name. (If the OS changes argv[0] to match the
 * binary name, there's nothing we can do.)
 */

#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

using namespace std::string_literals;

static constexpr char kLogFileEnvVar[] = "WRAPPER_OUTPUT";
static constexpr char kDefaultOutputFile[] = "/tmp/mountwrapper.log";

static constexpr char kMountBinaryEnvVar[] = "WRAPPER_BINARY";
static constexpr char kMountBinaryLocation[] = "/usr/bin/mount.real";

std::string logfile{};
std::string progname{};

std::string EnvStringWithDefault(const std::string& env, const std::string& default_value) {
    char *present = getenv(env.c_str());
    if (present == nullptr || (std::string(present) == "")) {
        return default_value;
    }
    return std::string(present);
}

[[noreturn]] void error_sys(int err, const std::string& message) {
    std::cerr << progname << " (wrapper): " << message << ": " << strerror(err) << "\n";
    exit(EXIT_FAILURE);
}

// Generate a timestamp of now (via clock_gettime()).
std::string GetTimestamp() {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        error_sys(errno, "clock_gettime() failed");
    }
    char ftime[64];
    time_t tt = ts.tv_sec;
    struct tm bdtime;

    if (gmtime_r(&tt, &bdtime) == nullptr) {
        error_sys(errno, "gmtime_r() failed");
    }

    if (strftime(ftime, sizeof(ftime), "%Y-%m-%dT%H:%M:%S", &bdtime) == 0) {
        error_sys(errno, "strftime() failed");
    }
    std::ostringstream ss;
    // formatted_time dot microsec (==nsec / 1000).
    ss << ftime << "." << std::setfill('0') << std::setw(6) << ts.tv_nsec / 1000;

    return ss.str();
}

// Turn the given string vector into a comma-separated string.
std::string GetVecString(const std::vector<std::string>& vec) {
    std::ostringstream ss;
    bool first = true;
    for (size_t n=0; n<vec.size(); n++) {
        if (first == true)
            first = false;
        else
            ss << ", ";
        ss << "\"" << vec[n] << "\"";
    }
    return ss.str();
}

// Append an item to the given vector with a timestamp prepended.
void Log(std::vector<std::string>& out, const std::string& str) {
    out.emplace_back(GetTimestamp() + " " + str);
}

int main(int argc, char* argv[]) {
    std::vector<std::string> output;
    logfile = EnvStringWithDefault(kLogFileEnvVar, kDefaultOutputFile);
    std::string binary = EnvStringWithDefault(kMountBinaryEnvVar, kMountBinaryLocation);

    // Set the program name for log messages.
    progname = std::string(basename(argv[0]));


    std::vector<std::string> arg;
    for (int n=0; n<argc; n++) {
        arg.push_back(std::string(argv[n]));
    }

    // Prepare a string for the log file.
    auto argstr = GetVecString(arg);
    auto cmdstr = "execute  '" + binary + "' with argv: "s + argstr;
    Log(output, cmdstr);

    //
    // fork(), then exec() in the child.
    //

    auto cpid = fork();
    if (cpid == -1) {
        error_sys(errno, "fork() failed");
    }
    if (cpid == 0) {
        // In child. execv() the real mount binary, but do nothing with the
        // output.
        execv(binary.c_str(), argv);

        // If we get here, the exec failed.
        error_sys(errno, "execv() failed");

        // Exit status 128 isn't used by the mount command. We'll use it to
        // indicate an execv() failure to the parent.
        exit(128);

    } else {
        // In parent.
        int wstatus;

        int w = waitpid(cpid, &wstatus, 0);
        if (w == -1) {
            error_sys(errno,  "waitpid() failed");
        }
        auto prefix = "executed '"s + binary + "' with args: " + argstr + ": ";
        if (WIFEXITED(wstatus)) {
            int ec = WEXITSTATUS(wstatus);
            if (ec == 128) {
                Log(output, prefix + " failed to execv(2) (ec==128)");
            } else {
                Log(output, prefix + " exited with code " + std::to_string(ec));
            }
        } else if (WIFSIGNALED(wstatus)) {
            int sig = WTERMSIG(wstatus);
            Log(output, prefix + " exited with signal " + std::to_string(sig));
        } else {
            Log(output, prefix + " stopped with unknown status " + std::to_string(wstatus));
        }
    }

    // Dump all output to the log file.

    // Use stdio.h so it's clear that we're using specific open flags.
    int logfd = open(logfile.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (logfd == -1) {
        error_sys(errno, "Failed to open log file");
    }

    for (const auto& line: output) {
        if ((dprintf(logfd, line.c_str()) == -1) || (dprintf(logfd, "\n") == -1)) {
            error_sys(errno, "Failed to write to log file");
        }
    }
    (void)close(logfd);
    return EXIT_SUCCESS;
}
