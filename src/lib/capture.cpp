// recordings of what went over the wire, and playing them back as if a device was there
// the format is plain text on purpose so a recording can be read, diffed and fixed by hand, see docs/CAPTURES.md

#include "transport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

const char MAGIC[] = "# rockcat capture v1";

std::string hex(const uint8_t *buf, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < len; ++i) {
        if (i)
            s += ' ';
        s += digits[buf[i] >> 4];
        s += digits[buf[i] & 15];
    }
    return s;
}

bool unhex(std::istringstream &in, std::vector<uint8_t> &out)
{
    std::string tok;
    while (in >> tok) {
        if (tok[0] == '@') // timestamp, only there for humans
            break;
        if (tok.size() != 2 || !isxdigit(static_cast<unsigned char>(tok[0])) || !isxdigit(static_cast<unsigned char>(tok[1])))
            return false;
        out.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
    }
    return true;
}

struct record_transport final : transport {
    std::unique_ptr<transport> inner;
    std::ofstream out;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    long ms() const
    {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        uint8_t id = buf[0];
        int r = inner->get_feature(buf, len);
        out << "G " << hex(&id, 1) << ' ' << len << " : " << (r < 0 ? "!" : hex(buf, static_cast<size_t>(r))) << " @" << ms() << '\n' << std::flush;
        return r;
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        int r = inner->set_feature(buf, len);
        out << "S " << hex(buf, len) << (r < 0 ? " : !" : "") << " @" << ms() << '\n' << std::flush;
        return r;
    }
    int read(uint8_t *buf, size_t len, int timeout_ms) override
    {
        int r = inner->read(buf, len, timeout_ms);
        // timeouts are not written down, an idle mouse would fill the disk with nothing
        if (r > 0)
            out << "R " << hex(buf, static_cast<size_t>(r)) << " @" << ms() << '\n' << std::flush;
        else if (r < 0)
            out << "R ! @" << ms() << '\n' << std::flush;
        return r;
    }
    int write(const uint8_t *buf, size_t len) override
    {
        int r = inner->write(buf, len);
        out << "W " << hex(buf, len) << (r < 0 ? " : !" : "") << " @" << ms() << '\n' << std::flush;
        return r;
    }
    void sleep_ms(int n) override { inner->sleep_ms(n); }
};

struct entry {
    char op = 0;
    uint8_t id = 0;
    size_t want = 0;
    bool failed = false;
    std::vector<uint8_t> data;
    int line = 0;
};

struct replay_transport final : transport {
    std::string file;
    std::vector<entry> entries;
    size_t pos = 0;
    bool diverged = false;

    int complain(const char *did, const std::string &sent)
    {
        if (!diverged) {
            if (pos < entries.size())
                std::fprintf(stderr, "replay %s:%d: code did '%s %s' but the recording has '%c %s' there\n", file.c_str(), entries[pos].line,
                             did, sent.c_str(), entries[pos].op, hex(entries[pos].data.data(), entries[pos].data.size()).c_str());
            else
                std::fprintf(stderr, "replay %s: code did '%s %s' but the recording is over\n", file.c_str(), did, sent.c_str());
        }
        diverged = true;
        return ROCKCAT_ERR_PROTOCOL;
    }

