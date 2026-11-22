// the real thing: hidraw through hidapi. this is the only file that includes hidapi

#include "transport.h"

#include <hidapi/hidapi.h>

#include <algorithm>
#include <chrono>
#include <thread>

void transport::sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

namespace {

std::string narrow(const wchar_t *src)
{
    std::string out;
    for (; src && *src; ++src)
        out += *src < 0x80 ? static_cast<char>(*src) : '?';
    return out;
}

struct hid_transport final : transport {
    hid_device *hid;
    explicit hid_transport(hid_device *h) : hid(h) {}
    ~hid_transport() override { hid_close(hid); }

    int get_feature(uint8_t *buf, size_t len) override
    {
        int r = hid_get_feature_report(hid, buf, len);
        return r < 0 ? ROCKCAT_ERR_IO : r;
    }
    int set_feature(const uint8_t *buf, size_t len) override
    {
        return hid_send_feature_report(hid, buf, len) < 0 ? ROCKCAT_ERR_IO : ROCKCAT_OK;
    }
    int read(uint8_t *buf, size_t len, int timeout_ms) override
    {
        int r = hid_read_timeout(hid, buf, len, timeout_ms);
        return r < 0 ? ROCKCAT_ERR_IO : r;
    }
};

} // namespace

int hid_backend_init()
{
    return hid_init() == 0 ? ROCKCAT_OK : ROCKCAT_ERR_INIT;
}

void hid_backend_exit()
{
    hid_exit();
}

std::vector<hid_node> hid_nodes(uint16_t vid)
{
    std::vector<hid_node> out;
    hid_device_info *all = hid_enumerate(vid, 0);
    for (hid_device_info *d = all; d; d = d->next) {
        // hidraw lists one entry per top level collection of the same node, we want the node once
        bool seen = std::any_of(out.begin(), out.end(), [&](const hid_node &n) { return n.path == d->path; });
        if (seen)
            continue;
        hid_node n;
        n.path = d->path;
        n.product = narrow(d->product_string);
        n.serial = narrow(d->serial_number);
        n.vid = d->vendor_id;
        n.pid = d->product_id;
        n.release = d->release_number;
        n.interface_number = d->interface_number;
        out.push_back(n);
    }
    hid_free_enumeration(all);
    return out;
}

std::vector<uint8_t> hid_report_ids(const std::string &path)
{
    std::vector<uint8_t> ids;
    hid_device *h = hid_open_path(path.c_str());
    if (!h)
        return ids;
    unsigned char desc[HID_API_MAX_REPORT_DESCRIPTOR_SIZE];
    int len = hid_get_report_descriptor(h, desc, sizeof desc);
    hid_close(h);
    // global item "Report ID" is encoded as 0x85 <id>
    for (int i = 0; i < len;) {
        unsigned char prefix = desc[i];
        if (prefix == 0xfe) { // long item
            if (i + 1 >= len)
                break;
            i += 3 + desc[i + 1];
            continue;
        }
        int size = prefix & 0x03;
        if (size == 3)
            size = 4;
        if ((prefix & 0xfc) == 0x84 && size == 1 && i + 1 < len &&
            std::find(ids.begin(), ids.end(), desc[i + 1]) == ids.end())
            ids.push_back(desc[i + 1]);
        i += 1 + size;
    }
    return ids;
}

std::unique_ptr<transport> transport_open_hid(const std::string &path)
{
    hid_device *h = hid_open_path(path.c_str());
    if (!h)
        return nullptr;
    return std::unique_ptr<transport>(new hid_transport(h));
}
