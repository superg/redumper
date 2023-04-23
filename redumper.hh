#pragma once



#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <vector>
#include "cd.hh"
#include "common.hh"
#include "drive.hh"
#include "options.hh"
#include "scsi.hh"
#include "toc.hh"



namespace gpsxre
{

enum class DiscType
{
	CD,
	DVD,
	BLURAY
};

void redumper(Options &options);

std::string redumper_version();
void normalize_options(Options &options);
std::string first_ready_drive();
DiscType query_disc_type(std::string drive);

void redumper_cd(Options &options);
void redumper_dump(Options &options);
void redumper_refine(Options &options);
void redumper_protection(Options &options);
void redumper_split(Options &options);
void redumper_info(Options &options);
void redumper_subchannel(Options &options);
void redumper_debug(Options &options);

}