    int get_feature(uint8_t *buf, size_t len) override
    {
        if (diverged || pos >= entries.size() || entries[pos].op != 'G' || entries[pos].id != buf[0] || entries[pos].want != len)
            return complain("G", hex(buf, 1) + " " + std::to_string(len));
        const entry &e = entries[pos++];
        if (e.failed)
            return ROCKCAT_ERR_IO;
        std::memcpy(buf, e.data.data(), std::min(len, e.data.size()));
        return static_cast<int>(e.data.size());
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        if (diverged || pos >= entries.size() || entries[pos].op != 'S' || entries[pos].data.size() != len ||
            std::memcmp(entries[pos].data.data(), buf, len) != 0)
            return complain("S", hex(buf, len));
        return entries[pos++].failed ? ROCKCAT_ERR_IO : ROCKCAT_OK;
    }
    int write(const uint8_t *buf, size_t len) override
    {
        if (diverged || pos >= entries.size() || entries[pos].op != 'W' || entries[pos].data.size() != len ||
            std::memcmp(entries[pos].data.data(), buf, len) != 0)
            return complain("W", hex(buf, len));
        return entries[pos++].failed ? ROCKCAT_ERR_IO : ROCKCAT_OK;
    }
    int read(uint8_t *buf, size_t len, int) override
    {
        // input reports come whenever the device feels like it, so they only get handed out when they are next in line
        if (diverged || pos >= entries.size() || entries[pos].op != 'R')
            return 0;
        const entry &e = entries[pos++];
        if (e.failed)
            return ROCKCAT_ERR_IO;
        size_t n = std::min(len, e.data.size());
        std::memcpy(buf, e.data.data(), n);
        return static_cast<int>(n);
    }
    void sleep_ms(int) override {}
    int verdict(bool finished) override
    {
        if (diverged)
            return ROCKCAT_ERR_PROTOCOL;
        if (finished && pos != entries.size()) {
            std::fprintf(stderr, "replay %s:%d: the recording goes on but the code stopped here\n", file.c_str(), entries[pos].line);
            return ROCKCAT_ERR_PROTOCOL;
        }
        return ROCKCAT_OK;
    }
};

} // namespace

std::unique_ptr<transport> transport_record(std::unique_ptr<transport> inner, const std::string &file, const rockcat_device_info &info)
{
    if (!inner)
        return nullptr;
    // appending lets a shell script build one recording out of several cli calls
    bool fresh = !std::ifstream(file).good() || std::ifstream(file).peek() == std::ifstream::traits_type::eof();
    auto *rec = new record_transport;
    rec->inner = std::move(inner);
    rec->out.open(file, std::ios::app);
    if (fresh) {
        char head[160];
        // the serial stays out of it, recordings end up in a public repo
        std::snprintf(head, sizeof head, "D %04x %04x %04x %d %s", info.vendor_id, info.product_id, info.release, info.interface_number, info.name);
        rec->out << MAGIC << '\n' << head << '\n';
    }
    rec->out << "# open\n" << std::flush;
    return std::unique_ptr<transport>(rec);
}

std::unique_ptr<transport> transport_replay(const std::string &file, rockcat_device_info *info)
{
    std::ifstream in(file);
    std::string line;
    if (!in || !std::getline(in, line) || line != MAGIC)
        return nullptr;
    auto rep = std::unique_ptr<replay_transport>(new replay_transport);
    rep->file = file;
    bool have_device = false;
    for (int n = 2; std::getline(in, line); ++n) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ls(line);
        std::string op;
        ls >> op;
        entry e;
        e.line = n;
        e.op = op[0];
        if (op == "D") {
            unsigned vid = 0, pid = 0, rel = 0;
            int iface = 0;
            ls >> std::hex >> vid >> pid >> rel >> std::dec >> iface;
            std::string name;
            std::getline(ls, name);
            if (info) {
                std::memset(info, 0, sizeof *info);
                std::snprintf(info->path, sizeof info->path, "replay:%s", file.c_str());
                info->vendor_id = static_cast<uint16_t>(vid);
                info->product_id = static_cast<uint16_t>(pid);
                info->release = static_cast<uint16_t>(rel);
                info->interface_number = iface;
                std::snprintf(info->name, sizeof info->name, "%s", name.c_str() + (name.empty() ? 0 : 1));
            }
            have_device = true;
            continue;
        }
        if (op == "G") {
            unsigned id = 0;
            std::string colon;
            ls >> std::hex >> id >> std::dec >> e.want >> colon;
            e.id = static_cast<uint8_t>(id);
            if (colon != ":")
                return nullptr;
        } else if (op != "S" && op != "R" && op != "W") {
            return nullptr;
        }
        // whats left is the bytes, or a "!" where the transfer failed while recording
        std::string rest;
        std::getline(ls, rest);
        size_t bang = rest.find('!');
        e.failed = bang != std::string::npos;
        if (e.failed)
            rest.resize(rest.find(':') == std::string::npos ? bang : rest.find(':'));
        std::istringstream bytes(rest);
        if (!unhex(bytes, e.data))
            return nullptr;
        rep->entries.push_back(std::move(e));
    }
    if (!have_device)
        return nullptr;
    return rep;
}
