#pragma once
#include <vector>
#include <string>

class sqlite3DB;
class authenticator;

struct serverData
{
	static sqlite3DB* database;
	static authenticator* auth;

	enum tables
	{
		USER,
		SUPPLIERS,
		PARTGROUPS,
		PARTS,
		PARTSINSERVICE,
		VEHICLESHARED,
		VEHICLES,
		SERVICESHARED,
		SERVICEUNAUTHORISED,
		SERVICEACTIVE,
		SERVICEOPEN,
		SERVICECLOSED
	};
	static const std::vector<std::string> tableNames;
};