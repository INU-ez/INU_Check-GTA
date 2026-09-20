// gtacheck — vehicle DFF rules VEH-nn (models from the `cars` section of vehicles.ide).
//
// What the engine does with a vehicle clump (gta_sa.exe 1.0 US, verified by decompile
// and by running a reimplementation inside the live game — see
// memory/gta-vehicle-dummy-mechanism.md and E:\RE\vehicle_descs.txt):
//
//   * CVehicleModelInfo::SetClump → PreprocessHierarchy picks the per-type table
//     ms_vehicleDescs[vehicleType] (0x8A7740) and CClumpModelInfo::SetFrameIds (0x4C5460)
//     walks it: every frame is looked up BY NAME (_stricmp — casing is free), the
//     hierarchy id is stamped through CVisibilityPlugins::SetFrameHierarchyId, entries
//     flagged VEH_STRUCT_PART (bit 0) are dummies/extras/upgrades handled by name
//     separately.  Only frames whose id is still 0 are considered, so a duplicate
//     name loses.  After that nothing looks at names again — every access goes
//     through GetFrameFromId (0x4C53C0).
//   * ms_wheelFrameIDs (0x8A7770) = {5, 7, 2, 4}: wheel_lf_dummy, wheel_lb_dummy,
//     wheel_rf_dummy, wheel_rb_dummy.  CVehicleModelInfo::GetWheelPosn (0x4C7D20)
//     dereferences the frame GetFrameFromId returns without a NULL check on either
//     branch (0x4C7D57 / 0x4C7DAD) — every CAutomobile-derived vehicle (car, mtruck,
//     quad, heli, plane, trailer) calls it when spawned, so a missing wheel dummy
//     faults instead of degrading.  wheel_lm/rm (ids 6/3) are never returned by it.
//   * The Collision Plugin (0x253F2FA) in the CLUMP extension is handed by
//     CollisionPlugin::ReadCB (0x41B1D0) to the COL3 parser.  Vanilla rccam.dff
//     carries an old-format blob there and gets garbage collision.
//
// The 12 tables below are a verbatim copy of E:\RE\vehicle_descs.txt, dumped from
// gta_sa.exe 1.0 US by E:\RE\dump_vehicle_descs.py (ms_vehicleDescs @ 0x8A7740,
// index = eVehicleType as written in vehicles.ide).  Kept complete so VEH-04/05/06
// can tell "a name the engine knows" from "a name only the modeller knows".
#include "gtacheck.h"

#include <algorithm>
#include <stdlib.h>

