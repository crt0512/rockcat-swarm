// checks that an old ~/.config/rossat gets dragged over to ~/.config/rockcat
// the move only happens on the first call per process, thats why ctest runs this once per case

#include "rockcat/rockcat.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

static bool is_dir(const std::string &p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool has_file(const std::string &p)
{
    return std::ifstream(p).good();
}

static int fail(const char *what)
{
    std::fprintf(stderr, "test_config: %s\n", what);
    return 1;
}

int main(int argc, char **argv)
{
    std::string mode = argc > 1 ? argv[1] : "";
    char tmpl[] = "/tmp/rockcat-test-XXXXXX";
    if (!::mkdtemp(tmpl))
        return fail("mkdtemp said no");
    std::string base = tmpl, old = base + "/rossat", now = base + "/rockcat";
    ::setenv("XDG_CONFIG_HOME", base.c_str(), 1);

    if (mode == "move" || mode == "both") {
        ::mkdir(old.c_str(), 0755);
        std::ofstream(old + "/2e2c.ini") << "[profile1]\nshift_light=on\n";
    }
    if (mode == "both") {
        ::mkdir(now.c_str(), 0755);
        std::ofstream(now + "/gui.ini") << "[gui]\ntray=true\n";
    }

    if (now != rockcat_config_dir())
        return fail("config dir is not $XDG_CONFIG_HOME/rockcat");

    if (mode == "move") {
        if (is_dir(old) || !has_file(now + "/2e2c.ini"))
            return fail("old dir was not moved");
    } else if (mode == "both") {
        // new dir already there, the old one has to be left alone and not merged or eaten
        if (!has_file(old + "/2e2c.ini") || !has_file(now + "/gui.ini") || has_file(now + "/2e2c.ini"))
            return fail("old dir got touched even though the new one exists");
    } else if (mode == "fresh") {
        if (is_dir(old) || is_dir(now))
            return fail("asking for the path created a directory");
    } else {
        return fail("usage: rockcat-test-config move|both|fresh");
    }

    std::string cleanup = "rm -rf '" + base + "'";
    return std::system(cleanup.c_str()) == 0 ? 0 : fail("cleanup failed");
}
