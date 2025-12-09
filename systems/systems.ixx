module;
#include <functional>
#include <list>
#include <memory>
#include "system.hh"

export module systems.systems;

import systems.cdrom;
import systems.high_sierra;
import systems.iso;
import systems.dc;
import systems.mcd;
import systems.psx;
import systems.ps2;
import systems.ps3;
import systems.ps4;
import systems.ps5;
import systems.securom;
import systems.sat;
import systems.xbox;



namespace gpsxre
{

export class Systems
{
public:
    using Creator = std::function<std::unique_ptr<System>()>;

    Systems() = delete;

    static std::list<Creator> get()
    {
        std::list<Creator> systems;

        systems.push_back([]() { return std::make_unique<SystemCDROM>(); });
        systems.push_back([]() { return std::make_unique<SystemSecuROM>(); });
        systems.push_back([]() { return std::make_unique<SystemHighSierra>(); });
        systems.push_back([]() { return std::make_unique<SystemISO>(); });
        systems.push_back([]() { return std::make_unique<SystemDC>(); });
        systems.push_back([]() { return std::make_unique<SystemMCD>(); });
        systems.push_back([]() { return std::make_unique<SystemPSX>(); });
        systems.push_back([]() { return std::make_unique<SystemPS2>(); });
        systems.push_back([]() { return std::make_unique<SystemPS3>(); });
        systems.push_back([]() { return std::make_unique<SystemPS4>(); });
        systems.push_back([]() { return std::make_unique<SystemPS5>(); });
        systems.push_back([]() { return std::make_unique<SystemSAT>(); });
        systems.push_back([]() { return std::make_unique<SystemXBOX>(); });

        return systems;
    }
};

}
