// PROJECT: Z-Wave CLI
// VERSION: 0.3
//
// DESCRIPTION:
//   Native OpenZWave bridge providing controller management,
//   device enumeration, device information, binary switch
//   control and verified operation results.

#include <Manager.h>
#include <Options.h>
#include <Notification.h>
#include <Driver.h>

#include <unistd.h>
#include <vector>
#include <mutex>
#include <algorithm>
#include <map>
#include <string>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <cstdio>
#include <chrono>

using namespace OpenZWave;

static std::mutex m;

static uint32_t g_home = 0;
static bool g_driver_failed = false;
static bool g_nodes_ready = false;
static bool g_manager_created = false;
static bool g_options_created = false;

static bool g_controller_active = false;
static bool g_controller_waiting = false;
static int g_controller_command = 0;
static int g_controller_result = -1;

static std::vector<uint8_t> g_nodes;
static std::map<uint8_t, ValueID> g_switches;
static std::map<uint8_t, unsigned int> g_updates;

static bool is_binary_switch(const ValueID& v)
{
    return v.GetCommandClassId() == 0x25 &&
           v.GetInstance() == 1 &&
           v.GetIndex() == 0 &&
           v.GetType() == ValueID::ValueType_Bool;
}

static void cb(Notification const* n, void*)
{
    std::lock_guard<std::mutex> lk(m);

    auto t = n->GetType();

    if (t == Notification::Type_DriverReady)
        g_home = n->GetHomeId();

    if (t == Notification::Type_DriverFailed)
        g_driver_failed = true;

    if (t == Notification::Type_AwakeNodesQueried ||
        t == Notification::Type_AllNodesQueried ||
        t == Notification::Type_AllNodesQueriedSomeDead)
        g_nodes_ready = true;

    if (t == Notification::Type_NodeAdded)
    {
        uint8_t id = n->GetNodeId();

        if (std::find(g_nodes.begin(),
                      g_nodes.end(), id) == g_nodes.end())
            g_nodes.push_back(id);
    }

    if (t == Notification::Type_NodeRemoved)
    {
        uint8_t id = n->GetNodeId();

        g_nodes.erase(
            std::remove(g_nodes.begin(), g_nodes.end(), id),
            g_nodes.end());

        g_switches.erase(id);
        g_updates.erase(id);
    }

    if (t == Notification::Type_ValueAdded ||
        t == Notification::Type_ValueChanged ||
        t == Notification::Type_ValueRefreshed)
    {
        const ValueID v = n->GetValueID();

        if (is_binary_switch(v))
        {
            uint8_t id = v.GetNodeId();

            g_switches.erase(id);
            g_switches.emplace(id, v);

            if (t != Notification::Type_ValueAdded)
                ++g_updates[id];
        }
    }

    if (t == Notification::Type_ValueRemoved)
    {
        const ValueID v = n->GetValueID();

        if (is_binary_switch(v))
        {
            g_switches.erase(v.GetNodeId());
            g_updates.erase(v.GetNodeId());
        }
    }

    if (t == Notification::Type_ControllerCommand &&
        g_controller_active &&
        n->GetCommand() == g_controller_command)
    {
        switch (n->GetEvent())
        {
            case Driver::ControllerState_Waiting:
                g_controller_waiting = true;
                break;

            case Driver::ControllerState_Completed:
                g_controller_result = 0;
                break;

            case Driver::ControllerState_Cancel:
            case Driver::ControllerState_Error:
            case Driver::ControllerState_Failed:
                g_controller_result = 4;
                break;

            default:
                break;
        }
    }
}

static std::string known(const std::string& value)
{
    return value.empty() ? "Unknown" : value;
}