namespace gc {

// ------------------------------------------------------------- tables ----

// RwObjectNameIdAssocation flags (bit meanings from the dump)
enum {
	VF_STRUCT_PART = 0x1,	// dummy / extra / upgrade — SetFrameIds skips it
	VF_DAMAGEABLE  = 0x2,
	VF_WHEEL       = 0x4,
	VF_DUMMY       = 0x8,
	VF_UPGRADE     = 0x20000,
	VF_EXTRA       = 0x200,
	VF_MAIN_WHEEL  = 0x10000,
};

struct VehDesc { const char *name; int id; uint32_t flags; };

// -- [0] CAR @ 0x008A6468 (61 entries)
static const VehDesc T_CAR[] = {
	{"chassis",1,0x0},{"wheel_rf_dummy",2,0x10040},{"wheel_rm_dummy",3,0x44},{"wheel_rb_dummy",4,0x44},
	{"wheel_lf_dummy",5,0x24},{"wheel_lm_dummy",6,0x24},{"wheel_lb_dummy",7,0x24},
	{"door_rf_dummy",8,0xD052},{"door_rr_dummy",9,0xB152},{"door_lf_dummy",10,0xD032},{"door_lr_dummy",11,0xB132},
	{"bump_front_dummy",12,0x82},{"bump_rear_dummy",13,0x102},{"wing_rf_dummy",14,0x2},{"wing_lf_dummy",15,0x2},
	{"bonnet_dummy",16,0x2},{"boot_dummy",17,0x8102},{"windscreen_dummy",18,0xC82},{"exhaust_ok",19,0x102},
	{"misc_a",20,0x0},{"misc_b",21,0x0},{"misc_c",22,0x0},{"misc_d",23,0x0},{"misc_e",24,0x0},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"ped_arm",10,0x9},
	{"miscpos_c",11,0x9},{"miscpos_d",12,0x9},{"miscpos_a",13,0x9},{"miscpos_b",14,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
	{"ug_bonnet",0,0x20001},{"ug_bonnet_left",1,0x20001},{"ug_bonnet_right",2,0x20001},{"ug_bonnet_dam",3,0x20001},
	{"ug_bonnet_left_dam",4,0x20001},{"ug_bonnet_right_dam",5,0x20001},{"ug_spoiler",6,0x20001},{"ug_spoiler_dam",7,0x20001},
	{"ug_wing_left",8,0x20001},{"ug_wing_right",9,0x20001},{"ug_frontbullbar",10,0x20001},{"ug_backbullbar",11,0x20001},
	{"ug_lights",12,0x20001},{"ug_lights_dam",13,0x20001},{"ug_roof",14,0x20001},{"ug_nitro",15,0x20001},
};
// -- [1] MTRUCK @ 0x008A6B80 (43 entries)
static const VehDesc T_MTRUCK[] = {
	{"chassis",1,0x0},{"wheel_rf_dummy",2,0x10040},{"wheel_rm_dummy",3,0x44},{"wheel_rb_dummy",4,0x44},
	{"wheel_lf_dummy",5,0x24},{"wheel_lm_dummy",6,0x24},{"wheel_lb_dummy",7,0x24},
	{"door_rf_dummy",8,0x5052},{"door_rr_dummy",9,0x3152},{"door_lf_dummy",10,0x5032},{"door_lr_dummy",11,0x3132},
	{"bump_front_dummy",12,0x82},{"bump_rear_dummy",13,0x102},{"wing_rf_dummy",14,0x2},{"wing_lf_dummy",15,0x2},
	{"bonnet_dummy",16,0x2},{"boot_dummy",17,0x102},{"windscreen_dummy",18,0xC82},
	{"transmission_f",19,0x0},{"transmission_r",20,0x0},{"loadbay",21,0x2},{"misc_a",22,0x2},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"ped_arm",10,0x9},
	{"miscpos_c",11,0x9},{"miscpos_d",12,0x9},{"miscpos_a",13,0x9},{"miscpos_b",14,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [2] QUAD @ 0x008A6D90 (40 entries)
static const VehDesc T_QUAD[] = {
	{"chassis",1,0x0},{"wheel_rf_dummy",2,0x10040},{"wheel_rm_dummy",3,0x44},{"wheel_rb_dummy",4,0x44},
	{"wheel_lf_dummy",5,0x24},{"wheel_lm_dummy",6,0x24},{"wheel_lb_dummy",7,0x24},
	{"door_rf_dummy",8,0x5052},{"door_rr_dummy",9,0x3152},{"door_lf_dummy",10,0x5032},{"door_lr_dummy",11,0x3132},
	{"body_front_dummy",12,0x82},{"body_rear_dummy",13,0x102},{"suspension_rf",14,0x82},{"suspension_lf",15,0x82},
	{"rear_axle",16,0x102},{"handlebars",17,0x82},{"misc_a",18,0x2},{"misc_b",19,0x2},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"ped_arm",10,0x9},
	{"miscpos_c",11,0x9},{"miscpos_d",12,0x9},{"miscpos_a",13,0x9},{"miscpos_b",14,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [3] HELI @ 0x008A6978 (42 entries)
static const VehDesc T_HELI[] = {
	{"chassis",1,0x0},{"wheel_rf_dummy",2,0x10040},{"wheel_rm_dummy",3,0x44},{"wheel_rb_dummy",4,0x44},
	{"wheel_lf_dummy",5,0x24},{"wheel_lm_dummy",6,0x24},{"wheel_lb_dummy",7,0x24},
	{"door_rf_dummy",8,0x404052},{"door_rr_dummy",9,0x402152},{"door_lf_dummy",10,0x404032},{"door_lr_dummy",11,0x402132},
	{"static_rotor",12,0x40402},{"moving_rotor",13,0x40402},{"static_rotor2",14,0x40042},{"moving_rotor2",15,0x40442},
	{"rudder",16,0x102},{"elevators",17,0x102},{"misc_a",18,0x2},{"misc_b",19,0x2},{"misc_c",20,0x2},{"misc_d",21,0x2},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"ped_arm",10,0x9},
	{"miscpos_c",11,0x9},{"miscpos_d",12,0x9},{"miscpos_a",13,0x9},{"miscpos_b",14,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [4] PLANE @ 0x008A6750 (45 entries)
static const VehDesc T_PLANE[] = {
	{"chassis",1,0x0},{"wheel_rf_dummy",2,0x10040},{"wheel_rm_dummy",3,0x44},{"wheel_rb_dummy",4,0x44},
	{"wheel_lf_dummy",5,0x24},{"wheel_lm_dummy",6,0x24},{"wheel_lb_dummy",7,0x24},
	{"door_rf_dummy",8,0x400412},{"door_rr_dummy",9,0x400412},{"door_lf_dummy",10,0x400412},{"door_lr_dummy",11,0x400412},
	{"static_prop",12,0x40482},{"moving_prop",13,0x40482},{"static_prop2",14,0x40482},{"moving_prop2",15,0x40482},
	{"rudder",16,0x102},{"elevator_l",17,0x102},{"elevator_r",18,0x102},{"aileron_l",19,0x2},{"aileron_r",20,0x2},
	{"gear_l",21,0x0},{"gear_r",22,0x0},{"misc_a",23,0x0},{"misc_b",24,0x0},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"aileron_pos",9,0x9},{"elevator_pos",10,0x9},{"rudder_pos",11,0x9},
	{"wingtip_pos",12,0x9},{"miscpos_a",13,0x9},{"miscpos_b",14,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [5] BOAT @ 0x008A6F80 (18 entries)
static const VehDesc T_BOAT[] = {
	{"boat_moving_hi",1,0x0},{"boat_rudder_hi",3,0x0},{"boat_flap_left",4,0x0},{"boat_flap_right",5,0x0},
	{"boat_rearflap_left",6,0x0},{"boat_rearflap_right",7,0x0},
	{"static_prop",8,0x40100},{"moving_prop",9,0x40500},{"static_prop2",10,0x40100},{"moving_prop2",11,0x40500},
	{"windscreen_hi_ok",2,0xC00},{"ped_frontseat",0,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [6] TRAIN @ 0x008A7068 (27 entries)
static const VehDesc T_TRAIN[] = {
	{"door_lf_dummy",1,0x22},{"door_rf_dummy",2,0x42},
	{"wheel_rf1_dummy",3,0x10040},{"wheel_rf2_dummy",4,0x44},{"wheel_rf3_dummy",5,0x44},
	{"wheel_rb1_dummy",6,0x44},{"wheel_rb2_dummy",7,0x44},{"wheel_rb3_dummy",8,0x44},
	{"wheel_lf1_dummy",9,0x24},{"wheel_lf2_dummy",10,0x24},{"wheel_lf3_dummy",11,0x24},
	{"wheel_lb1_dummy",12,0x24},{"wheel_lb2_dummy",13,0x24},{"wheel_lb3_dummy",14,0x24},
	{"bogie_front",15,0x100000},{"bogie_rear",16,0x200000},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"ped_left_entry",9,0x19},{"ped_mid_entry",10,0x19},{"ped_right_entry",11,0x19},
};
// -- [7] FHELI @ 0x008A71B8 (7 entries)
static const VehDesc T_FHELI[] = {
	{"chassis_dummy",1,0x2},{"toprotor",2,0x400},{"backrotor",3,0x400},{"tail",4,0x0},{"topknot",5,0x0},{"skid_left",6,0x0},{"skid_right",7,0x0},
};
// -- [8] FPLANE @ 0x008A7218 (6 entries)
static const VehDesc T_FPLANE[] = {
	{"wheel_front_dummy",2,0x0},{"wheel_rear_dummy",3,0x0},{"propeller",4,0x480},
	{"light_tailplane",0,0x9},{"light_left",1,0x9},{"light_right",2,0x9},
};
// -- [9] BIKE @ 0x008A7270 (28 entries)
static const VehDesc T_BIKE[] = {
	{"chassis_dummy",1,0x0},{"forks_front",2,0x0},{"forks_rear",3,0x0},{"wheel_front",4,0x0},{"wheel_rear",5,0x0},
	{"mudguard",6,0x0},{"handlebars",7,0x0},{"misc_a",8,0x0},{"misc_b",9,0x0},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"bargrip",10,0x9},{"miscpos_a",11,0x9},{"miscpos_b",12,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [10] BMX @ 0x008A73D0 (28 entries)
static const VehDesc T_BMX[] = {
	{"chassis_dummy",1,0x0},{"forks_front",2,0x0},{"forks_rear",3,0x0},{"wheel_front",4,0x0},{"wheel_rear",5,0x0},
	{"handlebars",6,0x0},{"chainset",7,0x0},{"pedal_r",8,0x0},{"pedal_l",9,0x0},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"bargrip",10,0x9},{"miscpos_a",11,0x9},{"miscpos_b",12,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};
// -- [11] TRAILER @ 0x008A7530 (43 entries)
static const VehDesc T_TRAILER[] = {
	{"chassis",1,0x0},{"wheel_rf_dummy",2,0x10040},{"wheel_rm_dummy",3,0x44},{"wheel_rb_dummy",4,0x44},
	{"wheel_lf_dummy",5,0x24},{"wheel_lm_dummy",6,0x24},{"wheel_lb_dummy",7,0x24},
	{"door_rf_dummy",8,0x5052},{"door_rr_dummy",9,0x3152},{"door_lf_dummy",10,0x5032},{"door_lr_dummy",11,0x3132},
	{"bump_front_dummy",12,0x82},{"bump_rear_dummy",13,0x102},{"wing_rf_dummy",14,0x2},{"wing_lf_dummy",15,0x2},
	{"bonnet_dummy",16,0x2},{"boot_dummy",17,0x102},{"windscreen_dummy",18,0xC82},{"exhaust_ok",19,0x102},
	{"misc_a",20,0x2},{"misc_b",21,0x2},{"misc_c",22,0x2},
	{"ped_frontseat",4,0x9},{"ped_backseat",5,0x9},{"headlights",0,0x9},{"taillights",1,0x9},{"headlights2",2,0x9},{"taillights2",3,0x9},
	{"exhaust",6,0x9},{"engine",7,0x9},{"petrolcap",8,0x9},{"hookup",9,0x9},{"ped_arm",10,0x9},
	{"miscpos_c",11,0x9},{"miscpos_d",12,0x9},{"miscpos_a",13,0x9},{"miscpos_b",14,0x9},
	{"extra1",0,0x601},{"extra2",0,0x601},{"extra3",0,0x601},{"extra4",0,0x601},{"extra5",0,0x601},{"extra6",0,0x601},
};

enum WheelScheme {
	WS_NONE,	// no wheel lookup by the engine (boat, f_heli, f_plane)
	WS_CAR,		// CAutomobile-derived: GetWheelPosn on ids {5,7,2,4}
	WS_BIKE,	// CBike: wheel_front / wheel_rear (ids 4/5)
	WS_TRAIN,	// CTrain: wheel_rf1..lb3 scheme of its own (VEH-09, not checked)
};

struct VehType {
	const char *ide;	// as written in vehicles.ide
	int vehicleType;	// eVehicleType / ms_vehicleDescs index
	const VehDesc *tab;
	int n;
	WheelScheme wheels;
	bool wantChassisDummy;	// every vanilla model of this type has chassis_dummy as the hierarchy root
};

#define TAB(x) x, (int)(sizeof(x) / sizeof(x[0]))
static const VehType VEH_TYPES[] = {
	{ "car",     0,  TAB(T_CAR),     WS_CAR,   true  },
	{ "mtruck",  1,  TAB(T_MTRUCK),  WS_CAR,   true  },
	{ "quad",    2,  TAB(T_QUAD),    WS_CAR,   true  },
	{ "heli",    3,  TAB(T_HELI),    WS_CAR,   true  },
	{ "plane",   4,  TAB(T_PLANE),   WS_CAR,   false },	// vanilla planes root at "<model>" and have no chassis_dummy
	{ "boat",    5,  TAB(T_BOAT),    WS_NONE,  false },
	{ "train",   6,  TAB(T_TRAIN),   WS_TRAIN, false },
	{ "f_heli",  7,  TAB(T_FHELI),   WS_NONE,  true  },	// chassis_dummy is in its table (id 1)
	{ "f_plane", 8,  TAB(T_FPLANE),  WS_NONE,  false },
	{ "bike",    9,  TAB(T_BIKE),    WS_BIKE,  true  },
	{ "bmx",     10, TAB(T_BMX),     WS_BIKE,  true  },
	{ "trailer", 11, TAB(T_TRAILER), WS_CAR,   true  },
};
#undef TAB

// ms_wheelFrameIDs @ 0x8A7770 in eCarWheel order; names resolved through the CAR table
static const char *CAR_WHEELS[4] = { "wheel_lf_dummy", "wheel_lb_dummy", "wheel_rf_dummy", "wheel_rb_dummy" };
static const char *CAR_WHEEL_DESC[4] = { "переднее левое", "заднее левое", "переднее правое", "заднее правое" };
static const char *BIKE_WHEELS[2] = { "wheel_front", "wheel_rear" };
static const char *BIKE_WHEEL_DESC[2] = { "переднее", "заднее" };

// ------------------------------------------------------------ helpers ----

static const VehType *findType(const std::string &ideType)
{
	std::string t = lower(trim(ideType));
	for(size_t i = 0; i < sizeof(VEH_TYPES) / sizeof(VEH_TYPES[0]); i++)
		if(t == VEH_TYPES[i].ide) return &VEH_TYPES[i];
	return nullptr;
}

static const VehDesc *findDesc(const VehType &vt, const std::string &lowerName)
{
	for(int i = 0; i < vt.n; i++) if(lowerName == vt.tab[i].name) return &vt.tab[i];
	return nullptr;
}

// Blender's duplicate suffix ".001" (mirrors _DUP_SUFFIX in INU_tools/ops/frame_hierarchy.py):
// returns the name without it, or "" when there is no such suffix.
static std::string stripBlenderSuffix(const std::string &lowerName)
{
	size_t n = lowerName.size();
	if(n < 5 || lowerName[n - 4] != '.') return "";
	for(size_t i = n - 3; i < n; i++) if(lowerName[i] < '0' || lowerName[i] > '9') return "";
	return lowerName.substr(0, n - 4);
}

struct NameIndex {
	std::map<std::string, std::vector<int>> byLower;	// lower name → frame indices (file order)
	std::vector<std::string> lowerNames;			// per frame
	bool has(const char *n) const { return byLower.count(n) != 0; }
	// a frame whose name is `n` + ".NNN"
	std::string nearMatch(const std::string &n) const {
		for(auto it = byLower.begin(); it != byLower.end(); ++it)
			if(stripBlenderSuffix(it->first) == n) return it->first;
		return "";
	}
};

// -------------------------------------------------------------- rules ----

void CheckVehicleDff(Context &ctx, const std::string &where, const ObjDef &def, const VehDff &v)
{
	auto add = [&](Severity s, const char *code, const std::string &msg, const std::string &detail = "") {
		ctx.add(s, CAT_VEH, code, where, -1, def.name, msg, detail, def.id);
	};

	const VehType *vt = findType(def.carType);
	if(!vt){
		// DAT-13 already reported the unknown type; the engine keeps the default (car)
		vt = &VEH_TYPES[0];
	}

	NameIndex ix;
	ix.lowerNames.reserve(v.frameNames.size());
	for(size_t i = 0; i < v.frameNames.size(); i++){
		std::string l = lower(v.frameNames[i]);
		ix.lowerNames.push_back(l);
		if(!l.empty()) ix.byLower[l].push_back((int)i);
	}

	std::string typeShown = fmt("%s (vehicleType %d)", vt->ide, vt->vehicleType);

	// VEH-01 / VEH-02 / VEH-09 — wheel dummies -------------------------------------------
	if(vt->wheels == WS_CAR){
		for(int w = 0; w < 4; w++){
			if(ix.has(CAR_WHEELS[w])) continue;
			const VehDesc *d = findDesc(*vt, CAR_WHEELS[w]);
			std::string near = ix.nearMatch(CAR_WHEELS[w]);
			std::string msg = fmt("нет фрейма «%s» (%s колесо, hierarchy id %d) — CVehicleModelInfo::GetWheelPosn 0x4C7D20 разыменует NULL при спавне → краш",
			                      CAR_WHEELS[w], CAR_WHEEL_DESC[w], d ? d->id : 0);
			if(!near.empty())
				msg += fmt("; есть «%s» — суффикс Blender ушёл в DFF, _stricmp его не сопоставит", near.c_str());
			add(SEV_FATAL, "VEH-01", msg,
			    fmt("Тип %s из vehicles.ide использует таблицу CAutomobile: SetFrameIds 0x4C5460 ищет все четыре wheel_*_dummy по имени (регистр не важен, имя — точно), "
			        "ms_wheelFrameIDs {5,7,2,4} → GetWheelPosn без проверки NULL (0x4C7D57 / 0x4C7DAD). Переименуй/добавь dummy; wheel_lm/rm_dummy колёса не заменяют.", typeShown.c_str()));
		}
	}else if(vt->wheels == WS_BIKE){
		for(int w = 0; w < 2; w++){
			if(ix.has(BIKE_WHEELS[w])) continue;
			const VehDesc *d = findDesc(*vt, BIKE_WHEELS[w]);
			std::string near = ix.nearMatch(BIKE_WHEELS[w]);
			std::string msg = fmt("нет фрейма «%s» (%s колесо байка, hierarchy id %d) — CBike берёт матрицу этого узла без проверки NULL → краш при спавне",
			                      BIKE_WHEELS[w], BIKE_WHEEL_DESC[w], d ? d->id : 0);
			if(!near.empty())
				msg += fmt("; есть «%s» — суффикс Blender ушёл в DFF, _stricmp его не сопоставит", near.c_str());
			add(SEV_FATAL, "VEH-02", msg,
			    fmt("Тип %s использует таблицу BIKE/BMX (0x8A7270 / 0x8A73D0): колёса называются wheel_front / wheel_rear, а не wheel_lf_dummy и т.п.", typeShown.c_str()));
		}
	}else if(vt->wheels == WS_TRAIN){
		add(SEV_INFO, "VEH-09", "поезд (vehicleType 6): собственная схема id колёс (wheel_rf1_dummy … wheel_lb3_dummy, bogie_front/rear) — правила VEH-01/02 не применяются",
		    "Таблица TRAIN @ 0x8A7068; GetWheelPosn для CTrain не вызывается.");
	}

	// VEH-03 — chassis_dummy -----------------------------------------------------------------
	if(vt->wantChassisDummy && !ix.has("chassis_dummy")){
		std::string near = ix.nearMatch("chassis_dummy");
		std::string msg = "нет фрейма «chassis_dummy» — корневой dummy машины, под которым лежат chassis и все *_dummy";
		if(!near.empty()) msg += fmt("; есть «%s» (суффикс Blender)", near.c_str());
		const VehDesc *ch = findDesc(*vt, "chassis");
		if(ch && !ix.has("chassis"))
			msg += "; нет и фрейма «chassis» (hierarchy id 1, CAR_CHASSIS) — кузов не получит id, всё, что движок берёт через GetFrameFromId(1), отвалится";
		add(SEV_ERROR, "VEH-03", msg,
		    fmt("У каждой ванильной модели типа %s иерархия: <model> → chassis_dummy → chassis + dummy-фреймы. Без chassis_dummy экспортёр обычно "
		        "выдаёт плоскую иерархию, и dummy оказываются не там, где их ждёт PreprocessHierarchy.", vt->ide));
	}

	// VEH-04 — duplicate frame names ---------------------------------------------------------
	for(auto it = ix.byLower.begin(); it != ix.byLower.end(); ++it){
		if(it->second.size() < 2) continue;
		const VehDesc *d = findDesc(*vt, it->first);
		std::string idxs;
		for(size_t k = 0; k < it->second.size(); k++) idxs += (k ? ", " : "") + fmt("%d", it->second[k]);
		if(d){
			bool byId = !(d->flags & VF_STRUCT_PART);
			add(SEV_WARN, "VEH-04",
			    fmt("имя фрейма «%s» повторяется %d раза (фреймы %s) — %s", v.frameNames[(size_t)it->second[0]].c_str(), (int)it->second.size(), idxs.c_str(),
			        byId ? fmt("SetFrameIds 0x4C5460 ставит hierarchy id %d только первому (id ещё 0), остальные копии движок не видит", d->id).c_str()
			             : "dummy ищется по имени один раз (первое совпадение), остальные копии игнорируются"),
			    "Оставь один фрейм с этим именем; лишние переименуй (misc_*/extra*) или объедини.");
		}else{
			add(SEV_INFO, "VEH-04", fmt("имя фрейма «%s» повторяется %d раза (фреймы %s) — имени нет в таблице типа %s, движок его не ищет",
			                            v.frameNames[(size_t)it->second[0]].c_str(), (int)it->second.size(), idxs.c_str(), vt->ide));
		}
	}

	// VEH-05 — table name with a Blender suffix ---------------------------------------------
	for(auto it = ix.byLower.begin(); it != ix.byLower.end(); ++it){
		std::string base = stripBlenderSuffix(it->first);
		if(base.empty()) continue;
		const VehDesc *d = findDesc(*vt, base);
		if(!d) continue;
		bool exactAlso = ix.has(base.c_str());
		add(SEV_WARN, "VEH-05",
		    fmt("фрейм «%s»: имя из таблицы %s с суффиксом Blender — _stricmp сравнивает точно, движок считает его безымянным%s",
		        v.frameNames[(size_t)it->second[0]].c_str(), vt->ide,
		        exactAlso ? fmt(" (есть и правильный «%s» — это дубль)", base.c_str()).c_str() : ""),
		    "Убери «.001» перед экспортом (в Blender это признак дубликата объекта).");
	}

	// VEH-06 — table coverage summary --------------------------------------------------------
	{
		int present = 0, total = 0;
		std::string absent;
		int absentN = 0;
		for(int i = 0; i < vt->n; i++){
			const VehDesc &d = vt->tab[i];
			if(d.flags & VF_UPGRADE) continue;	// ug_* live in separate tuning DFFs, never in the vehicle
			total++;
			if(ix.has(d.name)) present++;
			else{ if(absentN < 12) absent += (absentN ? ", " : "") + std::string(d.name); absentN++; }
		}
		if(absentN > 12) absent += fmt(", … (+%d)", absentN - 12);
		int unknown = 0;
		for(auto it = ix.byLower.begin(); it != ix.byLower.end(); ++it) if(!findDesc(*vt, it->first)) unknown++;
		add(SEV_INFO, "VEH-06", fmt("таблица %s: %d из %d имён есть; нет: %s; фреймов вне таблицы: %d", vt->ide, present, total, absentN ? absent.c_str() : "—", unknown),
		    "Отсутствующие необязательные имена (misc_*, extra*, двери, ped_*) просто не дают соответствующей детали/позиции.");
	}

	// VEH-07 / VEH-12 / VEH-12b — embedded collision ----------------------------------------
	// VEH-08 (>50 vehicle models loaded at once) needs runtime streaming state — not checkable offline, skipped.
	// The COL pass runs before the DFF pass, so def.colEntry already says whether a .col entry named
	// after the model exists (the fallback the engine takes when the plugin is absent).
	if(!v.hasCollision){
		if(def.colEntry < 0)
			add(SEV_FATAL, "VEH-07", "нет плагина Collision (0x253F2FA) в EXTENSION клампа и ни один .col не содержит записи с именем модели — CEntity::GetColModel 0x535300 вернёт modelinfo+0x14 без проверки, CAutomobile::SetupSuspensionLines 0x6A65EF / CBike 0x6B89C3 разыменуют NULL → краш при спавне",
			    "Экспортируй машину со встроенной COL3-коллизией (INU Tools / DragonFF: галочка коллизии в экспорте машины) или положи запись с именем модели в .col.");
		else
			add(SEV_WARN, "VEH-07", "нет плагина Collision (0x253F2FA) в EXTENSION клампа — коллизия берётся из .col-записи с именем модели (CollisionPlugin::ReadCB 0x41B1D0 не вызывается)",
			    "Для машин ванильный путь — встроенная COL3 в DFF; внешняя запись работает, но стримится отдельно.");
	}else{
		bool validFourcc = v.colSize >= 4 && (v.colFourcc == 0x4C4C4F43u /* COLL */ || v.colFourcc == 0x324C4F43u /* COL2 */ || v.colFourcc == 0x334C4F43u /* COL3 */);
		if(v.colSize > 16384)
			add(SEV_FATAL, "VEH-12", fmt("тело плагина Collision %u байт > 16384 — CollisionPlugin::ReadCB 0x41B1FB читает его RwStreamRead в PC_Scratch (0xC8E0C8, 16 КБ) без ограничения → переполнение bss (gamma и далее) → краш", (unsigned)v.colSize),
			    "Упрости коллизию машины: меньше граней/сфер/боксов, без shadow-меша, чтобы запись COL3 была короче 16 КБ.");
		if(!validFourcc)
			add(SEV_WARN, "VEH-07", fmt("плагин Collision начинается не с COLL/COL2/COL3 (первые байты %s, %u байт) — CollisionPlugin::ReadCB 0x41B1D0 передаёт запись парсеру как есть → мусорная коллизия",
			                            v.colSize >= 4 ? fmt("%02X %02X %02X %02X", v.colFourcc & 0xFF, (v.colFourcc >> 8) & 0xFF, (v.colFourcc >> 16) & 0xFF, (v.colFourcc >> 24) & 0xFF).c_str() : "нет", (unsigned)v.colSize),
			    "Так устроен ванильный rccam.dff (старый блоб III/VC, 160 байт). Пересохрани с COL3-записью.");
		else if(v.colSize < 0x20 || (size_t)v.colSizeField + 8 != v.colSize)
			add(SEV_ERROR, "VEH-12b", fmt("плагин Collision: поле size = %u, а тело %u байт (ожидается size + 8 = тело, тело ≥ 32) — LoadCollisionModelVer2/3 читают size−0x18 байт из scratch-буфера: за концом записи лежат устаревшие байты предыдущей коллизии",
			                              (unsigned)v.colSizeField, (unsigned)v.colSize),
			    "Поле size записи COL должно быть равно длине тела минус 8 (fourcc + size).");
	}

	// VEH-10 / VEH-11 / VEH-11b — extras and dummies vs atomics -------------------------------
	int numExtras = 0;
	std::vector<std::string> dummyNoChild;
	for(size_t i = 0; i < ix.lowerNames.size(); i++){
		const std::string &ln = ix.lowerNames[i];
		if(ln.empty()) continue;
		const VehDesc *d = findDesc(*vt, ln);
		if(!d) continue;
		bool hasAtomic = i < v.frameHasAtomic.size() && v.frameHasAtomic[i];
		if(d->flags & VF_EXTRA){
			// only the first frame with the name is taken (VEH-04)
			if(ix.byLower[ln][0] != (int)i) continue;
			numExtras++;
			if(!hasAtomic)
				add(SEV_FATAL, "VEH-10", fmt("фрейм «%s» не владеет атомиком — PreprocessHierarchy 0x4C8E60 (0x4C8FF9/0x4C9007) вызывает RpClumpRemoveAtomic 0x74A4C0 для атомика этого фрейма: mov ecx,[eax+0x44] без NULL-теста → краш при загрузке модели", v.frameNames[i].c_str()),
				    "Каждый extra1..extra6 должен быть узлом с геометрией (атомиком), а не пустым dummy. Пустой extra — удали или дай ему меш.");
		}else if(d->flags & VF_DUMMY){
			if(ix.byLower[ln][0] != (int)i) continue;
			if(hasAtomic)
				add(SEV_FATAL, "VEH-11", fmt("dummy-фрейм «%s» владеет атомиком — PreprocessHierarchy копирует позицию (0x4C8F24) и уничтожает фрейм RwFrameDestroy 0x4C8F6E → RwFreeListFree; атомик остаётся с указателем на освобождённый фрейм = use-after-free (сначала деталь съезжает, потом краш)", v.frameNames[i].c_str()),
				    "Позиционные dummy (ped_*seat, headlights*, taillights*, exhaust, engine, petrolcap, hookup, ped_arm, miscpos_*, bargrip …) должны быть пустыми узлами без геометрии.");
			// children?
			for(size_t k = 0; k < v.frameParent.size(); k++)
				if(v.frameParent[k] == (int)i){ dummyNoChild.push_back(v.frameNames[i]); break; }
		}
	}
	for(size_t k = 0; k < dummyNoChild.size(); k++)
		add(SEV_WARN, "VEH-11b", fmt("у dummy-фрейма «%s» есть дочерние фреймы — RwFrameDestroy (RW 3.7 _rwFrameInternalDeInit) ставит детям parent = NULL: они осиротеют и перестанут двигаться с машиной", dummyNoChild[k].c_str()),
		    "Позиционные dummy — листья иерархии; вложенные узлы перенеси под chassis.");

	// VEH-15 / VEH-15b — compRules vs extras present ------------------------------------------
	if(numExtras > 0 && def.tok.size() > 10){
		const std::string &ct = def.tok[10];
		if(IsHexToken(ct)){
			uint32_t rules = (uint32_t)strtoul(ct.c_str(), nullptr, 16);
			for(int r = 0; r < 2; r++){
				uint32_t half = (rules >> (16 * r)) & 0xFFFF;
				uint32_t rule = (half >> 12) & 0xF;
				if(rule == 0 || rule == 4) continue;	// none / FULL_RANDOM ignores the nibbles
				if(rule > 4) continue;			// unknown rule — not handled by ChooseComponent's switch
				int nonF = 0;
				std::string badHigh, badMissing;
				for(int n = 0; n < 3; n++){
					uint32_t c = (half >> (4 * n)) & 0xF;
					if(c == 0xF) continue;
					nonF++;
					if(c >= 6) badHigh += (badHigh.empty() ? "" : ", ") + fmt("%u", c);
					else if((int)c >= numExtras) badMissing += (badMissing.empty() ? "" : ", ") + fmt("%u", c);
				}
				const char *which = r == 0 ? "A" : "B";
				if(nonF == 0)
					add(SEV_FATAL, "VEH-15", fmt("compRules %s: правило %s = %u при всех трёх comp-нибблах F — ChooseComponent 0x4C7FB0: CountCompsInRule = 0 → GetRandomNumberInRange(0,0) = 0 → возвращает 0xF; CreateInstance 0x4C96E2 читает m_apExtras[15] (за концом CVehicleStructure 0x314) → RpAtomicClone(мусор) → краш при спавне", ct.c_str(), which, rule),
					    "Впиши индексы имеющихся extras (0..5 в порядке extra1..extra6) в нибблы правила или используй правило 4 (полный рандом).");
				else if(!badHigh.empty())
					add(SEV_FATAL, "VEH-15", fmt("compRules %s: правило %s содержит компонент(ы) %s ≥ 6 — m_apExtras всего 6, CreateInstance 0x4C96E2 читает m_nNumExtras/маску/соседний слот пула как указатель → RpAtomicClone(мусор) → краш при спавне", ct.c_str(), which, badHigh.c_str()),
					    "Индексы компонентов — 0..5 (порядок extra1..extra6 среди реально присутствующих).");
				if(!badMissing.empty())
					add(SEV_WARN, "VEH-15b", fmt("compRules %s: правило %s ссылается на компонент(ы) %s, а в DFF только %d extra — m_apExtras[n] = NULL, компонент молча пропускается", ct.c_str(), which, badMissing.c_str(), numExtras));
			}
		}
	}

	// VEH-21 — universal dummies of the type (verified over the 212 vanilla DFFs) -------------
	{
		static const char *U_CAR[]     = { "chassis", "headlights", "ped_frontseat", nullptr };
		static const char *U_BIKE[]    = { "chassis", "chassis_vlo", "engine", "exhaust", "forks_front", "forks_rear", "handlebars", "headlights", "mudguard", "ped_frontseat", "taillights", nullptr };
		static const char *U_BMX[]     = { "bargrip", "chainset", "chassis", "chassis_vlo", "forks_front", "handlebars", "headlights", "ped_frontseat", "pedal_l", "pedal_r", "taillights", nullptr };
		static const char *U_PLANE[]   = { "chassis", "ped_frontseat", "rudder", nullptr };
		static const char *U_BOAT[]    = { "ped_frontseat", nullptr };
		static const char *U_TRAIN[]   = { "bogie_front", "bogie_rear", "chassis", "chassis_dummy", "chassis_vlo", "headlights", "taillights",
		                                   "wheel_rf1_dummy", "wheel_rf2_dummy", "wheel_rb1_dummy", "wheel_rb2_dummy", "wheel_lf1_dummy", "wheel_lf2_dummy", "wheel_lb1_dummy", "wheel_lb2_dummy", nullptr };
		static const char *U_HELI[]    = { "chassis", "chassis_vlo", "moving_rotor", "static_rotor", "ped_frontseat", nullptr };
		static const char *U_TRAILER[] = { "chassis", "chassis_vlo", "hookup", nullptr };
		// wheels / chassis_dummy are already covered by VEH-01/02/03 at a higher level and are left out here
		const char **u = nullptr;
		switch(vt->vehicleType){
		case 0: u = U_CAR; break; case 9: u = U_BIKE; break; case 10: u = U_BMX; break; case 4: u = U_PLANE; break;
		case 5: u = U_BOAT; break; case 6: u = U_TRAIN; break; case 3: u = U_HELI; break; case 11: u = U_TRAILER; break;
		default: break;
		}
		if(u){
			std::string missing; int nm = 0;
			for(int i = 0; u[i]; i++) if(!ix.has(u[i])){ missing += (nm ? ", " : "") + std::string(u[i]); nm++; }
			if(nm)
				add(SEV_WARN, "VEH-21", fmt("нет фреймов, которые есть у всех ванильных моделей типа %s: %s — PreprocessHierarchy оставит m_avDummyPos = (0,0,0): педы садятся / фары и выхлоп рисуются в начале координат модели", vt->ide, missing.c_str()),
				    "Добавь недостающие dummy (пустые узлы) в нужных точках; chassis / chassis_vlo — узлы с геометрией.");
		}
	}
}

} // namespace gc
