// SoftEther VPN Source Code - Developer Edition Master Branch
// Cedar Communication Module


// VLan.h
// Header of VLan.c

#ifndef	VLAN_H
#define	VLAN_H

#include "Mayaqua/MayaType.h"

// Parameters related to VLAN
struct VLAN_PARAM
{
	UCHAR MacAddress[6];
	UCHAR Padding[2];
};

#endif

// Routing table tracking timer
#define	TRACKING_INTERVAL_INITIAL		444		// Initial
#define	TRACKING_INTERVAL_ADD			444		// Adding value
#define	TRACKING_INTERVAL_MAX			12345	// Maximum value
#define	TRACKING_INTERVAL_MAX_RC		87654	// Maximum value (OS which change detection mechanism enabled)