extern "C" {

void ozw_shutdown(const char* dev)
{
    if (g_manager_created)
    {
        try {
            Manager::Get()->RemoveDriver(dev);
        } catch (...) {}

        try {
            Manager::Destroy();
        } catch (...) {}

        g_manager_created = false;
    }

    if (g_options_created)
    {
        try {
            Options::Destroy();
        } catch (...) {}

        g_options_created = false;
    }
}

int ozw_init(const char* dev)
{
    try
    {
        Options::Create("/etc/openzwave/", ".", "");
        g_options_created = true;

        Options::Get()->AddOptionBool(
            "ConsoleOutput", false);

        Options::Get()->Lock();

        Manager::Create();
        g_manager_created = true;

        Manager::Get()->AddWatcher(cb, nullptr);

        if (!Manager::Get()->AddDriver(dev))
            return -1;

        for (int i = 0; i < 600; ++i)
        {
            {
                std::lock_guard<std::mutex> lk(m);

                if (g_driver_failed)
                    return -1;

                if (g_home != 0)
                    return 0;
            }

            usleep(100000);
        }
    }
    catch (...)
    {
        return -1;
    }

    return -1;
}

int ozw_node_exists(uint8_t node)
{
    for (int i = 0; i < 150; ++i)
    {
        {
            std::lock_guard<std::mutex> lk(m);

            if (g_driver_failed)
                return -1;

            if (std::find(g_nodes.begin(),
                          g_nodes.end(), node) != g_nodes.end())
                return 1;

            if (g_nodes_ready)
                return 0;
        }

        usleep(100000);
    }

    // Enumeration did not finish.
    // Do not incorrectly report a missing node.
    return -1;
}

static int controller_operation(bool adding)
{
    {
        std::lock_guard<std::mutex> lk(m);

        g_controller_active = true;
        g_controller_waiting = false;
        g_controller_command = adding
            ? Driver::ControllerCommand_AddDevice
            : Driver::ControllerCommand_RemoveDevice;
        g_controller_result = -1;
    }

    int result = 5;

    try
    {
        bool started = adding
            ? Manager::Get()->AddNode(g_home, true)
            : Manager::Get()->RemoveNode(g_home);

        if (!started)
        {
            result = 4;
        }
        else
        {
            bool prompted = false;
            auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(30);

            while (std::chrono::steady_clock::now() < deadline)
            {
                {
                    std::lock_guard<std::mutex> lk(m);

                    if (g_driver_failed)
                    {
                        result = 1;
                        break;
                    }

                    if (g_controller_waiting && !prompted)
                    {
                        std::fprintf(stderr,
                            "%s mode ACTIVE (30 seconds).\n",
                            adding ? "Inclusion" : "Exclusion");
                        std::fflush(stderr);
                        prompted = true;
                        deadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(30);
                    }

                    if (g_controller_result >= 0)
                    {
                        result = g_controller_result;
                        break;
                    }
                }

                usleep(100000);
            }

            if (result == 5)
            {
                if (!Manager::Get()->CancelControllerCommand(g_home))
                    result = 4;

                // Allow cancellation notification to arrive.
                for (int i = 0; i < 20; ++i)
                {
                    {
                        std::lock_guard<std::mutex> lk(m);

                        if (g_controller_result >= 0)
                            break;
                    }

                    usleep(100000);
                }
            }
        }
    }
    catch (...)
    {
        result = 4;
    }

    {
        std::lock_guard<std::mutex> lk(m);
        g_controller_active = false;
    }

    return result;
}

int ozw_add()
{
    return controller_operation(true);
}

int ozw_remove()
{
    return controller_operation(false);
}

int ozw_list(uint8_t* out, int max)
{
    for (int i = 0; i < 150; ++i)
    {
        {
            std::lock_guard<std::mutex> lk(m);

            if (g_driver_failed)
                return -1;

            if (g_nodes_ready)
            {
                int count = g_nodes.size();

                if (count > max)
                    return -1;

                for (int j = 0; j < count; ++j)
                    out[j] = g_nodes[j];

                return count;
            }
        }

        usleep(100000);
    }

    return -1;
}

int ozw_status(uint8_t node)
{
    try
    {
        std::vector<ValueID> values;
        unsigned int previous = 0;

        for (int i = 0; i < 100; ++i)
        {
            {
                std::lock_guard<std::mutex> lk(m);

                if (g_driver_failed)
                    return -1;

                auto it = g_switches.find(node);

                if (it != g_switches.end())
                {
                    values.push_back(it->second);
                    previous = g_updates[node];
                    break;
                }
            }

            usleep(100000);
        }

        if (values.empty())
            return -1;

        if (!Manager::Get()->RefreshValue(values[0]))
            return -1;

        for (int i = 0; i < 150; ++i)
        {
            bool updated;

            {
                std::lock_guard<std::mutex> lk(m);

                if (g_driver_failed)
                    return -1;

                updated = g_updates[node] != previous;
            }

            if (updated)
            {
                bool state = false;

                if (Manager::Get()->GetValueAsBool(
                        values[0], &state))
                    return state ? 1 : 0;

                return -1;
            }

            usleep(100000);
        }
    }
    catch (...)
    {
        return -1;
    }

    return -1;
}

static int ozw_set_switch(uint8_t node, bool state)
{
    try
    {
        std::vector<ValueID> values;

        for (int i = 0; i < 100; ++i)
        {
            {
                std::lock_guard<std::mutex> lk(m);

                if (g_driver_failed)
                    return -1;

                auto it = g_switches.find(node);

                if (it != g_switches.end())
                {
                    values.push_back(it->second);
                    break;
                }
            }

            usleep(100000);
        }

        if (values.empty())
            return -1;

        if (!Manager::Get()->SetValue(values[0], state))
            return -1;

        sleep(3);

        int actual = ozw_status(node);

        return actual == (state ? 1 : 0) ? 0 : -1;
    }
    catch (...)
    {
        return -1;
    }
}

int ozw_on(uint8_t node)
{
    return ozw_set_switch(node, true);
}

int ozw_off(uint8_t node)
{
    return ozw_set_switch(node, false);
}


int ozw_remove_failed(uint8_t node)
{
    int exists = ozw_node_exists(node);

    if (exists == 0)
        return 3;

    if (exists < 0)
        return 1;

    try
    {
        Manager* mgr = Manager::Get();

        if (node == mgr->GetControllerNodeId(g_home))
            return 2;

        {
            std::lock_guard<std::mutex> lk(m);

            g_controller_active = true;
            g_controller_waiting = false;
            g_controller_result = -1;
            g_controller_command =
                Driver::ControllerCommand_RemoveFailedNode;
        }

        int result = 4;

        if (mgr->RemoveFailedNode(g_home, node))
        {
            result = 5;

            for (int i = 0; i < 600; ++i)
            {
                {
                    std::lock_guard<std::mutex> lk(m);

                    if (g_driver_failed)
                    {
                        result = 1;
                        break;
                    }

                    if (g_controller_result >= 0)
                    {
                        result = g_controller_result;
                        break;
                    }
                }

                usleep(100000);
            }

            if (result == 5)
                mgr->CancelControllerCommand(g_home);
        }

        {
            std::lock_guard<std::mutex> lk(m);
            g_controller_active = false;
        }

        if (result != 0)
            return result;

        for (int i = 0; i < 50; ++i)
        {
            {
                std::lock_guard<std::mutex> lk(m);

                if (std::find(g_nodes.begin(),
                              g_nodes.end(), node)
                    == g_nodes.end())
                    return 0;
            }

            usleep(100000);
        }

        return 4;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lk(m);
        g_controller_active = false;
        return 4;
    }
}

int ozw_info(uint8_t node, char* out, int max)
{
    int exists = ozw_node_exists(node);

    if (exists == 0)
        return 3;

    if (exists < 0)
        return 1;

    try
    {
        Manager* mgr = Manager::Get();

        // Allow device identification data to arrive.
        for (int i = 0; i < 100; ++i)
        {
            if (!mgr->GetNodeManufacturerId(
                    g_home, node).empty())
                break;

            usleep(100000);
        }

        std::ostringstream info;

        info << "Node:             "
             << static_cast<unsigned>(node) << "\n";

        info << "Manufacturer:     "
             << known(mgr->GetNodeManufacturerName(
                      g_home,node)) << "\n";

        info << "Product:          "
             << known(mgr->GetNodeProductName(
                      g_home,node)) << "\n";

        info << "Device Type:      "
             << known(mgr->GetNodeType(
                      g_home,node)) << "\n";

        info << "Name:             "
             << known(mgr->GetNodeName(
                      g_home,node)) << "\n";

        info << "Manufacturer ID:  "
             << known(mgr->GetNodeManufacturerId(
                      g_home,node)) << "\n";

        info << "Product Type:     "
             << known(mgr->GetNodeProductType(
                      g_home,node)) << "\n";

        info << "Product ID:       "
             << known(mgr->GetNodeProductId(
                      g_home,node)) << "\n";

        info << "Listening:        "
             << (mgr->IsNodeListeningDevice(g_home,node)
                 ? "Yes" : "No") << "\n";

        info << "Routing:          "
             << (mgr->IsNodeRoutingDevice(g_home,node)
                 ? "Yes" : "No") << "\n";

        info << "Awake:            "
             << (mgr->IsNodeAwake(g_home,node)
                 ? "Yes" : "No") << "\n";

        info << "Failed:           "
             << (mgr->IsNodeFailed(g_home,node)
                 ? "Yes" : "No") << "\n";

        info << "Capabilities:\n";

        bool found = false;

        for (int cc = 0; cc < 256; ++cc)
        {
            std::string name;
            uint8_t version = 0;

            if (mgr->GetNodeClassInformation(
                    g_home,node,
                    static_cast<uint8_t>(cc),
                    &name,&version))
            {
                found = true;

                info << "  0x"
                     << std::hex
                     << std::uppercase
                     << std::setw(2)
                     << std::setfill('0')
                     << cc
                     << std::dec
                     << "  " << known(name)
                     << " (v"
                     << static_cast<unsigned>(version)
                     << ")\n";
            }
        }

        if (!found)
            info << "  Unknown\n";

        std::string result = info.str();

        if (max <= 0 ||
            result.size() >= static_cast<size_t>(max))
            return 4;

        std::memcpy(out,
                    result.c_str(),
                    result.size() + 1);

        return 0;
    }
    catch (...)
    {
        return 4;
    }
}

}
