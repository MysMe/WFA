#pragma once
#include "ServerData.h"

sqlite3DB* serverData::database = nullptr;
authenticator* serverData::auth = nullptr;

//Table names as found in sqlcrt.txt
const std::vector<std::string> serverData::tableNames
{
	"USERS",
	"SUPPLIERS",
	"PARTGROUPS",
	"PARTS",
	"PARTSINSERVICE",
	"VEHICLESHAREDDATA",
	"VEHICLES",
	"SERVICESHAREDDATA",
	"UNAUTHORISEDSERVICES",
	"ACTIVESERVICEDATA",
	"OPENSERVICES",
	"CLOSEDSERVICES"
};